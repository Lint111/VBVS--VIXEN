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

