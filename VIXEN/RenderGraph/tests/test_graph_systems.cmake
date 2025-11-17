# ===========================================================================
# Graph Systems - Consolidated Test Configuration
# ===========================================================================
# This file consolidates all graph-related system tests:
# - Graph Topology: Circular dependency detection, topological sorting
# - Resource Management: ResourceBudgetManager, DeferredDestruction, SlotTask
#
# Compatible with VULKAN_TRIMMED_BUILD (headers only, no Vulkan required).
# ===========================================================================

# ---------------------------------------------------------------------------
# Graph Topology Tests
# ---------------------------------------------------------------------------
# Validates graph topology validation and dependency tracking:
# - Circular dependency detection (direct & indirect)
# - Topological sorting
# - Dependency chain analysis
# - Complex graph validation
# - Edge case handling

message(STATUS "Configuring test_graph_topology (trimmed build compatible)")

if(TARGET GTest::gtest_main)
    add_executable(test_graph_topology
        test_graph_topology.cpp
    )

    # Allow tests to include library headers with clean paths: #include "RenderGraph/..."
    target_include_directories(test_graph_topology PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include  # RenderGraph's own headers
    )

    target_link_libraries(test_graph_topology PRIVATE
        GTest::gtest_main
        RenderGraph
    )

    # Platform-specific settings
    if(NOT VULKAN_TRIMMED_BUILD_ACTIVE)
        # Full build mode
        if(TARGET Vulkan::Vulkan)
            target_link_libraries(test_graph_topology PRIVATE Vulkan::Vulkan)
        endif()
    else()
        # Trimmed build mode
        message(STATUS "  Using Vulkan headers from: ${VULKAN_HEADERS_INCLUDE_DIR}")
    endif()

    gtest_discover_tests(test_graph_topology)

    message(STATUS "✓ test_graph_topology configured (trimmed build: ${VULKAN_TRIMMED_BUILD_ACTIVE})")
else()
    message(STATUS "⊗ test_graph_topology skipped (GoogleTest not available)")
endif()

# ---------------------------------------------------------------------------
# Resource Management Tests
# ---------------------------------------------------------------------------
# Validates resource management systems:
# - ResourceBudgetManager (memory budget tracking)
# - DeferredDestruction (cleanup queue management)
# - StatefulContainer (resource state tracking)
# - SlotTask (task status management)
#
# NOTE: This test has outdated API calls and requires refactoring to match
# the current system architecture. Temporarily disabled.
# TODO: Refactor test to use current ResourceBudgetManager and DeferredDestructionQueue APIs

message(STATUS "Configuring test_resource_management (trimmed build compatible)")

if(TARGET GTest::gtest_main)
    add_executable(test_resource_management
        test_resource_management.cpp
    )

    # Allow tests to include library headers with clean paths: #include "RenderGraph/..."
    target_include_directories(test_resource_management PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include  # RenderGraph's own headers
    )

    target_link_libraries(test_resource_management PRIVATE
        GTest::gtest_main
        RenderGraph
    )

    # Platform-specific settings
    if(NOT VULKAN_TRIMMED_BUILD_ACTIVE)
        # Full build mode
        if(TARGET Vulkan::Vulkan)
            target_link_libraries(test_resource_management PRIVATE Vulkan::Vulkan)
        endif()
    else()
        # Trimmed build mode
        message(STATUS "  Using Vulkan headers from: ${VULKAN_HEADERS_INCLUDE_DIR}")
    endif()

    gtest_discover_tests(test_resource_management)

    message(STATUS "✓ test_resource_management configured (trimmed build: ${VULKAN_TRIMMED_BUILD_ACTIVE})")
else()
    message(STATUS "⊗ test_resource_management skipped (GoogleTest not available)")
endif()
