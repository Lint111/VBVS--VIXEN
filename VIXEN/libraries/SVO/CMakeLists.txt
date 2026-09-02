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
    include/CellFootprintRegime.h  # Ray-vs-cell sampling/residency policy
    include/PhotonCells.h  # C0/C1 photon world-cell cache mirror
    include/Recipe/BodyFootprintRegime.h  # Shared CPU/GLSL rendering-regime policy
    include/Recipe/LodSelection.h  # Recipe LOD ladder and CPU transition contract (Wave A)
    include/Recipe/MultiBodyRenderer.h  # Multi-body placement, per-body LOD, and draw ordering
    include/Recipe/RecipeComposition.h  # Recipe composition blocks and LOD resolution (Wave B)
    include/SceneGenerator.h  # Procedural voxel scene generation (relocated from RenderGraph — AR#3/#4 cycle break)
    include/BulkMaterialization.h
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
    src/SceneGenerator.cpp  # Procedural voxel scene generation (relocated from RenderGraph — AR#3/#4 cycle break)
    src/BulkMaterialization.cpp
    # src/SVOFactory.cpp  # TODO: Implement
    src/RecipePackLoader.cpp  # I2.5: manifest+blobs→registry, fail-loud
    src/Recipe/LodSelection.cpp  # Recipe LOD selection, hysteresis, and upload gating (Wave A)
    src/Recipe/MultiBodyRenderer.cpp  # Multi-body render-list builder (R2)
    src/Recipe/RecipeComposition.cpp  # Recipe composition role validation and resolution (Wave B)
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

# The materialization evaluator is a serialization boundary: any floating-point
# contraction or reassociation can alter the canonical octree bytes.  Keep the
# wider SVO target's historical policy, but pin this translation unit to strict,
# non-contracting arithmetic so scalar and SIMD4 lanes execute the same sequence.
if(MSVC)
    set_source_files_properties(src/BulkMaterialization.cpp PROPERTIES
        COMPILE_OPTIONS "/fp:strict"
        SKIP_PRECOMPILE_HEADERS ON)
else()
    # The strict-FP options differ from the target's flags, so this TU cannot share the
    # target-level PCH (gcc rejects the mismatch as -Werror=invalid-pch). Build it PCH-free.
    set_source_files_properties(src/BulkMaterialization.cpp PROPERTIES
        COMPILE_OPTIONS "-fno-fast-math;-ffp-contract=off"
        SKIP_PRECOMPILE_HEADERS ON)
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
if(TARGET KernelDispatch)
    target_link_libraries(SVO PUBLIC KernelDispatch)
else()
    message(FATAL_ERROR "KernelDispatch target not found - ensure libraries/KernelDispatch is built before SVO")
endif()

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

# Respect the global test switch (BUILD_TESTS/BUILD_TESTING) like the other libraries — otherwise
# SVO_BUILD_TESTS (default ON) builds tests even under -DBUILD_TESTS=OFF, breaking on the absent GTest.
if(SVO_BUILD_TESTS AND BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ------------------------------------------------------------------------------
# Examples
# ------------------------------------------------------------------------------

if(SVO_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
