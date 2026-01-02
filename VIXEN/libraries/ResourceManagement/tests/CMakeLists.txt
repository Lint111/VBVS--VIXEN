cmake_minimum_required(VERSION 3.15)
project(ResourceManagementTests)

# ============================================================================
# Basic State Tests (original)
# ============================================================================

add_executable(test_resourcemanagement_basic test_resourcemanagement_basic.cpp)
target_link_libraries(test_resourcemanagement_basic PRIVATE
    GTest::gtest_main
    ResourceManagement
)
set_target_properties(test_resourcemanagement_basic PROPERTIES FOLDER "Tests/ResourceManagement Tests")

# ============================================================================
# Resource Management Tests (memory, budget, lifetime)
# ============================================================================

add_executable(test_resource_management test_resource_management.cpp)
target_link_libraries(test_resource_management PRIVATE
    GTest::gtest_main
    ResourceManagement
    RenderGraph  # For SlotTask and StatefulContainer tests
)
set_target_properties(test_resource_management PROPERTIES FOLDER "Tests/ResourceManagement Tests")

# ============================================================================
# Budget Manager Integration Tests
# ============================================================================

add_executable(test_budget_manager_integration test_budget_manager_integration.cpp)
target_link_libraries(test_budget_manager_integration PRIVATE
    GTest::gtest_main
    ResourceManagement
)
set_target_properties(test_budget_manager_integration PROPERTIES FOLDER "Tests/ResourceManagement Tests")

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(test_resourcemanagement_basic)
gtest_discover_tests(test_resource_management)
gtest_discover_tests(test_budget_manager_integration)
