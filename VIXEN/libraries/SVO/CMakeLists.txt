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
    include/SVO/ISVOStructure.h
    include/SVO/SVOTypes.h
    include/SVO/SVOBuilder.h
    include/SVO/LaineKarrasOctree.h

    # Implementation
    src/SVOTypes.cpp
    src/SVOBuilder.cpp
    src/ContourBuilder.cpp
    src/AttributeIntegrator.cpp
    src/LaineKarrasOctree.cpp
    src/LaineKarrasBuilder.cpp
    src/RayCaster.cpp
    src/Serialization.cpp
    src/SVOFactory.cpp
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

# GLM for math
find_package(glm REQUIRED)
target_link_libraries(SVO PUBLIC glm::glm)

# TBB for parallel processing
find_package(TBB REQUIRED)
target_link_libraries(SVO PRIVATE TBB::tbb)

# ------------------------------------------------------------------------------
# Optional Features
# ------------------------------------------------------------------------------

option(SVO_BUILD_TESTS "Build SVO unit tests" ON)
option(SVO_BUILD_EXAMPLES "Build SVO examples" ON)
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

# ------------------------------------------------------------------------------
# Installation
# ------------------------------------------------------------------------------

include(GNUInstallDirs)

install(TARGETS SVO
    EXPORT SVOTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT SVOTargets
    FILE SVOTargets.cmake
    NAMESPACE SVO::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SVO
)

# ------------------------------------------------------------------------------
# Package Config
# ------------------------------------------------------------------------------

include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/SVOConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/SVOConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SVO
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/SVOConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/SVOConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/SVOConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SVO
)
