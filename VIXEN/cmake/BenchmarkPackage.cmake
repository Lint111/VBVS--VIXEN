# ============================================================================
# VIXEN Benchmark Package Configuration
# ============================================================================
# Creates a self-contained, portable benchmark distribution folder
#
# Usage:
#   cmake --build build --target package_benchmark
#
# Output Structure:
#   VixenBenchmark/
#   ├── vixen_benchmark.exe
#   ├── benchmark_config.json
#   ├── README.txt
#   ├── shaders/           (GLSL sources for runtime compilation)
#   └── *.dll              (Runtime dependencies)

set(BENCHMARK_PACKAGE_DIR "${CMAKE_SOURCE_DIR}/VixenBenchmark")

# ============================================================================
# Shader Files Required for Benchmark
# ============================================================================
# Main shader files used by benchmark pipelines
set(BENCHMARK_SHADER_MAIN
    VoxelRayMarch.comp
    VoxelRayMarch_Compressed.comp
    VoxelRayMarch.frag
    VoxelRayMarch_Compressed.frag
    Fullscreen.vert
    VoxelRT.rgen
    VoxelRT.rmiss
    VoxelRT.rchit
    VoxelRT.rint
    VoxelRT_Compressed.rchit
)

# Shader include files (required by main shaders)
set(BENCHMARK_SHADER_INCLUDES
    Compression.glsl
    CoordinateTransforms.glsl
    ESVOCoefficients.glsl
    ESVOTraversal.glsl
    Lighting.glsl
    Materials.glsl
    OctreeTraversal-ESVO.glsl
    RayGeneration.glsl
    ShaderCounters.glsl
    SVOTypes.glsl
    TraceRecording.glsl
    VoxelTraversal.glsl
)

# ============================================================================
# Package Target
# ============================================================================
add_custom_target(package_benchmark
    COMMENT "Creating portable benchmark package in VixenBenchmark/"
    DEPENDS vixen_benchmark
)

# Create package directory structure
add_custom_command(TARGET package_benchmark PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BENCHMARK_PACKAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BENCHMARK_PACKAGE_DIR}/shaders"
    COMMENT "Creating package directories"
)

# Copy executable
add_custom_command(TARGET package_benchmark POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/binaries/vixen_benchmark.exe"
        "${BENCHMARK_PACKAGE_DIR}/"
    COMMENT "Copying benchmark executable"
)

# Copy TBB DLL
add_custom_command(TARGET package_benchmark POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/binaries/tbb12_debug.dll"
        "${BENCHMARK_PACKAGE_DIR}/"
    COMMENT "Copying TBB DLL"
)

# Copy benchmark config
add_custom_command(TARGET package_benchmark POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/application/benchmark/benchmark_config.json"
        "${BENCHMARK_PACKAGE_DIR}/"
    COMMENT "Copying benchmark config"
)

# Copy main shader files
foreach(shader ${BENCHMARK_SHADER_MAIN})
    add_custom_command(TARGET package_benchmark POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/shaders/${shader}"
            "${BENCHMARK_PACKAGE_DIR}/shaders/"
        COMMENT "Copying shader: ${shader}"
    )
endforeach()

# Copy shader include files
foreach(shader ${BENCHMARK_SHADER_INCLUDES})
    add_custom_command(TARGET package_benchmark POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/shaders/${shader}"
            "${BENCHMARK_PACKAGE_DIR}/shaders/"
        COMMENT "Copying shader include: ${shader}"
    )
endforeach()

# Copy README from VixenBenchmark source (maintained separately for easier editing)
# If the source README doesn't exist, create a minimal one
if(EXISTS "${CMAKE_SOURCE_DIR}/VixenBenchmark/README.txt")
    add_custom_command(TARGET package_benchmark POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/VixenBenchmark/README.txt"
            "${BENCHMARK_PACKAGE_DIR}/README.txt"
        COMMENT "Copying README from source"
    )
else()
    # Fallback: generate minimal README if source doesn't exist
    file(WRITE "${CMAKE_BINARY_DIR}/benchmark_readme.txt"
"================================================================================
VIXEN Benchmark Tool
================================================================================

QUICK START:
1. Double-click vixen_benchmark.exe
2. Wait for benchmark to complete (5-15 minutes)
3. Find results in Downloads/VIXEN_Benchmarks/

REQUIREMENTS:
- Windows 10/11 with Vulkan-capable GPU
- Updated graphics drivers

TROUBLESHOOTING:
- Update GPU drivers if Vulkan errors occur
- Try --headless flag if window is black
- Run as Administrator if crashes occur

For full documentation, visit: https://github.com/VIXEN-Engine/VIXEN
================================================================================
")
    add_custom_command(TARGET package_benchmark POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/benchmark_readme.txt"
            "${BENCHMARK_PACKAGE_DIR}/README.txt"
        COMMENT "Generating fallback README"
    )
endif()

# Print completion message
add_custom_command(TARGET package_benchmark POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "=========================================="
    COMMAND ${CMAKE_COMMAND} -E echo "Benchmark package created successfully!"
    COMMAND ${CMAKE_COMMAND} -E echo "Location: ${BENCHMARK_PACKAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E echo "=========================================="
    COMMENT "Package complete"
)

message(STATUS "Benchmark packaging target configured: package_benchmark")
