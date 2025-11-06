# ===========================================================================
# ResourceDependencyTracker Tests - Core Infrastructure
# ===========================================================================
# This test suite validates the ResourceDependencyTracker class (Core infrastructure):
# - Resource-to-producer mapping (register, query, update)
# - Producer-to-resources bidirectional mapping
# - nullptr handling and edge cases
# - Clear functionality and state management
# - Multiple resources per producer
# - Resource reassignment
# - Performance characteristics (1000+ resources)
#
# NOTE: GetDependenciesForNode() and BuildCleanupDependencies() require
# full NodeInstance integration and are tested in integration test suites.
#
# Compatible with VULKAN_TRIMMED_BUILD (headers only, no Vulkan required).

add_executable(test_resource_dependency_tracker
    Core/test_resource_dependency_tracker.cpp
)

target_link_libraries(test_resource_dependency_tracker PRIVATE
    GTest::gtest_main
    RenderGraph
)

gtest_discover_tests(test_resource_dependency_tracker)

message(STATUS "[RenderGraph Tests] Added: test_resource_dependency_tracker")
