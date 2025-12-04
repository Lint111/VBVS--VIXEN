# ============================================================================
# VIXEN Application Executables
# ============================================================================
# All application entry points and executables

cmake_minimum_required(VERSION 3.15...4.2 FATAL_ERROR)

# Main application executable
add_subdirectory(main)

# Benchmark tool (headless compute profiler)
add_subdirectory(benchmark)

message(STATUS "Application configured - all executables defined")
