cmake_minimum_required(VERSION 3.15)
project(RenderGraphTests)

# Include Google Test helpers (provided by FetchContent in the top-level CMake)
include(GoogleTest)

add_executable(test_rendergraph_basic
    test_rendergraph_basic.cpp
    test_rendergraph_dependency.cpp
)

# Link against the RenderGraph library target - includes propagate automatically
# RenderGraph target exposes its include directory as PUBLIC, no manual include_directories needed
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

# Link against the RenderGraph library target - includes propagate automatically
if(TARGET RenderGraph)
    target_link_libraries(test_rendergraph_dependency PRIVATE GTest::gtest_main RenderGraph)
else()
    target_link_libraries(test_rendergraph_dependency PRIVATE GTest::gtest_main)
endif()

gtest_discover_tests(test_rendergraph_dependency)

add_executable(test_typednode_helpers test_typednode_helpers.cpp)

# Link against the RenderGraph library target - includes propagate automatically
if(TARGET RenderGraph)
    target_link_libraries(test_typednode_helpers PRIVATE GTest::gtest_main RenderGraph)
else()
    target_link_libraries(test_typednode_helpers PRIVATE GTest::gtest_main)
endif()

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
        ${CMAKE_SOURCE_DIR}
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

    gtest_discover_tests(test_passthroughstorage_handles)

    message(STATUS "✓ test_passthroughstorage_handles configured")
else()
    message(STATUS "⊗ test_passthroughstorage_handles skipped (GoogleTest not available)")
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
