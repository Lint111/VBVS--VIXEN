cmake_minimum_required(VERSION 3.15)
project(ResourceManagementTests)

add_executable(test_resourcemanagement_basic test_resourcemanagement_basic.cpp)
if(TARGET ResourceManagement)
    target_link_libraries(test_resourcemanagement_basic PRIVATE GTest::gtest_main ResourceManagement)
else()
    target_link_libraries(test_resourcemanagement_basic PRIVATE GTest::gtest_main)
endif()

# Temporarily disabled - GTest not configured
# gtest_discover_tests(test_resourcemanagement_basic)
