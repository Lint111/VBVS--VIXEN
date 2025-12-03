cmake_minimum_required(VERSION 3.20)
project(VoxelComponents VERSION 1.0.0 LANGUAGES CXX)

# Pure component definitions - no heavy dependencies
# Only Gaia ECS and GLM required

# Library sources
add_library(VoxelComponents STATIC
    src/VoxelComponents.cpp
)

# Include directories
target_include_directories(VoxelComponents
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Link Gaia ECS, GLM, and Core (unified Morton encoding)
target_link_libraries(VoxelComponents
    PUBLIC
        gaia
        glm::glm
        Core  # Week 4 Phase A.1: Unified Morton Architecture
)

# C++23 standard
target_compile_features(VoxelComponents PUBLIC cxx_std_23)

# Compiler warnings
if(MSVC)
    target_compile_options(VoxelComponents PRIVATE /W4)
else()
    target_compile_options(VoxelComponents PRIVATE -Wall -Wextra -pedantic)
endif()

# ============================================================================
# Precompiled Headers
# ============================================================================

if(USE_PRECOMPILED_HEADERS)
    target_precompile_headers(VoxelComponents PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include/pch.h"
    )
    message(STATUS "VoxelComponents: Precompiled headers enabled")
endif()

# Export target
add_library(VoxelComponents::VoxelComponents ALIAS VoxelComponents)

# Tests
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# Installation rules
install(TARGETS VoxelComponents
    EXPORT VoxelComponentsTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)
