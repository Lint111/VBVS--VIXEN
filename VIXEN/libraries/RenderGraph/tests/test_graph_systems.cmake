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

    # Visual Studio solution folder organization
    set_target_properties(test_graph_topology PROPERTIES FOLDER "Tests/RenderGraph Tests")

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
# MOVED to libraries/ResourceManagement/tests/test_resource_management.cpp
# The test now lives with the ResourceManagement library.
message(STATUS "⊗ test_resource_management moved to ResourceManagement library")
