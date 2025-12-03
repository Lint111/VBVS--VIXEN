# Core - Fundamental utilities shared across all VIXEN libraries
# Pure utility library with no dependencies except GLM

cmake_minimum_required(VERSION 3.20)

project(Core VERSION 1.0.0 LANGUAGES CXX)

# Library sources
add_library(Core STATIC
    src/MortonEncoding.cpp
)

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

# Tests
if(BUILD_TESTING)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        add_subdirectory(tests)
    endif()
endif()

# Installation rules
install(TARGETS Core
    EXPORT CoreTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)
