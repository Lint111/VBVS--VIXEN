# VulkanResources static library
# Low-level Vulkan abstraction layer: device, swapchain, shaders, textures, utilities

cmake_minimum_required(VERSION 3.15...4.2)

project(VulkanResources)

# ============================================================================
# Source Files
# ============================================================================

set(VULKANRESOURCES_SOURCES
    src/VulkanDevice.cpp
    src/VulkanInstance.cpp
    src/VulkanLayerAndExtension.cpp
    src/VulkanSwapChain.cpp
    src/CommandBufferUtility.cpp
    src/error/VulkanError.cpp
    src/Time/EngineTime.cpp
    src/TextureHandling/Loading/GLITextureLoader.cpp
    src/TextureHandling/Loading/STBTextureLoader.cpp
    src/TextureHandling/Loading/TextureLoader.cpp
)

set(VULKANRESOURCES_HEADERS
    include/VulkanDevice.h
    include/VulkanInstance.h
    include/VulkanLayerAndExtension.h
    include/VulkanSwapChain.h
    include/CommandBufferUtility.h
    include/VulkanGlobalNames.h
    include/TextureHandling/Loading/TextureLoader.h
    include/TextureHandling/Loading/GLITextureLoader.h
    include/TextureHandling/Loading/STBTextureLoader.h
)

# ============================================================================
# Create Static Library
# ============================================================================

add_library(VulkanResources STATIC
    ${VULKANRESOURCES_SOURCES}
    ${VULKANRESOURCES_HEADERS}
)

# ============================================================================
# Compiler Options
# ============================================================================

# MSVC: Enable /FS for parallel compilation PDB access
if(MSVC)
    target_compile_options(VulkanResources PRIVATE /FS)
endif()

# C++23 standard
target_compile_features(VulkanResources PUBLIC cxx_std_23)

# ============================================================================
# Include Directories
# ============================================================================

target_include_directories(VulkanResources
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        # Main application includes - for Headers.h and other shared headers
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/application/main/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        # STB image loader headers
        $<BUILD_INTERFACE:${stb_SOURCE_DIR}>
)

# ============================================================================
# Link Dependencies
# ============================================================================

target_link_libraries(VulkanResources
    PUBLIC
        Logger
        ${VULKAN_LIB_LIST}
        glm::glm
        gli  # Needed for GLITextureLoader
)

# ============================================================================
# Install and Properties
# ============================================================================

# Set output directory
set_target_properties(VulkanResources PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    FOLDER "Libraries"
)

# Platform definitions
if(WIN32)
    target_compile_definitions(VulkanResources PRIVATE VK_USE_PLATFORM_WIN32_KHR)
endif()

# Precompiled headers for faster compilation
if(USE_PRECOMPILED_HEADERS)
    target_precompile_headers(VulkanResources PRIVATE
        "${CMAKE_SOURCE_DIR}/application/main/include/Headers.h"
    )
    message(STATUS "VulkanResources: Precompiled headers enabled (using Headers.h)")
endif()

# ============================================================================
# Source Groups for IDE Organization
# ============================================================================

source_group("Headers" FILES ${VULKANRESOURCES_HEADERS})
source_group("Sources" FILES ${VULKANRESOURCES_SOURCES})

# ============================================================================
# Tests
# ============================================================================

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
