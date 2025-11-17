# ============================================================================
# VIXEN Main Executable
# ============================================================================
# Primary application entry point

cmake_minimum_required(VERSION 3.15...4.2 FATAL_ERROR)

# Main application files
file(GLOB MAIN_CPP_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/VulkanApplicationBase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/VulkanGraphApplication.cpp
)

# All headers (for IDE project generation)
file(GLOB_RECURSE HPP_FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/*.*)

# Define include path
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Build main executable
add_executable(VIXEN
    ${MAIN_CPP_FILES}
    ${HPP_FILES}
)

# Add include directories for generated SDI headers
# Primary: Runtime-generated SDI files (generated during shader compilation)
# Fallback: Build-time generated SDI files (precompiled assets)
target_include_directories(VIXEN PRIVATE
    ${CMAKE_SOURCE_DIR}/binaries/generated/sdi
    ${CMAKE_SOURCE_DIR}/BuiltAssets/GeneratedIncludes
)

# Link libraries to main executable
target_link_libraries(VIXEN
    RenderGraph
    CashSystem
    VulkanResources
    Logger
    ${VULKAN_LIB_LIST}
    glm::glm
)

# Define project properties
set_property(TARGET VIXEN PROPERTY RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/binaries)
set_property(TARGET VIXEN PROPERTY RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/binaries)
set_property(TARGET VIXEN PROPERTY RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/binaries)
set_property(TARGET VIXEN PROPERTY RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_SOURCE_DIR}/binaries)
set_property(TARGET VIXEN PROPERTY RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_SOURCE_DIR}/binaries)

# Define C++ version to be used for building the project
set_property(TARGET VIXEN PROPERTY CXX_STANDARD 23)
set_property(TARGET VIXEN PROPERTY CXX_STANDARD_REQUIRED ON)

# Define C version to be used for building the project
set_property(TARGET VIXEN PROPERTY C_STANDARD 23)
set_property(TARGET VIXEN PROPERTY C_STANDARD_REQUIRED ON)

# Add /FS flag for MSVC to allow multiple CL.EXE to write to the same PDB file
if(MSVC)
    target_compile_options(VIXEN PRIVATE /FS)
endif()

# Precompiled headers for faster compilation
if(USE_PRECOMPILED_HEADERS)
    target_precompile_headers(VIXEN PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include/Headers.h"
    )
    message(STATUS "VIXEN: Precompiled headers enabled (using Headers.h)")
endif()

# Define directories and the contained folder and files inside (for IDE)
if(WIN32)
    source_group("include" REGULAR_EXPRESSION "include/*")
    source_group("source" REGULAR_EXPRESSION "source/*")
endif()
