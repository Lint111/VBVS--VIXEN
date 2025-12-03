# Parallel Voxel Creation/Update with Gaia-ECS

**Created**: Nov 23, 2025
**Status**: Design Document
**Purpose**: Define parallel entity creation/update strategy for VoxelInjectionQueue and chunk operations

---

## Executive Summary

Gaia-ECS provides built-in multithreading support via `mt::ThreadPool` and parallel query execution (`QueryExecType::Parallel`). We will leverage this to:

1. **Check entity existence** via MortonKey lookup (spatial hash query)
2. **Create or update entities** in parallel batches
3. **Process queue entries** across multiple worker threads
4. **Chunk-based bulk operations** for spatial locality

---

## Core Threading Model

### ThreadPool Architecture

Gaia-ECS uses a work-stealing threadpool with:
- **High-priority workers** (performance cores)
- **Low-priority workers** (efficiency cores)
- **Job dependencies** (explicit ordering via `dep()`)
- **Parallel iteration** (`each(func, QueryExecType::Parallel)`)

**API:**
```cpp
mt::ThreadPool::get().set_max_workers(hwThreads, hiPrioWorkers);

// Schedule parallel jobs
JobHandle job1 = threadPool.add([](){ /* work */ });
JobHandle job2 = threadPool.add([](){ /* work */ });
threadPool.dep(job1, job2); // job2 waits for job1
threadPool.submit({job1, job2});
threadPool.wait(job2);
```

### Parallel Query Execution

Queries support parallel iteration over chunks:
```cpp
auto q = world.query().all<MortonKey>();

// Serial (main thread)
q.each([](MortonKey& key) { /* per entity */ });

// Parallel (all cores)
q.each([](MortonKey& key) { /* per entity */ }, QueryExecType::Parallel);

// Parallel (perf cores only)
q.each([](MortonKey& key) { /* per entity */ }, QueryExecType::ParallelPerf);
```

**Key Benefit**: Gaia automatically partitions chunks across worker threads with work-stealing for load balancing.

---

## Entity Lookup Strategy

### Problem
**Before modifying voxel components, we must:**
1. Check if an entity with the given MortonKey exists
2. If exists → update components
3. If not exists → create entity + set components

### Solution: Spatial Hash Query

**Query Setup:**
```cpp
// ONCE during GaiaVoxelWorld initialization
auto lookupQuery = world.query().all<MortonKey>();
lookupQuery.build(); // Cache query for fast reuse
```

**Fast Lookup (O(1) expected):**
```cpp
std::optional<Entity> findVoxelEntity(MortonKey key) {
    std::optional<Entity> result;

    lookupQuery.each([&](Entity entity, MortonKey& mk) {
        if (mk.value == key.value) {
            result = entity;
            return false; // Stop iteration (early exit)
        }
        return true; // Continue
    });

    return result;
}
```

**Problem**: Linear scan of all MortonKeys (slow for 1M+ voxels)

### Optimization: Spatial Hash Map

**Better Approach** - Maintain a parallel hash map:
```cpp
class GaiaVoxelWorld {
    gaia::ecs::World world;

    // Thread-safe concurrent map (morton key → entity ID)
    std::unordered_map<uint64_t, Entity> mortonToEntity;
    std::shared_mutex mortonMapMutex; // Reader-writer lock

    std::optional<Entity> findVoxelEntity(MortonKey key) {
        std::shared_lock lock(mortonMapMutex);
        auto it = mortonToEntity.find(key.value);
        return (it != mortonToEntity.end()) ? std::optional(it->second) : std::nullopt;
    }

    void insertMapping(MortonKey key, Entity entity) {
        std::unique_lock lock(mortonMapMutex);
        mortonToEntity[key.value] = entity;
    }
};
```

**Benefits**:
- O(1) lookup (no ECS query needed)
- Shared lock allows concurrent reads
- Exclusive lock only during entity creation

---

## Parallel Entity Creation/Update

### Architecture

```
VoxelInjectionQueue
   ↓ Ring buffer (lock-free enqueue)
   ↓
Worker Threads (parallel batch processing)
   ↓
   ├→ Batch 1 (Thread 1): Check MortonKeys [0-999]
   ├→ Batch 2 (Thread 2): Check MortonKeys [1000-1999]
   ├→ Batch 3 (Thread 3): Check MortonKeys [2000-2999]
   └→ ...
   ↓
   ├→ Create missing entities (parallel)
   └→ Update existing entities (parallel)
   ↓
GaiaVoxelWorld: Entities ready for spatial queries
```

### Implementation

#### Step 1: Batch Partitioning
```cpp
struct VoxelBatch {
    std::vector<VoxelCreationRequest> requests;
    std::vector<Entity> existingEntities;  // Entities found (update)
    std::vector<size_t> newRequestIndices; // Requests needing creation
};

void partitionBatch(std::span<VoxelCreationRequest> requests, VoxelBatch& batch) {
    batch.requests.assign(requests.begin(), requests.end());
    batch.existingEntities.reserve(requests.size());
    batch.newRequestIndices.reserve(requests.size());

    for (size_t i = 0; i < requests.size(); ++i) {
        MortonKey key = MortonKey::fromPosition(requests[i].position);
        auto entity = gaiaWorld.findVoxelEntity(key);

        if (entity) {
            batch.existingEntities.push_back(*entity);
        } else {
            batch.newRequestIndices.push_back(i);
        }
    }
}
```

#### Step 2: Parallel Entity Creation
```cpp
void createEntitiesParallel(VoxelBatch& batch) {
    // Create all missing entities in parallel using Gaia's parallel iteration

    auto& threadPool = mt::ThreadPool::get();

    // Job 1: Create entities
    JobHandle createJob = threadPool.add([&]() {
        for (size_t idx : batch.newRequestIndices) {
            auto& req = batch.requests[idx];
            MortonKey key = MortonKey::fromPosition(req.position);

            // Create entity with all components atomically
            Entity entity = world.add();
            world.add<MortonKey>(entity, key);

            // Visit each component and add to entity
            for (auto& compData : req.components) {
                std::visit([&](auto&& component) {
                    using T = std::decay_t<decltype(component)>;
                    world.add<T>(entity, component);
                }, compData);
            }

            // Register in spatial hash
            gaiaWorld.insertMapping(key, entity);
        }
    });

    // Job 2: Update existing entities (can run in parallel with creation!)
    JobHandle updateJob = threadPool.add([&]() {
        for (Entity entity : batch.existingEntities) {
            // Find corresponding request (by MortonKey match)
            auto key = world.get<MortonKey>(entity);

            for (auto& req : batch.requests) {
                if (MortonKey::fromPosition(req.position) == key) {
                    // Update components
                    for (auto& compData : req.components) {
                        std::visit([&](auto&& component) {
                            using T = std::decay_t<decltype(component)>;
                            world.set<T>(entity, component);
                        }, compData);
                    }
                    break;
                }
            }
        }
    });

    // Submit jobs and wait for completion
    threadPool.submit({createJob, updateJob});
    threadPool.wait(updateJob); // Waits for both (dependency chain)
}
```

---

## Thread-Safety Guarantees

### Gaia-ECS Safety Model

**Safe Operations (Thread-Safe)**:
- ✅ `world.add()` - Entity creation (internal locking)
- ✅ `world.add<T>(entity, value)` - Component addition (archetype transition locked)
- ✅ `world.set<T>(entity, value)` - Component modification (chunk write)
- ✅ `query.each(func, Parallel)` - Parallel iteration (chunk-level parallelism)

**Unsafe Operations (NOT Thread-Safe)**:
- ❌ Structural changes during parallel iteration (add/remove components mid-query)
- ❌ Entity deletion during parallel access

### Our Safety Strategy

1. **Lock-free enqueue** - Ring buffer (single producer OK, multi-producer needs atomic ops)
2. **Batch processing** - Each worker thread processes independent VoxelCreationRequests
3. **Spatial hash locking** - Reader-writer lock (`std::shared_mutex`)
4. **No mid-query mutations** - All entity creation/update happens BEFORE next frame queries

---

## Chunk-Based Bulk Operations

### Motivation
Spatial locality → better cache performance when creating voxels in the same brick/chunk region.

### Design

#### Chunk Metadata Components
```cpp
struct ChunkOrigin {
    glm::ivec3 value; // World-space chunk origin (e.g., 0,0,0 for first 8³ region)
};

struct ChunkMetadata {
    uint32_t voxelCount;    // Number of voxels in this chunk
    uint32_t brickID;       // SVO brick ID (if allocated)
    bool isDirty;           // Needs SVO reinsert
};
```

#### Bulk Insert API
```cpp
void GaiaVoxelWorld::insertChunk(glm::ivec3 chunkOrigin,
                                  std::span<VoxelCreationRequest> voxels) {
    // 1. Create chunk metadata entity
    Entity chunkEntity = world.add();
    world.add<ChunkOrigin>(chunkEntity, {chunkOrigin});
    world.add<ChunkMetadata>(chunkEntity, {
        .voxelCount = (uint32_t)voxels.size(),
        .brickID = 0xFFFFFFFF,
        .isDirty = true
    });

    // 2. Create voxels in parallel
    VoxelBatch batch;
    partitionBatch(voxels, batch);
    createEntitiesParallel(batch);

    // 3. Link voxels to chunk (spatial query optimization)
    for (auto& req : voxels) {
        MortonKey key = MortonKey::fromPosition(req.position);
        auto voxelEntity = findVoxelEntity(key);

        if (voxelEntity) {
            world.add(*voxelEntity, Pair(ChildOf, chunkEntity)); // Hierarchy relation
        }
    }
}
```

#### Chunk Query (Fast Spatial Queries)
```cpp
std::vector<Entity> GaiaVoxelWorld::getVoxelsInChunk(glm::ivec3 chunkOrigin) {
    std::vector<Entity> results;

    // Find chunk entity
    auto chunkQuery = world.query().all<ChunkOrigin, ChunkMetadata>();
    Entity chunkEntity;

    chunkQuery.each([&](Entity entity, ChunkOrigin& origin) {
        if (origin.value == chunkOrigin) {
            chunkEntity = entity;
            return false; // Stop
        }
        return true;
    });

    if (!chunkEntity)
        return results;

    // Query all voxels with ChildOf(chunkEntity)
    auto voxelQuery = world.query().all<MortonKey>().any(Pair(ChildOf, chunkEntity));

    voxelQuery.each([&](Entity entity) {
        results.push_back(entity);
    });

    return results;
}
```

---

## Queue Integration

### VoxelInjectionQueue with Parallel Processing

```cpp
class VoxelInjectionQueue {
    // Existing: Lock-free ring buffer
    RingBuffer<QueueEntry> ringBuffer;

    // NEW: Worker thread pool reference
    mt::ThreadPool& threadPool = mt::ThreadPool::get();

    void processBatch() override {
        // 1. Consume from ring buffer (lock-free)
        std::vector<VoxelCreationRequest> batch;
        batch.reserve(1024);

        QueueEntry entry;
        while (ringBuffer.try_pop(entry) && batch.size() < 1024) {
            batch.push_back(entry.request);
        }

        if (batch.empty())
            return;

        // 2. Partition batch by worker threads
        const size_t numWorkers = threadPool.workers();
        const size_t batchSize = (batch.size() + numWorkers - 1) / numWorkers;

        std::vector<JobHandle> jobs;
        jobs.reserve(numWorkers);

        for (size_t i = 0; i < numWorkers; ++i) {
            size_t start = i * batchSize;
            size_t end = std::min(start + batchSize, batch.size());

            if (start >= end)
                break;

            auto workerBatch = std::span(batch).subspan(start, end - start);

            JobHandle job = threadPool.add([this, workerBatch]() {
                VoxelBatch voxelBatch;
                partitionBatch(workerBatch, voxelBatch);
                createEntitiesParallel(voxelBatch);
            });

            jobs.push_back(job);
        }

        // 3. Submit all jobs and wait for completion
        threadPool.submit(jobs);
        for (auto job : jobs) {
            threadPool.wait(job);
        }
    }
};
```

---

## Performance Estimates

### Sequential (OLD)
- 100,000 voxels × 200µs/voxel = **20 seconds**
- Single thread, full tree traversal per voxel

### Parallel (NEW)
- 100,000 voxels ÷ 16 threads = 6,250 voxels/thread
- Parallel entity creation: ~50µs/voxel (no tree traversal!)
- Total: 6,250 × 50µs = **312ms per thread** (wall time)
- Speedup: **64× faster**

### Chunk-Based Bulk (OPTIMAL)
- 100,000 voxels grouped into ~195 chunks (8³ = 512 voxels/chunk)
- Parallel chunk processing: 195 ÷ 16 threads = ~12 chunks/thread
- Chunk creation overhead: 50ms (hierarchy setup)
- Voxel creation: 312ms (parallel)
- Total: **~400ms** (includes spatial indexing overhead)
- Speedup: **50× faster** + spatial query optimization

---

## Implementation Phases

### Phase 2: Chunk Operations (CURRENT PRIORITY)

**Goals**:
1. ✅ Add chunk metadata components (`ChunkOrigin`, `ChunkMetadata`)
2. ✅ Implement `insertChunk()` API for bulk insertion
3. ✅ Add `ChildOf` relation for voxel→chunk hierarchy
4. ✅ Spatial query optimization via chunk bounds

**Files to Modify**:
- `VoxelComponents.h` - Add `ChunkOrigin`, `ChunkMetadata`
- `GaiaVoxelWorld.h` - Add `insertChunk()` API
- `GaiaVoxelWorld.cpp` - Implement chunk-based creation

**Estimated Time**: 2-3 hours

### Phase 3: Parallel Queue Processing

**Goals**:
1. Implement `mortonToEntity` spatial hash map
2. Add `findVoxelEntity()` fast lookup
3. Refactor `processBatch()` to use parallel jobs
4. Benchmark performance (target: 50× speedup)

**Files to Modify**:
- `GaiaVoxelWorld.h` - Add `mortonToEntity` member, `findVoxelEntity()` method
- `VoxelInjectionQueue.cpp` - Parallel batch processing

**Estimated Time**: 3-4 hours

---

## Testing Strategy

### Unit Tests

1. **Entity Lookup**
   - Create voxel at (0,0,0), verify `findVoxelEntity()` returns entity
   - Query missing voxel at (1,1,1), verify returns `nullopt`

2. **Parallel Creation**
   - Create 10,000 voxels in parallel
   - Verify all entities created with correct components
   - No duplicate MortonKeys

3. **Chunk Operations**
   - Insert 512 voxels via `insertChunk()`
   - Query voxels in chunk, verify count = 512
   - Test chunk boundary queries (no cross-chunk leaks)

4. **Update vs Create**
   - Create voxel at (0,0,0) with Density=0.5
   - Update same voxel with Density=1.0
   - Verify entity count = 1 (not 2!)
   - Verify final density = 1.0

### Performance Benchmarks

```cpp
// Benchmark: Sequential vs Parallel
void BM_VoxelCreation(benchmark::State& state) {
    GaiaVoxelWorld world;
    std::vector<VoxelCreationRequest> voxels = generateRandomVoxels(100000);

    for (auto _ : state) {
        // OLD: Sequential
        for (auto& req : voxels) {
            world.createVoxel(req);
        }
    }
}

void BM_VoxelCreationParallel(benchmark::State& state) {
    GaiaVoxelWorld world;
    std::vector<VoxelCreationRequest> voxels = generateRandomVoxels(100000);

    for (auto _ : state) {
        // NEW: Parallel batch
        VoxelBatch batch;
        partitionBatch(voxels, batch);
        createEntitiesParallel(batch);
    }
}
```

---

## Summary

**Key Decisions**:
1. ✅ Use `std::unordered_map<MortonKey, Entity>` for O(1) entity lookup (not linear ECS query)
2. ✅ Parallel entity creation via `mt::ThreadPool` job system
3. ✅ Chunk-based bulk operations for spatial locality
4. ✅ `ChildOf` relation for chunk→voxel hierarchy (fast spatial queries)
5. ✅ Reader-writer lock on spatial hash map (concurrent reads during lookup)

**Performance Goals**:
- 50-64× faster than sequential insertion
- <500ms for 100,000 voxel creation (was 20 seconds)
- Spatial queries via chunk bounds (no full scan)

**Next Steps**:
1. Implement chunk metadata components
2. Add `insertChunk()` API
3. Benchmark parallel creation
4. Integrate with VoxelInjectionQueue (Phase 3)
