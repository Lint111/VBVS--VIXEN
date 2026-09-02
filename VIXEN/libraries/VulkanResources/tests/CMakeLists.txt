cmake_minimum_required(VERSION 3.15)
project(VulkanResourcesTests)

# Include GoogleTest utilities (for gtest_discover_tests)
include(GoogleTest)

add_executable(test_vulkanresources_basic test_vulkanresources_basic.cpp)
target_include_directories(test_vulkanresources_basic PRIVATE
    ${VIXEN_ROOT}/application/main/include
)
if(TARGET VulkanResources)
    target_link_libraries(test_vulkanresources_basic PRIVATE GTest::gtest_main VulkanResources)
else()
    target_link_libraries(test_vulkanresources_basic PRIVATE GTest::gtest_main)
endif()

# Visual Studio solution folder organization
set_target_properties(test_vulkanresources_basic PROPERTIES FOLDER "Tests/VulkanResources Tests")

gtest_discover_tests(test_vulkanresources_basic)

# 0ew selector policy witness: compile against a deliberately absent Dozen manifest so the test
# proves strict WSL behavior independently of whatever the active build provisioned.
add_executable(test_vulkan_global_names_policy test_vulkan_global_names_policy.cpp)
target_link_libraries(test_vulkan_global_names_policy PRIVATE GTest::gtest_main VulkanResources)
target_compile_definitions(test_vulkan_global_names_policy PRIVATE
    VIXEN_WSL_DZN_ICD="${CMAKE_CURRENT_BINARY_DIR}/dzn_policy_missing_manifest.json")
set_target_properties(test_vulkan_global_names_policy PROPERTIES FOLDER "Tests/VulkanResources Tests")
gtest_discover_tests(test_vulkan_global_names_policy)
