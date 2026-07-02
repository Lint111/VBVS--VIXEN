# Hardware RT Pipeline - Handoff Summary (2025-12-07)

## Current Status: ✅ Major Fixes Complete, Testing In Progress

The RTX hardware ray tracing pipeline has had several critical issues fixed. The pipeline now builds and runs, but testing is still in progress.

---

## Completed Fixes

### 1. Frame Index Out of Bounds Crash (Critical)
**Problem:** `TraceRaysNode::framesInFlight_ = 2` but `FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT = 4`
**Solution:** Changed to use the config constant directly
```cpp
// TraceRaysNode.h:93
static constexpr uint32_t framesInFlight_ = FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT;
```
**Files:** [TraceRaysNode.h](libraries/RenderGraph/include/Nodes/TraceRaysNode.h#L93)

### 2. Push Constant Stage Flags Mismatch
**Problem:** `vkCmdPushConstants` used `VK_SHADER_STAGE_RAYGEN_BIT_KHR` only, but pipeline layout declared all RT stages
**Solution:** Store push constant config in `RayTracingPipelineData` and use dynamically
```cpp
// RayTracingPipelineNodeConfig.h - Added to struct:
VkShaderStageFlags pushConstantStages = 0;
uint32_t pushConstantOffset = 0;
uint32_t pushConstantSize = 0;

// TraceRaysNode.cpp - Now uses:
pipelineData_->pushConstantStages
```
**Files:** 
- [RayTracingPipelineNodeConfig.h](libraries/RenderGraph/include/Data/Nodes/RayTracingPipelineNodeConfig.h#L68-L70)
- [RayTracingPipelineNode.cpp](libraries/RenderGraph/src/Nodes/RayTracingPipelineNode.cpp#L391-L394)
- [TraceRaysNode.cpp](libraries/RenderGraph/src/Nodes/TraceRaysNode.cpp#L197-L207)

### 3. Storage Image Format Mismatch
**Problem:** Shader declared `layout(binding = 0, rgba8)` but swapchain uses `VK_FORMAT_B8G8R8A8_UNORM`
**Solution:** Use format-less storage image with `writeonly` qualifier
```glsl
// VoxelRT.rgen:13
layout(binding = 0) uniform writeonly image2D outputImage;
```
**Also enabled:** `shaderStorageImageWriteWithoutFormat` feature in [VulkanDevice.cpp](libraries/VulkanResources/src/VulkanDevice.cpp#L104-L105)

### 4. Intersection Shader AABB Lookup (Voxel Topology Fix)
**Problem:** Intersection shader tested against hardcoded unit cube instead of actual AABB bounds
**Solution:** Added AABB buffer binding for `gl_PrimitiveID` lookup
```glsl
// VoxelRT.rint - Added:
layout(binding = 2, set = 0, scalar) readonly buffer AABBBuffer {
    AABB aabbs[];
} aabbBuffer;

// In main():
AABB aabb = aabbBuffer.aabbs[gl_PrimitiveID];
```
**Files:**
- [VoxelRT.rint](shaders/VoxelRT.rint#L20-L23)
- [VoxelAABBConverterNodeConfig.h](libraries/RenderGraph/include/Data/Nodes/VoxelAABBConverterNodeConfig.h#L98-L101) - Added `AABB_BUFFER` output
- [VoxelAABBConverterNode.cpp](libraries/RenderGraph/src/Nodes/VoxelAABBConverterNode.cpp#L95-L96) - Outputs raw buffer
- [BenchmarkGraphFactory.cpp](libraries/Profiler/src/BenchmarkGraphFactory.cpp#L2164-L2169) - Wires buffer to descriptor

### 5. SDI Regeneration
**Problem:** SDI didn't include intersection shader bindings
**Solution:** Created batch config and regenerated SDI with all RT stages
```bash
./build/bin/Debug/sdi_tool.exe batch shaders/VoxelRT_batch.json --output-dir ./generated/sdi
```
**Files:**
- [VoxelRT_batch.json](shaders/VoxelRT_batch.json) - Batch config for RT shaders
- [VoxelRTNames.h](generated/sdi/VoxelRTNames.h) - Now includes `VoxelRT::aabbBuffer::BINDING`

---

## Key SDI References
```cpp
#include "VoxelRTNames.h"

// Descriptor bindings
VoxelRT::outputImage::BINDING  // = 0
VoxelRT::topLevelAS::BINDING   // = 1  
VoxelRT::aabbBuffer::BINDING   // = 2

// Push constants
VoxelRT::SDI::pc::STAGES       // Stage flags from reflection
VoxelRT::SDI::pc::SIZE         // = 64 bytes
```

---

## Test Command
```bash
cd C:/cpp/VBVS--VIXEN/VIXEN
./binaries/vixen_benchmark.exe --pipelines hardware_rt --resolutions 32 --densities 50 --render --width 256 --height 256
```

---

## Remaining Work / Known Issues

### 1. Validation Testing Incomplete
The pipeline runs but full validation error analysis was interrupted. Run with validation layers enabled to check for remaining issues.

### 2. Verify Voxel Topology Display
The intersection shader now looks up actual AABB bounds, but visual verification of correct voxel topology display is needed.

### 3. SDI Push Constant Stages
The SDI reports `VK_SHADER_STAGE_ALL` for push constants, but the pipeline uses specific RT stages. The SDI generator may need adjustment to reflect actual stage usage from ray tracing pipelines.

---

## Architecture Notes

### Descriptor Flow (RT Pipeline)
```
SwapchainNode.CURRENT_FRAME_IMAGE_VIEW ──┬─→ DescriptorResourceGathererNode ─→ DescriptorSetNode ─→ TraceRaysNode
AccelerationStructureNode.TLAS_HANDLE ───┤
VoxelAABBConverterNode.AABB_BUFFER ──────┘
```

### Push Constant Flow
```
CameraNode ─→ PushConstantGathererNode ─→ TraceRaysNode
                                          (uses pipelineData_->pushConstantStages)
```

---

## Files Modified This Session
| File | Change |
|------|--------|
| `TraceRaysNode.h` | Use `FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT` |
| `TraceRaysNode.cpp` | Get push constant stages from pipeline data |
| `RayTracingPipelineNodeConfig.h` | Added push constant fields to `RayTracingPipelineData` |
| `RayTracingPipelineNode.cpp` | Populate push constant config in pipeline data |
| `VoxelRT.rgen` | Use format-less `writeonly image2D` |
| `VoxelRT.rint` | Add AABB buffer lookup via `gl_PrimitiveID` |
| `VulkanDevice.cpp` | Enable `shaderStorageImageWriteWithoutFormat` |
| `VoxelAABBConverterNodeConfig.h` | Add `AABB_BUFFER` output slot |
| `VoxelAABBConverterNode.cpp` | Output raw AABB buffer |
| `BenchmarkGraphFactory.cpp` | Wire AABB buffer to descriptor gatherer |
| `VoxelRT_batch.json` | New file for SDI generation |
| `VoxelRTNames.h` | Regenerated with all RT shader bindings |
# Files Modified This Session


---

## Material Color Fix (Continued Session)

### Problem
The hardware RT pipeline was rendering voxels with per-primitive hash-based colors instead of the actual material colors from the voxel grid.

### Solution
Added a material ID buffer indexed by `gl_PrimitiveID` to pass material IDs to the closest-hit shader.

#### Changes Made

**1. VoxelAABBConverterNodeConfig.h**
- Added `MATERIAL_ID_BUFFER` output slot (index 2)
- Added material buffer fields to `VoxelAABBData` struct:
  - `materialIdBuffer: VkBuffer`
  - `materialIdBufferMemory: VkDeviceMemory`
  - `materialIdBufferSize: VkDeviceSize`

**2. VoxelAABBConverterNode.h/cpp**
- Modified `ExtractAABBsFromGrid()` to also return material IDs
- Added `CreateMaterialIdBuffer()` and `UploadMaterialIdData()` methods
- Updated `DestroyAABBBuffer()` to clean up material buffer resources

**3. VoxelRT.rchit (Closest-Hit Shader)**
- Added `MaterialIdBuffer` SSBO binding at slot 3
- Added `getMaterialColor()` function matching compute shader material palette
- Replaced hash-based coloring with material ID lookup via `gl_PrimitiveID`

**4. BenchmarkGraphFactory.cpp**
- Added wiring for `MATERIAL_ID_BUFFER` to descriptor gatherer at binding 3

**5. SDI Regenerated**
- VoxelRTNames.h now includes `VoxelRT::materialIdBuffer::BINDING` (binding 3)

### Testing
Build successful. Hardware RT pipeline now has access to material colors via:
```
gl_PrimitiveID → materialIdBuffer → getMaterialColor(matID)
```

Material color lookup uses same palette as compute shader:
- matID 1: Red
- matID 2: Green  
- matID 3: Light gray (white wall)
- matID 4: Yellow/Gold
- matID 5-6: Gray variants
