cmake_minimum_required(VERSION 3.15)

# Standalone test for array type validation (no Vulkan runtime required)
# This test only validates compile-time type traits

# Ensure Vulkan headers are available (fetches if needed)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/VulkanHeaders.cmake)

add_executable(test_array_type_validation
    test_array_type_validation.cpp
)

# Include RenderGraph headers (for type traits only)
target_include_directories(test_array_type_validation PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../RenderGraph/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${VULKAN_HEADERS_INCLUDE_DIR}  # Vulkan headers (system or fetched)
)

# Set C++23 (required for type traits)
target_compile_features(test_array_type_validation PRIVATE cxx_std_23)

# Link against Vulkan::Headers if available
if(TARGET Vulkan::Headers)
    target_link_libraries(test_array_type_validation PRIVATE Vulkan::Headers)
endif()

# This test doesn't link against RenderGraph library (no Vulkan needed!)
# It only includes headers for compile-time validation

# Note: This is a compile-test executable
# If it compiles successfully, all static_assert tests passed!
# Runtime execution just prints validation results

# Optional: Add to CTest
if(BUILD_TESTING)
    add_test(NAME ArrayTypeValidation COMMAND test_array_type_validation)
    set_tests_properties(ArrayTypeValidation PROPERTIES
        PASS_REGULAR_EXPRESSION "All tests passed"
    )
endif()

# MSVC: Enable /FS for parallel compilation
if(MSVC)
    target_compile_options(test_array_type_validation PRIVATE /FS)
endif()

message(STATUS "Added standalone test: test_array_type_validation (no Vulkan runtime required)")
