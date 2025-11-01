# Phase B: Advanced Rendering Features

**Created**: November 1, 2025
**Status**: Planning
**Priority**: HIGH - Build on completed infrastructure

---

## Overview

Phase B focuses on advanced rendering features now that core infrastructure (synchronization, caching, loops) is complete. This phase implements features that provide significant visual quality improvements and developer productivity gains.

---

## Prerequisites ✅

All prerequisites met:
- ✅ Phase 0 (Synchronization infrastructure) - COMPLETE
- ✅ Phase A (Persistent cache) - COMPLETE
- ✅ ShaderManagement Phases 0-5 - COMPLETE
- ✅ Multi-rate loop system - COMPLETE
- ✅ Command buffer state tracking - COMPLETE

---

## Proposed Features (Priority Order)

### High Priority (P0) - Maximum Impact

#### 1. Shader Hot-Reload
**Goal**: Reload SPIR-V shaders at runtime without restarting application

**Key Benefits**:
- Rapid iteration (shader tweaks take seconds, not minutes)
- Artist/designer friendly (see changes immediately)
- Debugging aid (test shader variations quickly)

**Implementation**:
```cpp
// Watch shader source files for changes
class ShaderWatcher {
    std::filesystem::file_time_type lastModified;
    void Poll();  // Check if file changed
};

// On change detected:
1. Recompile GLSL → SPIR-V (glslang)
2. Invalidate ShaderModuleCacher entry
3. Rebuild pipeline (PipelineCacher invalidation)
4. Recompile affected nodes (command buffer invalidation)
5. Next frame uses new shader
```

**Files to Create**:
- `ShaderManagement/include/ShaderWatcher.h`
- `ShaderManagement/src/ShaderWatcher.cpp`
- `RenderGraph/include/Nodes/ShaderHotReloadNode.h` (optional debug node)

**Estimated Effort**: 8-12 hours
**Risk**: Medium (pipeline recreation, descriptor set compatibility)

---

#### 2. ImGui Integration
**Goal**: Real-time debug UI overlay (sliders, graphs, profiler)

**Key Benefits**:
- Tweakable parameters (no recompile for constant changes)
- Performance metrics visualization
- Debug visualization toggles
- Scene inspection tools

**Implementation**:
```cpp
class ImGuiRenderNode : public RenderNode {
    // Renders ImGui overlay after main scene
    // Uses separate descriptor set + pipeline
    // Outputs to same framebuffer
};

// Integration with existing graph:
Scene → GeometryRenderNode → ImGuiRenderNode → PresentNode
```

**Dependencies**:
- ImGui library (header-only, easy integration)
- ImGui Vulkan backend (`imgui_impl_vulkan.cpp`)

**Files to Create**:
- `RenderGraph/include/Nodes/ImGuiRenderNode.h`
- `RenderGraph/src/Nodes/ImGuiRenderNode.cpp`
- `external/imgui/` (submodule)

**Estimated Effort**: 10-14 hours
**Risk**: Low (well-documented library, separate render pass)

---

#### 3. Compute Shader Support
**Goal**: Add compute pipeline support for general-purpose GPU computation

**Key Benefits**:
- Particle systems
- Post-processing effects (blur, bloom, tonemapping)
- Procedural generation
- Physics simulation offload

**Implementation**:
```cpp
class ComputePipelineCacher : public TypedCacher<...> {
    // Cache VkComputePipeline resources
};

class ComputeDispatchNode : public RenderNode {
    // Dispatch compute shader workgroups
    // Outputs storage buffer/image results
};
```

**Key Components**:
- Compute pipeline creation (VkComputePipelineCreateInfo)
- Storage buffer/image bindings (SSBO, storage images)
- Dispatch parameters (workgroup size, count)
- Synchronization barriers (image/buffer layout transitions)

**Files to Create**:
- `CashSystem/include/CashSystem/ComputePipelineCacher.h`
- `RenderGraph/include/Nodes/ComputeDispatchNode.h`
- `RenderGraph/src/Nodes/ComputeDispatchNode.cpp`

**Estimated Effort**: 12-16 hours
**Risk**: Medium (synchronization, storage resource management)

---

### Medium Priority (P1) - Quality of Life

#### 4. Multi-Pass Rendering (Deferred Shading)
**Goal**: G-buffer rendering for deferred lighting

**Key Benefits**:
- Many lights without performance penalty
- Decoupled geometry and lighting passes
- SSAO, SSR, and other screen-space effects

**Implementation**:
```cpp
// Pass 1: Geometry pass (write G-buffer)
GBufferNode → Outputs: albedo, normal, depth, metallic/roughness

// Pass 2: Lighting pass (read G-buffer, write color)
DeferredLightingNode → Inputs: G-buffer textures, light data

// Pass 3: Forward pass (transparent objects)
ForwardRenderNode → Inputs: color buffer, depth buffer
```

**Files to Create**:
- `RenderGraph/include/Nodes/GBufferNode.h`
- `RenderGraph/include/Nodes/DeferredLightingNode.h`
- Shaders: `Shaders/GBuffer.vert`, `Shaders/GBuffer.frag`, `Shaders/DeferredLighting.frag`

**Estimated Effort**: 16-20 hours
**Risk**: Medium (multiple render targets, attachment management)

---

#### 5. Dynamic Descriptor Sets
**Goal**: Update descriptors without pipeline recreation

**Key Benefits**:
- Faster material/texture changes
- Better cache utilization
- Runtime asset loading

**Implementation**:
```cpp
class DynamicDescriptorSetNode : public RenderNode {
    std::vector<VkDescriptorSet> descriptorSets;
    void UpdateDescriptor(uint32_t binding, VkBuffer buffer, VkDeviceSize offset);
};

// Update descriptors between frames without Compile()
node->UpdateDescriptor(0, newBuffer, 0);
```

**Files to Create**:
- `RenderGraph/include/Nodes/DynamicDescriptorSetNode.h`
- `RenderGraph/src/Nodes/DynamicDescriptorSetNode.cpp`

**Estimated Effort**: 8-10 hours
**Risk**: Low (straightforward vkUpdateDescriptorSets usage)

---

#### 6. Timestamp Queries (GPU Profiling)
**Goal**: Measure GPU execution time for each render pass

**Key Benefits**:
- Identify performance bottlenecks
- Frame time breakdown (per-pass timing)
- Validate optimization impact

**Implementation**:
```cpp
class TimestampQueryNode : public RenderNode {
    VkQueryPool queryPool;
    void BeginQuery(VkCommandBuffer cmd);
    void EndQuery(VkCommandBuffer cmd);
    double GetElapsedMs();  // CPU reads back results
};

// Usage:
TimestampQueryNode → wraps GeometryRenderNode → outputs timing
```

**Files to Create**:
- `RenderGraph/include/Nodes/TimestampQueryNode.h`
- `RenderGraph/src/Nodes/TimestampQueryNode.cpp`

**Estimated Effort**: 6-8 hours
**Risk**: Low (query pools are straightforward)

---

### Low Priority (P2) - Nice to Have

#### 7. Async Texture Loading
**Goal**: Stream textures from disk without blocking render thread

**Benefits**: Smooth loading screens, no frame hitches
**Estimated Effort**: 10-12 hours

#### 8. Mipmap Generation
**Goal**: Auto-generate mipmaps for textures (better sampling, performance)

**Benefits**: Reduced aliasing, faster texture filtering
**Estimated Effort**: 6-8 hours

#### 9. MSAA (Multisample Anti-Aliasing)
**Goal**: Hardware anti-aliasing for edge smoothing

**Benefits**: Smoother edges, better visual quality
**Estimated Effort**: 8-10 hours

---

## Recommended Execution Order

### Week 1: Developer Productivity (P0)
**Goal**: Fast iteration tools

**Day 1-2**: ImGui Integration (10-14h)
- Immediate visual feedback
- Foundation for all debug tools

**Day 3-4**: Shader Hot-Reload (8-12h)
- Enables rapid shader iteration
- Pairs well with ImGui for tweaking parameters

**Day 5**: Testing & Polish

---

### Week 2: Rendering Features (P0-P1)
**Goal**: Expand rendering capabilities

**Day 1-3**: Compute Shader Support (12-16h)
- Unlocks post-processing, particles, GPU physics

**Day 4-5**: Timestamp Queries (6-8h)
- Profile compute shader performance
- Validate optimizations

---

### Week 3: Advanced Rendering (P1)
**Goal**: Deferred shading pipeline

**Day 1-4**: Multi-Pass Rendering (16-20h)
- Deferred shading setup
- G-buffer rendering
- Lighting pass

**Day 5**: Dynamic Descriptor Sets (8-10h) OR Mipmap Generation (6-8h)

---

## Success Metrics

| Feature | Metric | Target |
|---------|--------|--------|
| Shader Hot-Reload | Reload time | <1 second |
| ImGui | Frame overhead | <0.5ms |
| Compute Shaders | Particle count | 100k particles @ 60fps |
| Timestamp Queries | Overhead | <0.1ms |
| Deferred Shading | Light count | 100+ lights @ 60fps |

---

## Dependencies

**External Libraries**:
- ✅ glslang (already integrated) - Shader compilation
- ⏳ ImGui (to add) - Debug UI
- ✅ GLI (already integrated) - Texture loading

**Internal Systems**:
- ✅ MainCacher - Resource caching
- ✅ ShaderManagement - Reflection automation
- ✅ LoopManager - Multi-rate loops
- ✅ EventBus - Hot-reload notifications

**All internal dependencies met - ready to begin!**

---

## Architecture Considerations

### Shader Hot-Reload Architecture
```
File Watcher → Recompile GLSL → Invalidate Cache → Rebuild Pipeline → Update Command Buffers
     ↓              ↓                   ↓                    ↓                   ↓
filesystem::   glslangValidator   ShaderModuleCacher   PipelineCacher   GeometryRenderNode
last_write_time                   .Erase(key)           .Erase(key)     MarkCommandBufferDirty()
```

### ImGui Integration Architecture
```
Main Scene → GeometryRenderNode → ImGuiRenderNode → PresentNode
                                       ↓
                              Separate render pass
                              Alpha blending enabled
                              Z-test disabled
```

### Compute Shader Architecture
```
Input Buffers/Images → ComputeDispatchNode → Output Buffers/Images
         ↓                      ↓                       ↓
    SSBO bindings        vkCmdDispatch         Memory barriers
    Storage images     Workgroup launch      Layout transitions
```

---

## Phase B Completion Criteria

**Must Have** (P0):
- ✅ Shader hot-reload working (detect file change → reload → render)
- ✅ ImGui overlay rendering
- ✅ Compute shader pipeline support
- ✅ At least one compute shader example (particle system or post-process)

**Should Have** (P1):
- ✅ Timestamp query profiling
- ✅ Deferred shading OR dynamic descriptor sets

**Nice to Have** (P2):
- Any P2 features (async loading, mipmaps, MSAA)

---

## Next Steps

1. ✅ Create this planning document
2. ⏳ **START HERE**: ImGui Integration (highest ROI for development)
3. ⏳ Shader Hot-Reload
4. ⏳ Compute Shader Support
5. ⏳ Timestamp Queries
6. ⏳ Multi-Pass Rendering OR Dynamic Descriptors
7. ⏳ Update memory bank with Phase B completion

---

## Open Questions

1. **ImGui Render Pass**: Separate pass or same as main scene?
   - **Recommendation**: Separate pass for cleaner separation

2. **Shader Hot-Reload Scope**: Watch all shaders or specific files?
   - **Recommendation**: Watch shader source directory, reload on any change

3. **Compute Shader First Example**: Particles or post-process?
   - **Recommendation**: Gaussian blur (simpler, immediate visual impact)

4. **Deferred Shading Scope**: Full deferred or just G-buffer?
   - **Recommendation**: Start with G-buffer + simple lighting (1-2 lights)

---

## Documentation Updates Needed

When Phase B complete:
- Update `memory-bank/activeContext.md` - Current focus
- Update `memory-bank/progress.md` - Mark Phase B complete
- Create `documentation/ImGuiIntegration.md` - Usage guide
- Create `documentation/ShaderHotReload.md` - Setup instructions
- Create `documentation/ComputeShaders.md` - Pipeline setup guide
