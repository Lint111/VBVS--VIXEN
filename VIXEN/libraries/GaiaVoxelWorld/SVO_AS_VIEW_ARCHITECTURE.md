# SVO as View Object Architecture

**Core Principle**: SVO stores **zero voxel data** - only spatial structure (octree nodes + entity references).

---

## Current Architecture (Problem)

```
VoxelInjector
  ↓
Creates descriptors with copied voxel data
  ↓
LaineKarrasOctree stores data in bricks
  ↓
AttributeRegistry duplicates data in SoA arrays
  ↓
THREE copies of same data!
```

**Issues**:
1. Data duplication (descriptor, brick, AttributeStorage)
2. VoxelInjector owns voxel creation logic (should be GaiaVoxelWorld)
3. SVO "owns" voxel data (should only reference it)

---

## New Architecture (Solution)

```
Application
  ↓
GaiaVoxelWorld::createVoxel(pos, density, color, normal)
  ↓ Creates entity
  gaia::ecs::Entity (MortonKey, Density, Color_R/G/B, Normal_X/Y/Z)

  ↓ Register with spatial index
LaineKarrasOctree::insert(entity)
  ↓ Stores ONLY
  - Octree structure (nodes, parent/child pointers)
  - Entity references (8 bytes per voxel, not data!)
  - Brick references (for dense regions)

  ↓ Ray casting
LaineKarrasOctree::raycast(ray) → gaia::ecs::Entity
  ↓ Read attributes
entity.get<CR::Density>().value
entity.get<CR::ColorR>().value
```

**Benefits**:
1. **Single source of truth**: Gaia ECS stores ALL voxel data
2. **Zero data duplication**: SVO stores only entity IDs (8 bytes)
3. **Zero-copy ray hits**: Return entity reference, not data copy
4. **Clear ownership**: GaiaVoxelWorld creates/destroys, SVO indexes

---

## Component Responsibilities

### GaiaVoxelWorld (Data Owner)
**Owns**: Entity lifecycle, component data, voxel creation/destruction

**API**:
```cpp
class GaiaVoxelWorld {
public:
    // Create voxel entity (returns lightweight reference)
    EntityID createVoxel(glm::vec3 pos, float density, glm::vec3 color, glm::vec3 normal);

    // Destroy voxel entity
    void destroyVoxel(EntityID id);

    // Query voxels by region (spatial hash)
    std::vector<EntityID> queryRegion(glm::vec3 min, glm::vec3 max);

    // Batch operations (for VoxelInjectionQueue)
    std::vector<EntityID> createVoxelsBatch(const std::vector<VoxelData>& voxels);

    // Get ECS world for advanced queries
    gaia::ecs::World& getWorld();
};
```

**Usage**:
```cpp
GaiaVoxelWorld world;

// Create voxel
auto entity = world.createVoxel(
    glm::vec3(10, 20, 30),  // Position
    1.0f,                    // Density
    glm::vec3(1, 0, 0),     // Color
    glm::vec3(0, 1, 0)      // Normal
);

// Entity ID: 8 bytes (not 64+ bytes of data!)
```

---

### LaineKarrasOctree (Spatial Index - View Only)
**Owns**: Octree structure, spatial queries, ray traversal

**Does NOT Own**: Voxel data (only references via EntityID)

**API**:
```cpp
class LaineKarrasOctree {
public:
    // Insert entity into spatial index
    void insert(gaia::ecs::Entity entity);

    // Remove entity from spatial index
    void remove(gaia::ecs::Entity entity);

    // Ray casting (returns entity reference, NOT data copy)
    struct RayHit {
        gaia::ecs::Entity entity;  // 8 bytes (was: full voxel data copy!)
        glm::vec3 hitPoint;
        float distance;
        uint8_t scale;  // LOD level
    };
    std::optional<RayHit> raycast(const Ray& ray);

    // Query entities in AABB
    std::vector<gaia::ecs::Entity> queryAABB(glm::vec3 min, glm::vec3 max);

private:
    gaia::ecs::World& m_world;  // Reference to ECS (read-only)

    // Octree structure (ONLY spatial data)
    std::vector<uint32_t> m_nodes;  // Octree nodes (child masks, etc.)
    std::vector<gaia::ecs::EntityID> m_leafEntities;  // Leaf → entity mapping

    // Brick storage (for dense regions)
    struct BrickRef {
        uint32_t nodeIdx;  // Octree node index
        std::array<gaia::ecs::EntityID, 512> entities;  // 8³ entity references
    };
    std::vector<BrickRef> m_bricks;
};
```

**Usage**:
```cpp
LaineKarrasOctree octree(world.getWorld());

// Insert voxels into spatial index
auto entity = world.createVoxel(pos, density, color, normal);
octree.insert(entity);

// Ray cast returns entity reference (zero-copy!)
Ray ray(origin, direction);
auto hit = octree.raycast(ray);
if (hit) {
    // Read voxel data from entity
    float density = hit->entity.get<CR::Density>().value;
    glm::vec3 color = CR::ColorRGB::get(hit->entity);
}
```

---

### VoxelInjector (MOVED TO GaiaVoxelWorld)
**Current Location**: `libraries/SVO/src/VoxelInjection.cpp` (1400+ lines)
**New Location**: `libraries/GaiaVoxelWorld/src/VoxelInjector.cpp`

**Why Move**:
- VoxelInjector creates voxels (GaiaVoxelWorld's responsibility)
- Currently duplicates data in descriptors (should create entities instead)
- Tightly coupled to AttributeRegistry (already in GaiaVoxelWorld)

**Refactored API**:
```cpp
class VoxelInjector {
public:
    VoxelInjector(GaiaVoxelWorld& world, LaineKarrasOctree& octree);

    // Create single voxel entity + insert into SVO
    gaia::ecs::Entity insertVoxel(
        const glm::vec3& position,
        const DynamicVoxelScalar& voxel);

    // Batch creation (for VoxelInjectionQueue)
    std::vector<gaia::ecs::Entity> insertVoxelsBatch(
        const std::vector<VoxelCreationRequest>& voxels);

private:
    GaiaVoxelWorld& m_world;       // Creates entities
    LaineKarrasOctree& m_octree;   // Inserts into spatial index

    // Helper: Group voxels by brick coordinate
    std::unordered_map<glm::ivec3, std::vector<gaia::ecs::Entity>>
        groupByBrick(const std::vector<gaia::ecs::Entity>& entities);

    // Helper: Insert brick of entities into SVO
    void insertBrick(const glm::ivec3& brickCoord,
                    const std::vector<gaia::ecs::Entity>& entities);
};
```

**New Implementation**:
```cpp
gaia::ecs::Entity VoxelInjector::insertVoxel(
    const glm::vec3& position,
    const DynamicVoxelScalar& voxel) {

    // Create entity (GaiaVoxelWorld owns creation)
    auto entity = m_world.createVoxel(
        position,
        voxel.get<float>("density"),
        voxel.get<glm::vec3>("color"),
        voxel.get<glm::vec3>("normal")
    );

    // Insert into SVO spatial index
    m_octree.insert(entity);

    return entity;
}

std::vector<gaia::ecs::Entity> VoxelInjector::insertVoxelsBatch(
    const std::vector<VoxelCreationRequest>& voxels) {

    // Batch create entities (GaiaVoxelWorld optimized creation)
    auto entities = m_world.createVoxelsBatch(voxels);

    // Group by brick coordinate for efficient SVO insertion
    auto brickGroups = groupByBrick(entities);

    // Insert each brick into SVO (one traversal per brick!)
    for (const auto& [brickCoord, brickEntities] : brickGroups) {
        insertBrick(brickCoord, brickEntities);
    }

    return entities;
}
```

---

### VoxelInjectionQueue (Async Pipeline - Simplified)
**Owns**: Queueing logic, worker threads, batch coordination

**Does NOT Own**: Voxel creation (VoxelInjector handles it)

**API**:
```cpp
class VoxelInjectionQueue {
public:
    // Enqueue voxel for creation (stores request, not data)
    void enqueue(const VoxelCreationRequest& request);

    // Worker thread processes batches
    void processWorker();

private:
    struct QueueEntry {
        MortonKey position;          // 8 bytes
        VoxelCreationRequest request; // Attribute values (16-32 bytes)
        // Total: 24-40 bytes (was: 64+ with DynamicVoxelScalar copy!)
    };

    VoxelInjector* m_injector;  // Delegates creation to injector
};
```

**Simplified Processing**:
```cpp
void VoxelInjectionQueue::processWorker() {
    while (m_running) {
        // Dequeue batch of requests
        auto batch = dequeueBatch(256);

        // Delegate to VoxelInjector (which uses GaiaVoxelWorld + SVO)
        m_injector->insertVoxelsBatch(batch);
    }
}
```

**Processing Flow**:
```cpp
void VoxelInjectionQueue::processWorker() {
    while (m_running) {
        // Dequeue batch
        std::vector<VoxelCreationRequest> batch = dequeueBatch(256);

        // Create entities in batch (GaiaVoxelWorld owns creation)
        std::vector<gaia::ecs::Entity> entities = m_world->createVoxelsBatch(batch);

        // Insert into spatial index (SVO is just indexing)
        for (auto entity : entities) {
            m_octree->insert(entity);
        }
    }
}
```

---

### BrickView (Read/Write Span over Entity References)
**Owns**: Temporary view into brick entity array

**Does NOT Own**: Entity data (read/write via Gaia ECS)

**API**:
```cpp
class BrickView {
public:
    // Get entity at voxel index
    gaia::ecs::Entity getEntity(size_t voxelIdx) const {
        return m_entities[voxelIdx];
    }

    // Set entity at voxel index
    void setEntity(size_t voxelIdx, gaia::ecs::Entity entity) {
        m_entities[voxelIdx] = entity;
    }

    // Convenience: Read component value
    template<typename TComponent>
    auto getValue(size_t voxelIdx) const -> typename TComponent::ValueType {
        auto entity = getEntity(voxelIdx);
        if (!entity.valid() || !entity.has<TComponent>()) {
            return TComponent{}.value;
        }
        return entity.get<TComponent>().value;
    }

    // Convenience: Write component value
    template<typename TComponent>
    void setValue(size_t voxelIdx, typename TComponent::ValueType value) {
        auto entity = getEntity(voxelIdx);
        if (entity.valid()) {
            entity.set<TComponent>(TComponent{value});
        }
    }

    // Span-based access (zero-copy iteration)
    std::span<gaia::ecs::Entity> entities() {
        return std::span<gaia::ecs::Entity>(m_entities.data(), m_entities.size());
    }

private:
    std::array<gaia::ecs::Entity, 512>& m_entities;  // Reference to brick's entity array
};
```

**Usage**:
```cpp
// Get brick view (read/write span over entity references)
BrickView brick = octree.getBrick(brickID);

// Read voxel data (zero-copy entity access)
float density = brick.getValue<CR::Density>(42);

// Write voxel data (direct entity modification)
brick.setValue<CR::Density>(42, 1.0f);

// Iterate entities (zero-copy span)
for (auto entity : brick.entities()) {
    if (entity.valid()) {
        float d = entity.get<CR::Density>().value;
    }
}
```

---

## Data Flow Examples

### Example 1: Voxel Creation
```cpp
// Application code
GaiaVoxelWorld world;
LaineKarrasOctree octree(world.getWorld());

// Create voxel (data lives in Gaia ECS)
auto entity = world.createVoxel(
    glm::vec3(10, 20, 30),
    1.0f,
    glm::vec3(1, 0, 0),
    glm::vec3(0, 1, 0)
);

// Insert into spatial index (SVO stores entity ID only)
octree.insert(entity);
```

**Memory Layout**:
```
Gaia ECS:
  Entity 42:
    MortonKey: 0x123456789ABCDEF (8 bytes)
    Density: 1.0f (4 bytes)
    Color_R: 1.0f (4 bytes)
    Color_G: 0.0f (4 bytes)
    Color_B: 0.0f (4 bytes)
    Normal_X: 0.0f (4 bytes)
    Normal_Y: 1.0f (4 bytes)
    Normal_Z: 0.0f (4 bytes)
  Total: 36 bytes

LaineKarrasOctree:
  Leaf node 7:
    entityID: 42 (8 bytes)
  Total: 8 bytes (NOT 36 bytes of data duplication!)
```

---

### Example 2: Ray Casting
```cpp
Ray ray(glm::vec3(0, 0, -10), glm::vec3(0, 0, 1));

// Ray cast returns entity reference (not data copy!)
auto hit = octree.raycast(ray);
if (hit) {
    // Zero-copy access to entity components
    float density = hit->entity.get<CR::Density>().value;
    glm::vec3 color = CR::ColorRGB::get(hit->entity);
    glm::vec3 normal = CR::NormalXYZ::get(hit->entity);

    std::cout << "Hit voxel at " << hit->hitPoint
              << " with density " << density << "\n";
}
```

**OLD Ray Hit** (64+ bytes):
```cpp
struct RayHit {
    glm::vec3 position;  // 12 bytes
    float density;       // 4 bytes
    glm::vec3 color;     // 12 bytes
    glm::vec3 normal;    // 12 bytes
    // ... more attributes
    // Total: 64+ bytes (data copy!)
};
```

**NEW Ray Hit** (24 bytes):
```cpp
struct RayHit {
    gaia::ecs::Entity entity;  // 8 bytes (lightweight reference)
    glm::vec3 hitPoint;        // 12 bytes
    float distance;            // 4 bytes
    // Total: 24 bytes (62% reduction!)
};
```

---

### Example 3: Batch Voxel Injection
```cpp
VoxelInjectionQueue queue(&world, &octree);

// Enqueue voxels (stores creation requests, not data)
for (int i = 0; i < 100000; i++) {
    queue.enqueue({
        glm::vec3(i % 100, i / 100, 0),  // Position
        1.0f,                             // Density
        glm::vec3(i / 100000.0f, 0, 0),  // Color
        glm::vec3(0, 1, 0)                // Normal
    });
}

// Worker thread processes batches
void VoxelInjectionQueue::processWorker() {
    // Dequeue 256 requests
    auto batch = dequeueBatch(256);

    // Create entities in batch (GaiaVoxelWorld owns creation)
    auto entities = m_world->createVoxelsBatch(batch);

    // Group by brick coordinate
    std::unordered_map<glm::ivec3, std::vector<gaia::ecs::Entity>> brickGroups;
    for (auto entity : entities) {
        glm::ivec3 brickCoord = getBrickCoord(entity);
        brickGroups[brickCoord].push_back(entity);
    }

    // Insert into SVO (one traversal per brick, not per voxel!)
    for (auto& [brickCoord, voxels] : brickGroups) {
        m_octree->insertBrick(brickCoord, voxels);
    }
}
```

---

## Migration Plan

### Phase 1: Move VoxelInjector to GaiaVoxelWorld Library
**Rationale**: VoxelInjector creates voxels → belongs in data owner (GaiaVoxelWorld), not spatial index (SVO).

**Files to MOVE**:
- `libraries/SVO/include/VoxelInjection.h` → `libraries/GaiaVoxelWorld/include/VoxelInjector.h`
- `libraries/SVO/src/VoxelInjection.cpp` → `libraries/GaiaVoxelWorld/src/VoxelInjector.cpp`

**Files to CREATE**:
- `libraries/GaiaVoxelWorld/include/VoxelCreationRequest.h` - Input struct for batch creation

**Refactor Strategy**:
```cpp
// OLD: VoxelInjector owns descriptors (data duplication)
class VoxelInjector {
    std::vector<VoxelDescriptor> m_descriptors;  // Copies voxel data
    void insertVoxel(const DynamicVoxelScalar& voxel);
};

// NEW: VoxelInjector delegates to GaiaVoxelWorld (zero-copy)
class VoxelInjector {
    GaiaVoxelWorld& m_world;       // Creates entities
    LaineKarrasOctree& m_octree;   // Indexes entities

    gaia::ecs::Entity insertVoxel(const glm::vec3& pos, const DynamicVoxelScalar& voxel) {
        auto entity = m_world.createVoxel(pos, density, color, normal);  // Create entity
        m_octree.insert(entity);  // Index entity
        return entity;            // Return lightweight reference
    }
};
```

**CMakeLists.txt Changes**:
```cmake
# libraries/GaiaVoxelWorld/CMakeLists.txt
add_library(GaiaVoxelWorld STATIC
    src/GaiaVoxelWorld.cpp
    src/VoxelComponents.cpp
    src/ECSBackedRegistry.cpp
    src/VoxelInjector.cpp  # NEW - moved from SVO
)

target_link_libraries(GaiaVoxelWorld
    PUBLIC
        gaia::gaia
        glm::glm
        VoxelData
    # NO dependency on SVO! GaiaVoxelWorld is lower-level
)

# libraries/SVO/CMakeLists.txt
target_link_libraries(SVO
    PUBLIC
        GaiaVoxelWorld  # NEW - SVO depends on data layer
        VoxelData
)
```

**Dependency Inversion**:
```
Before: SVO → VoxelData (VoxelInjector in SVO)
After:  SVO → GaiaVoxelWorld → VoxelData (VoxelInjector in GaiaVoxelWorld)
```

---

### Phase 2: Refactor BrickView to Entity Spans
**Files to modify**:
- `libraries/VoxelData/include/BrickView.h`
- `libraries/VoxelData/src/BrickView.cpp`

**Changes**:
```cpp
// OLD: AttributeStorage slot indices
struct BrickAllocation {
    std::unordered_map<std::string, uint32_t> attributeSlots;
};

// NEW: Entity reference array
struct BrickAllocation {
    std::array<gaia::ecs::Entity, 512> entities;
};
```

---

### Phase 3: Update LaineKarrasOctree to Store Entity IDs
**Files to modify**:
- `libraries/SVO/include/LaineKarrasOctree.h`
- `libraries/SVO/src/LaineKarrasOctree.cpp`

**Changes**:
```cpp
class LaineKarrasOctree {
    // OLD: Stores voxel data in descriptors
    std::vector<VoxelDescriptor> m_descriptors;

    // NEW: Stores entity IDs only
    std::vector<gaia::ecs::EntityID> m_leafEntities;

    // OLD: Ray hit with data copy
    struct RayHit {
        glm::vec3 position;
        float density;
        // ... 64+ bytes
    };

    // NEW: Ray hit with entity reference
    struct RayHit {
        gaia::ecs::Entity entity;  // 8 bytes
        glm::vec3 hitPoint;
        float distance;
    };
};
```

---

### Phase 4: Update VoxelInjectionQueue to Delegate Creation
**Files to modify**:
- `libraries/SVO/include/VoxelInjectionQueue.h`
- `libraries/SVO/src/VoxelInjectionQueue.cpp`

**Changes**:
```cpp
class VoxelInjectionQueue {
    // OLD: Creates descriptors directly
    void processBatch() {
        for (auto& voxel : batch) {
            m_injector->insertVoxel(voxel);  // VoxelInjector owns creation
        }
    }

    // NEW: Delegates to GaiaVoxelWorld
    void processBatch() {
        auto entities = m_world->createVoxelsBatch(batch);
        for (auto entity : entities) {
            m_octree->insert(entity);  // SVO only indexes
        }
    }

private:
    GaiaVoxelWorld* m_world;       // Owns creation
    LaineKarrasOctree* m_octree;   // Indexes entities
};
```

---

## Performance Impact

### Memory Savings

**Before (Data Duplication)**:
```
VoxelDescriptor: 64 bytes (position, attributes)
AttributeStorage: 36 bytes (SoA arrays)
BrickStorage: 40 bytes (dense allocation)
Total: 140 bytes per voxel (3 copies!)
```

**After (Single Source of Truth)**:
```
Gaia Entity: 36 bytes (components in SoA)
SVO Leaf: 8 bytes (entity ID reference)
Total: 44 bytes per voxel (1 copy + reference)
68% reduction!
```

### Ray Casting Performance

**Before**:
```
raycast() → RayHit {position, density, color, normal, ...}
  ↓ Copy 64+ bytes from BrickStorage
  ↓ Return by value (expensive copy)
```

**After**:
```
raycast() → RayHit {entityID, hitPoint, distance}
  ↓ Copy 24 bytes (lightweight)
  ↓ Read attributes on-demand from entity
  ↓ Zero-copy if only checking intersection
```

**Result**: 62% smaller ray hit struct, zero-copy attribute access.

---

## Summary

**SVO becomes a pure spatial index**:
- ✅ Stores octree structure (nodes, masks, child pointers)
- ✅ Stores entity references (8 bytes per voxel)
- ✅ Provides spatial queries (ray casting, AABB)
- ❌ Does NOT store voxel data (Gaia ECS owns it)

**GaiaVoxelWorld becomes the data owner**:
- ✅ Creates/destroys entities
- ✅ Owns component data (density, color, normal)
- ✅ Provides batch operations (createVoxelsBatch)
- ✅ Single source of truth for all voxel data

**Benefits**:
- 68% memory reduction (140 → 44 bytes per voxel)
- Zero data duplication (single source of truth)
- Clear ownership (GaiaVoxelWorld creates, SVO indexes)
- Zero-copy ray hits (return entity reference, not data)
- Simpler architecture (no AttributeStorage → BrickStorage → Descriptor copies)

**Next Session**:
1. Implement `GaiaVoxelWorld::createVoxelsBatch()`
2. Refactor `BrickView` to use entity spans
3. Update `LaineKarrasOctree::raycast()` to return entity references
4. Migrate `VoxelInjectionQueue` to delegate creation to GaiaVoxelWorld
