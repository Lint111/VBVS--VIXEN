# GaiaArchetypes - Archetype Builder with Relationship Hooks
# Extends Gaia ECS with archetype definitions, relationship hooks, and batch processing

cmake_minimum_required(VERSION 3.20)

project(GaiaArchetypes VERSION 1.0.0 LANGUAGES CXX)

# Gaia ECS is fetched globally in dependencies/CMakeLists.txt

# Library sources
add_library(GaiaArchetypes STATIC
    src/ArchetypeBuilder.cpp
    src/RelationshipObserver.cpp
    src/VoxelVolumeArchetype.cpp
)

# MSVC PDB fix - disable parallel compilation for this target only
if(MSVC)
    set_target_properties(GaiaArchetypes PROPERTIES
        COMPILE_FLAGS "/FS"
        VS_NO_COMPILE_BATCHING ON
    )
endif()

target_include_directories(GaiaArchetypes
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link dependencies
target_link_libraries(GaiaArchetypes
    PUBLIC
        gaia       # Gaia ECS library
        glm        # Math library
)

# C++23 standard (same as rest of project)
target_compile_features(GaiaArchetypes PUBLIC cxx_std_23)

# Compiler warnings
if(MSVC)
    target_compile_options(GaiaArchetypes PRIVATE /W4 /FS)
else()
    target_compile_options(GaiaArchetypes PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Export target
add_library(GaiaArchetypes::GaiaArchetypes ALIAS GaiaArchetypes)

# Tests subdirectory
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
