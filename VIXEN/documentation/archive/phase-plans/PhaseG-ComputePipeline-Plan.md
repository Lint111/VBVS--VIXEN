# Phase G: Compute Shader Pipeline - Implementation Plan

**Date**: November 2, 2025
**Status**: Planning Complete - Ready to Implement
**Duration**: 2-3 weeks (26-34 hours)
**Priority**: HIGHEST - Research Foundation
**Branch**: `claude/phase-g-compute-pipeline`

---

## Executive Summary

Phase G implements the simplest voxel ray marching pipeline using compute shaders. This serves as:
1. **Validation baseline** - Proves profiling methodology before tackling hardware RT
2. **Visual confirmation** - Renders voxel cube to screen (correctness check)
3. **Performance baseline** - Establishes metrics for comparing future pipelines

**Core Innovation**: Data-driven compute pipeline creation using existing ShaderManagement reflection infrastructure.

### Architectural Decision: Generalized Nodes ✅

**Key Design Choice**: Generic `ComputeDispatchNode` instead of specialized `ComputeRayMarchNode`.

**Rationale**:
- **Separation of Concerns**: Pipeline creation (ComputePipelineNode) ≠ Dispatch logic (ComputeDispatchNode)
- **Reusability**: Same dispatcher works for ray marching, voxel generation, post-processing, etc.
- **Research-Ready**: Easy to swap shaders for Phase L algorithm comparisons
- **Matches Existing Pattern**: GraphicsPipelineNode creates, GeometryRenderNode dispatches

**Node Chain**:
```
ShaderLibraryNode → ComputePipelineNode → ComputeDispatchNode → TimestampQueryNode
```

Ray marching becomes **application-level graph wiring**, not node-level logic.

---

## Prerequisites ✅

**Completed**:
- ✅ Phase F: Bundle-first organization (build successful)
- ✅ ShaderManagement: SPIRV reflection, descriptor layout automation
- ✅ CashSystem: Caching infrastructure with MainCacher registry
- ✅ Existing shader: `Shaders/VoxelRayMarch.comp` (245 lines, DDA traversal)

**Ready to Build On**:
- GraphicsPipelineNode pattern (data-driven pipeline creation)
- DescriptorSetLayoutCacher (automatic descriptor layout generation)
- PipelineLayoutCacher (layout sharing and caching)
- ShaderLibraryNode (shader loading and reflection)

---

## Implementation Tasks

### G.1: ComputePipelineNode (8-10 hours) 🎯

**Goal**: Create VkComputePipeline with automatic descriptor layout generation.

#### Files to Create:
```cpp
RenderGraph/include/Nodes/ComputePipelineNodeConfig.h   // Config (2h)
RenderGraph/src/Nodes/ComputePipelineNode.cpp            // Implementation (6-8h)
```

#### Config Design: ComputePipelineNodeConfig.h

**Inputs** (3):
- `VULKAN_DEVICE_IN` (VulkanDevicePtr) - Device for pipeline creation
- `SHADER_DATA_BUNDLE` (ShaderDataBundlePtr) - SPIRV reflection from ShaderLibraryNode
- `DESCRIPTOR_SET_LAYOUT` (VkDescriptorSetLayout) - Optional (auto-generated if not provided)

**Outputs** (4):
- `PIPELINE` (VkComputePipeline) - Compute pipeline handle
- `PIPELINE_LAYOUT` (VkPipelineLayout) - Pipeline layout handle
- `PIPELINE_CACHE` (VkPipelineCache) - Pipeline cache for reuse
- `VULKAN_DEVICE_OUT` (VulkanDevicePtr) - Device passthrough

**Parameters**:
- `WORKGROUP_SIZE_X` (uint32_t) - Default: 8 (extracted from shader if 0)
- `WORKGROUP_SIZE_Y` (uint32_t) - Default: 8
- `WORKGROUP_SIZE_Z` (uint32_t) - Default: 1

**Pattern**: Follow GraphicsPipelineNodeConfig.h structure (INPUT_SLOT, OUTPUT_SLOT macros).

#### Implementation: ComputePipelineNode.cpp

**CompileImpl() Logic**:
1. Get device and shader bundle from inputs
2. Extract compute shader stage from bundle reflection
3. Auto-generate descriptor set layout (if not provided):
   ```cpp
   if (!In(DESCRIPTOR_SET_LAYOUT)) {
       descriptorSetLayout = DescriptorSetLayoutCacher::GetOrCreate(device, bundle);
   }
   ```
4. Extract push constants from bundle reflection
5. Create pipeline layout via PipelineLayoutCacher
6. Extract workgroup size from SPIRV (if parameters are 0):
   ```cpp
   // Use bundle->GetWorkgroupSize() or SPIRV reflection
   uint32_t wgX = GetParameter(WORKGROUP_SIZE_X, 0);
   if (wgX == 0) wgX = ExtractWorkgroupSizeX(bundle);
   ```
7. Create compute pipeline via ComputePipelineCacher (NEW - G.1b)
8. Set outputs

**ExecuteImpl() Logic**:
- No-op (pipeline creation is compile-time only)

**CleanupImpl() Logic**:
- No explicit cleanup (cachers handle VkPipeline/VkPipelineLayout destruction)

#### Sub-task G.1b: ComputePipelineCacher (3-4h)

**Files to Create**:
```cpp
CashSystem/include/CashSystem/ComputePipelineCacher.h
CashSystem/src/ComputePipelineCacher.cpp
```

**Design Pattern**: Follow PipelineCacher.h structure.

**Key Differences from Graphics Pipeline**:
- Single shader stage (compute)
- No vertex input state
- No rasterization state
- No depth/stencil state
- No color blend state
- Workgroup size metadata (for dispatch calculations)

**CreateParams**:
```cpp
struct ComputePipelineCreateParams {
    VkShaderModule shaderModule;
    const char* entryPoint;           // "main"
    VkPipelineLayout pipelineLayout;
    uint32_t workgroupSizeX;          // Metadata for dispatch
    uint32_t workgroupSizeY;
    uint32_t workgroupSizeZ;

    bool operator==(const ComputePipelineCreateParams& other) const;
    size_t Hash() const;
};
```

**Cache Key**: Hash of (shaderModule, entryPoint, pipelineLayout, workgroup sizes)

**CreatePipeline()**:
```cpp
VkComputePipelineCreateInfo pipelineInfo{};
pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
pipelineInfo.stage = shaderStageInfo;
pipelineInfo.layout = params.pipelineLayout;

VkPipeline pipeline;
vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
```

**Success Criteria**:
- ✅ ComputePipelineNode compiles without errors
- ✅ VkComputePipeline created successfully
- ✅ Cache HIT on second compilation
- ✅ Descriptor layout auto-generated correctly

---

### G.2: Storage Resource Support (4-6 hours) 📦

**Goal**: Extend ResourceVariant to support storage buffers/images for compute shaders.

#### File to Modify:
```cpp
RenderGraph/include/Core/ResourceVariant.h  // Add new types (4-6h)
```

#### New Resource Types:

**Storage Buffer View**:
```cpp
REGISTER_RESOURCE_TYPE(VkBufferView, BufferViewDescriptor)
```

**Storage Image** (uses existing VkImageView):
- Already registered as `VkImageView`
- Need to add `StorageImageDescriptor` variant:
```cpp
struct StorageImageDescriptor {
    uint32_t width;
    uint32_t height;
    VkFormat format;           // VK_FORMAT_R8G8B8A8_UNORM
    VkImageLayout layout;      // VK_IMAGE_LAYOUT_GENERAL
    VkImageUsageFlags usage;   // VK_IMAGE_USAGE_STORAGE_BIT
};

REGISTER_RESOURCE_TYPE(VkImageView, StorageImageDescriptor)  // Duplicate registration OK
```

**3D Texture for Voxel Data**:
```cpp
struct Texture3DDescriptor {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    VkFormat format;
    VkImageLayout layout;
    VkImageUsageFlags usage;   // VK_IMAGE_USAGE_SAMPLED_BIT
};

REGISTER_RESOURCE_TYPE(VkImageView, Texture3DDescriptor)
```

#### Testing:
- Create simple storage image (256x256 RGBA8)
- Create 3D texture (64³ voxels)
- Verify descriptor updates work with new types

**Success Criteria**:
- ✅ VkBufferView, StorageImageDescriptor, Texture3DDescriptor registered
- ✅ No compilation errors or warnings
- ✅ Descriptor set updates accept new types

---

### G.3: ComputeDispatchNode - Generic Compute Dispatcher (6-8 hours) 🎨

**Goal**: Create generic node that dispatches ANY compute shader (not ray marching specific).

**Architecture Decision**: Generalized dispatcher matches existing pattern (GraphicsPipelineNode creates, GeometryRenderNode dispatches). Ray marching becomes application-level graph wiring, not node-level logic.

#### Files to Create:
```cpp
RenderGraph/include/Nodes/ComputeDispatchNodeConfig.h   // Generic config (2h)
RenderGraph/src/Nodes/ComputeDispatchNode.cpp           // Generic dispatcher (4-6h)
```

#### Design Philosophy:
- **Separation of Concerns**: Pipeline creation (G.1) ≠ Dispatch logic (G.3)
- **Reusability**: Works with ANY compute shader (ray marching, voxel generation, post-processing)
- **Data-Driven**: Shader selected via graph wiring, not hardcoded in node
- **Research-Ready**: Easy to swap algorithms for Phase L testing

#### Config Design: ComputeDispatchNodeConfig.h

**Inputs** (6):
- `VULKAN_DEVICE_IN` (VulkanDevicePtr) - Device for command buffer allocation
- `COMMAND_POOL` (VkCommandPool) - Pool for command buffer allocation
- `COMPUTE_PIPELINE` (VkComputePipeline) - From ComputePipelineNode
- `PIPELINE_LAYOUT` (VkPipelineLayout) - From ComputePipelineNode
- `DESCRIPTOR_SETS` (VkDescriptorSet*) - Array of descriptor sets (generic)
- `PUSH_CONSTANTS` (void*) - Optional push constant data

**Outputs** (2):
- `COMMAND_BUFFER` (VkCommandBuffer) - Recorded dispatch command
- `VULKAN_DEVICE_OUT` (VulkanDevicePtr) - Device passthrough

**Parameters**:
- `DISPATCH_X` (uint32_t) - Workgroups in X dimension (default: 1)
- `DISPATCH_Y` (uint32_t) - Workgroups in Y dimension (default: 1)
- `DISPATCH_Z` (uint32_t) - Workgroups in Z dimension (default: 1)
- `PUSH_CONSTANT_SIZE` (uint32_t) - Size of push constants in bytes (default: 0)
- `DESCRIPTOR_SET_COUNT` (uint32_t) - Number of descriptor sets (default: 1)

#### Implementation: ComputeDispatchNode.cpp

**CompileImpl() Logic**:
1. Get dispatch parameters:
   ```cpp
   uint32_t dispatchX = GetParameter(DISPATCH_X, 1);
   uint32_t dispatchY = GetParameter(DISPATCH_Y, 1);
   uint32_t dispatchZ = GetParameter(DISPATCH_Z, 1);
   uint32_t pushConstantSize = GetParameter(PUSH_CONSTANT_SIZE, 0);
   uint32_t descriptorSetCount = GetParameter(DESCRIPTOR_SET_COUNT, 1);
   ```

2. Allocate command buffer from pool:
   ```cpp
   VkCommandBufferAllocateInfo allocInfo{};
   allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
   allocInfo.commandPool = In(COMMAND_POOL);
   allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
   allocInfo.commandBufferCount = 1;
   vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);
   ```

3. Record dispatch command:
   ```cpp
   VkCommandBufferBeginInfo beginInfo{};
   beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
   beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
   vkBeginCommandBuffer(commandBuffer, &beginInfo);

   // Bind pipeline
   vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, In(COMPUTE_PIPELINE));

   // Bind descriptor sets (generic - any shader resources)
   VkDescriptorSet* descriptorSets = In(DESCRIPTOR_SETS);
   vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                           In(PIPELINE_LAYOUT), 0, descriptorSetCount,
                           descriptorSets, 0, nullptr);

   // Push constants (if provided)
   if (pushConstantSize > 0 && In(PUSH_CONSTANTS)) {
       vkCmdPushConstants(commandBuffer, In(PIPELINE_LAYOUT),
                          VK_SHADER_STAGE_COMPUTE_BIT, 0,
                          pushConstantSize, In(PUSH_CONSTANTS));
   }

   // Dispatch (generic - no assumptions about workload)
   vkCmdDispatch(commandBuffer, dispatchX, dispatchY, dispatchZ);

   vkEndCommandBuffer(commandBuffer);
   ```

4. Set outputs:
   ```cpp
   Out(COMMAND_BUFFER, commandBuffer);
   Out(VULKAN_DEVICE_OUT, In(VULKAN_DEVICE_IN));
   ```

**ExecuteImpl() Logic**:
- No-op (command buffer recorded at compile-time, submitted by graph)
- OR submit to queue if EXECUTE_IMMEDIATELY parameter is true (future optimization)

**CleanupImpl() Logic**:
- Free command buffer back to pool

#### Testing Strategy:
1. **Minimal Test**: Simple fill shader, dispatch 1x1x1 workgroup
   ```glsl
   // TestFill.comp
   layout(binding = 0, rgba8) uniform image2D outputImage;
   void main() {
       imageStore(outputImage, ivec2(gl_GlobalInvocationID.xy), vec4(1.0, 0.0, 0.0, 1.0));
   }
   ```
2. **Parameter Test**: Dispatch 16x16x1, verify all workgroups execute
3. **Descriptor Test**: Bind storage image, verify writes work
4. **Push Constants Test**: Pass data to shader, verify shader receives it

**Success Criteria**:
- ✅ Generic dispatch works with ANY compute shader
- ✅ Descriptor sets bind correctly (any type)
- ✅ Push constants work (optional)
- ✅ Dispatch dimensions configurable (X/Y/Z)
- ✅ Zero validation errors
- ✅ Reusable for ray marching, voxel generation, post-processing

---

### G.4: Timestamp Query Integration (6-8 hours) ⏱️

**Goal**: Measure GPU time for compute dispatch with <0.1ms overhead.

#### Files to Create:
```cpp
RenderGraph/include/Nodes/TimestampQueryNodeConfig.h   // Config (2h)
RenderGraph/src/Nodes/TimestampQueryNode.cpp           // Implementation (4-6h)
```

#### Config Design: TimestampQueryNodeConfig.h

**Inputs** (2):
- `VULKAN_DEVICE_IN` (VulkanDevicePtr)
- `COMMAND_BUFFER` (VkCommandBuffer) - Command buffer to wrap with queries

**Outputs** (3):
- `GPU_TIME_NS` (uint64_t*) - Pointer to GPU time in nanoseconds
- `QUERY_POOL` (VkQueryPool) - Query pool handle (for inspection)
- `VULKAN_DEVICE_OUT` (VulkanDevicePtr)

**Parameters**:
- `QUERY_NAME` (string) - Name for logging ("ComputeDispatch")
- `FRAMES_IN_FLIGHT` (uint32_t) - Default: 4 (double-buffered query pools)

#### Implementation: TimestampQueryNode.cpp

**SetupImpl() Logic**:
1. Check timestamp support:
   ```cpp
   VkPhysicalDeviceProperties props;
   vkGetPhysicalDeviceProperties(gpu, &props);
   if (!props.limits.timestampComputeAndGraphics) {
       throw std::runtime_error("Timestamps not supported");
   }
   timestampPeriod = props.limits.timestampPeriod;  // ns per tick
   ```
2. Create query pools (one per frame-in-flight):
   ```cpp
   for (uint32_t i = 0; i < framesInFlight; ++i) {
       VkQueryPoolCreateInfo poolInfo{};
       poolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
       poolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
       poolInfo.queryCount = 2;  // Begin + End

       vkCreateQueryPool(device, &poolInfo, nullptr, &queryPools[i]);
   }
   ```

**CompileImpl() Logic**:
- No-op (query pools created in Setup)

**ExecuteImpl() Logic**:
1. Reset query pool for current frame:
   ```cpp
   uint32_t frameIndex = currentFrameIndex % framesInFlight;
   vkCmdResetQueryPool(cmd, queryPools[frameIndex], 0, 2);
   ```
2. Write timestamp before work:
   ```cpp
   vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queryPools[frameIndex], 0);
   ```
3. Execute work (compute dispatch happens in ComputeRayMarchNode)
4. Write timestamp after work:
   ```cpp
   vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queryPools[frameIndex], 1);
   ```
5. Read results from PREVIOUS frame (avoid stall):
   ```cpp
   uint32_t readFrameIndex = (currentFrameIndex - 1) % framesInFlight;
   uint64_t timestamps[2];
   VkResult result = vkGetQueryPoolResults(
       device,
       queryPools[readFrameIndex],
       0, 2,
       sizeof(timestamps),
       timestamps,
       sizeof(uint64_t),
       VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
   );

   if (result == VK_SUCCESS) {
       gpuTimeNs = (timestamps[1] - timestamps[0]) * timestampPeriod;
       LOG_INFO("GPU Time: " << gpuTimeNs / 1000000.0 << " ms");
   }
   ```
6. Set output pointer

**CleanupImpl() Logic**:
- Destroy all query pools

#### Design Note: Double-Buffered Queries

**Why?** Reading query results causes GPU → CPU stall. By using frame N-1's results while recording frame N, we avoid blocking.

**Pattern**:
- Frame 0: Write queries to pool[0], skip read (no previous data)
- Frame 1: Write queries to pool[1], read pool[0]
- Frame 2: Write queries to pool[2], read pool[1]
- Frame 3: Write queries to pool[3], read pool[2]
- Frame 4: Write queries to pool[0], read pool[3] (wrap around)

**Success Criteria**:
- ✅ GPU time measured accurately (cross-reference with Nsight Graphics)
- ✅ Overhead < 0.1ms per frame
- ✅ No GPU stalls (frame time stable)
- ✅ Zero validation errors

---

## Integration Plan

### Phase G.0: Preparation (1-2h)
1. ✅ Create branch: `claude/phase-g-compute-pipeline`
2. ✅ Write Phase G plan (this document)
3. Create stub CMakeLists.txt entries for new nodes
4. Verify VoxelRayMarch.comp compiles with glslangValidator

### Phase G.1: ComputePipelineNode (8-10h)
1. Create ComputePipelineNodeConfig.h (follow GraphicsPipelineNodeConfig pattern)
2. Create ComputePipelineCacher (header + implementation)
3. Register ComputePipelineCacher in MainCacher
4. Create ComputePipelineNode.cpp (CompileImpl, CleanupImpl)
5. Register node type in NodeTypeRegistry
6. Build and test (minimal: verify pipeline creation)

### Phase G.2: Storage Resource Support (4-6h)
1. Add StorageImageDescriptor to ResourceVariant.h
2. Add Texture3DDescriptor to ResourceVariant.h
3. Add VkBufferView registration
4. Build and verify no warnings
5. Create test storage image (256x256 RGBA8)

### Phase G.3: ComputeDispatchNode (6-8h)
1. Create ComputeDispatchNodeConfig.h (generic config)
2. Create ComputeDispatchNode.cpp (generic dispatcher)
3. Test with simple fill shader (TestFill.comp)
4. Test descriptor set binding
5. Test push constants
6. Verify works with any compute shader

### Phase G.4: Timestamp Queries (6-8h)
1. Create TimestampQueryNodeConfig.h
2. Create TimestampQueryNode.cpp (SetupImpl, ExecuteImpl, CleanupImpl)
3. Integrate with ComputeRayMarchNode command buffer
4. Test: Measure GPU time, verify <0.1ms overhead
5. Cross-reference with Nsight Graphics

### Phase G.5: Ray Marching Integration (4-6h)
**New Task**: Wire up ray marching as application-level graph (not node-level).

1. Create simple test scene resources:
   - Storage image (1280x720 RGBA8, VK_IMAGE_LAYOUT_GENERAL)
   - 3D voxel texture (64³, simple cube data)
   - Camera UBO (view, projection, inverse projection matrices)

2. Create descriptor set for ray marching:
   - Binding 0: Storage image (output)
   - Binding 1: 3D texture (voxel data)
   - Binding 2: UBO (camera)

3. Build application graph (VulkanGraphApplication.cpp):
   ```cpp
   // Load ray marching shader
   auto shaderLib = graph.AddNode<ShaderLibraryNode>("VoxelRayMarchShader");
   shaderLib->SetParameter("SHADER_PATH", "Shaders/VoxelRayMarch.comp");

   // Create compute pipeline
   auto computePipeline = graph.AddNode<ComputePipelineNode>("RayMarchPipeline");
   graph.Connect(shaderLib, SHADER_DATA_BUNDLE, computePipeline, SHADER_DATA_BUNDLE);

   // Create descriptor set
   auto descriptorSet = graph.AddNode<DescriptorSetNode>("RayMarchDescriptors");
   // ... bind storage image, 3D texture, camera UBO

   // Generic compute dispatch
   auto dispatcher = graph.AddNode<ComputeDispatchNode>("RayMarchDispatch");
   dispatcher->SetParameter(DISPATCH_X, (1280 + 7) / 8);  // 160 workgroups
   dispatcher->SetParameter(DISPATCH_Y, (720 + 7) / 8);   // 90 workgroups
   dispatcher->SetParameter(DISPATCH_Z, 1);
   graph.Connect(computePipeline, PIPELINE, dispatcher, COMPUTE_PIPELINE);
   graph.Connect(descriptorSet, DESCRIPTOR_SET, dispatcher, DESCRIPTOR_SETS);

   // Timestamp queries
   auto timestampQuery = graph.AddNode<TimestampQueryNode>("RayMarchTiming");
   graph.Connect(dispatcher, COMMAND_BUFFER, timestampQuery, COMMAND_BUFFER);

   // Present to screen
   auto present = graph.AddNode<PresentNode>("Present");
   graph.Connect(dispatcher, COMMAND_BUFFER, present, COMMAND_BUFFER);
   ```

4. Run and verify:
   - Voxel cube renders to screen
   - No validation errors
   - GPU time printed to console
   - Voxel resolution adjustable via parameter

5. Performance test:
   - 64³ voxels @ 720p: Target <10ms
   - 128³ voxels @ 720p: Target <30ms
   - Compare to baseline (empty dispatch): Overhead <0.1ms

---

## Testing Strategy

### Unit Tests (Per Task)
- **G.1**: ComputePipelineNode compiles, creates VkComputePipeline
- **G.2**: Storage resources register, descriptor updates work
- **G.3**: Compute dispatch executes, output image not black
- **G.4**: Timestamp queries return reasonable values (1-50ms)

### Integration Tests (Phase G.5)
- **Visual Test**: Voxel cube visible on screen
- **Parameter Test**: Voxel resolution changes affect performance
- **Validation Test**: Zero Vulkan validation errors
- **Performance Test**: GPU time < 10ms for 64³ @ 720p

### Acceptance Criteria (Phase G Complete)
- ✅ Compute shader renders voxel cube to screen
- ✅ Timestamp queries measure dispatch time (<0.1ms overhead)
- ✅ Manually adjustable voxel resolution (32³, 64³, 128³)
- ✅ Zero validation errors
- ✅ Cache HIT on second run (pipeline cached)
- ✅ Code follows existing patterns (GraphicsPipelineNode, PipelineCacher)

---

## Risk Mitigation

### Risk: Compute Queue vs Graphics Queue
**Issue**: Some GPUs have separate compute queues, others share graphics queue.
**Mitigation**:
- Start with graphics queue (guaranteed to support compute)
- Phase H can add dedicated compute queue optimization

### Risk: Storage Image Layout Transitions
**Issue**: Incorrect layout transitions cause validation errors.
**Mitigation**:
- Explicitly transition GENERAL → SHADER_READ in ComputeRayMarchNode
- Add validation: Check image layout before present

### Risk: Timestamp Overhead
**Issue**: Query readback might stall GPU.
**Mitigation**:
- Double-buffered query pools (read frame N-1 while recording N)
- Validate <0.1ms overhead with Nsight Graphics

### Risk: Workgroup Size Mismatch
**Issue**: Shader workgroup size doesn't match dispatch.
**Mitigation**:
- Extract workgroup size from SPIRV reflection
- Validate: dispatchX = ceil(outputWidth / workgroupX)

---

## Dependencies on Future Phases

**Phase H (Voxel Data Infrastructure)**:
- Will replace simple 3D texture with sparse voxel octree
- ComputeRayMarchNode shader will be updated with octree traversal
- Current implementation uses placeholder cube

**Phase I (Performance Profiling System)**:
- Will consume TimestampQueryNode output for CSV export
- Will add bandwidth metrics (currently only GPU time)

**Phase J (Fragment Shader)**:
- Will reuse storage resource types (G.2)
- Will provide comparison baseline for compute performance

---

## Documentation Updates

**During Implementation**:
- Update activeContext.md after each G.x task completion
- Log decisions in PhaseG-ImplementationLog.md (new file)

**After Phase G Complete**:
- Create PhaseG-Architecture.md (compute pipeline architecture)
- Update systemPatterns.md (compute pipeline pattern)
- Update progress.md (Phase G complete)

---

## Next Steps (Immediate Actions)

1. ✅ Create branch: `claude/phase-g-compute-pipeline`
2. ✅ Write Phase G plan (this document)
3. ⏳ Start G.1: Create ComputePipelineNodeConfig.h
4. ⏳ Implement ComputePipelineCacher
5. ⏳ Build and test minimal compute pipeline

**Estimated Start Date**: November 2, 2025 (after Phase F merge)
**Estimated Completion**: November 24, 2025 (3 weeks)

---

## Conclusion

Phase G is the foundation for all research pipelines. By implementing the simplest ray marching approach first, we:
- Validate profiling methodology early
- Establish visual correctness baseline
- Build reusable infrastructure (compute pipelines, storage resources, timestamp queries)
- De-risk hardware RT implementation (Phase K)

**Success = Voxel cube rendering with accurate GPU timing measurements.**

Ready to implement G.1: ComputePipelineNode.
