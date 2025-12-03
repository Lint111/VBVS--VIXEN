# Profiler Tests
# Unit tests for RollingStats, BenchmarkConfig, and MetricsExporter

# Include Google Test helpers (provided by FetchContent in the top-level CMake)
include(GoogleTest)

# ============================================================================
# Test executable
# ============================================================================

add_executable(test_profiler
    test_profiler.cpp
)

target_link_libraries(test_profiler
    PRIVATE
        Profiler
        GTest::gtest
        GTest::gtest_main
)

# Set output directory
set_target_properties(test_profiler PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/libraries/Profiler/tests/Debug"
    FOLDER "Tests/Libraries"
)

# Register with CTest
gtest_discover_tests(test_profiler
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    PROPERTIES LABELS "Profiler"
    DISCOVERY_TIMEOUT 30
)

message(STATUS "[Profiler Tests] Added: test_profiler")
