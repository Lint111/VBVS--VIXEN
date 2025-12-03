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

# Core library (fundamental utilities: Morton encoding, etc.)
add_subdirectory(Core)

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

# VoxelComponents library (pure component definitions - no heavy dependencies, just Gaia + GLM)
add_subdirectory(VoxelComponents)

# VoxelData library (attribute storage, brick management - depends on VoxelComponents)
add_subdirectory(VoxelData)

# GaiaVoxelWorld library (ECS world management - depends on VoxelComponents + VoxelData)
add_subdirectory(GaiaVoxelWorld)

# SVO library (Sparse Voxel Octree - Laine & Karras 2010 implementation)
add_subdirectory(SVO)

# GaiaArchetypes library (Archetype builder with relationship hooks - depends on Gaia ECS)
add_subdirectory(GaiaArchetypes)

# Profiler library (Performance profiling and benchmarking - depends on Logger, VulkanResources, nlohmann_json)
add_subdirectory(Profiler)

message(STATUS "Libraries configured - all static libraries defined")
