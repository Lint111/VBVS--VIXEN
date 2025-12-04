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

message(STATUS "VIXEN Benchmark: Configured headless compute benchmark tool")
