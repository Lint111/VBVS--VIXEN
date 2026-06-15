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

# VMA (Vulkan Memory Allocator) - now provided by dependencies/CMakeLists.txt
# Linked transitively through ResourceManagement

# ============================================================================
# Data Headers (Build-Independent)
# ============================================================================

set(RENDERGRAPH_DATA_CORE_HEADERS
    include/Data/BasicDataTypes.h
    include/Data/DeviceHandles.h
    include/Data/DispatchPass.h
    include/Data/LayerAndExtensionRequirementData.h
    include/Data/ParameterDataTypes.h
    include/Data/VariantDescriptors.h
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
    include/Data/Nodes/MultiDispatchNodeConfig.h
    include/Data/Nodes/ConstantNodeConfig.h
    include/Data/Nodes/DepthBufferNodeConfig.h
    include/Data/Nodes/RenderTargetNodeConfig.h
    include/Data/Nodes/PickIdTargetNodeConfig.h
    include/Data/Nodes/SelectionCoordinatorNodeConfig.h
    include/Data/Nodes/VoxelSelectionProviderNodeConfig.h
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
# Connection System (Sprint 6.0.1)
# ============================================================================

set(RENDERGRAPH_CONNECTION_HEADERS
    include/Connection/ConnectionTypes.h
    include/Connection/ConnectionRule.h
    include/Connection/ConnectionRuleRegistry.h
    include/Connection/ConnectionPipeline.h
    include/Connection/ConnectionModifier.h
    include/Connection/Rules/DirectConnectionRule.h
    include/Connection/Rules/AccumulationConnectionRule.h
    include/Connection/Rules/VariadicConnectionRule.h
    include/Connection/Modifiers/FieldExtractionModifier.h
    include/Connection/Modifiers/SlotRoleModifier.h
    include/Connection/Modifiers/DebugTagModifier.h
    include/Connection/Modifiers/AccumulationSortConfig.h
)

set(RENDERGRAPH_CONNECTION_SOURCES
    src/Connection/ConnectionRuleRegistry.cpp
    src/Connection/ConnectionPipeline.cpp
    src/Connection/Rules/DirectConnectionRule.cpp
    src/Connection/Rules/AccumulationConnectionRule.cpp
    src/Connection/Rules/VariadicConnectionRule.cpp
)

# ============================================================================
# Core Implementation Headers
# ============================================================================

set(RENDERGRAPH_CORE_HEADERS
    # Note: Memory/Lifetime headers moved to ResourceManagement library
    # Note: ConnectionRule moved to Connection/ folder (Sprint 6.0.1)
    include/Core/ComputePerformanceLogger.h
    include/Core/FieldExtractor.h
    include/Core/GPUPerformanceLogger.h
    include/Core/GPUQueryManager.h  # Sprint 6.3: Task #344 - Query pool coordinator
    include/Core/GraphLifecycleHooks.h
    include/Core/GraphTopology.h
    include/Core/IGraphCompilable.h
    include/Core/INodeWiring.h
    include/Core/InstanceGroup.h
    include/Core/LoopManager.h
    include/Core/NodeInstance.h
    include/Core/NodeLogging.h
    include/Core/NodeType.h
    include/Core/NodeTypeRegistry.h
    include/Core/PerFrameResources.h
    include/Core/EngineConfig.h          # AR#7: instantiable engine config
    include/Core/EngineContext.h         # AR#7: instantiable EngineContext (owns registry/bus/graph/calibration)
    include/Core/RenderGraph.h
    include/Core/ResourceAccessTracker.h  # Sprint 6.4: Task #349 - Conflict detection
    include/Core/ResourceDependencyTracker.h
    include/Core/WaveScheduler.h  # Sprint 6.4: Task #347 - Wave computation
    include/Core/SlotTask.h
    # Note: StatefulContainer.h moved to ResourceManagement library
    include/Core/TaskQueue.h  # Sprint 6.2: Task #339 - Budget-aware task queue
    include/Core/TimelineCapacityTracker.h  # Sprint 6.3: Task #344 - Timeline capacity tracker
    include/Core/Timer.h
    include/Core/VirtualTask.h  # Sprint 6.5: Task-level parallelism
    include/Core/VirtualResourceAccessTracker.h  # Sprint 6.5: Per-task resource tracking
    include/Core/TaskDependencyGraph.h  # Sprint 6.5: Task dependency resolution
    include/Core/TBBVirtualTaskExecutor.h  # Sprint 6.5: Virtual task executor
    include/Core/TypedConnection.h
    include/Core/TypedNodeInstance.h
    include/Core/UnknownTypeRegistry.h
    include/Core/VariadicTypedNode.h
)

set(RENDERGRAPH_DATA_CORE_SOURCES
    src/Data/VoxelOctree.cpp
)

set(RENDERGRAPH_CORE_SOURCES
    # Note: Memory/Lifetime sources moved to ResourceManagement library
    # Note: ConnectionRule.cpp split into Connection/ folder
    src/Core/GPUPerformanceLogger.cpp
    src/Core/GPUQueryManager.cpp  # Sprint 6.3: Task #344 - Query pool coordinator
    src/Core/GraphLifecycleHooks.cpp
    src/Core/GraphTopology.cpp
    src/Core/LoopManager.cpp
    src/Core/NodeInstance.cpp
    src/Core/NodeType.cpp
    src/Core/NodeTypeRegistry.cpp
    src/Core/PerFrameResources.cpp
    src/Core/EngineContext.cpp           # AR#7: instantiable EngineContext
    src/Core/RenderGraph.cpp
    src/Core/ResourceAccessTracker.cpp  # Sprint 6.4: Task #349 - Conflict detection
    src/Core/ResourceDependencyTracker.cpp
    src/Core/WaveScheduler.cpp  # Sprint 6.4: Task #347 - Wave computation
    src/Core/SlotTask.cpp
    src/Core/TimelineCapacityTracker.cpp  # Sprint 6.3: Task #344 - Timeline capacity tracker
    src/Core/TaskProfileRegistry.cpp  # Sprint 6.5: Profile factory initialization
    src/Core/TaskQueue.cpp  # Sprint 6.2: Task #339 - Template instantiations
    src/Core/Timer.cpp
    src/Core/VirtualResourceAccessTracker.cpp  # Sprint 6.5: Per-task resource tracking
    src/Core/TaskDependencyGraph.cpp  # Sprint 6.5: Task dependency resolution
    src/Core/TBBVirtualTaskExecutor.cpp  # Sprint 6.5: Virtual task executor
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
    include/Nodes/MultiDispatchNode.h
    include/Nodes/ConstantNode.h
    include/Nodes/ConstantNodeType.h
    include/Nodes/DebugBufferReaderNode.h
    include/Nodes/DepthBufferNode.h
    include/Nodes/RenderTargetNode.h
    include/Nodes/PickIdTargetNode.h
    include/Nodes/SelectionCoordinatorNode.h
    include/Nodes/VoxelSelectionProviderNode.h
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
    src/Nodes/MultiDispatchNode.cpp
    src/Nodes/DebugBufferReaderNode.cpp
    src/Nodes/DepthBufferNode.cpp
    src/Nodes/RenderTargetNode.cpp
    src/Nodes/PickIdTargetNode.cpp
    src/Nodes/InstanceBufferNode.cpp
    src/Nodes/DynamicInstanceBufferNode.cpp
    src/Nodes/MvpUniformNode.cpp
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
    src/Nodes/SelectionCoordinatorNode.cpp
    src/Nodes/VoxelSelectionProviderNode.cpp
    src/Nodes/UISelectionProviderNode.cpp
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
# UI (RmlUi backend — data-driven, moddable RML/RCSS rendered through Vulkan)
# ============================================================================

set(RENDERGRAPH_UI_HEADERS
    include/Ui/VixenRmlRenderInterface.h
    include/Ui/VixenRmlSystemInterface.h
    include/Ui/UiShaderSpirv.h
    include/Data/Nodes/UIRenderNodeConfig.h
    include/Nodes/UIRenderNode.h
)

set(RENDERGRAPH_UI_SOURCES
    src/Ui/VixenRmlRenderInterface.cpp
    src/Nodes/UIRenderNode.cpp
)

# ============================================================================
# Selection (SEL-P1) — engine-native selection core types
# ============================================================================
# Provider-owned, dependency-light picking/selection abstraction. SelectionId
# itself lives in EventBus (so SelectionChangedEvent can carry it without a
# RenderGraph dep); these headers re-export it and add the engine-side types.

set(RENDERGRAPH_SELECTION_HEADERS
    include/Selection/SelectionId.h
    include/Selection/SelectionCandidate.h
    include/Selection/SelectionResolve.h
    include/Selection/SelectionSet.h
)

set(RENDERGRAPH_SELECTION_SOURCES
    src/Selection/SelectionSet.cpp
)

# ============================================================================
# Create Static Library
# ============================================================================

add_library(RenderGraph STATIC
    ${RENDERGRAPH_DATA_CORE_HEADERS}
    ${RENDERGRAPH_DATA_CORE_SOURCES}
    ${RENDERGRAPH_DATA_NODE_CONFIGS}
    ${RENDERGRAPH_CONNECTION_HEADERS}
    ${RENDERGRAPH_CONNECTION_SOURCES}
    ${RENDERGRAPH_CORE_HEADERS}
    ${RENDERGRAPH_CORE_SOURCES}
    ${RENDERGRAPH_NODE_HEADERS}
    ${RENDERGRAPH_NODE_SOURCES}
    ${RENDERGRAPH_DEBUG_HEADERS}
    ${RENDERGRAPH_DEBUG_SOURCES}
    ${RENDERGRAPH_EVENT_HEADERS}
    ${RENDERGRAPH_UI_HEADERS}
    ${RENDERGRAPH_UI_SOURCES}
    ${RENDERGRAPH_SELECTION_HEADERS}
    ${RENDERGRAPH_SELECTION_SOURCES}
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
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        # Common headers (Headers.h / VixenHash.h / MeshData.h) now come from Core::Core (linked below),
        # not the application layer (AR#3/#4 build-layering severance).
        # Generated SDI headers - priority order:
        #   1. Dev-time: Tool-generated SDI files in source tree
        #   2. Build-time: CMake-generated SDI files (main app)
        #   3. Build-time: CMake-generated SDI files (benchmark app)
        #   4. Binaries: Pre-built SDI files in binaries/generated/sdi
        $<BUILD_INTERFACE:${VIXEN_ROOT}/generated/sdi>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/application/main/generated/sdi>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/application/benchmark/generated/sdi>
        $<BUILD_INTERFACE:${VIXEN_ROOT}/binaries/generated/sdi>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Note: CashSystem and ShaderManagement includes come from target_link_libraries below

# ============================================================================
# Link Dependencies
# ============================================================================

target_link_libraries(RenderGraph
    PUBLIC
        Core::Core  # Common headers (Headers.h / VixenHash.h / MeshData.h) — AR#3/#4
        VulkanResources
        Logger
        EventBus
        ResourceManagement     # Includes VMA transitively
        CashSystem
        ShaderManagement
        SVO                    # Laine-Karras ESVO octrees
        magic_enum::magic_enum
        TBB::tbb               # Sprint 6.4: Parallel execution
        glfw                   # Cross-platform window/surface/input (WindowNode/InputNode/InstanceNode)
        rmlui_core             # Data-driven UI (RML/RCSS); freetype is its transitive font engine
        freetype
)

# RmlUi is built static (FetchContent); code including its headers needs this define.
target_compile_definitions(RenderGraph PUBLIC RMLUI_STATIC_LIB)

# UIRenderNode loads RmlUi assets (fonts/docs) via paths relative to the exe (vixen_stage_assets
# stages them next to demo binaries). Consumers that run from a different CWD (e.g. the UNDERTOW host)
# wouldn't find them; this source-dir fallback lets the node resolve assets/ui/* from the engine tree
# regardless of CWD. Private — only UIRenderNode.cpp uses it.
target_compile_definitions(RenderGraph PRIVATE
    VIXEN_UI_ASSET_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

# ============================================================================
# Install and Properties
# ============================================================================

# Set output directory
set_target_properties(RenderGraph PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    FOLDER "Libraries"
)

# Platform surface handling is now cross-platform via GLFW (glfwCreateWindowSurface +
# glfwGetRequiredInstanceExtensions); no VK_USE_PLATFORM_WIN32_KHR define needed.

# Precompiled headers for faster compilation
if(USE_PRECOMPILED_HEADERS)
    # Use library-specific header (no GLI or other app-level dependencies)
    target_precompile_headers(RenderGraph PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/RenderGraphHeaders.h"
    )
    message(STATUS "RenderGraph: Precompiled headers enabled (using RenderGraphHeaders.h)")

    # TBB-based executor needs TBB headers not in PCH - skip PCH for this file
    set_source_files_properties(
        src/Core/TBBVirtualTaskExecutor.cpp
        PROPERTIES SKIP_PRECOMPILE_HEADERS ON
    )
endif()

# ============================================================================
# Source Groups for IDE Organization
# ============================================================================

source_group("Data\\Core\\Headers" FILES ${RENDERGRAPH_DATA_CORE_HEADERS})
source_group("Data\\Core\\Sources" FILES ${RENDERGRAPH_DATA_CORE_SOURCES})
source_group("Data\\Nodes" FILES ${RENDERGRAPH_DATA_NODE_CONFIGS})
source_group("Connection\\Headers" FILES ${RENDERGRAPH_CONNECTION_HEADERS})
source_group("Connection\\Sources" FILES ${RENDERGRAPH_CONNECTION_SOURCES})
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

if(BUILD_TESTING OR BUILD_TESTS)
    add_subdirectory(tests)
endif()
