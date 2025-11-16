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

# ============================================================================
# Test Organization Note
# ============================================================================
#
# Tests have been migrated to component-local directories:
#   - CashSystem/tests/
#   - EventBus/tests/
#   - logger/tests/
#   - RenderGraph/tests/
#   - ResourceManagement/tests/
#   - ShaderManagement/tests/
#   - source/VulkanResources/tests/
#
# Each component's CMakeLists.txt now includes its own tests/ subdirectory
# when BUILD_TESTS is enabled. This centralized tests/ directory is deprecated
# and should be removed once migration is complete.
#
# ============================================================================

# ============================================================================
# Test Discovery Complete
# ============================================================================
# All component tests are now added via their respective CMakeLists.txt files:
#   - logger/CMakeLists.txt includes logger/tests/
#   - EventBus/CMakeLists.txt includes EventBus/tests/
#   - ResourceManagement/CMakeLists.txt includes ResourceManagement/tests/
#   - ShaderManagement/CMakeLists.txt includes ShaderManagement/tests/
#   - source/VulkanResources/CMakeLists.txt includes source/VulkanResources/tests/
#   - RenderGraph/CMakeLists.txt includes RenderGraph/tests/
#   - CashSystem/CMakeLists.txt includes CashSystem/tests/
#
# This centralized tests/ directory serves only as a common configuration point.
# No test targets are defined here directly.

message(STATUS "Tests aggregation ready. Use ctest to run the discovered tests.")
