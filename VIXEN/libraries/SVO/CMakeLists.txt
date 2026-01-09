# ==============================================================================
# Sparse Voxel Octree (SVO) Library
# Based on "Efficient Sparse Voxel Octrees" (Laine & Karras, NVIDIA 2010)
# ==============================================================================

cmake_minimum_required(VERSION 3.20)
project(SVO VERSION 1.0.0 LANGUAGES CXX)

# ------------------------------------------------------------------------------
# Library Target
# ------------------------------------------------------------------------------

add_library(SVO STATIC
    # Public headers
    include/ISVOStructure.h
    include/SVOTypes.h
    include/SVOBuilder.h
    include/LaineKarrasOctree.h
    include/SVOLOD.h  # Screen-space LOD parameters (Phase B.2)
    # VoxelInjection.h DEPRECATED - removed in favor of rebuild() API

    # Implementation
    src/ISVOStructure.cpp
    src/SVOTypes.cpp
    src/SVOBuilder.cpp
    src/ContourBuilder.cpp
    src/AttributeIntegrator.cpp

    # LaineKarrasOctree subsystem (Phase A.3 split)
    # - LaineKarrasOctree.cpp: Facade/coordinator, ISVOStructure interface
    # - SVOTraversal.cpp: ESVO ray casting algorithm (Laine & Karras 2010)
    # - SVOBrickDDA.cpp: Brick-level DDA traversal (Amanatides & Woo 1987)
    # - SVORebuild.cpp: Entity-based octree construction with Morton sorting
    src/LaineKarrasOctree.cpp
    src/SVOTraversal.cpp
    src/SVOBrickDDA.cpp
    src/SVORebuild.cpp

    # src/LaineKarrasBuilder.cpp  # TODO: Requires ISVOBuilder::InputGeometry and BuildConfig definitions
    # VoxelInjection.cpp DEPRECATED - removed in favor of rebuild() API
    src/VoxelSamplers.cpp
    # src/RayCaster.cpp  # TODO: Implement
    src/Serialization.cpp
    # src/SVOFactory.cpp  # TODO: Implement
)

# ------------------------------------------------------------------------------
# Compiler Settings
# ------------------------------------------------------------------------------

target_compile_features(SVO PUBLIC cxx_std_23)

# Enable SSE/AVX optimizations on x64
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    if(MSVC)
        target_compile_options(SVO PRIVATE /arch:AVX2 /fp:fast)
    else()
        target_compile_options(SVO PRIVATE -mavx2 -mfma -mf16c -ffast-math)
    endif()
endif()

# Enable parallel build
if(MSVC)
    target_compile_options(SVO PRIVATE /MP)
endif()

# Warnings
if(MSVC)
    target_compile_options(SVO PRIVATE /W4 /WX)
else()
    target_compile_options(SVO PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()

# ------------------------------------------------------------------------------
# Include Directories
# ------------------------------------------------------------------------------

target_include_directories(SVO
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

# GLM for math (fetched by dependencies/CMakeLists.txt)
if(TARGET glm)
    target_link_libraries(SVO PUBLIC glm)
else()
    message(FATAL_ERROR "GLM target not found - ensure dependencies/CMakeLists.txt is included before libraries/")
endif()

# VoxelData for voxel attribute management
if(TARGET VoxelData)
    target_link_libraries(SVO PUBLIC VoxelData)
else()
    message(FATAL_ERROR "VoxelData target not found - ensure libraries/VoxelData is built before SVO")
endif()

# GaiaVoxelWorld for EntityBrickView (Phase 3 - entity-based storage)
if(TARGET GaiaVoxelWorld)
    target_link_libraries(SVO PUBLIC GaiaVoxelWorld)
else()
    message(FATAL_ERROR "GaiaVoxelWorld target not found - ensure libraries/GaiaVoxelWorld is built before SVO")
endif()

# TBB for parallel processing (provided by dependencies/CMakeLists.txt)
if(TARGET TBB::tbb)
    target_link_libraries(SVO PRIVATE TBB::tbb)
    message(STATUS "SVO: TBB linked for parallel processing")
else()
    message(WARNING "SVO: TBB::tbb target not found - parallel processing disabled")
endif()

# ------------------------------------------------------------------------------
# Precompiled Headers
# ------------------------------------------------------------------------------

if(USE_PRECOMPILED_HEADERS)
    target_precompile_headers(SVO PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include/pch.h"
    )
    message(STATUS "SVO: Precompiled headers enabled")
endif()

# ------------------------------------------------------------------------------
# Optional Features
# ------------------------------------------------------------------------------

option(SVO_BUILD_TESTS "Build SVO unit tests" ON)
option(SVO_BUILD_EXAMPLES "Build SVO examples" OFF)  # Disabled - examples not yet created
option(SVO_ENABLE_PROFILING "Enable profiling instrumentation" OFF)
option(SVO_ENABLE_VALIDATION "Enable runtime validation checks" OFF)

if(SVO_ENABLE_PROFILING)
    target_compile_definitions(SVO PUBLIC SVO_ENABLE_PROFILING)
endif()

if(SVO_ENABLE_VALIDATION)
    target_compile_definitions(SVO PUBLIC SVO_ENABLE_VALIDATION)
endif()

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------

if(SVO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ------------------------------------------------------------------------------
# Examples
# ------------------------------------------------------------------------------

if(SVO_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
