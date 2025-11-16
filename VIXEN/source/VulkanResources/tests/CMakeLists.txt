cmake_minimum_required(VERSION 3.15)
project(VulkanResourcesTests)

add_executable(test_vulkanresources_basic test_vulkanresources_basic.cpp)
target_include_directories(test_vulkanresources_basic PRIVATE ${CMAKE_SOURCE_DIR})
if(TARGET VulkanResources)
    target_link_libraries(test_vulkanresources_basic PRIVATE GTest::gtest_main VulkanResources)
else()
    target_link_libraries(test_vulkanresources_basic PRIVATE GTest::gtest_main)
endif()

gtest_discover_tests(test_vulkanresources_basic)
