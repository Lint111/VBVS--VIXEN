# ============================================================================
# VIXEN Benchmark Executable
# ============================================================================
# Config-only benchmark orchestrator - all Vulkan handled by BenchmarkRunner
#
# Usage:
#   vixen_benchmark [--config benchmark.json] [--output ./results]
#   vixen_benchmark --quick --headless --output ./results
#   vixen_benchmark --list-gpus
#
# Architecture:
#   - BenchmarkMain.cpp contains ZERO Vulkan API calls
#   - CLI parsing creates BenchmarkSuiteConfig
#   - BenchmarkRunner::RunSuite() handles all Vulkan lifecycle
#
# Features:
#   - Headless or windowed execution modes
#   - Configurable test matrix via CLI or JSON
#   - JSON/CSV export of benchmark results
#   - GPU enumeration and selection

cmake_minimum_required(VERSION 3.15...4.2 FATAL_ERROR)

# ============================================================================
# Source Files
# ============================================================================

set(BENCHMARK_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/BenchmarkMain.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/BenchmarkCLI.cpp
)

set(BENCHMARK_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/BenchmarkCLI.h
)

# ============================================================================
# Executable Target
# ============================================================================

add_executable(vixen_benchmark
    ${BENCHMARK_SOURCES}
    ${BENCHMARK_HEADERS}
)

# ============================================================================
# Include Directories
# ============================================================================

target_include_directories(vixen_benchmark
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ============================================================================
# Link Libraries
# ============================================================================

target_link_libraries(vixen_benchmark
    PRIVATE
        Profiler
        RenderGraph       # RenderGraph + all node types
        Logger
        ${VULKAN_LIB_LIST}
)

# ============================================================================
# Shader Source Directory
# ============================================================================

# Pass shader source directory for runtime compilation
target_compile_definitions(vixen_benchmark
    PRIVATE
        VIXEN_SHADER_SOURCE_DIR="${VIXEN_SHADER_SOURCE_DIR}"
)

# ============================================================================
# Compiler Settings
# ============================================================================

# C++ standard
set_target_properties(vixen_benchmark PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)

# MSVC settings
if(MSVC)
    target_compile_options(vixen_benchmark PRIVATE
        /W4    # High warning level
        /FS    # Force serialized PDB writes
        /MP    # Multi-processor compilation
    )
else()
    target_compile_options(vixen_benchmark PRIVATE
        -Wall
        -Wextra
        -pedantic
    )
endif()

# ============================================================================
# Output Directory
# ============================================================================

# Output to binaries/ alongside main VIXEN executable
set_target_properties(vixen_benchmark PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/binaries
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/binaries
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/binaries
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_SOURCE_DIR}/binaries
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_SOURCE_DIR}/binaries
)

# ============================================================================
# Visual Studio Organization
# ============================================================================

set_target_properties(vixen_benchmark PROPERTIES
    FOLDER "Application"
)

# Source groups for IDE
if(WIN32)
    source_group("include" REGULAR_EXPRESSION "include/*")
    source_group("source" REGULAR_EXPRESSION "source/*")
endif()

# ============================================================================
# Precompiled Headers (Optional)
# ============================================================================

if(USE_PRECOMPILED_HEADERS)
    # Could add benchmark-specific PCH if needed
    # For now, rely on Profiler library's PCH
endif()

# ============================================================================
# Runtime DLL Dependencies
# ============================================================================
# Copy TBB DLL to output directory (required by SVO library)
if(TARGET TBB::tbb)
    add_custom_command(TARGET vixen_benchmark POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb>
            $<TARGET_FILE_DIR:vixen_benchmark>
        COMMENT "Copying TBB DLL for vixen_benchmark"
    )
    message(STATUS "vixen_benchmark: TBB DLL will be copied to output directory")
endif()

# ============================================================================
# Installation
# ============================================================================

install(TARGETS vixen_benchmark
    RUNTIME DESTINATION bin
)

# Install default benchmark config if exists
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/benchmark_config.json)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/benchmark_config.json
        DESTINATION bin
    )
endif()

# Install benchmark schema for data validation
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/benchmark_schema.json)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/benchmark_schema.json
        DESTINATION bin
    )
endif()

# Copy schema and config to binaries folder for development use
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/benchmark_schema.json)
    add_custom_command(TARGET vixen_benchmark POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/benchmark_schema.json
            $<TARGET_FILE_DIR:vixen_benchmark>
        COMMENT "Copying benchmark_schema.json to output directory"
    )
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/benchmark_config.json)
    add_custom_command(TARGET vixen_benchmark POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/benchmark_config.json
            $<TARGET_FILE_DIR:vixen_benchmark>
        COMMENT "Copying benchmark_config.json to output directory"
    )
endif()

# ============================================================================
# Standalone Benchmark Package Target
# ============================================================================
# Creates VixenBenchmark/ directory with all required files for distribution
# Usage: cmake --build build --config Release --target benchmark_package
#
# This target:
# - Copies vixen_benchmark.exe to VixenBenchmark/
# - Copies TBB DLL (correct Debug/Release version based on build config)
# - Copies benchmark_config.json and benchmark_schema.json
# - Copies shader sources for runtime compilation
# - Creates README.txt with usage instructions

set(BENCHMARK_PACKAGE_DIR "${CMAKE_SOURCE_DIR}/VixenBenchmark")

# Create the package target
add_custom_target(benchmark_package
    DEPENDS vixen_benchmark
    COMMENT "Creating standalone benchmark package in VixenBenchmark/"
)

# Create directories and copy all files in one command block
add_custom_command(TARGET benchmark_package POST_BUILD
    # Create directories
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BENCHMARK_PACKAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BENCHMARK_PACKAGE_DIR}/shaders"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BENCHMARK_PACKAGE_DIR}/cache"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BENCHMARK_PACKAGE_DIR}/generated"
    # Copy executable
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:vixen_benchmark>"
        "${BENCHMARK_PACKAGE_DIR}/"
    # Copy config files
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/benchmark_config.json"
        "${BENCHMARK_PACKAGE_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/benchmark_config_logic_test.json"
        "${BENCHMARK_PACKAGE_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/benchmark_schema.json"
        "${BENCHMARK_PACKAGE_DIR}/"
    COMMENT "Copying benchmark executable and config files"
)

# Copy TBB DLL only for Debug builds (Release links statically)
# Use generator expression to only execute copy when file is a DLL
if(TARGET TBB::tbb)
    add_custom_command(TARGET benchmark_package POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            "-DSRC=$<TARGET_FILE:TBB::tbb>"
            "-DDST=${BENCHMARK_PACKAGE_DIR}/"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyIfDll.cmake"
        COMMENT "Copying TBB DLL if needed ($<CONFIG>)"
    )
endif()

# Copy shader sources for runtime compilation
add_custom_command(TARGET benchmark_package POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/shaders"
        "${BENCHMARK_PACKAGE_DIR}/shaders"
    COMMENT "Copying shader sources"
)

# Generate README for the package (written at configure time, copied at build time)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/README_package.txt" [=[
VIXEN Benchmark Tool - Standalone Package
==========================================

Usage:
  vixen_benchmark.exe [options]

Quick Start:
  vixen_benchmark.exe --config benchmark_config_logic_test.json  # Logic test (6 configs, ~1 min)
  vixen_benchmark.exe --quick --render                           # Quick test (12 configs)
  vixen_benchmark.exe --quick --headless                         # Quick headless test
  vixen_benchmark.exe --full --render                            # Full matrix (144 configs)

Options:
  --help              Show all options
  --list-gpus         List available GPUs
  --runs N            Run each config N times for statistical robustness
  --config FILE       Use custom JSON configuration
  --output DIR        Output directory for results

Results are saved to: Downloads/VIXEN_Benchmarks/

Package Contents:
  vixen_benchmark.exe              - Benchmark executable
  tbb12*.dll                       - Threading Building Blocks library
  benchmark_config.json            - Default test configuration (144 tests)
  benchmark_config_logic_test.json - Minimal logic test (6 tests)
  benchmark_schema.json            - JSON schema for result validation
  shaders/                         - GLSL shader sources (compiled at runtime)
  cache/                           - Shader compilation cache (auto-populated)
  generated/                       - Generated files (auto-populated)

Configuration Variants:
  benchmark_config_logic_test.json - Minimal matrix for testing logic
    - 1 resolution (64³), 1 scene (cornell), 1 screen size (1280x720)
    - All 3 pipeline types (COMPUTE, FRAGMENT, HW_RT)
    - Both compression variants per pipeline
    - Total: 6 tests (~1 minute with multi-GPU)
    - Use for: Multi-GPU logic testing, quick smoke tests

  benchmark_config.json - Full performance matrix
    - 3 resolutions (64³, 128³, 256³)
    - 4 scenes (cornell, noise, tunnels, cityscape)
    - 2 screen sizes (720p, 1080p)
    - All 3 pipeline types with compression variants
    - Total: 144 tests (~30-60 minutes per GPU)
    - Use for: Performance data collection, thorough testing
]=])

add_custom_command(TARGET benchmark_package POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_BINARY_DIR}/README_package.txt"
        "${BENCHMARK_PACKAGE_DIR}/README.txt"
    COMMENT "Creating README.txt"
)

# Set folder for Visual Studio
set_target_properties(benchmark_package PROPERTIES FOLDER "Application")

message(STATUS "VIXEN Benchmark: Configured headless compute benchmark tool")
message(STATUS "VIXEN Benchmark: Use 'cmake --build build --config Release --target benchmark_package' to create standalone package")
