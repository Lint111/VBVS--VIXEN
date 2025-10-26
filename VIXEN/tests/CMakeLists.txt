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

cmake_minimum_required(VERSION 3.11)

# Include Google Test helpers
include(GoogleTest)

# ============================================================================
# TEST CONFIGURATION
# ============================================================================

# Test discovery mode - use CMake's built-in test discovery
set(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST)

# Common test compile options
if(MSVC)
    add_compile_options(/W4)  # Warning level 4
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ============================================================================
# LIBRARY TESTS
# ============================================================================

# ShaderManagement library tests
# TEMPORARILY DISABLED - integration issues
# add_subdirectory(ShaderManagement)

# Add more library test directories here as they are created:
# add_subdirectory(EventBus)
# add_subdirectory(ResourceManagement)
# add_subdirectory(RenderGraph)

# ============================================================================
# TEST SUMMARY
# ============================================================================

message(STATUS "")
message(STATUS "═══════════════════════════════════════════════════════════")
message(STATUS "  Test Suite Configuration")
message(STATUS "═══════════════════════════════════════════════════════════")
message(STATUS "  Enabled test suites:")
# message(STATUS "    - ShaderManagement")
message(STATUS "    (none currently enabled)")
message(STATUS "")
message(STATUS "  Run tests with:")
message(STATUS "    ctest")
message(STATUS "    ctest --output-on-failure")
message(STATUS "    ctest --verbose")
message(STATUS "═══════════════════════════════════════════════════════════")
message(STATUS "")
