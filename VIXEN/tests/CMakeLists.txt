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

# Helper macro: add subdirectory only if folder exists and contains CMakeLists.txt
macro(maybe_add_subdir dir)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/CMakeLists.txt")
        message(STATUS "Adding tests subdir: ${dir}")
        add_subdirectory(${dir})
    else()
        message(STATUS "Skipping tests subdir (missing): ${dir}")
    endif()
endmacro()

# List of test subfolders to include. Add new entries as needed.
set(TEST_SUBFOLDERS
    RenderGraph
    EventBus
    ResourceManagement
    Logger
    VulkanResources
)

# Optionally include ShaderManagement tests only when the library or headers are available.
# This prevents test aggregation failures on machines that don't build ShaderManagement yet.
if(TARGET ShaderManagement OR EXISTS "${CMAKE_SOURCE_DIR}/ShaderManagement/include/ShaderManagement/ShaderProgram.h")
    list(APPEND TEST_SUBFOLDERS ShaderManagement)
else()
    message(STATUS "Skipping ShaderManagement tests: library target or headers not available")
endif()

foreach(subdir IN LISTS TEST_SUBFOLDERS)
    maybe_add_subdir(${subdir})
endforeach()

message(STATUS "Tests aggregation ready. Use ctest to run the discovered tests.")
