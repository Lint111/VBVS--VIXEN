cmake_minimum_required(VERSION 3.20)

project(Logger VERSION 1.0.0 LANGUAGES CXX)

# ============================================================================
# Logger Static Library
# ============================================================================

# Collect all source and header files in logger directory
file(GLOB LOGGER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

file(GLOB LOGGER_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)

# Create static library
add_library(Logger STATIC
    ${LOGGER_SOURCES}
    ${LOGGER_HEADERS}
)

# Public include directory - consumers can #include "Logger.h" directly
target_include_directories(Logger
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

# C++23 standard
target_compile_features(Logger PUBLIC cxx_std_23)

# ============================================================================
# Precompiled Headers (Optional)
# ============================================================================

if(USE_PRECOMPILED_HEADERS)
    target_precompile_headers(Logger PRIVATE
        "${CMAKE_SOURCE_DIR}/include/Headers.h"
    )
    message(STATUS "Logger: Precompiled headers enabled")
endif()

# ============================================================================
# IDE Support - Group files
# ============================================================================

source_group("Header Files" FILES ${LOGGER_HEADERS})
source_group("Source Files" FILES ${LOGGER_SOURCES})

# ============================================================================
# Installation (optional)
# ============================================================================

install(TARGETS Logger
    EXPORT LoggerTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES ${LOGGER_HEADERS}
    DESTINATION include/Logger
)

# ============================================================================
# Tests
# ============================================================================

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
