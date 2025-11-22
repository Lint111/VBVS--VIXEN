# ECS-Backed Attribute System - Integration Roadmap

**Status**: Phase 1 Complete (ECSBackedRegistry implemented)
**Date**: November 22, 2025
**Goal**: Integrate unified attribute system into VoxelInjectionQueue and BrickView

---

## Phase 1: ECSBackedRegistry ✅ COMPLETE

**Deliverables**:
- ✅ [ECSBackedRegistry.h](include/ECSBackedRegistry.h) - Template-based component registration
- ✅ [ECSBackedRegistry.cpp](src/ECSBackedRegistry.cpp) - DynamicVoxelScalar ↔ Entity conversion
- ✅ [UNIFIED_ATTRIBUTE_DESIGN.md](UNIFIED_ATTRIBUTE_DESIGN.md) - Complete architecture design
- ✅ [GaiaVoxelWorld.cpp](src/GaiaVoxelWorld.cpp) - Updated to use MortonKey + split components
- ✅ CMakeLists.txt - Added VoxelData dependency

**Tested**: Build system integration (CMake configuration)

---

## Phase 2: BrickView Entity Storage (NEXT - 2 hours)

### Goal
Replace BrickView's AttributeStorage slot indices with Gaia entity references.

### Changes Required

#### 2.1 Update BrickAllocation Structure
**File**: `libraries/VoxelData/include/BrickView.h`

```cpp
struct BrickAllocation {
    uint32_t brickID;

    // OLD: Per-attribute slot indices
    // std::unordered_map<std::string, uint32_t> attributeSlots;

    // NEW: Entity array (512 voxels)
    std::array<gaia::ecs::Entity, 512> entities;

    // Validity tracking (optional - entities.valid() can check)
    std::bitset<512> occupied;
};
```

#### 2.2 Add Entity Access API to BrickView
**File**: `libraries/VoxelData/include/BrickView.h`

```cpp
class BrickView {
public:
    // NEW: Entity-based access
    gaia::ecs::Entity getEntity(size_t voxelIdx) const;
    void setEntity(size_t voxelIdx, gaia::ecs::Entity entity);

    // NEW: Component-based get/set
    template<typename TComponent>
    typename TComponent::ValueType getComponent(size_t voxelIdx) const {
        auto entity = getEntity(voxelIdx);
        if (! world.exists(entity) || !entity.has<TComponent>()) {
            return TComponent{}.value;
        }
        return entity.get<TComponent>().value;
    }

    template<typename TComponent>
    void setComponent(size_t voxelIdx, typename TComponent::ValueType value) {
        auto entity = getEntity(voxelIdx);
        if ( world.exists(entity)) {
            entity.set<TComponent>(TComponent{value});
        }
    }

    // BACKWARD COMPAT: String-based API (delegates to ECSBackedRegistry)
    template<typename T>
    T get(const std::string& name, size_t index) const;

    template<typename T>
    void set(const std::string& name, size_t index, const T& value);
};
```

#### 2.3 Modify BrickView Constructor
**File**: `libraries/VoxelData/src/BrickView.cpp`

```cpp
BrickView::BrickView(uint32_t brickID,
                     BrickAllocation* allocation,
                     ECSBackedRegistry* registry)
    : m_brickID(brickID)
    , m_allocation(allocation)
    , m_registry(registry) {
    // Entities already created by registry->createEntity()
    // BrickView is a zero-cost view into entity array
}
```

### Testing
- Unit test: Create brick, set/get entities
- Unit test: Backward compat - string-based get/set still works
- Unit test: 512 voxels in brick, sparse occupancy

---

## Phase 3: VoxelInjectionQueue Entity Integration (2 hours)

### Goal
Replace DynamicVoxelScalar data copies with entity IDs in queue entries.

### Changes Required

#### 3.1 Update Queue Entry Structure
**File**: `libraries/SVO/include/VoxelInjectionQueue.h`

```cpp
struct QueueEntry {
    // OLD: glm::vec3 position; DynamicVoxelScalar voxel;  // 64+ bytes

    // NEW: MortonKey + Entity ID
    MortonKey key;               // 8 bytes
    gaia::ecs::EntityID entityID; // 8 bytes
    // Total: 16 bytes (75% reduction!)
};
```

#### 3.2 Update Enqueue Method
**File**: `libraries/SVO/src/VoxelInjectionQueue.cpp`

```cpp
void VoxelInjectionQueue::enqueue(const glm::vec3& position,
                                 const DynamicVoxelScalar& voxel) {
    // Convert voxel to entity (via ECSBackedRegistry)
    auto entity = m_registry->createEntity(voxel);

    // Enqueue lightweight reference
    size_t writeIdx = m_writeIndex.fetch_add(1, std::memory_order_relaxed) % m_capacity;
    m_ringBuffer[writeIdx] = {
        MortonKey::fromPosition(position),
        entity.id()
    };

    m_pendingCount.fetch_add(1, std::memory_order_release);
}
```

#### 3.3 Update Worker Thread Processing
**File**: `libraries/SVO/src/VoxelInjectionQueue.cpp`

```cpp
void VoxelInjectionQueue::processWorker() {
    while (m_running) {
        // Pop entity IDs from queue
        std::vector<MortonKey> keys;
        std::vector<gaia::ecs::EntityID> entityIDs;
        dequeueEntities(keys, entityIDs, 256);  // Batch 256

        if (keys.empty()) continue;

        // Group by brick coordinate
        auto brickGroups = groupByBrick(keys, entityIDs);

        // Process each brick
        for (const auto& [brickCoord, voxelEntries] : brickGroups) {
            processBrickBatch(brickCoord, voxelEntries);
        }
    }
}
```

#### 3.4 Brick Batch Processing
**File**: `libraries/SVO/src/VoxelInjection.cpp`

```cpp
void VoxelInjector::processBrickBatch(
    const glm::ivec3& brickCoord,
    const std::vector<std::pair<MortonKey, gaia::ecs::EntityID>>& voxels) {

    // Get or create brick
    uint32_t brickID = getOrCreateBrick(brickCoord);
    BrickView brick = m_registry->getBrick(brickID);

    // Fill brick with entity references
    for (const auto& [key, entityID] : voxels) {
        glm::ivec3 gridPos = key.toGridPos();
        glm::ivec3 localPos = gridPos % 8;

        // Store entity in brick (zero-copy reference)
        brick.setEntity(
            localPos.x * 64 + localPos.y * 8 + localPos.z,
            gaia::ecs::Entity(entityID)
        );
    }

    // Update octree structure (one traversal per brick, not per voxel!)
    updateOctreeForBrick(brickID, brickCoord);
}
```

### Testing
- Functional test: 100k voxel enqueue → verify entity creation
- Performance test: Measure enqueue rate (expect >10k voxels/sec)
- Memory test: Verify 75% reduction in queue memory (64 → 16 bytes)
- Stress test: Concurrent enqueue from multiple threads

---

## Phase 4: Integration Testing & Validation (1.5 hours)

### Test Suite

#### 4.1 Unit Tests
**File**: `libraries/GaiaVoxelWorld/tests/test_ecs_backed_registry.cpp`

```cpp
TEST(ECSBackedRegistry, ComponentRegistration) {
    gaia::ecs::World world;
    ECSBackedRegistry registry(world);

    auto idx = registry.registerComponent<Density>("density", true);
    EXPECT_EQ(idx, 0);  // First index
}

TEST(ECSBackedRegistry, EntityCreation) {
    gaia::ecs::World world;
    ECSBackedRegistry registry(world);
    registry.registerComponent<Density>("density", true);

    DynamicVoxelScalar voxel(&registry);
    voxel.set("density", 1.0f);

    auto entity = registry.createEntity(voxel);
    EXPECT_TRUE( world.exists(entity));
    EXPECT_TRUE(entity.has<Density>());
    EXPECT_FLOAT_EQ(entity.get<Density>().value, 1.0f);
}

TEST(ECSBackedRegistry, VoxelFromEntity) {
    gaia::ecs::World world;
    ECSBackedRegistry registry(world);
    registry.registerComponent<Density>("density", true);

    auto entity = registry.createEntity(glm::vec3(1,2,3), {{"density", 0.8f}});
    auto voxel = registry.getVoxelFromEntity(entity);

    EXPECT_FLOAT_EQ(voxel.get<float>("density"), 0.8f);
}
```

#### 4.2 Integration Tests
**File**: `libraries/SVO/tests/test_voxel_injection_ecs.cpp`

```cpp
TEST(VoxelInjectionECS, QueueMemoryReduction) {
    // OLD: 64+ bytes per entry
    size_t oldSize = sizeof(glm::vec3) + sizeof(DynamicVoxelScalar);

    // NEW: 16 bytes per entry
    size_t newSize = sizeof(MortonKey) + sizeof(gaia::ecs::EntityID);

    EXPECT_LT(newSize, oldSize * 0.3f);  // >70% reduction
}

TEST(VoxelInjectionECS, 100kVoxelBatch) {
    gaia::ecs::World world;
    ECSBackedRegistry registry(world);
    registry.registerComponent<Density>("density", true);

    VoxelInjectionQueue queue(&registry);

    // Enqueue 100k voxels
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 100000; i++) {
        DynamicVoxelScalar voxel(&registry);
        voxel.set("density", 1.0f);
        queue.enqueue(glm::vec3(i % 100, i / 100, 0), voxel);
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "Enqueue rate: " << (100000.0 / duration.count() * 1000) << " voxels/sec\n";

    // Verify entity creation
    EXPECT_EQ(world.count<MortonKey>(), 100000);
}
```

#### 4.3 Performance Benchmarks
**File**: `libraries/GaiaVoxelWorld/tests/benchmark_ecs_backend.cpp`

```cpp
// Benchmark: Entity creation throughput
// Benchmark: Queue enqueue/dequeue throughput
// Benchmark: Brick filling from entities
// Benchmark: Memory footprint (queue + entities)
```

### Success Criteria
- ✅ All unit tests pass
- ✅ 100k voxel injection completes in <10 seconds
- ✅ Queue memory usage: <2 MB for 100k voxels (16 bytes × 100k)
- ✅ Entity count matches voxel count (sparse allocation working)
- ✅ Zero crashes under concurrent enqueue

---

## Phase 5: Documentation & Examples (1 hour)

### Documentation Updates

#### 5.1 Usage Guide
**File**: `libraries/GaiaVoxelWorld/USAGE_GUIDE.md`

```markdown
# ECS-Backed Attribute System Usage

## Setup
```cpp
// 1. Create ECS world
gaia::ecs::World world;

// 2. Create registry
ECSBackedRegistry registry(world);

// 3. Register components
registry.registerComponent<Density>("density", true);  // Key attribute
registry.registerVec3<Color_R, Color_G, Color_B>("color");

// 4. Create VoxelInjectionQueue
VoxelInjectionQueue queue(&registry);
```

## Enqueueing Voxels
```cpp
DynamicVoxelScalar voxel(&registry);
voxel.set("density", 1.0f);
voxel.set("color", glm::vec3(1, 0, 0));

queue.enqueue(glm::vec3(10, 20, 30), voxel);
```

## Querying Entities
```cpp
auto voxels = world.query().all<Density, Color_R>();
voxels.each([](gaia::ecs::Entity entity) {
    float density = entity.get<Density>().value;
    float red = entity.get<Color_R>().value;
    // ...
});
```
```

#### 5.2 API Reference
**File**: `libraries/GaiaVoxelWorld/API_REFERENCE.md`

- ECSBackedRegistry methods
- BrickView entity access
- VoxelInjectionQueue entity-based API
- Component type mapping

---

## Timeline Summary

| Phase | Task | Estimated Time | Status |
|-------|------|----------------|--------|
| 1 | ECSBackedRegistry implementation | 2 hours | ✅ DONE |
| 2 | BrickView entity storage | 2 hours | 🔲 TODO |
| 3 | VoxelInjectionQueue integration | 2 hours | 🔲 TODO |
| 4 | Integration testing | 1.5 hours | 🔲 TODO |
| 5 | Documentation & examples | 1 hour | 🔲 TODO |
| **Total** | **Complete unified system** | **8.5 hours** | **12% done** |

---

## Memory Savings Validation

### Before (Current System)
```
Queue (100k voxels):
  Entry size: 64 bytes
  Total: 6.4 MB

Bricks (5k bricks, 10% occupancy):
  Per-attribute arrays: 512 × 4 bytes × 4 attrs = 8 KB/brick
  Total: 40 MB

TOTAL: 46.4 MB
```

### After (ECS-Backed System)
```
Queue (100k voxels):
  Entry size: 16 bytes
  Total: 1.6 MB (-75%)

Bricks (5k bricks, sparse entities):
  Entity references: 512 × 8 bytes = 4 KB/brick
  Component data (sparse): 51 voxels × 16 bytes = 816 bytes/brick
  Total: ~24 MB (-40%)

TOTAL: 25.6 MB (-45% overall)
```

**Projected Savings**: **20.8 MB** for 100k voxel scene

---

## Next Session Goals

1. ✅ Test CMake build
2. Implement BrickView::getEntity() / setEntity()
3. Update VoxelInjectionQueue entry structure
4. Write basic integration test
5. Measure memory savings

**Estimated time**: 3-4 hours for functional prototype
