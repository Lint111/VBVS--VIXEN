cmake_minimum_required(VERSION 3.20...4.2)

project(ResourceManagement VERSION 1.0.0 LANGUAGES CXX)

# ============================================================================
# ResourceManagement Static Library
# ============================================================================

set(RM_HEADERS
    include/ResourceManagement/ResourceState.h
    include/ResourceManagement/RM.h
)

# Header-only library (template-based)
add_library(ResourceManagement INTERFACE)

target_include_directories(ResourceManagement
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(ResourceManagement INTERFACE cxx_std_23)

# ============================================================================
# IDE Support - Show headers in project
# ============================================================================

# Create a custom target to make headers visible in IDE
add_custom_target(ResourceManagement_IDE SOURCES ${RM_HEADERS})

# ============================================================================
# Installation (optional)
# ============================================================================

install(TARGETS ResourceManagement
    EXPORT ResourceManagementTargets
)

install(DIRECTORY include/ResourceManagement
    DESTINATION include
)

# ============================================================================
# Tests
# ============================================================================

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
