# Async Layer Architecture - VoxelInjectionQueue in GaiaVoxelWorld

**Core Principle**: VoxelInjectionQueue is the async interface to GaiaVoxelWorld, not to SVO.

---

## Current Architecture (Problem)

```
Application
  ↓
VoxelInjectionQueue (in SVO library)
  ↓
VoxelInjector (in SVO library)
  ↓
Creates descriptors + inserts into SVO
```

**Issues**:
1. Queue tightly coupled to SVO (wrong layer)
2. Queue should interact with data layer (GaiaVoxelWorld), not spatial index
3. SVO insertion is implementation detail, not queue's concern

---

## New Architecture (Solution)

```
Application
  ↓
VoxelInjectionQueue (in GaiaVoxelWorld - async data layer)
  ↓
GaiaVoxelWorld::createVoxelsBatch() - Creates entities
  ↓
VoxelInjector (in GaiaVoxelWorld - sync insertion helper)
  ↓
LaineKarrasOctree::insert() - Indexes entities (optional!)
```

**Key Insight**: Queue creates entities first, SVO indexing is separate optional step.

---

## Component Responsibilities

### VoxelInjectionQueue (Async Data Layer)
**New Location**: `libraries/GaiaVoxelWorld/include/VoxelInjectionQueue.h`

**Owns**:
- Lock-free ring buffer for batch requests
- Worker threads for parallel entity creation
- Async entity creation pipeline

**Does NOT Own**:
- SVO insertion (that's VoxelInjector's job)
- Spatial indexing logic

**API**:
```cpp
class VoxelInjectionQueue {
public:
    VoxelInjectionQueue(GaiaVoxelWorld& world, size_t capacity = 65536);

    // Enqueue voxel creation request (lock-free, non-blocking)
    void enqueue(const glm::vec3& position, const VoxelCreationRequest& request);

    // Start async worker threads
    void start(size_t numThreads = 1);

    // Stop worker threads (completes pending work)
    void stop();

    // Get created entities (for SVO insertion)
    std::vector<gaia::ecs::Entity> getCreatedEntities();

    // Statistics
    struct Stats {
        size_t pendingCount;
        size_t processedCount;
        size_t entitiesCreated;
    };
    Stats getStats() const;

private:
    struct QueueEntry {
        MortonKey key;                 // 8 bytes
        VoxelCreationRequest request;  // 32 bytes
        // Total: 40 bytes (was: 64+ with DynamicVoxelScalar!)
    };

    GaiaVoxelWorld& m_world;  // Creates entities
    std::vector<QueueEntry> m_ringBuffer;
    std::atomic<size_t> m_readIndex{0};
    std::atomic<size_t> m_writeIndex{0};

    // Created entities (for SVO insertion)
    std::vector<gaia::ecs::Entity> m_createdEntities;
    std::mutex m_createdEntitiesMutex;

    void processWorker();
};
```

**Usage**:
```cpp
// Application creates queue on GaiaVoxelWorld
GaiaVoxelWorld world;
VoxelInjectionQueue queue(world);
queue.start(4);  // 4 worker threads

// Enqueue voxel creation (lock-free, async)
for (int i = 0; i < 100000; i++) {
    queue.enqueue(
        glm::vec3(i % 100, i / 100, 0),
        VoxelCreationRequest{1.0f, glm::vec3(1, 0, 0), glm::vec3(0, 1, 0)}
    );
}

// Wait for completion
queue.stop();

// Get created entities for SVO insertion (optional)
auto entities = queue.getCreatedEntities();
```

---

### VoxelInjector (Sync Insertion Helper)
**New Location**: `libraries/GaiaVoxelWorld/include/VoxelInjector.h`

**Owns**:
- SVO insertion logic
- Brick grouping optimization
- Octree traversal coordination

**Does NOT Own**:
- Entity creation (GaiaVoxelWorld does that)
- Async queueing (VoxelInjectionQueue does that)

**API**:
```cpp
class VoxelInjector {
public:
    VoxelInjector(GaiaVoxelWorld& world, LaineKarrasOctree& octree);

    // Insert entities into SVO (entities already created!)
    void insertEntities(const std::vector<gaia::ecs::Entity>& entities);

    // Insert entities grouped by brick (optimized)
    void insertEntitiesBatched(const std::vector<gaia::ecs::Entity>& entities);

    // Compact SVO after batch insertion
    void compactOctree();

private:
    GaiaVoxelWorld& m_world;       // Read entity positions
    LaineKarrasOctree& m_octree;   // Insert into spatial index

    // Group entities by brick coordinate
    std::unordered_map<glm::ivec3, std::vector<gaia::ecs::Entity>>
        groupByBrick(const std::vector<gaia::ecs::Entity>& entities);
};
```

**Usage**:
```cpp
GaiaVoxelWorld world;
LaineKarrasOctree octree(world.getWorld());
VoxelInjector injector(world, octree);

// Get entities from queue
auto entities = queue.getCreatedEntities();

// Insert into SVO (batched, optimized)
injector.insertEntitiesBatched(entities);
injector.compactOctree();
```

---

## Data Flow Examples

### Example 1: Async Entity Creation (No SVO)
```cpp
// Application creates voxels asynchronously
GaiaVoxelWorld world;
VoxelInjectionQueue queue(world);
queue.start();

// Enqueue 100k voxels
for (int i = 0; i < 100000; i++) {
    queue.enqueue(pos, request);
}

queue.stop();

// Entities exist in GaiaVoxelWorld, NO SVO indexing yet!
// Can query via world.queryRegion() using Morton codes
auto voxels = world.queryRegion(min, max);
```

---

### Example 2: Async Entity Creation + SVO Indexing
```cpp
// Application creates voxels + indexes in SVO
GaiaVoxelWorld world;
LaineKarrasOctree octree(world.getWorld());
VoxelInjectionQueue queue(world);
VoxelInjector injector(world, octree);

queue.start();

// Enqueue voxels
for (int i = 0; i < 100000; i++) {
    queue.enqueue(pos, request);
}

// Wait for entity creation
queue.stop();

// Insert entities into SVO (separate step!)
auto entities = queue.getCreatedEntities();
injector.insertEntitiesBatched(entities);
injector.compactOctree();

// Now can raycast via SVO
auto hit = octree.raycast(ray);
```

---

### Example 3: Streaming Pipeline (Continuous)
```cpp
// Continuous voxel streaming with periodic SVO updates
GaiaVoxelWorld world;
LaineKarrasOctree octree(world.getWorld());
VoxelInjectionQueue queue(world);
VoxelInjector injector(world, octree);

queue.start();

// Application loop
while (running) {
    // Enqueue new voxels
    queue.enqueue(pos, request);

    // Periodically flush to SVO (e.g., every frame)
    if (frameCount % 60 == 0) {
        auto entities = queue.getCreatedEntities();
        if (!entities.empty()) {
            injector.insertEntitiesBatched(entities);
        }
    }
}

// Final compaction
queue.stop();
injector.compactOctree();
```

---

## Implementation Details

### VoxelInjectionQueue Worker Thread
```cpp
void VoxelInjectionQueue::processWorker() {
    std::vector<GaiaVoxelWorld::VoxelCreationEntry> batch;
    batch.reserve(256);

    while (m_running) {
        batch.clear();

        // Dequeue batch (lock-free)
        size_t count = 0;
        while (count < 256) {
            size_t readIdx = m_readIndex.load(std::memory_order_acquire);
            size_t writeIdx = m_writeIndex.load(std::memory_order_acquire);

            if (readIdx == writeIdx) break;  // Queue empty

            auto& entry = m_ringBuffer[readIdx % m_capacity];
            batch.push_back({
                entry.key.toWorldPos(),
                entry.request
            });

            m_readIndex.store((readIdx + 1) % m_capacity, std::memory_order_release);
            count++;
        }

        if (batch.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        // Batch create entities (GaiaVoxelWorld handles creation)
        auto entities = m_world.createVoxelsBatch(batch);

        // Store created entities for SVO insertion
        {
            std::lock_guard<std::mutex> lock(m_createdEntitiesMutex);
            m_createdEntities.insert(
                m_createdEntities.end(),
                entities.begin(),
                entities.end()
            );
        }

        m_processedCount.fetch_add(batch.size(), std::memory_order_relaxed);
    }
}
```

---

### VoxelInjector Batch Insertion
```cpp
void VoxelInjector::insertEntitiesBatched(
    const std::vector<gaia::ecs::Entity>& entities) {

    // Group by brick coordinate (one traversal per brick!)
    auto brickGroups = groupByBrick(entities);

    // Insert each brick into SVO
    for (const auto& [brickCoord, brickEntities] : brickGroups) {
        // Get or create brick in octree
        uint32_t brickID = m_octree.getOrCreateBrick(brickCoord);

        // Get brick view (entity reference array)
        auto brickView = m_octree.getBrickView(brickID);

        // Populate brick with entity references
        for (auto entity : brickEntities) {
            glm::ivec3 gridPos = entity.get<MortonKey>().toGridPos();
            glm::ivec3 localPos = gridPos % 8;

            size_t voxelIdx = localPos.x * 64 + localPos.y * 8 + localPos.z;
            brickView.setEntity(voxelIdx, entity);
        }
    }
}
```

---

## File Organization

### GaiaVoxelWorld Library (Data Layer)
```
libraries/GaiaVoxelWorld/
├── include/
│   ├── GaiaVoxelWorld.h           - Entity creation API
│   ├── VoxelComponents.h          - ECS components
│   ├── ComponentRegistry.h        - Type-safe component tags
│   ├── ECSBackedRegistry.h        - Unified attribute system
│   ├── VoxelCreationRequest.h     - Batch creation struct
│   ├── VoxelInjectionQueue.h      - Async entity creation (NEW)
│   └── VoxelInjector.h            - SVO insertion helper (NEW)
├── src/
│   ├── GaiaVoxelWorld.cpp
│   ├── VoxelComponents.cpp
│   ├── ECSBackedRegistry.cpp
│   ├── VoxelInjectionQueue.cpp    - Async worker threads (NEW)
│   └── VoxelInjector.cpp          - Batch SVO insertion (NEW)
└── CMakeLists.txt
```

### SVO Library (Spatial Index - View Only)
```
libraries/SVO/
├── include/
│   ├── LaineKarrasOctree.h        - Spatial queries, ray casting
│   └── OctreeTypes.h              - Node structures
├── src/
│   └── LaineKarrasOctree.cpp      - Octree traversal
└── CMakeLists.txt                  - Depends on GaiaVoxelWorld
```

---

## Dependency Chain

```
Application
  ↓
VoxelInjectionQueue (async entity creation)
  ↓
GaiaVoxelWorld (entity lifecycle)
  ↓
Gaia ECS (component storage)

  ↓ (optional - for spatial queries)
VoxelInjector (entity → SVO indexing)
  ↓
LaineKarrasOctree (spatial index)
  ↓
Queries GaiaVoxelWorld entities
```

**Key Point**: GaiaVoxelWorld has **ZERO dependency** on SVO!

---

## CMakeLists.txt Changes

### GaiaVoxelWorld
```cmake
add_library(GaiaVoxelWorld STATIC
    src/GaiaVoxelWorld.cpp
    src/VoxelComponents.cpp
    src/ECSBackedRegistry.cpp
    src/VoxelInjectionQueue.cpp  # NEW - async layer
    src/VoxelInjector.cpp        # NEW - SVO insertion helper
)

target_link_libraries(GaiaVoxelWorld
    PUBLIC
        gaia::gaia
        glm::glm
        VoxelData
    # NO dependency on SVO!
)
```

**Problem**: VoxelInjector needs LaineKarrasOctree (from SVO library).

**Solution 1**: Keep VoxelInjector in SVO, only move VoxelInjectionQueue
```cmake
# GaiaVoxelWorld - pure data layer
add_library(GaiaVoxelWorld STATIC
    src/GaiaVoxelWorld.cpp
    src/VoxelComponents.cpp
    src/ECSBackedRegistry.cpp
    src/VoxelInjectionQueue.cpp  # Async entity creation
)

# SVO - spatial index + insertion helper
add_library(SVO STATIC
    src/LaineKarrasOctree.cpp
    src/VoxelInjector.cpp  # Stays in SVO (needs octree)
)

target_link_libraries(SVO
    PUBLIC
        GaiaVoxelWorld  # SVO depends on data layer
        VoxelData
)
```

**Solution 2**: Interface/Implementation split (better!)
```cpp
// GaiaVoxelWorld/include/IVoxelInjector.h
class IVoxelInjector {
public:
    virtual ~IVoxelInjector() = default;
    virtual void insertEntities(const std::vector<gaia::ecs::Entity>&) = 0;
};

// SVO/include/VoxelInjector.h
class VoxelInjector : public IVoxelInjector {
    // Implementation uses LaineKarrasOctree
};

// VoxelInjectionQueue accepts interface
class VoxelInjectionQueue {
    VoxelInjectionQueue(GaiaVoxelWorld& world, IVoxelInjector* injector = nullptr);
};
```

---

## Migration Plan

### Phase 1: Move VoxelInjectionQueue to GaiaVoxelWorld
**Files to MOVE**:
- `libraries/SVO/include/VoxelInjectionQueue.h` → `libraries/GaiaVoxelWorld/include/`
- `libraries/SVO/src/VoxelInjectionQueue.cpp` → `libraries/GaiaVoxelWorld/src/`

**Refactor**:
1. Remove VoxelInjector dependency from queue
2. Queue creates entities via `GaiaVoxelWorld::createVoxelsBatch()`
3. Queue stores created entities for optional SVO insertion
4. Add `getCreatedEntities()` method

---

### Phase 2: Refactor VoxelInjector (Stay in SVO)
**Rationale**: VoxelInjector needs `LaineKarrasOctree`, so keep in SVO library.

**Refactor**:
1. Change constructor: `VoxelInjector(GaiaVoxelWorld&, LaineKarrasOctree&)`
2. Remove entity creation logic (GaiaVoxelWorld does that)
3. Focus on SVO insertion optimization (brick grouping, traversal)
4. Add `insertEntities()` / `insertEntitiesBatched()` methods

---

### Phase 3: Update Tests
**New test flow**:
```cpp
TEST(VoxelInjectionQueue, AsyncEntityCreation) {
    GaiaVoxelWorld world;
    VoxelInjectionQueue queue(world);
    queue.start();

    // Enqueue 100k voxels
    for (int i = 0; i < 100000; i++) {
        queue.enqueue(pos, request);
    }

    queue.stop();

    // Verify entities created
    auto entities = queue.getCreatedEntities();
    EXPECT_EQ(entities.size(), 100000);

    // Verify entities exist in world
    auto stats = world.getStats();
    EXPECT_EQ(stats.totalEntities, 100000);
}

TEST(VoxelInjector, BatchSVOInsertion) {
    GaiaVoxelWorld world;
    LaineKarrasOctree octree(world.getWorld());
    VoxelInjector injector(world, octree);

    // Create entities
    auto entities = world.createVoxelsBatch(voxels);

    // Insert into SVO
    injector.insertEntitiesBatched(entities);

    // Verify ray casting works
    auto hit = octree.raycast(ray);
    EXPECT_TRUE(hit.has_value());
}
```

---

## Summary

**VoxelInjectionQueue moves to GaiaVoxelWorld**:
- ✅ Async entity creation (data layer concern)
- ✅ Lock-free ring buffer
- ✅ Worker thread pool
- ✅ Returns created entities for optional SVO insertion

**VoxelInjector stays in SVO**:
- ✅ SVO insertion logic (spatial index concern)
- ✅ Brick grouping optimization
- ✅ Octree traversal coordination
- ✅ Depends on LaineKarrasOctree (can't move to GaiaVoxelWorld)

**Clean separation**:
```
GaiaVoxelWorld (data layer)
  ↓ creates entities
  VoxelInjectionQueue (async)

SVO (spatial layer)
  ↓ indexes entities
  VoxelInjector (sync)
  LaineKarrasOctree (queries)
```

**Next session**: Implement queue migration + VoxelInjector refactoring.
