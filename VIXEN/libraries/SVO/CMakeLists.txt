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
    include/VoxelInjection.h

    # Implementation
    src/ISVOStructure.cpp
    src/SVOTypes.cpp
    src/SVOBuilder.cpp
    src/ContourBuilder.cpp
    src/AttributeIntegrator.cpp
    src/LaineKarrasOctree.cpp
    # src/LaineKarrasBuilder.cpp  # TODO: Requires ISVOBuilder::InputGeometry and BuildConfig definitions
    src/VoxelInjection.cpp
    src/VoxelSamplers.cpp
    # src/RayCaster.cpp  # TODO: Implement
    # src/Serialization.cpp  # TODO: Implement
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

# TBB for parallel processing (optional - fetch if not available)
find_package(TBB QUIET)
if(TBB_FOUND)
    target_link_libraries(SVO PRIVATE TBB::tbb)
    message(STATUS "TBB found - multi-threading enabled")
else()
    message(STATUS "TBB not found - fetching oneTBB from GitHub")
    include(FetchContent)
    FetchContent_Declare(
        TBB
        GIT_REPOSITORY https://github.com/oneapi-src/oneTBB.git
        GIT_TAG v2021.11.0
        GIT_SHALLOW TRUE
    )
    set(TBB_TEST OFF CACHE BOOL "" FORCE)
    set(TBB_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(TBB)
    target_link_libraries(SVO PRIVATE TBB::tbb)
    set_target_properties(tbb PROPERTIES FOLDER "Dependencies")
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
