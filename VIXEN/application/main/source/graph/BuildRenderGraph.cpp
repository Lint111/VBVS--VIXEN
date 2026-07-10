// BuildRenderGraph -- extracted from VulkanGraphApplication.cpp (M4: per-subgraph construction TU).
// Editing a node config now recompiles only the subgraph TU(s) wiring it, not the
// app's lifecycle code. Node includes below are derived from this subgraph's wiring.
//
// SP2 body-octree render route (merged onto the M4 TU split): this subgraph builds the
// BodyOctreeSceneNode + BodyInstanceRayMarch.comp route (bindings 1/2/3/5/10) and the
// composite-HUD/UI-selection passes, so it also pulls the UI nodes. The include ORDER below
// is load-bearing: BodyOctreeSceneNode.h MUST precede UIRenderNode.h (and any RmlUi/robin_hood
// header) because BodyOctreeSceneNode.h transitively pulls gaia.h (ShellOctree ->
// LaineKarrasOctree -> ISVOStructure), whose std::hash<> specialisations must be visible before
// RmlUi's bundled robin_hood.h wraps them.
#include "VulkanGraphApplication.h"
#include <algorithm>  // std::clamp for the VIXEN_PROCEDURAL_UBER_DEMO N clamp
#include <cmath>    // std::tan for the LOD ray-cone (raySizeCoef) computation
#include <cstdlib>  // std::strtof for the VIXEN_RENDER_SCALE env parse (M4)
#include <fstream>  // Inc0 M5: read BodyInstanceRayMarch.comp's raw source for the recipe splice
#include <sstream>  // Inc0 M5: rdbuf() into a string for the splice
#include "Recipe/UberShaderSplice.h"  // Inc0 M5: SpliceProceduralRecipesIntoSource
#include "Connection/ConnectionModifier.h"
#include "Connection/Modifiers/FieldExtractionModifier.h"
#include "Connection/Modifiers/AccumulationSortConfig.h"  // SEL-P3: accumulation-connect sort key (provider fan-in)
#include "Core/NodeRegistration.h"
#include "MeshData.h"
#include "VoxelRayMarchNames.h"  // Generated SDI shader-binding constants (VoxelRayMarch::*)
// --- nodes this subgraph wires ---
#include "Data/Nodes/BodyOctreeSceneNodeConfig.h"  // M-wire: sparse shell octree + instance SSBO config
#include "Data/Nodes/CameraNodeConfig.h"
#include "Data/Nodes/CommandPoolNodeConfig.h"
#include "Data/Nodes/ComputeDispatchNodeConfig.h"
#include "Data/Nodes/ComputePipelineNodeConfig.h"
#include "Data/Nodes/ConstantNodeConfig.h"
#include "Data/Nodes/DebugBufferReaderNodeConfig.h"
#include "Data/Nodes/DepthBufferNodeConfig.h"
#include "Data/Nodes/DescriptorResourceGathererNodeConfig.h"
#include "Data/Nodes/DescriptorSetNodeConfig.h"
#include "Data/Nodes/DeviceNodeConfig.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "Data/Nodes/FramebufferNodeConfig.h"
#include "Data/Nodes/GeometryRenderNodeConfig.h"
#include "Data/Nodes/GraphicsPipelineNodeConfig.h"
#include "Data/Nodes/InputNodeConfig.h"
#include "Data/Nodes/InstanceNodeConfig.h"
#include "Data/Nodes/LoopBridgeNodeConfig.h"
#include "Data/Nodes/PickIdTargetNodeConfig.h"
#include "Data/Nodes/PresentNodeConfig.h"
#include "Data/Nodes/PushConstantGathererNodeConfig.h"
#include "Data/Nodes/RaySizeCoefNodeConfig.h"  // M4: live LOD ray-cone recompute
#include "Data/Nodes/RenderPassNodeConfig.h"
#include "Data/Nodes/RenderTargetNodeConfig.h"  // M4: render-scale decoupling offscreen target
#include "Data/Nodes/SelectionCoordinatorNodeConfig.h"
#include "Data/Nodes/ShaderLibraryNodeConfig.h"
#include "Data/Nodes/SkyProjectionNodeConfig.h"  // Tiered ESVO Inc1 M3: address-derived sky-point composite pass
#include "Data/Nodes/SwapChainNodeConfig.h"
#include "Data/Nodes/TextureLoaderNodeConfig.h"
#include "Data/Nodes/UIRenderNodeConfig.h"  // S0: composite-HUD render node config
#include "Data/Nodes/UISelectionProviderNodeConfig.h"  // SEL-P3: UI-domain selection provider config
#include "Data/Nodes/VertexBufferNodeConfig.h"
#include "Data/Nodes/VoxelGridNodeConfig.h"
#include "Data/Nodes/VoxelSelectionProviderNodeConfig.h"
#include "Data/Nodes/WindowNodeConfig.h"
// M-wire: BodyOctreeSceneNode.h MUST precede UIRenderNode.h (gaia std::hash before robin_hood) — see file header above.
#include "MipBake.h"  // Tiered-ESVO Inc2 M4: BakeAndAttachMipPool for the tier-crossing demo scene
#include "Nodes/BodyOctreeSceneNode.h"  // M-wire: sparse shell octree + instance SSBO
#include "Nodes/CameraNode.h"
#include "Nodes/CommandPoolNode.h"
#include "Nodes/ComputeDispatchNode.h"
#include "Nodes/ComputePipelineNode.h"
#include "Nodes/ConstantNodeType.h"
#include "Nodes/DebugBufferReaderNode.h"
#include "Nodes/DepthBufferNode.h"
#include "Nodes/DescriptorResourceGathererNode.h"
#include "Nodes/DescriptorSetNode.h"
#include "Nodes/DeviceNode.h"
#include "Nodes/FrameSyncNode.h"
#include "Nodes/FramebufferNode.h"
#include "Nodes/GeometryRenderNode.h"
#include "Nodes/GraphicsPipelineNode.h"
#include "Nodes/InputNode.h"
#include "Nodes/InstanceNode.h"
#include "Nodes/LoopBridgeNode.h"
#include "Nodes/PickIdTargetNode.h"
#include "Nodes/PresentNode.h"
#include "Nodes/PushConstantGathererNode.h"
#include "Nodes/RaySizeCoefNode.h"  // M4: live LOD ray-cone recompute
#include "Nodes/RenderPassNode.h"
#include "Nodes/RenderTargetNode.h"  // M4: render-scale decoupling offscreen target
#include "Nodes/SelectionCoordinatorNode.h"
#include "Nodes/ShaderLibraryNode.h"
#include "Nodes/SkyProjectionNode.h"  // Tiered ESVO Inc1 M3: address-derived sky-point composite pass
#include "Nodes/SwapChainNode.h"
#include "Nodes/TextureLoaderNode.h"
#include "Nodes/UIRenderNode.h"  // S0: composite-HUD render node (RmlUi) — AFTER BodyOctreeSceneNode.h
#include "Nodes/UISelectionProviderNode.h"  // SEL-P3: UI-domain selection provider (RmlUi hit-test)
#include "Nodes/VertexBufferNode.h"
#include "Nodes/VoxelGridNode.h"
#include "Nodes/VoxelSelectionProviderNode.h"
#include "Nodes/WindowNode.h"

void VulkanGraphApplication::BuildRenderGraph() {
    if (!renderGraph) {
        mainLogger->Error("Cannot build render graph: RenderGraph not initialized");
        return;
    }

    // S0: opt into the UI-only RmlUi demo graph via env var, leaving the voxel path untouched.
    if (std::getenv("VIXEN_UI_DEMO")) {
        mainLogger->Info("VIXEN_UI_DEMO set - building UI-only RmlUi demo graph");
        BuildUIGraph();
        return;
    }

    // AR#31: opt into the isolated instanced-cube raster demo via env var, leaving the
    // live voxel-compute path untouched.
    if (std::getenv("VIXEN_INSTANCING_DEMO")) {
        mainLogger->Info("VIXEN_INSTANCING_DEMO set - building instanced-cube raster demo graph");
        BuildInstancingDemoGraph();
        return;
    }

    // AR#21 P4: opt into the isolated auto-sync FrameGraph demo via env var. Proves
    // buffer-hazard auto-synchronization (compute->compute->render->present in ONE
    // command buffer via PassGroupNode). Leaves the live voxel-compute path untouched.
    if (std::getenv("VIXEN_AUTOSYNC_DEMO")) {
        mainLogger->Info("VIXEN_AUTOSYNC_DEMO set - building auto-sync FrameGraph demo graph");
        BuildAutoSyncDemoGraph();
        return;
    }

    // AR#21 P5b M2: opt into the multi-submit fan-in demo via env var. Proves
    // TIMELINE-ONLY ordering across separate compute submits: 2 independent producer
    // compute submits write 2 buffers, 1 consumer compute submit waits BOTH via 2 baked
    // timeline edges (NO binary handoff between them) + writes the swapchain. Leaves the
    // live voxel-compute path untouched.
    if (std::getenv("VIXEN_FANIN_DEMO")) {
        mainLogger->Info("VIXEN_FANIN_DEMO set - building multi-submit fan-in timeline demo graph");
        BuildFanInDemoGraph();
        return;
    }

    mainLogger->Info("Building complete render pipeline with typed connections");

    // ===================================================================
    // PHASE 1: Create all nodes
    // ===================================================================

    // --- Infrastructure Nodes ---
    NodeHandle instanceNode = renderGraph->AddNode<InstanceNodeType>( "main_instance");  // Phase 1.1
    NodeHandle windowNode = renderGraph->AddNode<WindowNodeType>("main_window");
    windowNode_ = windowNode;                        // store for GetWindowHandle() live lookup
    NodeHandle deviceNode = renderGraph->AddNode<DeviceNodeType>("main_device");
    NodeHandle swapChainNode = renderGraph->AddNode<SwapChainNodeType>("main_swapchain");
    NodeHandle commandPoolNode = renderGraph->AddNode<CommandPoolNodeType>("main_cmd_pool");

    NodeHandle presentNode = renderGraph->AddNode<PresentNodeType>("present");

    // --- Phase G: Compute Pipeline Nodes ---
    NodeHandle computeShaderLib = renderGraph->AddNode<ShaderLibraryNodeType>("compute_shader_lib");
    computeShaderLibNode_ = computeShaderLib;  // stored so RecompileProceduralShader can MarkNodeNeedsRecompile (Inc0 M5)
    NodeHandle descriptorGatherer = renderGraph->AddNode<DescriptorResourceGathererNodeType>("compute_desc_gatherer");  // Phase H
    NodeHandle pushConstantGatherer = renderGraph->AddNode<PushConstantGathererNodeType>("push_constant_gatherer");  // Phase H
    NodeHandle computeDescriptorSet = renderGraph->AddNode<DescriptorSetNodeType>("compute_descriptors");
    NodeHandle computePipeline = renderGraph->AddNode<ComputePipelineNodeType>("test_compute_pipeline");
    NodeHandle computeDispatch = renderGraph->AddNode<ComputeDispatchNodeType>("test_dispatch");
    NodeHandle frameSyncNode = renderGraph->AddNode<FrameSyncNodeType>("frame_sync");

    // M4: offscreen render target the compute dispatch writes into (render-scale decoupling).
    // Sized by EXTENT_SOURCE (the swapchain) x PARAM_SCALE; ComputeDispatchNode blits it to the
    // swapchain after dispatch. See Widescreen-Perf-Fix-Plan-2026-07.md M4.
    NodeHandle renderTargetNode = renderGraph->AddNode<RenderTargetNodeType>("compute_render_target");

    // --- Ray Marching Nodes ---
    NodeHandle cameraNode = renderGraph->AddNode<CameraNodeType>("raymarch_camera");
    cameraNode_ = cameraNode;  // Sparse-Mip ESVO LOD Inc1 M4c: store for Update()'s live residency-trigger lookup
    NodeHandle voxelGridNode = renderGraph->AddNode<VoxelGridNodeType>("voxel_grid");
    voxelGridNode_ = voxelGridNode;                  // store for MarkVoxelSceneDirty() (debug buffers only; not the render source post M-wire)

    // M-wire Task 8: sparse shell octree node — the live render source for bodies.
    // Outputs OCTREE_NODES/BRICKS/MATERIALS/CONFIG_BUFFER (identical slot names to VoxelGridNode,
    // so the descriptor wiring for bindings 1/2/3/5 just points here instead) plus
    // INSTANCE_BUFFER (binding 10) and INSTANCE_COUNT.
    NodeHandle bodyOctreeSceneNode = renderGraph->AddNode<BodyOctreeSceneNodeType>("body_octree_scene");
    bodyOctreeSceneNode_ = bodyOctreeSceneNode;      // store so SetBodyInstances() can forward to it

    // --- Input Node ---
    NodeHandle inputNode = renderGraph->AddNode<InputNodeType>("input_handler");
    inputNode_ = inputNode;                          // store for Update()'s live ProcessPendingInput() lookup

    // --- Pick ID Target (AR#35 GPU picking P1: R32_UINT storage-image ring at binding 9) ---
    NodeHandle pickIdTargetNode = renderGraph->AddNode<PickIdTargetNodeType>("pick_id_target");

    // --- Voxel Selection Provider (SEL-P2: providers are nodes) — on a click edge it reads back the
    // center pick-ID texel from PickIdTargetNode's ID image, decodes brick/voxel, and emits a
    // SelectionCandidate into the coordinator's MultiConnect candidate slot. ---
    NodeHandle voxelSelectionProviderNode = renderGraph->AddNode<VoxelSelectionProviderNodeType>("voxel_selection_provider");
    // Provider HIT/miss is user-facing; enable its logger to the terminal (defaults DISABLED).
    if (auto* provInst = renderGraph->GetInstance(voxelSelectionProviderNode)) {
        if (auto* pl = provInst->GetLogger()) { pl->SetEnabled(true); pl->SetTerminalOutput(true); }
    }

    // --- UI Selection Provider (SEL-P3: providers are nodes) — on a click edge it hit-tests the HUD's
    // Rml::Context at the cursor and emits a SelectionCandidate (priority 10) into the coordinator's
    // gather slot, so a HUD click OCCLUDES the voxel pick (priority 0). It reads the live context from
    // the UI composite node via SetUiRenderNode (wired below, once uiCompositeNode exists). ---
    NodeHandle uiSelectionProviderNode = renderGraph->AddNode<UISelectionProviderNodeType>("ui_selection_provider");
    uiSelectionProviderNode_ = uiSelectionProviderNode;   // store for GetUiSelectionProviderNode() live lookup (host drains HUD clicks)
    // Provider HIT/miss is user-facing; enable its logger to the terminal (defaults DISABLED).
    if (auto* uiProvInst = renderGraph->GetInstance(uiSelectionProviderNode)) {
        if (auto* pl = uiProvInst->GetLogger()) { pl->SetEnabled(true); pl->SetTerminalOutput(true); }
    }

    // --- Selection Coordinator (SEL-P2: engine-wide selection; gathers provider candidates via a
    // MultiConnect slot, priority-resolves, and owns the durable SelectionSet) ---
    NodeHandle selectionCoordinatorNode = renderGraph->AddNode<SelectionCoordinatorNodeType>("selection_coordinator");
    // Node loggers default DISABLED (NodeInstance ctor); selection results are user-facing, so enable
    // this node's logger to the terminal (otherwise its HIT/miss + diagnostics are silently dropped).
    if (auto* selInst = renderGraph->GetInstance(selectionCoordinatorNode)) {
        if (auto* sl = selInst->GetLogger()) { sl->SetEnabled(true); sl->SetTerminalOutput(true); }
    }

    NodeHandle physicsLoopBridge = renderGraph->AddNode<LoopBridgeNodeType>("physics_loop");
    NodeHandle physicsLoopIDConstant = renderGraph->AddNode<ConstantNodeType>("physics_loop_id");

    // M-wire Task 8: push constants for BodyInstanceRayMarch.comp (fields 8 and 9).
    // raySizeCoef: LOD cone-spread constant, recomputed LIVE from the render target's height every
    // Compile (M4 — was a ConstantNode frozen at graph-build time, see RaySizeCoefNodeConfig).
    // raySizeBias = 0.0 (pinhole camera; no bias at origin).
    NodeHandle raySizeCoefNode = renderGraph->AddNode<RaySizeCoefNodeType>("ray_size_coef");
    // Node loggers default DISABLED; the "[LOD] raySizeCoef recomputed" line is a live-gate signal
    // for the resize->recompile cascade (M4.3), so enable it (mirrors voxelSelectionProviderNode below).
    if (auto* rscInst = renderGraph->GetInstance(raySizeCoefNode)) {
        if (auto* rl = rscInst->GetLogger()) { rl->SetEnabled(true); rl->SetTerminalOutput(true); }
    }
    NodeHandle raySizeBiasConstant = renderGraph->AddNode<ConstantNodeType>("ray_size_bias");

    // Tiered-ESVO Inc2 M4 Task 9 live-gate knob: a demo-only ConstantNode that, when
    // VIXEN_TIER_CROSSING_LOD_COEF_OVERRIDE is set, is wired to push-constant field 8 INSTEAD of
    // raySizeCoefNode (never used otherwise -- default path is byte-identical to pre-M4). Bumping
    // RaySizeCoefNode's own FOV parameter was tried and rejected: raySizeCoef = 2*tan((fovRad/
    // height)/2) grows only linearly with fovDegrees in the small-angle regime this project's real
    // FOV values live in, so even an extreme (170deg) override only yields ~3.8x raySizeCoef --
    // nowhere near enough to force a single octant's leaf-level footprint sub-pixel while the whole
    // sphere silhouette stays resolved. A DIRECT literal override (e.g. 10.0, as this increment's own
    // GPU test harness — test_tier_crossing_lod_residency.cpp — already uses to force every leaf-level
    // footprint sub-pixel) is the correct, robust lever.
    NodeHandle tierCrossingLodCoefOverrideConstant = renderGraph->AddNode<ConstantNodeType>("tier_crossing_lod_coef_override");

    NodeHandle debugCaptureNode = renderGraph->AddNode<DebugBufferReaderNodeType>("debug_capture");

    // --- Sky-projection composite pass (Tiered ESVO Inc1 M3: address-derived sky points) ---
    // A color-only graphics pass layered over the compute output, sitting BETWEEN the voxel
    // compute and the UI/HUD composite (compute -> sky-projection -> UI, so the HUD still draws
    // over everything including sky points; UI stays the LAST pass, unchanged): RenderPassNode
    // (LOAD, initial=General, final=General — hands off to the UI composite pass's own
    // initial=General, since UI is still the one that transitions to PresentSrc) + FramebufferNode
    // (swapchain image views) + SkyProjectionNode. Mirrors the ui_composite_* triple exactly, one
    // stage earlier in the chain.
    NodeHandle skyProjectionRenderPassNode  = renderGraph->AddNode<RenderPassNodeType>("sky_projection_render_pass");
    NodeHandle skyProjectionFramebufferNode = renderGraph->AddNode<FramebufferNodeType>("sky_projection_framebuffer");
    NodeHandle skyProjectionNode = renderGraph->AddNode<SkyProjectionNodeType>("sky_projection");
    skyProjectionNode_ = skyProjectionNode;           // stored for potential live lookup
    // Node loggers default DISABLED (NodeInstance ctor); the fixture's computed direction/
    // magnitude values are the live-gate's ground truth (M3 Progress Log records the hand-
    // computed expectation to compare against), so enable this node's logger to the terminal —
    // mirrors raySizeCoefNode/voxelSelectionProviderNode's own "live-gate signal" opt-in below.
    if (auto* skyInst = renderGraph->GetInstance(skyProjectionNode)) {
        if (auto* sl = skyInst->GetLogger()) { sl->SetEnabled(true); sl->SetTerminalOutput(true); }
    }

    // --- UI composite pass (HUD over the voxel render) ---
    // A color-only graphics pass layered over the compute output: RenderPassNode (LOAD, initial=General,
    // final=PresentSrc) + FramebufferNode (swapchain image views) + UIRenderNode (composite). Mirrors
    // BuildUIGraph's UIRenderNode shape, but LOADs the voxel image instead of clearing.
    NodeHandle uiRenderPassNode  = renderGraph->AddNode<RenderPassNodeType>("ui_composite_render_pass");
    NodeHandle uiFramebufferNode = renderGraph->AddNode<FramebufferNodeType>("ui_composite_framebuffer");
    NodeHandle uiCompositeNode   = renderGraph->AddNode<UIRenderNodeType>("ui_composite_render");
    uiRenderNode_ = uiCompositeNode;                 // store for GetUiRenderNode() live lookup (host SetHudData)

    // SEL-P3: give the UI selection provider the composite UIRenderNode, so it can hit-test that
    // node's Rml::Context on a click (the context is a raw RmlUi pointer created in UIRenderNode's
    // CompileImpl — not a graph slot, so it is passed by node reference, not connected). The context
    // is null until the UI node first compiles; the provider tolerates that (emits a miss).
    if (auto* uiNodeInst = static_cast<UIRenderNode*>(renderGraph->GetInstance(uiCompositeNode))) {
        if (auto* uiProvInst = static_cast<UISelectionProviderNode*>(renderGraph->GetInstance(uiSelectionProviderNode))) {
            uiProvInst->SetUiRenderNode(uiNodeInst);
        }
    }

    mainLogger->Info("Created node instances (including compute pipeline, camera, voxel grid, gatherers, selection provider, and UI composite pass)");

    // ===================================================================
    // PHASE 2: Configure node parameters
    // ===================================================================

    // Window parameters
    auto* window = static_cast<WindowNode*>(renderGraph->GetInstance(windowNode));
    window->SetParameter(WindowNodeConfig::PARAM_WIDTH, static_cast<uint32_t>(width));
    window->SetParameter(WindowNodeConfig::PARAM_HEIGHT, static_cast<uint32_t>(height));

    // Device parameters (default GPU = 0)
    auto* device = static_cast<DeviceNode*>(renderGraph->GetInstance(deviceNode));
    device->SetParameter(DeviceNodeConfig::PARAM_GPU_INDEX, 0u);

    // M4: render-scale decoupling. VIXEN_RENDER_SCALE in (0,1] shrinks the offscreen target the
    // compute dispatch writes into relative to the swapchain; ComputeDispatchNode blits it back up.
    // Default 1.0 = same resolution as the swapchain (render-scale disabled, byte-identical to pre-M4).
    float renderScale = 1.0f;
    if (const char* renderScaleEnv = std::getenv("VIXEN_RENDER_SCALE")) {
        renderScale = std::strtof(renderScaleEnv, nullptr);
        if (!(renderScale > 0.0f) || renderScale > 1.0f) {
            if (mainLogger && mainLogger->IsEnabled()) {
                mainLogger->Warning("[BuildRenderGraph] VIXEN_RENDER_SCALE=" + std::string(renderScaleEnv) +
                                    " out of (0,1] — clamping to 1.0");
            }
            renderScale = 1.0f;
        }
    }
    auto* renderTarget = static_cast<RenderTargetNode*>(renderGraph->GetInstance(renderTargetNode));
    renderTarget->SetParameter(RenderTargetNodeConfig::PARAM_SCALE, renderScale);
    // STORAGE for the compute imageStore; TRANSFER_SRC for the blit-to-swapchain source.
    renderTarget->SetParameter(RenderTargetNodeConfig::PARAM_USAGE,
        static_cast<uint32_t>(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT));
    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] Render-scale=" + std::to_string(renderScale) +
                         " (VIXEN_RENDER_SCALE env; 1.0 = full resolution)");
    }

    // Present parameters (needed for both graphics and compute)
    auto* present = static_cast<PresentNode*>(renderGraph->GetInstance(presentNode));
    // Critique R6: no vkDeviceWaitIdle per present. The frame is already paced by the
    // per-flight in-flight fences + imageAvailable/renderComplete semaphores (+ per-image
    // present fences when VK_EXT_swapchain_maintenance1 is available — SwapChainNode waits
    // them after acquire). The full device drain serialized every frame and dominated frame
    // time through the WSLg paravirtualized device (~186ms/frame measured at 500x500).
    present->SetParameter(PresentNodeConfig::WAIT_FOR_IDLE, false);

    // Phase 0.4: Loop ID constant (connects to LoopBridgeNode) - needed for both graphics and compute
    auto* loopIDConst = static_cast<ConstantNode*>(renderGraph->GetInstance(physicsLoopIDConstant));
    loopIDConst->SetValue<uint32_t>(physicsLoopID);
    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] Loop ID set, moving to shader library...");
    }

    // Vertical FOV of the ray-march camera, in degrees. Single source of truth: used both to
    // configure CameraNode (PARAM_FOV, below) and to derive the LOD ray-cone spread (raySizeCoef).
    constexpr float kRaymarchCameraFovDegrees = 45.0f;

    // M-wire Task 8 / M4: set LOD push constant values (fields 8 and 9 of BodyInstanceRayMarch.comp).
    // raySizeCoef is the ray cone spread per unit distance — drives the screen-space-error LOD
    // stop in BodyInstanceRayMarch.comp (gated on raySizeCoef > 0.0). RaySizeCoefNode recomputes it
    // LIVE every Compile from the render target's live height (wired below, once renderTargetNode
    // is in scope) — was a one-shot ConstantNode frozen at the INITIAL window height (rank 6: a
    // resize left it stale, silently under-detailing large windows). kRaymarchCameraFovDegrees is
    // the vertical FOV; fed to both CameraNode (PARAM_FOV) and RaySizeCoefNode so they stay in
    // lock-step. raySizeBias = 0.0 (pinhole camera; zero cone diameter at origin).
    auto* raySizeCoef = static_cast<RaySizeCoefNode*>(renderGraph->GetInstance(raySizeCoefNode));
    raySizeCoef->SetParameter(RaySizeCoefNodeConfig::PARAM_FOV_DEGREES, kRaymarchCameraFovDegrees);
    auto* raySizeBiasConst = static_cast<ConstantNode*>(renderGraph->GetInstance(raySizeBiasConstant));
    raySizeBiasConst->SetValue<float>(0.0f);   // 0.0 = pinhole camera bias

    // Tiered-ESVO Inc2 M4 Task 9 live-gate knob (see tierCrossingLodCoefOverrideConstant's own
    // declaration comment above for why a direct literal, not an FOV bump, is the correct lever).
    bool tierCrossingLodCoefOverrideActive = false;
    {
        auto* lodOverrideConst = static_cast<ConstantNode*>(renderGraph->GetInstance(tierCrossingLodCoefOverrideConstant));
        if (const char* lodOverrideEnv = std::getenv("VIXEN_TIER_CROSSING_LOD_COEF_OVERRIDE")) {
            const float overrideValue = std::strtof(lodOverrideEnv, nullptr);
            lodOverrideConst->SetValue<float>(overrideValue);
            tierCrossingLodCoefOverrideActive = true;
            mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_CROSSING_LOD_COEF_OVERRIDE: raySizeCoef "
                              "forced to " + std::to_string(overrideValue) +
                              " (bypasses RaySizeCoefNode entirely for this run)");
        } else {
            lodOverrideConst->SetValue<float>(0.0f);  // unused when the override isn't active
        }
    }

    auto* frameSync = static_cast<FrameSyncNode*>(renderGraph->GetInstance(frameSyncNode));

    // Voxel ray marching compute shader (VoxelRayMarch.comp)
    // Load from pre-compiled shaders in build directory
    auto* computeShaderLibNode = static_cast<ShaderLibraryNode*>(renderGraph->GetInstance(computeShaderLib));

    // M-wire Task 8: use the instanced shell-octree ray-march shader (BodyInstanceRayMarch.comp).
    // Replaces VoxelRayMarch_Compressed.comp. Bindings 1/2/3/5 come from BodyOctreeSceneNode;
    // binding 10 = per-body instance SSBO; bindings 4/8 = debug/counters from voxelGridNode.
    computeShaderLibNode->RegisterShaderBuilder([this](int vulkanVer, int spirvVer) {
        ShaderManagement::ShaderBundleBuilder builder;

        constexpr const char* shaderName = "BodyInstanceRayMarch.comp";
        constexpr const char* programName = "BodyInstanceRayMarch";

        // Find shader source — try compile-time dir first, then fallback runtime paths.
        std::vector<std::filesystem::path> possiblePaths = {
#ifdef VIXEN_SHADER_SOURCE_DIR
            std::string(VIXEN_SHADER_SOURCE_DIR) + "/" + shaderName,
#endif
            std::string("shaders/") + shaderName,
            std::string("../shaders/") + shaderName,
            shaderName
        };

        std::filesystem::path compPath;
        for (const auto& path : possiblePaths) {
            if (std::filesystem::exists(path)) {
                compPath = path;
                break;
            }
        }

        if (compPath.empty()) {
            if (mainLogger && mainLogger->IsEnabled()) {
                mainLogger->Error("[BuildRenderGraph] " + std::string(shaderName) + " not found in search paths");
                mainLogger->Error("[BuildRenderGraph] Current working directory: " + std::filesystem::current_path().string());
#ifdef VIXEN_SHADER_SOURCE_DIR
                mainLogger->Error("[BuildRenderGraph] VIXEN_SHADER_SOURCE_DIR: " VIXEN_SHADER_SOURCE_DIR);
#endif
            }
            throw std::runtime_error(std::string(shaderName) + " not found - check shader search paths");
        }

        // Lazy-Procedural-Delta-Baseline Inc0 M5 Task 11: read the raw source and splice in
        // every registered procedural recipe's emitted field function + the evalRecipeField/
        // getRecipeBoundSphere switches, BEFORE handing the source to the builder. Uses
        // AddStage (source text), NOT AddStageFromFile — the file is still the origin of the
        // #include-relative-path text, but the text itself is no longer the file's own bytes
        // verbatim. #include resolution is unaffected: it goes through the explicit
        // AddIncludePath calls below (preprocessor-driven), not sourcePath (AddStageFromFile's
        // OWN #include convenience, which this path deliberately bypasses).
        std::ifstream compFile(compPath);
        std::ostringstream compBuf;
        compBuf << compFile.rdbuf();
        const std::string rawSource = compBuf.str();

        std::string splicedSource;
        // M6 Task 13: collect the concatenated per-recipe occupancy-grid blob alongside the
        // splice, then push it to BodyOctreeSceneNode's new SSBO (binding 16) — same "derived
        // once at shader-build time, forces a recompile like everything else the splice
        // touches" discipline as the bound-sphere/relaxation literals already baked in.
        std::vector<float> occupancyGridBlob;
        try {
            splicedSource = Vixen::SVO::Recipe::SpliceProceduralRecipesIntoSource(
                rawSource, proceduralRecipes_, &occupancyGridBlob);
        } catch (const std::exception& e) {
            if (mainLogger && mainLogger->IsEnabled()) {
                mainLogger->Error(std::string("[BuildRenderGraph] procedural recipe splice failed: ") + e.what());
            }
            throw;
        }
        if (auto* bodyScene = static_cast<Vixen::RenderGraph::BodyOctreeSceneNode*>(
                renderGraph->GetInstance(bodyOctreeSceneNode_))) {
            bodyScene->SetOccupancyGrid(std::move(occupancyGridBlob));
        }

        builder.SetProgramName(programName)
               .SetPipelineType(ShaderManagement::PipelineTypeConstraint::Compute)
               .SetTargetVulkanVersion(vulkanVer)
               .SetTargetSpirvVersion(spirvVer)
               .AddIncludePath("shaders")
               .AddIncludePath("../shaders")
#ifdef VIXEN_SHADER_SOURCE_DIR
               .AddIncludePath(VIXEN_SHADER_SOURCE_DIR)
#endif
               // Inc0 M5: BodyInstanceRayMarch.comp now #includes "recipe/SdfCoreKernels.glsl"
               // (the SdfCore_* kernel set the spliced recipe field functions call), which
               // lives under libraries/SVO/shaders — a different tree than the paths above.
               .AddIncludePath("libraries/SVO/shaders")
               .AddIncludePath("../libraries/SVO/shaders")
#ifdef VIXEN_SVO_SHADER_SOURCE_DIR
               .AddIncludePath(VIXEN_SVO_SHADER_SOURCE_DIR)
#endif
               .AddStage(ShaderManagement::ShaderStage::Compute, splicedSource, "main");

        // Shader counters (perf sweep rank 2) are compiled OUT unconditionally: the live
        // app has no consumer for them, and every pixel was paying 3-4 unread atomic RMWs
        // into a HOST_COHERENT SSBO. No env opt-in — ShaderBundleBuilder::SetStageDefines
        // does line-level token substitution, not textual #define injection, so it cannot
        // drive ShaderCounters.glsl's #ifdef ENABLE_SHADER_COUNTERS guard (verified: passing
        // an empty-value define here turns "#ifdef ENABLE_SHADER_COUNTERS" into "#ifdef ",
        // a glslang compile error). Re-enable by hand-editing this .comp's #define if needed.

        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Info("[BuildRenderGraph] Using BodyInstanceRayMarch shader: " + compPath.string() +
                             " (" + std::to_string(proceduralRecipes_.Ids().size()) + " procedural recipes spliced)");
            mainLogger->Info("[BuildRenderGraph] Octree buffers at bindings 1/2/3/5 (BodyOctreeSceneNode); instances at binding 10");
        }

        return builder;
    });

    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] Registered VoxelRayMarch shader builder");
    }

    // Phase G: Compute dispatch parameters
    auto* dispatch = static_cast<ComputeDispatchNode*>(renderGraph->GetInstance(computeDispatch));
    uint32_t dispatchX = width / 8;
    uint32_t dispatchY = height / 8;
    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] Setting dispatch dims: " + std::to_string(dispatchX) + "x" + std::to_string(dispatchY) + "x1 (from window " + std::to_string(width) + "x" + std::to_string(height) + ")");
    }
    dispatch->SetParameter(ComputeDispatchNodeConfig::DISPATCH_X, dispatchX);  // Workgroup size 8x8
    dispatch->SetParameter(ComputeDispatchNodeConfig::DISPATCH_Y, dispatchY);
    dispatch->SetParameter(ComputeDispatchNodeConfig::DISPATCH_Z, 1u);

    // Ray marching: Camera parameters
    auto* camera = static_cast<CameraNode*>(renderGraph->GetInstance(cameraNode));

    // Enable camera logger to debug position
    if (auto* cameraLogger = camera->GetLogger()) {
        cameraLogger->SetEnabled(false);
        cameraLogger->SetTerminalOutput(false);
    }

    camera->SetParameter(CameraNodeConfig::PARAM_FOV, kRaymarchCameraFovDegrees);  // shared with raySizeCoef
    camera->SetParameter(CameraNodeConfig::PARAM_NEAR_PLANE, 0.1f);
    camera->SetParameter(CameraNodeConfig::PARAM_FAR_PLANE, 500.0f);
    // Camera presets for Cornell box (grid spans [0,128], center at 64,64,64)
    // Uncomment one preset below:

    // PRESET 1: Front view looking into box (camera outside grid)
    // Orbit mode: yaw=0 means camera at +Z looking toward orbitCenter
    // yaw=pi means camera at -Z looking toward +Z (into the grid)
    // For camera at +Z looking toward grid, use yaw=0
    camera->SetParameter(CameraNodeConfig::PARAM_CAMERA_X, 64.0f);   // Center X (ignored in orbit mode)
    camera->SetParameter(CameraNodeConfig::PARAM_CAMERA_Y, 64.0f);   // Center Y (ignored in orbit mode)
    camera->SetParameter(CameraNodeConfig::PARAM_CAMERA_Z, 300.0f);  // Outside grid (ignored in orbit mode)
    camera->SetParameter(CameraNodeConfig::PARAM_YAW, 0.0f);         // Camera at +Z, looking toward -Z
    camera->SetParameter(CameraNodeConfig::PARAM_PITCH, 0.0f);

    // Tiered-ESVO Inc2 M5 Task 11: VIXEN_TIER_ZOOM_DEMO drives the camera via
    // SetOrbitDistanceForTest, which orbits around CameraNode's own orbitCenter -- left at its
    // stale Cornell-box default (5,5,5) unless a consumer configures PARAM_ORBIT_CENTER_*
    // (CameraNode.cpp's own SetupImpl comment: "orbitCenter itself is the pivot and can't be
    // derived from position alone"). The tier-crossing demo body sits at world (64,64,64) (see
    // the VIXEN_TIER_CROSSING_DEMO scene-construction block below), nowhere near (5,5,5) -- an
    // unconfigured orbit here would swing the camera away from the body on the very first
    // SetOrbitDistanceForTest call (caught live: every captured frame was byte-identical sky
    // until this was added). Configuring PARAM_ORBIT_CENTER_* here declares orbit-mode intent
    // from SetupImpl (CameraNode.cpp's own orbitActive_ latch), so EngageOrbit()'s idempotent
    // guard makes the zoom demo's first SetOrbitDistanceForTest call a no-op re-seed.
    if (std::getenv("VIXEN_TIER_ZOOM_DEMO")) {
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_X, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Y, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Z, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_DISTANCE, 236.0f);  // matches the at-rest Z=300 distance
        mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_ZOOM_DEMO: orbitCenter set to demo body's "
                          "world center (64,64,64) so the scripted zoom actually orbits the body");
    }
    camera->SetParameter(CameraNodeConfig::PARAM_GRID_RESOLUTION, 128u);

    // Ray marching: Voxel grid parameters
    auto* voxelGrid = static_cast<VoxelGridNode*>(renderGraph->GetInstance(voxelGridNode));
    voxelGrid->SetParameter(VoxelGridNodeConfig::PARAM_RESOLUTION, 128u);
    // Scene type defaults to the Cornell-box test scene; a host can override it (e.g. the UNDERTOW
    // render host sets VIXEN_SCENE=starsystem after registering its bodies with the scene factory).
    const char* sceneEnv = std::getenv("VIXEN_SCENE");
    voxelGrid->SetParameter(VoxelGridNodeConfig::PARAM_SCENE_TYPE,
                            std::string(sceneEnv != nullptr ? sceneEnv : "cornell"));

    // --- Standalone default body scene (Option A) ---
    // The live render path dispatches BodyInstanceRayMarch.comp, which only draws per-body INSTANCES
    // (numInstances = clamp(pc.instanceCount, ...)). The UNDERTOW host feeds real bodies at runtime via
    // VulkanGraphApplication::SetBodyInstances() -> BodyOctreeSceneNode::SetInstances(), but standalone
    // VIXEN.exe has no body source, so with 0 instances every ray misses and the screen is just the
    // dark sky color (looks black). Seed a few default instances so the standalone app shows a scene.
    //
    // SetInstances REPLACES the list (instances_ = std::move(...)), so a host that calls SetBodyInstances
    // at runtime fully overwrites these defaults — they are a standalone fallback only, no host gating
    // needed. BodyOctreeSceneNode builds 3 shell-octree "kinds" (octreeIndex 0/1/2), each a [0,64]^3 shell
    // (base center (32,32,32)). The instance transform is instOrigin = (rayOrigin - worldPos)/renderScale,
    // so a shell centered at world C needs worldPos = C - (32,32,32)*renderScale. We center the 3 shells
    // around the 128^3 grid centre (64,64,64) and spread them along X so they don't overlap and all sit
    // in the default camera view (verified on screen: three distinct red/green/white spheres).
    // color[3] is a per-instance tint that MULTIPLIES the kind's material (1=red, 2=green, 3=white), kept
    // near-white per instance (slight warm/neutral/cool bias) so each stays bright and the three are
    // distinguishable by both base material and tint.
    {
        if (std::getenv("VIXEN_TIER_CROSSING_DEMO")) {
            // Tiered-ESVO Inc2 M3 Task 8: live gate — a single tier-crossing leaf,
            // one PARENT SDF octree (octree 0) with ONE leaf marked farBit=1 via
            // MarkLeafAsTierCrossing, pointing at an independently-built CHILD SDF
            // octree (octree 1). Manually concatenated (mirrors test_tier_crossing_
            // construction.cpp's TwoTreeFixtureRoundTripsThroughSerializeAndConcatenate
            // — Concatenate/ConcatenateSdf call Serialize/SerializeSdf INTERNALLY and
            // would discard a pre-concatenation MarkLeafAsTierCrossing mutation, so
            // this loop replicates that bookkeeping by hand, exactly as the test does).
            //
            // Geometry: n=16, r=6.0, brickDepth=3 -> bricksPerAxis=2 -> the root's 8
            // children are ALL deterministic brick-level leaves (same fixture shape
            // test_tier_crossing_construction.cpp/test_tier_ref_table.cpp/test_mip_
            // sample_bake.cpp all rely on) -- FindFirstLeaf below locates the first
            // one deterministically instead of guessing an index.
            mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_CROSSING_DEMO: building hand-authored two-tree tier-crossing scene");

            constexpr int   kN          = 16;
            constexpr float kR          = 6.0f;
            constexpr int   kBrickDepth = 3;
            const glm::vec3 kCenter(8.0f, 8.0f, 8.0f);

            Vixen::SVO::RecipeParams rp{};
            rp.radius = kR;

            Vixen::SVO::SdfBakeResult parentBaked =
                Vixen::SVO::BakeRecipeToSdfWorld(Vixen::SVO::RECIPE_SPHERE, kCenter, rp, kN, 2.0f);
            Vixen::SVO::SdfBodyOctree parentBody = Vixen::SVO::BuildSdfBodyOctree(parentBaked, kBrickDepth);

            // Child recipe deliberately DIFFERENT from the parent's, per the coordinator's
            // request for an unambiguous visual A/B (not just a different position):
            // a LARGER radius (fills more of the crossing leaf's local cell) AND — below,
            // after SerializeSdf — a solid saturated-magenta color override replacing the
            // shared BakeSdfWorld cosine-gradient (SdfBake.h's own per-voxel color formula
            // is identical for parent/child otherwise, since it is hardcoded inside the
            // shared bake function, not exposed as a parameter — overriding channelPool's
            // SEM_COLOR channel post-bake is the surgical fix that does not touch that
            // shared, widely-used function).
            constexpr float kChildR = 7.2f;  // vs parent's 6.0f — visibly larger/rounder
            Vixen::SVO::RecipeParams childRp{};
            childRp.radius = kChildR;
            Vixen::SVO::SdfBakeResult childBaked =
                Vixen::SVO::BakeRecipeToSdfWorld(Vixen::SVO::RECIPE_SPHERE, kCenter, childRp, kN, 2.0f);
            Vixen::SVO::SdfBodyOctree childBody = Vixen::SVO::BuildSdfBodyOctree(childBaked, kBrickDepth);

            Vixen::SVO::SerializedOctree parentSer = Vixen::SVO::SerializeSdf(parentBody);
            Vixen::SVO::SerializedOctree childSer  = Vixen::SVO::SerializeSdf(childBody);

            // Overwrite the CHILD's entire SEM_COLOR channel with a solid, saturated
            // magenta (1,0,1) — unmistakably distinct from the parent's warm-white/rainbow
            // cosine gradient (SdfBake.h's col = 0.5+0.5*cos(p*0.12+phase), which stays in
            // muted mid-tones and never reaches a pure saturated primary). Iterates every
            // brick/voxel slot in the child's channelPool directly (the same addressing
            // ShellOctreeGpu.h's own readPoolVoxel documents:
            // channelPool[brick*brickStrideFloats + channelBaseFloats(SEM_COLOR) + comp*512 + voxel]).
            {
                const uint32_t colorBase = childSer.channelBaseFloats(Vixen::SVO::SEM_COLOR);
                if (colorBase != 0xFFFFFFFFu) {
                    float* pool = reinterpret_cast<float*>(childSer.channelPool.data());
                    const size_t poolFloats = childSer.channelPool.size() / sizeof(float);
                    for (uint32_t brick = 0; brick < childSer.brickCount; ++brick) {
                        for (uint32_t comp = 0; comp < 3; ++comp) {
                            const float magentaComp = (comp == 1) ? 0.0f : 1.0f;  // (1,0,1)
                            for (uint32_t voxel = 0; voxel < Vixen::SVO::SerializedOctree::kVoxelsPerBrick; ++voxel) {
                                const size_t idx = static_cast<size_t>(brick) * childSer.brickStrideFloats
                                                 + colorBase + comp * Vixen::SVO::SerializedOctree::kVoxelsPerBrick + voxel;
                                if (idx < poolFloats) pool[idx] = magentaComp;
                            }
                        }
                    }
                    mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_CROSSING_DEMO: child channelPool SEM_COLOR overwritten to solid magenta (1,0,1)");
                } else {
                    mainLogger->Error("[BuildRenderGraph] VIXEN_TIER_CROSSING_DEMO: child has no SEM_COLOR channel — color override skipped");
                }
            }

            // Tiered-ESVO Inc2 M4 Task 9/10: bake + attach a real mip pool to BOTH trees
            // (ConcatenateSdfWithMips's own per-tree convention, MipBake.h) so
            // shadeFromMipSample has genuine coverage/color to read when either the LOD
            // gate or the residency check declines a crossing -- without this, the
            // fallback would silently degrade to the neutral-grey placeholder shade
            // (still correct/non-crashing, but a weaker "did it actually mip-shade real
            // geometry" proof). MUST run AFTER the magenta override above (BakeMipPool
            // reads serialized.channelPool directly, so an out-of-order bake would mip a
            // pre-override cosine-gradient color instead of the overridden magenta).
            if (const Vixen::SVO::Octree* parentOctForMip = parentBody.octree->getOctree()) {
                Vixen::SVO::BakeAndAttachMipPool(*parentOctForMip, parentSer);
            }
            if (const Vixen::SVO::Octree* childOctForMip = childBody.octree->getOctree()) {
                Vixen::SVO::BakeAndAttachMipPool(*childOctForMip, childSer);
            }

            // Locate a CAMERA-FACING leaf child in the parent's raw (pre-concatenation)
            // Octree (the same "scan childDescriptors directly" convention
            // test_tier_crossing_construction.cpp's FindAllLeaves uses), rather than
            // blindly taking the first leaf found. localToWorld is a PURE uniform scale
            // (ShellOctreeGpu.h: translate(0)*scale(10), no axis flip), so local [1,2)
            // maps monotonically to world space with no mirroring; the demo camera looks
            // down -Z from world Z=300 (yaw=0,pitch=0 -> forward=(0,0,-1)), so it sees the
            // sphere's +Z-facing (near-camera) hemisphere, i.e. LARGER local-z, i.e. an
            // octant with bit 2 (z) SET. Octant bit convention (SVOTypes.h mirroredToLocalOctant
            // and friends: bit0=x,bit1=y,bit2=z) confirmed directly by reading those functions.
            // The root's 8 children for this n=16/brickDepth=3 fixture are ALL brick-level
            // leaves (bricksPerAxis=2), so preferring octant>=4 (z bit set) is guaranteed to
            // find one deterministically.
            const Vixen::SVO::Octree* parentOct = parentBody.octree->getOctree();
            uint32_t markParentDescIdx = 0;
            int markOctant = -1;
            if (parentOct != nullptr) {
                const auto& descs = parentOct->root->childDescriptors;
                // First pass: prefer a camera-facing octant (bit2/z set -> octants 4-7).
                for (uint32_t i = 0; i < descs.size() && markOctant < 0; ++i) {
                    const Vixen::SVO::ChildDescriptor& d = descs[i];
                    for (int oct = 4; oct < 8; ++oct) {
                        if (d.hasChild(oct) && d.isLeaf(oct)) {
                            markParentDescIdx = i;
                            markOctant = oct;
                            break;
                        }
                    }
                }
                // Fallback: any leaf, if no camera-facing octant exists (shouldn't happen
                // for this fixture, but don't silently build an unmarked scene).
                if (markOctant < 0) {
                    for (uint32_t i = 0; i < descs.size() && markOctant < 0; ++i) {
                        const Vixen::SVO::ChildDescriptor& d = descs[i];
                        for (int oct = 0; oct < 8; ++oct) {
                            if (d.hasChild(oct) && d.isLeaf(oct)) {
                                markParentDescIdx = i;
                                markOctant = oct;
                                break;
                            }
                        }
                    }
                }
            }

            if (markOctant >= 0) {
                // TierRef: child's [1,2)-frame origin/scale expressed in the PARENT's
                // local frame (§3.2/§3.3). childScale=1.0 keeps the child at the SAME
                // physical scale as the parent for this milestone's proof (M3 does not
                // require a scale change — that is a rendering/LOD refinement, not the
                // mechanism this gate proves); childOriginLocal=(1.5,1.5,1.5) is the
                // parent-local frame's own center, so the child tree occupies the
                // SAME [1,2) cell the marked leaf itself occupies (a clean, well-
                // conditioned "known" placement for the hand-computed screen-position
                // cross-check below).
                Vixen::SVO::TierRef ref{};
                ref.childOctreeIndex = 1u;  // child will be concatenated at slot 1
                ref.childOriginLocal[0] = 1.5f;
                ref.childOriginLocal[1] = 1.5f;
                ref.childOriginLocal[2] = 1.5f;
                ref.childScale = 1.0f;
                constexpr uint8_t kChildRootScaleHint = 22;  // child's own root ESVO scale

                Vixen::SVO::MarkLeafAsTierCrossing(parentSer, markParentDescIdx, markOctant, ref, kChildRootScaleHint);

                // Manual concatenation (parent=slot0, child=slot1) — mirrors
                // ConcatenateSdf's own per-octree bookkeeping loop exactly.
                Vixen::SVO::ConcatenatedOctrees cat;
                cat.count = 2;
                cat.configs.resize(2);
                cat.nodeCounts.resize(2);
                cat.brickCounts.resize(2);
                cat.tierRefCounts.resize(2);

                Vixen::SVO::SerializedOctree* octs[2] = {&parentSer, &childSer};
                uint32_t nodeBase = 0, brickBase = 0, poolBase = 0, tierRefBase = 0, mipPoolBase = 0;
                for (int k = 0; k < 2; ++k) {
                    Vixen::SVO::SerializedOctree& s = *octs[k];
                    s.config.nodeArrayBase  = static_cast<int32_t>(nodeBase);
                    s.config.brickArrayBase = static_cast<int32_t>(brickBase);
                    Vixen::SVO::setSdfBrickArrayBase(s.config, poolBase);
                    Vixen::SVO::setTierRefTableBase(s.config, tierRefBase);
                    Vixen::SVO::setMipPoolBase(s.config, mipPoolBase);

                    cat.configs[k]       = s.config;
                    cat.nodeCounts[k]    = s.nodeCount;
                    cat.brickCounts[k]   = s.brickCount;
                    cat.tierRefCounts[k] = static_cast<uint32_t>(s.tierRefs.size());

                    cat.nodes.insert(cat.nodes.end(), s.nodes.begin(), s.nodes.end());
                    cat.bricks.insert(cat.bricks.end(), s.bricks.begin(), s.bricks.end());
                    cat.channelPool.insert(cat.channelPool.end(), s.channelPool.begin(), s.channelPool.end());
                    cat.brickGridLookup.insert(cat.brickGridLookup.end(), s.brickGridLookup.begin(), s.brickGridLookup.end());
                    cat.tierRefTable.insert(cat.tierRefTable.end(), s.tierRefs.begin(), s.tierRefs.end());
                    cat.mipPool.insert(cat.mipPool.end(), s.mipPool.begin(), s.mipPool.end());

                    if (cat.materials.empty()) {
                        cat.materials = s.materials;
                    }

                    nodeBase    += s.nodeCount;
                    brickBase   += s.brickCount;
                    poolBase    += s.brickCount * s.brickStrideFloats;
                    tierRefBase += static_cast<uint32_t>(s.tierRefs.size());
                    mipPoolBase += s.nodeCount * s.channelCount;
                }

                // Tiered-ESVO Inc2 M4 Task 10 live-gate knob: VIXEN_TIER_CROSSING_NONRESIDENT calls
                // RequestBrickResidency(false) below (NOT a direct setBrickResident() poke on
                // cat.configs[1] -- CreateOctreeBuffers's own `for (auto& cfg : concatenated_.
                // configs) setBrickResident(cfg, brickPoolUploaded_)` loop unconditionally
                // re-stamps EVERY config's brickResident from residencyRequested_/brickPoolUploaded_
                // on the very first Compile, so a pre-SetRecipePool poke on the concatenated struct
                // would be silently clobbered the instant the node actually builds its buffers).
                // RequestBrickResidency is a WHOLE-NODE flag applied uniformly to every octree in
                // this one ConcatenatedOctrees pool -- there is no existing mechanism to make the
                // child non-resident while the parent stays resident within a single
                // BodyOctreeSceneNode, so this demo's "non-resident" case makes BOTH trees
                // non-resident (both fall back to mip-shading, per Sparse-Mip's existing sentinel-
                // miss pattern) -- still a genuine, honest proof of the crossing correctly declining
                // and mip-shading rather than crashing/rendering garbage, just not isolated to the
                // child alone (that isolation would need a genuinely new per-octree residency
                // mechanism, out of this increment's scope per the design doc's own "no new
                // residency state machine" line).
                // Tiered-ESVO Inc2 M5 Task 11: VIXEN_TIER_ZOOM_DEMO reuses the SAME
                // RequestBrickResidency(false) start-state as VIXEN_TIER_CROSSING_NONRESIDENT
                // above, so the scripted zoom (VulkanGraphApplication::Update) has a real 0->1
                // transition to exercise mid-flight via its own scripted RequestBrickResidency(true)
                // at tick 24 -- proving the composed lifecycle live, not just as two separate runs.
                const bool forceNonResident = std::getenv("VIXEN_TIER_CROSSING_NONRESIDENT") != nullptr
                                            || std::getenv("VIXEN_TIER_ZOOM_DEMO") != nullptr;

                if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                    bodyScene->SetRecipePool(std::move(cat));
                    if (forceNonResident) {
                        bodyScene->RequestBrickResidency(false);
                        mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_CROSSING_NONRESIDENT/VIXEN_TIER_ZOOM_DEMO: "
                                          "RequestBrickResidency(false) -- both octrees mip-only at start");
                    } else {
                        // Lazy-Procedural-Delta-Baseline Inc0 M2 Task 4 demo-knob audit: both
                        // trees here are mip-baked (BakeAndAttachMipPool above), so this pool
                        // is mip-capable and M2's capability-derived default would flip it
                        // LAZY at boot -- a real behavior change for this demo, which existed
                        // to prove the tier-crossing MECHANISM (not residency laziness) and has
                        // always booted with real bricks resident. Pin eager explicitly so
                        // plain VIXEN_TIER_CROSSING_DEMO (no _NONRESIDENT/_ZOOM_DEMO) keeps its
                        // pre-M2 boot behavior byte-for-byte.
                        bodyScene->RequestBrickResidency(true);
                        mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_CROSSING_DEMO: "
                                          "RequestBrickResidency(true) -- pinned eager (M2 demo-knob audit)");
                    }

                    // ONE instance, pointing at octree 0 (the parent). Placed at the
                    // default camera's frame center so the WHOLE parent sphere (and
                    // thus, for at least some pixels, its tier-crossing leaf) is on
                    // screen.
                    //
                    // IMPORTANT: BodyInstanceRayMarch.comp's worldToLocal/localToWorld
                    // (SerializeSdf's kWorldGridSize=10.0f) maps the octree's OWN
                    // "config-local-world" cube to a FIXED [0,10]^3 span, INDEPENDENT
                    // of the bake's own grid resolution `n` -- gridMin/gridMax are never
                    // read by this shader (confirmed by direct grep; only VoxelRayMarch.comp's
                    // dense path reads them). So (unlike a naive "n * renderScale" guess)
                    // the instance transform's actual world span is
                    // renderScale * [0,10], centered at worldPos + 5*renderScale.
                    // renderScale=4.8 -> world diameter 48, matching the other demo
                    // bodies' ~48-unit apparent size (kHalf=24 in the Stored-SDF/
                    // Procedural demos above).
                    constexpr float kRenderScale = 4.8f;
                    constexpr float kHalf = 5.0f * kRenderScale;  // = 24.0f (half of the [0,10] span)
                    Vixen::SVO::BodyInstanceGpu inst{};
                    inst.worldPos[0]  = 64.0f - kHalf;
                    inst.worldPos[1]  = 64.0f - kHalf;
                    inst.worldPos[2]  = 64.0f - kHalf;
                    inst.renderScale  = kRenderScale;
                    inst.color[0]     = 1.0f;
                    inst.color[1]     = 1.0f;
                    inst.color[2]     = 1.0f;
                    inst.octreeIndex  = 0u;    // parent tree
                    inst.providerKind = 0u;    // PROVIDER_STORED
                    inst.recipeId     = 0u;

                    bodyScene->SetInstances({inst});
                    mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_CROSSING_DEMO: parent leaf ("
                                  + std::to_string(markParentDescIdx) + "," + std::to_string(markOctant)
                                  + ") marked tier-crossing -> child octree 1");
                }
            } else {
                mainLogger->Error("[BuildRenderGraph] VIXEN_TIER_CROSSING_DEMO: no leaf found in parent octree — demo scene not built");
            }
        } else if (const char* uberDemoEnv = std::getenv("VIXEN_PROCEDURAL_UBER_DEMO")) {
            // VIXEN_PROCEDURAL_UBER_DEMO — Lazy-Procedural-Delta-Baseline Inc0 M5 Task 12 zero-bake
            // live gate, generalized (perf-scaling measurement handoff) to an ARBITRARY recipe
            // count N. Registers N registry-driven recipes (recipeId >= 2) and positions them
            // OVERLAPPING along the default camera's Z sight line (bodies at increasing world-Z,
            // same X/Y so each ray that hits the nearer body's bound sphere would ALSO have hit a
            // farther one's, giving the entryT>bestT early-reject in main() something real to
            // reject). Value selects the recipe count via std::atoi, clamped to [1, kMaxUberN] —
            // the clamp exists only to keep a garbage/huge env value from producing an unbounded
            // allocation/splice, not because the switch itself is known to have a ceiling at that
            // size (finding out where the switch DOES break is the point of this measurement).
            // NO octree bake occurs for these bodies (RegisterProceduralRecipe never touches
            // BakeSdfWorld/BuildSdfBodyOctree) — the (a) proof for the live gate.
            constexpr int kMaxUberN = 2000;
            const int requestedN = std::atoi(uberDemoEnv);
            const int n = std::clamp(requestedN <= 0 ? 3 : requestedN, 1, kMaxUberN);
            mainLogger->Info("[BuildRenderGraph] VIXEN_PROCEDURAL_UBER_DEMO: registering " +
                             std::to_string(n) + " zero-bake procedural recipes");

            // Recipe programs must grow STRUCTURALLY distinct with N (not N clones of the same
            // opcodes with different numbers) so the switch's real cost -- shader size, register
            // pressure, warp divergence, compile time -- grows honestly. Programs 0/1/2 (index
            // i%3==0/1/2) reproduce the ORIGINAL 3 legacy programs byte-for-byte (plain sphere;
            // box+sphere SmoothUnion; sphere+Round-box SmoothSubtract) so N=3 and N=10 stay
            // comparable to pre-generalization measurements. Beyond that, the generator cycles a
            // {leaf prim} x {CSG op} x {modifier} product so each subsequent id gets a genuinely
            // different opcode program.
            using Vixen::SVO::Recipe::SdfOpCode;
            using Vixen::SVO::Recipe::SdfInstruction;
            auto sphereInstr = [](glm::vec3 c, float r) {
                SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Sphere;
                in.data[0] = c.x; in.data[1] = c.y; in.data[2] = c.z; in.data[3] = r;
                return in;
            };
            auto boxInstr = [](glm::vec3 he) {
                SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Box;
                in.data[0] = he.x; in.data[1] = he.y; in.data[2] = he.z;
                return in;
            };
            auto torusInstr = [](float majorR, float minorR) {
                SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Torus;
                in.data[0] = majorR; in.data[1] = minorR;
                return in;
            };
            auto roundInstr = [](float r) {
                SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Round; in.data[0] = r;
                return in;
            };
            auto onionInstr = [](float thickness) {
                SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Onion; in.data[0] = thickness;
                return in;
            };
            auto combineInstr = [](SdfOpCode op, float k) {
                SdfInstruction in{}; in.opCode = (uint8_t)op; in.data[2] = k;
                return in;
            };
            // CSG ops beyond the legacy 2 (SmoothUnion/SmoothSubtract), cycled for i>=3.
            const SdfOpCode kExtraCsgOps[] = {
                SdfOpCode::Union, SdfOpCode::Subtract, SdfOpCode::Intersect,
                SdfOpCode::SmoothIntersect, SdfOpCode::Xor, SdfOpCode::SmoothMax,
            };

            std::vector<Vixen::SVO::BodyInstanceGpu> uberBodies;
            uberBodies.reserve(static_cast<size_t>(n));
            constexpr float kSpacingZ = 40.0f;  // overlapping bound spheres along +Z sight line
            constexpr float kBaseZ    = 30.0f;
            const glm::vec3 kColors[3] = {
                glm::vec3(1.00f, 0.55f, 0.55f),
                glm::vec3(0.55f, 1.00f, 0.55f),
                glm::vec3(0.55f, 0.70f, 1.00f),
            };
            for (int i = 0; i < n; ++i) {
                const uint32_t recipeId = static_cast<uint32_t>(2 + i);
                const float instZ = kBaseZ + kSpacingZ * static_cast<float>(i);
                const glm::vec3 center(64.0f, 64.0f, instZ);

                std::vector<SdfInstruction> prog;
                const int shape = i % 3;
                if (i < 3) {
                    // The original 3 legacy programs, byte-for-byte, so N=3/N=10 measurement
                    // points stay comparable to pre-generalization runs.
                    if (shape == 0) {
                        prog = { sphereInstr(center, 8.0f) };
                    } else if (shape == 1) {
                        prog = { boxInstr(glm::vec3(6.0f, 6.0f, 6.0f)),
                                 sphereInstr(center + glm::vec3(4.0f, 0.0f, 0.0f), 5.0f),
                                 combineInstr(SdfOpCode::SmoothUnion, 1.5f) };
                    } else {
                        prog = { sphereInstr(glm::vec3(0.0f), 9.0f),
                                 boxInstr(glm::vec3(5.0f, 5.0f, 5.0f)),
                                 roundInstr(1.0f),
                                 combineInstr(SdfOpCode::SmoothSubtract, 1.0f) };
                    }
                } else {
                    // Structurally-varied extension: cycles {sphere/box/torus} x {6 extra CSG
                    // ops} x {none/Round/Onion modifier}, with varied radii/thicknesses so no
                    // two programs beyond the legacy 3 are opcode-for-opcode identical.
                    const int leaf = (i / 3) % 3;             // 0=sphere-pair,1=box-pair,2=torus-pair
                    const SdfOpCode csgOp = kExtraCsgOps[i % 6];
                    const int modSel = (i / 6) % 3;           // 0=none,1=Round,2=Onion
                    const float k = 0.5f + 0.1f * static_cast<float>(i % 7);
                    const float r1 = 6.0f + 0.05f * static_cast<float>(i % 40);
                    const float r2 = 3.0f + 0.03f * static_cast<float>(i % 30);

                    if (leaf == 0) {
                        prog = { sphereInstr(glm::vec3(0.0f), r1),
                                 sphereInstr(glm::vec3(r2 * 0.4f, 0.0f, 0.0f), r2),
                                 combineInstr(csgOp, k) };
                    } else if (leaf == 1) {
                        prog = { boxInstr(glm::vec3(r1, r1 * 0.8f, r1 * 0.6f)),
                                 sphereInstr(glm::vec3(r2 * 0.3f, 0.0f, 0.0f), r2),
                                 combineInstr(csgOp, k) };
                    } else {
                        prog = { torusInstr(r1, r2 * 0.4f),
                                 boxInstr(glm::vec3(r2, r2, r2)),
                                 combineInstr(csgOp, k) };
                    }
                    if (modSel == 1) {
                        prog.push_back(roundInstr(0.3f + 0.02f * static_cast<float>(i % 10)));
                    } else if (modSel == 2) {
                        prog.push_back(onionInstr(0.2f + 0.02f * static_cast<float>(i % 10)));
                    }
                }

                Vixen::SVO::RecipeRegistry::RecipeEntry entry{};
                entry.bytecode = std::move(prog);
                // Every program beyond shape==0 (i<3) samples in body-local / non-absolute space
                // (Box/Torus carry no position offset of their own) -- for this demo, authoring
                // boundCenter/boundRadius explicitly sidesteps relying on
                // DeriveConservativeBounds's local-origin-relative result matching a body's
                // actual world placement (recipeParams/worldPos are NOT read for recipeId>=2;
                // the field function samples WORLD p directly, unlike the legacy analytic path).
                if (!(i < 3 && shape == 0)) {
                    entry.boundCenter = center;
                    entry.boundRadius = 12.0f;
                }

                auto regResult = RegisterProceduralRecipe(recipeId, entry);
                if (regResult != Vixen::SVO::RecipeRegistry::RegisterResult::Ok) {
                    mainLogger->Error("[BuildRenderGraph] VIXEN_PROCEDURAL_UBER_DEMO: "
                                     "RegisterProceduralRecipe(" + std::to_string(recipeId) +
                                     ") failed, code " + std::to_string(static_cast<int>(regResult)));
                    continue;
                }

                Vixen::SVO::BodyInstanceGpu inst{};
                inst.worldPos[0] = 0.0f; inst.worldPos[1] = 0.0f; inst.worldPos[2] = 0.0f;  // unused: field samples world p directly
                inst.renderScale = 1.0f;   // unused by Procedural
                const glm::vec3& tint = kColors[shape];
                inst.color[0] = tint.x; inst.color[1] = tint.y; inst.color[2] = tint.z;
                inst.octreeIndex = 0u;    // unused by Procedural
                inst.providerKind = 1u;   // PROVIDER_PROCEDURAL
                inst.recipeId = recipeId; // >=2 -> routes through the spliced uber path
                uberBodies.push_back(inst);
            }

            if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                bodyScene->SetInstances(std::move(uberBodies));
                mainLogger->Info("[BuildRenderGraph] VIXEN_PROCEDURAL_UBER_DEMO: seeded " +
                                 std::to_string(n) + " zero-bake procedural body instances "
                                 "(0 BakeSdfWorld/BuildSdfBodyOctree calls for these bodies)");
            }
        } else if (std::getenv("VIXEN_STORED_SDF_DEMO")) {
            // VIXEN_STORED_SDF_DEMO — Stored-SDF bodies (Increment 2, M5 Task 10).
            // EnsureOctreesBuilt has baked 3 SdfBodyOctrees (kinds 0/1/2) via ConcatenateSdf,
            // setting configs[k].formatId = STORED_SDF and populating the sdfBricks /
            // brickGridLookup buffers (bindings 11/12). Instances use providerKind=0 (STORED)
            // and octreeIndex=0/1/2 to select the per-kind OctreeConfig.
            //
            // Transform convention (binary-shell / marchStoredSdf AABB):
            //   renderScale = 0.75       — scales grid-voxel [0,64] into world units
            //   worldPos    = center - 32*0.75 = center - 24
            //     → de-instance transform: instOrigin = (rayOrigin - worldPos) / renderScale
            //       so a ray at world center maps to grid (32,32,32) = [0,64] AABB center.
            //
            // Body centers in world space (same spread as the Procedural seed so the
            // default camera (X=64, Z=300, looking -Z) frames all three):
            //   left   center = (14, 64, 64)  → worldPos = (14-24, 64-24, 64-24) = (-10, 40, 40)
            //   centre center = (64, 64, 64)  → worldPos = (64-24, 64-24, 64-24) = ( 40, 40, 40)
            //   right  center = (114,64, 64)  → worldPos = (114-24,64-24, 64-24) = ( 90, 40, 40)
            constexpr float kRenderScale = 0.75f;
            constexpr float kHalf        = 32.0f * kRenderScale;  // = 24.0f

            auto placeStored = [&](float cx, float cy, float cz,
                                   float r, float g, float b,
                                   uint32_t octreeIdx) {
                Vixen::SVO::BodyInstanceGpu inst{};
                inst.worldPos[0]  = cx - kHalf;  // worldPos = center - 24 per axis
                inst.worldPos[1]  = cy - kHalf;
                inst.worldPos[2]  = cz - kHalf;
                inst.renderScale  = kRenderScale;
                inst.color[0]     = r;
                inst.color[1]     = g;
                inst.color[2]     = b;
                inst.octreeIndex  = octreeIdx;    // selects configs[k] (incl. formatId)
                inst.providerKind = 0u;           // PROVIDER_STORED: octree/Stored path
                inst.recipeId     = 0u;           // unused by Stored path
                return inst;
            };
            std::vector<Vixen::SVO::BodyInstanceGpu> storedBodies = {
                placeStored( 14.0f, 64.0f, 64.0f, 1.00f, 0.95f, 0.85f, 0u),  // left   — smooth sphere   (kind 0, red)
                placeStored( 64.0f, 64.0f, 64.0f, 0.55f, 0.75f, 1.00f, 1u),  // centre — displaced sphere (kind 1, green)
                placeStored(114.0f, 64.0f, 64.0f, 0.85f, 0.90f, 1.00f, 2u),  // right  — smooth sphere   (kind 2, white)
            };
            if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                bodyScene->SetInstances(std::move(storedBodies));
                mainLogger->Info("[BuildRenderGraph] VIXEN_STORED_SDF_DEMO: seeded 3 Stored-SDF body instances");
            }
        } else {
            // Default — Procedural SDF bodies (Increment 1): true smooth spheres, no octree.
            // worldPos = world centre; recipeParams = (radius, displaceAmp, displaceFreq).
            // Radius 24 matches the prior Stored shells' on-screen size (kHalf=24), so the
            // default camera frames all three. providerKind=1 selects the Procedural path.
            constexpr float kRadius = 24.0f;
            auto placeProcedural = [&](float cx, float cy, float cz,
                                       float r, float g, float b,
                                       uint32_t recipeId, float amp, float freq) {
                Vixen::SVO::BodyInstanceGpu inst{};
                inst.worldPos[0] = cx;
                inst.worldPos[1] = cy;
                inst.worldPos[2] = cz;
                inst.renderScale = 1.0f;            // unused by Procedural
                inst.color[0]    = r;
                inst.color[1]    = g;
                inst.color[2]    = b;
                inst.octreeIndex = 0u;              // unused by Procedural
                inst.providerKind = 1u;             // PROVIDER_PROCEDURAL
                inst.recipeId     = recipeId;       // 0 = sphere, 1 = displaced sphere
                inst.recipeParams[0] = kRadius;
                inst.recipeParams[1] = amp;
                inst.recipeParams[2] = freq;
                return inst;
            };
            std::vector<Vixen::SVO::BodyInstanceGpu> defaultBodies = {
                placeProcedural( 14.0f, 64.0f, 64.0f, 1.00f, 0.95f, 0.85f, 0u, 0.0f, 0.0f),  // left   — smooth star/sphere
                placeProcedural( 64.0f, 64.0f, 64.0f, 0.55f, 0.75f, 1.00f, 1u, 2.0f, 0.5f),  // centre — displaced planet
                placeProcedural(114.0f, 64.0f, 64.0f, 0.85f, 0.90f, 1.00f, 0u, 0.0f, 0.0f),  // right  — smooth sphere
            };
            if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                bodyScene->SetInstances(std::move(defaultBodies));
                mainLogger->Info("[BuildRenderGraph] Seeded 3 Procedural SDF body instances (standalone fallback)");
            }
        }
    }

    // Enable logging for VoxelGridNode to see octree generation
    if (auto* voxelLogger = voxelGrid->GetLogger()) {
        voxelLogger->SetEnabled(true);  // Enable to debug voxel rendering
        voxelLogger->SetTerminalOutput(true);
    }

    // Enable logging for descriptor gatherer to debug bindings
    auto* descGatherer = static_cast<DescriptorResourceGathererNode*>(renderGraph->GetInstance(descriptorGatherer));
    if (auto* gathererLogger = descGatherer->GetLogger()) {
        gathererLogger->SetEnabled(true);  // TEMP DEBUG: tracing the debug-capture attachment bug (KI-009 follow-up)
        gathererLogger->SetTerminalOutput(true);
    }

    // Enable logging for compute dispatch to see execution
    if (auto* dispatchLogger = dispatch->GetLogger()) {
        dispatchLogger->SetEnabled(true);
        dispatchLogger->SetTerminalOutput(true);
    }

    // Enable logging for push constant gatherer to see packing
    auto* pcGatherer = static_cast<PushConstantGathererNode*>(renderGraph->GetInstance(pushConstantGatherer));
    if (auto* pcLogger = pcGatherer->GetLogger()) {
        pcLogger->SetEnabled(false);
        pcLogger->SetTerminalOutput(false);
    }

    auto* debugCapture = static_cast<DebugBufferReaderNode*>(renderGraph->GetInstance(debugCaptureNode));
    debugCapture->SetParameter(DebugBufferReaderNodeConfig::PARAM_MAX_SAMPLES, 1000u);
    debugCapture->SetParameter(DebugBufferReaderNodeConfig::PARAM_AUTO_EXPORT, true);
    debugCapture->SetParameter(DebugBufferReaderNodeConfig::PARAM_EXPORT_FORMAT, static_cast<int>(DebugExportFormat::JSON));
    debugCapture->SetParameter(DebugBufferReaderNodeConfig::PARAM_OUTPUT_PATH, std::string("binaries/compute_debug_output"));
    debugCapture->SetParameter(DebugBufferReaderNodeConfig::PARAM_FRAMES_PER_EXPORT, 10u);
    if (auto* debugLogger = debugCapture->GetLogger()) {
        debugLogger->SetEnabled(true);
        debugLogger->SetTerminalOutput(true);
    }

    

    // --- Sky-projection composite pass parameters (Tiered ESVO Inc1 M3) ---
    // Sits between the compute (GENERAL) and the UI composite pass (which also expects
    // initial=General — see its own PARAM_INITIAL_LAYOUT below, unchanged): LOADs the voxel
    // output, draws the sky points, and leaves the image in GENERAL for the UI composite pass
    // to LOAD in turn (this pass does NOT transition to PresentSrc — UI still owns that, as the
    // last pass in the chain).
    auto* skyProjectionRenderPass = static_cast<RenderPassNode*>(renderGraph->GetInstance(skyProjectionRenderPassNode));
    skyProjectionRenderPass->SetParameter(RenderPassNodeConfig::PARAM_COLOR_LOAD_OP, AttachmentLoadOp::Load);   // preserve voxels
    skyProjectionRenderPass->SetParameter(RenderPassNodeConfig::PARAM_COLOR_STORE_OP, AttachmentStoreOp::Store);
    skyProjectionRenderPass->SetParameter(RenderPassNodeConfig::PARAM_INITIAL_LAYOUT, ImageLayout::General);   // compute leaves GENERAL
    skyProjectionRenderPass->SetParameter(RenderPassNodeConfig::PARAM_FINAL_LAYOUT, ImageLayout::General);     // UI composite pass LOADs GENERAL next
    skyProjectionRenderPass->SetParameter(RenderPassNodeConfig::PARAM_SAMPLES, 1u);

    auto* skyProjectionFramebuffer = static_cast<FramebufferNode*>(renderGraph->GetInstance(skyProjectionFramebufferNode));
    skyProjectionFramebuffer->SetParameter(FramebufferNodeConfig::PARAM_LAYERS, 1u);

    // --- UI composite pass parameters ---
    // The compute leaves the swapchain image in GENERAL (it no longer transitions to PRESENT_SRC); the
    // sky-projection pass LOADs+draws+leaves it in GENERAL (above); the UI render pass LOADs that
    // image, draws the HUD over it, and owns the →PRESENT_SRC transition.
    dispatch->SetParameter(ComputeDispatchNodeConfig::PARAM_LEAVE_IMAGE_IN_GENERAL, true);

    auto* uiRenderPass = static_cast<RenderPassNode*>(renderGraph->GetInstance(uiRenderPassNode));
    uiRenderPass->SetParameter(RenderPassNodeConfig::PARAM_COLOR_LOAD_OP, AttachmentLoadOp::Load);   // preserve voxels
    uiRenderPass->SetParameter(RenderPassNodeConfig::PARAM_COLOR_STORE_OP, AttachmentStoreOp::Store);
    uiRenderPass->SetParameter(RenderPassNodeConfig::PARAM_INITIAL_LAYOUT, ImageLayout::General);    // compute leaves GENERAL
    uiRenderPass->SetParameter(RenderPassNodeConfig::PARAM_FINAL_LAYOUT, ImageLayout::PresentSrc);   // ready for present
    uiRenderPass->SetParameter(RenderPassNodeConfig::PARAM_SAMPLES, 1u);

    auto* uiFramebuffer = static_cast<FramebufferNode*>(renderGraph->GetInstance(uiFramebufferNode));
    uiFramebuffer->SetParameter(FramebufferNodeConfig::PARAM_LAYERS, 1u);

    auto* uiComposite = static_cast<UIRenderNode*>(renderGraph->GetInstance(uiCompositeNode));
    uiComposite->SetParameter(UIRenderNodeConfig::PARAM_COMPOSITE, true);
    uiComposite->SetParameter(UIRenderNodeConfig::RML_DOCUMENT_PATH, std::string("assets/ui/hud.rml"));

    // View Contract Inc-2 Task 5: wire the app's native HudView onto the now-generic UI node.
    // Routed through WireHudView (HudViewBridge) rather than a direct SetView call here -- this TU
    // transitively includes BodyOctreeSceneNode.h's gaia.h (via the M-wire body-octree includes
    // above), and gaia vendors a DIFFERENT VERSION of RmlUi's bundled robin_hood.h under the SAME
    // include guard; the bridge is the one place HudView.h's RmlUi-touching inline code actually
    // instantiates, in a TU that never sees gaia.h (see HudViewBridge.h's file header).
    Vixen::App::WireHudView(*uiComposite, *hudView_);

    mainLogger->Info("Configured all node parameters (including camera, voxel grid, and UI composite pass)");

    // ===================================================================
    // PHASE 3: Wire connections using TypedConnection API
    // ===================================================================

    using namespace Vixen::RenderGraph;

    mainLogger->Info("Wiring node connections using TypedConnection API");

    // Use ConnectionBatch for atomic registration
    ConnectionBatch batch(renderGraph);

    // --- Instance → Device connection (Phase 1.1: Dependency injection) ---
    batch.Connect(instanceNode, InstanceNodeConfig::INSTANCE,
                  deviceNode, DeviceNodeConfig::INSTANCE_IN);

    // --- Device → Window connection (VkInstance passthrough) ---
    batch.Connect(deviceNode, DeviceNodeConfig::INSTANCE_OUT,
                  windowNode, WindowNodeConfig::INSTANCE);

    // --- Window → SwapChain connections ---
    batch.Connect(windowNode, WindowNodeConfig::WINDOW,
                  swapChainNode, SwapChainNodeConfig::WINDOW)
         .Connect(windowNode, WindowNodeConfig::WIDTH_OUT,
                  swapChainNode, SwapChainNodeConfig::WIDTH)
         .Connect(windowNode, WindowNodeConfig::HEIGHT_OUT,
                  swapChainNode, SwapChainNodeConfig::HEIGHT);

    // --- Window → Input connection ---
    batch.Connect(windowNode, WindowNodeConfig::WINDOW,
                  inputNode, InputNodeConfig::WINDOW);

    // --- Device → SwapChain connections ---
    batch.Connect(deviceNode, DeviceNodeConfig::INSTANCE_OUT,
                  swapChainNode, SwapChainNodeConfig::INSTANCE)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  swapChainNode, SwapChainNodeConfig::VULKAN_DEVICE_IN);

    // --- Device → FrameSync connection (Phase 0.2) ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  frameSyncNode, FrameSyncNodeConfig::VULKAN_DEVICE);

    // --- FrameSync → SwapChain connections (Phase 0.4) ---
    // Phase 0.4: Per-flight semaphores and current frame index
    batch.Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  swapChainNode, SwapChainNodeConfig::CURRENT_FRAME_INDEX);
    // Per-image in-flight fence tracking: SwapChainNode records this per-flight fence against the
    // acquired image and waits on it before the image's command buffer/descriptor/query resources
    // are reused (fixes the flights!=images desync — see SwapChainNode::ExecuteImpl).
    batch.Connect(frameSyncNode, FrameSyncNodeConfig::IN_FLIGHT_FENCE,
                  swapChainNode, SwapChainNodeConfig::IN_FLIGHT_FENCE);
    batch.Connect(frameSyncNode, FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY,
                  swapChainNode, SwapChainNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY);
    // FR-3: renderComplete + presentFences are now PRODUCED by swapChainNode (sized to the actual image count).

    // --- Device → CommandPool connection ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  commandPoolNode, CommandPoolNodeConfig::VULKAN_DEVICE_IN);

    // --- Device → Present device connection ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  presentNode, PresentNodeConfig::VULKAN_DEVICE_IN);

    // --- SwapChain → Present connections (for compute-only rendering) ---
    batch.Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_HANDLE,
                  presentNode, PresentNodeConfig::SWAPCHAIN)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX,
                  presentNode, PresentNodeConfig::IMAGE_INDEX);

    // --- UI composite → Present semaphore connection ---
    // The UI pass is now the frame's last submit: present waits on the UI's render-complete semaphore
    // (not the compute's). The compute's render-complete is consumed by the UI as the compute→UI handoff.
    batch.Connect(uiCompositeNode, UIRenderNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                  presentNode, PresentNodeConfig::RENDER_COMPLETE_SEMAPHORE);


    // --- Gatherer/ComputeDispatch → DebugBufferReader connections ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  debugCaptureNode, DebugBufferReaderNodeConfig::VULKAN_DEVICE_IN);
    batch.Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL,
                  debugCaptureNode, DebugBufferReaderNodeConfig::COMMAND_POOL);
    // Debug capture flows: Gatherer extracts from entries → ComputeDispatch (passthrough) → DebugReader
    batch.Connect(descriptorGatherer, DescriptorResourceGathererNodeConfig::DEBUG_CAPTURE,
                  computeDispatch, ComputeDispatchNodeConfig::DEBUG_CAPTURE);
    batch.Connect(computeDispatch, ComputeDispatchNodeConfig::DEBUG_CAPTURE_OUT,
                  debugCaptureNode, DebugBufferReaderNodeConfig::DEBUG_CAPTURE);
    // Fence connection - wait for GPU to finish before reading debug buffer
    batch.Connect(frameSyncNode, FrameSyncNodeConfig::IN_FLIGHT_FENCE,
                  debugCaptureNode, DebugBufferReaderNodeConfig::IN_FLIGHT_FENCE);

    // --- SwapChain → Present present-fence array (FR-3: owned by swapChainNode) ---
    batch.Connect(swapChainNode, SwapChainNodeConfig::PRESENT_FENCES_ARRAY,
                  presentNode, PresentNodeConfig::PRESENT_FENCE_ARRAY);

    // MVP: Shader connection happens in CompileRenderGraph (after device creation)

    // --- Phase 0.4: Loop System Connections ---
    batch.Connect(physicsLoopIDConstant, ConstantNodeConfig::OUTPUT,
                  physicsLoopBridge, LoopBridgeNodeConfig::LOOP_ID);

    // --- Phase G: Compute Pipeline Connections ---
    // Pipeline setup
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  computeShaderLib, ShaderLibraryNodeConfig::VULKAN_DEVICE_IN)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  computeDescriptorSet, DescriptorSetNodeConfig::VULKAN_DEVICE_IN)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  computePipeline, ComputePipelineNodeConfig::VULKAN_DEVICE_IN)
         // Phase H: Shader bundle → Gatherer for descriptor discovery
         .Connect(computeShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  descriptorGatherer, DescriptorResourceGathererNodeConfig::SHADER_DATA_BUNDLE)
         // Phase H: Gatherer → DescriptorSet (data-driven resources with embedded slotRole + debugCapture)
         .Connect(descriptorGatherer, DescriptorResourceGathererNodeConfig::DESCRIPTOR_RESOURCES,
                  computeDescriptorSet, DescriptorSetNodeConfig::DESCRIPTOR_RESOURCES)
         // Phase H: Push constant gatherer connections
         .Connect(computeShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  pushConstantGatherer, PushConstantGathererNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(pushConstantGatherer, PushConstantGathererNodeConfig::PUSH_CONSTANT_DATA,
                  computeDispatch, ComputeDispatchNodeConfig::PUSH_CONSTANT_DATA)
         .Connect(pushConstantGatherer, PushConstantGathererNodeConfig::PUSH_CONSTANT_RANGES,
                  computeDispatch, ComputeDispatchNodeConfig::PUSH_CONSTANT_RANGES)
         // Pass shader bundle directly to descriptor set and pipeline (needed during Compile)
         .Connect(computeShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  computeDescriptorSet, DescriptorSetNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(computeShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  computePipeline, ComputePipelineNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(computeShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  computeDispatch, ComputeDispatchNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(computeDescriptorSet, DescriptorSetNodeConfig::DESCRIPTOR_SET_LAYOUT,
                  computePipeline, ComputePipelineNodeConfig::DESCRIPTOR_SET_LAYOUT)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  computeDispatch, ComputeDispatchNodeConfig::VULKAN_DEVICE_IN)
         .Connect(computePipeline, ComputePipelineNodeConfig::PIPELINE,
                  computeDispatch, ComputeDispatchNodeConfig::COMPUTE_PIPELINE)
         .Connect(computePipeline, ComputePipelineNodeConfig::PIPELINE_LAYOUT,
                  computeDispatch, ComputeDispatchNodeConfig::PIPELINE_LAYOUT)
         .Connect(computeDescriptorSet, DescriptorSetNodeConfig::DESCRIPTOR_SETS,
                  computeDispatch, ComputeDispatchNodeConfig::DESCRIPTOR_SETS)
         .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL,
                  computeDispatch, ComputeDispatchNodeConfig::COMMAND_POOL);

    // --- Ray Marching Resource Connections ---
    // Camera node connections
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  cameraNode, CameraNodeConfig::VULKAN_DEVICE_IN)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  cameraNode, CameraNodeConfig::SWAPCHAIN_PUBLIC)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX,
                  cameraNode, CameraNodeConfig::IMAGE_INDEX)
         .Connect(inputNode, InputNodeConfig::INPUT_STATE,
                  cameraNode, CameraNodeConfig::INPUT_STATE);

    // Selection (SEL-P2) — providers are NODES. The voxel provider node copies the crosshair texel of
    // PickIdTargetNode's ID image (binding-9 target) via a one-shot fenced copy on a left-click edge,
    // decodes brick/voxel, and emits a SelectionCandidate. Its inputs: per-frame InputState; the ID
    // VkImage; device + command pool for the one-shot copy; the frame-in-flight index; the RENDER
    // viewport size (M4 — matches the pick-ID image's own extent, for the center offset).
    batch.Connect(inputNode, InputNodeConfig::INPUT_STATE,
                  voxelSelectionProviderNode, VoxelSelectionProviderNodeConfig::INPUT_STATE)
         .Connect(pickIdTargetNode, PickIdTargetNodeConfig::ID_IMAGE,
                  voxelSelectionProviderNode, VoxelSelectionProviderNodeConfig::ID_IMAGE)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  voxelSelectionProviderNode, VoxelSelectionProviderNodeConfig::VULKAN_DEVICE)
         .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL,
                  voxelSelectionProviderNode, VoxelSelectionProviderNodeConfig::COMMAND_POOL)
         .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  voxelSelectionProviderNode, VoxelSelectionProviderNodeConfig::CURRENT_FRAME_INDEX)
         // M4.4: the crosshair readback samples PickIdTargetNode's ring, which now follows the
         // RENDER extent (not the window) — VIEWPORT_WIDTH/HEIGHT must be the same extent so
         // width/2,height/2 lands on the actual image center. Was windowNode::WIDTH_OUT/HEIGHT_OUT.
         .Connect(renderTargetNode, RenderTargetNodeConfig::WIDTH_OUT,
                  voxelSelectionProviderNode, VoxelSelectionProviderNodeConfig::VIEWPORT_WIDTH)
         .Connect(renderTargetNode, RenderTargetNodeConfig::HEIGHT_OUT,
                  voxelSelectionProviderNode, VoxelSelectionProviderNodeConfig::VIEWPORT_HEIGHT);

    // SEL-P3 UI provider: only needs per-frame InputState (cursor position + left button). It reads
    // the HUD's Rml::Context via the UIRenderNode reference wired above (SetUiRenderNode), not a slot.
    batch.Connect(inputNode, InputNodeConfig::INPUT_STATE,
                  uiSelectionProviderNode, UISelectionProviderNodeConfig::INPUT_STATE);

    // The coordinator GATHERS every provider's CANDIDATE into its PROVIDER_CANDIDATES accumulation
    // slot, priority-resolves (pickBestCandidate over the gathered vector), applies the input modifier
    // to the durable SelectionSet, and broadcasts a SelectionChangedEvent. It also reads InputState
    // for the click edge. Both providers MultiConnect into the gather slot via the accumulation-connect
    // path (ConnectionMeta{}.With<AccumulationSortConfig>(key)); the sort key only orders the gathered
    // vector — the WINNER is decided by candidate priority (UI=10 occludes voxel world=0). Adding
    // another provider is one more MultiConnect here, with no coordinator change.
    batch.Connect(inputNode, InputNodeConfig::INPUT_STATE,
                  selectionCoordinatorNode, SelectionCoordinatorNodeConfig::INPUT_STATE)
         .Connect(voxelSelectionProviderNode, VoxelSelectionProviderNodeConfig::CANDIDATE,
                  selectionCoordinatorNode, SelectionCoordinatorNodeConfig::PROVIDER_CANDIDATES,
                  ConnectionMeta{}.With<AccumulationSortConfig>(0))
         .Connect(uiSelectionProviderNode, UISelectionProviderNodeConfig::CANDIDATE,
                  selectionCoordinatorNode, SelectionCoordinatorNodeConfig::PROVIDER_CANDIDATES,
                  ConnectionMeta{}.With<AccumulationSortConfig>(1));

    // Pick ID target (AR#35 GPU picking P1): allocate the R32_UINT storage-image ring sized to the
    // RENDER extent (M4.4 — was the window; the compute shader now writes the offscreen render
    // target, not the swapchain, so the pick-ID image must match ITS resolution or the shader's
    // per-pixel idOutputImage writes go out of bounds / land at the wrong texel under scale<1),
    // transition it to GENERAL once, and expose the current frame's view for binding 9. Device +
    // command pool drive allocation + the one-shot UNDEFINED->GENERAL transition; the frame index
    // advances the ring each Execute. The ID_IMAGE_VIEW -> descriptorGatherer binding-9 wiring is
    // below, beside the other compute descriptor connections.
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  pickIdTargetNode, PickIdTargetNodeConfig::VULKAN_DEVICE_IN)
         .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL,
                  pickIdTargetNode, PickIdTargetNodeConfig::COMMAND_POOL)
         .Connect(renderTargetNode, RenderTargetNodeConfig::WIDTH_OUT,
                  pickIdTargetNode, PickIdTargetNodeConfig::WIDTH)
         .Connect(renderTargetNode, RenderTargetNodeConfig::HEIGHT_OUT,
                  pickIdTargetNode, PickIdTargetNodeConfig::HEIGHT)
         .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  pickIdTargetNode, PickIdTargetNodeConfig::CURRENT_FRAME_INDEX);

    // Voxel grid node connections (debug-only; no longer the render source — kept for bindings 4 and 8)
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  voxelGridNode, VoxelGridNodeConfig::VULKAN_DEVICE_IN)
         .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL,
                  voxelGridNode, VoxelGridNodeConfig::COMMAND_POOL);

    // M-wire Task 8: body octree scene node connections (the live render source post M-wire).
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::VULKAN_DEVICE_IN)
         .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL,
                  bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::COMMAND_POOL)
         // FR-7 ring fix: supply the per-frame index so ExecuteImpl picks which ring
         // slot to upload instances into (prevents CPU/GPU races on the instance SSBO).
         .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::CURRENT_FRAME_INDEX);

    // Connect push constant fields to push constant gatherer using member extraction
    // CameraNode now outputs a CameraData struct, so we can extract individual fields
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          pushConstantGatherer, VoxelRayMarch::cameraPos::BINDING,  // vec3 cameraPos
                          ExtractField(&CameraData::cameraPos, SlotRole::Execute));  // Mark as Execute-only

    // Note: time field (index 1) NOT connected - will be filled with zero by gatherer
    // This will trigger a warning log but shader will receive valid (zero) value
    // TODO: Connect actual time source when animation is needed

    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          pushConstantGatherer, VoxelRayMarch::cameraDir::BINDING,  // vec3 cameraDir
                          ExtractField(&CameraData::cameraDir, SlotRole::Execute));  // Mark as Execute-only
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          pushConstantGatherer, VoxelRayMarch::fov::BINDING,  // float fov
                          ExtractField(&CameraData::fov, SlotRole::Execute));  // Mark as Execute-only
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          pushConstantGatherer, VoxelRayMarch::cameraUp::BINDING,  // vec3 cameraUp
                          ExtractField(&CameraData::cameraUp, SlotRole::Execute));  // Mark as Execute-only
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          pushConstantGatherer, VoxelRayMarch::aspect::BINDING,  // float aspect
                          ExtractField(&CameraData::aspect, SlotRole::Execute));  // Mark as Execute-only
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          pushConstantGatherer, VoxelRayMarch::cameraRight::BINDING,  // vec3 cameraRight
                          ExtractField(&CameraData::cameraRight, SlotRole::Execute));  // Mark as Execute-only

    // Connect debugMode from InputState to push constant gatherer for debug visualization
    // Press 0-9 keys to switch between visualization modes at runtime
    batch.Connect(inputNode, InputNodeConfig::INPUT_STATE,
                          pushConstantGatherer, VoxelRayMarch::debugMode::BINDING,  // int debugMode
                          ExtractField(&InputState::debugMode, SlotRole::Execute));  // Mark as Execute-only

    // M-wire Task 8: new push constant fields 8, 9, 10 for BodyInstanceRayMarch.comp.
    // Field indices match shader reflection order: cameraPos(0),time(1),cameraDir(2),fov(3),
    // cameraUp(4),aspect(5),cameraRight(6),debugMode(7), raySizeCoef(8),raySizeBias(9),instanceCount(10).
    // raySizeCoef (binding 8): LOD cone-spread constant; 0.0 disables LOD (full-detail traversal).
    // M4: value now comes from RaySizeCoefNode, recomputed live at Compile from the render target's
    // height (Dependency|Execute — Compile-derived but still read into the per-frame push constants,
    // mirroring instanceCount below).
    // Tiered-ESVO Inc2 M4 Task 9: when VIXEN_TIER_CROSSING_LOD_COEF_OVERRIDE is set, feed the
    // demo-only override ConstantNode instead of the live RaySizeCoefNode -- default path (env
    // unset) is the unchanged pre-M4 connection.
    if (tierCrossingLodCoefOverrideActive) {
        batch.Connect(tierCrossingLodCoefOverrideConstant, ConstantNodeConfig::OUTPUT,
                              pushConstantGatherer, 8,  // push constant field 8: float raySizeCoef
                              SlotRoleModifier(SlotRole::Execute));
    } else {
        batch.Connect(raySizeCoefNode, RaySizeCoefNodeConfig::RAY_SIZE_COEF,
                              pushConstantGatherer, 8,  // push constant field 8: float raySizeCoef
                              SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    }
    // raySizeBias (binding 9): LOD origin cone size; 0.0 for pinhole camera.
    batch.Connect(raySizeBiasConstant, ConstantNodeConfig::OUTPUT,
                          pushConstantGatherer, 9,  // push constant field 9: float raySizeBias
                          SlotRoleModifier(SlotRole::Execute));
    // instanceCount (binding 10): number of valid entries in bodyInstances[]; from BodyOctreeSceneNode.
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::INSTANCE_COUNT,
                          pushConstantGatherer, 10,  // push constant field 10: int instanceCount
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    // debugTargetPixel (binding 11): TEMP DEBUG — last left-click pixel, so the ray-trace debug
    // buffer (TraceRecording.glsl) force-captures that exact ray regardless of DEBUG_GRID_SPACING.
    batch.Connect(inputNode, InputNodeConfig::INPUT_STATE,
                          pushConstantGatherer, 11,  // push constant field 11: ivec2 debugTargetPixel
                          ExtractField(&InputState::lastClickPixel, SlotRole::Execute));

    // Connect ray marching resources to descriptor gatherer using VoxelRayMarchNames.h bindings
    // Binding 0: outputImage - Transient (Execute-only), others are Persistent (Dependency|Execute)
    // Binding 0: outputImage — M4: now the offscreen render target's view, wired further down
    // (beside the rest of the M4 render-target connections) once renderTargetNode exists in scope.
    // Note: outputImage is not in SDI (writeonly image) so we use literal binding index 0

    // M-wire Task 8: bindings 1/2/3/5 now come from BodyOctreeSceneNode (sparse shell octrees).
    // Slot names are identical to VoxelGridNode's octree outputs (by design in BodyOctreeSceneNodeConfig).
    // The shader's esvoNodes/brickData/materials/OctreeConfigsUBO at these bindings are now the
    // concatenated per-kind shell octrees, NOT the dense 128^3 grid.

    // Binding 1: esvoNodes (SSBO) - concatenated shell octree node descriptors for <= 3 kinds
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::OCTREE_NODES_BUFFER,
                          descriptorGatherer, VoxelRayMarch::esvoNodes::BINDING,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    // Binding 2: brickData (SSBO) - concatenated brick voxel data
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::OCTREE_BRICKS_BUFFER,
                          descriptorGatherer, VoxelRayMarch::brickData::BINDING,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    // Binding 3: materials (SSBO) - material palette
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::OCTREE_MATERIALS_BUFFER,
                          descriptorGatherer, VoxelRayMarch::materials::BINDING,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    // Binding 4: RayTraceBuffer (debug capture) — still from voxelGridNode (it has the buffer;
    // BodyOctreeSceneNode has no debug capture). VoxelGridNode stays in graph for this purpose.
    batch.Connect(voxelGridNode, VoxelGridNodeConfig::DEBUG_CAPTURE_BUFFER,
                          descriptorGatherer, VoxelRayMarch::traceWriteIndex::BINDING,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute | SlotRole::Debug));

    // Binding 5: OctreeConfigsSSBO (std430, N x 432 B) — runtime-sized per-octree config (I3.2).
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::OCTREE_CONFIG_BUFFER,
                          descriptorGatherer, 5,  // Binding 5 (hardcoded; no SDI regen yet)
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    // Binding 9: idOutputImage (R32_UINT storage image) - AR#35 GPU picking P1. The compute shader
    // writes the per-pixel pick ID here. Execute-only, exactly like the swapchain output at binding 0:
    // PickIdTargetNode re-emits the current ring image's view each frame and the gatherer refreshes it.
    // The shader reflects binding 9 as a STORAGE_IMAGE; DescriptorSetNode writes it with layout GENERAL.
    batch.Connect(pickIdTargetNode, PickIdTargetNodeConfig::ID_IMAGE_VIEW,
                          descriptorGatherer, 9,  // Binding 9: idOutputImage
                          SlotRoleModifier(SlotRole::Execute));

    // Binding 8: ShaderCounters is compiled out of BodyInstanceRayMarch.comp unconditionally
    // (see shader builder above), so binding 8 no longer exists in the reflected SPIR-V —
    // wiring a descriptor for a binding the shader doesn't declare is itself a validation
    // error, so this Connect() is deliberately removed, not just disabled.
    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] Connected debug: binding 4 (voxelGridNode debug capture); shader counters (binding 8) compiled out");
    }

    // Binding 10: BodyInstanceBuffer (SSBO) — per-body BodyInstanceGpu records (64 B each).
    // M-wire Task 8: this is the NEW binding not present in the dense path.
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::INSTANCE_BUFFER,
                          descriptorGatherer, 10,  // Binding 10: BodyInstanceBuffer
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] Connected body instance SSBO at binding 10 (BodyOctreeSceneNode)");
    }

    // Binding 11/12: Surface-Shell ESVO cache (the bandwidth win). The render now
    // reads the COMPACT shell pool (SHELL_DATA_BUFFER) + grid->shellSlot remap
    // (SHELL_LOOKUP_BUFFER) instead of the full-interior OCTREE_SDF_BUFFER /
    // OCTREE_BRICKLOOKUP_BUFFER. This is a DROP-IN swap: DeriveShell builds the
    // remap so the shader's existing addressing (brickIdx = brickLookup[flat];
    // channelPool[poolBrickBase + brickIdx*stride + ...]) reads the compact pool
    // with NO shader-logic change. The full-interior buffers stay live as the
    // ShellRevalidate compute pass's SOURCE (bindings on that node), never bound
    // to the render. BodyOctreeSceneNode re-emits SHELL_DATA/SHELL_LOOKUP each
    // frame as the current double-buffer read slot [frame&1].
    // (Placeholder 1-byte for binary/Procedural bodies — shader only reads these
    //  when OctreeConfig.formatId == FORMAT_STORED_SDF.)
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::SHELL_DATA_BUFFER,
                          descriptorGatherer, 11,  // Binding 11: compact shell pool
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::SHELL_LOOKUP_BUFFER,
                          descriptorGatherer, 12,  // Binding 12: grid->shellSlot remap
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    // Sparse-Mip ESVO LOD Inc1 M3: Binding 13: mip pool SSBO (packed {value,coverage}
    // floats, one per node/channel). Placeholder for a tree that was never mip-baked;
    // read by the shader's leaf-existence (Task 7) and LOD-cutoff (Task 8) fallbacks.
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::OCTREE_MIPPOOL_BUFFER,
                          descriptorGatherer, 13,  // Binding 13: MipPoolBuffer
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] Connected SoA-SDF buffer at binding 11, brick-grid lookup at binding 12, mip pool at binding 13 (Inc1 M3)");
    }

    // Tiered-ESVO Inc2 M3: Binding 15: tier-ref table SSBO (TierRef records, one
    // per registered tier-crossing leaf). Placeholder for a scene with no
    // tier-crossing leaves; read by the shader's traversal-restart (Task 6/7)
    // when a farBit==1 leaf is hit. (Binding 14 is InstanceIterDebugBuffer.)
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::OCTREE_TIERREFTABLE_BUFFER,
                          descriptorGatherer, 15,  // Binding 15: TierRefTableBuffer
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] Connected tier-ref table at binding 15 (Tiered-ESVO Inc2 M3)");
    }

    // Swapchain connections to descriptor set and dispatch
    // Pass swapchain public vars; DescriptorSetNode reads swapChainImageCount during Compile.
    // DESCRIPTOR_RESOURCES provides the actual bindings.
    batch.Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  computeDescriptorSet, DescriptorSetNodeConfig::SWAPCHAIN_INFO)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX,
                  computeDescriptorSet, DescriptorSetNodeConfig::IMAGE_INDEX)
         // REMOVED DUPLICATE: descriptorGatherer -> computeDescriptorSet DESCRIPTOR_RESOURCES (already connected at line 919-920)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  computeDispatch, ComputeDispatchNodeConfig::SWAPCHAIN_INFO)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX,
                  computeDispatch, ComputeDispatchNodeConfig::IMAGE_INDEX);

    // M4: render-scale decoupling. The offscreen render target follows the swapchain's extent
    // (EXTENT_SOURCE), scaled by PARAM_SCALE (set above from VIXEN_RENDER_SCALE); it rides the
    // standard resize->recompile cascade — no per-frame extent checks anywhere in this wiring.
    // ComputeDispatchNode's RENDER_TARGET_INFO input makes it dispatch into and blit from this
    // target instead of writing the swapchain image directly.
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  renderTargetNode, RenderTargetNodeConfig::VULKAN_DEVICE_IN)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  renderTargetNode, RenderTargetNodeConfig::EXTENT_SOURCE)
         .Connect(renderTargetNode, RenderTargetNodeConfig::RENDER_TARGET,
                  computeDispatch, ComputeDispatchNodeConfig::RENDER_TARGET_INFO,
                  SlotRoleModifier(SlotRole::Execute));

    // M4: the compute shader's output image (binding 0) is now the offscreen render target's
    // current view, not the swapchain's — the swapchain is written only by the blit inside
    // ComputeDispatchNode. Was: swapChainNode::CURRENT_FRAME_IMAGE_VIEW -> descriptorGatherer 0.
    batch.Connect(renderTargetNode, RenderTargetNodeConfig::CURRENT_VIEW,
                          descriptorGatherer, 0,  // outputImage at binding 0
                          SlotRoleModifier(SlotRole::Execute));

    // M4.3: raySizeCoef derives from the render target's live height (rank 6) — rides the same
    // resize->recompile cascade as the render target itself.
    batch.Connect(renderTargetNode, RenderTargetNodeConfig::HEIGHT_OUT,
                  raySizeCoefNode, RaySizeCoefNodeConfig::HEIGHT);

    // Sync connections
    batch.Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  computeDispatch, ComputeDispatchNodeConfig::CURRENT_FRAME_INDEX)
         .Connect(frameSyncNode, FrameSyncNodeConfig::IN_FLIGHT_FENCE,
                  computeDispatch, ComputeDispatchNodeConfig::IN_FLIGHT_FENCE)
         .Connect(frameSyncNode, FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY,
                  computeDispatch, ComputeDispatchNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY)
         .Connect(swapChainNode, SwapChainNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY,
                  computeDispatch, ComputeDispatchNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY)
         // P5b M1: wire FrameSyncNode timeline primitives into ComputeDispatchNode
         .Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_SEMAPHORE,
                  computeDispatch, ComputeDispatchNodeConfig::TIMELINE_SEMAPHORE_IN)
         .Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_FRAME_BASE,
                  computeDispatch, ComputeDispatchNodeConfig::TIMELINE_FRAME_BASE_IN);

    // REMOVED DUPLICATE: computeDispatch -> present RENDER_COMPLETE_SEMAPHORE (already connected at line 894-895)

    // ===================================================================
    // Sky-projection composite pass (Tiered ESVO Inc1 M3): address-derived sky points over the
    // compute output, BEFORE the UI composite pass (compute -> sky-projection -> UI).
    // Mirrors the UI composite triple's own RenderPassNode -> FramebufferNode -> consumer shape,
    // one stage earlier in the chain.
    // ===================================================================

    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, skyProjectionRenderPassNode, RenderPassNodeConfig::VULKAN_DEVICE_IN)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC, skyProjectionRenderPassNode, RenderPassNodeConfig::SWAPCHAIN_INFO);

    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, skyProjectionFramebufferNode, FramebufferNodeConfig::VULKAN_DEVICE_IN)
         .Connect(skyProjectionRenderPassNode, RenderPassNodeConfig::RENDER_PASS, skyProjectionFramebufferNode, FramebufferNodeConfig::RENDER_PASS)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC, skyProjectionFramebufferNode, FramebufferNodeConfig::SWAPCHAIN_INFO);

    // SkyProjectionNode DATA-role inputs (device/cmdpool — mirrors BodyOctreeSceneNode's exact
    // connection block) + DRAW-role inputs (swapchain-info/camera-data/render-pass/framebuffers/
    // image-index/frame-index/fence/timeline — mirrors UIRenderNode's composite-mode wiring).
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, skyProjectionNode, SkyProjectionNodeConfig::VULKAN_DEVICE_IN)
         .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL, skyProjectionNode, SkyProjectionNodeConfig::COMMAND_POOL)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC, skyProjectionNode, SkyProjectionNodeConfig::SWAPCHAIN_INFO)
         .Connect(cameraNode, CameraNodeConfig::CAMERA_DATA, skyProjectionNode, SkyProjectionNodeConfig::CAMERA_DATA)
         .Connect(skyProjectionRenderPassNode, RenderPassNodeConfig::RENDER_PASS, skyProjectionNode, SkyProjectionNodeConfig::RENDER_PASS)
         .Connect(skyProjectionFramebufferNode, FramebufferNodeConfig::FRAMEBUFFERS, skyProjectionNode, SkyProjectionNodeConfig::FRAMEBUFFERS)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX, skyProjectionNode, SkyProjectionNodeConfig::IMAGE_INDEX)
         .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX, skyProjectionNode, SkyProjectionNodeConfig::CURRENT_FRAME_INDEX)
         .Connect(frameSyncNode, FrameSyncNodeConfig::IN_FLIGHT_FENCE, skyProjectionNode, SkyProjectionNodeConfig::IN_FLIGHT_FENCE)
         .Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_SEMAPHORE, skyProjectionNode, SkyProjectionNodeConfig::TIMELINE_SEMAPHORE_IN)
         .Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_FRAME_BASE, skyProjectionNode, SkyProjectionNodeConfig::TIMELINE_FRAME_BASE_IN);
    // IMAGE_AVAILABLE_SEMAPHORES_ARRAY deliberately left UNCONNECTED: this pass is never the
    // first submit in the live composite pipeline (the upstream compute already waits the WSI
    // acquire), so ordering vs. compute is carried solely by the timeline waitEdge above — see
    // SkyProjectionNodeConfig.h's doc comment and SkyProjectionNode::ExecuteImpl's empty-vector
    // guard (mirrors UIRenderNode's composite_ convention exactly).

    // ===================================================================
    // UI composite pass: HUD render pass over the compute output, before present.
    // Mirrors BuildUIGraph's RenderPassNode → FramebufferNode → UIRenderNode shape, but the render pass
    // LOADs (initial=General, from the compute) instead of clearing, and the UI node runs in composite
    // mode (waits on the compute→UI handoff, signals its own present semaphore, owns the frame fence).
    // ===================================================================

    // UI render pass: device + swapchain format. (Color-only; no depth → LOAD/initial=General set above.)
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, uiRenderPassNode, RenderPassNodeConfig::VULKAN_DEVICE_IN)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC, uiRenderPassNode, RenderPassNodeConfig::SWAPCHAIN_INFO);

    // UI framebuffers: wrap each swapchain image view against the UI render pass.
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, uiFramebufferNode, FramebufferNodeConfig::VULKAN_DEVICE_IN)
         .Connect(uiRenderPassNode, RenderPassNodeConfig::RENDER_PASS, uiFramebufferNode, FramebufferNodeConfig::RENDER_PASS)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC, uiFramebufferNode, FramebufferNodeConfig::SWAPCHAIN_INFO);

    // UIRenderNode (composite) inputs — mirrors BuildUIGraph's UIRenderNode wiring.
    batch.Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC, uiCompositeNode, UIRenderNodeConfig::SWAPCHAIN_INFO)
         .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL, uiCompositeNode, UIRenderNodeConfig::COMMAND_POOL)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, uiCompositeNode, UIRenderNodeConfig::VULKAN_DEVICE)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX, uiCompositeNode, UIRenderNodeConfig::IMAGE_INDEX)
         .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX, uiCompositeNode, UIRenderNodeConfig::CURRENT_FRAME_INDEX)
         .Connect(frameSyncNode, FrameSyncNodeConfig::IN_FLIGHT_FENCE, uiCompositeNode, UIRenderNodeConfig::IN_FLIGHT_FENCE)
         .Connect(frameSyncNode, FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY, uiCompositeNode, UIRenderNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY)
         .Connect(swapChainNode, SwapChainNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY, uiCompositeNode, UIRenderNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY)
         .Connect(uiRenderPassNode, RenderPassNodeConfig::RENDER_PASS, uiCompositeNode, UIRenderNodeConfig::RENDER_PASS)
         .Connect(uiFramebufferNode, FramebufferNodeConfig::FRAMEBUFFERS, uiCompositeNode, UIRenderNodeConfig::FRAMEBUFFERS)
         // P5b M1: wire FrameSyncNode timeline primitives into UIRenderNode (consumer waits on edges)
         .Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_SEMAPHORE, uiCompositeNode, UIRenderNodeConfig::TIMELINE_SEMAPHORE_IN)
         .Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_FRAME_BASE, uiCompositeNode, UIRenderNodeConfig::TIMELINE_FRAME_BASE_IN);

    // P5b M3 (extended for Tiered ESVO Inc1 M3): the compute→sky-projection→UI ordering is carried
    // by the baked timeline edges for GPU SYNC (memory visibility), but the graph still needs the
    // TOPOLOGY edges so the execution order (and hence the timeline edges the scheduler bakes from
    // them) is compute-before-sky-projection-before-UI. The FrameSyncScheduler derives edge
    // DIRECTION from groupId order (== execution order); without these dependencies the topological
    // sort could place UI/sky-projection before compute, baking the edges BACKWARDS, tagging the
    // wrong group as the swapchain present-signal, and leaving the presented image in the wrong
    // layout at the wrong point — VUID-...-01430-class bugs, VUID-vkCmdDraw-None-09600-class bugs.
    // So we keep these two connections purely as ORDERING edges (their documented secondary
    // purpose, mirroring UIRenderNodeConfig's own SWAPCHAIN/COMPOSITE_WAIT convention exactly): the
    // binary semaphores they carry are INERT — compute no longer SIGNALS renderComplete in
    // composite (ComputeDispatchNode gates it to !leaveImageInGeneral), SkyProjectionNode never
    // WAITS its own COMPOSITE_WAIT_SEMAPHORE input, and UIRenderNode no longer WAITS compositeWait
    // either (the M3 binary handoff was dropped from its submit). With the edges in the right
    // direction the scheduler bakes compute(GENERAL)→sky-projection(GENERAL)→UI(GENERAL) timeline
    // edges (each consumer gets a waitEdge on its producer's timeline value; every layout stays
    // GENERAL end-to-end ⇒ no transitions anywhere in this 3-pass chain), tags the UI group as
    // present (its render pass owns GENERAL→PRESENT_SRC, unchanged), and the timeline alone — not a
    // binary handoff — orders all three passes. WSI acquire (compute waits imageAvailable) and
    // present (UI signals its uiComplete) stay binary, exactly as before this milestone's change.
    batch.Connect(computeDispatch, ComputeDispatchNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                  skyProjectionNode, SkyProjectionNodeConfig::COMPOSITE_WAIT_SEMAPHORE);
    batch.Connect(skyProjectionNode, SkyProjectionNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                  uiCompositeNode, UIRenderNodeConfig::COMPOSITE_WAIT_SEMAPHORE);

    // Atomically register all connections
    size_t connectionCount = batch.GetConnectionCount();
    mainLogger->Info("Registering " + std::to_string(connectionCount) + " connections...");
    batch.RegisterAll();

    mainLogger->Info("Successfully wired " + std::to_string(connectionCount) + " connections");

    mainLogger->Info("Complete render pipeline built with " + std::to_string(renderGraph->GetNodeCount()) + " nodes");
}
