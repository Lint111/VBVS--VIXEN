cmake_minimum_required(VERSION 3.20)

project(VulkanResources VERSION 1.0.0 LANGUAGES CXX)

# ============================================================================
# VulkanResources Static Library
# ============================================================================
# Core Vulkan resource handling: device, instance, swap chain, descriptors,
# pipeline, shader, texture handling, time utilities, error handling

# Collect VulkanResources source files from multiple locations
file(GLOB_RECURSE VULKAN_RESOURCES_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../TextureHandling/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../Time/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../error/*.cpp
)

# Add standalone Vulkan resource files from source root
list(APPEND VULKAN_RESOURCES_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../VulkanDescriptor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../VulkanPipeline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../VulkanShader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../VulkanSwapChain.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../wrapper.cpp
)

# Create static library
add_library(VulkanResources STATIC
    ${VULKAN_RESOURCES_SOURCES}
)

# ============================================================================
# Include Directories
# ============================================================================

target_include_directories(VulkanResources
    PUBLIC
        # Main project include directory for VulkanResources headers
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# ============================================================================
# Link Dependencies
# ============================================================================

target_link_libraries(VulkanResources
    PUBLIC
        Logger
        glm::glm
)

# Link Vulkan (conditional on build mode)
if(NOT VULKAN_TRIMMED_BUILD_ACTIVE)
    if(DEFINED VULKAN_LIB_LIST)
        target_link_libraries(VulkanResources PUBLIC ${VULKAN_LIB_LIST})
    endif()
else()
    # Trimmed build: headers only
    if(TARGET Vulkan::Headers)
        target_link_libraries(VulkanResources PUBLIC Vulkan::Headers)
    endif()
    if(DEFINED VULKAN_HEADERS_INCLUDE_DIR)
        target_include_directories(VulkanResources PUBLIC ${VULKAN_HEADERS_INCLUDE_DIR})
    endif()
endif()

# ============================================================================
# Compiler Options
# ============================================================================

# C++23 standard
target_compile_features(VulkanResources PUBLIC cxx_std_23)

# Platform definitions
if(WIN32)
    target_compile_definitions(VulkanResources PRIVATE VK_USE_PLATFORM_WIN32_KHR)
endif()

# MSVC: Enable /FS for parallel compilation PDB access
if(MSVC)
    target_compile_options(VulkanResources PRIVATE /FS)
endif()

# ============================================================================
# Precompiled Headers (Optional)
# ============================================================================

if(USE_PRECOMPILED_HEADERS)
    target_precompile_headers(VulkanResources PRIVATE
        "${CMAKE_SOURCE_DIR}/include/Headers.h"
    )
    message(STATUS "VulkanResources: Precompiled headers enabled")
endif()

# ============================================================================
# Tests
# ============================================================================

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
