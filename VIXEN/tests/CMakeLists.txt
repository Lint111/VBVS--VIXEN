# ============================================================================
# VIXEN Project Test Suite
# ============================================================================
#
# This file configures all unit tests for the project using Google Test.
# Each library has its own test subdirectory with focused unit tests.
#
# To run tests:
#   cmake --build . && ctest
#   or
#   cmake --build . && ctest --output-on-failure
#

cmake_minimum_required(VERSION 3.15)

# This CMakeLists aggregates all per-library test subdirectories so the top-level
# CMake can simply call add_subdirectory(tests).

# Include Google Test helpers (provided by FetchContent in the top-level CMake)
include(GoogleTest OPTIONAL)

# Common test compile options
if(MSVC)
    add_compile_options(/W4)  # Warning level 4
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Helper macro: add external test directory from outside the tests/ folder
macro(maybe_add_external_subdir dir)
    if(EXISTS "${PROJECT_SOURCE_DIR}/${dir}/CMakeLists.txt")
        message(STATUS "Adding external tests subdir: ${dir}")
        add_subdirectory(${PROJECT_SOURCE_DIR}/${dir} ${CMAKE_CURRENT_BINARY_DIR}/${dir})
    else()
        message(STATUS "Skipping external tests subdir (missing): ${dir}")
    endif()
endmacro()

# All tests are now in component-local directories.
# Reference them from their respective component locations.
# Note: RenderGraph and ShaderManagement tests are included directly from their component CMakeLists.txt
set(EXTERNAL_TEST_DIRS
    logger/tests
    EventBus/tests
    CashSystem/tests
    ResourceManagement/tests
    source/VulkanResources/tests
)

foreach(external_subdir IN LISTS EXTERNAL_TEST_DIRS)
    maybe_add_external_subdir(${external_subdir})
endforeach()

message(STATUS "Tests aggregation ready. Use ctest to run the discovered tests.")
