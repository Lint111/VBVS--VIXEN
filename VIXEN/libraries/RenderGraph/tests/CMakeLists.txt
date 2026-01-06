cmake_minimum_required(VERSION 3.15)
project(RenderGraphTests)

# Include Google Test helpers (provided by FetchContent in the top-level CMake)
include(GoogleTest)

add_executable(test_rendergraph_basic
    test_rendergraph_basic.cpp
    test_rendergraph_dependency.cpp
)

# Ensure tests can include generated headers and centralized test mocks
# (Allows includes like <generated/sdi/Name.h> and <RenderGraph/tests/TestMocks.h>)
include_directories(${CMAKE_SOURCE_DIR})

# Link against the RenderGraph library target - includes propagate automatically
# RenderGraph target exposes its include directory as PUBLIC, no manual include_directories needed
if(TARGET RenderGraph)
    target_link_libraries(test_rendergraph_basic PRIVATE GTest::gtest_main RenderGraph)
else()
    target_link_libraries(test_rendergraph_basic PRIVATE GTest::gtest_main)
endif()

# Visual Studio solution folder organization
set_target_properties(test_rendergraph_basic PROPERTIES FOLDER "Tests/RenderGraph Tests")

gtest_discover_tests(test_rendergraph_basic)

add_executable(test_rendergraph_dependency
    test_rendergraph_dependency.cpp
    test_array_slot_dedup.cpp
    test_multiple_producers.cpp
)

# Link against the RenderGraph library target - includes propagate automatically
if(TARGET RenderGraph)
    target_link_libraries(test_rendergraph_dependency PRIVATE GTest::gtest_main RenderGraph)
else()
    target_link_libraries(test_rendergraph_dependency PRIVATE GTest::gtest_main)
endif()

# Visual Studio solution folder organization
set_target_properties(test_rendergraph_dependency PROPERTIES FOLDER "Tests/RenderGraph Tests")

gtest_discover_tests(test_rendergraph_dependency)

add_executable(test_typednode_helpers test_typednode_helpers.cpp)

# Link against the RenderGraph library target - includes propagate automatically
if(TARGET RenderGraph)
    target_link_libraries(test_typednode_helpers PRIVATE GTest::gtest_main RenderGraph)
else()
    target_link_libraries(test_typednode_helpers PRIVATE GTest::gtest_main)
endif()

# Visual Studio solution folder organization
set_target_properties(test_typednode_helpers PROPERTIES FOLDER "Tests/RenderGraph Tests")

gtest_discover_tests(test_typednode_helpers)

# Additional typed-node tests
target_sources(test_typednode_helpers PRIVATE test_executeonly_behavior.cpp)

# ===========================================================================
# Type System Tests - CONSOLIDATED
# ===========================================================================
# Validates compile-time type system features:
# - Array type validation, field extraction, resource gathering
# Compatible with VULKAN_TRIMMED_BUILD (headers only).

include(test_type_system.cmake)

# ===========================================================================
# Graph Systems Tests - CONSOLIDATED
# ===========================================================================
# Validates graph-related systems:
# - Graph topology, resource management
# Compatible with VULKAN_TRIMMED_BUILD (headers only).

include(test_graph_systems.cmake)

# ===========================================================================
# Core Infrastructure Tests - CONSOLIDATED
# ===========================================================================
# Validates core infrastructure classes:
# - Timer, LoopManager, ResourceDependencyTracker, PerFrameResources
# Compatible with VULKAN_TRIMMED_BUILD (headers only).

include(test_core_systems.cmake)

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
# Descriptor Resource Gatherer - Comprehensive Test Suite
# ===========================================================================
# This test suite validates DescriptorResourceGathererNode (Phase G):
# - Order-agnostic binding connections
# - SDI naming.h integration
# - Expected failures and edge cases
# - Full validation coverage
# Requires GoogleTest framework.
#
# NOTE: This test has outdated API calls and requires refactoring to match
# the current system architecture. Temporarily disabled.
# TODO: Refactor test to use current RenderGraph and SDI APIs

if(FALSE)  # Disabled due to API incompatibilities
if(TARGET GTest::gtest_main)
    add_executable(test_descriptor_gatherer_comprehensive
        test_descriptor_gatherer_comprehensive.cpp
    )

    # Link against library targets - includes propagate automatically via CMAKE_SOURCE_DIR
    target_include_directories(test_descriptor_gatherer_comprehensive PRIVATE
        ${CMAKE_SOURCE_DIR}/application/main/include
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
endif()  # Disabled due to API incompatibilities

message(STATUS "⊗ test_descriptor_gatherer_comprehensive skipped (outdated API - requires refactoring)")

# ===========================================================================
# PassThroughStorage Handle Types Test
# ===========================================================================
# This test validates PassThroughStorage accepts platform handle types used by nodes.
# Tests compile-time type registration (no Vulkan runtime required).

if(TARGET GTest::gtest_main)
    add_executable(test_passthroughstorage_handles
        test_passthroughstorage_handles.cpp
    )

    # Link against the RenderGraph library target - includes propagate automatically
    target_link_libraries(test_passthroughstorage_handles PRIVATE
        GTest::gtest_main
        RenderGraph
    )

    # Visual Studio solution folder organization
    set_target_properties(test_passthroughstorage_handles PROPERTIES FOLDER "Tests/RenderGraph Tests")

    gtest_discover_tests(test_passthroughstorage_handles)

    message(STATUS "✓ test_passthroughstorage_handles configured")
else()
    message(STATUS "⊗ test_passthroughstorage_handles skipped (GoogleTest not available)")
endif()

# ===========================================================================
# Connection Concepts Test (Sprint 6.0.1)
# ===========================================================================
# This test validates C++20 concepts for the unified connection system:
# - SlotReference, BindingReference, AccumulationSlot concepts
# - ConnectionMetadata concept and helper types
# - Legacy type trait wrappers
# Tests are primarily compile-time (static_assert).

if(TARGET GTest::gtest_main)
    add_executable(test_connection_concepts
        test_connection_concepts.cpp
    )

    # Link against the RenderGraph library target - includes propagate automatically
    target_link_libraries(test_connection_concepts PRIVATE
        GTest::gtest_main
        RenderGraph
    )

    # Visual Studio solution folder organization
    set_target_properties(test_connection_concepts PROPERTIES FOLDER "Tests/RenderGraph Tests")

    gtest_discover_tests(test_connection_concepts)

    message(STATUS "✓ test_connection_concepts configured (Sprint 6.0.1)")
else()
    message(STATUS "⊗ test_connection_concepts skipped (GoogleTest not available)")
endif()

# ===========================================================================
# Connection Rule Test (Sprint 6.0.1)
# ===========================================================================
# This test validates the ConnectionRule system:
# - SlotDescriptor and BindingDescriptor creation
# - ConnectionRuleRegistry rule matching
# - DirectConnectionRule validation logic
# Tests use GoogleTest framework.

if(TARGET GTest::gtest_main)
    add_executable(test_connection_rule
        test_connection_rule.cpp
    )

    # Link against the RenderGraph library target
    target_link_libraries(test_connection_rule PRIVATE
        GTest::gtest_main
        RenderGraph
    )

    # Visual Studio solution folder organization
    set_target_properties(test_connection_rule PROPERTIES FOLDER "Tests/RenderGraph Tests")

    gtest_discover_tests(test_connection_rule)

    message(STATUS "✓ test_connection_rule configured (Sprint 6.0.1)")
else()
    message(STATUS "⊗ test_connection_rule skipped (GoogleTest not available)")
endif()

# ===========================================================================
# Sprint 6.1: Group-Based Dispatch Tests
# ===========================================================================
# Tests for MultiDispatchNode GROUP_INPUTS and GroupKeyModifier
# - GroupKeyModifier validation and metadata storage
# - DispatchPass validation (IsValid())
# - Group partitioning logic
# - Deterministic ordering (std::map)
# - Backward compatibility with QueueDispatch()

if(TARGET GTest::gtest_main)
    add_executable(test_group_dispatch
        test_group_dispatch.cpp
    )

    target_link_libraries(test_group_dispatch PRIVATE
        GTest::gtest_main
        RenderGraph
    )

    set_target_properties(test_group_dispatch PROPERTIES FOLDER "Tests/RenderGraph Tests")

    gtest_discover_tests(test_group_dispatch)

    message(STATUS "✓ test_group_dispatch configured (Sprint 6.1)")
else()
    message(STATUS "⊗ test_group_dispatch skipped (GoogleTest not available)")
endif()

# ===========================================================================
# Sprint 6.2: TaskQueue Budget-Aware System Tests
# ===========================================================================
# Tests for TaskQueue template and MultiDispatchNode integration
# - Budget enforcement (strict/lenient modes)
# - Priority-based scheduling
# - Overflow protection
# - TaskBudget configuration and presets
# - Integration with MultiDispatchNode
# - Backward compatibility with Sprint 6.1

if(TARGET GTest::gtest_main)
    # Unit tests for TaskQueue template
    add_executable(test_task_queue
        test_task_queue.cpp
    )

    target_link_libraries(test_task_queue PRIVATE
        GTest::gtest_main
        RenderGraph
    )

    set_target_properties(test_task_queue PROPERTIES FOLDER "Tests/RenderGraph Tests")

    gtest_discover_tests(test_task_queue)

    message(STATUS "✓ test_task_queue configured (Sprint 6.2)")

    # Integration tests for MultiDispatchNode + TaskQueue
    add_executable(test_multidispatch_integration
        test_multidispatch_integration.cpp
    )

    target_link_libraries(test_multidispatch_integration PRIVATE
        GTest::gtest_main
        RenderGraph
    )

    set_target_properties(test_multidispatch_integration PROPERTIES FOLDER "Tests/RenderGraph Tests")

    gtest_discover_tests(test_multidispatch_integration)

    message(STATUS "✓ test_multidispatch_integration configured (Sprint 6.2)")
else()
    message(STATUS "⊗ Sprint 6.2 tests skipped (GoogleTest not available)")
endif()

# ===========================================================================
# Voxel Systems Tests - CONSOLIDATED (Phase H)
# ===========================================================================
# Validates voxel infrastructure:
# - Octree construction, scene generation, ray traversal
# Compatible with VULKAN_TRIMMED_BUILD (headers only).

include(test_voxel_systems.cmake)

# ===========================================================================
# CompileTimeResourceSystem Tests
# ===========================================================================
# Test suite for compile-time type system validation, caching, and traits.
# Moved from root CMakeLists.txt for proper organization.

add_subdirectory(CompileTimeResourceSystem)
