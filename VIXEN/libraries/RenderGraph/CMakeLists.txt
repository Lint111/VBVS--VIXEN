# RenderGraph static library
# Render graph system for managing Vulkan resources and execution

cmake_minimum_required(VERSION 3.15...4.2)

project(RenderGraph)

# Fetch magic_enum for enum-to-string conversions
include(FetchContent)
FetchContent_Declare(
    magic_enum
    GIT_REPOSITORY https://github.com/Neargye/magic_enum
    GIT_TAG v0.9.6
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(magic_enum)

# Organize magic_enum in Dependencies folder
if(TARGET magic_enum)
    set_target_properties(magic_enum PROPERTIES FOLDER "Dependencies")
endif()

# Fetch VulkanMemoryAllocator (VMA) for efficient GPU memory management
FetchContent_Declare(
    VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
    GIT_TAG v3.1.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(VulkanMemoryAllocator)

# Organize VMA in Dependencies folder
if(TARGET VulkanMemoryAllocator)
    set_target_properties(VulkanMemoryAllocator PROPERTIES FOLDER "Dependencies")
endif()

# ============================================================================
# Data Headers (Build-Independent)
# ============================================================================

set(RENDERGRAPH_DATA_CORE_HEADERS
    include/Data/BasicDataTypes.h
    include/Data/DeviceHandles.h
    include/Data/LayerAndExtensionRequirementData.h
    include/Data/ParameterDataTypes.h
    include/Data/VariantDescriptors.h
    include/Data/SceneGenerator.h
    include/Data/VoxelOctree.h
    include/Data/Core/GraphMessages.h
    include/Data/Core/ResourceConfig.h
    include/Data/Core/ResourceTypeTraits.h
    include/Data/Core/ResourceTypes.h
    include/Data/Core/CompileTimeResourceSystem.h
    include/Data/Core/BoolVector.h
)

set(RENDERGRAPH_DATA_NODE_CONFIGS
    include/Data/Nodes/BoolOpNodeConfig.h
    include/Data/Nodes/CameraNodeConfig.h
    include/Data/Nodes/CommandPoolNodeConfig.h
    include/Data/Nodes/ComputeDispatchNodeConfig.h
    include/Data/Nodes/ComputePipelineNodeConfig.h
    include/Data/Nodes/ConstantNodeConfig.h
    include/Data/Nodes/DepthBufferNodeConfig.h
    include/Data/Nodes/DescriptorResourceGathererNodeConfig.h
    include/Data/Nodes/DescriptorSetNodeConfig.h
    include/Data/Nodes/DeviceNodeConfig.h
    include/Data/Nodes/FrameSyncNodeConfig.h
    include/Data/Nodes/FramebufferNodeConfig.h
    include/Data/Nodes/GeometryRenderNodeConfig.h
    include/Data/Nodes/GraphicsPipelineNodeConfig.h
    include/Data/Nodes/InstanceNodeConfig.h
    include/Data/Nodes/LoopBridgeNodeConfig.h
    include/Data/Nodes/PresentNodeConfig.h
    include/Data/Nodes/PushConstantGathererNodeConfig.h
    include/Data/Nodes/RenderPassNodeConfig.h
    include/Data/Nodes/ShaderLibraryNodeConfig.h
    include/Data/Nodes/StructSpreaderNodeConfig.h
    include/Data/Nodes/SwapChainNodeConfig.h
    include/Data/Nodes/SwapChainStructSpreaderNodeConfig.h
    include/Data/Nodes/TextureLoaderNodeConfig.h
    include/Data/Nodes/VertexBufferNodeConfig.h
    include/Data/Nodes/VoxelGridNodeConfig.h
    include/Data/Nodes/VoxelAABBConverterNodeConfig.h
    include/Data/Nodes/AccelerationStructureNodeConfig.h
    include/Data/Nodes/RayTracingPipelineNodeConfig.h
    include/Data/Nodes/TraceRaysNodeConfig.h
    include/Data/Nodes/WindowNodeConfig.h
)

# ============================================================================
# Core Implementation Headers
# ============================================================================

set(RENDERGRAPH_CORE_HEADERS
    include/Core/ComputePerformanceLogger.h
    include/Core/DeferredDestruction.h
    include/Core/DeviceBudgetManager.h
    include/Core/DirectAllocator.h
    include/Core/FieldExtractor.h
    include/Core/GPUPerformanceLogger.h
    include/Core/GraphLifecycleHooks.h
    include/Core/GraphTopology.h
    include/Core/HostBudgetManager.h
    include/Core/IGraphCompilable.h
    include/Core/IMemoryAllocator.h
    include/Core/INodeWiring.h
    include/Core/InstanceGroup.h
    include/Core/LoopManager.h
    include/Core/NodeInstance.h
    include/Core/NodeLogging.h
    include/Core/NodeType.h
    include/Core/NodeTypeRegistry.h
    include/Core/PerFrameResources.h
    include/Core/RenderGraph.h
    include/Core/ResourceBudgetManager.h
    include/Core/ResourceDependencyTracker.h
    include/Core/SlotTask.h
    include/Core/StatefulContainer.h
    include/Core/Timer.h
    include/Core/TypedConnection.h
    include/Core/TypedNodeInstance.h
    include/Core/UnknownTypeRegistry.h
    include/Core/VariadicTypedNode.h
    include/Core/VMAAllocator.h
)

set(RENDERGRAPH_DATA_CORE_SOURCES
    src/Data/SceneGenerator.cpp
    src/Data/VoxelOctree.cpp
)

set(RENDERGRAPH_CORE_SOURCES
    src/Core/DeviceBudgetManager.cpp
    src/Core/DirectAllocator.cpp
    src/Core/GPUPerformanceLogger.cpp
    src/Core/GraphLifecycleHooks.cpp
    src/Core/GraphTopology.cpp
    src/Core/HostBudgetManager.cpp
    src/Core/LoopManager.cpp
    src/Core/NodeInstance.cpp
    src/Core/NodeType.cpp
    src/Core/NodeTypeRegistry.cpp
    src/Core/PerFrameResources.cpp
    src/Core/RenderGraph.cpp
    src/Core/ResourceBudgetManager.cpp
    src/Core/ResourceDependencyTracker.cpp
    src/Core/SlotTask.cpp
    src/Core/Timer.cpp
    src/Core/VMAAllocator.cpp
)

# ============================================================================
# Node Implementation Headers and Sources
# ============================================================================

set(RENDERGRAPH_NODE_HEADERS
    include/Nodes/BoolOpNode.h
    include/Nodes/CameraNode.h
    include/Nodes/CommandPoolNode.h
    include/Nodes/ComputeDispatchNode.h
    include/Nodes/ComputePipelineNode.h
    include/Nodes/ConstantNode.h
    include/Nodes/ConstantNodeType.h
    include/Nodes/DebugBufferReaderNode.h
    include/Nodes/DepthBufferNode.h
    include/Nodes/DescriptorResourceGathererNode.h
    include/Nodes/DescriptorSetNode.h
    include/Nodes/DeviceNode.h
    include/Nodes/FrameSyncNode.h
    include/Nodes/FramebufferNode.h
    include/Nodes/GeometryRenderNode.h
    include/Nodes/GraphicsPipelineNode.h
    include/Nodes/InputNode.h
    include/Nodes/InstanceNode.h
    include/Nodes/LoopBridgeNode.h
    include/Nodes/PresentNode.h
    include/Nodes/PushConstantGathererNode.h
    include/Nodes/RenderPassNode.h
    include/Nodes/ResourceGathererNode.h
    include/Nodes/ShaderLibraryNode.h
    include/Nodes/StructSpreaderNode.h
    include/Nodes/SwapChainNode.h
    include/Nodes/SwapChainStructSpreaderNode.h
    include/Nodes/TextureLoaderNode.h
    include/Nodes/VertexBufferNode.h
    include/Nodes/VoxelGridNode.h
    include/Nodes/VoxelAABBConverterNode.h
    include/Nodes/AccelerationStructureNode.h
    include/Nodes/RayTracingPipelineNode.h
    include/Nodes/TraceRaysNode.h
    include/Nodes/WindowNode.h
)

set(RENDERGRAPH_NODE_SOURCES
    src/Nodes/BoolOpNode.cpp
    src/Nodes/CameraNode.cpp
    src/Nodes/CommandPoolNode.cpp
    src/Nodes/ComputeDispatchNode.cpp
    src/Nodes/ComputePipelineNode.cpp
    src/Nodes/DebugBufferReaderNode.cpp
    src/Nodes/DepthBufferNode.cpp
    src/Nodes/DescriptorResourceGathererNode.cpp
    src/Nodes/DescriptorSetNode.cpp
    src/Nodes/DeviceNode.cpp
    src/Nodes/FrameSyncNode.cpp
    src/Nodes/FramebufferNode.cpp
    src/Nodes/GeometryRenderNode.cpp
    src/Nodes/GraphicsPipelineNode.cpp
    src/Nodes/InputNode.cpp
    src/Nodes/InstanceNode.cpp
    src/Nodes/LoopBridgeNode.cpp
    src/Nodes/PresentNode.cpp
    src/Nodes/PushConstantGathererNode.cpp
    src/Nodes/RenderPassNode.cpp
    src/Nodes/ShaderLibraryNode.cpp
    src/Nodes/StructSpreaderNode.cpp
    src/Nodes/SwapChainNode.cpp
    src/Nodes/TextureLoaderNode.cpp
    src/Nodes/VertexBufferNode.cpp
    src/Nodes/VoxelGridNode.cpp
    src/Nodes/VoxelAABBConverterNode.cpp
    src/Nodes/AccelerationStructureNode.cpp
    src/Nodes/RayTracingPipelineNode.cpp
    src/Nodes/TraceRaysNode.cpp
    src/Nodes/WindowNode.cpp
)

# ============================================================================
# Debug Infrastructure
# ============================================================================

set(RENDERGRAPH_DEBUG_HEADERS
    include/Debug/DebugCaptureResource.h
    include/Debug/DebugRaySample.h
    include/Debug/IDebugBuffer.h
    include/Debug/IDebugCapture.h
    include/Debug/IExportable.h
    include/Debug/RayTraceBuffer.h
    include/Debug/ShaderCountersBuffer.h
)

set(RENDERGRAPH_DEBUG_SOURCES
    src/Debug/RayTraceBuffer.cpp
    src/Debug/ShaderCountersBuffer.cpp
)

# ============================================================================
# Event Types
# ============================================================================

set(RENDERGRAPH_EVENT_HEADERS
    include/EventTypes/RenderGraphEvents.h
)

# ============================================================================
# Create Static Library
# ============================================================================

add_library(RenderGraph STATIC
    ${RENDERGRAPH_DATA_CORE_HEADERS}
    ${RENDERGRAPH_DATA_CORE_SOURCES}
    ${RENDERGRAPH_DATA_NODE_CONFIGS}
    ${RENDERGRAPH_CORE_HEADERS}
    ${RENDERGRAPH_CORE_SOURCES}
    ${RENDERGRAPH_NODE_HEADERS}
    ${RENDERGRAPH_NODE_SOURCES}
    ${RENDERGRAPH_DEBUG_HEADERS}
    ${RENDERGRAPH_DEBUG_SOURCES}
    ${RENDERGRAPH_EVENT_HEADERS}
)

# ============================================================================
# Compiler Options
# ============================================================================

# MSVC: Enable /FS for parallel compilation PDB access and /bigobj for large object files
if(MSVC)
    target_compile_options(RenderGraph PRIVATE /FS /bigobj)
endif()

# C++23 standard
target_compile_features(RenderGraph PUBLIC cxx_std_23)

# ============================================================================
# Include Directories
# ============================================================================

target_include_directories(RenderGraph
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        # Main application includes - use SOURCE_DIR for project root
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/application/main/include>
        # Generated SDI headers - priority order:
        #   1. Dev-time: Tool-generated SDI files in source tree
        #   2. Build-time: CMake-generated SDI files (main app)
        #   3. Build-time: CMake-generated SDI files (benchmark app)
        #   4. Binaries: Pre-built SDI files in binaries/generated/sdi
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/generated/sdi>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/application/main/generated/sdi>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/application/benchmark/generated/sdi>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/binaries/generated/sdi>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Note: CashSystem and ShaderManagement includes come from target_link_libraries below

# ============================================================================
# Link Dependencies
# ============================================================================

target_link_libraries(RenderGraph
    PUBLIC
        VulkanResources
        Logger
        EventBus
        ResourceManagement
        CashSystem
        ShaderManagement
        SVO                    # New SVO library for Laine-Karras ESVO octrees
        magic_enum::magic_enum
        VulkanMemoryAllocator  # GPU memory management (VMA)
)

# ============================================================================
# Install and Properties
# ============================================================================

# Set output directory
set_target_properties(RenderGraph PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    FOLDER "Libraries"
)

# Platform definitions
if(WIN32)
    target_compile_definitions(RenderGraph PRIVATE VK_USE_PLATFORM_WIN32_KHR)
endif()

# Precompiled headers for faster compilation
if(USE_PRECOMPILED_HEADERS)
    # Use library-specific header (no GLI or other app-level dependencies)
    target_precompile_headers(RenderGraph PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/RenderGraphHeaders.h"
    )
    message(STATUS "RenderGraph: Precompiled headers enabled (using RenderGraphHeaders.h)")
endif()

# ============================================================================
# Source Groups for IDE Organization
# ============================================================================

source_group("Data\\Core\\Headers" FILES ${RENDERGRAPH_DATA_CORE_HEADERS})
source_group("Data\\Core\\Sources" FILES ${RENDERGRAPH_DATA_CORE_SOURCES})
source_group("Data\\Nodes" FILES ${RENDERGRAPH_DATA_NODE_CONFIGS})
source_group("Core\\Headers" FILES ${RENDERGRAPH_CORE_HEADERS})
source_group("Core\\Sources" FILES ${RENDERGRAPH_CORE_SOURCES})
source_group("Nodes\\Headers" FILES ${RENDERGRAPH_NODE_HEADERS})
source_group("Nodes\\Sources" FILES ${RENDERGRAPH_NODE_SOURCES})
source_group("Debug\\Headers" FILES ${RENDERGRAPH_DEBUG_HEADERS})
source_group("Debug\\Sources" FILES ${RENDERGRAPH_DEBUG_SOURCES})
source_group("EventTypes" FILES ${RENDERGRAPH_EVENT_HEADERS})

# ============================================================================
# Tests
# ============================================================================

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
