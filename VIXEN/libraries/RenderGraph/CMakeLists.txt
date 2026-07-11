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
    include/Data/Nodes/BlitNodeConfig.h  # Sampled Lighting Inc3 M1: presentation-only render-target->swapchain blit
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
    include/Data/Nodes/PassGroupNodeConfig.h
    include/Data/Nodes/RenderPassNodeConfig.h
    include/Data/Nodes/ShaderLibraryNodeConfig.h
    include/Data/Nodes/SwapChainNodeConfig.h
    include/Data/Nodes/TextureLoaderNodeConfig.h
    include/Data/Nodes/VertexBufferNodeConfig.h
    include/Data/Nodes/VoxelGridNodeConfig.h
    include/Data/Nodes/VoxelAABBConverterNodeConfig.h
    include/Data/Nodes/AccelerationStructureNodeConfig.h
    include/Data/Nodes/RayTracingPipelineNodeConfig.h
    include/Data/Nodes/TraceRaysNodeConfig.h
    include/Data/Nodes/WindowNodeConfig.h
    include/Data/Nodes/BodyOctreeSceneNodeConfig.h
    include/Data/Nodes/SkyProjectionNodeConfig.h
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
    include/Core/NodeRegistration.h
    include/Core/NodeType.h
    include/Core/NodeTypeRegistry.h
    include/Core/PerFrameResources.h
    include/Core/EngineConfig.h          # AR#7: instantiable engine config
    include/Core/EngineContext.h         # AR#7: instantiable EngineContext (owns registry/bus/graph/calibration)
    include/Core/RenderGraph.h
    include/Core/FrameSyncScheduler.h     # auto-sync P2: pure scheduling core + FrameSyncScheduler class
    include/Core/PassGroupSchedule.h     # auto-sync P4: node-local barrier baking (header-only)
    include/Core/PassRecorder.h          # auto-sync P4 M2: baggage-free recorder (barrier replay + compute + render)
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

# Emptied: the deprecated SparseVoxelOctree (src/Data/VoxelOctree.cpp) was removed (live path is
# LaineKarrasOctree). The remaining Data/ files are struct-only headers — no .cpp to compile here.
set(RENDERGRAPH_DATA_CORE_SOURCES
)

set(RENDERGRAPH_CORE_SOURCES
    # Note: Memory/Lifetime sources moved to ResourceManagement library
    # Note: ConnectionRule.cpp split into Connection/ folder
    src/Core/FailScenario.cpp  # Inc 1: whole file compiles out when VIXEN_FAIL_SCENARIOS is OFF
    src/Core/GPUPerformanceLogger.cpp
    src/Core/GPUQueryManager.cpp  # Sprint 6.3: Task #344 - Query pool coordinator
    src/Core/GraphLifecycleHooks.cpp
    src/Core/GraphTopology.cpp
    src/Core/LoopManager.cpp
    src/Core/NodeInstance.cpp
    src/Core/NodeRegistration.cpp
    src/Core/NodeType.cpp
    src/Core/NodeTypeRegistry.cpp
    src/Core/PerFrameResources.cpp
    src/Core/EngineContext.cpp           # AR#7: instantiable EngineContext
    src/Core/RenderGraph.cpp
    src/Core/FrameSyncScheduler.cpp     # auto-sync P2: pure scheduling core
    src/Core/PassRecorder.cpp           # auto-sync P4 M2: baggage-free recorder (barrier replay + compute + render)
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
    include/Nodes/BlitNode.h  # Sampled Lighting Inc3 M1: presentation-only render-target->swapchain blit
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
    include/Nodes/PassGroupNode.h
    include/Nodes/RenderPassNode.h
    include/Nodes/ResourceGathererNode.h
    include/Nodes/ShaderLibraryNode.h
    include/Nodes/SwapChainNode.h
    include/Nodes/TextureLoaderNode.h
    include/Nodes/VertexBufferNode.h
    include/Nodes/VoxelGridNode.h
    include/Nodes/VoxelAABBConverterNode.h
    include/Nodes/AccelerationStructureNode.h
    include/Nodes/RayTracingPipelineNode.h
    include/Nodes/TraceRaysNode.h
    include/Nodes/WindowNode.h
    include/Nodes/BodyOctreeSceneNode.h
)

set(RENDERGRAPH_NODE_SOURCES
    src/Nodes/BoolOpNode.cpp
    src/Nodes/BlitNode.cpp  # Sampled Lighting Inc3 M1: presentation-only render-target->swapchain blit
    src/Nodes/ConstantNode.cpp  # M3: self-registration TU for the header-only Constant node types
    src/Nodes/CameraNode.cpp
    src/Nodes/CommandPoolNode.cpp
    src/Nodes/ComputeDispatchNode.cpp
    src/Nodes/ComputeStageNode.cpp      # auto-sync P5b M2: generic single-compute-pass submit node
    src/Nodes/ComputePipelineNode.cpp
    src/Nodes/MultiDispatchNode.cpp
    src/Nodes/DebugBufferReaderNode.cpp
    src/Nodes/DepthBufferNode.cpp
    src/Nodes/RenderTargetNode.cpp
    src/Nodes/RaySizeCoefNode.cpp       # M4: live LOD ray-cone recompute from render-target height
    src/Nodes/PickIdTargetNode.cpp
    src/Nodes/InstanceBufferNode.cpp
    src/Nodes/StorageBufferNode.cpp     # auto-sync P4 M4: generic arbitrary-sized SSBO node
    src/Nodes/DynamicInstanceBufferNode.cpp
    src/Nodes/LightingConfigNode.cpp    # Sampled Lighting Inc0 M3: LightingConfig upload ring
    src/Nodes/ShadowConfigNode.cpp      # Sampled Lighting Inc1 M4: ShadowConfig upload ring
    src/Nodes/AccumulationConfigNode.cpp  # Sampled Lighting Inc2 M1: AccumulationConfig upload ring
    src/Nodes/AccumulationHistoryNode.cpp # Sampled Lighting Inc2 M1: persistent temporal-accumulation history image
    src/Nodes/WorldPosHistoryNode.cpp     # Sampled Lighting Inc3 M2: persistent worldPos/depth companion history image (KI-023)
    src/Nodes/PrevCameraConfigNode.cpp    # Sampled Lighting Inc2 M3: prev-frame camera matrix upload ring
    src/Nodes/ReservoirConfigNode.cpp     # Sampled Lighting Inc3 M3: ReservoirConfig upload ring (M4/M5 scaffolding)
    src/Nodes/LightTreeBufferNode.cpp     # Sampled Lighting Inc3 M4: mip-cut light-tree upload ring (RIS candidate source)
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
    src/Nodes/PassGroupNode.cpp
    src/Nodes/RenderPassNode.cpp
    src/Nodes/ShaderLibraryNode.cpp
    src/Nodes/SwapChainNode.cpp
    src/Nodes/TextureLoaderNode.cpp
    src/Nodes/VertexBufferNode.cpp
    src/Nodes/VoxelGridNode.cpp
    src/Nodes/VoxelAABBConverterNode.cpp
    src/Nodes/AccelerationStructureNode.cpp
    src/Nodes/RayTracingPipelineNode.cpp
    src/Nodes/TraceRaysNode.cpp
    src/Nodes/WindowNode.cpp
    src/Nodes/BodyOctreeSceneNode.cpp
    src/Nodes/ShellRevalidateNode.cpp
    src/Nodes/SkyProjectionNode.cpp  # Tiered ESVO Inc1 M3: address-derived sky-point composite pass
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
    include/Ui/UiHitMask.h
    include/Ui/IView.h
    include/Ui/ViewBlob.h
    include/Ui/ViewStore.h
    include/Ui/ViewBlobFile.h
    include/Ui/ViewWireReader.h
    include/Ui/BlobView.h
    include/Data/Nodes/UIRenderNodeConfig.h
    include/Nodes/UIRenderNode.h
)

set(RENDERGRAPH_UI_SOURCES
    src/Ui/VixenRmlRenderInterface.cpp
    src/Ui/UiHitMask.cpp
    src/Ui/ViewStore.cpp
    src/Ui/ViewBlobFile.cpp
    src/Ui/ViewWireReader.cpp
    src/Ui/BlobView.cpp
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
# Create Libraries — split for build granularity:
#   RenderGraphCore   graph engine, ZERO concrete-node dependencies
#   RenderGraphNodes  the concrete nodes (+ their heavy deps), depends on Core
#   RenderGraph       back-compat INTERFACE facade (consumers link this, get both)
# Editing a node can never recompile RenderGraphCore — that is the whole point.
# ============================================================================

# ----------------------------------------------------------------------------
# RenderGraphCore — graph engine, zero concrete-node dependencies
# ----------------------------------------------------------------------------
add_library(RenderGraphCore STATIC
    ${RENDERGRAPH_DATA_CORE_HEADERS}
    ${RENDERGRAPH_DATA_CORE_SOURCES}
    ${RENDERGRAPH_CONNECTION_HEADERS}
    ${RENDERGRAPH_CONNECTION_SOURCES}
    ${RENDERGRAPH_CORE_HEADERS}
    ${RENDERGRAPH_CORE_SOURCES}
    ${RENDERGRAPH_DEBUG_HEADERS}
    ${RENDERGRAPH_DEBUG_SOURCES}
    ${RENDERGRAPH_EVENT_HEADERS}
    ${RENDERGRAPH_SELECTION_HEADERS}
    ${RENDERGRAPH_SELECTION_SOURCES}
)

# ----------------------------------------------------------------------------
# RenderGraphNodes — the concrete nodes (+ their heavy deps), depends on Core
# ----------------------------------------------------------------------------
add_library(RenderGraphNodes STATIC
    ${RENDERGRAPH_DATA_NODE_CONFIGS}
    ${RENDERGRAPH_NODE_HEADERS}
    ${RENDERGRAPH_NODE_SOURCES}
    ${RENDERGRAPH_UI_HEADERS}
    ${RENDERGRAPH_UI_SOURCES}
)

# ----------------------------------------------------------------------------
# RenderGraph — back-compat facade: existing consumers link this and get both.
# RenderGraphNodes is linked WHOLE-ARCHIVE so the per-node VIXEN_REGISTER_NODE
# registrars (anonymous-namespace statics that nothing references) are not
# stripped by the linker. Without it RegisterAllNodes() registers nothing — the
# guard is test_node_self_registration.
# ----------------------------------------------------------------------------
add_library(RenderGraph INTERFACE)
target_link_libraries(RenderGraph INTERFACE RenderGraphCore)

if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
    target_link_libraries(RenderGraph INTERFACE
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,RenderGraphNodes>")
elseif(MSVC)
    target_link_libraries(RenderGraph INTERFACE RenderGraphNodes)
    target_link_options(RenderGraph INTERFACE
        "/WHOLEARCHIVE:$<TARGET_FILE:RenderGraphNodes>")
else()
    target_link_libraries(RenderGraph INTERFACE
        "-Wl,--whole-archive" RenderGraphNodes "-Wl,--no-whole-archive")
endif()

# ============================================================================
# Compiler Options (both compiled targets)
# ============================================================================

# MSVC: Enable /FS for parallel compilation PDB access and /bigobj for large object files
if(MSVC)
    target_compile_options(RenderGraphCore PRIVATE /FS /bigobj)
    target_compile_options(RenderGraphNodes PRIVATE /FS /bigobj)
endif()

# C++23 standard
target_compile_features(RenderGraphCore PUBLIC cxx_std_23)
target_compile_features(RenderGraphNodes PUBLIC cxx_std_23)

# ============================================================================
# Include Directories (both targets: include/ root + generated SDI dirs)
# ============================================================================
# SDI header priority order: (1) dev-time tool-generated in source tree,
# (2) build-time main app, (3) build-time benchmark app, (4) pre-built binaries.
# Common headers (Headers.h / VixenHash.h / MeshData.h) come from Core::Core
# (linked below), not the application layer (AR#3/#4 build-layering severance).

foreach(rg_target RenderGraphCore RenderGraphNodes)
    target_include_directories(${rg_target}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${VIXEN_ROOT}/generated/sdi>
            $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/application/main/generated/sdi>
            $<BUILD_INTERFACE:${VIXEN_BINARIES_DIR}/generated/sdi>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
endforeach()

# Note: CashSystem and ShaderManagement includes come from target_link_libraries below

# ============================================================================
# Link Dependencies
# ============================================================================

# Core: graph-engine deps only. No glfw/rmlui/freetype — verified that no Core,
# Connection, Data, Debug or Selection TU includes those; they are Nodes-only.
target_link_libraries(RenderGraphCore
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
)

# Nodes: depends on Core + the windowing/UI deps the concrete nodes pull in.
target_link_libraries(RenderGraphNodes
    PUBLIC
        RenderGraphCore
        glfw                   # Cross-platform window/surface/input (WindowNode/InputNode/InstanceNode)
        rmlui_core             # Data-driven UI (RML/RCSS); freetype is its transitive font engine
        freetype
    PRIVATE
        stb                    # header-only; only UiHitMask.cpp uses it (stbi_load for image hit-masks)
)

# RmlUi is built static (FetchContent); code including its headers needs this
# define. Only the Nodes lib (UIRenderNode + UI headers) includes RmlUi.
target_compile_definitions(RenderGraphNodes PUBLIC RMLUI_STATIC_LIB)

# UIRenderNode loads RmlUi assets (fonts/docs) via paths relative to the exe (vixen_stage_assets
# stages them next to demo binaries). Consumers that run from a different CWD (e.g. the UNDERTOW host)
# wouldn't find them; this source-dir fallback lets the node resolve assets/ui/* from the engine tree
# regardless of CWD. Private — only UIRenderNode.cpp uses it.
# NOTE (merge integration): post the M2 library split, `RenderGraph` is an INTERFACE alias and cannot
# carry PRIVATE definitions; UIRenderNode.cpp lives in RenderGraphNodes, so target that lib (mirrors the
# RMLUI_STATIC_LIB define above).
target_compile_definitions(RenderGraphNodes PRIVATE
    VIXEN_UI_ASSET_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

# Sampled Lighting Inc3 M1 finding: SkyProjectionNode::ResolveShaderPath() (and any other
# RenderGraphNodes node compiling its own shaders straight from source) falls back to CWD-relative
# paths ("shaders", "../shaders") when VIXEN_SHADER_SOURCE_DIR isn't defined -- which it never was
# for this target (only application/main/CMakeLists.txt defined it, for VixenApp). Pre-existing gap,
# not introduced by M1; mirrors application/main's own definition exactly so every RenderGraphNodes
# node gets the same reliable, CWD-independent shader-source resolution VixenApp has always had.
target_compile_definitions(RenderGraphNodes PRIVATE
    VIXEN_SHADER_SOURCE_DIR="${VIXEN_SHADER_SOURCE_DIR}")

# ============================================================================
# Install and Properties (both static libs)
# ============================================================================

# Set output directory
set_target_properties(RenderGraphCore RenderGraphNodes PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    FOLDER "Libraries"
)

# Platform surface handling is now cross-platform via GLFW (glfwCreateWindowSurface +
# glfwGetRequiredInstanceExtensions); no VK_USE_PLATFORM_WIN32_KHR define needed.

# Precompiled headers for faster compilation. RenderGraphHeaders.h is Vulkan/GLM/STL
# only (no node deps), so it lives on Core. Add a node-side PCH only if measured
# beneficial (YAGNI for now).
if(USE_PRECOMPILED_HEADERS)
    target_precompile_headers(RenderGraphCore PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/RenderGraphHeaders.h"
    )
    message(STATUS "RenderGraphCore: Precompiled headers enabled (using RenderGraphHeaders.h)")

    # TBB-based executor needs TBB headers not in PCH - skip PCH for this file
    set_source_files_properties(
        src/Core/TBBVirtualTaskExecutor.cpp
        PROPERTIES SKIP_PRECOMPILE_HEADERS ON
    )

    # UiHitMask is intentionally dependency-light (pure math + header-only stb); skip the heavy PCH.
    set_source_files_properties(
        src/Ui/UiHitMask.cpp
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
