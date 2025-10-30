cmake_minimum_required(VERSION 3.15)
project(ResourceManagementTests)

add_executable(test_resourcemanagement_basic test_resourcemanagement_basic.cpp)
if(TARGET ResourceManagement)
    target_link_libraries(test_resourcemanagement_basic PRIVATE GTest::gtest_main ResourceManagement)
else()
    target_link_libraries(test_resourcemanagement_basic PRIVATE GTest::gtest_main)
endif()

gtest_discover_tests(test_resourcemanagement_basic)

# Additional test: ensure ResourceVariant accepts platform handle types used by nodes
add_executable(test_resourcevariant_handles test_resourcevariant_handles.cpp)
if(TARGET ResourceManagement)
    target_link_libraries(test_resourcevariant_handles PRIVATE GTest::gtest_main ResourceManagement)
else()
    target_link_libraries(test_resourcevariant_handles PRIVATE GTest::gtest_main)
endif()

gtest_discover_tests(test_resourcevariant_handles)

# Ensure RenderGraph include dir is available so ResourceVariant.h can find its sibling headers
target_include_directories(test_resourcevariant_handles PRIVATE ${CMAKE_SOURCE_DIR}/RenderGraph/include)
