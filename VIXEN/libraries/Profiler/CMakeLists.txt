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
    src/BenchmarkConfig.cpp
    src/ProfilerSystem.cpp
)

# ============================================================================
# Include directories
# ============================================================================

target_include_directories(Profiler
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# ============================================================================
# Dependencies
# ============================================================================

target_link_libraries(Profiler
    PUBLIC
        Logger
        nlohmann_json::nlohmann_json
    PRIVATE
        VulkanResources
)

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
