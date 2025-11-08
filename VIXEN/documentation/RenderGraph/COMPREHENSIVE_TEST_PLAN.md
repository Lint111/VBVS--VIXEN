# Comprehensive Test Plan - Class-by-Class Coverage

**Date**: November 5, 2025
**Goal**: Achieve 80%+ test coverage for all RenderGraph classes
**Current Coverage**: 40% (19/48 components)
**Target Coverage**: 90%+ (43+/48 components)

---

## Priority Matrix

### Priority 1: Core Infrastructure (CRITICAL) 🔴
**Estimated Time**: 8-12 hours
**Target Coverage**: 85%+

1. **Timer.h** (0% → 90%)
   - Delta time calculation
   - Reset functionality
   - Precision validation

2. **LoopManager.h** (0% → 85%)
   - Loop registration
   - Fixed timestep accumulator
   - Catchup modes (FireAndForget, SingleCorrectiveStep, MultipleSteps)
   - Frame rate calculation

3. **ResourceDependencyTracker.h** (0% → 85%)
   - Dependency detection from input connections
   - CleanupDependencies generation
   - Visited tracking

4. **PerFrameResources.h** (0% → 85%)
   - Ring buffer pattern
   - Current index tracking
   - Resource access by frame

---

### Priority 2: Resource Management (CRITICAL) 🔴
**Estimated Time**: 6-8 hours
**Target Coverage**: 90%+
**Status**: ✅ COMPLETE (test_resource_management.cpp created)

1. **ResourceBudgetManager.h** (✅ 90%)
2. **DeferredDestruction.h** (✅ 95%)
3. **StatefulContainer.h** (✅ 85%)
4. **SlotTask.h** (✅ 90%)

---

### Priority 3: Critical Nodes (HIGH PRIORITY) 🟠
**Estimated Time**: 12-16 hours
**Target Coverage**: 80%+

#### Infrastructure Nodes (4 nodes)
1. **DeviceNode.h** (0% → 85%)
   - Vulkan device creation
   - Queue family selection
   - Extension/feature enabling
   - Device destruction

2. **WindowNode.h** (0% → 80%)
   - GLFW window creation
   - Event processing
   - Resize handling
   - Window destruction

3. **CommandPoolNode.h** (0% → 80%)
   - Command pool creation per queue family
   - Reset capability
   - Command buffer allocation

4. **SwapChainNode.h** (0% → 85%)
   - Swapchain creation
   - Image acquisition
   - Present mode selection
   - Swapchain recreation on resize

#### Synchronization Nodes (1 node)
5. **FrameSyncNode.h** (0% → 80%)
   - Fence creation (per-flight)
   - Semaphore creation (imageAvailable, renderComplete)
   - Present fence creation
   - Frame-in-flight management

---

### Priority 4: Pipeline Nodes (HIGH PRIORITY) 🟠
**Estimated Time**: 10-14 hours
**Target Coverage**: 75%+

#### Graphics Pipeline (2 nodes)
1. **GraphicsPipelineNode.h** (0% → 80%)
   - Pipeline creation from ShaderDataBundle
   - Descriptor layout auto-generation
   - Vertex input extraction from SPIRV
   - Pipeline caching

2. **RenderPassNode.h** (0% → 75%)
   - Render pass creation
   - Subpass configuration
   - Attachment descriptions
   - Dependency setup

#### Compute Pipeline (2 nodes)
3. **ComputePipelineNode.h** (0% → 80%)
   - Compute pipeline creation
   - Descriptor layout auto-generation
   - Workgroup size extraction

4. **ComputeDispatchNode.h** (0% → 80%)
   - Generic dispatch command recording
   - Descriptor set binding
   - Push constant support
   - Dynamic dispatch calculation

---

### Priority 5: Descriptor & Resource Nodes (MEDIUM PRIORITY) 🟡
**Estimated Time**: 8-10 hours
**Target Coverage**: 75%+

1. **DescriptorSetNode.h** (0% → 80%)
   - Descriptor pool creation
   - Descriptor set allocation
   - Resource binding (images, buffers, samplers)
   - Pool sizing from SPIRV reflection

2. **DescriptorResourceGathererNode.h** (N/A → 80%)
   - Unblock test_descriptor_gatherer_comprehensive.cpp
   - Requires ShaderManagement build fix

3. **TextureLoaderNode.h** (0% → 75%)
   - Image loading from file
   - Staging buffer upload
   - Format selection
   - Mipmap generation (if supported)

4. **VertexBufferNode.h** (0% → 75%)
   - Vertex data upload
   - Staging buffer pattern
   - Memory alignment

5. **DepthBufferNode.h** (0% → 75%)
   - Depth image creation
   - Format selection (D32_SFLOAT, D24_UNORM_S8_UINT)
   - Image view creation

---

### Priority 6: Rendering Nodes (MEDIUM PRIORITY) 🟡
**Estimated Time**: 6-8 hours
**Target Coverage**: 70%+

1. **FramebufferNode.h** (0% → 75%)
   - Framebuffer creation from attachments
   - Compatibility with render pass
   - Per-swapchain-image framebuffers

2. **GeometryRenderNode.h** (0% → 75%)
   - Command buffer recording
   - Render pass begin/end
   - Pipeline binding
   - Descriptor set binding
   - Draw command recording

3. **PresentNode.h** (0% → 70%)
   - Presentation to swapchain
   - Semaphore synchronization
   - Present fence signaling

---

### Priority 7: Data Flow Nodes (LOW PRIORITY) 🟢
**Estimated Time**: 4-6 hours
**Target Coverage**: 70%+

1. **ShaderLibraryNode.h** (0% → 75%)
   - Shader loading via ShaderBundleBuilder
   - SPIRV reflection
   - Cache integration

2. **BoolOpNode.h** (0% → 80%)
   - Boolean operations (AND, OR, XOR, NOT, NAND, NOR)
   - Vector input handling

3. **LoopBridgeNode.h** (0% → 75%)
   - Loop state publication
   - Should-execute logic

4. **StructSpreaderNode.h** (0% → 70%)
   - Struct field spreading to multiple outputs

---

### Priority 8: Utilities & Interfaces (LOW PRIORITY) 🟢
**Estimated Time**: 4-6 hours
**Target Coverage**: 60%+

1. **ComputePerformanceLogger.h** (0% → 70%)
   - Hierarchical logging
   - Performance metrics
   - Parent-child relationship

2. **UnknownTypeRegistry.h** (0% → 60%)
   - Type registration
   - Lookup functionality

3. **IGraphCompilable.h** (0% → 60%)
   - Interface compliance tests

4. **INodeWiring.h** (0% → 60%)
   - Wiring interface tests

5. **GraphMessages.h** (0% → 60%)
   - Message structure validation

6. **RenderGraphEvents.h** (0% → 60%)
   - Event type validation

---

## Test File Organization

### Current Structure
```
VIXEN/RenderGraph/tests/
├── test_array_type_validation.cpp       ✅ (Type system)
├── test_field_extraction.cpp            ✅ (Field extraction)
├── test_resource_gatherer.cpp           ✅ (Variadic nodes)
├── test_resource_management.cpp         ✅ (Resource mgmt - NEW)
├── test_graph_topology.cpp              ✅ (Graph validation - NEW)
└── test_descriptor_gatherer_comprehensive.cpp ⚠️ (Blocked)
```

### Proposed New Test Files

#### Core Infrastructure Tests
```
VIXEN/RenderGraph/tests/Core/
├── test_timer.cpp                       ⏳ (NEW)
├── test_loop_manager.cpp                ⏳ (NEW)
├── test_resource_dependency_tracker.cpp ⏳ (NEW)
├── test_per_frame_resources.cpp         ⏳ (NEW)
└── test_node_lifecycle.cpp              ⏳ (NEW - generic lifecycle pattern)
```

#### Node Tests (Critical)
```
VIXEN/RenderGraph/tests/Nodes/
├── test_device_node.cpp                 ⏳ (NEW)
├── test_window_node.cpp                 ⏳ (NEW)
├── test_command_pool_node.cpp           ⏳ (NEW)
├── test_swapchain_node.cpp              ⏳ (NEW)
└── test_frame_sync_node.cpp             ⏳ (NEW)
```

#### Pipeline Tests
```
VIXEN/RenderGraph/tests/Pipelines/
├── test_graphics_pipeline_node.cpp      ⏳ (NEW)
├── test_render_pass_node.cpp            ⏳ (NEW)
├── test_compute_pipeline_node.cpp       ⏳ (NEW)
└── test_compute_dispatch_node.cpp       ⏳ (NEW)
```

#### Descriptor & Resource Tests
```
VIXEN/RenderGraph/tests/Resources/
├── test_descriptor_set_node.cpp         ⏳ (NEW)
├── test_texture_loader_node.cpp         ⏳ (NEW)
├── test_vertex_buffer_node.cpp          ⏳ (NEW)
└── test_depth_buffer_node.cpp           ⏳ (NEW)
```

#### Rendering Tests
```
VIXEN/RenderGraph/tests/Rendering/
├── test_framebuffer_node.cpp            ⏳ (NEW)
├── test_geometry_render_node.cpp        ⏳ (NEW)
└── test_present_node.cpp                ⏳ (NEW)
```

#### Utility Tests
```
VIXEN/RenderGraph/tests/Utilities/
├── test_shader_library_node.cpp         ⏳ (NEW)
├── test_bool_op_node.cpp                ⏳ (NEW)
├── test_loop_bridge_node.cpp            ⏳ (NEW)
├── test_performance_logger.cpp          ⏳ (NEW)
└── test_interfaces.cpp                  ⏳ (NEW)
```

---

## Testing Approach

### Mock Objects Strategy

Many nodes require Vulkan resources. We'll use two approaches:

1. **Headers-Only Tests** (No Vulkan required)
   - Config validation
   - Slot metadata
   - Type checking
   - Lifecycle state transitions

2. **Integration Tests** (Full Vulkan)
   - Actual resource creation
   - Vulkan API interaction
   - End-to-end workflows

### Test Pattern Template

```cpp
// test_example_node.cpp
#include <gtest/gtest.h>
#include "RenderGraph/include/Nodes/ExampleNode.h"
#include "RenderGraph/include/Core/RenderGraph.h"

class ExampleNodeTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup test environment
    }

    void TearDown() override {
        // Cleanup
    }

    std::unique_ptr<RenderGraph> graph;
};

// 1. Config Validation Tests
TEST_F(ExampleNodeTest, ConfigHasRequiredSlots) {
    // Verify INPUT_SLOT and OUTPUT_SLOT declarations
}

// 2. Lifecycle Tests
TEST_F(ExampleNodeTest, SetupInitializesResources) {
    // Test Setup() phase
}

TEST_F(ExampleNodeTest, CompileCreatesVulkanResources) {
    // Test Compile() phase (may require full SDK)
}

TEST_F(ExampleNodeTest, ExecuteRecordsCommands) {
    // Test Execute() phase
}

TEST_F(ExampleNodeTest, CleanupDestroysResources) {
    // Test Cleanup() phase
}

// 3. Edge Cases
TEST_F(ExampleNodeTest, HandlesNullInputGracefully) {
    // Test error handling
}

// 4. Integration Tests
TEST_F(ExampleNodeTest, IntegrationWithUpstreamNodes) {
    // Test node connections
}
```

---

## Timeline Estimation

| Priority | Components | Est. Time | Target Date |
|----------|-----------|-----------|-------------|
| P1 | Core Infrastructure (4 classes) | 8-12h | Nov 7 |
| P2 | Resource Management | ✅ DONE | ✅ Nov 5 |
| P3 | Critical Nodes (5 nodes) | 12-16h | Nov 10 |
| P4 | Pipeline Nodes (4 nodes) | 10-14h | Nov 14 |
| P5 | Descriptor Nodes (5 nodes) | 8-10h | Nov 17 |
| P6 | Rendering Nodes (3 nodes) | 6-8h | Nov 19 |
| P7 | Data Flow Nodes (4 nodes) | 4-6h | Nov 21 |
| P8 | Utilities (6 components) | 4-6h | Nov 23 |

**Total Estimated Time**: 52-72 hours (1.5-2 weeks of focused work)

**Target Completion**: November 23, 2025 (before Phase H starts)

---

## Success Criteria

1. **Coverage Target**: 90%+ (43+/48 components)
2. **All Priority 1-4 classes**: 80%+ coverage
3. **All Priority 5-8 classes**: 70%+ coverage
4. **Zero test failures**: All tests pass on CI
5. **Documentation**: Each test file documents what it covers

---

## Risk Mitigation

### Risk: Full Vulkan SDK Required
**Mitigation**: Write headers-only tests first (config validation, lifecycle transitions), defer integration tests

### Risk: Time Estimation Too Low
**Mitigation**: Focus on P1-P4 first (critical coverage), defer P5-P8 if needed

### Risk: Test Environment Setup Complex
**Mitigation**: Use existing test infrastructure (GoogleTest, trimmed build mode)

---

## Next Steps

1. ✅ Create this plan document
2. ⏳ Start with P1: Core Infrastructure tests (Timer, LoopManager, etc.)
3. ⏳ Move to P3: Critical Node tests (DeviceNode, WindowNode, etc.)
4. ⏳ Continue through priorities systematically
5. ⏳ Generate final coverage report

---

**Status**: Plan created, ready to implement
**Next Action**: Create test_timer.cpp (Priority 1, ~1-2 hours)
