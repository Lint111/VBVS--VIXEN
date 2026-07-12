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
#include <cmath>    // std::tan for the LOD ray-cone (raySizeCoef) computation
#include <cstdlib>  // std::strtof for the VIXEN_RENDER_SCALE env parse (M4)
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
#include "Data/Nodes/BlitNodeConfig.h"  // Sampled Lighting Inc3 M1: presentation-only blit (post-DirectLighting)
#include "Data/Nodes/ComputeDispatchNodeConfig.h"
#include "Data/Nodes/ComputePipelineNodeConfig.h"
#include "Data/Nodes/ComputeStageNodeConfig.h"  // Sampled Lighting Inc3 M1: DirectLightingNode
#include "Data/Nodes/BufferSyncGathererNodeConfig.h"  // Sampled Lighting Inc3 M5: array-hazard buffer gatherer
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
#include "Data/Nodes/ShadowConfigNodeConfig.h"  // Sampled Lighting Inc1 M4: ShadowConfig upload ring
#include "Data/Nodes/AccumulationConfigNodeConfig.h"   // Sampled Lighting Inc2 M1: AccumulationConfig upload ring
#include "Data/Nodes/AccumulationHistoryNodeConfig.h"  // Sampled Lighting Inc2 M1: persistent history image
#include "Data/Nodes/WorldPosHistoryNodeConfig.h"      // Sampled Lighting Inc3 M2: worldPos/depth companion history image (KI-023)
#include "Data/Nodes/PrevCameraConfigNodeConfig.h"     // Sampled Lighting Inc2 M3: prev-frame camera matrix upload ring
#include "Data/Nodes/ReservoirConfigNodeConfig.h"      // Sampled Lighting Inc3 M3: ReservoirConfig upload ring (M4/M5 scaffolding)
#include "Data/Nodes/LightTreeBufferNodeConfig.h"      // Sampled Lighting Inc3 M4: mip-cut light-tree upload ring
#include "Data/Nodes/StorageBufferNodeConfig.h"        // Sampled Lighting Inc3 M4: reservoir CURRENT/PREVIOUS ping-pong SSBOs
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
#include "SdfBake.h"    // Sampled Lighting Inc3 M4: BakeRecipeToSdfWorldWithEmission for the ReSTIR gate demo scene
#include "LightTree.h"  // Sampled Lighting Inc3 M4: BuildLightTreeCut/BruteForceTotalEmissivePower for the ReSTIR gate demo
#include "Nodes/BlitNode.h"  // Sampled Lighting Inc3 M1: presentation-only blit (post-DirectLighting)
#include "Nodes/BodyOctreeSceneNode.h"  // M-wire: sparse shell octree + instance SSBO
#include "Nodes/CameraNode.h"
#include "Nodes/CommandPoolNode.h"
#include "Nodes/ComputeDispatchNode.h"
#include "Nodes/ComputePipelineNode.h"
#include "Nodes/ComputeStageNode.h"  // Sampled Lighting Inc3 M1: DirectLightingNode
#include "Nodes/BufferSyncGathererNode.h"  // Sampled Lighting Inc3 M5: array-hazard buffer gatherer
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
#include "Nodes/LightingConfigNode.h"  // Sampled Lighting Inc0 M3: LightingConfig upload ring
#include "Nodes/ShadowConfigNode.h"    // Sampled Lighting Inc1 M4: ShadowConfig upload ring
#include "Nodes/AccumulationConfigNode.h"   // Sampled Lighting Inc2 M1: AccumulationConfig upload ring
#include "Nodes/AccumulationHistoryNode.h"  // Sampled Lighting Inc2 M1: persistent history image
#include "Nodes/WorldPosHistoryNode.h"      // Sampled Lighting Inc3 M2: worldPos/depth companion history image (KI-023)
#include "Nodes/PrevCameraConfigNode.h"     // Sampled Lighting Inc2 M3: prev-frame camera matrix upload ring
#include "Nodes/ReservoirConfigNode.h"      // Sampled Lighting Inc3 M3: ReservoirConfig upload ring (M4/M5 scaffolding)
#include "Nodes/LightTreeBufferNode.h"      // Sampled Lighting Inc3 M4: mip-cut light-tree upload ring
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
#include "Nodes/StorageBufferNode.h"  // Sampled Lighting Inc1 M3: HitRecord SSBO (binding 17), extent-driven
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

    // Sampled Lighting Inc0 M3: LightingConfig data (binding 16). Static default content
    // this increment (a single directional light matching Lighting.glsl's old hardcoded
    // default) uploaded per-frame through a PerFrameResources ring, mirroring
    // DynamicInstanceBufferNode's pattern.
    NodeHandle lightingConfigNode = renderGraph->AddNode<LightingConfigNodeType>("lighting_config");

    // Sampled Lighting Inc1 M4: ShadowConfig data (binding 18). Same per-frame ring upload
    // pattern as lightingConfigNode above — separate node (see ShadowConfigNode.h's file
    // header for the separate-vs-extend decision).
    NodeHandle shadowConfigNode = renderGraph->AddNode<ShadowConfigNodeType>("shadow_config");

    // Sampled Lighting Inc1 M3: HitRecord SSBO (binding 17) — one HitRecord (64 B, see
    // shaders/HitRecord.glsl) per pixel of the offscreen render target. Reuses the generic
    // StorageBufferNode (auto-sync P4 M4) rather than a bespoke node: this milestone's whole
    // scope is proving the pack/write/read/unpack round-trips losslessly THROUGH a real SSBO
    // inside BodyInstanceRayMarch.comp's own dispatch (no separate pass yet — that is Task 4's
    // DirectLightingNode). SWAPCHAIN_INFO is wired below to renderTargetNode's RENDER_TARGET
    // (not the raw swapchain) so this buffer's extent always matches outputImage's actual
    // imgSize (imageSize(outputImage) in the shader) even under render-scale (<1.0) — the same
    // extent-follow cascade RenderTargetNode itself rides.
    NodeHandle hitRecordBufferNode = renderGraph->AddNode<StorageBufferNodeType>("hit_record_buffer");

    // Sampled Lighting Inc3 M1 (KI-018): DirectLightingNode — the shading pass split out of
    // BodyInstanceRayMarch.comp (which is now traversal-only: writes HitRecord + the pick-ID
    // image, no longer outputImage). Own ComputeStageNode submit (its OWN vkQueueSubmit2 /
    // SubmitGroup, separate from the march's ComputeDispatchNode submit), reading HitRecord
    // (binding 17) and writing outputImage (binding 0) via renderTargetNode's RENDER_TARGET
    // through the generic IMAGE_WRITE sync slot. Needs its own shaderLib/gatherer/pushConstant-
    // gatherer/descSet/pipeline quintet — DescriptorResourceGathererNode and
    // PushConstantGathererNode reflect from THEIR wired SHADER_DATA_BUNDLE, so a second compiled
    // shader (different push-constant field usage after dead-code elimination) needs its own
    // instances, mirroring BuildFanInDemoGraph's per-stage wirePipeline/wireStageCommon shape.
    NodeHandle directLightingShaderLib = renderGraph->AddNode<ShaderLibraryNodeType>("direct_lighting_shader_lib");
    NodeHandle directLightingGatherer  = renderGraph->AddNode<DescriptorResourceGathererNodeType>("direct_lighting_desc_gatherer");
    NodeHandle directLightingPushConstantGatherer = renderGraph->AddNode<PushConstantGathererNodeType>("direct_lighting_push_constant_gatherer");
    NodeHandle directLightingDescriptorSet = renderGraph->AddNode<DescriptorSetNodeType>("direct_lighting_descriptors");
    NodeHandle directLightingPipeline = renderGraph->AddNode<ComputePipelineNodeType>("direct_lighting_pipeline");
    NodeHandle directLightingNode = renderGraph->AddNode<ComputeStageNodeType>("direct_lighting");

    // Sampled Lighting Inc3 M5: array-hazard buffer-sync gatherer for DirectLightingNode's
    // HitRecord read (generalized from the old fixed BUFFER_READ_A slot — see
    // ComputeStageNodeConfig.h's own class doc). One entry: HitRecord.
    NodeHandle directLightingReadGatherer = renderGraph->AddNode<BufferSyncGathererNodeType>("direct_lighting_read_gatherer");

    // Sampled Lighting Inc3 M1: presentation-only blit of the offscreen render target to the
    // swapchain (extracted from ComputeDispatchNode's M4 render-target blit — same
    // SwapchainBarriers::BlitRenderTargetToSwapchain free function, now shared). Runs after
    // DirectLightingNode, before the sky-projection/UI composite chain (which still reads the
    // swapchain image, unchanged).
    NodeHandle blitNode = renderGraph->AddNode<BlitNodeType>("render_target_blit");

    // Sampled Lighting Inc2 M1: AccumulationConfig data (binding 19). Same per-frame ring
    // upload pattern as shadowConfigNode above — separate node (see AccumulationConfigNode.h's
    // file header for the separate-vs-extend decision). Default content: enabled=0, so this
    // milestone's render is a byte-identical passthrough vs Inc1.
    NodeHandle accumulationConfigNode = renderGraph->AddNode<AccumulationConfigNodeType>("accumulation_config");

    // Sampled Lighting Inc2 M1: persistent temporal-accumulation history image (binding 20) — a
    // SINGLE persistent storage image (NOT a per-frame ring; see AccumulationHistoryNode.h's file
    // header for why). Allocated + transitioned + wired this milestone; not yet read/written by
    // the shader (M2 consumes it).
    NodeHandle accumulationHistoryNode = renderGraph->AddNode<AccumulationHistoryNodeType>("accumulation_history");

    // Sampled Lighting Inc3 M2 (KI-023): persistent worldPos/depth companion history image
    // (binding 22) — mirrors accumulationHistoryNode above (single persistent storage image,
    // NOT a ring; see WorldPosHistoryNode.h's file header). Written by DirectLighting.comp
    // alongside historyImage; read back at the reprojected texel to validate reprojection
    // GEOMETRICALLY instead of by color-consistency (the KI-023 fix). Also serves Inc3's own
    // ReSTIR reservoir-reprojection validity (M4/M5) — one buffer, two future consumers.
    NodeHandle worldPosHistoryNode = renderGraph->AddNode<WorldPosHistoryNodeType>("worldpos_history");

    // Sampled Lighting Inc2 M3: prev-frame camera matrix data (binding 21). Same per-frame
    // ring upload pattern as accumulationConfigNode above — separate node (see
    // PrevCameraConfigNode.h for the separate-vs-extend decision). Uploaded every frame but
    // not yet read by the shader this milestone (M4 consumes it for reprojection); this
    // milestone's render must stay byte-identical to M2.
    NodeHandle prevCameraConfigNode = renderGraph->AddNode<PrevCameraConfigNodeType>("prev_camera_config");

    // Sampled Lighting Inc3 M3: ReservoirConfig data (binding 23). Same per-frame ring
    // upload pattern as shadowConfigNode/prevCameraConfigNode above — separate node (see
    // ReservoirConfigNode.h for the separate-vs-extend decision). M3 scaffolding only:
    // reservoirEnabled=0 by default and nothing reads this buffer yet (M4/M5 wire the
    // reservoir/RIS shading logic that consumes it); this milestone's render must stay
    // byte-identical to M1/M2.
    NodeHandle reservoirConfigNode = renderGraph->AddNode<ReservoirConfigNodeType>("reservoir_config");

    // Sampled Lighting Inc3 M4: mip-cut light-tree upload ring (binding 24) -- RIS candidate
    // generation samples this. Content pushed via LightTreeBufferNode::SetLightTreeCut (host ->
    // node seam, mirrors BodyOctreeSceneNode::SetInstances); empty by default (byte-identity
    // escape hatch -- no cut pushed means nodeCount=0, DirectLighting.comp's RIS loop is a no-op).
    NodeHandle lightTreeBufferNode = renderGraph->AddNode<LightTreeBufferNodeType>("light_tree_buffer");

    // Sampled Lighting Inc3 M4: reservoir CURRENT/PREVIOUS ping-pong SSBOs (bindings 25/26) --
    // one Vixen::Gpu::ReservoirRecord (16B) per pixel of the offscreen render target, same
    // extent-driven StorageBufferNode pattern as hitRecordBufferNode above. TWO separate
    // StorageBufferNode instances (not a single ring) because the ping-pong swap is EXPLICIT
    // per-frame (current becomes next frame's previous) -- see the CPU-side swap below, mirrored
    // by which gatherer binding/sync-slot each is wired to.
    NodeHandle reservoirBufferA = renderGraph->AddNode<StorageBufferNodeType>("reservoir_buffer_a");
    NodeHandle reservoirBufferB = renderGraph->AddNode<StorageBufferNodeType>("reservoir_buffer_b");

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

    // Sampled Lighting Inc1 M3: HitRecord SSBO sized to sizeof(HitRecord) (64 B, see
    // shaders/HitRecord.glsl) bytes per pixel of the offscreen render target it is wired to
    // below (SWAPCHAIN_INFO <- renderTargetNode's RENDER_TARGET), so it always matches
    // outputImage's own extent (including under render-scale).
    auto* hitRecordBuffer = static_cast<StorageBufferNode*>(renderGraph->GetInstance(hitRecordBufferNode));
    hitRecordBuffer->SetParameter(StorageBufferNodeConfig::PARAM_BYTES_PER_PIXEL, 64u);

    // Sampled Lighting Inc3 M4: reservoir ping-pong SSBOs sized to sizeof(Vixen::Gpu::
    // ReservoirRecord) (16 B, see Generated/ReservoirRecord.g.h) bytes per pixel — same
    // extent-follow pattern as hitRecordBuffer above.
    auto* reservoirBufferAInst = static_cast<StorageBufferNode*>(renderGraph->GetInstance(reservoirBufferA));
    reservoirBufferAInst->SetParameter(StorageBufferNodeConfig::PARAM_BYTES_PER_PIXEL, 16u);
    auto* reservoirBufferBInst = static_cast<StorageBufferNode*>(renderGraph->GetInstance(reservoirBufferB));
    reservoirBufferBInst->SetParameter(StorageBufferNodeConfig::PARAM_BYTES_PER_PIXEL, 16u);

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

        builder.SetProgramName(programName)
               .SetPipelineType(ShaderManagement::PipelineTypeConstraint::Compute)
               .SetTargetVulkanVersion(vulkanVer)
               .SetTargetSpirvVersion(spirvVer)
               .AddIncludePath("shaders")
               .AddIncludePath("../shaders")
#ifdef VIXEN_SHADER_SOURCE_DIR
               .AddIncludePath(VIXEN_SHADER_SOURCE_DIR)
#endif
               .AddStageFromFile(ShaderManagement::ShaderStage::Compute, compPath, "main");

        // Shader counters (perf sweep rank 2) are compiled OUT unconditionally: the live
        // app has no consumer for them, and every pixel was paying 3-4 unread atomic RMWs
        // into a HOST_COHERENT SSBO. No env opt-in — ShaderBundleBuilder::SetStageDefines
        // does line-level token substitution, not textual #define injection, so it cannot
        // drive ShaderCounters.glsl's #ifdef ENABLE_SHADER_COUNTERS guard (verified: passing
        // an empty-value define here turns "#ifdef ENABLE_SHADER_COUNTERS" into "#ifdef ",
        // a glslang compile error). Re-enable by hand-editing this .comp's #define if needed.

        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Info("[BuildRenderGraph] Using BodyInstanceRayMarch shader: " + compPath.string());
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

    // Sampled Lighting Inc3 M1 (KI-018): DirectLighting.comp shader registration. Same shader-source
    // search-path pattern as BodyInstanceRayMarch.comp above; includes shaders/SceneBindings.glsl
    // (shared scene/traversal declarations) so both compiled programs stay byte-identical on the
    // shared portion.
    auto* directLightingShaderLibNode = static_cast<ShaderLibraryNode*>(renderGraph->GetInstance(directLightingShaderLib));
    directLightingShaderLibNode->RegisterShaderBuilder([this](int vulkanVer, int spirvVer) {
        ShaderManagement::ShaderBundleBuilder builder;
        constexpr const char* shaderName = "DirectLighting.comp";
        constexpr const char* programName = "DirectLighting";
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
            if (std::filesystem::exists(path)) { compPath = path; break; }
        }
        if (compPath.empty()) {
            throw std::runtime_error(std::string(shaderName) + " not found - check shader search paths");
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
               .AddStageFromFile(ShaderManagement::ShaderStage::Compute, compPath, "main");
        return builder;
    });

    // DirectLightingNode: NOT swapchain-adjacent (isConsumer=false — no WSI, no fence, no
    // PRESENT_SRC). Dispatch dims left at 0/0 so RecordComputeCommands derives them LIVE from
    // IMAGE_WRITE's (renderTargetNode's) extent every Execute — the same live-derivation M4
    // relies on for VIXEN_RENDER_SCALE, now mirrored on the shading pass.
    auto* directLighting = static_cast<ComputeStageNode*>(renderGraph->GetInstance(directLightingNode));
    directLighting->SetParameter(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);
    directLighting->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_X, 0u);
    directLighting->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Y, 0u);
    directLighting->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Z, 1u);

    // Sampled Lighting Inc3 M5: pre-register the HitRecord read-gatherer's single slot
    // (fixed count, no shader reflection needed).
    static_cast<BufferSyncGathererNode*>(renderGraph->GetInstance(directLightingReadGatherer))->PreRegisterBufferSlots(1);

    // BlitNode: mirrors ComputeDispatchNode's own M4 PARAM_LEAVE_IMAGE_IN_GENERAL=true (set
    // below beside uiComposite's own parameters) — the sky-projection/UI composite chain still
    // owns the final GENERAL->PRESENT_SRC transition, unchanged by this milestone's pass-split.
    auto* blit = static_cast<BlitNode*>(renderGraph->GetInstance(blitNode));
    blit->SetParameter(BlitNodeConfig::PARAM_LEAVE_IMAGE_IN_GENERAL, true);

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
    // Tiered-ESVO Inc3 M4: the SAME orbitCenter gotcha applies to the Earth-scale zoom demo
    // (VIXEN_TIER_EARTH_ZOOM_DEMO's own body is built at the IDENTICAL world center
    // (64,64,64) as VIXEN_TIER_CROSSING_DEMO/VIXEN_TIER_CHAIN_DEMO/VIXEN_TIER_ZOOM_DEMO) --
    // this demo's own scripted SetOrbitDistanceForTest call would otherwise orbit the stale
    // Cornell-box default (5,5,5), producing a distant/empty-looking capture regardless of
    // orbitDistance (caught live: this milestone's first capture pass showed a tiny distant
    // dot at EVERY tick, near and far alike, until this was added -- the exact class of bug
    // M5's own comment above already documents and warns about).
    if (std::getenv("VIXEN_TIER_EARTH_ZOOM_DEMO") || std::getenv("VIXEN_TIER_EARTH_ZOOM_SCRIPT")) {
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_X, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Y, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Z, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_DISTANCE, 236.0f);
        mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_EARTH_ZOOM_DEMO: orbitCenter set to demo "
                          "body's world center (64,64,64) so the scripted Earth-scale zoom "
                          "actually orbits the body");
    }
    // Tiered-ESVO Inc3 M7 Task 13: SAME orbitCenter gotcha applies to the reconstructed
    // observable demo (also built at world center (64,64,64)). VIXEN_TIER_OBSERVABLE_DISTANCE
    // (optional) statically re-seeds orbitDistance for a single-shot pixel-decode capture at a
    // hand-picked distance (e.g. the predicted hop0/hop1 handoffs), without needing a scripted
    // zoom -- this milestone's own gate is "confirm concentric magnification on the
    // reconstructed body", not the live zoom (that is M7 Task 14, a separate milestone).
    if (std::getenv("VIXEN_TIER_OBSERVABLE_DEMO")) {
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_X, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Y, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Z, 64.0f);
        float obsDistance = 40.0f;  // default: between hop1 (~19.89) and hop0 (~79.58)
        if (const char* obsDistEnv = std::getenv("VIXEN_TIER_OBSERVABLE_DISTANCE")) {
            obsDistance = std::strtof(obsDistEnv, nullptr);
            if (!(obsDistance > 0.0f)) obsDistance = 40.0f;
        }
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_DISTANCE, obsDistance);
        mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_OBSERVABLE_DEMO: orbitCenter set to "
                          "demo body's world center (64,64,64), orbitDistance=" + std::to_string(obsDistance));
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
                // mechanism this gate proves); childOriginLocal is the MARKED LEAF'S
                // OWN cell center (Tiered-ESVO Inc3 M5 Task 9 fix — see
                // RootLeafOctantCenterLocal's own header comment: the constant
                // (1.5,1.5,1.5) used here pre-M5 is the ROOT CUBE'S shared corner, not
                // any one octant's center, and made the crossing collapse the child
                // toward a corner-anchored, non-concentric "wedge" instead of shrinking
                // around the leaf at any non-unity childScale), so the child tree
                // occupies the SAME [1,2) cell the marked leaf itself occupies (a clean,
                // well-conditioned "known" placement for the hand-computed screen-
                // position cross-check below).
                // Tiered-ESVO Inc3 M2 Task 4: VIXEN_TIER_CROSSING_SCALE_DEMO exercises a genuinely
                // non-unity TierRef::childScale (default 0.25 if set with no value, or the parsed
                // float value) instead of the M3/Inc2 baseline's childScale=1.0 -- the ONLY variable
                // changed vs. the childScale==1.0 fixture above (same childOriginLocal, same leaf,
                // same magenta child, same camera), per the plan's "vary ONLY X" discipline. childScale
                // is a raw linear multiplier on the child's [1,2) unit cube in parent-local space
                // (remapRayIntoChildFrame: childLocalOrigin=(parentLocalOrigin-childOrigin)*invScale+1.5),
                // NOT pre-scaled by the marked leaf's own octant fraction -- so at childScale=1.0 the
                // child fills the ENTIRE marked leaf's own [1,2) cell footprint (world edge 24 =
                // half of 10*renderScale, since the leaf is one root-level octant), and at
                // childScale=0.25 it fills a cube 1/4 the linear size, CONCENTRICALLY centered on
                // the same leaf-cell world point since childOriginLocal is unchanged.
                float childScale = 1.0f;
                if (const char* scaleEnv = std::getenv("VIXEN_TIER_CROSSING_SCALE_DEMO")) {
                    childScale = (scaleEnv[0] != '\0') ? std::strtof(scaleEnv, nullptr) : 0.25f;
                    if (!(childScale > 0.0f)) {
                        childScale = 0.25f;  // guard against a garbage/zero env value
                    }
                    mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_CROSSING_SCALE_DEMO: childScale="
                                  + std::to_string(childScale));
                }

                const glm::vec3 leafCenterLocal = Vixen::SVO::RootLeafOctantCenterLocal(markOctant);
                Vixen::SVO::TierRef ref{};
                ref.childOctreeIndex = 1u;  // child will be concatenated at slot 1
                ref.childOriginLocal[0] = leafCenterLocal.x;
                ref.childOriginLocal[1] = leafCenterLocal.y;
                ref.childOriginLocal[2] = leafCenterLocal.z;
                ref.childScale = childScale;
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
        } else if (std::getenv("VIXEN_TIER_CHAIN_DEMO")) {
            // Tiered-ESVO Inc3 M3 Task 5 live gate: a THREE-tree chain, T0 -> T1 -> T2,
            // reusing the EXACT construction pattern the two-tree VIXEN_TIER_CROSSING_DEMO
            // above already live-gates (SDF sphere per tree, magenta/color-override for
            // per-tier visual attribution, MarkLeafAsTierCrossing on every root-facing leaf,
            // manual ConcatenatedOctrees bookkeeping) — extended to a SECOND crossing:
            // T0's marked leaf points at T1 (slot 1), and T1's OWN marked leaf points at
            // T2 (slot 2). Distinct per-tier colors (parent: default cosine-gradient;
            // T1: solid green; T2: solid cyan) make each hop's contribution visually
            // attributable in a capture, matching Inc2 M3's own "distinct color per tier"
            // discipline (this milestone's own plan §M3 gate requirement).
            mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_CHAIN_DEMO: building hand-authored three-tree chained tier-crossing scene");

            constexpr int   kN          = 16;
            constexpr int   kBrickDepth = 3;
            const glm::vec3 kCenter(8.0f, 8.0f, 8.0f);

            auto bakeSphereTree = [&](float radius) {
                Vixen::SVO::RecipeParams rp{};
                rp.radius = radius;
                Vixen::SVO::SdfBakeResult baked =
                    Vixen::SVO::BakeRecipeToSdfWorld(Vixen::SVO::RECIPE_SPHERE, kCenter, rp, kN, 2.0f);
                return Vixen::SVO::BuildSdfBodyOctree(baked, kBrickDepth);
            };

            Vixen::SVO::SdfBodyOctree t0Body = bakeSphereTree(6.0f);
            Vixen::SVO::SdfBodyOctree t1Body = bakeSphereTree(6.5f);
            Vixen::SVO::SdfBodyOctree t2Body = bakeSphereTree(7.2f);

            Vixen::SVO::SerializedOctree t0Ser = Vixen::SVO::SerializeSdf(t0Body);
            Vixen::SVO::SerializedOctree t1Ser = Vixen::SVO::SerializeSdf(t1Body);
            Vixen::SVO::SerializedOctree t2Ser = Vixen::SVO::SerializeSdf(t2Body);

            // Per-tier solid color override (parent T0 keeps the shared cosine-gradient;
            // T1 solid green, T2 solid cyan — distinct from each other AND from T0's
            // muted-rainbow default, per the plan's "distinct color per tier so each hop
            // is visually attributable" gate requirement).
            auto overrideColor = [&](Vixen::SVO::SerializedOctree& ser, glm::vec3 rgb, const char* label) {
                const uint32_t colorBase = ser.channelBaseFloats(Vixen::SVO::SEM_COLOR);
                if (colorBase == 0xFFFFFFFFu) {
                    mainLogger->Error(std::string("[BuildRenderGraph] VIXEN_TIER_CHAIN_DEMO: ") + label
                                      + " has no SEM_COLOR channel — color override skipped");
                    return;
                }
                float* pool = reinterpret_cast<float*>(ser.channelPool.data());
                const size_t poolFloats = ser.channelPool.size() / sizeof(float);
                for (uint32_t brick = 0; brick < ser.brickCount; ++brick) {
                    for (uint32_t comp = 0; comp < 3; ++comp) {
                        const float c = rgb[static_cast<int>(comp)];
                        for (uint32_t voxel = 0; voxel < Vixen::SVO::SerializedOctree::kVoxelsPerBrick; ++voxel) {
                            const size_t idx = static_cast<size_t>(brick) * ser.brickStrideFloats
                                             + colorBase + comp * Vixen::SVO::SerializedOctree::kVoxelsPerBrick + voxel;
                            if (idx < poolFloats) pool[idx] = c;
                        }
                    }
                }
            };
            overrideColor(t1Ser, glm::vec3(0.0f, 1.0f, 0.0f), "T1");  // solid green
            overrideColor(t2Ser, glm::vec3(0.0f, 1.0f, 1.0f), "T2");  // solid cyan

            // Mip pools (M4 gate reuse: shadeFromMipSample needs real coverage for the
            // LOD/residency fallback paths, exactly like VIXEN_TIER_CROSSING_DEMO above).
            if (const Vixen::SVO::Octree* oct0 = t0Body.octree->getOctree()) Vixen::SVO::BakeAndAttachMipPool(*oct0, t0Ser);
            if (const Vixen::SVO::Octree* oct1 = t1Body.octree->getOctree()) Vixen::SVO::BakeAndAttachMipPool(*oct1, t1Ser);
            if (const Vixen::SVO::Octree* oct2 = t2Body.octree->getOctree()) Vixen::SVO::BakeAndAttachMipPool(*oct2, t2Ser);

            // Locate a camera-facing leaf in T0's root (same octant-selection convention
            // as VIXEN_TIER_CROSSING_DEMO above) and mark it -> T1 (slot 1).
            auto findCameraFacingLeaf = [](const Vixen::SVO::Octree* oct, uint32_t& outDescIdx, int& outOctant) {
                outOctant = -1;
                if (oct == nullptr) return;
                const auto& descs = oct->root->childDescriptors;
                for (uint32_t i = 0; i < descs.size() && outOctant < 0; ++i) {
                    const Vixen::SVO::ChildDescriptor& d = descs[i];
                    for (int o = 4; o < 8; ++o) {
                        if (d.hasChild(o) && d.isLeaf(o)) { outDescIdx = i; outOctant = o; break; }
                    }
                }
                if (outOctant < 0) {
                    for (uint32_t i = 0; i < descs.size() && outOctant < 0; ++i) {
                        const Vixen::SVO::ChildDescriptor& d = descs[i];
                        for (int o = 0; o < 8; ++o) {
                            if (d.hasChild(o) && d.isLeaf(o)) { outDescIdx = i; outOctant = o; break; }
                        }
                    }
                }
            };

            uint32_t t0MarkDescIdx = 0; int t0MarkOctant = -1;
            findCameraFacingLeaf(t0Body.octree->getOctree(), t0MarkDescIdx, t0MarkOctant);
            uint32_t t1MarkDescIdx = 0; int t1MarkOctant = -1;
            findCameraFacingLeaf(t1Body.octree->getOctree(), t1MarkDescIdx, t1MarkOctant);

            if (t0MarkOctant >= 0 && t1MarkOctant >= 0) {
                // Hop 0: T0's marked leaf -> T1 (slot 1). childScale=1.0 (same-scale
                // chaining — Inc3 M4's job is the scale-magnified version; M3 proves the
                // HOP LOOP mechanism itself, same discipline as Inc2 M3 proving the
                // single-restart mechanism before Inc2 M4/Inc3 M1-M2 added LOD/scale).
                // childOriginLocal is the marked leaf's OWN cell center (Inc3 M5 Task 9
                // fix — see RootLeafOctantCenterLocal's header comment); at childScale=1.0
                // this is a no-op vs. the pre-M5 constant (1.5,1.5,1.5) for hop 0's own
                // proof (both formulas agree exactly at unity — see M1's own byte-
                // identical-at-unity gate), so this hop is UNCHANGED in observable behavior.
                const glm::vec3 t0LeafCenterLocal = Vixen::SVO::RootLeafOctantCenterLocal(t0MarkOctant);
                Vixen::SVO::TierRef refT0ToT1{};
                refT0ToT1.childOctreeIndex = 1u;
                refT0ToT1.childOriginLocal[0] = t0LeafCenterLocal.x;
                refT0ToT1.childOriginLocal[1] = t0LeafCenterLocal.y;
                refT0ToT1.childOriginLocal[2] = t0LeafCenterLocal.z;
                refT0ToT1.childScale = 1.0f;
                Vixen::SVO::MarkLeafAsTierCrossing(t0Ser, t0MarkDescIdx, t0MarkOctant, refT0ToT1, 22);

                // Hop 1: T1's OWN marked leaf -> T2 (slot 2, T1's own child-slot
                // numbering — ConcatenatedOctrees resolves childOctreeIndex against the
                // GLOBAL concatenated configs[] array, so this is genuinely slot 2, not
                // slot 1 relative to T1). Same M5 leaf-center fix, same unity no-op.
                const glm::vec3 t1LeafCenterLocal = Vixen::SVO::RootLeafOctantCenterLocal(t1MarkOctant);
                Vixen::SVO::TierRef refT1ToT2{};
                refT1ToT2.childOctreeIndex = 2u;
                refT1ToT2.childOriginLocal[0] = t1LeafCenterLocal.x;
                refT1ToT2.childOriginLocal[1] = t1LeafCenterLocal.y;
                refT1ToT2.childOriginLocal[2] = t1LeafCenterLocal.z;
                refT1ToT2.childScale = 1.0f;
                Vixen::SVO::MarkLeafAsTierCrossing(t1Ser, t1MarkDescIdx, t1MarkOctant, refT1ToT2, 22);

                // Manual 3-tree concatenation (parent=slot0, T1=slot1, T2=slot2) — same
                // per-octree bookkeeping loop as the two-tree demo above, generalized to 3.
                Vixen::SVO::ConcatenatedOctrees cat;
                cat.count = 3;
                cat.configs.resize(3);
                cat.nodeCounts.resize(3);
                cat.brickCounts.resize(3);
                cat.tierRefCounts.resize(3);

                Vixen::SVO::SerializedOctree* octs[3] = {&t0Ser, &t1Ser, &t2Ser};
                uint32_t nodeBase = 0, brickBase = 0, poolBase = 0, tierRefBase = 0, mipPoolBase = 0;
                for (int k = 0; k < 3; ++k) {
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

                if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                    bodyScene->SetRecipePool(std::move(cat));

                    constexpr float kRenderScale = 4.8f;
                    constexpr float kHalf = 5.0f * kRenderScale;
                    Vixen::SVO::BodyInstanceGpu inst{};
                    inst.worldPos[0]  = 64.0f - kHalf;
                    inst.worldPos[1]  = 64.0f - kHalf;
                    inst.worldPos[2]  = 64.0f - kHalf;
                    inst.renderScale  = kRenderScale;
                    inst.color[0]     = 1.0f;
                    inst.color[1]     = 1.0f;
                    inst.color[2]     = 1.0f;
                    inst.octreeIndex  = 0u;    // parent (T0) tree
                    inst.providerKind = 0u;    // PROVIDER_STORED
                    inst.recipeId     = 0u;

                    bodyScene->SetInstances({inst});
                    mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_CHAIN_DEMO: T0 leaf ("
                                  + std::to_string(t0MarkDescIdx) + "," + std::to_string(t0MarkOctant)
                                  + ") -> T1 octree1; T1 leaf (" + std::to_string(t1MarkDescIdx) + ","
                                  + std::to_string(t1MarkOctant) + ") -> T2 octree2");
                }
            } else {
                mainLogger->Error("[BuildRenderGraph] VIXEN_TIER_CHAIN_DEMO: no camera-facing leaf found in T0 or T1 — demo scene not built");
            }
        } else if (std::getenv("VIXEN_TIER_EARTH_DEMO")) {
            // Tiered-ESVO Inc3 M4 Task 6 (the epic gate): the SAME T0->T1->T2 chained
            // construction as VIXEN_TIER_CHAIN_DEMO above, but at the REAL per-hop tier
            // ratio the epic exists for (childScale=2^-10 at BOTH hops, not M3's
            // proof-of-mechanism 1.0) — the "Earth-diameter-scale" demonstration.
            //
            // Numeric derivation (hand-computed BEFORE this scene was built, per this
            // increment's prediction-first discipline; full trace in the milestone's
            // Progress Log / Tiered-ESVO-Inc3-M4-earth-scale-derivation.py):
            //   - T0's own world diameter (existing convention: kRenderScale=4.8 *
            //     kWorldGridSize=10) = 48.0 world units. Declaring this span AS Earth's
            //     actual diameter (12,742 km) fixes 1 world unit = 265,458 m.
            //   - Hop 0 (T0->T1, childScale=2^-10): T1's own world diameter =
            //     48.0 * 2^-10 = 0.046875 units = 12,443 m (~12.4 km, a "region" tier).
            //   - Hop 1 (T1->T2, childScale=2^-10): T2's own world diameter =
            //     0.046875 * 2^-10 ~= 4.578e-5 units ~= 12.15 m (a "bedrock" tier);
            //     T2's own single brick spans ~6.08 m, a single voxel ~0.76 m.
            //   - Total scale ratio across both hops: 2^-20 (~9.5e-7), i.e. 20 extra
            //     bits of dynamic range chained on top of a single tree's own 23-level
            //     (2^23) internal range -- this is the actual mechanism the "~30-31
            //     effective levels" epic framing refers to (a rough order-of-magnitude
            //     estimate in the design doc, not a load-bearing exact figure; the
            //     precise, verifiable claim is the 2^-20 total ratio and the concrete
            //     per-tier meter figures above).
            mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_EARTH_DEMO: building Earth-scale "
                              "(childScale=2^-10/hop) three-tree chained tier-crossing scene");

            constexpr int   kN          = 16;
            constexpr int   kBrickDepth = 3;
            const glm::vec3 kCenter(8.0f, 8.0f, 8.0f);
            constexpr float kChildScale = 0.0009765625f;  // 2^-10, the real per-hop tier ratio

            auto bakeSphereTree = [&](float radius) {
                Vixen::SVO::RecipeParams rp{};
                rp.radius = radius;
                Vixen::SVO::SdfBakeResult baked =
                    Vixen::SVO::BakeRecipeToSdfWorld(Vixen::SVO::RECIPE_SPHERE, kCenter, rp, kN, 2.0f);
                return Vixen::SVO::BuildSdfBodyOctree(baked, kBrickDepth);
            };

            Vixen::SVO::SdfBodyOctree t0Body = bakeSphereTree(6.0f);
            Vixen::SVO::SdfBodyOctree t1Body = bakeSphereTree(6.5f);
            Vixen::SVO::SdfBodyOctree t2Body = bakeSphereTree(7.2f);

            Vixen::SVO::SerializedOctree t0Ser = Vixen::SVO::SerializeSdf(t0Body);
            Vixen::SVO::SerializedOctree t1Ser = Vixen::SVO::SerializeSdf(t1Body);
            Vixen::SVO::SerializedOctree t2Ser = Vixen::SVO::SerializeSdf(t2Body);

            // Per-tier solid color override (parent T0 keeps the shared cosine-gradient;
            // T1 solid green (region tier); T2 solid cyan (bedrock tier)) -- IDENTICAL
            // convention to VIXEN_TIER_CHAIN_DEMO above, so a capture's per-tier
            // attribution reads the same way at both ratios.
            auto overrideColor = [&](Vixen::SVO::SerializedOctree& ser, glm::vec3 rgb, const char* label) {
                const uint32_t colorBase = ser.channelBaseFloats(Vixen::SVO::SEM_COLOR);
                if (colorBase == 0xFFFFFFFFu) {
                    mainLogger->Error(std::string("[BuildRenderGraph] VIXEN_TIER_EARTH_DEMO: ") + label
                                      + " has no SEM_COLOR channel — color override skipped");
                    return;
                }
                float* pool = reinterpret_cast<float*>(ser.channelPool.data());
                const size_t poolFloats = ser.channelPool.size() / sizeof(float);
                for (uint32_t brick = 0; brick < ser.brickCount; ++brick) {
                    for (uint32_t comp = 0; comp < 3; ++comp) {
                        const float c = rgb[static_cast<int>(comp)];
                        for (uint32_t voxel = 0; voxel < Vixen::SVO::SerializedOctree::kVoxelsPerBrick; ++voxel) {
                            const size_t idx = static_cast<size_t>(brick) * ser.brickStrideFloats
                                             + colorBase + comp * Vixen::SVO::SerializedOctree::kVoxelsPerBrick + voxel;
                            if (idx < poolFloats) pool[idx] = c;
                        }
                    }
                }
            };
            overrideColor(t1Ser, glm::vec3(0.0f, 1.0f, 0.0f), "T1");  // solid green (region)
            overrideColor(t2Ser, glm::vec3(0.0f, 1.0f, 1.0f), "T2");  // solid cyan (bedrock)

            if (const Vixen::SVO::Octree* oct0 = t0Body.octree->getOctree()) Vixen::SVO::BakeAndAttachMipPool(*oct0, t0Ser);
            if (const Vixen::SVO::Octree* oct1 = t1Body.octree->getOctree()) Vixen::SVO::BakeAndAttachMipPool(*oct1, t1Ser);
            if (const Vixen::SVO::Octree* oct2 = t2Body.octree->getOctree()) Vixen::SVO::BakeAndAttachMipPool(*oct2, t2Ser);

            // Camera-facing leaf selection -- IDENTICAL convention to VIXEN_TIER_CHAIN_DEMO
            // (octant bit2/z set preferred, i.e. octants 4-7). For this fixture (n=16,
            // brickDepth=3, camera along -Z through the sphere's center) the selected leaf
            // is DETERMINISTICALLY octant 4 (x=0,y=0,z=1 bit pattern) -- verified via a
            // temporary discovery probe (test_tier_crossing_mirror_parity.cpp, removed
            // before that increment's commit) before this scene was written, not assumed.
            auto findCameraFacingLeaf = [](const Vixen::SVO::Octree* oct, uint32_t& outDescIdx, int& outOctant) {
                outOctant = -1;
                if (oct == nullptr) return;
                const auto& descs = oct->root->childDescriptors;
                for (uint32_t i = 0; i < descs.size() && outOctant < 0; ++i) {
                    const Vixen::SVO::ChildDescriptor& d = descs[i];
                    for (int o = 4; o < 8; ++o) {
                        if (d.hasChild(o) && d.isLeaf(o)) { outDescIdx = i; outOctant = o; break; }
                    }
                }
                if (outOctant < 0) {
                    for (uint32_t i = 0; i < descs.size() && outOctant < 0; ++i) {
                        const Vixen::SVO::ChildDescriptor& d = descs[i];
                        for (int o = 0; o < 8; ++o) {
                            if (d.hasChild(o) && d.isLeaf(o)) { outDescIdx = i; outOctant = o; break; }
                        }
                    }
                }
            };

            uint32_t t0MarkDescIdx = 0; int t0MarkOctant = -1;
            findCameraFacingLeaf(t0Body.octree->getOctree(), t0MarkDescIdx, t0MarkOctant);
            uint32_t t1MarkDescIdx = 0; int t1MarkOctant = -1;
            findCameraFacingLeaf(t1Body.octree->getOctree(), t1MarkDescIdx, t1MarkOctant);

            if (t0MarkOctant >= 0 && t1MarkOctant >= 0) {
                // tEntryWorld / k-invariant placement (Inc3 M4's own carry-forward
                // constraint, sharpened from M1/M3): at childScale=2^-10, 1/childScale
                // ~= 1024 per hop, so ANY macroscopically-off-boundary entry point would
                // be amplified by up to ~1024x (hop 1) or ~1,048,576x (hop 2) via the
                // cumulative-length multiply. This is handled by APPROACH (b): enforcing
                // that every hop's remapped entry lands well INSIDE the marked leaf's own
                // child grid, via the k-invariant childOriginLocal placement technique
                // (childOriginLocal = entryPointLocal - offset*childScale collapses the
                // remapped entry to a k-invariant 1.5+offset regardless of childScale;
                // see test_tier_crossing_mirror_parity.cpp's BuildTask3ParentWithScale /
                // ChainedTwoHopCrossingComposesHitT / EarthScaleChainedCrossingKInvariant-
                // Placement for the CPU-side proof of this exact technique at this exact
                // ratio, verified BEFORE this scene was written).
                //
                // CRITICAL, discovery-trail-verified correction (a first attempt at this
                // scene used a uniform offset and produced a chain that only crossed
                // ONCE, not twice -- caught via a temporary discovery probe, removed
                // before commit): the offset's sign per axis is NOT a universal constant
                // -- it must point INTO the SPECIFIC octant's own box being targeted.
                // Octant 4 (the camera-facing leaf this fixture always selects, bit
                // pattern x=0,y=0,z=1) occupies the ASYMMETRIC box x in [1,1.5),
                // y in [1,1.5), z in [1.5,2) relative to (1.5,1.5,1.5) -- so the offset
                // must be NEGATIVE on x/y (pull toward the box, which sits BELOW 1.5) and
                // POSITIVE on z (pull toward the box, which sits ABOVE 1.5). A uniform
                // (-0.1,-0.1,-0.1) landed the remapped entry at local z=1.4, OUTSIDE
                // octant 4's own z>=1.5 requirement -- confirmed the entry fell through to
                // a DIFFERENT part of the tree (or T1's own surface) rather than back into
                // the marked leaf, breaking the second crossing.
                //
                // SECOND, deeper correction (found via a live capture showing the crossing
                // wedge rendering as pure background/miss, not T1/T2's own color): magnitude
                // 0.1 lands the remapped entry at grid-space distance ~2.77 from T1/T2's OWN
                // local center (8,8,8 in their [0,16] grid) -- WELL INSIDE their solid sphere
                // interior (radius 6.5/7.2), nowhere near either body's own ISO-SURFACE. The
                // SDF march (handleLeafHitInstancedSdf/marchBrickSdf) searches for a
                // sign-change (surface) within its OWN local brick, not "any solid voxel" (the
                // CPU GpuTraversalMirror's own hit=true finding for this construction is a
                // BINARY-DDA-path artifact -- GpuTraversalMirror does NOT model the SDF march
                // at all, per its own header comment -- so it could not have caught this).
                // Magnitude 0.25 (verified via a hand derivation BEFORE this fix: a point along
                // octant 4's own (-1,-1,+1) diagonal at magnitude 0.25 sits at grid-space
                // distance ~6.93 from center, within ~0.43/0.27 grid units of T1's (r=6.5) and
                // T2's (r=7.2) own surfaces respectively -- comfortably inside marchBrickSdf's
                // own brick-local search range) keeps the SAME k-invariant safety property
                // (still comfortably inside [1,2) at every hop) while ALSO landing close enough
                // to each child's own real surface to be found.
                const glm::vec3 kBoxOffset(-0.25f, -0.25f, 0.25f);

                // Hop 0's crossing point is geometry-determined (wherever the camera ray
                // through the sphere's center actually enters octant 4's leaf) -- for this
                // fixture + camera it is (1.5,1.5,2.0), the leaf's own outer (+Z, camera-
                // facing) corner, confirmed via the same discovery probe referenced above
                // (a camera ray straight through a sphere's silhouette center necessarily
                // enters the nearest octant's OWN outer face -- geometric, not an
                // accident of this specific fixture).
                const glm::vec3 kHop0EntryPointLocal(1.5f, 1.5f, 2.0f);
                const glm::vec3 t0ChildOriginLocal = kHop0EntryPointLocal - kBoxOffset * kChildScale;

                Vixen::SVO::TierRef refT0ToT1{};
                refT0ToT1.childOctreeIndex = 1u;
                refT0ToT1.childOriginLocal[0] = t0ChildOriginLocal.x;
                refT0ToT1.childOriginLocal[1] = t0ChildOriginLocal.y;
                refT0ToT1.childOriginLocal[2] = t0ChildOriginLocal.z;
                refT0ToT1.childScale = kChildScale;
                Vixen::SVO::MarkLeafAsTierCrossing(t0Ser, t0MarkDescIdx, t0MarkOctant, refT0ToT1, 22);

                // Hop 1's crossing point: the k-invariant collapse (see kBoxOffset's own
                // comment) lands T1's OWN remapped entry at EXACTLY (1.5,1.5,1.5)+
                // kBoxOffset = (1.4,1.4,1.6), safely inside octant 4's own box on every
                // axis -- T1's marked leaf (also camera-facing octant 4, by the SAME
                // deterministic selection) is placed relative to THIS point using the
                // IDENTICAL technique, so hop 2's remapped entry lands inside T2's octant-4
                // box too.
                const glm::vec3 kHop1EntryPointLocal = glm::vec3(1.5f, 1.5f, 1.5f) + kBoxOffset;
                const glm::vec3 t1ChildOriginLocal = kHop1EntryPointLocal - kBoxOffset * kChildScale;

                Vixen::SVO::TierRef refT1ToT2{};
                refT1ToT2.childOctreeIndex = 2u;
                refT1ToT2.childOriginLocal[0] = t1ChildOriginLocal.x;
                refT1ToT2.childOriginLocal[1] = t1ChildOriginLocal.y;
                refT1ToT2.childOriginLocal[2] = t1ChildOriginLocal.z;
                refT1ToT2.childScale = kChildScale;
                Vixen::SVO::MarkLeafAsTierCrossing(t1Ser, t1MarkDescIdx, t1MarkOctant, refT1ToT2, 22);

                Vixen::SVO::ConcatenatedOctrees cat;
                cat.count = 3;
                cat.configs.resize(3);
                cat.nodeCounts.resize(3);
                cat.brickCounts.resize(3);
                cat.tierRefCounts.resize(3);

                Vixen::SVO::SerializedOctree* octs[3] = {&t0Ser, &t1Ser, &t2Ser};
                uint32_t nodeBase = 0, brickBase = 0, poolBase = 0, tierRefBase = 0, mipPoolBase = 0;
                for (int k = 0; k < 3; ++k) {
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

                // Every octree resident from the start (Earth-scale demo's own residency
                // exercise is a SEPARATE, explicit mid-flight RequestBrickResidency(true)
                // scripted by VIXEN_TIER_EARTH_ZOOM_DEMO below, at ONE hop, matching Inc2
                // M5's "start non-resident, grant mid-flight" discipline) -- unlike
                // VIXEN_TIER_CROSSING_NONRESIDENT/VIXEN_TIER_ZOOM_DEMO's whole-node
                // start-false convention, this scene starts resident by default so a
                // bare VIXEN_TIER_EARTH_DEMO=1 run (no zoom script) shows real geometry
                // immediately; VIXEN_TIER_EARTH_ZOOM_DEMO explicitly forces non-resident
                // at start via its own RequestBrickResidency(false) call, mirroring the
                // Inc2 M5 pattern exactly (see the Update()/PreTick scripted-zoom block).
                if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                    bodyScene->SetRecipePool(std::move(cat));
                    if (std::getenv("VIXEN_TIER_EARTH_ZOOM_DEMO")) {
                        bodyScene->RequestBrickResidency(false);
                        mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_EARTH_ZOOM_DEMO: "
                                          "RequestBrickResidency(false) -- all octrees mip-only at start");
                    }

                    constexpr float kRenderScale = 4.8f;
                    constexpr float kHalf = 5.0f * kRenderScale;
                    Vixen::SVO::BodyInstanceGpu inst{};
                    inst.worldPos[0]  = 64.0f - kHalf;
                    inst.worldPos[1]  = 64.0f - kHalf;
                    inst.worldPos[2]  = 64.0f - kHalf;
                    inst.renderScale  = kRenderScale;
                    inst.color[0]     = 1.0f;
                    inst.color[1]     = 1.0f;
                    inst.color[2]     = 1.0f;
                    inst.octreeIndex  = 0u;    // parent (T0) tree
                    inst.providerKind = 0u;    // PROVIDER_STORED
                    inst.recipeId     = 0u;

                    bodyScene->SetInstances({inst});
                    mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_EARTH_DEMO: T0 leaf ("
                                  + std::to_string(t0MarkDescIdx) + "," + std::to_string(t0MarkOctant)
                                  + ") -> T1 octree1 (childScale=2^-10); T1 leaf (" + std::to_string(t1MarkDescIdx) + ","
                                  + std::to_string(t1MarkOctant) + ") -> T2 octree2 (childScale=2^-10)");
                }
            } else {
                mainLogger->Error("[BuildRenderGraph] VIXEN_TIER_EARTH_DEMO: no camera-facing leaf found in T0 or T1 — demo scene not built");
            }
        } else if (std::getenv("VIXEN_TIER_OBSERVABLE_DEMO")) {
            // Tiered-ESVO Inc3 M7 Task 13: the SAME T0->T1->T2 chained construction as
            // VIXEN_TIER_CHAIN_DEMO/VIXEN_TIER_EARTH_DEMO above (per-tier color override,
            // camera-facing-octant selection, RootLeafOctantCenterLocal concentric
            // placement), but with body/tier proportions chosen so BOTH LOD-handoff
            // distances are reachable, outside the solid interior, AND inside the
            // camera's default 22.5-deg half-FOV cone -- unlike the Earth demo
            // (M6 Progress Log), whose 48-world-unit body + 2^-10-per-hop ratio locks
            // the two handoffs 1024x apart and forces at least one of them either past
            // the orbit ceiling (120) or deep inside the marked octant's ~62-125-deg
            // off-axis blind zone.
            //
            // Numeric derivation (hand-computed BEFORE this scene was built; verified
            // against the Earth demo's OWN two independently-reported handoff distances
            // -- 14.921wu and 0.0146wu at childScale=2^-10, renderScale=4.8 -- which both
            // match a single calibrated constant to 4+ significant figures, confirming
            // the formula below rather than assuming it):
            //   worldDistance_handoff = 20 * R * childScale * scale_exp2 / raySizeCoef
            // where R = renderScale (world diameter = 10*R), scale_exp2 = 0.25 (a root-
            // level leaf, this fixture's marked octant is always a direct child of the
            // root), and raySizeCoef = 2*tan((fovRad/height)*0.5) = 0.0015708 at this
            // app's 45-deg-FOV/500-px-tall default render target.
            //
            // The marked octant (always octant 4 in this fixture's camera-facing
            // selection, bit pattern x=0,y=0,z=1) sits at a FIXED world offset of
            // (-2.5R,-2.5R,+2.5R) from the body's own center -- a direct consequence of
            // RootLeafOctantCenterLocal's own 1.25/1.75-per-axis convention, independent
            // of R. The angle between the camera's forward axis (looking down -Z at the
            // body center) and this octant, as seen from a camera at orbit distance d,
            // is therefore a SCALE-INVARIANT function of (d/R) alone -- it crosses the
            // 22.5-deg half-FOV boundary at d ~= 10*R (bisected numerically). The body's
            // own solid surface radius is empirically ~5.625*R (measured on the Earth/
            // Chain demos' shared recipe proportions: 27wu/4.8 renderScale).
            //
            // Chosen: R=0.1 (body world diameter 1.0 unit), childScale=0.25/hop (a
            // deliberately gentle, non-2^-10 ratio per the M7 plan's own "may mean a
            // deliberately NON-Earth-diameter ratio for a first observable proof"
            // allowance):
            //   hop0 (T0->T1) = 20*0.1*0.25*0.25/0.0015708 ~= 79.58 world units
            //   hop1 (T1->T2) = hop0*0.25                  ~= 19.89 world units
            //   in-FOV threshold (10*R)                     =  1.00 world units
            //   solid surface radius (5.625*R)               =  0.5625 world units
            //   orbit ceiling (CameraNode::kOrbitDistanceMax) = 120.0 world units
            // Both hop0 and hop1 sit inside [1.0, 120.0] (comfortably clear of the
            // in-FOV floor AND the solid radius) -- and at these distances the octant's
            // own off-axis angle is under 1.1 deg (bisection-verified below), an order
            // of magnitude inside the 22.5-deg half-cone, not merely "reachable" but
            // solidly centered.
            mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_OBSERVABLE_DEMO: building "
                              "reconstructed three-tree chained tier-crossing scene "
                              "(R=0.1, childScale=0.25/hop -- both crossings observable "
                              "on the default view axis)");

            constexpr int   kN          = 16;
            constexpr int   kBrickDepth = 3;
            const glm::vec3 kCenter(8.0f, 8.0f, 8.0f);
            // VIXEN_TIER_OBSERVABLE_CHILDSCALE (optional): sweeps childScale live for the
            // concentric-magnification proof (M5's own methodology — vary ONLY childScale at a
            // FIXED camera distance, verify the crossing patch shrinks concentrically about a
            // stable center at the predicted 0.5*childScale-per-axis law). Default 0.25 matches
            // this scene's own hand-computed hop0/hop1 derivation above.
            float kObsChildScale = 0.25f;
            if (const char* obsScaleEnv = std::getenv("VIXEN_TIER_OBSERVABLE_CHILDSCALE")) {
                kObsChildScale = std::strtof(obsScaleEnv, nullptr);
                if (!(kObsChildScale > 0.0f)) kObsChildScale = 0.25f;
            }

            auto bakeSphereTreeObs = [&](float radius) {
                Vixen::SVO::RecipeParams rp{};
                rp.radius = radius;
                Vixen::SVO::SdfBakeResult baked =
                    Vixen::SVO::BakeRecipeToSdfWorld(Vixen::SVO::RECIPE_SPHERE, kCenter, rp, kN, 2.0f);
                return Vixen::SVO::BuildSdfBodyOctree(baked, kBrickDepth);
            };

            Vixen::SVO::SdfBodyOctree obsT0Body = bakeSphereTreeObs(6.0f);
            Vixen::SVO::SdfBodyOctree obsT1Body = bakeSphereTreeObs(6.5f);
            Vixen::SVO::SdfBodyOctree obsT2Body = bakeSphereTreeObs(7.2f);

            Vixen::SVO::SerializedOctree obsT0Ser = Vixen::SVO::SerializeSdf(obsT0Body);
            Vixen::SVO::SerializedOctree obsT1Ser = Vixen::SVO::SerializeSdf(obsT1Body);
            Vixen::SVO::SerializedOctree obsT2Ser = Vixen::SVO::SerializeSdf(obsT2Body);

            auto overrideColorObs = [&](Vixen::SVO::SerializedOctree& ser, glm::vec3 rgb, const char* label) {
                const uint32_t colorBase = ser.channelBaseFloats(Vixen::SVO::SEM_COLOR);
                if (colorBase == 0xFFFFFFFFu) {
                    mainLogger->Error(std::string("[BuildRenderGraph] VIXEN_TIER_OBSERVABLE_DEMO: ") + label
                                      + " has no SEM_COLOR channel — color override skipped");
                    return;
                }
                float* pool = reinterpret_cast<float*>(ser.channelPool.data());
                const size_t poolFloats = ser.channelPool.size() / sizeof(float);
                for (uint32_t brick = 0; brick < ser.brickCount; ++brick) {
                    for (uint32_t comp = 0; comp < 3; ++comp) {
                        const float c = rgb[static_cast<int>(comp)];
                        for (uint32_t voxel = 0; voxel < Vixen::SVO::SerializedOctree::kVoxelsPerBrick; ++voxel) {
                            const size_t idx = static_cast<size_t>(brick) * ser.brickStrideFloats
                                             + colorBase + comp * Vixen::SVO::SerializedOctree::kVoxelsPerBrick + voxel;
                            if (idx < poolFloats) pool[idx] = c;
                        }
                    }
                }
            };
            overrideColorObs(obsT1Ser, glm::vec3(0.0f, 1.0f, 0.0f), "T1");  // solid green (region)
            overrideColorObs(obsT2Ser, glm::vec3(0.0f, 1.0f, 1.0f), "T2");  // solid cyan (bedrock)

            if (const Vixen::SVO::Octree* oct0 = obsT0Body.octree->getOctree()) Vixen::SVO::BakeAndAttachMipPool(*oct0, obsT0Ser);
            if (const Vixen::SVO::Octree* oct1 = obsT1Body.octree->getOctree()) Vixen::SVO::BakeAndAttachMipPool(*oct1, obsT1Ser);
            if (const Vixen::SVO::Octree* oct2 = obsT2Body.octree->getOctree()) Vixen::SVO::BakeAndAttachMipPool(*oct2, obsT2Ser);

            auto findCameraFacingLeafObs = [](const Vixen::SVO::Octree* oct, uint32_t& outDescIdx, int& outOctant) {
                outOctant = -1;
                if (oct == nullptr) return;
                const auto& descs = oct->root->childDescriptors;
                for (uint32_t i = 0; i < descs.size() && outOctant < 0; ++i) {
                    const Vixen::SVO::ChildDescriptor& d = descs[i];
                    for (int o = 4; o < 8; ++o) {
                        if (d.hasChild(o) && d.isLeaf(o)) { outDescIdx = i; outOctant = o; break; }
                    }
                }
                if (outOctant < 0) {
                    for (uint32_t i = 0; i < descs.size() && outOctant < 0; ++i) {
                        const Vixen::SVO::ChildDescriptor& d = descs[i];
                        for (int o = 0; o < 8; ++o) {
                            if (d.hasChild(o) && d.isLeaf(o)) { outDescIdx = i; outOctant = o; break; }
                        }
                    }
                }
            };

            uint32_t obsT0MarkDescIdx = 0; int obsT0MarkOctant = -1;
            findCameraFacingLeafObs(obsT0Body.octree->getOctree(), obsT0MarkDescIdx, obsT0MarkOctant);
            uint32_t obsT1MarkDescIdx = 0; int obsT1MarkOctant = -1;
            findCameraFacingLeafObs(obsT1Body.octree->getOctree(), obsT1MarkDescIdx, obsT1MarkOctant);

            if (obsT0MarkOctant >= 0 && obsT1MarkOctant >= 0) {
                // M5's proven concentric fix: childOriginLocal = the marked leaf's OWN
                // cell center (RootLeafOctantCenterLocal), not the root cube's shared
                // corner (1.5,1.5,1.5). Applied identically at BOTH hops -- unlike the
                // Earth demo's entry-anchored k-invariant technique (needed there ONLY
                // because childScale=2^-10 would blow up a macroscopically-off-boundary
                // entry by ~1024x per hop), this demo's childScale=0.25 has no such
                // precision hazard, so the simpler, already-concentric-proven M5
                // technique applies directly with no adaptation.
                const glm::vec3 obsT0LeafCenterLocal = Vixen::SVO::RootLeafOctantCenterLocal(obsT0MarkOctant);
                Vixen::SVO::TierRef obsRefT0ToT1{};
                obsRefT0ToT1.childOctreeIndex = 1u;
                obsRefT0ToT1.childOriginLocal[0] = obsT0LeafCenterLocal.x;
                obsRefT0ToT1.childOriginLocal[1] = obsT0LeafCenterLocal.y;
                obsRefT0ToT1.childOriginLocal[2] = obsT0LeafCenterLocal.z;
                obsRefT0ToT1.childScale = kObsChildScale;
                Vixen::SVO::MarkLeafAsTierCrossing(obsT0Ser, obsT0MarkDescIdx, obsT0MarkOctant, obsRefT0ToT1, 22);

                const glm::vec3 obsT1LeafCenterLocal = Vixen::SVO::RootLeafOctantCenterLocal(obsT1MarkOctant);
                Vixen::SVO::TierRef obsRefT1ToT2{};
                obsRefT1ToT2.childOctreeIndex = 2u;
                obsRefT1ToT2.childOriginLocal[0] = obsT1LeafCenterLocal.x;
                obsRefT1ToT2.childOriginLocal[1] = obsT1LeafCenterLocal.y;
                obsRefT1ToT2.childOriginLocal[2] = obsT1LeafCenterLocal.z;
                obsRefT1ToT2.childScale = kObsChildScale;
                Vixen::SVO::MarkLeafAsTierCrossing(obsT1Ser, obsT1MarkDescIdx, obsT1MarkOctant, obsRefT1ToT2, 22);

                Vixen::SVO::ConcatenatedOctrees obsCat;
                obsCat.count = 3;
                obsCat.configs.resize(3);
                obsCat.nodeCounts.resize(3);
                obsCat.brickCounts.resize(3);
                obsCat.tierRefCounts.resize(3);

                Vixen::SVO::SerializedOctree* obsOcts[3] = {&obsT0Ser, &obsT1Ser, &obsT2Ser};
                uint32_t obsNodeBase = 0, obsBrickBase = 0, obsPoolBase = 0, obsTierRefBase = 0, obsMipPoolBase = 0;
                for (int k = 0; k < 3; ++k) {
                    Vixen::SVO::SerializedOctree& s = *obsOcts[k];
                    s.config.nodeArrayBase  = static_cast<int32_t>(obsNodeBase);
                    s.config.brickArrayBase = static_cast<int32_t>(obsBrickBase);
                    Vixen::SVO::setSdfBrickArrayBase(s.config, obsPoolBase);
                    Vixen::SVO::setTierRefTableBase(s.config, obsTierRefBase);
                    Vixen::SVO::setMipPoolBase(s.config, obsMipPoolBase);

                    obsCat.configs[k]       = s.config;
                    obsCat.nodeCounts[k]    = s.nodeCount;
                    obsCat.brickCounts[k]   = s.brickCount;
                    obsCat.tierRefCounts[k] = static_cast<uint32_t>(s.tierRefs.size());

                    obsCat.nodes.insert(obsCat.nodes.end(), s.nodes.begin(), s.nodes.end());
                    obsCat.bricks.insert(obsCat.bricks.end(), s.bricks.begin(), s.bricks.end());
                    obsCat.channelPool.insert(obsCat.channelPool.end(), s.channelPool.begin(), s.channelPool.end());
                    obsCat.brickGridLookup.insert(obsCat.brickGridLookup.end(), s.brickGridLookup.begin(), s.brickGridLookup.end());
                    obsCat.tierRefTable.insert(obsCat.tierRefTable.end(), s.tierRefs.begin(), s.tierRefs.end());
                    obsCat.mipPool.insert(obsCat.mipPool.end(), s.mipPool.begin(), s.mipPool.end());

                    if (obsCat.materials.empty()) {
                        obsCat.materials = s.materials;
                    }

                    obsNodeBase    += s.nodeCount;
                    obsBrickBase   += s.brickCount;
                    obsPoolBase    += s.brickCount * s.brickStrideFloats;
                    obsTierRefBase += static_cast<uint32_t>(s.tierRefs.size());
                    obsMipPoolBase += s.nodeCount * s.channelCount;
                }

                if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                    bodyScene->SetRecipePool(std::move(obsCat));

                    // renderScale=0.1 -> world diameter = 10*0.1 = 1.0 unit, centered on
                    // (64,64,64) (the same world center every tier-crossing demo in this
                    // file uses, so the existing VIXEN_TIER_ZOOM_DEMO-style orbitCenter
                    // wiring below still applies unmodified).
                    constexpr float kObsRenderScale = 0.1f;
                    constexpr float kObsHalf = 5.0f * kObsRenderScale;  // = 0.5f
                    Vixen::SVO::BodyInstanceGpu obsInst{};
                    obsInst.worldPos[0]  = 64.0f - kObsHalf;
                    obsInst.worldPos[1]  = 64.0f - kObsHalf;
                    obsInst.worldPos[2]  = 64.0f - kObsHalf;
                    obsInst.renderScale  = kObsRenderScale;
                    obsInst.color[0]     = 1.0f;
                    obsInst.color[1]     = 1.0f;
                    obsInst.color[2]     = 1.0f;
                    obsInst.octreeIndex  = 0u;
                    obsInst.providerKind = 0u;
                    obsInst.recipeId     = 0u;

                    bodyScene->SetInstances({obsInst});
                    mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_OBSERVABLE_DEMO: T0 leaf ("
                                  + std::to_string(obsT0MarkDescIdx) + "," + std::to_string(obsT0MarkOctant)
                                  + ") -> T1 octree1 (childScale=0.25); T1 leaf (" + std::to_string(obsT1MarkDescIdx) + ","
                                  + std::to_string(obsT1MarkOctant) + ") -> T2 octree2 (childScale=0.25); "
                                  "predicted hop0~=79.58wu, hop1~=19.89wu, both <1.1deg off-axis");
                }
            } else {
                mainLogger->Error("[BuildRenderGraph] VIXEN_TIER_OBSERVABLE_DEMO: no camera-facing leaf found in T0 or T1 — demo scene not built");
            }
        } else if (std::getenv("VIXEN_RESTIR_GATE_DEMO")) {
            // Sampled Lighting Inc3 M4 live gate: the equal-error-vs-brute-force check the
            // plan's Task 4 requires. Bakes THE SAME >=10^3-emissive-voxel gate scene
            // test_light_tree.cpp's BakeEmissiveGateScene uses (n=32, r=10, center(16,16,16),
            // spatially-varying emissive intensity over the whole occupied volume), computes
            // the CPU light-tree cut + brute-force reference from it, pushes the cut to the
            // GPU via LightTreeBufferNode::SetLightTreeCut (transformed grid->world -- see
            // LightTreeBufferNode.h's own scope note on this), and bakes the SAME body into
            // BodyOctreeSceneNode for the live render. VulkanGraphApplication::Update's own
            // VIXEN_RESTIR_GATE_DEMO tick hook (below) reads back reservoirRecordsA/B after
            // enough frames for temporal convergence and logs the numeric comparison.
            mainLogger->Info("[BuildRenderGraph] VIXEN_RESTIR_GATE_DEMO: building the M3 emissive gate scene "
                              "for the ReSTIR equal-error-vs-brute-force live gate");

            constexpr int   kN      = 32;
            constexpr float kR      = 10.0f;
            constexpr float kBand   = 2.0f;
            const glm::vec3 kCenter(16.0f, 16.0f, 16.0f);

            Vixen::SVO::RecipeParams rp{kR, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
            Vixen::SVO::SdfBakeResult baked = Vixen::SVO::BakeRecipeToSdfWorldWithEmission(
                Vixen::SVO::RECIPE_SPHERE, kCenter, rp, kN, kBand,
                [](const glm::vec3& p) { return 1.0f + 0.1f * (p.x + p.y + p.z); });
            Vixen::SVO::SdfBodyOctree body = Vixen::SVO::BuildSdfBodyOctree(baked, 3);

            Vixen::SVO::SerializedOctree ser = Vixen::SVO::SerializeSdf(body);
            const Vixen::SVO::Octree* oct = body.octree->getOctree();

            if (oct != nullptr) {
                Vixen::SVO::BakeAndAttachMipPool(*oct, ser);
                Vixen::SVO::MipPool mipPool = Vixen::SVO::BakeMipPool(*oct, ser);

                // A FINE cut (test_light_tree.cpp's own CutAggregatePowerApproximatesBruteForceLeafSum
                // tolerance test uses powerThreshold=0.001 for exactly this reason): the cut decision
                // compares against MipSample::value (a per-voxel MEAN intensity, ~1-4 for this scene's
                // emit function), NOT an aggregate/summed power -- a coarse threshold like 50.0 (the
                // OTHER test_light_tree.cpp test, "bounded non-empty cut", which only asserts the cut
                // is non-empty/smaller than the raw voxel count, not that it approximates the true
                // total) prunes at the ROOT immediately (root mean-intensity < 50), collapsing the
                // whole scene into ONE grossly-under-representative node. This gate needs the cut's
                // AGGREGATE power to actually approximate BruteForceTotalEmissivePower (per
                // LightTreeCutTotalPower's own contract), so it must use the FINE threshold.
                Vixen::SVO::LightTreeCutParams cutParams;
                cutParams.powerThreshold = 0.001f;
                std::vector<Vixen::SVO::LightTreeNode> cut =
                    Vixen::SVO::BuildLightTreeCut(*oct, ser, mipPool, kN, cutParams);

                // Instance transform: SAME single-body placement pattern VIXEN_TIER_CROSSING_DEMO
                // uses (renderScale=4.8, world diameter 48, world span = renderScale*[0,10] per
                // SerializeSdf's kWorldGridSize -- NOT n=32; gridMin/gridMax are never read by
                // this shader). Centered at the default camera's frame center (64,64,64).
                constexpr float kRenderScale = 4.8f;
                constexpr float kWorldGridSize = 10.0f;  // SerializeSdf's fixed config-local-world span
                constexpr float kHalf = 5.0f * kRenderScale;
                const glm::vec3 instWorldPos(64.0f - kHalf, 64.0f - kHalf, 64.0f - kHalf);

                // LightTreeBufferNode.h's scope note: worldPos in the cut is grid space [0,n) --
                // transform to world space using the SAME p_world = p_base*renderScale + worldPos
                // TraceWorld.glsl uses, where p_base = p_grid / n * kWorldGridSize (the shader's
                // ACTUAL grid->config-local-world map, independent of the bake's own n).
                std::vector<Vixen::SVO::LightTreeNode> worldCut;
                worldCut.reserve(cut.size());
                for (const auto& node : cut) {
                    Vixen::SVO::LightTreeNode w = node;
                    const glm::vec3 pBase = (node.worldPos / static_cast<float>(kN)) * kWorldGridSize;
                    w.worldPos = pBase * kRenderScale + instWorldPos;
                    w.worldExtent = (node.worldExtent / static_cast<float>(kN)) * kWorldGridSize * kRenderScale;
                    worldCut.push_back(w);
                }

                if (auto* lightTreeInst = static_cast<LightTreeBufferNode*>(renderGraph->GetInstance(lightTreeBufferNode))) {
                    lightTreeInst->SetLightTreeCut(worldCut);
                }

                // This gate's GPU-side RIS estimator computes Sum_i(power_i/dist_i^2) -- the SAME
                // rendering-equation-shaped quantity DirectLighting.comp's lightTreeNodePHat
                // evaluates (power = intensity*coverage*extent^3, matching LightTree.h's
                // LightTreeCutTotalPower per-node power definition) -- NOT BruteForceTotalEmissive
                // Power (a raw sum-of-intensity with no distance falloff; a different estimator
                // target, the M3 cut-approximation check's own quantity). The brute-force reference
                // is therefore computed PER-PIXEL (see VulkanGraphApplication.cpp's readback tick
                // hook), evaluated at each pixel's OWN HitRecord.worldPos over this SAME world-
                // transformed cut -- an EARLIER version of this gate instead evaluated a single
                // hand-picked "canonical" shading point, which a live-gate DIAG dump proved wrong
                // (recomputed pHat vs the shader's own targetPdf, for the SAME chosen node, varied
                // 28x-71x across different pixels/nodes -- the signature of a geometric mismatch,
                // not a uniform scale bug). Per-pixel evaluation removes that assumption entirely.
                mainLogger->Info("[BuildRenderGraph] VIXEN_RESTIR_GATE_DEMO: cut=" + std::to_string(cut.size()) + " nodes");

                // Stash the world-transformed cut where VulkanGraphApplication::Update's tick hook
                // can read it for its own per-pixel brute-force recomputation (a plain static --
                // this demo scene is process-lifetime-scoped, same as every other VIXEN_*_DEMO
                // env-gated block in this file).
                extern std::vector<Vixen::SVO::LightTreeNode>* g_restirGateWorldCut;
                static std::vector<Vixen::SVO::LightTreeNode> worldCutStash = worldCut;
                g_restirGateWorldCut = &worldCutStash;
            } else {
                mainLogger->Error("[BuildRenderGraph] VIXEN_RESTIR_GATE_DEMO: body octree is null -- gate scene not built");
            }

            std::vector<const Vixen::SVO::SdfBodyOctree*> octreesForCat = {&body};
            Vixen::SVO::ConcatenatedOctrees cat = Vixen::SVO::ConcatenateSdfWithMips(octreesForCat);

            if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                bodyScene->SetRecipePool(std::move(cat));

                constexpr float kRenderScale = 4.8f;
                constexpr float kHalf = 5.0f * kRenderScale;
                Vixen::SVO::BodyInstanceGpu inst{};
                inst.worldPos[0]  = 64.0f - kHalf;
                inst.worldPos[1]  = 64.0f - kHalf;
                inst.worldPos[2]  = 64.0f - kHalf;
                inst.renderScale  = kRenderScale;
                inst.color[0]     = 1.0f;
                inst.color[1]     = 1.0f;
                inst.color[2]     = 1.0f;
                inst.octreeIndex  = 0u;
                inst.providerKind = 0u;  // PROVIDER_STORED
                inst.recipeId     = 0u;
                bodyScene->SetInstances({inst});
                mainLogger->Info("[BuildRenderGraph] VIXEN_RESTIR_GATE_DEMO: seeded 1 Stored-SDF body instance");
            }
        } else if (std::getenv("VIXEN_SHADOW_DEMO")) {
            // VIXEN_SHADOW_DEMO — Sampled Lighting Inc1 M4 live gate: two Procedural
            // spheres positioned so the smaller "occluder" sits directly between the
            // larger "target" sphere's CAMERA-FACING surface and the default directional
            // light (normalize(1,1,-1) — see LightingConfigNode's default), casting a
            // visible shadow onto the target's visible hemisphere. A third sphere far to
            // the side stays fully lit (no occluder in its light path) as an in-frame
            // A/B control.
            //
            // Geometry: the default camera sits at (64,64,300) looking toward -Z at the
            // scene centre (64,64,64) (see PARAM_CAMERA_*/PARAM_ORBIT_* above) — the
            // camera-visible hemisphere of any body at (64,64,64) is its +Z-facing side.
            // light direction (1,1,-1) points from a surface point TOWARD the light (the
            // Light.direction_or_position convention — see Lighting.glsl's data-driven
            // overload) — its -Z component means the light itself sits toward -Z, i.e.
            // BEHIND the camera, so a +Z-facing point's dot(normal,lightDir) is positive
            // and it DOES get lit (normal ~=(0,0,1), lightDir has -Z component but also
            // +X/+Y, dot = -(-1)/sqrt3 + 0 + 0 ... to guarantee a clean positive NdotL on
            // the exact camera-facing point (0,0,1) normal, use dot((0,0,1),(1,1,-1)) =
            // -1/sqrt3 < 0 — NEGATIVE, meaning the dead-center camera-facing point is
            // actually NOT lit by this light. Placing the occluder to block a grazing
            // lit point instead: the point offset toward (+1,+1,0) from centre (normal
            // (1,1,0)/sqrt2) has dot with lightDir = (1+1+0)/(sqrt2*sqrt3) > 0 — lit and
            // camera-visible (still has positive Z-ish visibility at this camera
            // distance/FOV). Occluder sits between THAT point and the light.
            constexpr float kTargetRadius   = 24.0f;
            constexpr float kOccluderRadius = 8.0f;
            constexpr float kOccluderGap    = 3.0f;  // standoff so the occluder doesn't contain surfacePoint
            auto placeProceduralSphere = [&](float cx, float cy, float cz, float radius,
                                             float r, float g, float b) {
                Vixen::SVO::BodyInstanceGpu inst{};
                inst.worldPos[0] = cx; inst.worldPos[1] = cy; inst.worldPos[2] = cz;
                inst.renderScale = 1.0f;
                inst.color[0] = r; inst.color[1] = g; inst.color[2] = b;
                inst.octreeIndex = 0u;
                inst.providerKind = 1u;  // PROVIDER_PROCEDURAL
                inst.recipeId = 0u;      // sphere
                inst.recipeParams[0] = radius;
                inst.recipeParams[1] = 0.0f;
                inst.recipeParams[2] = 0.0f;
                return inst;
            };
            const glm::vec3 targetCenter(64.0f, 64.0f, 64.0f);
            const glm::vec3 lightDir = glm::normalize(glm::vec3(1.0f, 1.0f, -1.0f));
            // The grazing lit-and-visible point: centre + radius * normalize(1,1,0.3) —
            // mostly toward +X/+Y (lit by lightDir, NdotL~0.68) with a touch of +Z
            // (camera-visible at this FOV/distance, dot(normal,viewDir)~0.11 > 0).
            const glm::vec3 litPointDir = glm::normalize(glm::vec3(1.0f, 1.0f, 0.3f));
            const glm::vec3 surfacePoint = targetCenter + litPointDir * kTargetRadius;
            // Occluder sits along lightDir from surfacePoint, offset by radius+gap so its
            // near edge (not its centre) is the one that meets the surface — verified via
            // the analytic ray-sphere test (t0~2.97>0 along [surfacePoint,lightDir]).
            const glm::vec3 occluderCenter = surfacePoint + lightDir * (kOccluderRadius + kOccluderGap);
            std::vector<Vixen::SVO::BodyInstanceGpu> shadowBodies = {
                placeProceduralSphere(targetCenter.x, targetCenter.y, targetCenter.z,
                                      kTargetRadius, 0.9f, 0.9f, 0.9f),                 // target
                placeProceduralSphere(occluderCenter.x, occluderCenter.y, occluderCenter.z,
                                      kOccluderRadius, 0.2f, 0.2f, 0.2f),               // occluder
                placeProceduralSphere(150.0f, 64.0f, 64.0f, kTargetRadius, 0.9f, 0.9f, 0.9f), // lit control
            };
            if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                bodyScene->SetInstances(std::move(shadowBodies));
                mainLogger->Info("[BuildRenderGraph] VIXEN_SHADOW_DEMO: seeded target+occluder+litControl body instances");
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
    // The chain is now march -> DirectLighting -> BlitNode -> sky-projection -> UI. BlitNode leaves
    // the swapchain image in GENERAL (PARAM_LEAVE_IMAGE_IN_GENERAL=true, set above beside its own
    // parameters); the sky-projection pass LOADs+draws+leaves it in GENERAL (below); the UI render
    // pass LOADs that image, draws the HUD over it, and owns the →PRESENT_SRC transition.
    // Sampled Lighting Inc3 M1 (KI-018): the march itself no longer writes any presentable image —
    // PARAM_WRITES_NO_IMAGE=true skips its (now-obsolete) SWAPCHAIN_INFO layout transitions
    // entirely (see that param's doc comment); PARAM_LEAVE_IMAGE_IN_GENERAL stays true because the
    // march is still the frame's FIRST compute submit and must not own the in-flight fence (BlitNode
    // does, as the last compute-queue submit before the sky/UI graphics passes) — the two params are
    // orthogonal (image-ownership vs fence-ownership).
    dispatch->SetParameter(ComputeDispatchNodeConfig::PARAM_LEAVE_IMAGE_IN_GENERAL, true);
    dispatch->SetParameter(ComputeDispatchNodeConfig::PARAM_WRITES_NO_IMAGE, true);

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

    // Sampled Lighting Inc0 M3: lighting config node connections (same ring pattern as
    // bodyOctreeSceneNode above — device + per-frame index so ExecuteImpl picks which
    // ring slot to upload LightingConfig into).
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  lightingConfigNode, LightingConfigNodeConfig::VULKAN_DEVICE_IN)
         .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  lightingConfigNode, LightingConfigNodeConfig::CURRENT_FRAME_INDEX);

    // Sampled Lighting Inc1 M4: shadow config node connections (same ring pattern as
    // lightingConfigNode above).
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  shadowConfigNode, ShadowConfigNodeConfig::VULKAN_DEVICE_IN)
         .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  shadowConfigNode, ShadowConfigNodeConfig::CURRENT_FRAME_INDEX);

    // Sampled Lighting Inc2 M1/M2: accumulation config node connections (same ring pattern as
    // shadowConfigNode above). CAMERA_DATA (M2) feeds the node's own reset-on-motion frame
    // counter — see AccumulationConfigNode.h's file header for why the counter lives here
    // rather than on CameraData itself.
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  accumulationConfigNode, AccumulationConfigNodeConfig::VULKAN_DEVICE_IN)
         .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  accumulationConfigNode, AccumulationConfigNodeConfig::CURRENT_FRAME_INDEX)
         .Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                  accumulationConfigNode, AccumulationConfigNodeConfig::CAMERA_DATA);

    // Sampled Lighting Inc2 M1: accumulation history image connections — device + command pool
    // drive allocation + the one-shot UNDEFINED->GENERAL transition; extent follows the RENDER
    // target (renderTargetNode's WIDTH_OUT/HEIGHT_OUT, not the window), mirroring
    // pickIdTargetNode's own extent-follow wiring above so the history image always matches
    // outputImage's real per-dispatch extent (including under render-scale <1.0).
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  accumulationHistoryNode, AccumulationHistoryNodeConfig::VULKAN_DEVICE_IN)
         .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL,
                  accumulationHistoryNode, AccumulationHistoryNodeConfig::COMMAND_POOL)
         .Connect(renderTargetNode, RenderTargetNodeConfig::WIDTH_OUT,
                  accumulationHistoryNode, AccumulationHistoryNodeConfig::WIDTH)
         .Connect(renderTargetNode, RenderTargetNodeConfig::HEIGHT_OUT,
                  accumulationHistoryNode, AccumulationHistoryNodeConfig::HEIGHT);

    // Sampled Lighting Inc3 M2 (KI-023): worldPos history image connections — identical
    // device/command-pool/extent-follow wiring as accumulationHistoryNode above.
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  worldPosHistoryNode, WorldPosHistoryNodeConfig::VULKAN_DEVICE_IN)
         .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL,
                  worldPosHistoryNode, WorldPosHistoryNodeConfig::COMMAND_POOL)
         .Connect(renderTargetNode, RenderTargetNodeConfig::WIDTH_OUT,
                  worldPosHistoryNode, WorldPosHistoryNodeConfig::WIDTH)
         .Connect(renderTargetNode, RenderTargetNodeConfig::HEIGHT_OUT,
                  worldPosHistoryNode, WorldPosHistoryNodeConfig::HEIGHT);

    // Sampled Lighting Inc2 M3: prev-frame camera config node connections (same ring pattern
    // as accumulationConfigNode above). PREV_VIEW_PROJ comes from CameraNode's own retained-
    // last-frame matrix (see CameraNode::ExecuteImpl/UpdateCameraData).
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  prevCameraConfigNode, PrevCameraConfigNodeConfig::VULKAN_DEVICE_IN)
         .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  prevCameraConfigNode, PrevCameraConfigNodeConfig::CURRENT_FRAME_INDEX)
         .Connect(cameraNode, CameraNodeConfig::PREV_VIEW_PROJ,
                  prevCameraConfigNode, PrevCameraConfigNodeConfig::PREV_VIEW_PROJ);

    // Sampled Lighting Inc3 M3: reservoir config node connections (same ring pattern as
    // shadowConfigNode above). M3 scaffolding only — no shader consumes this buffer yet.
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  reservoirConfigNode, ReservoirConfigNodeConfig::VULKAN_DEVICE_IN)
         .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  reservoirConfigNode, ReservoirConfigNodeConfig::CURRENT_FRAME_INDEX);

    // Sampled Lighting Inc3 M4: light-tree buffer node connections (same ring pattern as
    // reservoirConfigNode above). Content pushed via LightTreeBufferNode::SetLightTreeCut
    // (host -> node seam); empty by default (nodeCount=0 -> RIS loop is a no-op, the
    // byte-identity escape hatch).
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  lightTreeBufferNode, LightTreeBufferNodeConfig::VULKAN_DEVICE_IN)
         .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  lightTreeBufferNode, LightTreeBufferNodeConfig::CURRENT_FRAME_INDEX);

    // Sampled Lighting Inc3 M4: reservoir CURRENT/PREVIOUS ping-pong SSBOs — device +
    // extent-driven sizing from renderTargetNode's own RENDER_TARGET output, same
    // pattern as hitRecordBufferNode (one Vixen::Gpu::ReservoirRecord per pixel of the
    // offscreen render target, always matching outputImage's real per-dispatch extent
    // including under render-scale).
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  reservoirBufferA, StorageBufferNodeConfig::VULKAN_DEVICE_IN)
         .Connect(renderTargetNode, RenderTargetNodeConfig::RENDER_TARGET,
                  reservoirBufferA, StorageBufferNodeConfig::SWAPCHAIN_INFO)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  reservoirBufferB, StorageBufferNodeConfig::VULKAN_DEVICE_IN)
         .Connect(renderTargetNode, RenderTargetNodeConfig::RENDER_TARGET,
                  reservoirBufferB, StorageBufferNodeConfig::SWAPCHAIN_INFO);

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
    // accumFrameCount (binding 12, Sampled Lighting Inc2 M2): consecutive-static-camera frame
    // counter from AccumulationConfigNode's own reset-on-motion tracking; drives the shader's
    // converging-1/N accumulate-seam alpha.
    batch.Connect(accumulationConfigNode, AccumulationConfigNodeConfig::FRAME_COUNTER,
                          pushConstantGatherer, 12,  // push constant field 12: uint accumFrameCount
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

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

    // Sampled Lighting Inc0 M3: Binding 16: LightingConfig SSBO (single record, re-uploaded
    // per-frame from LightingConfigNode's ring). Default content = one directional light
    // matching Lighting.glsl's previous hardcoded default (zero-visual-delta gate).
    batch.Connect(lightingConfigNode, LightingConfigNodeConfig::LIGHTING_CONFIG_BUFFER,
                          descriptorGatherer, 16,  // Binding 16: LightingConfigSSBO
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] Connected lighting config at binding 16 (Sampled Lighting Inc0 M3)");
    }

    // Sampled Lighting Inc1 M3: Binding 17: HitRecord SSBO. Device input + extent-driven sizing
    // from renderTargetNode's own RENDER_TARGET output (NOT the raw swapchain) — so this buffer
    // always matches outputImage's real per-dispatch extent (including under render-scale <1.0),
    // same as descriptorGatherer binding 0 below. This makes hitRecordBufferNode a transitive
    // dependent of renderTargetNode and rides the identical resize->recompile cascade.
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  hitRecordBufferNode, StorageBufferNodeConfig::VULKAN_DEVICE_IN)
         .Connect(renderTargetNode, RenderTargetNodeConfig::RENDER_TARGET,
                  hitRecordBufferNode, StorageBufferNodeConfig::SWAPCHAIN_INFO);

    batch.Connect(hitRecordBufferNode, StorageBufferNodeConfig::STORAGE_BUFFER,
                          descriptorGatherer, 17,  // Binding 17: HitRecordBuffer
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] Connected HitRecord SSBO at binding 17 (Sampled Lighting Inc1 M3)");
    }

    // Sampled Lighting Inc1 M4: Binding 18: ShadowConfig SSBO (single record, re-uploaded
    // per-frame from ShadowConfigNode's ring). Default content = enabled hard shadows,
    // whole-scene reach, tuned bias (see ShadowConfigNode.cpp's MakeDefaultShadowConfig).
    batch.Connect(shadowConfigNode, ShadowConfigNodeConfig::SHADOW_CONFIG_BUFFER,
                          descriptorGatherer, 18,  // Binding 18: ShadowConfigSSBO
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] Connected shadow config at binding 18 (Sampled Lighting Inc1 M4)");
    }

    // Sampled Lighting Inc2 M1: Binding 19: AccumulationConfig SSBO (single record, re-uploaded
    // per-frame from AccumulationConfigNode's ring). Default content = enabled=0 (pure
    // passthrough — this milestone's byte-identity gate vs Inc1).
    batch.Connect(accumulationConfigNode, AccumulationConfigNodeConfig::ACCUMULATION_CONFIG_BUFFER,
                          descriptorGatherer, 19,  // Binding 19: AccumulationConfigSSBO
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] Connected accumulation config at binding 19 (Sampled Lighting Inc2 M1)");
    }

    // Sampled Lighting Inc2 M1: Binding 20: historyImage (persistent R8G8B8A8_UNORM storage
    // image, AccumulationHistoryNode). Declared in the shader but not yet read/written this
    // milestone (M2 consumes it) — a pure plumbing wire. Execute-only, mirroring
    // pickIdTargetNode's own binding-9 storage-image wiring above (re-emitted each frame; no
    // compile-time dependency edge needed beyond the initial Compile-time publish).
    batch.Connect(accumulationHistoryNode, AccumulationHistoryNodeConfig::HISTORY_IMAGE_VIEW,
                          descriptorGatherer, 20,  // Binding 20: historyImage
                          SlotRoleModifier(SlotRole::Execute));

    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] Connected accumulation history image at binding 20 (Sampled Lighting Inc2 M1)");
    }

    // Sampled Lighting Inc2 M3: Binding 21: PrevCameraConfig SSBO (single record, re-uploaded
    // per-frame from PrevCameraConfigNode's ring). Declared in the shader but not yet read
    // this milestone (M4 consumes it for reprojection) — a pure plumbing wire, mirroring
    // binding 19/20's own M1 plumbing-only precedent.
    batch.Connect(prevCameraConfigNode, PrevCameraConfigNodeConfig::PREV_CAMERA_CONFIG_BUFFER,
                          descriptorGatherer, 21,  // Binding 21: PrevCameraConfigSSBO
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] Connected prev-camera config at binding 21 (Sampled Lighting Inc2 M3)");
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
    // Sampled Lighting Inc3 M1 (KI-018): computeDispatch (the march) is DELIBERATELY NOT wired to
    // RENDER_TARGET_INFO anymore — it no longer writes or blits the render target (DirectLighting
    // does, via its own IMAGE_WRITE sync slot further below); wiring RENDER_TARGET_INFO here would
    // fire ComputeDispatchNode's blit path PREMATURELY, before DirectLighting has shaded anything.
    // See PARAM_WRITES_NO_IMAGE (set above) for how the march's now-untouched SWAPCHAIN_INFO image
    // is kept safe without RENDER_TARGET_INFO.
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  renderTargetNode, RenderTargetNodeConfig::VULKAN_DEVICE_IN)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  renderTargetNode, RenderTargetNodeConfig::EXTENT_SOURCE);

    // M-wire/M4: the march's binding-0 outputImage is kept wired to the render target's current
    // view SOLELY so imageSize(outputImage) resolves the dispatch's pixel bounds (never
    // imageStore'd post-split — see PARAM_WRITES_NO_IMAGE's doc comment). The real write happens
    // on DirectLighting's OWN gatherer binding 0, wired further below.
    batch.Connect(renderTargetNode, RenderTargetNodeConfig::CURRENT_VIEW,
                          descriptorGatherer, 0,  // outputImage at binding 0 (extent query only)
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

    // ===================================================================
    // Sampled Lighting Inc3 M1 (KI-018): DirectLightingNode + BlitNode wiring. The chain is now
    // march (HitRecord+pick-ID only) -> DirectLighting (shades, writes the render target) ->
    // BlitNode (blits render target -> swapchain) -> sky-projection -> UI -> present.
    // ===================================================================

    // DirectLighting's own descriptor path (shaderLib -> gatherer -> descSet -> pipeline), mirroring
    // the march's own wiring above and BuildFanInDemoGraph's wirePipeline helper.
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  directLightingShaderLib, ShaderLibraryNodeConfig::VULKAN_DEVICE_IN)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  directLightingDescriptorSet, DescriptorSetNodeConfig::VULKAN_DEVICE_IN)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  directLightingPipeline, ComputePipelineNodeConfig::VULKAN_DEVICE_IN)
         .Connect(directLightingShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  directLightingGatherer, DescriptorResourceGathererNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(directLightingGatherer, DescriptorResourceGathererNodeConfig::DESCRIPTOR_RESOURCES,
                  directLightingDescriptorSet, DescriptorSetNodeConfig::DESCRIPTOR_RESOURCES)
         .Connect(directLightingShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  directLightingDescriptorSet, DescriptorSetNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  directLightingDescriptorSet, DescriptorSetNodeConfig::SWAPCHAIN_INFO)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX,
                  directLightingDescriptorSet, DescriptorSetNodeConfig::IMAGE_INDEX)
         .Connect(directLightingShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  directLightingPipeline, ComputePipelineNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(directLightingDescriptorSet, DescriptorSetNodeConfig::DESCRIPTOR_SET_LAYOUT,
                  directLightingPipeline, ComputePipelineNodeConfig::DESCRIPTOR_SET_LAYOUT);

    // DirectLighting's own push-constant gatherer: SAME field sources as the march's own gatherer
    // above (cameraPos/time/cameraDir/fov/cameraUp/aspect/cameraRight/debugMode/raySizeCoef/
    // raySizeBias/instanceCount/debugTargetPixel/accumFrameCount) — DirectLighting.comp declares
    // the identical PushConstants block (shared via SceneBindings.glsl), but glslang reflects
    // push-constant RANGES per-COMPILED-shader (dead-code-eliminated fields differ), so a second
    // compiled program needs its own PushConstantGathererNode instance, not the march's.
    batch.Connect(directLightingShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  directLightingPushConstantGatherer, PushConstantGathererNodeConfig::SHADER_DATA_BUNDLE);
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          directLightingPushConstantGatherer, VoxelRayMarch::cameraPos::BINDING,
                          ExtractField(&CameraData::cameraPos, SlotRole::Execute));
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          directLightingPushConstantGatherer, VoxelRayMarch::cameraDir::BINDING,
                          ExtractField(&CameraData::cameraDir, SlotRole::Execute));
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          directLightingPushConstantGatherer, VoxelRayMarch::fov::BINDING,
                          ExtractField(&CameraData::fov, SlotRole::Execute));
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          directLightingPushConstantGatherer, VoxelRayMarch::cameraUp::BINDING,
                          ExtractField(&CameraData::cameraUp, SlotRole::Execute));
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          directLightingPushConstantGatherer, VoxelRayMarch::aspect::BINDING,
                          ExtractField(&CameraData::aspect, SlotRole::Execute));
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          directLightingPushConstantGatherer, VoxelRayMarch::cameraRight::BINDING,
                          ExtractField(&CameraData::cameraRight, SlotRole::Execute));
    batch.Connect(inputNode, InputNodeConfig::INPUT_STATE,
                          directLightingPushConstantGatherer, VoxelRayMarch::debugMode::BINDING,
                          ExtractField(&InputState::debugMode, SlotRole::Execute));
    if (tierCrossingLodCoefOverrideActive) {
        batch.Connect(tierCrossingLodCoefOverrideConstant, ConstantNodeConfig::OUTPUT,
                              directLightingPushConstantGatherer, 8,
                              SlotRoleModifier(SlotRole::Execute));
    } else {
        batch.Connect(raySizeCoefNode, RaySizeCoefNodeConfig::RAY_SIZE_COEF,
                              directLightingPushConstantGatherer, 8,
                              SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    }
    batch.Connect(raySizeBiasConstant, ConstantNodeConfig::OUTPUT,
                          directLightingPushConstantGatherer, 9,
                          SlotRoleModifier(SlotRole::Execute));
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::INSTANCE_COUNT,
                          directLightingPushConstantGatherer, 10,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(inputNode, InputNodeConfig::INPUT_STATE,
                          directLightingPushConstantGatherer, 11,
                          ExtractField(&InputState::lastClickPixel, SlotRole::Execute));
    batch.Connect(accumulationConfigNode, AccumulationConfigNodeConfig::FRAME_COUNTER,
                          directLightingPushConstantGatherer, 12,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    // DirectLighting's descriptor bindings: the scene SSBOs (octree/brick/instance/shell/mip/
    // tier-ref) are READ-ONLY in both the march and DirectLighting — read-read is not a hazard, so
    // they're wired the same way as the march's own gatherer (plain DescriptorResourceGathererNode
    // bindings, no sync slot needed), mirroring bindings 1/2/3/5/10/11/12/13/15/16/18/19/20/21
    // above. Binding 17 (HitRecord) is the genuine cross-submit hazard — wired below via the sync
    // slots, not here. Binding 0 (outputImage) is the genuine write hazard — also wired below via
    // IMAGE_WRITE, not here (it needs the render target's CURRENT view, same as the march's own
    // binding-0 wiring further up).
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::OCTREE_NODES_BUFFER,
                          directLightingGatherer, VoxelRayMarch::esvoNodes::BINDING,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::OCTREE_BRICKS_BUFFER,
                          directLightingGatherer, VoxelRayMarch::brickData::BINDING,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::OCTREE_MATERIALS_BUFFER,
                          directLightingGatherer, VoxelRayMarch::materials::BINDING,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::OCTREE_CONFIG_BUFFER,
                          directLightingGatherer, 5,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::INSTANCE_BUFFER,
                          directLightingGatherer, 10,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::SHELL_DATA_BUFFER,
                          directLightingGatherer, 11,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::SHELL_LOOKUP_BUFFER,
                          directLightingGatherer, 12,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::OCTREE_MIPPOOL_BUFFER,
                          directLightingGatherer, 13,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::OCTREE_TIERREFTABLE_BUFFER,
                          directLightingGatherer, 15,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(lightingConfigNode, LightingConfigNodeConfig::LIGHTING_CONFIG_BUFFER,
                          directLightingGatherer, 16,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(shadowConfigNode, ShadowConfigNodeConfig::SHADOW_CONFIG_BUFFER,
                          directLightingGatherer, 18,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(accumulationConfigNode, AccumulationConfigNodeConfig::ACCUMULATION_CONFIG_BUFFER,
                          directLightingGatherer, 19,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(accumulationHistoryNode, AccumulationHistoryNodeConfig::HISTORY_IMAGE_VIEW,
                          directLightingGatherer, 20,
                          SlotRoleModifier(SlotRole::Execute));
    batch.Connect(prevCameraConfigNode, PrevCameraConfigNodeConfig::PREV_CAMERA_CONFIG_BUFFER,
                          directLightingGatherer, 21,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    // Sampled Lighting Inc3 M2 (KI-023): Binding 22: worldPosImage (persistent rgba32f storage
    // image, WorldPosHistoryNode). Read+written by DirectLighting.comp itself in the SAME
    // dispatch it also reads/writes historyImage in (both are the accumulate seam's own
    // read-then-write-back pattern, same shape as PickIdTargetNode's binding-9 self-contained
    // read/write) — no cross-submit hazard, no sync slot needed, mirrors binding 20's own
    // Execute-only wiring exactly.
    batch.Connect(worldPosHistoryNode, WorldPosHistoryNodeConfig::WORLDPOS_IMAGE_VIEW,
                          directLightingGatherer, 22,
                          SlotRoleModifier(SlotRole::Execute));

    // Sampled Lighting Inc3 M3: Binding 23: ReservoirConfig SSBO (single record, re-uploaded
    // per-frame from ReservoirConfigNode's ring). Declared in the shader but not yet read
    // this milestone (M4/M5 wire the reservoir/RIS shading logic) — a pure plumbing wire,
    // mirroring binding 21's own M3-predecessor plumbing-only precedent (PrevCameraConfig).
    batch.Connect(reservoirConfigNode, ReservoirConfigNodeConfig::RESERVOIR_CONFIG_BUFFER,
                          directLightingGatherer, 23,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    // Sampled Lighting Inc3 M4: Binding 24: LightTreeBuffer SSBO (mip-cut light-tree, re-uploaded
    // per-frame from LightTreeBufferNode's ring). RIS candidate generation samples this — read-only
    // in DirectLighting.comp, so plain Dependency|Execute wiring (like binding 23) is sufficient.
    batch.Connect(lightTreeBufferNode, LightTreeBufferNodeConfig::LIGHT_TREE_BUFFER,
                          directLightingGatherer, 24,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    // Sampled Lighting Inc3 M4: Bindings 25/26: reservoir CURRENT/PREVIOUS ping-pong SSBOs
    // (Vixen::Gpu::ReservoirRecord[], one per pixel). BOTH buffers are ALWAYS bound at BOTH
    // bindings 25/26 — DirectLighting.comp itself picks which is "current" (write) vs "previous"
    // (read) each frame via reservoirConfig.frameParity&1 (see ReservoirConfig.cs's own doc
    // comment), so no CPU-side rewiring/swap is needed frame-to-frame.
    //
    // No sync slot (Execute-only, like worldPosHistoryImage@22 and historyImage@20): this is a
    // SAME-NODE persistent resource read-of-own-previous-write, not a cross-node hazard — frame
    // N's dispatch fully completes (FrameSyncNode's in-flight-fence CPU-GPU wait) before frame N+1's
    // dispatch begins, so there is no genuine concurrent-GPU-execution race across frames on the
    // SAME node's own resource (the M2 Progress Log documents this exact precedent for
    // historyImage/worldPosHistoryImage: "a genuine but benign intra-dispatch cross-invocation
    // race — NOT a new hazard class"). Only the intra-frame per-invocation read/write ordering
    // within a single dispatch is in play, which is benign here since RIS/WRS per-pixel logic is
    // independent across pixels (each invocation only touches ITS OWN pixel's reservoir record).
    batch.Connect(reservoirBufferA, StorageBufferNodeConfig::STORAGE_BUFFER,
                          directLightingGatherer, 25,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(reservoirBufferB, StorageBufferNodeConfig::STORAGE_BUFFER,
                          directLightingGatherer, 26,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    // Binding 0 (outputImage): DirectLighting is the genuine writer now (the march never
    // imageStore's it — see PARAM_WRITES_NO_IMAGE's doc comment). Same renderTargetNode::
    // CURRENT_VIEW source the march used to bind here pre-split.
    batch.Connect(renderTargetNode, RenderTargetNodeConfig::CURRENT_VIEW,
                          directLightingGatherer, 0,
                          SlotRoleModifier(SlotRole::Execute));

    // Binding 17 (HitRecord): DirectLighting READS what the march WROTE — the genuine cross-submit
    // hazard this whole milestone exists to correctly bake. Descriptor binding (plain gatherer,
    // Dependency|Execute) + the BUFFER_WRITE/BUFFER_READ_A sync-slot pair further below (the
    // auto-sync hazard DECLARATION — see ComputeStageNodeConfig's own doc comment on the split
    // between descriptor binding and sync-slot declaration).
    batch.Connect(hitRecordBufferNode, StorageBufferNodeConfig::STORAGE_BUFFER,
                          directLightingGatherer, 17,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    // --- DirectLightingNode (ComputeStageNode) common inputs — mirrors BuildFanInDemoGraph's
    // wireStageCommon shape. NOT swapchain-adjacent: no SWAPCHAIN_INFO connection (isConsumer=false
    // was already set above), IMAGE_WRITE carries the render-target write instead (below). ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  directLightingNode, ComputeStageNodeConfig::VULKAN_DEVICE_IN)
         .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL,
                  directLightingNode, ComputeStageNodeConfig::COMMAND_POOL)
         .Connect(directLightingPipeline, ComputePipelineNodeConfig::PIPELINE,
                  directLightingNode, ComputeStageNodeConfig::COMPUTE_PIPELINE)
         .Connect(directLightingPipeline, ComputePipelineNodeConfig::PIPELINE_LAYOUT,
                  directLightingNode, ComputeStageNodeConfig::PIPELINE_LAYOUT)
         .Connect(directLightingDescriptorSet, DescriptorSetNodeConfig::DESCRIPTOR_SETS,
                  directLightingNode, ComputeStageNodeConfig::DESCRIPTOR_SETS)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX,
                  directLightingNode, ComputeStageNodeConfig::IMAGE_INDEX)
         .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  directLightingNode, ComputeStageNodeConfig::CURRENT_FRAME_INDEX)
         .Connect(frameSyncNode, FrameSyncNodeConfig::IN_FLIGHT_FENCE,
                  directLightingNode, ComputeStageNodeConfig::IN_FLIGHT_FENCE)
         .Connect(frameSyncNode, FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY,
                  directLightingNode, ComputeStageNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY)
         .Connect(swapChainNode, SwapChainNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY,
                  directLightingNode, ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY)
         .Connect(directLightingShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  directLightingNode, ComputeStageNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(directLightingPushConstantGatherer, PushConstantGathererNodeConfig::PUSH_CONSTANT_DATA,
                  directLightingNode, ComputeStageNodeConfig::PUSH_CONSTANT_DATA)
         .Connect(directLightingPushConstantGatherer, PushConstantGathererNodeConfig::PUSH_CONSTANT_RANGES,
                  directLightingNode, ComputeStageNodeConfig::PUSH_CONSTANT_RANGES)
         .Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_SEMAPHORE,
                  directLightingNode, ComputeStageNodeConfig::TIMELINE_SEMAPHORE_IN)
         .Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_FRAME_BASE,
                  directLightingNode, ComputeStageNodeConfig::TIMELINE_FRAME_BASE_IN);

    // --- Sync slots: the hazard-correlation identity. ---
    // HitRecord: march's ComputeDispatchNodeConfig::BUFFER_WRITE slot (unaffected by
    // M5 — a DIFFERENT config, not migrated) <-> DirectLighting's BUFFER_READ_ARRAY (M5:
    // generalized from the old fixed BUFFER_READ_A slot via directLightingReadGatherer).
    // Same hitRecordBufferNode::STORAGE_BUFFER Resource* feeds both the march's write
    // slot AND the gatherer's single variadic entry, whose preserved constituent
    // identity (BufferSyncGathererNode::hazardConstituents_) is what lets
    // ResourceAccessTracker::AddNode correlate DirectLighting's read against the SAME
    // Resource* the march wrote — bakes the march->DirectLighting edge exactly as
    // before.
    batch.Connect(hitRecordBufferNode, StorageBufferNodeConfig::STORAGE_BUFFER,
                  computeDispatch, ComputeDispatchNodeConfig::BUFFER_WRITE, SlotRoleModifier(SlotRole::Execute));
    // execDep (Dependency|Execute), not Execute-only: VariadicConnectionRule only
    // populates a variadic slot's actual Resource* via a PostCompile hook when the
    // connection carries SlotRole::Dependency — see BuildFanInDemoGraph.cpp's own
    // identical fix + comment for the full mechanism.
    batch.Connect(hitRecordBufferNode, StorageBufferNodeConfig::STORAGE_BUFFER,
                  directLightingReadGatherer, 0, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(directLightingReadGatherer, BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                  directLightingNode, ComputeStageNodeConfig::BUFFER_READ_ARRAY, SlotRoleModifier(SlotRole::Execute));
    // Render target: DirectLighting's IMAGE_WRITE <-> BlitNode's IMAGE_READ (wired below), same
    // renderTargetNode::RENDER_TARGET Resource* on both — bakes the DirectLighting->BlitNode edge.
    batch.Connect(renderTargetNode, RenderTargetNodeConfig::RENDER_TARGET,
                  directLightingNode, ComputeStageNodeConfig::IMAGE_WRITE, SlotRoleModifier(SlotRole::Execute));

    // --- BlitNode: presentation-only blit of the render target to the swapchain. ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  blitNode, BlitNodeConfig::VULKAN_DEVICE_IN)
         .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL,
                  blitNode, BlitNodeConfig::COMMAND_POOL)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  blitNode, BlitNodeConfig::SWAPCHAIN_INFO)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX,
                  blitNode, BlitNodeConfig::IMAGE_INDEX)
         .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  blitNode, BlitNodeConfig::CURRENT_FRAME_INDEX)
         .Connect(frameSyncNode, FrameSyncNodeConfig::IN_FLIGHT_FENCE,
                  blitNode, BlitNodeConfig::IN_FLIGHT_FENCE)
         .Connect(swapChainNode, SwapChainNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY,
                  blitNode, BlitNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY)
         .Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_SEMAPHORE,
                  blitNode, BlitNodeConfig::TIMELINE_SEMAPHORE_IN)
         .Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_FRAME_BASE,
                  blitNode, BlitNodeConfig::TIMELINE_FRAME_BASE_IN);
    batch.Connect(renderTargetNode, RenderTargetNodeConfig::RENDER_TARGET,
                  blitNode, BlitNodeConfig::IMAGE_READ, SlotRoleModifier(SlotRole::Execute));
    // Ordering-only edge (BlitNode never waits it — see BlitNodeConfig's ORDERING_WAIT_SEMAPHORE
    // doc): establishes the DirectLighting-before-Blit TOPOLOGY so the scheduler's groupId-order
    // edge direction is correct (mirrors the sky-projection/UI ordering-edge convention below).
    batch.Connect(directLightingNode, ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                  blitNode, BlitNodeConfig::ORDERING_WAIT_SEMAPHORE);

    // P5b M3 (extended for Tiered ESVO Inc1 M3; Sampled Lighting Inc3 M1: chain now runs through
    // DirectLighting + BlitNode first): the march->DirectLighting->Blit->sky-projection->UI
    // ordering is carried by the baked timeline edges for GPU SYNC (memory visibility), but the
    // graph still needs the TOPOLOGY edges so the execution order (and hence the timeline edges
    // the scheduler bakes from them) follows that same sequence. The FrameSyncScheduler derives
    // edge DIRECTION from groupId order (== execution order); without these dependencies the
    // topological sort could place a later pass before an earlier one, baking the edges BACKWARDS,
    // tagging the wrong group as the swapchain present-signal, and leaving the presented image in
    // the wrong layout at the wrong point — VUID-...-01430-class bugs, VUID-vkCmdDraw-None-09600-
    // class bugs. So we keep these connections purely as ORDERING edges (their documented secondary
    // purpose, mirroring UIRenderNodeConfig's own SWAPCHAIN/COMPOSITE_WAIT convention exactly): the
    // binary semaphores they carry are INERT — the march no longer signals a real renderComplete in
    // composite (writesNoImage + leaveImageInGeneral), BlitNode's own renderComplete output is
    // real (it owns the fence) but SkyProjectionNode never WAITS its COMPOSITE_WAIT_SEMAPHORE
    // input, and UIRenderNode no longer waits compositeWait either (the M3 binary handoff was
    // dropped from its submit). With the edges in the right direction the scheduler bakes
    // march(HitRecord)->DirectLighting(GENERAL)->Blit(GENERAL)->sky-projection(GENERAL)->UI(GENERAL)
    // timeline edges, tags the UI group as present (its render pass owns GENERAL->PRESENT_SRC,
    // unchanged), and the timeline alone — not a binary handoff — orders every pass. WSI acquire
    // (march waits imageAvailable) and present (UI signals its uiComplete) stay binary, unchanged.
    batch.Connect(blitNode, BlitNodeConfig::RENDER_COMPLETE_SEMAPHORE,
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
