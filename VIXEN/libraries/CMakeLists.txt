# ============================================================================
# VIXEN Static Libraries
# ============================================================================
# Core library components - all business logic as reusable static libraries
# Each library follows modern CMake target-based dependency management

cmake_minimum_required(VERSION 3.15...4.2 FATAL_ERROR)

# ============================================================================
# Library Build Order (respecting dependencies)
# ============================================================================
# Order matters: dependencies must be built before libraries that use them

# Logger library (used by everything)
add_subdirectory(logger)

# EventBus library (message passing, worker thread integration)
add_subdirectory(EventBus)

# ResourceManagement library (RM<T> wrapper with state tracking)
add_subdirectory(ResourceManagement)

# ShaderManagement library (device-agnostic shader compilation and hot reload)
add_subdirectory(ShaderManagement)

# VulkanResources library (device, swapchain, descriptors, pipeline, shader, texture handling, time, error, utilities)
add_subdirectory(VulkanResources)

# CashSystem library (caching and resource management)
add_subdirectory(CashSystem)

# RenderGraph static library (render graph system for managing Vulkan resources and execution)
add_subdirectory(RenderGraph)

message(STATUS "Libraries configured - all static libraries defined")
