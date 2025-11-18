cmake_minimum_required(VERSION 3.15)
project(RenderGraphTests)

# ===========================================================================
# Code Coverage Configuration
# ===========================================================================
# Enable with: cmake -DENABLE_COVERAGE=ON ..
# Visual Studio: Test > Analyze Code Coverage for All Tests
# See Coverage.cmake for detailed documentation
include(Coverage.cmake)

# Include Google Test helpers (provided by FetchContent in the top-level CMake)
include(GoogleTest)

add_executable(test_rendergraph_basic
    test_rendergraph_basic.cpp
    test_rendergraph_dependency.cpp
)
# Link against the RenderGraph library target if it exists in the main project
# Use the imported target GTest::gtest_main provided by the top-level FetchContent
if(TARGET RenderGraph)
    target_link_libraries(test_rendergraph_basic PRIVATE GTest::gtest_main RenderGraph)
else()
    target_link_libraries(test_rendergraph_basic PRIVATE GTest::gtest_main)
endif()

gtest_discover_tests(test_rendergraph_basic)

add_executable(test_rendergraph_dependency
    test_rendergraph_dependency.cpp
    test_array_slot_dedup.cpp
    test_multiple_producers.cpp
)
if(TARGET RenderGraph)
    target_link_libraries(test_rendergraph_dependency PRIVATE GTest::gtest_main RenderGraph)
else()
    target_link_libraries(test_rendergraph_dependency PRIVATE GTest::gtest_main)
endif()

gtest_discover_tests(test_rendergraph_dependency)

add_executable(test_typednode_helpers test_typednode_helpers.cpp)
if(TARGET RenderGraph)
    target_link_libraries(test_typednode_helpers PRIVATE GTest::gtest_main RenderGraph)
else()
    target_link_libraries(test_typednode_helpers PRIVATE GTest::gtest_main)
endif()

gtest_discover_tests(test_typednode_helpers)

# Additional typed-node tests
target_sources(test_typednode_helpers PRIVATE test_executeonly_behavior.cpp)

# ===========================================================================
# Standalone array type validation test (no Vulkan runtime required)
# ===========================================================================
# This test validates compile-time type traits only - no Vulkan needed!
# If this compiles successfully, all static_assert tests passed.

include(test_array_validation.cmake)

# ===========================================================================
# Standalone field extraction test (no Vulkan runtime required)
# ===========================================================================
# This test validates struct field extraction for ergonomic slot connections.
# Compatible with VULKAN_TRIMMED_BUILD (headers only).

include(test_field_extraction.cmake)

# ===========================================================================
# Resource gatherer node test (no Vulkan runtime required)
# ===========================================================================
# This test validates the variadic template resource gatherer node works
# with all type system features (arrays, variants, field extraction).
# Compatible with VULKAN_TRIMMED_BUILD (headers only).

include(test_resource_gatherer.cmake)

# ===========================================================================
# Resource Management Tests
# ===========================================================================
# This test suite validates resource management systems:
# - ResourceBudgetManager, DeferredDestruction, StatefulContainer, SlotTask
# Compatible with VULKAN_TRIMMED_BUILD (headers only).

include(test_resource_management.cmake)

# ===========================================================================
# Graph Topology Tests
# ===========================================================================
# This test suite validates graph topology and dependency tracking:
# - Circular dependency detection, topological sorting, validation
# Compatible with VULKAN_TRIMMED_BUILD (headers only).

include(test_graph_topology.cmake)

# ===========================================================================
# Core Infrastructure Tests
# ===========================================================================
# These test suites validate core infrastructure classes:
# - Timer: High-resolution delta time measurement
# - LoopManager: Fixed timestep accumulator with catchup modes
# - ResourceDependencyTracker: Resource-to-producer mapping and dependency tracking
# - PerFrameResources: Ring buffer pattern for per-frame GPU resources
# Compatible with VULKAN_TRIMMED_BUILD (headers only).

include(test_timer.cmake)
include(test_loop_manager.cmake)
include(test_resource_dependency_tracker.cmake)
include(test_per_frame_resources.cmake)

# ===========================================================================
# Critical Nodes Tests (Priority 3)
# ===========================================================================
# These test suites validate critical infrastructure and synchronization nodes:
# - DeviceNode: Vulkan device initialization
# - WindowNode: Window and surface creation
# - CommandPoolNode: Command buffer management
# - SwapChainNode: Swapchain and presentation
# - FrameSyncNode: Frame synchronization primitives
# Unit tests compatible with VULKAN_TRIMMED_BUILD.
# Integration tests require full Vulkan SDK.

include(test_critical_nodes.cmake)

# ===========================================================================
# Pipeline Nodes Tests (Priority 4)
# ===========================================================================
# These test suites validate pipeline creation and dispatch nodes:
# - GraphicsPipelineNode: Graphics pipeline configuration
# - RenderPassNode: Render pass management
# - ComputePipelineNode: Compute pipeline creation
# - ComputeDispatchNode: Compute dispatch operations
# Unit tests compatible with VULKAN_TRIMMED_BUILD.
# Integration tests require full Vulkan SDK.

include(Pipelines/test_pipeline_nodes.cmake)

# ===========================================================================
# Descriptor & Resource Nodes Tests (Priority 5)
# ===========================================================================
# These test suites validate descriptor and resource management nodes:
# - DescriptorSetNode: Descriptor set allocation and updates
# - TextureLoaderNode: Texture loading and image creation
# - VertexBufferNode: Vertex buffer allocation and transfer
# - DepthBufferNode: Depth/stencil buffer management
# - DescriptorResourceGathererNode: Order-agnostic binding resolution
# Unit tests compatible with VULKAN_TRIMMED_BUILD.
# Integration tests require full Vulkan SDK.

include(Resources/test_resource_nodes.cmake)

# ===========================================================================
# Rendering Nodes Tests (Priority 6)
# ===========================================================================
# These test suites validate rendering execution nodes:
# - FramebufferNode: Framebuffer creation and attachment management
# - GeometryRenderNode: Geometry rendering and draw command recording
# - PresentNode: Swapchain presentation and synchronization
# Unit tests compatible with VULKAN_TRIMMED_BUILD.
# Integration tests require full Vulkan SDK.

include(Rendering/test_rendering_nodes.cmake)

# ===========================================================================
# Data Flow Nodes Tests (Priority 7)
# ===========================================================================
# These test suites validate data flow and logic nodes:
# - ConstantNode: Constant value sources
# - LoopBridgeNode: Cross-loop data transfer
# - BoolOpNode: Boolean logic operations (AND, OR, XOR, NAND, NOR)
# - ShaderLibraryNode: Shader loading and SPIRV reflection
# Unit tests compatible with VULKAN_TRIMMED_BUILD.
# Integration tests require full graph execution.

include(DataFlow/test_dataflow_nodes.cmake)

# ===========================================================================
# Utility Classes Tests (Priority 8)
# ===========================================================================
# These test suites validate utility classes and interfaces:
# - NodeType: Type system base class
# - NodeTypeRegistry: Factory pattern for node creation
# - TypedConnection: Type-safe slot connections
# - IGraphCompilable: Compilable interface contract
# - INodeWiring: Wiring interface contract
# - UnknownTypeRegistry: Runtime type discovery
# - NodeLogging: Structured logging for node lifecycle
# Unit tests compatible with VULKAN_TRIMMED_BUILD.
# Integration tests require full graph execution.

include(Utilities/test_utilities.cmake)

# ===========================================================================
# Descriptor Resource Gatherer - Comprehensive Test Suite
# ===========================================================================
# This test suite validates DescriptorResourceGathererNode (Phase G):
# - Order-agnostic binding connections
# - SDI naming.h integration
# - Expected failures and edge cases
# - Full validation coverage
# Requires GoogleTest framework.

if(TARGET GTest::gtest_main)
    add_executable(test_descriptor_gatherer_comprehensive
        test_descriptor_gatherer_comprehensive.cpp
    )

    target_include_directories(test_descriptor_gatherer_comprehensive PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
    )

    target_link_libraries(test_descriptor_gatherer_comprehensive PRIVATE
        GTest::gtest_main
        RenderGraph
        ShaderManagement
    )

    if(TARGET Vulkan::Vulkan)
        target_link_libraries(test_descriptor_gatherer_comprehensive PRIVATE Vulkan::Vulkan)
    endif()

    gtest_discover_tests(test_descriptor_gatherer_comprehensive)

    message(STATUS "✓ test_descriptor_gatherer_comprehensive configured")
else()
    message(STATUS "⊗ test_descriptor_gatherer_comprehensive skipped (GoogleTest not available)")
endif()

# ===========================================================================
# ResourceVariant Handle Types Test
# ===========================================================================
# This test validates ResourceVariant accepts platform handle types used by nodes.
# Tests compile-time type registration (no Vulkan runtime required).

if(TARGET GTest::gtest_main)
    add_executable(test_resourcevariant_handles
        test_resourcevariant_handles.cpp
    )

    target_link_libraries(test_resourcevariant_handles PRIVATE
        GTest::gtest_main
        RenderGraph
    )

    gtest_discover_tests(test_resourcevariant_handles)

    message(STATUS "✓ test_resourcevariant_handles configured")
else()
    message(STATUS "⊗ test_resourcevariant_handles skipped (GoogleTest not available)")
endif()
