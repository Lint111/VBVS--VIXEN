# ===========================================================================
# Graph Topology Tests
# ===========================================================================
# Tests for RenderGraph topology validation and dependency tracking:
# - Circular dependency detection (direct & indirect)
# - Topological sorting
# - Dependency chain analysis
# - Complex graph validation
# - Edge case handling
#
# Compatible with VULKAN_TRIMMED_BUILD (headers only).
# ===========================================================================

message(STATUS "Configuring test_graph_topology (trimmed build compatible)")

if(TARGET GTest::gtest_main)
    add_executable(test_graph_topology
        test_graph_topology.cpp
    )

    target_include_directories(test_graph_topology PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
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
