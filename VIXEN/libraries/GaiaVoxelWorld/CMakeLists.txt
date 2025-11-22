# GaiaVoxelWorld - Central ECS-based voxel data management
# Provides single source of truth for all voxel data across AttributeRegistry,
# VoxelInjectionQueue, and SVO trees.

cmake_minimum_required(VERSION 3.20)

project(GaiaVoxelWorld VERSION 1.0.0 LANGUAGES CXX)

# Gaia ECS is fetched globally in dependencies/CMakeLists.txt

# Library sources
add_library(GaiaVoxelWorld STATIC
    src/GaiaVoxelWorld.cpp
    src/ECSBackedRegistry.cpp
    src/VoxelInjectionQueue.cpp
    src/VoxelInjector.cpp
    src/EntityBrickView.cpp
)

# MSVC PDB fix - disable parallel compilation for this target only
if(MSVC)
    set_target_properties(GaiaVoxelWorld PROPERTIES
        COMPILE_FLAGS "/FS"
        VS_NO_COMPILE_BATCHING ON
    )
endif()

target_include_directories(GaiaVoxelWorld
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link dependencies
target_link_libraries(GaiaVoxelWorld
    PUBLIC
        VoxelComponents  # Component definitions (includes Gaia + GLM)
        VoxelData        # Attribute storage and brick management
)

# C++23 standard
target_compile_features(GaiaVoxelWorld PUBLIC cxx_std_23)

# Compiler warnings and MSVC parallel build fix
if(MSVC)
    target_compile_options(GaiaVoxelWorld PRIVATE /W4 /FS)
else()
    target_compile_options(GaiaVoxelWorld PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Export target
add_library(GaiaVoxelWorld::GaiaVoxelWorld ALIAS GaiaVoxelWorld)

# Tests subdirectory
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
