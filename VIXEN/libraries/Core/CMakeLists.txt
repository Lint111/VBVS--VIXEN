# Core - Fundamental utilities shared across all VIXEN libraries
# Pure utility library with no dependencies except GLM

cmake_minimum_required(VERSION 3.20)

project(Core VERSION 1.0.0 LANGUAGES CXX)

# Library sources
add_library(Core STATIC
    src/MortonEncoding.cpp
    src/RuntimeCachePaths.cpp
)

# The absolute runtime-cache root differs per build tree. Scope it to the one source that reads
# it (and keep that source out of the PCH) so no other compile command embeds a tree path.
if(DEFINED VIXEN_RUNTIME_CACHE_DIR)
    set_source_files_properties(src/RuntimeCachePaths.cpp PROPERTIES
        COMPILE_DEFINITIONS "VIXEN_RUNTIME_CACHE_DIR=\"${VIXEN_RUNTIME_CACHE_DIR}\""
        SKIP_PRECOMPILE_HEADERS ON)
endif()

# Include directories
target_include_directories(Core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Link only GLM (minimal dependencies)
target_link_libraries(Core
    PUBLIC
        glm::glm
)

# C++23 standard
target_compile_features(Core PUBLIC cxx_std_23)

# Compiler warnings
if(MSVC)
    target_compile_options(Core PRIVATE /W4 /FS)
else()
    target_compile_options(Core PRIVATE -Wall -Wextra -pedantic)
endif()

# Export target
add_library(Core::Core ALIAS Core)

# Precompiled headers
if(USE_PRECOMPILED_HEADERS)
    target_precompile_headers(Core PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/CoreHeaders.h"
    )
endif()

# Tests
if(BUILD_TESTING)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        add_subdirectory(tests)
    endif()
endif()

# Install/export is handled centrally for all VIXEN libraries — see cmake/VixenInstall.cmake (AR#2).
