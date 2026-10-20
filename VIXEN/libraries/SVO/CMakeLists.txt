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
# Kernel-owned generated headers retain their golden gate, while diagnostics in
# them follow the same system-header policy as other imported providers.
target_include_directories(SVO SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include/Recipe/generated")
if(MSVC)
    # BodyInstanceGpu is over-aligned. stable_sort's temporary storage must honor
    # that alignment rather than MSVC's historical max_align_t compatibility ABI.
    target_compile_definitions(SVO PUBLIC _ENABLE_EXTENDED_ALIGNED_STORAGE)
endif()

# Native SIMD ISA policy (owner ruling R645.4): ONE compile-time x86-64-v3 floor (AVX2, FMA, F16C,
# BMI1/2, LZCNT), no runtime multiversioning. The UndersetLoader detects the machine and selects
# environment-specific handling, so a pre-v3 host gets a loader-selected launcher variant instead
# of an illegal-instruction crash here. Non-x86 targets keep the portable SIMD4 scalar path of the
# generated RecipeSimd header. MSVC has no finer switch than /arch:AVX2, which covers the v3 set.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|x64)$")
    if(MSVC)
        target_compile_options(SVO PRIVATE /arch:AVX2)
    else()
        include(CheckCXXCompilerFlag)
        check_cxx_compiler_flag(-march=x86-64-v3 VIXEN_SVO_COMPILER_HAS_X86_64_V3)
        if(NOT VIXEN_SVO_COMPILER_HAS_X86_64_V3)
            message(FATAL_ERROR "SVO: the native ISA floor is x86-64-v3, but ${CMAKE_CXX_COMPILER_ID} "
                "${CMAKE_CXX_COMPILER_VERSION} does not accept -march=x86-64-v3 (needs GCC 11+ or Clang 12+)")
        endif()
        target_compile_options(SVO PRIVATE -march=x86-64-v3)
    endif()
endif()
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    if(MSVC)
        target_compile_options(SVO PUBLIC /fp:strict)
    else()
        target_compile_options(SVO PUBLIC -fno-fast-math -ffp-contract=off)
    endif()
endif()

add_test(NAME SVO.IsaFloor
    COMMAND "${CMAKE_COMMAND}"
        "-DSVO_COMPILE_OPTIONS=$<TARGET_PROPERTY:SVO,COMPILE_OPTIONS>"
        "-DPROCESSOR=${CMAKE_SYSTEM_PROCESSOR}" -DMSVC=${MSVC}
        -P "${VIXEN_ROOT}/cmake/tests/test_svo_isa_floor.cmake")
set_tests_properties(SVO.IsaFloor PROPERTIES LABELS "cpu")

# Recipe hashes and the materialization evaluator are determinism boundaries: floating-point
# contraction or fast-math assumptions must not alter raw uint32 float-slot payloads or canonical
# octree bytes. Export the strict policy to every consumer of the header-only recipe evaluator.
# The explicit per-source setting below remains a guard if the target-wide policy changes later.
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

# BakeArtifactCache is header-only and uses Core's atomic runtime-cache writer.
if(TARGET Core::Core)
    target_link_libraries(SVO PUBLIC Core::Core)
else()
    message(FATAL_ERROR "Core::Core target not found - ensure libraries/Core is built before SVO")
endif()

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

# KernelDispatch owns worker admission and its TBB implementation. SVOBuilder receives a
# non-owning TaskExecutor from its host; it does not create or link a private TBB arena.
if(TARGET KernelDispatch)
    target_link_libraries(SVO PUBLIC KernelDispatch)
else()
    message(FATAL_ERROR "KernelDispatch target not found - ensure libraries/KernelDispatch is built before SVO")
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
