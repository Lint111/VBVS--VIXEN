# Phase 3: LaineKarrasOctree Entity Storage Migration

**Status**: DEFERRED - Foundational architecture complete, full migration pending.

---

## Completed in Phases 1A-2

✅ **VoxelInjectionQueue** - Moved to GaiaVoxelWorld (async entity creation)
✅ **VoxelInjector** - Created entity-based injector in GaiaVoxelWorld
✅ **EntityBrickView** - Lightweight span over entity arrays (4 KB vs 70 KB)

---

## Remaining Work: LaineKarrasOctree API Extension

### Goal
Extend LaineKarrasOctree to support entity-based storage alongside existing AttributeStorage API.

### Current State (OLD)
```cpp
// LaineKarrasOctree stores attribute data via BrickReferences
struct BrickReference {
    uint32_t brickID;           // Index into BrickStorage
    uint8_t localX, localY, localZ;  // Voxel within brick
};

// Ray hit returns data copy (64+ bytes)
struct RayHit {
    glm::vec3 position;
    float density;
    glm::vec3 color;
    glm::vec3 normal;
    // ... more attributes
};
```

**Memory per brick**: 512 voxels × 140 bytes = 70 KB

### Target State (NEW)
```cpp
// LaineKarrasOctree stores entity references
struct EntityBrickReference {
    std::array<gaia::ecs::Entity, 512> entities;  // 8 bytes each
};

// Ray hit returns entity reference (24 bytes)
struct RayHit {
    gaia::ecs::Entity entity;  // 8 bytes (lightweight reference)
    glm::vec3 hitPoint;        // 12 bytes
    float distance;            // 4 bytes
};
```

**Memory per brick**: 512 entities × 8 bytes = 4 KB (17.5× reduction!)

---

## Implementation Plan

### Step 1: Add Entity-Based Brick Storage to Octree
**File**: `libraries/SVO/include/SVOTypes.h`

```cpp
// Add new entity-based brick storage
struct EntityBrick {
    std::array<gaia::ecs::Entity, 512> entities;

    gaia::ecs::Entity& operator[](size_t idx) {
        return entities[idx];
    }

    const gaia::ecs::Entity& operator[](size_t idx) const {
        return entities[idx];
    }
};

// Update Octree to support both storage modes
struct Octree {
    // ... existing fields

    // NEW: Entity-based brick storage
    std::vector<EntityBrick> entityBricks;
    bool useEntityStorage = false;  // Feature flag
};
```

### Step 2: Update LaineKarrasOctree Constructor
**File**: `libraries/SVO/include/LaineKarrasOctree.h`

```cpp
class LaineKarrasOctree : public ISVOStructure {
public:
    // NEW: Constructor accepting GaiaVoxelWorld for entity-based storage
    LaineKarrasOctree(
        GaiaVoxel::GaiaVoxelWorld* world,
        int maxLevels = 23,
        int brickDepthLevels = 3);

    // Enable entity-based storage mode
    void setEntityStorageMode(bool enabled);

private:
    GaiaVoxel::GaiaVoxelWorld* m_world = nullptr;  // For entity component access
    bool m_useEntityStorage = false;
};
```

### Step 3: Add Entity Insertion API
**File**: `libraries/SVO/include/LaineKarrasOctree.h`

```cpp
// NEW: Entity-based insertion (replaces DynamicVoxelScalar copies)
bool insertEntity(
    const glm::vec3& position,
    gaia::ecs::Entity entity,
    const InjectionConfig& config = InjectionConfig{});

// NEW: Batch entity insertion
size_t insertEntitiesBatch(
    const std::vector<std::pair<glm::vec3, gaia::ecs::Entity>>& entities,
    const InjectionConfig& config = InjectionConfig{});
```

### Step 4: Update Ray Casting to Return Entity References
**File**: `libraries/SVO/include/ISVOStructure.h`

```cpp
// NEW: Entity-based ray hit
struct EntityRayHit {
    gaia::ecs::Entity entity;  // 8 bytes
    glm::vec3 hitPoint;        // 12 bytes
    float distance;            // 4 bytes
    int scale;                 // 4 bytes (optional)
    // Total: 24-28 bytes (vs 64+ bytes for data copy!)
};

// Add entity-based ray casting method
virtual std::optional<EntityRayHit> castRayEntity(
    const glm::vec3& origin,
    const glm::vec3& direction,
    float tMin = 0.0f,
    float tMax = std::numeric_limits<float>::max()) const = 0;
```

### Step 5: Update Brick Traversal to Use Entity Storage
**File**: `libraries/SVO/src/LaineKarrasOctree.cpp`

```cpp
// Update traverseBrick() to access entities instead of AttributeStorage
bool LaineKarrasOctree::traverseBrick(
    const BrickReference& brickRef,
    TraversalState& state) const {

    if (m_useEntityStorage) {
        // NEW: Access entity brick
        const EntityBrick& brick = m_octree->entityBricks[brickRef.brickID];

        // Sample entity at voxel
        gaia::ecs::Entity entity = brick[localIdx];

        // Read density from entity (via Gaia ECS)
        if (entity.valid() && m_world) {
            auto density = m_world->getDensity(entity);
            if (density.has_value() && evaluateKey(density.value())) {
                // Hit!
                return true;
            }
        }
    } else {
        // OLD: Access AttributeStorage
        // ... existing code
    }
}
```

---

## Migration Strategy

### Phase 3A: Dual Storage Mode (Backward Compatible)
1. Add entity storage alongside AttributeStorage
2. Use feature flag to switch between modes
3. All tests pass with both modes
4. Measure memory savings in entity mode

### Phase 3B: Deprecate AttributeStorage (Breaking Change)
1. Update all tests to use entity mode
2. Remove AttributeStorage code paths
3. Delete unused BrickStorage wrapper
4. Document breaking changes

### Phase 3C: Optimize Entity Access (Performance)
1. Benchmark entity component reads in hot paths
2. Add entity caching if needed
3. Optimize Gaia query patterns
4. Target: <5% performance overhead vs raw AttributeStorage

---

## Expected Benefits

### Memory Savings
- **Queue entries**: 40 bytes (vs 64+ bytes) - 37% reduction
- **Brick storage**: 4 KB (vs 70 KB) - 94% reduction
- **Ray hits**: 24 bytes (vs 64+ bytes) - 62% reduction

### Code Simplification
- Single source of truth (Gaia ECS owns data)
- No data duplication between AttributeRegistry and SVO
- Simpler VoxelInjector (delegates creation to GaiaVoxelWorld)
- Zero-copy entity access

### Flexibility
- Runtime attribute changes (add/remove components)
- Custom entity queries (filter by material, emission, etc.)
- Integration with game logic (entities = gameplay objects)

---

## Testing Plan

1. **Unit Tests**: Entity storage vs AttributeStorage equivalence
2. **Integration Tests**: VoxelInjectionQueue → EntityBrickView → LaineKarrasOctree
3. **Performance Tests**: Ray casting throughput (entity vs attribute mode)
4. **Memory Tests**: Verify 94% brick storage reduction

---

## Status

**Completed**:
- ✅ VoxelInjectionQueue (GaiaVoxelWorld)
- ✅ VoxelInjector (GaiaVoxelWorld)
- ✅ EntityBrickView (GaiaVoxelWorld)

**Deferred** (8-16 hours estimated):
- ⏸️ LaineKarrasOctree entity storage API
- ⏸️ Entity-based ray casting
- ⏸️ Dual storage mode implementation
- ⏸️ Test migration to entity mode

**Rationale for Deferral**:
- Foundation complete (data layer owns entities)
- LaineKarrasOctree refactoring is complex (would break 100+ tests)
- Current AttributeStorage API works (can defer optimization)
- Architecture proven via design docs and prototypes
