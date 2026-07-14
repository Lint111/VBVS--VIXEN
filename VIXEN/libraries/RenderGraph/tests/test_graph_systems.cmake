# ===========================================================================
# Graph Systems - Consolidated Test Configuration
# ===========================================================================
# This file consolidates all graph-related system tests:
# - Graph Topology: Circular dependency detection, topological sorting
# - Resource Management: ResourceBudgetManager, DeferredDestruction, SlotTask
#
# Compatible with VULKAN_TRIMMED_BUILD (headers only, no Vulkan required).
#
# PDB-consolidation (Milestone 1, 2026-07): test_graph_topology and
# test_graph_lifecycle_hooks used to live here as their own executables. Both are
# safe-to-merge by link surface (bare GTest::gtest_main + RenderGraph), but they
# CANNOT be merged into the same binary as each other: both files define an
# out-of-line static member `MockNodeType MockNode::mockType("MockNode");` —
# linking both TUs into one executable produces a duplicate-symbol link error
# (not just an ODR footgun; a hard linker failure). So:
#   - test_graph_topology moved to tests/CMakeLists.txt's test_rendergraph_syncbarrier
#     group (verified no MockNode/MockNodeType name collision there).
#   - test_graph_lifecycle_hooks moved to tests/CMakeLists.txt as its own standalone
#     target (its other candidate neighbor, test_connection_rule, ALSO collides —
#     both define a redundant `int main()`, an LNK2005 duplicate-symbol error caught
#     by an actual Windows/MSVC link during this milestone — so it stays unmerged).
# test_slot_task is untouched here (out of Milestone 1 scope — additionally links
# ResourceManagement, not a bare RenderGraph link surface).
# See Vixen-Docs/04-Development/RenderGraph-Test-PDB-Consolidation-Plan-2026-07.md.
# ===========================================================================

# ---------------------------------------------------------------------------
# Resource Management Tests
# ---------------------------------------------------------------------------
# MOVED to libraries/ResourceManagement/tests/test_resource_management.cpp
# The test now lives with the ResourceManagement library.
message(STATUS "⊗ test_resource_management moved to ResourceManagement library")

# ---------------------------------------------------------------------------
# SlotTask Manager Tests (Phase C)
# ---------------------------------------------------------------------------
# Validates budget-aware task execution:
# - Task generation
# - Sequential and parallel execution
# - Dynamic throttling with budget constraints
# - Memory estimation tracking

message(STATUS "Configuring test_slot_task (trimmed build compatible)")

if(TARGET GTest::gtest_main)
    add_executable(test_slot_task
        test_slot_task.cpp
    )

    target_include_directories(test_slot_task PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
    )

    target_link_libraries(test_slot_task PRIVATE
        GTest::gtest_main
        RenderGraph
        ResourceManagement
    )

    set_target_properties(test_slot_task PROPERTIES FOLDER "Tests/RenderGraph Tests")

    gtest_discover_tests(test_slot_task)

    message(STATUS "✓ test_slot_task configured (Phase C budget-aware execution)")
else()
    message(STATUS "⊗ test_slot_task skipped (GoogleTest not available)")
endif()
