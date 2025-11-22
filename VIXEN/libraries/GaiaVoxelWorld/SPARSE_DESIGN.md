# GaiaVoxelWorld - Sparse Voxel Storage via Morton Codes

## Core Principle: Morton Code = Position

**No explicit Position component.** The Morton code (spatial hash) **IS** the voxel position.

```cpp
// ❌ OLD (wasteful): Store position explicitly
struct Position { float x, y, z; };  // 12 bytes redundant
entity.add<Position>({x, y, z});
entity.add<SpatialHash>(mortonCode(x, y, z));

// ✅ NEW (optimal): Morton code only
// Position computed from Morton code when needed
uint64_t morton = mortonCode(x, y, z);
entity.add<MortonKey>(morton);  // 8 bytes, encodes position
```

---

## Sparse-Only Storage

**Only create entities for solid voxels** (those passing key predicate).

```cpp
// Example: 8³ chunk with 512 voxels
// Dense storage: 512 entities (even if empty)
// Sparse storage: ~50 entities (only solid voxels)

for (const auto& voxel : chunk.voxels) {
    if (!voxel.passesKeyPredicate()) {
        continue;  // Skip empty voxels - NO entity created
    }

    // Create entity ONLY for solid voxels
    auto entity = world.add();
    entity.add<MortonKey>(computeMorton(x, y, z));
    entity.add<Density>(voxel.get<float>("density"));
    // ... other attributes
}
```

**Result:**
- 8³ chunk with 10% occupancy: **51 entities** instead of 512
- Memory: ~3 KB instead of ~30 KB (10× reduction)

---

## Component Schema

### Core Components (Always Present)

```cpp
/**
 * Morton code - encodes 3D position in single uint64.
 * Bits [0-20]:  X coordinate (21 bits)
 * Bits [21-41]: Y coordinate (21 bits)
 * Bits [42-62]: Z coordinate (21 bits)
 *
 * Enables O(1) spatial queries via bit operations.
 */
struct MortonKey {
    uint64_t code = 0;

    // Decode position from Morton code
    glm::ivec3 decode() const {
        return {
            decodeBits(code, 0),       // X
            decodeBits(code >> 1, 0),  // Y
            decodeBits(code >> 2, 0)   // Z
        };
    }

    glm::vec3 toWorldPos() const {
        glm::ivec3 grid = decode();
        return glm::vec3(grid);
    }

private:
    static int decodeBits(uint64_t v, int offset);
};
```

### Attribute Components (Dynamic based on AttributeRegistry)

```cpp
// Mapped from AttributeRegistry schema
struct Density { float value = 1.0f; };
struct Color_R { float value = 1.0f; };
struct Color_G { float value = 1.0f; };
struct Color_B { float value = 1.0f; };
struct Normal_X { float value = 0.0f; };
struct Normal_Y { float value = 1.0f; };
struct Normal_Z { float value = 0.0f; };
struct Material { uint32_t id = 0; };
struct Emission_R { float value = 0.0f; };
struct Emission_G { float value = 0.0f; };
struct Emission_B { float value = 0.0f; };
struct Emission_Intensity { float value = 0.0f; };
```

**Why split vec3 into 3 components?**
- **SoA optimization**: Each float stored contiguously
- **SIMD-friendly**: Process 4-8 R values in parallel
- **Cache efficiency**: Better locality than {r,g,b} structs

### Chunk Metadata (Optional)

```cpp
/**
 * Chunk reference - links voxel to parent chunk.
 * Only needed if tracking chunk ownership.
 */
struct ChunkID {
    uint32_t id = 0;
};
```

---

## Spatial Queries via Morton Codes

### Query Voxels in AABB Region

```cpp
std::vector<gaia::ecs::Entity> queryRegion(
    const glm::vec3& min,
    const glm::vec3& max) {

    // Compute Morton range
    uint64_t minMorton = mortonCode(min);
    uint64_t maxMorton = mortonCode(max);

    std::vector<gaia::ecs::Entity> results;

    // Query all entities with MortonKey in range
    auto query = world.query().all<MortonKey>();
    query.each([&](gaia::ecs::Entity entity) {
        uint64_t morton = entity.get<MortonKey>().code;

        // Morton codes preserve spatial locality
        // Check if in range (with bit masking for precision)
        if (mortonInRange(morton, minMorton, maxMorton)) {
            results.push_back(entity);
        }
    });

    return results;
}
```

**Optimization:** Pre-sort entities by Morton code for O(log N) range queries.

---

## Chunk Insertion Workflow

### Input: Chunk origin + DynamicVoxelScalar array (sparse)

```cpp
void insertChunk(
    const glm::ivec3& chunkOrigin,
    const std::vector<VoxelData>& sparseVoxels,  // Only solid voxels!
    const AttributeRegistry* registry) {

    // Batch entity creation
    std::vector<gaia::ecs::Entity> entities;
    entities.reserve(sparseVoxels.size());

    for (const auto& voxel : sparseVoxels) {
        // Skip if not solid (should already be filtered, but double-check)
        if (!voxel.attributes.passesKeyPredicate(registry)) {
            continue;
        }

        // Compute Morton code from local + chunk position
        glm::ivec3 worldPos = chunkOrigin + voxel.localPos;
        uint64_t morton = mortonCode(worldPos);

        // Create entity
        auto entity = world.add();
        entity.add<MortonKey>(MortonKey{morton});

        // Add dynamic attributes from DynamicVoxelScalar
        addAttributesFromDynamic(entity, voxel.attributes, registry);

        entities.push_back(entity);
    }

    std::cout << "[GaiaVoxelWorld] Inserted " << entities.size()
              << " sparse voxels for chunk " << chunkOrigin << "\n";
}
```

---

## AttributeRegistry Integration

### Attribute Name → Component Mapping

```cpp
class GaiaVoxelWorld {
private:
    // Map AttributeRegistry attribute names to Gaia component adders
    std::unordered_map<std::string, std::function<void(Entity, const std::any&)>> attributeAdders;

    void initializeAttributeMapping() {
        // Float attributes
        attributeAdders["density"] = [](Entity e, const std::any& val) {
            e.add<Density>(Density{std::any_cast<float>(val)});
        };

        attributeAdders["material"] = [](Entity e, const std::any& val) {
            e.add<Material>(Material{std::any_cast<uint32_t>(val)});
        };

        // Vec3 attributes (split into RGB or XYZ)
        attributeAdders["color"] = [](Entity e, const std::any& val) {
            glm::vec3 color = std::any_cast<glm::vec3>(val);
            e.add<Color_R>(Color_R{color.r});
            e.add<Color_G>(Color_G{color.g});
            e.add<Color_B>(Color_B{color.b});
        };

        attributeAdders["normal"] = [](Entity e, const std::any& val) {
            glm::vec3 normal = std::any_cast<glm::vec3>(val);
            e.add<Normal_X>(Normal_X{normal.x});
            e.add<Normal_Y>(Normal_Y{normal.y});
            e.add<Normal_Z>(Normal_Z{normal.z});
        };

        attributeAdders["emission"] = [](Entity e, const std::any& val) {
            glm::vec4 emis = std::any_cast<glm::vec4>(val);  // RGB + intensity
            e.add<Emission_R>(Emission_R{emis.r});
            e.add<Emission_G>(Emission_G{emis.g});
            e.add<Emission_B>(Emission_B{emis.b});
            e.add<Emission_Intensity>(Emission_Intensity{emis.a});
        };
    }

    void addAttributesFromDynamic(
        gaia::ecs::Entity entity,
        const DynamicVoxelScalar& voxel,
        const AttributeRegistry* registry) {

        for (const auto& attrName : voxel.getAttributeNames()) {
            auto it = attributeAdders.find(attrName);
            if (it != attributeAdders.end()) {
                // Get value from DynamicVoxelScalar
                std::any value = getAttributeValue(voxel, attrName, registry);

                // Add corresponding Gaia component(s)
                it->second(entity, value);
            } else {
                // Attribute not in fixed pool - ignore or warn
                std::cerr << "[Warning] Attribute '" << attrName
                          << "' not supported in GaiaVoxelWorld\n";
            }
        }
    }
};
```

---

## VoxelInjectionQueue Integration

### Queue Entry (Sparse)

```cpp
struct VoxelEntry {
    uint64_t mortonCode;            // 8 bytes (position encoded)
    gaia::ecs::Entity entityID;     // 8 bytes
};  // Total: 16 bytes vs 64+ bytes (DynamicVoxelScalar)
```

### Enqueue Workflow

```cpp
// Producer thread
void enqueue(const glm::vec3& pos, const DynamicVoxelScalar& voxel) {
    // Check if solid (sparse filtering)
    if (!voxel.passesKeyPredicate(registry)) {
        return;  // Skip empty voxels
    }

    // Create entity in GaiaVoxelWorld (sparse)
    auto entity = gaiaWorld.createVoxelFromDynamic(pos, voxel, registry);

    // Compute Morton code
    uint64_t morton = mortonCode(pos);

    // Enqueue sparse entry
    queue.enqueue(morton, entity);  // 16 bytes, not 64+
}
```

### Worker Thread (Chunk-Level Batching)

```cpp
void processQueue() {
    auto batch = queue.dequeueBatch(512);

    // Group by chunk coordinate (upper bits of Morton code)
    std::unordered_map<uint32_t, std::vector<gaia::ecs::Entity>> chunks;

    for (const auto& entry : batch) {
        // Extract chunk ID from Morton code (e.g., upper 15 bits)
        uint32_t chunkID = entry.mortonCode >> 48;  // Chunk-level Morton
        chunks[chunkID].push_back(entry.entityID);
    }

    // Process each chunk (parallel possible)
    for (const auto& [chunkID, entities] : chunks) {
        processChunkEntities(chunkID, entities);
    }
}
```

---

## Memory Comparison

### Dense (Old Approach)
```
8³ chunk = 512 voxels
Each voxel:
- Position: 12 bytes
- Density: 4 bytes
- Color: 12 bytes
- Normal: 12 bytes
Total: 40 bytes × 512 = 20,480 bytes = 20 KB
```

### Sparse + Morton (New Approach)
```
8³ chunk with 10% occupancy = 51 solid voxels
Each solid voxel:
- MortonKey: 8 bytes
- Density: 4 bytes
- Color_R/G/B: 12 bytes (3×4)
- Normal_X/Y/Z: 12 bytes (3×4)
Total: 36 bytes × 51 = 1,836 bytes = 1.8 KB

Savings: 20 KB → 1.8 KB (11× reduction)
```

---

## Implementation Checklist

### Phase 1: Morton-Only Storage
- [x] Define `MortonKey` component with encode/decode
- [ ] Remove `Position` component entirely
- [ ] Update `createVoxel()` to use Morton codes
- [ ] Implement Morton-based spatial queries

### Phase 2: Sparse Filtering
- [ ] Add `passesKeyPredicate()` check in `insertChunk()`
- [ ] Only create entities for solid voxels
- [ ] Update VoxelInjectionQueue to skip empty voxels

### Phase 3: AttributeRegistry Integration
- [ ] Implement attribute name → component mapping
- [ ] Add `createVoxelFromDynamic(DynamicVoxelScalar)`
- [ ] Handle vec3 splitting (color → R/G/B)
- [ ] Implement `IAttributeRegistryObserver`

### Phase 4: Chunk-Level Operations
- [ ] Batch entity creation for chunks
- [ ] Morton-based chunk grouping in queue
- [ ] Parallel chunk processing

---

## Key Design Benefits

1. **Sparse-only storage** - Only allocate entities for solid voxels (10-50% occupancy)
2. **No redundant position** - Morton code encodes position implicitly (8 bytes vs 20 bytes)
3. **SoA-optimized attributes** - Split vec3 into 3 components for SIMD
4. **O(log N) spatial queries** - Morton codes enable efficient range searches
5. **Cache-friendly** - Contiguous float arrays, not scattered structs

**Result:** 10-20× memory reduction compared to dense storage.
