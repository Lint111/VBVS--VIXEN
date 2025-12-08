---
title: Scene Data Cacher Design
tags: [design, caching, optimization, implemented, complete]
created: 2025-12-08
status: complete
updated: 2025-12-08
---

# Scene Data Cacher Design

**STATUS: COMPLETE** - Both cachers are fully integrated and operational.

Design for caching voxel scene data to speed up benchmark test setup.

## Problem

Current flow rebuilds everything per test:
```
VoxelGrid → Octree → Compression → GPU Buffers → (RT only) AABBs → AccelStruct
```

For 180-config benchmark, same scene/resolution/density combo is rebuilt multiple times across pipeline variants.

## Solution: Two Cachers

### 1. VoxelSceneCacher

**Key:** `(sceneType, resolution, density)` → hash

**Cached Data (CPU + GPU buffers):**

| Data | Type | Description |
|------|------|-------------|
| esvoNodes | `std::vector<uint8_t>` + VkBuffer | Octree node array |
| brickData | `std::vector<uint8_t>` + VkBuffer | Raw brick voxels |
| materials | `std::vector<MaterialData>` + VkBuffer | Material definitions |
| compressedColors | `std::vector<uint8_t>` + VkBuffer | DXT1 color blocks |
| compressedNormals | `std::vector<uint8_t>` + VkBuffer | DXT normal blocks |
| octreeConfig | OctreeConfig + VkBuffer | UBO data |
| brickGridLookup | `std::vector<uint32_t>` + VkBuffer | Grid→brick mapping |

**CreateInfo:**
```cpp
struct VoxelSceneCreateInfo {
    SceneType sceneType;
    uint32_t resolution;
    float density;  // 0.0-1.0
    
    uint64_t ComputeHash() const {
        return hash_combine(sceneType, resolution, 
                           static_cast<uint32_t>(density * 100));
    }
};
```

**Resource Wrapper:**
```cpp
struct VoxelSceneData {
    // CPU-side data (for re-upload if needed)
    std::vector<uint8_t> esvoNodesCPU;
    std::vector<uint8_t> brickDataCPU;
    std::vector<MaterialData> materialsCPU;
    std::vector<uint8_t> compressedColorsCPU;
    std::vector<uint8_t> compressedNormalsCPU;
    OctreeConfig configCPU;
    std::vector<uint32_t> brickGridLookupCPU;
    
    // GPU buffers
    VkBuffer esvoNodesBuffer = VK_NULL_HANDLE;
    VkBuffer brickDataBuffer = VK_NULL_HANDLE;
    VkBuffer materialsBuffer = VK_NULL_HANDLE;
    VkBuffer compressedColorsBuffer = VK_NULL_HANDLE;
    VkBuffer compressedNormalsBuffer = VK_NULL_HANDLE;
    VkBuffer octreeConfigBuffer = VK_NULL_HANDLE;
    VkBuffer brickGridLookupBuffer = VK_NULL_HANDLE;
    
    // Memory allocations
    VkDeviceMemory memory = VK_NULL_HANDLE;  // Single allocation for all
    
    bool IsValid() const;
    void Cleanup(VkDevice device);
};
```

---

### 2. AccelerationStructureCacher

**Key:** `(VoxelSceneData*, buildFlags)` → hash

Uses scene data pointer + RT build flags as key. Different build flags (PREFER_FAST_TRACE, ALLOW_COMPACTION) produce different AS.

**Cached Data:**
```cpp
struct CachedAccelerationStructure {
    // From VoxelAABBConverterNode
    VoxelAABBData aabbData;
    
    // From AccelerationStructureNode
    AccelerationStructureData accelStruct;
    
    bool IsValid() const;
    void Cleanup(VkDevice device);
};
```

**CreateInfo:**
```cpp
struct AccelStructCreateInfo {
    VoxelSceneData* sceneData;  // Pointer to cached scene
    bool preferFastTrace = true;
    bool allowUpdate = false;
    bool allowCompaction = true;
    
    uint64_t ComputeHash() const {
        return hash_combine(
            reinterpret_cast<uintptr_t>(sceneData),
            preferFastTrace, allowUpdate, allowCompaction
        );
    }
};
```

---

## Integration with VoxelGridNode

```cpp
// In VoxelGridNode::CompileImpl()
auto& sceneCacher = MainCacher::Get<VoxelSceneCacher>();

VoxelSceneCreateInfo ci{
    .sceneType = params.sceneType,
    .resolution = params.resolution,
    .density = params.density
};

auto sceneData = sceneCacher.GetOrCreate(ci);

// Output cached buffers instead of creating new ones
ctx.Out<OCTREE_NODES_BUFFER>(sceneData->esvoNodesBuffer);
ctx.Out<OCTREE_BRICKS_BUFFER>(sceneData->brickDataBuffer);
// ... etc
```

---

## Integration with AccelerationStructureNode

```cpp
// In AccelerationStructureNode::CompileImpl()
auto& asCacher = MainCacher::Get<AccelerationStructureCacher>();

AccelStructCreateInfo ci{
    .sceneData = inputSceneData,
    .preferFastTrace = params.preferFastTrace
};

auto cached = asCacher.GetOrCreate(ci);

ctx.Out<ACCELERATION_STRUCTURE_DATA>(&cached->accelStruct);
ctx.Out<TLAS_HANDLE>(cached->accelStruct.tlas);
```

---

## Benchmark Speedup

### Before (per test):
1. Generate VoxelGrid (~100ms)
2. Build octree (~50ms)
3. Compress DXT (~200ms)
4. Upload to GPU (~20ms)
5. Build AABBs (~10ms)
6. Build BLAS/TLAS (~50ms)

**Total: ~430ms per test × 180 tests = 77 seconds setup**

### After (with caching):
- First test per unique config: ~430ms
- Subsequent tests same config: ~1ms (cache hit)

**Unique configs: 4 scenes × 4 resolutions × 5 densities = 80**
**Total: 80 × 430ms + 100 × 1ms = 34 seconds setup**

**Speedup: ~2.3x** (more if same scene used across pipeline variants)

---

## File Structure

```
libraries/CashSystem/
├── include/
│   ├── VoxelSceneCacher.h      # NEW
│   └── AccelerationStructureCacher.h  # NEW
├── src/
│   ├── VoxelSceneCacher.cpp    # NEW
│   └── AccelerationStructureCacher.cpp  # NEW
```

---

## Implementation Order

1. [x] Define `VoxelSceneData` and `VoxelSceneCreateInfo` structs
2. [x] Implement `VoxelSceneCacher : TypedCacher<VoxelSceneData, VoxelSceneCreateInfo>`
3. [x] Register in MainCacher (via `RegisterVoxelSceneCacher()` helper)
4. [x] Modify VoxelGridNode to use cacher
5. [x] Define `CachedAccelerationStructure` and `AccelStructCreateInfo`
6. [x] Implement `AccelerationStructureCacher`
7. [x] Modify AccelerationStructureNode to use cacher
8. [ ] Add tests (deferred to post-benchmark validation)

---

## Implementation Notes (2025-12-08)

### Files Created

- `libraries/CashSystem/include/VoxelSceneCacher.h`
- `libraries/CashSystem/src/VoxelSceneCacher.cpp`
- `libraries/CashSystem/include/AccelerationStructureCacher.h`
- `libraries/CashSystem/src/AccelerationStructureCacher.cpp`

### Current Status: COMPLETE ✅

Both cachers are fully integrated and operational. Legacy manual creation paths in the nodes have been disabled via `#if 0 ... #endif` blocks.

**VoxelSceneCacher handles:**
- Scene generation via `SceneGeneratorFactory`
- `GaiaVoxelWorld` population
- `LaineKarrasOctree` building
- DXT compression
- GPU buffer creation and upload
- OctreeConfig UBO
- Brick grid lookup buffer

**AccelerationStructureCacher handles:**
- AABB conversion from scene data
- BLAS creation
- TLAS creation with instance buffer
- Device address management

### Node Integration

**VoxelGridNode:**
- Registers `VoxelSceneCacher` on-demand (idempotent)
- Legacy `GenerateProceduralScene()`, `ExtractNodeData()`, `UploadOctreeBuffers()`, `UploadESVOBuffers()` wrapped in `#if 0`
- `DestroyOctreeBuffers()` releases `shared_ptr` to cacher-owned resources

**AccelerationStructureNode:**
- Registers `AccelerationStructureCacher` on-demand (idempotent)
- `VOXEL_SCENE_DATA` input now **required** (was optional)
- Legacy `BuildBLAS()`, `BuildTLAS()`, `CreateInstanceBuffer()`, etc. wrapped in `#if 0`
- `DestroyAccelerationStructures()` releases `shared_ptr` to cacher-owned resources

### Registration

Cachers are registered on-demand by their respective nodes during `CompileImpl()`:

```cpp
// VoxelGridNode::CompileImpl()
CashSystem::RegisterVoxelSceneCacher();  // Idempotent

// AccelerationStructureNode::CompileImpl()
CashSystem::RegisterAccelerationStructureCacher();  // Idempotent
```

### Future Cleanup

After benchmark validation confirms stability, remove the `#if 0` legacy blocks from:
- `libraries/RenderGraph/src/Nodes/VoxelGridNode.cpp`
- `libraries/RenderGraph/src/Nodes/AccelerationStructureNode.cpp`

---

## Related

- [[../Libraries/CashSystem|CashSystem Library]]
- [[../02-Implementation/VoxelGridNode|VoxelGridNode]]
- [[../03-Research/Hardware-RT|Hardware RT Pipeline]]
