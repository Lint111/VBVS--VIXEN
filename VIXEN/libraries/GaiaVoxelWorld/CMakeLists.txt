# GaiaVoxelWorld - Central ECS-based voxel data management
# Provides single source of truth for all voxel data across AttributeRegistry,
# VoxelInjectionQueue, and SVO trees.

cmake_minimum_required(VERSION 3.20)

project(GaiaVoxelWorld VERSION 1.0.0 LANGUAGES CXX)

# Gaia ECS is fetched globally in dependencies/CMakeLists.txt

# Library sources
add_library(GaiaVoxelWorld STATIC
    src/GaiaVoxelWorld.cpp
    src/VoxelComponents.cpp
    src/ECSBackedRegistry.cpp
    src/VoxelInjectionQueue.cpp
    src/VoxelInjector.cpp
    src/EntityBrickView.cpp
)

target_include_directories(GaiaVoxelWorld
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link Gaia ECS and dependencies
target_link_libraries(GaiaVoxelWorld
    PUBLIC
        gaia
        glm::glm
        VoxelData  # For AttributeRegistry integration
)

# C++23 standard
target_compile_features(GaiaVoxelWorld PUBLIC cxx_std_23)

# Compiler warnings
if(MSVC)
    target_compile_options(GaiaVoxelWorld PRIVATE /W4)
else()
    target_compile_options(GaiaVoxelWorld PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Export target
add_library(GaiaVoxelWorld::GaiaVoxelWorld ALIAS GaiaVoxelWorld)
