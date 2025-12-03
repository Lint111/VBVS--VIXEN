# GaiaVoxelWorld + VoxelData Integration Design

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                    AttributeRegistry                             │
│  - Defines voxel schema dynamically                              │
│  - Registers: density, color, normal, material, etc.             │
│  - Key attribute for solidity                                    │
└────────────────┬────────────────────────────────────────────────┘
                 │
                 ↓ Schema Synchronization
┌─────────────────────────────────────────────────────────────────┐
│                    GaiaVoxelWorld                                │
│  - Maps AttributeRegistry schema to Gaia ECS components          │
│  - Creates Gaia entities for voxels/chunks                       │
│  - Stores entity IDs, NOT data copies                            │
└────────────────┬────────────────────────────────────────────────┘
                 │
                 ↓ Entity Storage
┌─────────────────────────────────────────────────────────────────┐
│                    Gaia ECS World                                │
│  - Component data stored in SoA chunks                           │
│  - Position component (always present)                           │
│  - Dynamic components based on registry schema                   │
│  - Chunk/Brick metadata components                               │
└─────────────────────────────────────────────────────────────────┘
```

---

## Problem Statement

**Current Issue:** Hard-coded Position/Density/Color/Normal components don't match VoxelData's dynamic schema.

**What We Need:**
1. **Dynamic component creation** - GaiaVoxelWorld must mirror AttributeRegistry's schema
2. **Chunk-based operations** - Insert/update entire chunks (8³ voxels), not individual voxels
3. **Zero-copy integration** - Store entity IDs in VoxelInjectionQueue, read components from Gaia

---

## Integration Strategy

### Phase 1: Component Mapping (AttributeRegistry → Gaia)

**Schema Synchronization:**
```cpp
class GaiaVoxelWorld : public IAttributeRegistryObserver {
    void onAttributeAdded(const std::string& name, AttributeType type) override {
        // Create corresponding Gaia component type
        registerDynamicComponent(name, type);
    }
};
```

**Challenge:** Gaia requires **compile-time** component types, but AttributeRegistry is **runtime** dynamic.

**Solution Options:**

#### Option A: Fixed Component Pool (RECOMMENDED)
Pre-define component types for common attributes:
```cpp
// Fixed components (compile-time)
struct Position { float x, y, z; };
struct Density { float value; };
struct Color_R { float value; };   // RGB split for SoA
struct Color_G { float value; };
struct Color_B { float value; };
struct Normal_X { float value; };  // Normal split
struct Normal_Y { float value; };
struct Normal_Z { float value; };
struct Material { uint32_t id; };
struct Emission { float r, g, b, intensity; };

// Map AttributeRegistry names to fixed components
std::unordered_map<std::string, ComponentType> attributeToComponent = {
    {"density", ComponentType::Density},
    {"color", ComponentType::ColorRGB},  // Maps to Color_R/G/B
    {"normal", ComponentType::NormalXYZ},
    {"material", ComponentType::Material},
};
```

**Pros:**
- Works with Gaia's compile-time requirements
- Optimal SoA layout (1 float per component)
- Covers 95% of voxel use cases

**Cons:**
- Limited to pre-defined attributes
- Requires mapping layer

#### Option B: Generic Attribute Components
Use generic components with indices:
```cpp
struct FloatAttribute0 { float value; };
struct FloatAttribute1 { float value; };
// ... up to FloatAttribute31 (max 32 components/entity)

struct Vec3Attribute0 { float x, y, z; };
struct Vec3Attribute1 { float x, y, z; };
// ... up to Vec3Attribute10
```

**Pros:**
- Fully dynamic attribute support
- Works with any AttributeRegistry schema

**Cons:**
- Awkward API (attribute names lost)
- Complex mapping logic

---

### Phase 2: Chunk-Based Operations

**Chunk Representation:**
```cpp
// Chunk entity (one per 8³ voxel region)
struct ChunkEntity {
    gaia::ecs::Entity chunkID;
    glm::ivec3 chunkCoord;     // Grid coordinate
    uint32_t brickID;          // AttributeRegistry brick ID
};

// Voxel entities reference parent chunk
struct VoxelEntity {
    gaia::ecs::Entity entityID;
    gaia::ecs::Entity parentChunk;  // Reference to ChunkEntity
    glm::vec3 localPos;              // Position within chunk [0,8)
    // + dynamic attribute components
};
```

**Chunk Insertion Workflow:**
```cpp
// Input: Chunk start position + DynamicVoxelArray (8³ voxels)
void insertChunk(
    const glm::vec3& chunkOrigin,
    const std::vector<DynamicVoxelScalar>& voxels) {

    // 1. Create chunk entity
    auto chunk = world.add();
    chunk.add<ChunkMetadata>(ChunkMetadata{chunkOrigin, brickID});

    // 2. Create voxel entities (batch)
    std::vector<gaia::ecs::Entity> voxelEntities;
    voxelEntities.reserve(8*8*8);

    for (int z = 0; z < 8; ++z) {
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                int idx = x + y*8 + z*64;
                const auto& voxel = voxels[idx];

                auto entity = world.add();
                entity.add<Position>(Position{
                    chunkOrigin.x + x,
                    chunkOrigin.y + y,
                    chunkOrigin.z + z
                });

                // Add dynamic attributes from DynamicVoxelScalar
                for (const auto& attrName : voxel.getAttributeNames()) {
                    addDynamicComponent(entity, attrName, voxel);
                }

                entity.add<ChunkParent>(ChunkParent{chunk});
                voxelEntities.push_back(entity);
            }
        }
    }

    // 3. Store entity IDs in chunk metadata
    chunk.add<VoxelEntityList>(VoxelEntityList{voxelEntities});
}
```

---

### Phase 3: VoxelInjectionQueue Integration

**Old Queue (copies data):**
```cpp
struct VoxelEntry {
    glm::vec3 position;
    DynamicVoxelScalar attributes;  // COPY (~64+ bytes)
};
```

**New Queue (references only):**
```cpp
struct VoxelEntry {
    glm::vec3 position;             // 12 bytes
    gaia::ecs::Entity entityID;     // 8 bytes
    uint32_t chunkID;                // 4 bytes (optional)
};  // Total: 24 bytes vs 64+ bytes
```

**Enqueue Workflow:**
```cpp
// Producer thread
void enqueue(const glm::vec3& pos, const DynamicVoxelScalar& data) {
    // Create entity in GaiaVoxelWorld
    auto entity = gaiaWorld.createVoxelFromDynamic(pos, data);

    // Store entity ID in queue (not data!)
    queue.enqueue(pos, entity);
}

// Worker thread
void processQueue() {
    auto batch = queue.dequeueBatch(512);

    // Group by chunk coordinate
    std::unordered_map<glm::ivec3, std::vector<gaia::ecs::Entity>> chunks;
    for (const auto& entry : batch) {
        glm::ivec3 chunkCoord = glm::floor(entry.position / 8.0f);
        chunks[chunkCoord].push_back(entry.entityID);
    }

    // Process each chunk in parallel
    for (const auto& [chunkCoord, entities] : chunks) {
        processChunk(chunkCoord, entities);
    }
}
```

---

## Recommended Implementation Path

### Phase 1: Fixed Component Pool (Immediate)
1. Define 8-10 common components (density, color, normal, material, emission)
2. Create mapping from AttributeRegistry attribute names to component types
3. Implement `createVoxelFromDynamic(position, DynamicVoxelScalar)`
4. Test with existing VoxelInjectionQueue (still copying data)

### Phase 2: Chunk Operations (Week 2)
1. Add `ChunkMetadata`, `ChunkParent`, `VoxelEntityList` components
2. Implement `insertChunk(chunkOrigin, voxels[])`
3. Update VoxelInjector to use chunk-based insertion
4. Verify spatial queries work across chunk boundaries

### Phase 3: Queue Integration (Week 3)
1. Change `VoxelEntry` to store entity ID instead of DynamicVoxelScalar
2. Update queue workers to read entity components (not copied data)
3. Implement chunk-level batching in queue processing
4. Performance validation (memory usage, throughput)

---

## Key Design Decisions

### Decision 1: Fixed vs Dynamic Components
**Choice:** Fixed component pool with name mapping
**Rationale:**
- Gaia requires compile-time component types
- 8-10 fixed components cover 95% of use cases
- Cleaner API, better performance

### Decision 2: Chunk-Level vs Voxel-Level Entities
**Choice:** Both (chunk entities + voxel entities)
**Rationale:**
- Chunk entities enable spatial queries (query all chunks in region)
- Voxel entities store per-voxel data (needed for ray tracing)
- Hierarchical structure mirrors octree organization

### Decision 3: AttributeRegistry Integration
**Choice:** GaiaVoxelWorld implements IAttributeRegistryObserver
**Rationale:**
- Synchronizes Gaia component schema with AttributeRegistry changes
- Ensures consistency between VoxelData and Gaia
- Minimal coupling (observer pattern)

---

## Next Steps

1. **Implement Fixed Component Pool** (VoxelComponents.h update)
2. **Add Component Mapping** (AttributeRegistry name → Gaia component type)
3. **Implement `createVoxelFromDynamic()`** (bridge DynamicVoxelScalar → Gaia)
4. **Update GaiaVoxelWorld.h API** (chunk operations, observer implementation)
5. **Write integration tests** (AttributeRegistry schema → Gaia components)

---

## Open Questions

1. **How to handle custom attributes** not in fixed pool?
   - Option: Generic `CustomFloat0-31`, `CustomVec30-10` components
   - Option: Ignore (print warning)

2. **Chunk size assumption** (8³ hardcoded)?
   - Make configurable via template parameter?
   - Support variable chunk sizes?

3. **Entity lifecycle** - when to destroy entities?
   - On chunk removal?
   - On voxel sparsity check?
   - Reference counting?
