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
    src/GPUTimestampQuery.cpp
    src/CapabilityGraph.cpp
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
    include/GPUTimestampQuery.h
    include/CapabilityGraph.h
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
        # Headers.h and other shared headers now come from Core::Core (linked below), not the
        # application layer (AR#3/#4 build-layering severance).
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ============================================================================
# Link Dependencies
# ============================================================================

target_link_libraries(VulkanResources
    PUBLIC
        Core::Core  # Common headers (Headers.h / VixenHash.h / MeshData.h) — AR#3/#4
        Logger
        ${VULKAN_LIB_LIST}
        glm::glm
        gli  # Needed for GLITextureLoader
        glfw  # Cross-platform Vulkan surface (glfwCreateWindowSurface) + required-extension query
    PRIVATE
        stb  # Needed for STBTextureLoader (header-only)
        ResourceManagement  # Sprint 5 Phase 2.5.3: Upload infrastructure
)

# ============================================================================
# Install and Properties
# ============================================================================

# Set output directory
set_target_properties(VulkanResources PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    FOLDER "Libraries"
)

# Platform surface handling is now cross-platform via GLFW (glfwCreateWindowSurface); no
# VK_USE_PLATFORM_WIN32_KHR define needed.

# Exclude STBTextureLoader from PCH (needs STB headers not available during PCH generation)
set_source_files_properties(
    src/TextureHandling/Loading/STBTextureLoader.cpp
    PROPERTIES
        SKIP_PRECOMPILE_HEADERS ON
)

# Precompiled headers for faster compilation
if(USE_PRECOMPILED_HEADERS)
    target_precompile_headers(VulkanResources PRIVATE
        "${VIXEN_ROOT}/libraries/Core/include/Headers.h"
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
