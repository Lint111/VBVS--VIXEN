# Profiler - Performance profiling and benchmarking system
# External system (like EventBus) for collecting metrics via GraphLifecycleHooks

cmake_minimum_required(VERSION 3.20)

project(Profiler VERSION 1.0.0 LANGUAGES CXX)

# ============================================================================
# Library sources
# ============================================================================

add_library(Profiler STATIC
    src/RollingStats.cpp
    src/DeviceCapabilities.cpp
    src/MetricsCollector.cpp
    src/MetricsExporter.cpp
    src/MetricsSanityChecker.cpp
    src/NVMLWrapper.cpp
    src/BenchmarkConfig.cpp
    src/BenchmarkRunner.cpp
    src/BenchmarkGraphFactory.cpp
    src/SceneInfo.cpp
    src/ProfilerSystem.cpp
    src/VulkanIntegration.cpp
    src/FrameCapture.cpp
    src/TesterPackage.cpp
)

# ============================================================================
# Include directories
# ============================================================================

target_include_directories(Profiler
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_BINARY_DIR}/_deps/stb-src  # For stb_image_write.h and stb_image_resize2.h
)

# ============================================================================
# Dependencies
# ============================================================================

target_link_libraries(Profiler
    PUBLIC
        Logger
        nlohmann_json::nlohmann_json
        RenderGraph
        EventBus
    PRIVATE
        VulkanResources
        ShaderManagement
        ResourceManagement  # Sprint 4 Phase D: For DeviceBudgetManager metrics
        miniz
)

# ============================================================================
# Shader Source Directory
# ============================================================================

# Pass shader source directory for runtime shader compilation in BenchmarkGraphFactory
if(VIXEN_SHADER_SOURCE_DIR)
    target_compile_definitions(Profiler
        PRIVATE
            VIXEN_SHADER_SOURCE_DIR="${VIXEN_SHADER_SOURCE_DIR}"
    )
endif()

# ============================================================================
# Compiler settings
# ============================================================================

target_compile_features(Profiler PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(Profiler PRIVATE /W4 /FS)
else()
    target_compile_options(Profiler PRIVATE -Wall -Wextra -pedantic)
endif()

# ============================================================================
# Export target
# ============================================================================

add_library(Profiler::Profiler ALIAS Profiler)

# Visual Studio solution folder
set_target_properties(Profiler PROPERTIES FOLDER "Libraries")

# ============================================================================
# Tests
# ============================================================================

if(BUILD_TESTING)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        add_subdirectory(tests)
    endif()
endif()

# ============================================================================
# Precompiled Headers
# ============================================================================

if(USE_PRECOMPILED_HEADERS)
    target_precompile_headers(Profiler PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/ProfilerHeaders.h"
    )
    message(STATUS "Profiler: Precompiled headers enabled (using ProfilerHeaders.h)")
endif()

# ============================================================================
# Installation
# ============================================================================

install(TARGETS Profiler
    EXPORT ProfilerTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)
