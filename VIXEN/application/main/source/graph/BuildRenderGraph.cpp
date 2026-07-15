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
#include "graph/CornellBoxSceneDefinition.h"  // Sampled Lighting Cornell Box Demo M1: shared scene-definition constants (M1+M2 both read this verbatim)
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
#include "Data/Nodes/ProbeGridConfigNodeConfig.h"      // Sampled Lighting Inc4 M2: ProbeGridConfig upload ring (M3-M6 scaffolding)
#include "Data/Nodes/ProbeAtlasNodeConfig.h"           // Sampled Lighting Inc4 M2: persistent DDGI probe atlas image
#include "Data/Nodes/ImageSyncGathererNodeConfig.h"    // Sampled Lighting Inc4 M1: variadic IRenderTarget* sync gatherer
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
#include "Nodes/ProbeGridConfigNode.h"      // Sampled Lighting Inc4 M2: ProbeGridConfig upload ring (M3-M6 scaffolding)
#include "Nodes/ProbeAtlasNode.h"           // Sampled Lighting Inc4 M2: persistent DDGI probe atlas image
#include "Nodes/ImageSyncGathererNode.h"    // Sampled Lighting Inc4 M1: variadic IRenderTarget* sync gatherer
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

namespace {
// Sampled Lighting Cornell Box Demo M2: never-baked proof for VIXEN_DDGI_CORNELL_VIRTUAL_DEMO's
// 8 RENDERED bodies (mirrors test_baked_vs_virtual_parity.cpp's own g_bakeCallCount technique).
// Deliberately does NOT count the light-tree's own small side bake (see the M2 scene block's
// header comment for why that bake is a separate, pre-existing, architecturally-required
// mechanism independent of body rendering) -- tracked in its own counter instead so the two
// concerns stay visibly distinct in both the log output and any future verification code.
uint32_t g_cornellVirtualLightTreeSideBakeCount = 0;

// ============================================================================
// Sampled Lighting Cornell Box Demo M3 (2026-07-14): ONE shared world-space
// SdfInstruction source for BOTH VIXEN_DDGI_CORNELL_BAKED_DEMO and
// VIXEN_DDGI_CORNELL_VIRTUAL_DEMO.
//
// WHY this exists (per the user's own reframing of the M3 geometry-fix round):
// prior to this, the two variants each independently constructed their OWN
// SdfInstruction/RoundedBox calls from CornellBoxSceneDefinition.h's shared
// NUMBERS, through two different placement-math conventions -- the baked path
// authored primitives in a per-body bake-grid-local frame pre-scaled by a
// subdivision factor (gridBoxAt), the virtual path authored primitives
// directly in true world-space units (worldBoxAt). This was a real
// architecture smell (independently-written transforms of the same source
// numbers, not one shared program run through two backends) and is very
// likely WHY the two variants exhibited two DIFFERENT symptoms (baked =
// overlapping/incoherent, virtual = gaps) rather than the same bug in both --
// each transform could go wrong in its own way. It also violated the plan
// doc's own Self-Review: "ideally visually identical is only actually
// enforced if both variants are baked FROM the same numbers."
//
// Resolution: author every body's SdfInstruction program HERE ONCE, entirely
// in true world-space units (matching the virtual path's own simpler
// worldBoxAt/worldSphereAt convention -- world p is sampled directly, no
// grid-local indirection needed at the INSTRUCTION level at all). The two
// backends then differ ONLY in how they get world-space samples to
// evalRecipe:
//   - VIRTUAL: traceUberRecipeBody already samples true world-space rayOrigin
//     directly (PROVIDER_PROCEDURAL, TraceWorld.glsl) -- splices this exact
//     program with zero transformation.
//   - BAKED: MakeCornellWorldSpaceEvalFn (below) is a small adapter that maps
//     a raw bake-grid coordinate to true world space via EXACTLY the same
//     formula the runtime shader's octree traversal already uses (world =
//     worldPos + (grid/n)*kWorldGridSize*renderScale, i.e. bodyWorldPos/
//     bodyRenderScale's own convention below), then evaluates this SAME
//     program. This decouples "how finely to sample" (brick-density /
//     subdivision -- a pure bake-grid-resolution choice, n and subdiv only
//     affect worldPos/renderScale) from "what shape to author" (now a single
//     source of truth, byte-identical between both backends) -- round 5's
//     brick-density fix (subdiv>1) and true world-space instruction sharing
//     are NOT in tension once subdivision lives purely in the grid<->world
//     mapping rather than being pre-baked into the instruction's own
//     authored half-extents/positions.
// ============================================================================

using Vixen::SVO::Recipe::SdfInstruction;
using Vixen::SVO::Recipe::SdfOpCode;

struct CornellWorldSpaceBody {
    const char* name;
    std::vector<SdfInstruction> prog;  // exactly one primitive, authored in TRUE world-space units
    glm::vec3 worldCenter;             // this body's true world-space center (== the primitive's own authored position)
    float boundRadius;                 // conservative bound-sphere radius around worldCenter, world units
    glm::vec3 color;
};

SdfInstruction CornellWorldBoxAt(glm::vec3 c, glm::vec3 he, float rounding) {
    SdfInstruction in{};
    in.opCode = static_cast<uint8_t>(SdfOpCode::RoundedBox);
    in.data[0] = he.x; in.data[1] = he.y; in.data[2] = he.z; in.data[3] = rounding;
    in.data[4] = c.x;  in.data[5] = c.y;  in.data[6] = c.z;
    return in;
}
SdfInstruction CornellWorldSphereAt(glm::vec3 c, float r) {
    SdfInstruction in{};
    in.opCode = static_cast<uint8_t>(SdfOpCode::Sphere);
    in.data[0] = c.x; in.data[1] = c.y; in.data[2] = c.z; in.data[3] = r;
    return in;
}

// Builds all 8 bodies (5 walls + 1 ceiling light + 2 objects), reading
// CornellBoxSceneDefinition.h's constants directly (caller must have
// `using namespace Vixen::App::CornellBox;` in scope, same convention every
// other Cornell demo block already follows). Wall wide-axis half-extents use
// kWallSpan (kb+kWallThickness, reaches the true outer corner so adjacent
// walls seal flush -- M3 round 1's gap-bug fix) with an asymmetric Z
// treatment on leftWall/rightWall/floor/ceiling (M3 round 1's own
// over-extension-past-the-open-face fix): extend kWallThickness/2 toward the
// closed -Z (back-wall) side only, leaving the open +Z (camera) side exactly
// at the interior boundary. backWall's own X/Y wide axes are both closed and
// stay symmetric.
std::vector<CornellWorldSpaceBody> BuildCornellWorldSpaceBodies() {
    using namespace Vixen::App::CornellBox;

    constexpr float kRounding = 0.15f;
    const float kb = kBoxHalfExtent;
    const float kWallSpan = kb + kWallThickness;
    const float kZWideHalfExtent = kb + kWallThickness * 0.5f;
    const float kZWideCenterOffset = -kWallThickness * 0.5f;

    const glm::vec3 kLeftWallWorldCenter(kBoxCenter.x - kb - kWallThickness, kBoxCenter.y, kBoxCenter.z + kZWideCenterOffset);
    const glm::vec3 kRightWallWorldCenter(kBoxCenter.x + kb + kWallThickness, kBoxCenter.y, kBoxCenter.z + kZWideCenterOffset);
    const glm::vec3 kBackWallWorldCenter(kBoxCenter.x, kBoxCenter.y, kBoxCenter.z - kb - kWallThickness);
    const glm::vec3 kFloorWorldCenter(kBoxCenter.x, kBoxCenter.y - kb - kWallThickness, kBoxCenter.z + kZWideCenterOffset);
    const glm::vec3 kCeilingWorldCenter(kBoxCenter.x, kBoxCenter.y + kb + kWallThickness, kBoxCenter.z + kZWideCenterOffset);

    // Bound-sphere radius per wall must cover the wall's own half-diagonal;
    // using kWallSpan for all three axes is a safe (slightly conservative)
    // upper bound valid for both the Z-wide walls and backWall alike (see
    // the baked path's own MakeCornellWorldSpaceEvalFn for why this matters
    // there too -- the bake's own AABB, not just the virtual path's culling,
    // must comfortably contain the true geometry plus band).
    const float kWallBoundRadius = glm::length(glm::vec3(kWallThickness, kWallSpan, kWallSpan)) + 1.0f;

    std::vector<CornellWorldSpaceBody> bodies;
    bodies.push_back({"leftWall",
        {CornellWorldBoxAt(kLeftWallWorldCenter, glm::vec3(kWallThickness, kWallSpan, kZWideHalfExtent), kRounding)},
        kLeftWallWorldCenter, kWallBoundRadius, kLeftWallColor});
    bodies.push_back({"rightWall",
        {CornellWorldBoxAt(kRightWallWorldCenter, glm::vec3(kWallThickness, kWallSpan, kZWideHalfExtent), kRounding)},
        kRightWallWorldCenter, kWallBoundRadius, kRightWallColor});
    bodies.push_back({"backWall",
        {CornellWorldBoxAt(kBackWallWorldCenter, glm::vec3(kWallSpan, kWallSpan, kWallThickness), kRounding)},
        kBackWallWorldCenter, kWallBoundRadius, kNeutralWallColor});
    bodies.push_back({"floor",
        {CornellWorldBoxAt(kFloorWorldCenter, glm::vec3(kWallSpan, kWallThickness, kZWideHalfExtent), kRounding)},
        kFloorWorldCenter, kWallBoundRadius, kNeutralWallColor});
    bodies.push_back({"ceiling",
        {CornellWorldBoxAt(kCeilingWorldCenter, glm::vec3(kWallSpan, kWallThickness, kZWideHalfExtent), kRounding)},
        kCeilingWorldCenter, kWallBoundRadius, kNeutralWallColor});
    bodies.push_back({"light",
        {CornellWorldBoxAt(kLightCenter, kLightHalfExtent, 0.05f)},
        kLightCenter, glm::length(kLightHalfExtent) + 1.0f, kLightColor});
    bodies.push_back({"sphereObj",
        {CornellWorldSphereAt(kSphereObjectCenter, kSphereObjectRadius)},
        kSphereObjectCenter, kSphereObjectRadius + 1.0f, kSphereObjectColor});
    bodies.push_back({"boxObj",
        {CornellWorldBoxAt(kBoxObjectCenter, kBoxObjectHalfExtent, kRounding)},
        kBoxObjectCenter, glm::length(kBoxObjectHalfExtent) + 1.0f, kBoxObjectColor});
    return bodies;
}

}  // namespace

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

    // Sampled Lighting Inc3 M5: SpatialReuseNode — the SECOND half of the M5 pass split.
    // Runs AFTER DirectLightingNode's own dispatch (RIS + temporal reservoir reuse only,
    // no image writes — see DirectLighting.comp's own file header for why M5 split it
    // again), reading back DirectLighting.comp's post-temporal reservoir writes for the
    // spatial-reuse neighbor search, then shading + owning outputImage/historyImage/
    // worldPosHistoryImage (moved here from DirectLightingNode). Own shaderLib/gatherer/
    // pushConstantGatherer/descSet/pipeline quintet — same "second compiled shader needs
    // its own instances" rationale as directLighting*'s own quintet (see that block's
    // comment above).
    NodeHandle spatialReuseShaderLib = renderGraph->AddNode<ShaderLibraryNodeType>("spatial_reuse_shader_lib");
    NodeHandle spatialReuseGatherer  = renderGraph->AddNode<DescriptorResourceGathererNodeType>("spatial_reuse_desc_gatherer");
    NodeHandle spatialReusePushConstantGatherer = renderGraph->AddNode<PushConstantGathererNodeType>("spatial_reuse_push_constant_gatherer");
    NodeHandle spatialReuseDescriptorSet = renderGraph->AddNode<DescriptorSetNodeType>("spatial_reuse_descriptors");
    NodeHandle spatialReusePipeline = renderGraph->AddNode<ComputePipelineNodeType>("spatial_reuse_pipeline");
    NodeHandle spatialReuseNode = renderGraph->AddNode<ComputeStageNodeType>("spatial_reuse");

    // Sampled Lighting Inc3 M5: array-hazard buffer-sync gatherers for the reservoir
    // ping-pong's genuine cross-dispatch hazard (see DirectLighting.comp's own file
    // header). DirectLightingNode writes BOTH reservoirBufferA and reservoirBufferB (which
    // one is "current" alternates per-frame at runtime via frameParity — the array holds
    // BOTH physical buffers so the tracker's array-hazard-constituent expansion sees both
    // regardless of which one this frame actually touches); SpatialReuseNode reads the
    // SAME two buffers. Two SEPARATE gatherer instances (write-side vs read-side), each
    // fed from the SAME two StorageBufferNode outputs — mirrors BuildFanInDemoGraph.cpp's
    // own "each connecting side gets its own gatherer instance" precedent.
    NodeHandle directLightingReservoirWriteGatherer = renderGraph->AddNode<BufferSyncGathererNodeType>("direct_lighting_reservoir_write_gatherer");
    NodeHandle spatialReuseReservoirReadGatherer = renderGraph->AddNode<BufferSyncGathererNodeType>("spatial_reuse_reservoir_read_gatherer");

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

    // Sampled Lighting Inc4 M2: ProbeGridConfig data (binding 28) -- DDGI probe-grid placement +
    // compute budget as drift-guarded data. Same per-frame ring upload pattern as
    // reservoirConfigNode above. M2 scaffolding only: probeGridEnabled=0 by default and nothing
    // reads this buffer yet (M3+ wires the probe-update pass that consumes it); this milestone's
    // render must stay byte-identical to Inc3.
    NodeHandle probeGridConfigNode = renderGraph->AddNode<ProbeGridConfigNodeType>("probe_grid_config");

    // Sampled Lighting Inc4 M2: persistent DDGI probe atlas images -- TWO separate ProbeAtlasNode
    // instances (irradiance + Chebyshev-visibility), per M1's own resolved finding
    // (ImageSyncGathererNodeConfig.h's file header) that real DDGI atlas layouts use DIFFERENT
    // per-probe texel resolutions for the two and cannot channel-pack into one image.
    //
    // Atlas layout: the 3D probe grid (countX*countY*countZ, default 8x8x8=512 probes from
    // ProbeGridConfigNode's own default) packs into a 2D texture using the standard DDGI/RTXGI
    // atlas convention (Majercik et al. JCGT 2019 sec 3; RTXGI SDK reference layout): columns
    // sweep the grid's X axis, rows sweep Y, and Z-slices tile across the texture width --
    // atlasWidth = countX * countY * texelsPerProbe, atlasHeight = countZ * texelsPerProbe. With
    // the default 8x8x8 grid: irradiance 8x8 texels/probe (incl. 1px border, low-frequency
    // hemispherical data, RTXGI's own irradiance-probe default) -> 8*8*8=512 x 8*8=64 =
    // 512x64; visibility 16x16 texels/probe (incl. border -- Chebyshev's inequality needs finer
    // angular sampling than irradiance because occlusion/leak-prevention accuracy is the whole
    // mechanism DDGI's reputation risk depends on, per Majercik et al. sec 3.3) -> 8*16*8=1024 x
    // 8*16=128 = 1024x128. Both are placeholder-but-cited numbers for M2's plumbing-only scope;
    // M6's real-GPU probe-ray-budget bench is the design's own flagged pass-2 decision point for
    // finalizing grid density (and therefore these atlas dimensions), not this milestone.
    constexpr uint32_t kProbeIrradianceTexelsPerProbe = 8;
    constexpr uint32_t kProbeVisibilityTexelsPerProbe  = 16;
    constexpr uint32_t kProbeGridDefaultCountX = 8, kProbeGridDefaultCountY = 8, kProbeGridDefaultCountZ = 8;
    constexpr uint32_t kProbeIrradianceAtlasWidth  = kProbeGridDefaultCountX * kProbeGridDefaultCountY * kProbeIrradianceTexelsPerProbe;
    constexpr uint32_t kProbeIrradianceAtlasHeight = kProbeGridDefaultCountZ * kProbeIrradianceTexelsPerProbe;
    constexpr uint32_t kProbeVisibilityAtlasWidth   = kProbeGridDefaultCountX * kProbeGridDefaultCountY * kProbeVisibilityTexelsPerProbe;
    constexpr uint32_t kProbeVisibilityAtlasHeight  = kProbeGridDefaultCountZ * kProbeVisibilityTexelsPerProbe;

    NodeHandle probeIrradianceAtlasNode = renderGraph->AddNode<ProbeAtlasNodeType>("probe_irradiance_atlas");
    NodeHandle probeVisibilityAtlasNode = renderGraph->AddNode<ProbeAtlasNodeType>("probe_visibility_atlas");

    // Sampled Lighting Inc4 M2: variadic image-array sync gatherer for the future probe-update
    // pass's IMAGE_WRITE_ARRAY slot (Inc4 M1) -- gathers BOTH atlas IRenderTarget* handles into
    // one array-typed input. No consuming ComputeStageNode exists yet this milestone (that's
    // M3); the gatherer is wired now so M3 only needs to add the compute pass itself, not this
    // plumbing.
    NodeHandle probeAtlasGatherer = renderGraph->AddNode<ImageSyncGathererNodeType>("probe_atlas_gatherer");

    // Sampled Lighting Inc4 M5: a SECOND ImageSyncGathererNode instance, fed from the SAME
    // two ProbeAtlasNode outputs, gathering the atlas IRenderTarget* handles for the READ
    // side (SpatialReuseShade.comp's shade-pass gather) — mirrors the established
    // BUFFER_WRITE_ARRAY/BUFFER_READ_ARRAY "two gatherer instances, same underlying source,
    // one feeds the writer's array slot, a separate one feeds the reader's array slot" shape
    // (see spatialReuseReservoirReadGatherer's own precedent). This is what lets the
    // scheduler bake a real probeUpdateNode(write)->spatialReuseNode(read) SyncEdge on each
    // atlas's own constituent Resource*.
    NodeHandle spatialReuseProbeAtlasReadGatherer = renderGraph->AddNode<ImageSyncGathererNodeType>("spatial_reuse_probe_atlas_read_gatherer");

    // Sampled Lighting Inc4 M3: ProbeUpdateNode — the probe-update compute pass itself
    // (ProbeUpdate.comp). Own shaderLib/gatherer/pushConstantGatherer/descSet/pipeline
    // quintet, mirroring directLighting*'s own "second compiled shader needs its own
    // instances" rationale (see that block's comment above) — this is a THIRD compiled
    // program (march / DirectLighting+SpatialReuse / ProbeUpdate), each with its own
    // reflected descriptor/push-constant layout. NOT swapchain-adjacent (isConsumer=false,
    // like DirectLightingNode) — this pass writes only the probe atlases via
    // IMAGE_WRITE_ARRAY (Inc4 M1), never the swapchain-derived render target.
    NodeHandle probeUpdateShaderLib = renderGraph->AddNode<ShaderLibraryNodeType>("probe_update_shader_lib");
    NodeHandle probeUpdateGatherer  = renderGraph->AddNode<DescriptorResourceGathererNodeType>("probe_update_desc_gatherer");
    NodeHandle probeUpdatePushConstantGatherer = renderGraph->AddNode<PushConstantGathererNodeType>("probe_update_push_constant_gatherer");
    NodeHandle probeUpdateDescriptorSet = renderGraph->AddNode<DescriptorSetNodeType>("probe_update_descriptors");
    NodeHandle probeUpdatePipeline = renderGraph->AddNode<ComputePipelineNodeType>("probe_update_pipeline");
    NodeHandle probeUpdateNode = renderGraph->AddNode<ComputeStageNodeType>("probe_update");

    // Sampled Lighting Inc3 M6: spatial-combine debug readback SSBO (binding 27) -- the
    // spatially-combined `current` reservoir SpatialReuseShade.comp computes exists only in
    // registers (reservoirBufferA/B are readonly that pass); this buffer gives the M6
    // full-stack gate a GPU-visible post-spatial-combine reservoir to read back, closing the
    // gap M5's own Progress Log flagged (M4/M5's equal-error gate only ever read
    // DirectLightingNode's PRE-spatial buffer). Same ReservoirRecord/16B-per-pixel layout and
    // extent-follow sizing as reservoirBufferA/B (see PARAM_BYTES_PER_PIXEL below).
    NodeHandle spatialReservoirDebugBuffer = renderGraph->AddNode<StorageBufferNodeType>("spatial_reservoir_debug_buffer");

    // Sampled Lighting Inc4 M4: DDGI leak-test gate debug SSBO (binding 31) -- a SINGLE
    // fixed-size DDGILeakGateDebug record (see ProbeUpdate.comp's own struct), NOT
    // extent-driven (no SWAPCHAIN_INFO connection below, unlike reservoirBufferA/B/
    // spatialReservoirDebugBuffer above) -- PARAM_SIZE_BYTES is used instead, mirroring
    // StorageBufferNodeConfig's own documented size-resolution priority ("2. Else
    // PARAM_SIZE_BYTES"). Host-visible/coherent by construction (StorageBufferNodeConfig's
    // own STORAGE_BUFFER output descriptor), so VulkanGraphApplication's readback hook can
    // MapForReadback/UnmapReadback it exactly like reservoirBufferA/B already do.
    NodeHandle ddgiLeakGateDebugBuffer = renderGraph->AddNode<StorageBufferNodeType>("ddgi_leak_gate_debug_buffer");

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

    // Device parameters (auto-select: prefers a discrete GPU over integrated)
    auto* device = static_cast<DeviceNode*>(renderGraph->GetInstance(deviceNode));
    device->SetParameter(DeviceNodeConfig::PARAM_GPU_INDEX, DeviceNodeConfig::GPU_INDEX_AUTO);

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

    // Sampled Lighting Inc4 M2: DDGI probe atlas dimensions/formats -- see kProbeIrradianceAtlas*/
    // kProbeVisibilityAtlas* constants above (PHASE 1) for the layout derivation + citations.
    // Irradiance wants HDR color (RGBA16F is the RTXGI-reference default for low-frequency
    // hemispherical irradiance); visibility wants two float moments (depth, depth^2) for
    // Chebyshev's inequality -- RG16F is the RTXGI-reference default and sufficient precision for
    // this milestone's plumbing-only scope (M4's leak-test gate is the numeric-sensitivity check
    // that would motivate RG32F if RG16F proves insufficient; not assumed here).
    auto* probeIrradianceAtlas = static_cast<ProbeAtlasNode*>(renderGraph->GetInstance(probeIrradianceAtlasNode));
    probeIrradianceAtlas->SetParameter(ProbeAtlasNodeConfig::PARAM_WIDTH,  kProbeIrradianceAtlasWidth);
    probeIrradianceAtlas->SetParameter(ProbeAtlasNodeConfig::PARAM_HEIGHT, kProbeIrradianceAtlasHeight);
    probeIrradianceAtlas->SetParameter(ProbeAtlasNodeConfig::PARAM_FORMAT,
        static_cast<uint32_t>(VK_FORMAT_R16G16B16A16_SFLOAT));

    auto* probeVisibilityAtlas = static_cast<ProbeAtlasNode*>(renderGraph->GetInstance(probeVisibilityAtlasNode));
    probeVisibilityAtlas->SetParameter(ProbeAtlasNodeConfig::PARAM_WIDTH,  kProbeVisibilityAtlasWidth);
    probeVisibilityAtlas->SetParameter(ProbeAtlasNodeConfig::PARAM_HEIGHT, kProbeVisibilityAtlasHeight);
    probeVisibilityAtlas->SetParameter(ProbeAtlasNodeConfig::PARAM_FORMAT,
        static_cast<uint32_t>(VK_FORMAT_R16G16_SFLOAT));

    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] DDGI probe atlases: irradiance " +
                         std::to_string(kProbeIrradianceAtlasWidth) + "x" + std::to_string(kProbeIrradianceAtlasHeight) +
                         " RGBA16F, visibility " +
                         std::to_string(kProbeVisibilityAtlasWidth) + "x" + std::to_string(kProbeVisibilityAtlasHeight) +
                         " RG16F (default 8x8x8 probe grid)");
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

    // Sampled Lighting Inc3 M6: same extent-follow sizing as reservoirBufferA/B above.
    auto* spatialReservoirDebugInst = static_cast<StorageBufferNode*>(renderGraph->GetInstance(spatialReservoirDebugBuffer));
    spatialReservoirDebugInst->SetParameter(StorageBufferNodeConfig::PARAM_BYTES_PER_PIXEL, 16u);

    // Sampled Lighting Inc4 M4: DDGILeakGateDebug is a SINGLE fixed-size record, not
    // extent-driven -- PARAM_SIZE_BYTES, not PARAM_BYTES_PER_PIXEL. std430 layout: 5x
    // uint (20B) + 7x float (28B) = 48B, no padding (all members are 4-byte scalars).
    // (DIAG temporary fields, see ProbeUpdate.comp's own struct.) M5 added two more uints
    // (shadeM5IndirectLumaBits + diagShadeAnyHitCount, both written by SpatialReuseShade.comp)
    // -> 56B. M6 added one more float (diagNearProbeBlendedAtlasLuma, the edit-loop gate's
    // post-hysteresis-blend atlas readback) -> 60B.
    auto* ddgiLeakGateDebugInst = static_cast<StorageBufferNode*>(renderGraph->GetInstance(ddgiLeakGateDebugBuffer));
    ddgiLeakGateDebugInst->SetParameter(StorageBufferNodeConfig::PARAM_SIZE_BYTES, 60u);

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
    // Tiered-ESVO Inc3 M8 Task 17 reuses this SAME generic ConstantNode-bypass knob (it is not
    // demo-specific -- "bypass RaySizeCoefNode with a direct literal raySizeCoef") for
    // VIXEN_TIER_M8_EARTH_DEMO's own hop0/solid-radius fix (see that demo block's own comment
    // for the derivation of ~2.8935e-4, the value that clears the solid with a 3x margin).
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

    // Sampled Lighting Inc3 M5: SpatialReuseShade.comp shader registration. Same search-path
    // pattern as DirectLighting.comp above.
    auto* spatialReuseShaderLibNode = static_cast<ShaderLibraryNode*>(renderGraph->GetInstance(spatialReuseShaderLib));
    spatialReuseShaderLibNode->RegisterShaderBuilder([this](int vulkanVer, int spirvVer) {
        ShaderManagement::ShaderBundleBuilder builder;
        constexpr const char* shaderName = "SpatialReuseShade.comp";
        constexpr const char* programName = "SpatialReuseShade";
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

    // Sampled Lighting Inc4 M3: ProbeUpdate.comp shader registration. Same search-path
    // pattern as DirectLighting.comp/SpatialReuseShade.comp above.
    auto* probeUpdateShaderLibNode = static_cast<ShaderLibraryNode*>(renderGraph->GetInstance(probeUpdateShaderLib));
    probeUpdateShaderLibNode->RegisterShaderBuilder([this](int vulkanVer, int spirvVer) {
        ShaderManagement::ShaderBundleBuilder builder;
        constexpr const char* shaderName = "ProbeUpdate.comp";
        constexpr const char* programName = "ProbeUpdate";
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
    // PRESENT_SRC). Sampled Lighting Inc3 M5: this pass no longer owns IMAGE_WRITE (moved to
    // SpatialReuseNode below — DirectLighting.comp is now a pure buffer producer, see that
    // shader's own file header), so its dispatch dims can no longer live-derive from an
    // IMAGE_WRITE target. Set EXPLICITLY from the render-target's build-time extent instead —
    // mirrors ComputeDispatchNode's own march dispatch dims (set from `width`/`height`, static
    // since graph-build), NOT a regression: DirectLightingNode's own pre-M5 "live from
    // IMAGE_WRITE" derivation existed to track VIXEN_RENDER_SCALE (fixed at process start, read
    // once during this function), not live window-resize (the march's own dispatch dims have
    // never lived-resized either).
    // Ceiling-divide (NOT the March pass's floor-divided dispatchX/dispatchY above) — must
    // match RecordComputeCommands' own (extent+7)/8 live-derivation exactly, since
    // SpatialReuseNode (dims left at 0/0, live-derived from IMAGE_WRITE below) reads every
    // reservoir DirectLighting wrote. A floor/ceil mismatch here left one edge workgroup
    // column/row's reservoirs unwritten while SpatialReuseShade still shaded those pixels
    // (found via a byte-identity gate diff — a tight 32x32 mismatched block at the render
    // target's right/bottom edge, exactly one 8px workgroup row/column wide at 500x500).
    uint32_t directLightingDispatchX =
        (static_cast<uint32_t>(width * renderScale) + 7) / 8;
    uint32_t directLightingDispatchY =
        (static_cast<uint32_t>(height * renderScale) + 7) / 8;
    auto* directLighting = static_cast<ComputeStageNode*>(renderGraph->GetInstance(directLightingNode));
    directLighting->SetParameter(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);
    directLighting->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_X, directLightingDispatchX);
    directLighting->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Y, directLightingDispatchY);
    directLighting->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Z, 1u);

    // SpatialReuseNode (Sampled Lighting Inc3 M5): the second half of the pass split — NOW owns
    // IMAGE_WRITE (moved from DirectLightingNode), so it keeps the ORIGINAL live-derivation
    // (dims left at 0/0, RecordComputeCommands derives them from IMAGE_WRITE's renderTargetNode
    // extent every Execute — the same VIXEN_RENDER_SCALE live-derivation DirectLightingNode used
    // to own pre-M5).
    auto* spatialReuse = static_cast<ComputeStageNode*>(renderGraph->GetInstance(spatialReuseNode));
    spatialReuse->SetParameter(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);
    spatialReuse->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_X, 0u);
    spatialReuse->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Y, 0u);
    spatialReuse->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Z, 1u);

    // Sampled Lighting Inc3 M5: pre-register each gatherer's fixed slot count (no shader
    // reflection needed). HitRecord read: 1 entry. Reservoir write/read: 2 entries each
    // (reservoirBufferA + reservoirBufferB — both physical buffers, see this block's own
    // declaration comment for why BOTH are declared regardless of which is "current").
    static_cast<BufferSyncGathererNode*>(renderGraph->GetInstance(directLightingReadGatherer))->PreRegisterBufferSlots(1);
    static_cast<BufferSyncGathererNode*>(renderGraph->GetInstance(directLightingReservoirWriteGatherer))->PreRegisterBufferSlots(2);
    static_cast<BufferSyncGathererNode*>(renderGraph->GetInstance(spatialReuseReservoirReadGatherer))->PreRegisterBufferSlots(2);

    // Sampled Lighting Inc4 M2: probe atlas image-array gatherer -- 2 entries (irradiance +
    // visibility atlas, see probeAtlasGatherer's own declaration comment above).
    static_cast<ImageSyncGathererNode*>(renderGraph->GetInstance(probeAtlasGatherer))->PreRegisterImageSlots(2);

    // Sampled Lighting Inc4 M5: read-side atlas gatherer -- same 2 entries, feeding
    // spatialReuseNode's IMAGE_READ_ARRAY (see spatialReuseProbeAtlasReadGatherer's own
    // declaration comment above).
    static_cast<ImageSyncGathererNode*>(renderGraph->GetInstance(spatialReuseProbeAtlasReadGatherer))->PreRegisterImageSlots(2);

    // Sampled Lighting Inc4 M3: ProbeUpdateNode dispatch — ONE WORKGROUP PER PROBE
    // (ProbeUpdate.comp's local_size_x=PROBE_UPDATE_MAX_RAYS_PER_PROBE=256 covers every
    // ray in a probe's raysPerProbe budget within a single workgroup — see that shader's
    // own file header for the one-workgroup-per-probe reduction rationale). Dispatch X =
    // probeCount = countX*countY*countZ from ProbeGridConfigNode's own default (M2's
    // MakeDefaultProbeGridConfig — the only content this milestone; no live authoring
    // exists yet, so this dispatch dimension is a build-time constant, not a live
    // per-frame-varying quantity, matching the atlas dimensions' own build-time-derived
    // precedent above). NOT swapchain-adjacent — no IMAGE_WRITE, only IMAGE_WRITE_ARRAY
    // (wired below), so no live extent to derive dims from the way DirectLighting/
    // SpatialReuse do; explicit dispatch dims are the correct shape here (mirrors
    // ComputeDispatchNode's own static width/height-derived dispatch for the march).
    //
    // Sampled Lighting Inc6 M1 (sparse-dispatch amortization): dispatch X is now
    // ceil(probeCount/amortizationFactor), not the flat probeCount — ProbeUpdate.comp's
    // gl_WorkGroupID.x is stride-remapped into the active-slot probe index (see that
    // shader's own main() comment), so only the ACTIVE probeCount/F workgroups are
    // dispatched at all, eliminating per-workgroup dispatch/scheduling overhead for
    // skipped probes entirely (vs. Inc5's dispatch-all-early-out-inside mechanism).
    // amortizationFactor is read via ResolveDdgiAmortizationFactor() (ProbeGridConfigNode.h) —
    // the SAME accessor ProbeGridConfigNode's own MakeDefaultProbeGridConfig uses to fill
    // ProbeGridConfig::amortizationFactor — so this build-time dispatch-X computation and the
    // GPU-side config upload can never disagree on the value, without duplicating the
    // env-var-read/default logic in a second place. CEIL division (not truncating) so a
    // probeCount not evenly divisible by amortizationFactor still dispatches enough
    // workgroups to reach every probe across a full F-tick rotation (see ProbeUpdate.comp's
    // `probeIndex >= probeCount` bound-check, which now guards the harmless occasional
    // over-dispatch this produces).
    constexpr uint32_t kProbeUpdateDefaultProbeCount =
        kProbeGridDefaultCountX * kProbeGridDefaultCountY * kProbeGridDefaultCountZ;
    const uint32_t kDdgiAmortizationFactor = ResolveDdgiAmortizationFactor();
    const uint32_t kProbeUpdateDispatchX =
        (kProbeUpdateDefaultProbeCount + kDdgiAmortizationFactor - 1u) / kDdgiAmortizationFactor;
    mainLogger->Info("[BuildRenderGraph] ProbeUpdate sparse dispatch (Inc6 M1): probeCount=" +
                      std::to_string(kProbeUpdateDefaultProbeCount) + " amortizationFactor=" +
                      std::to_string(kDdgiAmortizationFactor) + " -> dispatchX=" +
                      std::to_string(kProbeUpdateDispatchX));
    auto* probeUpdate = static_cast<ComputeStageNode*>(renderGraph->GetInstance(probeUpdateNode));
    probeUpdate->SetParameter(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);
    probeUpdate->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_X, kProbeUpdateDispatchX);
    probeUpdate->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Y, 1u);
    probeUpdate->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Z, 1u);

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
    // Tiered-ESVO Inc3 M8 Task 19: the translating flight-path demo needs the camera to
    // approach as close as ~0.05 world units from the crossing-octant's own surface point (to
    // clear hop1's predicted ~0.079wu handoff threshold) -- own live-gate finding: the default
    // 0.1wu near-clip plane is LARGER than hop1's own threshold, so the target geometry was
    // being near-plane-clipped away entirely at every near-field tick (empty/background
    // frames, not a tier-crossing or aim bug). Widen near-clip for this demo only, the same
    // "widen the bound, own justified commit" precedent CameraNode.h's kOrbitDistanceMin
    // widening already established for an analogous too-coarse-default problem.
    if (std::getenv("VIXEN_TIER_M8_FLIGHT_DEMO")) {
        // M8 Task 23: with the corrected (camera-anchored) crossing gate at the REAL
        // unoverridden raySizeCoef, hop1 (T1->T2) fires only below ~7.3e-3wu camera-to-child
        // and T2's own content needs an approach to ~2e-4wu to subtend more than a few
        // pixels (childScale^2 = 2^-20 of the body) -- narrow near-clip accordingly
        // (was 0.01wu for the Task-19-era override-based schedule).
        camera->SetParameter(CameraNodeConfig::PARAM_NEAR_PLANE, 0.0001f);
        mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_M8_FLIGHT_DEMO: near-clip plane "
                          "narrowed to 1e-4wu (M8 Task 23: true-2^-10 hop1 fires below "
                          "~7.3e-3wu and T2 needs a ~2e-4wu approach to be attributable)");
    }
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
    // Tiered-ESVO Inc3 M8 Task 17: SAME orbitCenter gotcha applies to the true Earth-scale
    // demo (also built at world center (64,64,64), M4's own established convention).
    //
    // M8 Task 19: this wiring is SKIPPED when VIXEN_TIER_M8_FLIGHT_DEMO is also set. Setting
    // any PARAM_ORBIT_* here latches CameraNode::orbitActive_ = true (SetupImpl's own
    // "explicit orbit param = orbit-mode intent" convention), which makes UpdateCameraData's
    // ORBIT MODE branch recompute cameraPosition from orbitCenter/orbitDistance/yaw/pitch EVERY
    // frame -- silently overriding a scripted CameraNode::SetPositionForTest() write from the
    // Task 19 flight-path demo (own live-gate finding: the very first capture attempt rendered
    // a completely static, unchanging frame across all 400 ticks despite SetPositionForTest
    // being called every tick, because this orbit-param wiring had already engaged orbit mode
    // at scene-build time). The flight-path demo needs FIXED mode (cameraPosition authoritative
    // at rest, per the "bodies-0" convention) so its own scripted position writes are what
    // actually drives the camera.
    if (std::getenv("VIXEN_TIER_M8_EARTH_DEMO") && !std::getenv("VIXEN_TIER_M8_FLIGHT_DEMO")) {
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_X, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Y, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Z, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_DISTANCE, 236.0f);
        mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_M8_EARTH_DEMO: orbitCenter set to demo "
                          "body's world center (64,64,64) so the scripted zoom actually orbits the body");
    } else if (std::getenv("VIXEN_TIER_M8_FLIGHT_DEMO")) {
        mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_M8_FLIGHT_DEMO: skipping orbit-param "
                          "wiring -- camera stays in FIXED mode so the Task 19 scripted "
                          "SetPositionForTest flight path is authoritative, not overridden by "
                          "an orbit-mode recompute every frame");
    }
    // Sampled Lighting Cornell Box Demo M1: shared camera preset (ONE source, both
    // VIXEN_DDGI_CORNELL_BAKED_DEMO and M2's VIXEN_DDGI_CORNELL_VIRTUAL_DEMO read the SAME
    // CornellBoxSceneDefinition.h constants). BuildRenderGraph.cpp's own stale "Camera presets
    // for Cornell box" comment above (PRESET 1, world grid [0,128] / center (64,64,64)) is the
    // LEGACY VoxelGridNode/CashSystem "cornell" scene type (BuildRenderGraph.cpp's own
    // PARAM_SCENE_TYPE default, wired at VIXEN_SCENE below) -- confirmed unrelated to
    // BodyOctreeSceneNode/DDGI (DDGI's pipeline never reads VoxelGridNode's scene type at all,
    // per this program's own plan doc "Grounding from codebase investigation"), NOT reused here.
    // Orbit mode, yaw=0 (CameraNode.cpp's own convention: yaw=0 -> cameraPosition = orbitCenter
    // + (0,0,orbitDistance), camera on the +Z side looking toward -Z/orbitCenter) -- the box's
    // +Z face is the deliberately-open one (CornellBoxSceneDefinition.h's own wall layout), so
    // a yaw=0 camera sits just outside that open face looking straight in, no yaw override
    // needed (CameraNodeConfig::PARAM_YAW already defaults to 0 from SetupImpl above).
    if (std::getenv("VIXEN_DDGI_CORNELL_BAKED_DEMO") || std::getenv("VIXEN_DDGI_CORNELL_VIRTUAL_DEMO")) {
        using namespace Vixen::App::CornellBox;
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_X, kCameraOrbitCenter.x);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Y, kCameraOrbitCenter.y);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Z, kCameraOrbitCenter.z);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_DISTANCE, kCameraOrbitDistance);
        camera->SetParameter(CameraNodeConfig::PARAM_FOV, kCameraFovDegrees);
        mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_*_DEMO: orbitCenter set to shared "
                          "Cornell box center, framed straight into the open +Z face");
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
        } else if (std::getenv("VIXEN_TIER_M8_EARTH_DEMO")) {
            // Tiered-ESVO Inc3 M8 Task 17: the TRUE Earth-scale (childScale=2^-10 at BOTH
            // hops) observable surface-to-orbit demo -- the epic's literal original ask,
            // finally attempted with a genuinely controllable camera (Task 16's look-target
            // decoupling). Originally reused the M4 VIXEN_TIER_EARTH_DEMO block's k-invariant
            // entry-anchored childOriginLocal placement; M8 Task 23 replaced that placement
            // (below, this block only -- VIXEN_TIER_EARTH_DEMO above is UNCHANGED/out of
            // scope) with RootLeafOctantCenterLocal after a live-gate investigation found the
            // entry-anchored point sat on the leaf's own boundary plane, making the child
            // unreachable off-axis -- see this block's own construction comment for the full
            // derivation. Same renderScale=4.8 (48-world-unit body diameter). ONE deliberate
            // difference from M4/M6's attempt (historical, superseded by Task 23 below): the
            // LOD ray-cone coefficient is overridden (VIXEN_TIER_M8_EARTH_LOD_COEF_OVERRIDE,
            // wired below alongside VIXEN_TIER_CROSSING_LOD_COEF_OVERRIDE's existing
            // ConstantNode-bypass mechanism) so hop0 (T0->T1) lands OUTSIDE the body's own
            // ~27-world-unit solid surface radius with real margin, not (as M4/M6's default-
            // raySizeCoef attempt found) buried ~1.8x INSIDE it.
            //
            // THE FINDING THIS FIXES (verified analytically before writing this scene, python
            // trace against M4/M6's own reported numbers): the calibrated LOD-gate formula
            // (M7 Task 13, validator re-derived from the actual shader gate)
            //   hop0 = 20*R*childScale*scale_exp2/raySizeCoef,  solidRadius = 5.625*R
            // gives a ratio hop0/solidRadius = 20*childScale*scale_exp2/(raySizeCoef*5.625)
            // that is INDEPENDENT OF R (R cancels) -- at childScale=2^-10, scale_exp2=0.25
            // (a root-level leaf, this fixture's marked octant is always a direct child of
            // root) and the DEFAULT raySizeCoef=0.0015708 (45deg FOV / 500px height), this
            // ratio is a fixed ~0.524 < 1: hop0 is ALWAYS inside the solid at the default
            // coefficient, for ANY renderScale. This is a stronger, previously-uncharacterized
            // fact than the M6/M7 off-axis-angle finding: M4's original Earth demo (R=4.8)
            // already had hop0=14.92wu strictly inside its own solid radius (~27wu) -- the
            // "camera dives inside the solid, noisy render" symptom M4 attributed only to its
            // near-end zoom schedule (kNearDist=1e-5) was ALSO, independently, a structural
            // property of hop0 itself at the default coefficient.
            // FIX (construction/render-parameter only, no shader/traversal change):
            // raySizeCoef is already a demo-overridable literal via the existing
            // VIXEN_TIER_CROSSING_LOD_COEF_OVERRIDE ConstantNode-bypass mechanism (see that
            // constant's own declaration comment above for why a direct literal, not an FOV
            // bump, is the correct/only-effective lever -- FOV only grows raySizeCoef
            // linearly and cannot cross the needed order of magnitude). Solving
            // hop0 = 3*solidRadius (a 3x safety margin, clear of the solid with room for a
            // real near-orbit approach) for raySizeCoef at R=4.8:
            //   raySizeCoef_override = 20*R*childScale*scale_exp2/(3*5.625*R)
            //                        = 20*childScale*scale_exp2/(3*5.625)  (R cancels again)
            //                        ~= 2.8935e-4
            // giving hop0 = 3*27.0 = 81.0 world units (comfortably < the 120wu orbit ceiling)
            // and hop1 = hop0*childScale = 81.0 * 2^-10 ~= 0.0791016 world units.
            // M8 Task 23 supersedes the override plan above: the corrected (camera-anchored,
            // world-unit-correct) crossing gate makes the crossing a genuine distance-driven
            // handoff at the REAL unoverridden raySizeCoef — no coefficient override is
            // needed or wanted for this demo any more (the historical derivation above is
            // kept for the record; VIXEN_TIER_CROSSING_LOD_COEF_OVERRIDE remains a generic
            // debugging/ablation knob only).
            mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_M8_EARTH_DEMO: building TRUE "
                              "Earth-scale (childScale=2^-10/hop) three-tree chained "
                              "tier-crossing scene -- M8 Task 23 gate: crossing fires by "
                              "camera-to-child distance at the REAL raySizeCoef (no override)");

            constexpr int   kN          = 16;
            constexpr int   kBrickDepth = 3;
            const glm::vec3 kCenter(8.0f, 8.0f, 8.0f);
            constexpr float kChildScale = 0.0009765625f;  // 2^-10, the real per-hop tier ratio

            auto bakeSphereTreeM8 = [&](float radius) {
                Vixen::SVO::RecipeParams rp{};
                rp.radius = radius;
                Vixen::SVO::SdfBakeResult baked =
                    Vixen::SVO::BakeRecipeToSdfWorld(Vixen::SVO::RECIPE_SPHERE, kCenter, rp, kN, 2.0f);
                return Vixen::SVO::BuildSdfBodyOctree(baked, kBrickDepth);
            };

            Vixen::SVO::SdfBodyOctree m8T0Body = bakeSphereTreeM8(6.0f);
            Vixen::SVO::SdfBodyOctree m8T1Body = bakeSphereTreeM8(6.5f);
            Vixen::SVO::SdfBodyOctree m8T2Body = bakeSphereTreeM8(7.2f);

            Vixen::SVO::SerializedOctree m8T0Ser = Vixen::SVO::SerializeSdf(m8T0Body);
            Vixen::SVO::SerializedOctree m8T1Ser = Vixen::SVO::SerializeSdf(m8T1Body);
            Vixen::SVO::SerializedOctree m8T2Ser = Vixen::SVO::SerializeSdf(m8T2Body);

            auto overrideColorM8 = [&](Vixen::SVO::SerializedOctree& ser, glm::vec3 rgb, const char* label) {
                const uint32_t colorBase = ser.channelBaseFloats(Vixen::SVO::SEM_COLOR);
                if (colorBase == 0xFFFFFFFFu) {
                    mainLogger->Error(std::string("[BuildRenderGraph] VIXEN_TIER_M8_EARTH_DEMO: ") + label
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
            // Per-tier color: T0 default cosine-gradient (purple/magenta-adjacent), T1
            // solid green (region tier), T2 solid cyan (bedrock tier) -- IDENTICAL
            // convention to VIXEN_TIER_CHAIN_DEMO/VIXEN_TIER_EARTH_DEMO/VIXEN_TIER_OBSERVABLE_DEMO.
            overrideColorM8(m8T1Ser, glm::vec3(0.0f, 1.0f, 0.0f), "T1");
            overrideColorM8(m8T2Ser, glm::vec3(0.0f, 1.0f, 1.0f), "T2");

            if (const Vixen::SVO::Octree* oct0 = m8T0Body.octree->getOctree()) Vixen::SVO::BakeAndAttachMipPool(*oct0, m8T0Ser);
            if (const Vixen::SVO::Octree* oct1 = m8T1Body.octree->getOctree()) Vixen::SVO::BakeAndAttachMipPool(*oct1, m8T1Ser);
            if (const Vixen::SVO::Octree* oct2 = m8T2Body.octree->getOctree()) Vixen::SVO::BakeAndAttachMipPool(*oct2, m8T2Ser);

            auto findCameraFacingLeafM8 = [](const Vixen::SVO::Octree* oct, uint32_t& outDescIdx, int& outOctant) {
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

            uint32_t m8T0MarkDescIdx = 0; int m8T0MarkOctant = -1;
            findCameraFacingLeafM8(m8T0Body.octree->getOctree(), m8T0MarkDescIdx, m8T0MarkOctant);
            uint32_t m8T1MarkDescIdx = 0; int m8T1MarkOctant = -1;
            findCameraFacingLeafM8(m8T1Body.octree->getOctree(), m8T1MarkDescIdx, m8T1MarkOctant);

            if (m8T0MarkOctant >= 0 && m8T1MarkOctant >= 0) {
                // M8 Task 23 CORRECTION: the previous entry-anchored placement
                // (childOriginLocal = (1.5,1.5,2.0) - kBoxOffset*childScale) put the anchor
                // ON octant 4's own x=1.5/y=1.5 boundary plane (margin 0.000244 at cs=2^-10 --
                // sub-ULP-adjacent), not safely INSIDE its box. A live-gate investigation (two
                // independent traces, this session) found EVERY ray except a sub-pixel bullseye
                // around that corner missed the remapped child's [0,1]^3 grid entirely --
                // explaining the flight capture's flat, unchanging mip-gray quadrant (the
                // child-miss fallback correctly serving T0's own mip, because T1 was
                // structurally unreachable, not because of any Task 23 gate/fallback bug).
                // FIX: use RootLeafOctantCenterLocal (M5's proven-safe fixed point, margin 0.25
                // on every axis -- the SAME technique VIXEN_TIER_CROSSING_DEMO/CHAIN_DEMO/
                // OBSERVABLE_DEMO already use successfully) instead of a corner-adjacent entry
                // point. This does NOT reopen the tEntryWorld/off-boundary constraint (M1/M3
                // carry-forward): a CENTERED anchor is MORE robust to off-boundary entry than a
                // corner-anchored one, since the safe margin is 0.25 local units either way,
                // independent of childScale (the amplification only affects how far a given
                // WORLD deviation reaches in child-local units, not the parent-local anchor
                // point itself).
                const glm::vec3 m8T0ChildOriginLocal = Vixen::SVO::RootLeafOctantCenterLocal(m8T0MarkOctant);

                Vixen::SVO::TierRef m8RefT0ToT1{};
                m8RefT0ToT1.childOctreeIndex = 1u;
                m8RefT0ToT1.childOriginLocal[0] = m8T0ChildOriginLocal.x;
                m8RefT0ToT1.childOriginLocal[1] = m8T0ChildOriginLocal.y;
                m8RefT0ToT1.childOriginLocal[2] = m8T0ChildOriginLocal.z;
                m8RefT0ToT1.childScale = kChildScale;
                Vixen::SVO::MarkLeafAsTierCrossing(m8T0Ser, m8T0MarkDescIdx, m8T0MarkOctant, m8RefT0ToT1, 22);

                const glm::vec3 m8T1ChildOriginLocal = Vixen::SVO::RootLeafOctantCenterLocal(m8T1MarkOctant);

                Vixen::SVO::TierRef m8RefT1ToT2{};
                m8RefT1ToT2.childOctreeIndex = 2u;
                m8RefT1ToT2.childOriginLocal[0] = m8T1ChildOriginLocal.x;
                m8RefT1ToT2.childOriginLocal[1] = m8T1ChildOriginLocal.y;
                m8RefT1ToT2.childOriginLocal[2] = m8T1ChildOriginLocal.z;
                m8RefT1ToT2.childScale = kChildScale;
                Vixen::SVO::MarkLeafAsTierCrossing(m8T1Ser, m8T1MarkDescIdx, m8T1MarkOctant, m8RefT1ToT2, 22);

                Vixen::SVO::ConcatenatedOctrees m8Cat;
                m8Cat.count = 3;
                m8Cat.configs.resize(3);
                m8Cat.nodeCounts.resize(3);
                m8Cat.brickCounts.resize(3);
                m8Cat.tierRefCounts.resize(3);

                Vixen::SVO::SerializedOctree* m8Octs[3] = {&m8T0Ser, &m8T1Ser, &m8T2Ser};
                uint32_t m8NodeBase = 0, m8BrickBase = 0, m8PoolBase = 0, m8TierRefBase = 0, m8MipPoolBase = 0;
                for (int k = 0; k < 3; ++k) {
                    Vixen::SVO::SerializedOctree& s = *m8Octs[k];
                    s.config.nodeArrayBase  = static_cast<int32_t>(m8NodeBase);
                    s.config.brickArrayBase = static_cast<int32_t>(m8BrickBase);
                    Vixen::SVO::setSdfBrickArrayBase(s.config, m8PoolBase);
                    Vixen::SVO::setTierRefTableBase(s.config, m8TierRefBase);
                    Vixen::SVO::setMipPoolBase(s.config, m8MipPoolBase);

                    m8Cat.configs[k]       = s.config;
                    m8Cat.nodeCounts[k]    = s.nodeCount;
                    m8Cat.brickCounts[k]   = s.brickCount;
                    m8Cat.tierRefCounts[k] = static_cast<uint32_t>(s.tierRefs.size());

                    m8Cat.nodes.insert(m8Cat.nodes.end(), s.nodes.begin(), s.nodes.end());
                    m8Cat.bricks.insert(m8Cat.bricks.end(), s.bricks.begin(), s.bricks.end());
                    m8Cat.channelPool.insert(m8Cat.channelPool.end(), s.channelPool.begin(), s.channelPool.end());
                    m8Cat.brickGridLookup.insert(m8Cat.brickGridLookup.end(), s.brickGridLookup.begin(), s.brickGridLookup.end());
                    m8Cat.tierRefTable.insert(m8Cat.tierRefTable.end(), s.tierRefs.begin(), s.tierRefs.end());
                    m8Cat.mipPool.insert(m8Cat.mipPool.end(), s.mipPool.begin(), s.mipPool.end());

                    if (m8Cat.materials.empty()) {
                        m8Cat.materials = s.materials;
                    }

                    m8NodeBase    += s.nodeCount;
                    m8BrickBase   += s.brickCount;
                    m8PoolBase    += s.brickCount * s.brickStrideFloats;
                    m8TierRefBase += static_cast<uint32_t>(s.tierRefs.size());
                    m8MipPoolBase += s.nodeCount * s.channelCount;
                }

                if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                    bodyScene->SetRecipePool(std::move(m8Cat));
                    // M8 Task 21: grant brick residency FROM CONSTRUCTION (not mid-flight).
                    // Task 20's validator found this scene otherwise stays mip-only
                    // (streaming-only), and a mip-only body can ONLY render via the LOD
                    // gate's decline-to-mip path -- there is no resident-brick fallback for
                    // it to fall through to below the coefficient hop0's crossing needs.
                    // Explicit call (rather than relying on residencyRequested_'s default)
                    // so this scene's residency state is unambiguous regardless of the
                    // node's default.
                    bodyScene->RequestBrickResidency(true);

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
                    inst.providerKind = 0u;
                    inst.recipeId     = 0u;

                    bodyScene->SetInstances({inst});

                    // Record the two crossing octants' world positions for the scripted
                    // look-target retargeting (VulkanGraphApplication.cpp). Both hops use the
                    // SAME deterministic camera-facing selection (octant 4, bit pattern
                    // x=0,y=0,z=1), so the world offset from body center is the established
                    // (-2.5R,-2.5R,+2.5R) constant (M7 Task 13, validator-confirmed
                    // R-proportional fact of RootLeafOctantCenterLocal's own convention) --
                    // IDENTICAL for hop0 and hop1 since renderScale is fixed at every tier
                    // (only the child octree's own internal scale shrinks via childScale;
                    // the crossing octant's own WORLD position is a property of the PARENT
                    // tree's instance placement, unaffected by what childScale the crossing
                    // leads into).
                    const glm::vec3 bodyCenterWorld(64.0f, 64.0f, 64.0f);
                    // M8 Task 23: record the ACTUAL child anchor's world position (now
                    // RootLeafOctantCenterLocal(4), the proven-safe centered fixed point — see
                    // this scene's own construction comment above for why the prior
                    // corner-adjacent entry point was replaced) so the flight schedule
                    // approaches/aims at genuinely reachable child content, not a boundary
                    // point ~1024x too small a target to hit off-axis. World mapping: the
                    // [1,2) local cube spans 10*renderScale world units anchored at
                    // inst.worldPos (kHalf = 5R puts local 1.5 at body center 64, verified:
                    // worldPos + 0.5*10R = 40 + 24 = 64).
                    const float kCubeWorldEdge = 10.0f * kRenderScale;  // 48wu
                    const glm::vec3 instWorldPos(64.0f - kHalf, 64.0f - kHalf, 64.0f - kHalf);
                    m8EarthHop0OctantWorld_ = instWorldPos
                        + (m8T0ChildOriginLocal - glm::vec3(1.0f)) * kCubeWorldEdge;
                    // M8 Task 24: T1's own marked crossing leaf (m8T1ChildOriginLocal, T1->T2's
                    // childOriginLocal) lives in T1's OWN local [1,2) frame, NOT T0's -- it must
                    // NOT be aliased to hop0's world position (the Task 23 bug: both hops aimed at
                    // the SAME point, so hop1 never got its own distinct flight target). Map it
                    // into T0's local frame first via the INVERSE of remapRayIntoChildFrame
                    // (GpuTraversalMirror.h/BodyInstanceRayMarch.comp's own forward remap is
                    // childLocal = (parentLocal - childOrigin)*invScale + 1.5, so the inverse is
                    // parentLocal = childOrigin + (childLocal - 1.5)*childScale), then apply the
                    // SAME instWorldPos/kCubeWorldEdge mapping hop0 uses to reach world space.
                    const glm::vec3 m8T1AnchorInT0Local = m8T0ChildOriginLocal
                        + (m8T1ChildOriginLocal - glm::vec3(1.5f)) * kChildScale;
                    m8EarthHop1OctantWorld_ = instWorldPos
                        + (m8T1AnchorInT0Local - glm::vec3(1.0f)) * kCubeWorldEdge;

                    mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_M8_EARTH_DEMO: T0 leaf ("
                                  + std::to_string(m8T0MarkDescIdx) + "," + std::to_string(m8T0MarkOctant)
                                  + ") -> T1 octree1 (childScale=2^-10); T1 leaf (" + std::to_string(m8T1MarkDescIdx) + ","
                                  + std::to_string(m8T1MarkOctant) + ") -> T2 octree2 (childScale=2^-10); "
                                  "hop0 crossing octant world pos ("
                                  + std::to_string(m8EarthHop0OctantWorld_.x) + ","
                                  + std::to_string(m8EarthHop0OctantWorld_.y) + ","
                                  + std::to_string(m8EarthHop0OctantWorld_.z) + "); "
                                  "hop1 (T1's own) crossing world pos ("
                                  + std::to_string(m8EarthHop1OctantWorld_.x) + ","
                                  + std::to_string(m8EarthHop1OctantWorld_.y) + ","
                                  + std::to_string(m8EarthHop1OctantWorld_.z) + ")");
                }
            } else {
                mainLogger->Error("[BuildRenderGraph] VIXEN_TIER_M8_EARTH_DEMO: no camera-facing leaf found in T0 or T1 — demo scene not built");
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
        } else if (std::getenv("VIXEN_DDGI_CORNELL_BAKED_DEMO")) {
            // Sampled Lighting — Cornell Box GI Reference Scene, M1 (baked variant).
            // Plan: Vixen-Docs/01-Architecture/Sampled-Lighting-Cornell-Box-Demo-Plan-2026-07.md
            //
            // Geometry/color/camera/probe-grid numbers all come from ONE shared source
            // (application/main/include/graph/CornellBoxSceneDefinition.h) — M2's virtual
            // variant reads the SAME header verbatim, so "ideally visually identical" is
            // enforced by construction (see that header's own file comment).
            //
            // M3 round 2 (2026-07-14): this block now consumes BuildCornellWorldSpaceBodies()
            // (this file's anonymous namespace, above) instead of independently authoring its
            // own gridBoxAt-based instructions -- the SAME world-space SdfInstruction list the
            // virtual variant splices directly now feeds this bake path too, via
            // MakeCornellWorldSpaceEvalFn's grid->world adapter (below). See that function's own
            // header comment for the full "why unify" reasoning. M1's own recipe-VM
            // investigation findings (RoundedBox primitive choice, emission being a bake-time-
            // only concept) are unchanged and still apply — see BuildCornellWorldSpaceBodies.
            //
            // Multi-body assembly mirrors VIXEN_DDGI_LEAK_GATE_DEMO's own established shape
            // (this file, above): bake each body separately, BuildSdfBodyOctree per body,
            // ConcatenateSdfWithMips across all bodies, BuildLightTreeCut over the emissive body
            // only, SetRecipePool + SetInstances. 8 bodies here (5 walls + 1 light + 2 objects)
            // vs. the leak-gate scene's 2 — same mechanism, more bodies.
            using namespace Vixen::App::CornellBox;
            mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_BAKED_DEMO: building the Cornell "
                              "box GI reference scene (baked/octree variant)");

            constexpr float kBand = 2.0f;
            constexpr float kWorldGridSize = 10.0f;  // ShellOctreeGpu.h's fixed octree-local->world span

            // bodyWorldPos/bodyRenderScale: the SAME grid<->world mapping the runtime shader's
            // octree traversal already uses (world = worldPos + (grid/n)*kWorldGridSize*
            // renderScale) -- unchanged from every prior round, still the single source of truth
            // for how a per-body bake grid maps onto the scene's true world coordinates.
            auto bodyWorldPos = [](glm::vec3 bodyWorldCenter, int n, int subdiv) {
                return bodyWorldCenter - glm::vec3(static_cast<float>(n) / (2.0f * static_cast<float>(subdiv)));
            };
            auto bodyRenderScale = [](int n, int subdiv) {
                return static_cast<float>(n) / (static_cast<float>(subdiv) * kWorldGridSize);
            };

            // MakeCornellWorldSpaceEvalFn (M3 round 2): a small adapter that lets BakeSdfWorld's
            // raw-grid-coordinate eval callback evaluate a WORLD-SPACE-authored SdfInstruction
            // program directly, by mapping p_raw -> true world position via EXACTLY
            // bodyWorldPos/bodyRenderScale's own formula above (algebraically verified: world
            // (p_raw) = bodyWorldCenter + (p_raw - n/2)/subdiv, i.e. grid center n/2 maps to the
            // body's own true world center). This is what makes brick-density (subdiv, chosen
            // per-body purely for bake-grid resolution) fully independent of instruction
            // authoring (now a single shared world-space source, BuildCornellWorldSpaceBodies) --
            // subdivision no longer needs to be pre-baked into the recipe's own half-extents/
            // positions the way the pre-unification gridBoxAt did.
            auto makeWorldSpaceEval = [](const std::vector<SdfInstruction>& prog, glm::vec3 bodyWorldCenter, int n, int subdiv) {
                return [&prog, bodyWorldCenter, n, subdiv](const glm::vec3& pRaw) {
                    const glm::vec3 world = bodyWorldCenter + (pRaw - glm::vec3(static_cast<float>(n) * 0.5f)) / static_cast<float>(subdiv);
                    return Vixen::SVO::Recipe::evalRecipe(prog.data(), static_cast<uint32_t>(prog.size()), world);
                };
            };
            auto bakeWorldSpaceBody = [&](const std::vector<SdfInstruction>& prog, glm::vec3 bodyWorldCenter, int n, int subdiv) {
                Vixen::SVO::SdfBakeResult baked = Vixen::SVO::BakeSdfWorld(
                    makeWorldSpaceEval(prog, bodyWorldCenter, n, subdiv), bodyWorldCenter, n, kBand);
                return Vixen::SVO::BuildSdfBodyOctree(baked, 3);
            };

            // Wall grid (subdiv=4): widest true extent 2*(kBoxHalfExtent+kWallThickness)(=22)
            // + 2*kBand(=4) margin, scaled by subdiv -> (22+4)*4=104.
            //
            // CRITICAL (combined-verify round, 2026-07-15): the bake grid `n` MUST be a POWER OF
            // TWO. BuildSdfBodyOctree (SdfBake.h) derives the octree's own voxel resolution as
            // 2^(maxLevels-brickDepth) where maxLevels = floor(log2(n)) + brickDepth -- i.e. it
            // silently ROUNDS n DOWN to the previous power of two. The prior value 112 (and the
            // even-earlier 96) are NOT powers of two, so the octree was built at a 64-voxel grid
            // while the SDF was baked into a 112-voxel grid: the octree's bricksPerAxis (8) and
            // the config's bricksPerAxisSdf from the 112-grid bake (14) DISAGREE, so the shader's
            // brick/voxel addressing reads the wrong cells -- producing spurious/misplaced
            // occupancy (grazing rays over the ceiling hit a far ghost surface at ~y=37.6, z=-24,
            // hitT~78; ~4% of pixels land out-of-bounds and poison the DDGI probe gather with
            // noise). Using 128 (the next power of two >= 104) makes the octree grid EQUAL the
            // bake grid, so addressing is consistent end to end. This is the "leftover interaction
            // between the world-space-unification refactor and the baked traversal path" the
            // geometry-fix rounds flagged but never pinned: those rounds set n to non-pow2 values
            // (96 then 112) for brick-alignment without noticing the octree builder's pow2 contract.
            //
            // subdiv breaks the "grid-unit == 1 world unit" identity so a grid cell is
            // 1/subdiv world units instead of 1 -- brick world-size (always brickSide=8 world
            // units under subdiv=1, independent of n) becomes brickSide/subdiv, making thin
            // (~2-6 world unit) walls resolvable instead of dilating into solid overlapping
            // slabs (round 5's own finding). Light/object grid: no subdivision needed (their
            // thinnest dims are close enough to a full brick that subdividing them is a smaller,
            // non-blocking cosmetic concern). n=16 (pow2) as before for the small bodies (subdiv=1).
            constexpr int kWallSubdiv = 4;
            constexpr int kWallN   = 128;  // power of two (was 112 -- see the pow2 note above)
            constexpr int kSmallN  = 16;
            constexpr int kSmallSubdiv = 1;

            std::vector<CornellWorldSpaceBody> bodies = BuildCornellWorldSpaceBodies();
            // bodies[]: leftWall, rightWall, backWall, floor, ceiling, light, sphereObj, boxObj (fixed order, see BuildCornellWorldSpaceBodies)

            Vixen::SVO::SdfBodyOctree leftWallBody   = bakeWorldSpaceBody(bodies[0].prog, bodies[0].worldCenter, kWallN, kWallSubdiv);
            Vixen::SVO::SdfBodyOctree rightWallBody  = bakeWorldSpaceBody(bodies[1].prog, bodies[1].worldCenter, kWallN, kWallSubdiv);
            Vixen::SVO::SdfBodyOctree backWallBody   = bakeWorldSpaceBody(bodies[2].prog, bodies[2].worldCenter, kWallN, kWallSubdiv);
            Vixen::SVO::SdfBodyOctree floorBody      = bakeWorldSpaceBody(bodies[3].prog, bodies[3].worldCenter, kWallN, kWallSubdiv);
            Vixen::SVO::SdfBodyOctree ceilingBody    = bakeWorldSpaceBody(bodies[4].prog, bodies[4].worldCenter, kWallN, kWallSubdiv);
            Vixen::SVO::SdfBodyOctree sphereObjBody  = bakeWorldSpaceBody(bodies[6].prog, bodies[6].worldCenter, kSmallN, kSmallSubdiv);
            Vixen::SVO::SdfBodyOctree boxObjBody     = bakeWorldSpaceBody(bodies[7].prog, bodies[7].worldCenter, kSmallN, kSmallSubdiv);

            // Light body: baked WITH emission (constant intensity across its whole volume —
            // the ceiling-recessed box IS the emitter, no separate "emissive surface only"
            // distinction at this milestone's fidelity). Same world-space-eval adapter as every
            // other body, plus an EmitFn (BakeSdfWorld's own generic EmitFn template param).
            const glm::vec3& kLightWorldCenter = bodies[5].worldCenter;
            Vixen::SVO::SdfBakeResult lightBaked = Vixen::SVO::BakeSdfWorld(
                makeWorldSpaceEval(bodies[5].prog, kLightWorldCenter, kSmallN, kSmallSubdiv),
                kLightWorldCenter, kSmallN, kBand, 3,
                [](const glm::vec3&) { return kLightEmissionIntensity; });
            Vixen::SVO::SdfBodyOctree lightBody = Vixen::SVO::BuildSdfBodyOctree(lightBaked, 3);
            const glm::vec3 kLightWorldPos = bodyWorldPos(kLightWorldCenter, kSmallN, kSmallSubdiv);
            const float kLightRenderScale = bodyRenderScale(kSmallN, kSmallSubdiv);

            const Vixen::SVO::Octree* lightOct = lightBody.octree->getOctree();
            if (lightOct == nullptr) {
                mainLogger->Error("[BuildRenderGraph] VIXEN_DDGI_CORNELL_BAKED_DEMO: light body octree is null -- scene not built");
            } else {
                Vixen::SVO::SerializedOctree lightSer = Vixen::SVO::SerializeSdf(lightBody);
                Vixen::SVO::BakeAndAttachMipPool(*lightOct, lightSer);
                Vixen::SVO::MipPool lightMipPool = Vixen::SVO::BakeMipPool(*lightOct, lightSer);

                Vixen::SVO::LightTreeCutParams cutParams;
                cutParams.powerThreshold = 0.001f;  // same fine-cut rationale as VIXEN_DDGI_LEAK_GATE_DEMO
                std::vector<Vixen::SVO::LightTreeNode> cut =
                    Vixen::SVO::BuildLightTreeCut(*lightOct, lightSer, lightMipPool, kSmallN, cutParams);

                // Grid->world transform for the light-tree cut, mirroring the SAME formula this
                // block's own header comment derives for body geometry (world = worldPos +
                // (grid/n)*kWorldGridSize*renderScale) -- the light-tree cut's own worldPos/
                // worldExtent fields are in the light body's bake-grid frame and need the
                // IDENTICAL transform the light body's own BodyInstanceGpu (kLightWorldPos,
                // kLightRenderScale, both derived from the SAME kSmallN grid above) applies, or
                // the light-tree's stashed cut (read by ProbeUpdate.comp for shading) would
                // disagree with where the light BODY geometry actually renders -- the exact
                // class of bug VIXEN_DDGI_LEAK_GATE_DEMO's own header comment warns about (an
                // unrescaled cut silently lands outside the probe grid's [0,32) coverage).
                std::vector<Vixen::SVO::LightTreeNode> worldCut;
                worldCut.reserve(cut.size());
                for (const auto& node : cut) {
                    Vixen::SVO::LightTreeNode w = node;
                    w.worldPos = kLightWorldPos + (node.worldPos / static_cast<float>(kSmallN)) * kWorldGridSize * kLightRenderScale;
                    w.worldExtent = (node.worldExtent / static_cast<float>(kSmallN)) * kWorldGridSize * kLightRenderScale;
                    worldCut.push_back(w);
                }

                if (auto* lightTreeInst = static_cast<LightTreeBufferNode*>(renderGraph->GetInstance(lightTreeBufferNode))) {
                    lightTreeInst->SetLightTreeCut(worldCut);
                }
                mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_BAKED_DEMO: light-tree cut=" +
                                  std::to_string(cut.size()) + " nodes");

                std::vector<const Vixen::SVO::SdfBodyOctree*> octreesForCat = {
                    &leftWallBody, &rightWallBody, &backWallBody, &floorBody, &ceilingBody,
                    &lightBody, &sphereObjBody, &boxObjBody,
                };
                // TEMP DIAG (root-causing invisible walls): per-body node/brick counts +
                // world-frame bounds before concatenation, to rule out a degenerate
                // (zero-node or wrongly-bounded) octree despite non-empty voxel bake batches.
                {
                    const char* names[] = {"leftWall","rightWall","backWall","floor","ceiling","light","sphereObj","boxObj"};
                    for (size_t di = 0; di < octreesForCat.size(); ++di) {
                        const Vixen::SVO::Octree* diagOct = octreesForCat[di]->octree->getOctree();
                        if (diagOct == nullptr) {
                            mainLogger->Info(std::string("[BuildRenderGraph] CORNELL DIAG body=") + names[di] + " octree=NULL");
                            continue;
                        }
                        Vixen::SVO::SerializedOctree diagSer = Vixen::SVO::SerializeSdf(*octreesForCat[di]);
                        mainLogger->Info(std::string("[BuildRenderGraph] CORNELL DIAG body=") + names[di] +
                                          " nodeCount=" + std::to_string(diagSer.nodeCount) +
                                          " brickCount=" + std::to_string(diagSer.brickCount) +
                                          " gridMin=(" + std::to_string(diagSer.config.gridMinX) + "," + std::to_string(diagSer.config.gridMinY) + "," + std::to_string(diagSer.config.gridMinZ) + ")" +
                                          " gridMax=(" + std::to_string(diagSer.config.gridMaxX) + "," + std::to_string(diagSer.config.gridMaxY) + "," + std::to_string(diagSer.config.gridMaxZ) + ")");
                    }
                }
                Vixen::SVO::ConcatenatedOctrees cat = Vixen::SVO::ConcatenateSdfWithMips(octreesForCat);

                auto makeInstance = [&](uint32_t octreeIdx, glm::vec3 color, glm::vec3 worldPos, float renderScale) {
                    Vixen::SVO::BodyInstanceGpu inst{};
                    inst.worldPos[0] = worldPos.x;
                    inst.worldPos[1] = worldPos.y;
                    inst.worldPos[2] = worldPos.z;
                    inst.renderScale = renderScale;
                    inst.color[0] = color.x; inst.color[1] = color.y; inst.color[2] = color.z;
                    inst.octreeIndex = octreeIdx;
                    inst.providerKind = 0u;  // PROVIDER_STORED
                    inst.recipeId = 0u;
                    return inst;
                };
                const float kWallRenderScale  = bodyRenderScale(kWallN, kWallSubdiv);
                const float kSmallRenderScale = bodyRenderScale(kSmallN, kSmallSubdiv);
                std::vector<Vixen::SVO::BodyInstanceGpu> instances = {
                    makeInstance(0u, bodies[0].color, bodyWorldPos(bodies[0].worldCenter, kWallN, kWallSubdiv),  kWallRenderScale),   // leftWall
                    makeInstance(1u, bodies[1].color, bodyWorldPos(bodies[1].worldCenter, kWallN, kWallSubdiv),  kWallRenderScale),   // rightWall
                    makeInstance(2u, bodies[2].color, bodyWorldPos(bodies[2].worldCenter, kWallN, kWallSubdiv),  kWallRenderScale),   // backWall
                    makeInstance(3u, bodies[3].color, bodyWorldPos(bodies[3].worldCenter, kWallN, kWallSubdiv),  kWallRenderScale),   // floor
                    makeInstance(4u, bodies[4].color, bodyWorldPos(bodies[4].worldCenter, kWallN, kWallSubdiv),  kWallRenderScale),   // ceiling
                    makeInstance(5u, bodies[5].color, kLightWorldPos,                                            kLightRenderScale),  // light
                    makeInstance(6u, bodies[6].color, bodyWorldPos(bodies[6].worldCenter, kSmallN, kSmallSubdiv), kSmallRenderScale), // sphereObj
                    makeInstance(7u, bodies[7].color, bodyWorldPos(bodies[7].worldCenter, kSmallN, kSmallSubdiv), kSmallRenderScale), // boxObj
                };

                if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                    bodyScene->SetRecipePool(std::move(cat));
                    // Force EAGER brick residency so the primary visible hit brick-marches the
                    // real trilinear SDF surface (handleLeafHitInstancedSdf) instead of the coarse
                    // mip-fallback (shadeFromMipSample) -- root cause of the "fragmented Cornell box"
                    // this demo was reported broken with (combined-verify round, 2026-07-15).
                    //
                    // WHY this is required (two stacked causes, both found this round):
                    //  (1) Every body is mip-capable, so DeriveResidencyDefault boots the scene LAZY
                    //      (brickResident=0). Under LAZY the primary hit resolves via mip-fallback at
                    //      coarse leaf-NODE granularity -- grey base color, flat up-normal, leaf-entry
                    //      t. A coarse-grid HitRecord readback showed this renders geometrically
                    //      INCOHERENT hits: wrong body per screen region, worldPos.z never below ~18.8
                    //      (rays never reach the interior/back wall), no coherent wall planes.
                    //  (2) The prior M3-round-4 "RequestBrickResidency ruled out" note was wrong for a
                    //      subtle reason: VulkanGraphApplication::UpdateBodySceneResidency() runs a
                    //      per-FRAME frustum/resolvability heuristic (tuned for the orbit-scale
                    //      tiered-ESVO demos) that calls RequestBrickResidency(anyInstanceWantsBricks)
                    //      every tick and STOMPED this grant back to false for this fixed close box
                    //      (its walls fail the pixel-resolvability threshold) -- so the earlier trial's
                    //      residency never actually stuck. That heuristic now early-returns for this
                    //      demo (see UpdateBodySceneResidency), leaving this call the exclusive driver,
                    //      exactly like VIXEN_TIER_ZOOM_DEMO's own scripted residency ownership.
                    bodyScene->RequestBrickResidency(true);
                    bodyScene->SetInstances(std::move(instances));
                    mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_BAKED_DEMO: seeded 8 body "
                                      "instances (5 walls + 1 ceiling light + 2 objects)");
                }
            }
            // Force-enable the DDGI probe grid for this demo -- ProbeGridConfigNode.cpp's own
            // MakeDefaultProbeGridConfig defaults probeGridEnabled=0 and only reads
            // VIXEN_PROBE_GRID_CONFIG_ENABLED independently later in the SAME process (its own
            // SetupImpl, not called from here) -- setting it here means this demo's whole point
            // (visually inspect DDGI GI quality) works from ONE env var, without the operator
            // needing to separately remember VIXEN_PROBE_GRID_CONFIG_ENABLED=1 (an easy miss that
            // would silently render the box with probeGridEnabled=0 -- no bounce lighting at all,
            // a false-negative-looking "it doesn't work" report). Mirrors
            // VIXEN_DDGI_LEAK_GATE_DEMO's own reliance on that lever (that gate's own comment,
            // this file ~line 3016, documents the SAME dependency but relies on the operator
            // setting it manually since that gate is a numeric/CPU-readback proof, not a live
            // visual demo). _putenv_s (not setenv) for MSVC CRT portability.
#if defined(_WIN32)
            _putenv_s("VIXEN_PROBE_GRID_CONFIG_ENABLED", "1");
#else
            setenv("VIXEN_PROBE_GRID_CONFIG_ENABLED", "1", 1);
#endif
            mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_BAKED_DEMO: force-enabled "
                              "VIXEN_PROBE_GRID_CONFIG_ENABLED=1 (probe grid must be on for this "
                              "demo's own point -- visible bounce lighting -- to be visible at all)");
        } else if (std::getenv("VIXEN_DDGI_CORNELL_VIRTUAL_DEMO")) {
            // Sampled Lighting — Cornell Box GI Reference Scene, M2 (virtual/zero-bake variant).
            // Plan: Vixen-Docs/01-Architecture/Sampled-Lighting-Cornell-Box-Demo-Plan-2026-07.md
            //
            // SAME geometry/color/camera/probe-grid numbers as M1's baked variant (ONE shared
            // source, application/main/include/graph/CornellBoxSceneDefinition.h) -- "ideally
            // visually identical" enforced by construction. Content-representation backend is
            // the ONLY thing that differs: every one of the 8 bodies here renders through the
            // zero-bake GPU-direct path (RegisterProceduralRecipe -> PROVIDER_PROCEDURAL
            // BodyInstanceGpu, the same live mechanism VIXEN_PROCEDURAL_UBER_DEMO above already
            // proves works end-to-end, extended here to a hand-authored multi-body scene instead
            // of a generated stress corpus) -- ZERO BakeRecipeInstructionsToSdfWorld/
            // BuildSdfBodyOctree calls for any of them: static inspection of this whole block
            // confirms neither function is ever called for the 8-body registration loop below
            // (only RegisterProceduralRecipe), and g_cornellVirtualLightTreeSideBakeCount (this
            // file's top-level namespace, mirrors test_baked_vs_virtual_parity.cpp's own
            // g_bakeCallCount call-counter technique) proves at runtime that the ONLY bake call
            // anywhere in this block is the light-tree's own explicitly-scoped side bake below.
            //
            // M1's own recipe-VM investigation answers (load-bearing here, see M1's block above
            // for the full reasoning):
            //   (a) Box primitive: RoundedBox (data[0..2]=halfExtents, data[3]=rounding,
            //       data[4..6]=position) -- same primitive M1 used for the baked variant, now
            //       registered live via RecipeRegistry instead of baked directly.
            //   (b) Emission: the recipe-VM/RecipeEntry has NO emission opcode or material-tag
            //       field (confirmed again here: BodyInstanceGpu carries only worldPos/
            //       renderScale/color/octreeIndex/providerKind/recipeId/recipeParams -- no
            //       emission field anywhere, ShellOctreeGpu.h:349-357). More fundamentally,
            //       BuildLightTreeCut (LightTree.h) structurally REQUIRES an already-baked
            //       Octree+SerializedOctree+MipPool with a SEM_EMISSION voxel channel -- the
            //       light-tree mechanism that feeds DDGI's bounce-lighting gather is inherently
            //       baked-content-only, independent of how the body's own VISIBLE geometry is
            //       represented. This is a real architectural boundary, not a gap this milestone
            //       can route around: the light-tree cut is consumed by ProbeUpdate.comp as a
            //       flat world-space SSBO (LightTreeBufferNode::SetLightTreeCut), decoupled from
            //       whichever provider (Stored/Procedural) renders the light body's own pixels.
            //       Resolution: the light BODY's VISIBLE geometry still renders through the
            //       zero-bake procedural path (a real registered recipe, PROVIDER_PROCEDURAL,
            //       zero bake calls, same as every other body here) -- but the light-TREE CUT
            //       that feeds DDGI's gather is computed from a small, explicitly-scoped side
            //       bake of ONLY the light body's own geometry+emission (mirrors M1's
            //       BakeRecipeInstructionsToSdfWorldWithEmission call almost exactly, since this
            //       IS the same mechanism M1 already used for its own light-tree cut -- the only
            //       difference is this bake's result is discarded after the light-tree cut is
            //       derived, never fed into SetRecipePool/rendered). This bake is NOT counted
            //       against the zero-bake proof below (the proof's own scope is "the 8 bodies'
            //       RENDERED representation," matching the plan's own "never-baked proof for the
            //       virtual variant" gate wording) -- it is called out explicitly in both the
            //       log line and the Progress Log so this distinction is never silently assumed.
            using namespace Vixen::App::CornellBox;
            mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_VIRTUAL_DEMO: building the Cornell "
                              "box GI reference scene (virtual/zero-bake variant)");

            using Vixen::SVO::Recipe::SdfInstruction;

            // M3 round 2 (2026-07-14): geometry now comes from BuildCornellWorldSpaceBodies()
            // (this file's anonymous namespace, above) -- the SAME world-space SdfInstruction
            // list the baked variant's bake path also consumes (via a grid->world eval adapter),
            // instead of this block independently re-authoring its own worldBoxAt/worldSphereAt
            // calls from the shared CONSTANTS. See BuildCornellWorldSpaceBodies's own header
            // comment for the full "why unify" reasoning. The virtual/Procedural path's field
            // function already samples WORLD p directly (recipeId>=2 convention,
            // VIXEN_PROCEDURAL_UBER_DEMO's own "unused: field samples world p directly" comment),
            // so these world-space-authored instructions splice in completely unchanged -- no
            // adapter needed on this side, unlike the baked path.
            std::vector<CornellWorldSpaceBody> worldBodies = BuildCornellWorldSpaceBodies();

            // Recipe id allocation: 2..9 (0/1 reserved for the legacy analytic RECIPE_SPHERE/
            // RECIPE_DISPLACED_SPHERE path, same convention VIXEN_PROCEDURAL_UBER_DEMO's own
            // "recipeId = 2 + i" comment documents). Bound sphere authored explicitly for every
            // entry (same reasoning VIXEN_PROCEDURAL_UBER_DEMO's own comment gives: every
            // program here samples WORLD p directly via a position-carrying primitive, so
            // DeriveConservativeBounds' derivation would be redundant with an authored bound
            // anyway -- authoring it directly also sidesteps ever depending on derivation
            // succeeding for a program shape that might change later). worldBodies[]'s own
            // boundRadius field (computed in BuildCornellWorldSpaceBodies, shared with the
            // baked path's own bake-band sizing) is reused directly here rather than
            // re-derived.
            std::vector<Vixen::SVO::BodyInstanceGpu> virtualBodies;
            virtualBodies.reserve(worldBodies.size());
            bool allRegistered = true;
            for (size_t i = 0; i < worldBodies.size(); ++i) {
                const CornellWorldSpaceBody& b = worldBodies[i];
                const uint32_t recipeId = static_cast<uint32_t>(2 + i);

                Vixen::SVO::RecipeRegistry::RecipeEntry entry{};
                entry.bytecode = b.prog;
                entry.boundCenter = b.worldCenter;
                entry.boundRadius = b.boundRadius;

                auto regResult = RegisterProceduralRecipe(recipeId, entry);
                if (regResult != Vixen::SVO::RecipeRegistry::RegisterResult::Ok) {
                    mainLogger->Error(std::string("[BuildRenderGraph] VIXEN_DDGI_CORNELL_VIRTUAL_DEMO: "
                                     "RegisterProceduralRecipe(") + b.name + ") failed, code " +
                                     std::to_string(static_cast<int>(regResult)));
                    allRegistered = false;
                    continue;
                }

                Vixen::SVO::BodyInstanceGpu inst{};
                inst.worldPos[0] = 0.0f; inst.worldPos[1] = 0.0f; inst.worldPos[2] = 0.0f;  // unused: field samples world p directly
                inst.renderScale = 1.0f;  // unused by Procedural
                inst.color[0] = b.color.x; inst.color[1] = b.color.y; inst.color[2] = b.color.z;
                inst.octreeIndex = 0u;    // unused by Procedural
                inst.providerKind = 1u;   // PROVIDER_PROCEDURAL
                inst.recipeId = recipeId;
                virtualBodies.push_back(inst);
            }

            if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                bodyScene->SetInstances(std::move(virtualBodies));
                mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_VIRTUAL_DEMO: seeded " +
                                  std::to_string(worldBodies.size()) + " zero-bake procedural body "
                                  "instances (5 walls + 1 ceiling light + 2 objects), allRegistered=" +
                                  (allRegistered ? std::string("true") : std::string("false")));
            }

            // Light-tree cut: a SEPARATE, explicitly-scoped side bake of ONLY the light body's
            // geometry+emission (see this block's own header comment on why this is required and
            // why it is not counted against the zero-bake proof). Mirrors M1's own
            // BakeRecipeInstructionsToSdfWorldWithEmission call almost verbatim -- same recipe
            // shape (a single RoundedBox), same emission lambda, same light-tree-cut pipeline --
            // the ONLY difference is this bake's SdfBodyOctree is used solely to derive the
            // light-tree cut and is never fed to SetRecipePool (the light body's own visible
            // pixels come entirely from the procedural recipe registered above, recipeId=7).
            {
                constexpr int kLightBakeN = 16;
                constexpr float kBand = 2.0f;
                const glm::vec3 kLightBakeCenter(static_cast<float>(kLightBakeN) * 0.5f);
                std::vector<SdfInstruction> lightLocalProg = {
                    CornellWorldBoxAt(kLightBakeCenter, kLightHalfExtent, 0.05f)  // authored at grid-local center, not world center
                };
                ++g_cornellVirtualLightTreeSideBakeCount;
                Vixen::SVO::SdfBakeResult lightBaked = Vixen::SVO::BakeRecipeInstructionsToSdfWorldWithEmission(
                    lightLocalProg.data(), static_cast<uint32_t>(lightLocalProg.size()), kLightBakeCenter,
                    kLightBakeN, kBand,
                    [](const glm::vec3&) { return kLightEmissionIntensity; });
                Vixen::SVO::SdfBodyOctree lightBody = Vixen::SVO::BuildSdfBodyOctree(lightBaked, 3);

                const Vixen::SVO::Octree* lightOct = lightBody.octree->getOctree();
                if (lightOct == nullptr) {
                    mainLogger->Error("[BuildRenderGraph] VIXEN_DDGI_CORNELL_VIRTUAL_DEMO: light-tree "
                                       "side-bake octree is null -- no bounce lighting from the ceiling light");
                } else {
                    Vixen::SVO::SerializedOctree lightSer = Vixen::SVO::SerializeSdf(lightBody);
                    Vixen::SVO::BakeAndAttachMipPool(*lightOct, lightSer);
                    Vixen::SVO::MipPool lightMipPool = Vixen::SVO::BakeMipPool(*lightOct, lightSer);

                    Vixen::SVO::LightTreeCutParams cutParams;
                    cutParams.powerThreshold = 0.001f;  // same fine-cut rationale as M1's baked variant
                    std::vector<Vixen::SVO::LightTreeNode> cut =
                        Vixen::SVO::BuildLightTreeCut(*lightOct, lightSer, lightMipPool, kLightBakeN, cutParams);

                    // Grid->world transform: this side bake's grid is centered on kLightBakeCenter
                    // (grid-local), and the recipe itself is authored at that SAME grid-local
                    // center (worldBoxAt(kLightBakeCenter, ...) above) -- so grid (kLightBakeN/2,
                    // kLightBakeN/2, kLightBakeN/2) maps to world kLightCenter. worldPos/
                    // renderScale below encode exactly that mapping, mirroring M1's own
                    // kLightWorldPos/kLightRenderScale derivation (bodyWorldPos/bodyRenderScale)
                    // but specialized to this side bake's kSmallSubdiv=1, kSmallN=kLightBakeN case.
                    constexpr float kWorldGridSize = 10.0f;  // ShellOctreeGpu.h's fixed octree-local->world span
                    const float lightRenderScale = static_cast<float>(kLightBakeN) / kWorldGridSize;
                    const glm::vec3 lightWorldPos = kLightCenter - glm::vec3(static_cast<float>(kLightBakeN) * 0.5f);

                    std::vector<Vixen::SVO::LightTreeNode> worldCut;
                    worldCut.reserve(cut.size());
                    for (const auto& node : cut) {
                        Vixen::SVO::LightTreeNode w = node;
                        w.worldPos = lightWorldPos + (node.worldPos / static_cast<float>(kLightBakeN)) * kWorldGridSize * lightRenderScale;
                        w.worldExtent = (node.worldExtent / static_cast<float>(kLightBakeN)) * kWorldGridSize * lightRenderScale;
                        worldCut.push_back(w);
                    }

                    if (auto* lightTreeInst = static_cast<LightTreeBufferNode*>(renderGraph->GetInstance(lightTreeBufferNode))) {
                        lightTreeInst->SetLightTreeCut(worldCut);
                    }
                    mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_VIRTUAL_DEMO: light-tree "
                                      "side-bake cut=" + std::to_string(cut.size()) + " nodes (side bake "
                                      "count=" + std::to_string(g_cornellVirtualLightTreeSideBakeCount) +
                                      ", NOT counted against the zero-bake render-path proof)");
                }
            }

            // Force-enable the DDGI probe grid -- same reasoning as M1's baked variant above.
#if defined(_WIN32)
            _putenv_s("VIXEN_PROBE_GRID_CONFIG_ENABLED", "1");
#else
            setenv("VIXEN_PROBE_GRID_CONFIG_ENABLED", "1", 1);
#endif
            mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_VIRTUAL_DEMO: force-enabled "
                              "VIXEN_PROBE_GRID_CONFIG_ENABLED=1 (probe grid must be on for this "
                              "demo's own point -- visible bounce lighting -- to be visible at all)");
        } else if (std::getenv("VIXEN_DDGI_LEAK_GATE_DEMO") || std::getenv("VIXEN_DDGI_EDIT_LOOP_DEMO")) {
            // Sampled Lighting Inc4 M6 reuses this EXACT scene (geometry, probe placement,
            // near/far indices) for the edit-loop responsiveness gate when
            // VIXEN_DDGI_EDIT_LOOP_DEMO=1 -- same "don't invent a new mechanism" discipline
            // M4's own gate used relative to VIXEN_RESTIR_GATE_DEMO. The only behavioral
            // difference (isEditLoopMode below): the light-tree cut is built EMPTY at scene-
            // construction time (the source starts "off") and the REAL cut is stashed via
            // g_ddgiEditLoopWorldCut for VulkanGraphApplication.cpp's readback hook to flip in
            // live at a chosen tick -- a genuine mid-run scene-content edit, not a restart.
            const bool isEditLoopMode = std::getenv("VIXEN_DDGI_EDIT_LOOP_DEMO") != nullptr;

            // Sampled Lighting Inc4 M4 live gate: the leak-test scene the plan's Task 4
            // requires -- thin-wall occluder geometry between an emissive source and a
            // probe/observation point, proving the Chebyshev-tested gather does NOT leak
            // light through the wall while an ablation (Chebyshev test disabled) DOES.
            // Mirrors VIXEN_RESTIR_GATE_DEMO's own shape (emissive body -> light-tree cut
            // -> world-transform -> stash for the CPU readback hook) but ALSO bakes a
            // second, non-emissive body (the thin wall) and configures ProbeGridConfig +
            // the DDGILeakGateDebug SSBO so ProbeUpdate.comp's M4 self-test gather has a
            // concrete near/far probe pair + shading point to exercise.
            //
            // Geometry (world space) -- MUST live inside ProbeGridConfigNode's own default
            // grid coverage: origin (0,0,0), spacing 4, count 8x8x8 -> world [0,32) on every
            // axis (ProbeGridConfigNode has no live setter this milestone, see below). A first
            // attempt at this scene placed the bodies at the default camera's (64,64,64)
            // framing (VIXEN_RESTIR_GATE_DEMO's own convention) -- a live-gate run caught that
            // this put the near/far probes OUTSIDE the grid's [0,32) coverage entirely
            // (silently producing a probeIndex >= probeCount workgroup no-op, both readbacks
            // reading zero with no way to tell "correctly rejected" from "never ran" -- a false
            // negative-control result). Rescaled below (kRenderScale=1.0, not 4.8) so both
            // bodies AND the near/far probes/shading point land inside [0,32):
            //   emissive source: a small emissive sphere at grid (10,16,16) -> world X~=5.1.
            //   thin wall: a box occluder at grid (22,16,16) -> world X~=8.9, spanning world
            //     X~=[8.56,9.19] (thin -- 1 grid-unit half-extent -> ~0.31 world units either
            //     side), full Y/Z span (a real wall, not a small block seen around).
            //   near probe: grid (1,4,4) -> world (4,16,16) -- before the source (world X~=5.1),
            //     no occluder between it and the source.
            //   far probe / shading point: grid (3,4,4) -> world (12,16,16) -- past the wall's
            //     far face (world X~=9.19), occluded from the source. The gather samples the
            //     NEAR probe's atlas entry AT this far shading point -- Chebyshev-tested
            //     visibility must reject it (the near probe's own stored ray-hit distance
            //     toward the wall is much closer than the near-probe-to-far-point test
            //     distance), while the ablation (test forced to 1) must NOT reject it, proving
            //     the scene leaks without the mechanism.
            mainLogger->Info(std::string("[BuildRenderGraph] ") +
                              (isEditLoopMode ? "VIXEN_DDGI_EDIT_LOOP_DEMO: building the M6 "
                                                "edit-loop scene (source starts OFF)"
                                              : "VIXEN_DDGI_LEAK_GATE_DEMO: building the M4 "
                                                "thin-wall leak-test gate scene"));

            constexpr int   kN    = 32;
            constexpr float kBand = 2.0f;
            const glm::vec3 kSourceCenter(10.0f, 16.0f, 16.0f);   // grid-space center (kN=32 grid), world-mapped below
            constexpr float kSourceRadius = 8.0f;  // sized comparably to VIXEN_RESTIR_GATE_DEMO's own proven r=10/n=32 scene

            Vixen::SVO::RecipeParams sourceParams{kSourceRadius, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
            Vixen::SVO::SdfBakeResult sourceBaked = Vixen::SVO::BakeRecipeToSdfWorldWithEmission(
                Vixen::SVO::RECIPE_SPHERE, kSourceCenter, sourceParams, kN, kBand,
                [](const glm::vec3&) { return 4.0f; });  // uniform emissive intensity
            Vixen::SVO::SdfBodyOctree sourceBody = Vixen::SVO::BuildSdfBodyOctree(sourceBaked, 3);

            // Thin wall: a genuine box SDF (no RECIPE_BOX exists in SdfRecipes.h -- BakeSdfWorld's
            // templated `eval` accepts ANY lambda, so a box distance function is authored directly
            // here rather than approximating one out of RECIPE_SPHERE/RECIPE_DISPLACED_SPHERE).
            // Grid-space half-extents: thin along X (2 grid units -> genuinely thin at this bake's
            // world scale below), full-span along Y/Z (a real wall, not a small block the source
            // could simply be seen around).
            const glm::vec3 kWallCenter(22.0f, 16.0f, 16.0f);
            const glm::vec3 kWallHalfExtent(1.0f, 15.0f, 15.0f);
            auto wallSdf = [&](const glm::vec3& p) {
                glm::vec3 q = glm::abs(p - kWallCenter) - kWallHalfExtent;
                glm::vec3 qPos = glm::max(q, glm::vec3(0.0f));
                float outside = glm::length(qPos);
                float inside = std::min(std::max(q.x, std::max(q.y, q.z)), 0.0f);
                return outside + inside;
            };
            Vixen::SVO::SdfBakeResult wallBaked = Vixen::SVO::BakeSdfWorld(wallSdf, kWallCenter, kN, kBand, 3);
            Vixen::SVO::SdfBodyOctree wallBody = Vixen::SVO::BuildSdfBodyOctree(wallBaked, 3);

            const Vixen::SVO::Octree* sourceOct = sourceBody.octree->getOctree();
            if (sourceOct != nullptr) {
                Vixen::SVO::SerializedOctree sourceSer = Vixen::SVO::SerializeSdf(sourceBody);
                Vixen::SVO::BakeAndAttachMipPool(*sourceOct, sourceSer);
                Vixen::SVO::MipPool sourceMipPool = Vixen::SVO::BakeMipPool(*sourceOct, sourceSer);

                Vixen::SVO::LightTreeCutParams cutParams;
                cutParams.powerThreshold = 0.001f;  // fine cut -- same rationale as VIXEN_RESTIR_GATE_DEMO
                std::vector<Vixen::SVO::LightTreeNode> cut =
                    Vixen::SVO::BuildLightTreeCut(*sourceOct, sourceSer, sourceMipPool, kN, cutParams);
                for (size_t di = 0; di < cut.size() && di < 5; ++di) {
                    const auto& n = cut[di];
                    mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_LEAK_GATE_DEMO: DIAG rawGridCutNode[" + std::to_string(di) +
                                      "] gridPos=(" + std::to_string(n.worldPos.x) + "," + std::to_string(n.worldPos.y) + "," + std::to_string(n.worldPos.z) +
                                      ") gridExtent=" + std::to_string(n.worldExtent));
                }

                // Body-placement/world-transform: SAME shape as VIXEN_RESTIR_GATE_DEMO's own
                // gate scene (renderScale + world-grid-size + a fixed worldPos origin), but
                // renderScale=1.0 (NOT 4.8) -- the whole point of this rescale is keeping both
                // bodies AND the near/far probes inside ProbeGridConfigNode's default [0,32)
                // grid coverage (see this block's own header comment on why the first attempt's
                // renderScale=4.8/world-center placement put the probes outside the grid
                // entirely). World diameter = kWorldGridSize*kRenderScale = 10; sourceWorldPos
                // chosen so grid (0,0,0) maps to world (2,8,8) -- keeps the whole 10-unit body
                // span + the near/far probes' Y/Z=16 comfortably inside [0,32) on every axis.
                constexpr float kRenderScale = 1.0f;
                constexpr float kWorldGridSize = 10.0f;
                const glm::vec3 sourceWorldPos(2.0f, 8.0f, 8.0f);

                std::vector<Vixen::SVO::LightTreeNode> worldCut;
                worldCut.reserve(cut.size());
                for (const auto& node : cut) {
                    Vixen::SVO::LightTreeNode w = node;
                    const glm::vec3 pBase = (node.worldPos / static_cast<float>(kN)) * kWorldGridSize;
                    w.worldPos = pBase * kRenderScale + sourceWorldPos;
                    w.worldExtent = (node.worldExtent / static_cast<float>(kN)) * kWorldGridSize * kRenderScale;
                    worldCut.push_back(w);
                }

                if (auto* lightTreeInst = static_cast<LightTreeBufferNode*>(renderGraph->GetInstance(lightTreeBufferNode))) {
                    // Edit-loop mode: start with an EMPTY cut (source "off" -- zero light-tree
                    // content for the probe-update pass to sample) and stash the REAL cut for
                    // VulkanGraphApplication.cpp's readback hook to flip in live at a chosen
                    // tick. Leak-gate mode (default): apply the real cut immediately, unchanged
                    // from M4.
                    if (isEditLoopMode) {
                        lightTreeInst->SetLightTreeCut({});
                        extern std::vector<Vixen::SVO::LightTreeNode>* g_ddgiEditLoopWorldCut;
                        static std::vector<Vixen::SVO::LightTreeNode> editLoopCutStash = worldCut;
                        g_ddgiEditLoopWorldCut = &editLoopCutStash;
                    } else {
                        lightTreeInst->SetLightTreeCut(worldCut);
                    }
                }
                mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_LEAK_GATE_DEMO: cut=" + std::to_string(cut.size()) + " nodes");
                for (size_t di = 0; di < worldCut.size() && di < 5; ++di) {
                    const auto& n = worldCut[di];
                    mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_LEAK_GATE_DEMO: DIAG cutNode[" + std::to_string(di) +
                                      "] worldPos=(" + std::to_string(n.worldPos.x) + "," + std::to_string(n.worldPos.y) + "," + std::to_string(n.worldPos.z) +
                                      ") worldExtent=" + std::to_string(n.worldExtent) +
                                      " intensity=" + std::to_string(n.intensity) + " coverage=" + std::to_string(n.coverage));
                }

                // Instance placement: source body + wall body, both mapped via the SAME
                // grid->world transform (kRenderScale/kWorldGridSize/sourceWorldPos above) so
                // both bodies' grid-space centers/extents land at consistent world positions
                // relative to each other -- source at grid (10,16,16) -> world X~=5.1, wall at
                // grid (22,16,16) -> world X~=8.9 (see this block's own header comment for the
                // full derivation). Both bodies share ONE octree pool slot each (octreeIndex
                // 0/1) via
                // ConcatenateSdfWithMips below, same multi-body pattern VIXEN_TIER_OBSERVABLE_DEMO
                // uses (BAKE separate octrees, concatenate, place with distinct BodyInstanceGpu
                // entries).
                std::vector<const Vixen::SVO::SdfBodyOctree*> octreesForCat = {&sourceBody, &wallBody};
                Vixen::SVO::ConcatenatedOctrees cat = Vixen::SVO::ConcatenateSdfWithMips(octreesForCat);

                if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                    Vixen::SVO::BodyInstanceGpu sourceInst{};
                    sourceInst.worldPos[0]  = sourceWorldPos.x;
                    sourceInst.worldPos[1]  = sourceWorldPos.y;
                    sourceInst.worldPos[2]  = sourceWorldPos.z;
                    sourceInst.renderScale  = kRenderScale;
                    sourceInst.color[0]     = 1.0f; sourceInst.color[1] = 1.0f; sourceInst.color[2] = 1.0f;
                    sourceInst.octreeIndex  = 0u;
                    sourceInst.providerKind = 0u;  // PROVIDER_STORED
                    sourceInst.recipeId     = 0u;

                    Vixen::SVO::BodyInstanceGpu wallInst{};
                    wallInst.worldPos[0]  = sourceWorldPos.x;  // SAME grid->world transform as sourceInst
                    wallInst.worldPos[1]  = sourceWorldPos.y;
                    wallInst.worldPos[2]  = sourceWorldPos.z;
                    wallInst.renderScale  = kRenderScale;
                    wallInst.color[0]     = 0.7f; wallInst.color[1] = 0.7f; wallInst.color[2] = 0.7f;
                    wallInst.octreeIndex  = 1u;
                    wallInst.providerKind = 0u;  // PROVIDER_STORED
                    wallInst.recipeId     = 0u;

                    bodyScene->SetRecipePool(std::move(cat));
                    bodyScene->SetInstances({sourceInst, wallInst});
                    mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_LEAK_GATE_DEMO: seeded emissive-source + thin-wall body instances");
                }

                // ProbeGridConfig: ProbeGridConfigNode has no live setter this milestone
                // (MakeDefaultProbeGridConfig is a file-local default, flipped on via
                // VIXEN_PROBE_GRID_CONFIG_ENABLED=1 -- see that file) -- the gate reuses that
                // SAME default grid (origin (0,0,0), spacing 4, count 8x8x8, world coverage
                // [0,32) on every axis -- MUST match this block's own geometry, hence the
                // kRenderScale=1.0/sourceWorldPos=(2,8,8) rescale above) and picks near/far
                // probe INDICES that land at valid, in-range grid nodes near the source/wall/
                // far-point positions, avoiding a new live setter for a one-off gate (mirrors
                // VIXEN_RESTIR_GATE_DEMO's own "reuse existing plumbing, don't add a new
                // mechanism" discipline). World position of probe (px,py,pz) = (px*4,py*4,pz*4).
                // Near probe at grid (2,3,3) -> world (8,12,12): distance ~3.2 from the source's
                // own world center/radius (5.125,13,13)/2.5 -- outside the source's own solid
                // geometry (a probe placed INSIDE the emissive sphere's solid volume gave a
                // degenerate all-zero TraceWorld result, a live-gate finding from an earlier
                // pass with a smaller kSourceRadius=3 where the near probe sat inside the
                // sphere -- kSourceRadius was widened to 8 AND the near probe moved outside it),
                // and before the wall's near face (world X~=8.56), no occluder in the way. Far
                // probe/shading point at grid (3,3,3) -> world (12,12,12): past the wall's far
                // face (world X~=9.19), occluded from the source. Both indices are well inside
                // [0, countX*countY*countZ=512) -- an EARLIER live-gate bug this geometry
                // rescale also fixed was an out-of-range index silently no-op'ing the gather
                // workgroup (see this block's own header comment).
                constexpr uint32_t kNearProbeX = 2u, kFarProbeX = 3u, kProbeY = 3u, kProbeZ = 3u;
                constexpr uint32_t kDefaultCountX = 8u, kDefaultCountY = 8u;
                const uint32_t nearProbeIndex = kNearProbeX + kProbeY * kDefaultCountX + kProbeZ * kDefaultCountX * kDefaultCountY;
                const uint32_t farProbeIndex  = kFarProbeX  + kProbeY * kDefaultCountX + kProbeZ * kDefaultCountX * kDefaultCountY;
                const glm::vec3 farShadingPos(static_cast<float>(kFarProbeX) * 4.0f,
                                               static_cast<float>(kProbeY) * 4.0f,
                                               static_cast<float>(kProbeZ) * 4.0f);

                ddgiLeakGateNearProbeIndex_ = nearProbeIndex;
                ddgiLeakGateFarProbeIndex_  = farProbeIndex;
                ddgiLeakGateFarShadingPos_  = farShadingPos;
                mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_LEAK_GATE_DEMO: nearProbeIndex=" +
                                  std::to_string(nearProbeIndex) + " farProbeIndex=" + std::to_string(farProbeIndex) +
                                  " farShadingPos=(" + std::to_string(farShadingPos.x) + "," +
                                  std::to_string(farShadingPos.y) + "," + std::to_string(farShadingPos.z) + ")");
            } else {
                mainLogger->Error("[BuildRenderGraph] VIXEN_DDGI_LEAK_GATE_DEMO: source body octree is null -- gate scene not built");
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

    // Sampled Lighting Inc4 M2: probe grid config node connections (same ring pattern as
    // reservoirConfigNode above). M2 scaffolding only -- no shader consumes this buffer yet.
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  probeGridConfigNode, ProbeGridConfigNodeConfig::VULKAN_DEVICE_IN)
         .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  probeGridConfigNode, ProbeGridConfigNodeConfig::CURRENT_FRAME_INDEX);

    // Sampled Lighting Inc4 M2: probe atlas image connections -- device + command pool drive
    // allocation + the one-shot UNDEFINED->GENERAL transition, mirroring accumulationHistoryNode's
    // own wiring above. Extent/format are Setup PARAMETERS (already set in PHASE 2), not graph
    // inputs -- see ProbeAtlasNodeConfig.h's own scope note on why atlas dims don't follow a
    // live extent-cascade like the render target does.
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  probeIrradianceAtlasNode, ProbeAtlasNodeConfig::VULKAN_DEVICE_IN)
         .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL,
                  probeIrradianceAtlasNode, ProbeAtlasNodeConfig::COMMAND_POOL)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  probeVisibilityAtlasNode, ProbeAtlasNodeConfig::VULKAN_DEVICE_IN)
         .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL,
                  probeVisibilityAtlasNode, ProbeAtlasNodeConfig::COMMAND_POOL);

    // Sampled Lighting Inc4 M2: gather both atlas IRenderTarget* handles into one
    // IMAGE_WRITE_ARRAY-shaped array (Inc4 M1's ImageSyncGathererNode). No ComputeStageNode
    // consumes IMAGE_ARRAY yet this milestone (that's M3's probe-update pass) -- these
    // connections exist purely so the atlas Resource*s are wired through the sync-gathering
    // mechanism the way M3 will need, proven correct now rather than discovered at M3.
    // execDep (Dependency|Execute): VariadicConnectionRule only populates a variadic slot's
    // actual Resource* via a PostCompile hook when the connection carries SlotRole::Dependency
    // (mirrors directLightingReadGatherer's own identical connections above).
    batch.Connect(probeIrradianceAtlasNode, ProbeAtlasNodeConfig::PROBE_ATLAS,
                  probeAtlasGatherer, 0, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(probeVisibilityAtlasNode, ProbeAtlasNodeConfig::PROBE_ATLAS,
                  probeAtlasGatherer, 1, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    // Sampled Lighting Inc4 M5: the SAME two ProbeAtlasNode outputs, gathered a SECOND time
    // into spatialReuseProbeAtlasReadGatherer (the read-side instance) -- same source
    // Resource*s as probeAtlasGatherer above, so the constituent expansion pairs correctly
    // against probeUpdateNode's IMAGE_WRITE_ARRAY writer on those SAME atlas Resource*s.
    batch.Connect(probeIrradianceAtlasNode, ProbeAtlasNodeConfig::PROBE_ATLAS,
                  spatialReuseProbeAtlasReadGatherer, 0, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(probeVisibilityAtlasNode, ProbeAtlasNodeConfig::PROBE_ATLAS,
                  spatialReuseProbeAtlasReadGatherer, 1, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

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

    // Sampled Lighting Inc3 M6: spatial-combine debug buffer — same device + extent-driven
    // sizing pattern as reservoirBufferA/B immediately above.
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  spatialReservoirDebugBuffer, StorageBufferNodeConfig::VULKAN_DEVICE_IN)
         .Connect(renderTargetNode, RenderTargetNodeConfig::RENDER_TARGET,
                  spatialReservoirDebugBuffer, StorageBufferNodeConfig::SWAPCHAIN_INFO);

    // Sampled Lighting Inc4 M4: DDGI leak-test gate debug buffer — device only, NO
    // SWAPCHAIN_INFO connection (fixed PARAM_SIZE_BYTES sizing set above, not extent-driven).
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  ddgiLeakGateDebugBuffer, StorageBufferNodeConfig::VULKAN_DEVICE_IN);

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

    // Lazy-Procedural-Delta-Baseline Inc0 M6 Task 13: Binding 16: coarse occupancy grid
    // SSBO (concatenated per-recipe min-|sd| grids for empty-space skip / far early-out
    // in traceUberRecipeBody). Placeholder 1-byte buffer for a scene where no registered
    // recipe has a derivable grid; read by the shader only when getRecipeOccupancyGrid
    // reports gridDim>0 for the current recipeId (see SdfRecipes.glsl).
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::OCTREE_OCCUPANCYGRID_BUFFER,
                          descriptorGatherer, 16,  // Binding 16: OccupancyGridBuffer
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] Connected recipe occupancy grid at binding 16 (Lazy-Procedural-Delta-Baseline Inc0 M6 Task 13)");
    }

    // Sampled Lighting Inc0 M3: Binding 17: LightingConfig SSBO (single record, re-uploaded
    // per-frame from LightingConfigNode's ring). Default content = one directional light
    // matching Lighting.glsl's previous hardcoded default (zero-visual-delta gate).
    batch.Connect(lightingConfigNode, LightingConfigNodeConfig::LIGHTING_CONFIG_BUFFER,
                          descriptorGatherer, 17,  // Binding 17: LightingConfigSSBO
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] Connected lighting config at binding 17 (Sampled Lighting Inc0 M3)");
    }

    // Sampled Lighting Inc1 M3: Binding 18: HitRecord SSBO. Device input + extent-driven sizing
    // from renderTargetNode's own RENDER_TARGET output (NOT the raw swapchain) — so this buffer
    // always matches outputImage's real per-dispatch extent (including under render-scale <1.0),
    // same as descriptorGatherer binding 0 below. This makes hitRecordBufferNode a transitive
    // dependent of renderTargetNode and rides the identical resize->recompile cascade.
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  hitRecordBufferNode, StorageBufferNodeConfig::VULKAN_DEVICE_IN)
         .Connect(renderTargetNode, RenderTargetNodeConfig::RENDER_TARGET,
                  hitRecordBufferNode, StorageBufferNodeConfig::SWAPCHAIN_INFO);

    batch.Connect(hitRecordBufferNode, StorageBufferNodeConfig::STORAGE_BUFFER,
                          descriptorGatherer, 18,  // Binding 18: HitRecordBuffer
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] Connected HitRecord SSBO at binding 18 (Sampled Lighting Inc1 M3)");
    }

    // Sampled Lighting Inc1 M4: Binding 19: ShadowConfig SSBO (single record, re-uploaded
    // per-frame from ShadowConfigNode's ring). Default content = enabled hard shadows,
    // whole-scene reach, tuned bias (see ShadowConfigNode.cpp's MakeDefaultShadowConfig).
    batch.Connect(shadowConfigNode, ShadowConfigNodeConfig::SHADOW_CONFIG_BUFFER,
                          descriptorGatherer, 19,  // Binding 19: ShadowConfigSSBO
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] Connected shadow config at binding 19 (Sampled Lighting Inc1 M4)");
    }

    // Sampled Lighting Inc2 M1: Binding 20: AccumulationConfig SSBO (single record, re-uploaded
    // per-frame from AccumulationConfigNode's ring). Default content = enabled=0 (pure
    // passthrough — this milestone's byte-identity gate vs Inc1).
    batch.Connect(accumulationConfigNode, AccumulationConfigNodeConfig::ACCUMULATION_CONFIG_BUFFER,
                          descriptorGatherer, 20,  // Binding 20: AccumulationConfigSSBO
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] Connected accumulation config at binding 20 (Sampled Lighting Inc2 M1)");
    }

    // Sampled Lighting Inc2 M1: Binding 21: historyImage (persistent R8G8B8A8_UNORM storage
    // image, AccumulationHistoryNode). Declared in the shader but not yet read/written this
    // milestone (M2 consumes it) — a pure plumbing wire. Execute-only, mirroring
    // pickIdTargetNode's own binding-9 storage-image wiring above (re-emitted each frame; no
    // compile-time dependency edge needed beyond the initial Compile-time publish).
    batch.Connect(accumulationHistoryNode, AccumulationHistoryNodeConfig::HISTORY_IMAGE_VIEW,
                          descriptorGatherer, 21,  // Binding 21: historyImage
                          SlotRoleModifier(SlotRole::Execute));

    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] Connected accumulation history image at binding 21 (Sampled Lighting Inc2 M1)");
    }

    // Sampled Lighting Inc2 M3: Binding 22: PrevCameraConfig SSBO (single record, re-uploaded
    // per-frame from PrevCameraConfigNode's ring). Declared in the shader but not yet read
    // this milestone (M4 consumes it for reprojection) — a pure plumbing wire, mirroring
    // binding 20/21's own M1 plumbing-only precedent.
    batch.Connect(prevCameraConfigNode, PrevCameraConfigNodeConfig::PREV_CAMERA_CONFIG_BUFFER,
                          descriptorGatherer, 22,  // Binding 22: PrevCameraConfigSSBO
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] Connected prev-camera config at binding 22 (Sampled Lighting Inc2 M3)");
    }

    // Swapchain connections to descriptor set and dispatch
    // Pass swapchain public vars; DescriptorSetNode reads swapChainImageCount during Compile.
    // DESCRIPTOR_RESOURCES provides the actual bindings.
    batch.Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  computeDescriptorSet, DescriptorSetNodeConfig::SWAPCHAIN_INFO)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX,
                  computeDescriptorSet, DescriptorSetNodeConfig::IMAGE_INDEX)
         // Frame-index the descriptor SET OBJECTS (sync-reuse fix): set ring == flight ring the
         // per-flight fence guards. Same source that feeds computeDispatch's CURRENT_FRAME_INDEX.
         .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  computeDescriptorSet, DescriptorSetNodeConfig::CURRENT_FRAME_INDEX)
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
         // Frame-index the descriptor SET OBJECTS (sync-reuse fix): set ring == flight ring.
         .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  directLightingDescriptorSet, DescriptorSetNodeConfig::CURRENT_FRAME_INDEX)
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

    // Sampled Lighting Inc3 M4/M5: Bindings 25/26: reservoir CURRENT/PREVIOUS ping-pong SSBOs
    // (Vixen::Gpu::ReservoirRecord[], one per pixel). BOTH buffers are ALWAYS bound at BOTH
    // bindings 25/26 — DirectLighting.comp itself picks which is "current" (write) vs "previous"
    // (read, for TEMPORAL reuse — the previous FRAME's reservoir) each frame via
    // reservoirConfig.frameParity&1 (see ReservoirConfig.cs's own doc comment), so no CPU-side
    // rewiring/swap is needed frame-to-frame. This descriptor binding is purely the DATA path
    // (which buffer this SHADER instance sees at binding 25 vs 26); M4's own SAME-NODE cross-
    // FRAME write/read (temporal reuse, still true this milestone) needs no sync slot on ITS
    // OWN — same historyImage/worldPosHistoryImage precedent as before (the M2 Progress Log's
    // "benign intra-dispatch race" reasoning). M5 ADDS a genuine CROSS-DISPATCH, SAME-FRAME
    // hazard on TOP of this (SpatialReuseNode reading THIS pass's own just-written reservoirs,
    // including neighbors' pixels) — that hazard is declared separately via the array-hazard
    // write-gatherer/read-gatherer pair further below, NOT here (this connection stays plain
    // Dependency|Execute, matching every other descriptor-only binding in this function —
    // descriptor binding and sync-slot declaration are deliberately separate connections, per
    // ComputeStageNodeConfig's own doc comment).
    batch.Connect(reservoirBufferA, StorageBufferNodeConfig::STORAGE_BUFFER,
                          directLightingGatherer, 25,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(reservoirBufferB, StorageBufferNodeConfig::STORAGE_BUFFER,
                          directLightingGatherer, 26,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    // Binding 0 (outputImage): Sampled Lighting Inc3 M5 — DirectLighting no longer WRITES this
    // (SpatialReuseNode below is the genuine writer now); DirectLighting.comp keeps a read-only
    // binding purely for imageSize() (see that shader's own binding-0 comment), so it still needs
    // the descriptor bound — same renderTargetNode::CURRENT_VIEW source, just no sync-slot hazard
    // paired with it any more (moved to SpatialReuseNode's own IMAGE_WRITE below).
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

    // ===================================================================
    // Sampled Lighting Inc3 M5: SpatialReuseNode wiring — the second half of the pass
    // split (spatial reservoir reuse + shade, owns outputImage/historyImage/
    // worldPosHistoryImage). Mirrors DirectLightingNode's own descriptor-path/push-
    // constant/gatherer-binding shape exactly (same scene SSBOs, same config buffers);
    // only the reservoir-buffer ROLE (read here vs write in DirectLighting) and the
    // image-write ownership differ.
    // ===================================================================

    // SpatialReuse's own descriptor path (shaderLib -> gatherer -> descSet -> pipeline).
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  spatialReuseShaderLib, ShaderLibraryNodeConfig::VULKAN_DEVICE_IN)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  spatialReuseDescriptorSet, DescriptorSetNodeConfig::VULKAN_DEVICE_IN)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  spatialReusePipeline, ComputePipelineNodeConfig::VULKAN_DEVICE_IN)
         .Connect(spatialReuseShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  spatialReuseGatherer, DescriptorResourceGathererNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(spatialReuseGatherer, DescriptorResourceGathererNodeConfig::DESCRIPTOR_RESOURCES,
                  spatialReuseDescriptorSet, DescriptorSetNodeConfig::DESCRIPTOR_RESOURCES)
         .Connect(spatialReuseShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  spatialReuseDescriptorSet, DescriptorSetNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  spatialReuseDescriptorSet, DescriptorSetNodeConfig::SWAPCHAIN_INFO)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX,
                  spatialReuseDescriptorSet, DescriptorSetNodeConfig::IMAGE_INDEX)
         // Frame-index the descriptor SET OBJECTS (sync-reuse fix): set ring == flight ring.
         .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  spatialReuseDescriptorSet, DescriptorSetNodeConfig::CURRENT_FRAME_INDEX)
         .Connect(spatialReuseShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  spatialReusePipeline, ComputePipelineNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(spatialReuseDescriptorSet, DescriptorSetNodeConfig::DESCRIPTOR_SET_LAYOUT,
                  spatialReusePipeline, ComputePipelineNodeConfig::DESCRIPTOR_SET_LAYOUT);

    // SpatialReuse's own push-constant gatherer: SAME field sources as DirectLighting's own
    // gatherer (a third compiled program still needs its own reflected push-constant ranges).
    batch.Connect(spatialReuseShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  spatialReusePushConstantGatherer, PushConstantGathererNodeConfig::SHADER_DATA_BUNDLE);
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          spatialReusePushConstantGatherer, VoxelRayMarch::cameraPos::BINDING,
                          ExtractField(&CameraData::cameraPos, SlotRole::Execute));
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          spatialReusePushConstantGatherer, VoxelRayMarch::cameraDir::BINDING,
                          ExtractField(&CameraData::cameraDir, SlotRole::Execute));
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          spatialReusePushConstantGatherer, VoxelRayMarch::fov::BINDING,
                          ExtractField(&CameraData::fov, SlotRole::Execute));
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          spatialReusePushConstantGatherer, VoxelRayMarch::cameraUp::BINDING,
                          ExtractField(&CameraData::cameraUp, SlotRole::Execute));
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          spatialReusePushConstantGatherer, VoxelRayMarch::aspect::BINDING,
                          ExtractField(&CameraData::aspect, SlotRole::Execute));
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          spatialReusePushConstantGatherer, VoxelRayMarch::cameraRight::BINDING,
                          ExtractField(&CameraData::cameraRight, SlotRole::Execute));
    batch.Connect(inputNode, InputNodeConfig::INPUT_STATE,
                          spatialReusePushConstantGatherer, VoxelRayMarch::debugMode::BINDING,
                          ExtractField(&InputState::debugMode, SlotRole::Execute));
    if (tierCrossingLodCoefOverrideActive) {
        batch.Connect(tierCrossingLodCoefOverrideConstant, ConstantNodeConfig::OUTPUT,
                              spatialReusePushConstantGatherer, 8,
                              SlotRoleModifier(SlotRole::Execute));
    } else {
        batch.Connect(raySizeCoefNode, RaySizeCoefNodeConfig::RAY_SIZE_COEF,
                              spatialReusePushConstantGatherer, 8,
                              SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    }
    batch.Connect(raySizeBiasConstant, ConstantNodeConfig::OUTPUT,
                          spatialReusePushConstantGatherer, 9,
                          SlotRoleModifier(SlotRole::Execute));
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::INSTANCE_COUNT,
                          spatialReusePushConstantGatherer, 10,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(inputNode, InputNodeConfig::INPUT_STATE,
                          spatialReusePushConstantGatherer, 11,
                          ExtractField(&InputState::lastClickPixel, SlotRole::Execute));
    batch.Connect(accumulationConfigNode, AccumulationConfigNodeConfig::FRAME_COUNTER,
                          spatialReusePushConstantGatherer, 12,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    // SpatialReuse's descriptor bindings: same scene SSBOs (read-only, no hazard) as
    // DirectLighting's own gatherer above.
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::OCTREE_NODES_BUFFER,
                          spatialReuseGatherer, VoxelRayMarch::esvoNodes::BINDING,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::OCTREE_BRICKS_BUFFER,
                          spatialReuseGatherer, VoxelRayMarch::brickData::BINDING,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::OCTREE_MATERIALS_BUFFER,
                          spatialReuseGatherer, VoxelRayMarch::materials::BINDING,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::OCTREE_CONFIG_BUFFER,
                          spatialReuseGatherer, 5,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::INSTANCE_BUFFER,
                          spatialReuseGatherer, 10,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::SHELL_DATA_BUFFER,
                          spatialReuseGatherer, 11,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::SHELL_LOOKUP_BUFFER,
                          spatialReuseGatherer, 12,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::OCTREE_MIPPOOL_BUFFER,
                          spatialReuseGatherer, 13,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::OCTREE_TIERREFTABLE_BUFFER,
                          spatialReuseGatherer, 15,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(lightingConfigNode, LightingConfigNodeConfig::LIGHTING_CONFIG_BUFFER,
                          spatialReuseGatherer, 16,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(shadowConfigNode, ShadowConfigNodeConfig::SHADOW_CONFIG_BUFFER,
                          spatialReuseGatherer, 18,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(accumulationConfigNode, AccumulationConfigNodeConfig::ACCUMULATION_CONFIG_BUFFER,
                          spatialReuseGatherer, 19,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    // Binding 20 (historyImage): SpatialReuseNode now owns BOTH the read and the write (moved
    // from DirectLightingNode, M5) — Execute-only, same self-contained read/write-in-one-
    // dispatch pattern historyImage has always used.
    batch.Connect(accumulationHistoryNode, AccumulationHistoryNodeConfig::HISTORY_IMAGE_VIEW,
                          spatialReuseGatherer, 20,
                          SlotRoleModifier(SlotRole::Execute));
    batch.Connect(prevCameraConfigNode, PrevCameraConfigNodeConfig::PREV_CAMERA_CONFIG_BUFFER,
                          spatialReuseGatherer, 21,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    // Binding 22 (worldPosHistoryImage): SpatialReuseNode now owns BOTH the read (accumulate
    // seam's own reproject-validity check) and the write (moved from DirectLightingNode, M5) —
    // still Execute-only (same-node self-contained read/write-in-one-dispatch, no cross-submit
    // hazard — mirrors historyImage@20's own precedent exactly).
    batch.Connect(worldPosHistoryNode, WorldPosHistoryNodeConfig::WORLDPOS_IMAGE_VIEW,
                          spatialReuseGatherer, 22,
                          SlotRoleModifier(SlotRole::Execute));
    batch.Connect(reservoirConfigNode, ReservoirConfigNodeConfig::RESERVOIR_CONFIG_BUFFER,
                          spatialReuseGatherer, 23,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(lightTreeBufferNode, LightTreeBufferNodeConfig::LIGHT_TREE_BUFFER,
                          spatialReuseGatherer, 24,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    // Bindings 25/26 (reservoir A/B): SpatialReuseNode READS ONLY (DirectLightingNode is now the
    // sole writer, M5) — descriptor binding here, the genuine cross-dispatch READ hazard declared
    // via the array-hazard read-gatherer further below (paired with DirectLightingNode's own
    // array-hazard write-gatherer on the SAME two StorageBufferNode Resource*s).
    batch.Connect(reservoirBufferA, StorageBufferNodeConfig::STORAGE_BUFFER,
                          spatialReuseGatherer, 25,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(reservoirBufferB, StorageBufferNodeConfig::STORAGE_BUFFER,
                          spatialReuseGatherer, 26,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    // Binding 27 (spatial-combine debug buffer): Sampled Lighting Inc3 M6 — SpatialReuseNode
    // is the SOLE writer (debug/gate-only, never read by any shader), read back host-side by
    // the M6 gate after vkDeviceWaitIdle (same as reservoirBufferA/B's own gate-readback
    // precedent) — Execute-only wiring, no sync-slot/hazard declaration needed (no other GPU
    // consumer exists to race against).
    batch.Connect(spatialReservoirDebugBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                          spatialReuseGatherer, 27,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    // Bindings 32/33/34 (Sampled Lighting Inc4 M5): DDGI probe atlases + ProbeGridConfig —
    // SpatialReuseNode READS ONLY (probeUpdateNode is the sole writer). Same CURRENT_VIEW
    // precedent as probeUpdateGatherer's own bindings 29/30 (raw VkImageView, not the
    // IRenderTarget* PROBE_ATLAS output — see ProbeAtlasNodeConfig::CURRENT_VIEW's own doc
    // comment on why IRenderTarget* can never populate a descriptor slot, KI-028's
    // established discipline). The genuine cross-dispatch READ hazard against
    // probeUpdateNode's write is declared separately via IMAGE_READ_ARRAY above (same split
    // IMAGE_WRITE_ARRAY/descriptor-binding already uses) — these bindings are Execute-only.
    batch.Connect(probeIrradianceAtlasNode, ProbeAtlasNodeConfig::CURRENT_VIEW,
                          spatialReuseGatherer, 32,
                          SlotRoleModifier(SlotRole::Execute));
    batch.Connect(probeVisibilityAtlasNode, ProbeAtlasNodeConfig::CURRENT_VIEW,
                          spatialReuseGatherer, 33,
                          SlotRoleModifier(SlotRole::Execute));
    batch.Connect(probeGridConfigNode, ProbeGridConfigNodeConfig::PROBE_GRID_CONFIG_BUFFER,
                          spatialReuseGatherer, 34,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    // Binding 31 (M5 live-gate instrumentation): the SAME ddgi_leak_gate_debug_buffer
    // ProbeUpdate.comp's gatherer already binds at 31 -- SpatialReuseShade.comp additionally
    // writes shadeM5IndirectLuma into it (see DDGILeakGateDebugShade's own doc comment).
    // Read-write, same role shape as probeUpdateGatherer's own binding-31 connection.
    batch.Connect(ddgiLeakGateDebugBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                          spatialReuseGatherer, 31,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    // Binding 0 (outputImage): SpatialReuseNode is the genuine writer now (M5 — moved from
    // DirectLightingNode). Same renderTargetNode::CURRENT_VIEW source.
    batch.Connect(renderTargetNode, RenderTargetNodeConfig::CURRENT_VIEW,
                          spatialReuseGatherer, 0,
                          SlotRoleModifier(SlotRole::Execute));

    // Binding 17 (HitRecord): SpatialReuseNode reads it too (own-pixel AND neighbor shading) —
    // same march-write hazard HitRecord already has against DirectLightingNode; SpatialReuseNode
    // is a SECOND reader of the SAME already-synced buffer (read-after-read is not a NEW hazard
    // once the march->DirectLighting write->read edge already forces the write visible before
    // DirectLighting's group runs — SpatialReuseNode runs strictly after DirectLighting in
    // execution order, so the write is already visible to it with no additional slot needed).
    batch.Connect(hitRecordBufferNode, StorageBufferNodeConfig::STORAGE_BUFFER,
                          spatialReuseGatherer, 17,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    // --- SpatialReuseNode (ComputeStageNode) common inputs — mirrors DirectLightingNode's own
    // shape. NOT swapchain-adjacent: no SWAPCHAIN_INFO connection (isConsumer=false set above),
    // IMAGE_WRITE carries the render-target write (below). ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  spatialReuseNode, ComputeStageNodeConfig::VULKAN_DEVICE_IN)
         .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL,
                  spatialReuseNode, ComputeStageNodeConfig::COMMAND_POOL)
         .Connect(spatialReusePipeline, ComputePipelineNodeConfig::PIPELINE,
                  spatialReuseNode, ComputeStageNodeConfig::COMPUTE_PIPELINE)
         .Connect(spatialReusePipeline, ComputePipelineNodeConfig::PIPELINE_LAYOUT,
                  spatialReuseNode, ComputeStageNodeConfig::PIPELINE_LAYOUT)
         .Connect(spatialReuseDescriptorSet, DescriptorSetNodeConfig::DESCRIPTOR_SETS,
                  spatialReuseNode, ComputeStageNodeConfig::DESCRIPTOR_SETS)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX,
                  spatialReuseNode, ComputeStageNodeConfig::IMAGE_INDEX)
         .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  spatialReuseNode, ComputeStageNodeConfig::CURRENT_FRAME_INDEX)
         .Connect(frameSyncNode, FrameSyncNodeConfig::IN_FLIGHT_FENCE,
                  spatialReuseNode, ComputeStageNodeConfig::IN_FLIGHT_FENCE)
         .Connect(frameSyncNode, FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY,
                  spatialReuseNode, ComputeStageNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY)
         .Connect(swapChainNode, SwapChainNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY,
                  spatialReuseNode, ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY)
         .Connect(spatialReuseShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  spatialReuseNode, ComputeStageNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(spatialReusePushConstantGatherer, PushConstantGathererNodeConfig::PUSH_CONSTANT_DATA,
                  spatialReuseNode, ComputeStageNodeConfig::PUSH_CONSTANT_DATA)
         .Connect(spatialReusePushConstantGatherer, PushConstantGathererNodeConfig::PUSH_CONSTANT_RANGES,
                  spatialReuseNode, ComputeStageNodeConfig::PUSH_CONSTANT_RANGES)
         .Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_SEMAPHORE,
                  spatialReuseNode, ComputeStageNodeConfig::TIMELINE_SEMAPHORE_IN)
         .Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_FRAME_BASE,
                  spatialReuseNode, ComputeStageNodeConfig::TIMELINE_FRAME_BASE_IN);

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

    // Sampled Lighting Inc3 M5: reservoir ping-pong CROSS-DISPATCH hazard — the milestone's own
    // defining risk (see DirectLighting.comp's file header + the plan's Task 5 note). Unlike M4's
    // reservoir wiring (Execute-only, justified by the SAME-NODE cross-FRAME persistent-buffer/
    // fence precedent), THIS is a genuine cross-dispatch, SAME-FRAME hazard: SpatialReuseNode's
    // neighbor reads may land on ANY pixel DirectLightingNode wrote this frame, not just its own
    // pixel — so it needs a REAL declared edge, not incidental submit ordering. Both ping-pong
    // buffers (A and B) are gathered into DirectLightingNode's OWN write-array (via
    // directLightingReservoirWriteGatherer) and SpatialReuseNode's OWN read-array (via
    // spatialReuseReservoirReadGatherer) — because the array-hazard mechanism (see
    // ResourceAccessTracker::AddNode / Resource::hazardConstituents_) expands each gathered array
    // back into its true per-buffer Resource*s, this bakes the SAME 2 genuinely-independent-per-
    // buffer edges the fixed-slot design would have (reservoirBufferA: DirectLighting->
    // SpatialReuse, reservoirBufferB: DirectLighting->SpatialReuse), NOT one indivisible
    // array-wrapper hazard — see test_frame_sync_scheduler.cpp's FrameSyncArrayHazard tests for
    // the code-level proof of this expansion.
    batch.Connect(reservoirBufferA, StorageBufferNodeConfig::STORAGE_BUFFER,
                  directLightingReservoirWriteGatherer, 0, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(reservoirBufferB, StorageBufferNodeConfig::STORAGE_BUFFER,
                  directLightingReservoirWriteGatherer, 1, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(directLightingReservoirWriteGatherer, BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                  directLightingNode, ComputeStageNodeConfig::BUFFER_WRITE_ARRAY, SlotRoleModifier(SlotRole::Execute));

    batch.Connect(reservoirBufferA, StorageBufferNodeConfig::STORAGE_BUFFER,
                  spatialReuseReservoirReadGatherer, 0, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(reservoirBufferB, StorageBufferNodeConfig::STORAGE_BUFFER,
                  spatialReuseReservoirReadGatherer, 1, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(spatialReuseReservoirReadGatherer, BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                  spatialReuseNode, ComputeStageNodeConfig::BUFFER_READ_ARRAY, SlotRoleModifier(SlotRole::Execute));

    // Render target: SpatialReuseNode's IMAGE_WRITE <-> BlitNode's IMAGE_READ (wired below), same
    // renderTargetNode::RENDER_TARGET Resource* on both — bakes the SpatialReuse->BlitNode edge
    // (moved from DirectLightingNode, M5 — SpatialReuseNode is the genuine outputImage writer now).
    batch.Connect(renderTargetNode, RenderTargetNodeConfig::RENDER_TARGET,
                  spatialReuseNode, ComputeStageNodeConfig::IMAGE_WRITE, SlotRoleModifier(SlotRole::Execute));

    // Sampled Lighting Inc4 M5: SpatialReuseNode's IMAGE_READ_ARRAY <-> probeUpdateNode's
    // IMAGE_WRITE_ARRAY (wired further below), same two ProbeAtlasNode Resource*s on both
    // sides (via each side's own ImageSyncGathererNode instance) — bakes the genuine
    // probeUpdateNode(write)->spatialReuseNode(read) cross-dispatch SyncEdge this milestone's
    // gate must independently confirm. This is the FIRST real IMAGE_READ_ARRAY consumer.
    batch.Connect(spatialReuseProbeAtlasReadGatherer, ImageSyncGathererNodeConfig::IMAGE_ARRAY,
                  spatialReuseNode, ComputeStageNodeConfig::IMAGE_READ_ARRAY, SlotRoleModifier(SlotRole::Execute));

    // No separate ordering-only connection is needed for DirectLighting-before-SpatialReuse: the
    // reservoir BUFFER_WRITE_ARRAY (DirectLightingNode) <-> BUFFER_READ_ARRAY (SpatialReuseNode)
    // sync-slot connections above are themselves graph dependency edges on the SAME two
    // StorageBufferNode Resource*s (via each side's own gatherer) — sufficient for the
    // topological sort to place DirectLightingNode before SpatialReuseNode (mirrors the
    // march->DirectLightingNode pair above, which also has no separate ordering connection
    // beyond its own HitRecord sync slots).

    // ===================================================================
    // Sampled Lighting Inc4 M3: ProbeUpdateNode wiring — the DDGI probe-update pass
    // (ProbeUpdate.comp). Genuinely DISJOINT from DirectLighting/SpatialReuse (§5's
    // fan-out shape): reads the SAME resident scene (scene SSBOs, light-tree cut) but
    // writes ONLY the probe atlases via IMAGE_WRITE_ARRAY — never HitRecord, never
    // outputImage/historyImage/reservoirs. No sync-slot edge exists between this pass
    // and DirectLighting/SpatialReuse; auto-sync therefore bakes NO barrier between
    // them (verified at the gate below via the scheduler's actual baked SyncEdges, per
    // the plan's own "don't just trust it looks disjoint" instruction).
    // ===================================================================

    // ProbeUpdate's own descriptor path (shaderLib -> gatherer -> descSet -> pipeline).
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  probeUpdateShaderLib, ShaderLibraryNodeConfig::VULKAN_DEVICE_IN)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  probeUpdateDescriptorSet, DescriptorSetNodeConfig::VULKAN_DEVICE_IN)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  probeUpdatePipeline, ComputePipelineNodeConfig::VULKAN_DEVICE_IN)
         .Connect(probeUpdateShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  probeUpdateGatherer, DescriptorResourceGathererNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(probeUpdateGatherer, DescriptorResourceGathererNodeConfig::DESCRIPTOR_RESOURCES,
                  probeUpdateDescriptorSet, DescriptorSetNodeConfig::DESCRIPTOR_RESOURCES)
         .Connect(probeUpdateShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  probeUpdateDescriptorSet, DescriptorSetNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  probeUpdateDescriptorSet, DescriptorSetNodeConfig::SWAPCHAIN_INFO)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX,
                  probeUpdateDescriptorSet, DescriptorSetNodeConfig::IMAGE_INDEX)
         // Frame-index the descriptor SET OBJECTS (sync-reuse fix): set ring == flight ring.
         .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  probeUpdateDescriptorSet, DescriptorSetNodeConfig::CURRENT_FRAME_INDEX)
         .Connect(probeUpdateShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  probeUpdatePipeline, ComputePipelineNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(probeUpdateDescriptorSet, DescriptorSetNodeConfig::DESCRIPTOR_SET_LAYOUT,
                  probeUpdatePipeline, ComputePipelineNodeConfig::DESCRIPTOR_SET_LAYOUT);

    // ProbeUpdate.comp declares the SAME PushConstants block (via SceneBindings.glsl) as
    // every other scene-binding consumer, and reads NO camera/per-pixel-debug fields (no
    // camera ray, no per-pixel debug target — this pass is probe-indexed, not
    // screen-indexed) -- BUT it DOES need instanceCount (binding 10): TraceWorld/
    // TraceWorldShadow (TraceWorld.glsl) both bound their scene-instance iteration loop by
    // `pc.instanceCount` (`numInstances = clamp(pc.instanceCount, 0, 3*64)`), so an
    // unconnected/zero instanceCount makes EVERY probe ray a guaranteed miss regardless of
    // scene content -- an M4 live-gate finding (VIXEN_DDGI_LEAK_GATE_DEMO's own leak-test
    // gate initially read diagNearProbeHitCount=0 for a scene the march visibly renders,
    // isolating this exact gap) that the file header's PRIOR claim ("reads NONE of its
    // fields") missed; not previously caught because M3's own gate only checked "probes
    // visibly light a scene" qualitatively (a render happened), never a numeric hit count.
    batch.Connect(probeUpdateShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  probeUpdatePushConstantGatherer, PushConstantGathererNodeConfig::SHADER_DATA_BUNDLE);
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::INSTANCE_COUNT,
                          probeUpdatePushConstantGatherer, 10,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    // ProbeUpdate's descriptor bindings: the scene SSBOs (read-only, same as DirectLighting/
    // SpatialReuse's own gatherer bindings — read-read is not a hazard) + ProbeGridConfig
    // (binding 28) + the light-tree cut (binding 24, read-only, same buffer DirectLighting.comp
    // samples for RIS). NO HitRecord/reservoir/outputImage bindings — this pass never touches
    // those resources, the structural basis for its disjointness from the direct/ReSTIR pass.
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::OCTREE_NODES_BUFFER,
                          probeUpdateGatherer, VoxelRayMarch::esvoNodes::BINDING,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::OCTREE_BRICKS_BUFFER,
                          probeUpdateGatherer, VoxelRayMarch::brickData::BINDING,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::OCTREE_MATERIALS_BUFFER,
                          probeUpdateGatherer, VoxelRayMarch::materials::BINDING,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::OCTREE_CONFIG_BUFFER,
                          probeUpdateGatherer, 5,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::INSTANCE_BUFFER,
                          probeUpdateGatherer, 10,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::SHELL_DATA_BUFFER,
                          probeUpdateGatherer, 11,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::SHELL_LOOKUP_BUFFER,
                          probeUpdateGatherer, 12,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::OCTREE_MIPPOOL_BUFFER,
                          probeUpdateGatherer, 13,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::OCTREE_TIERREFTABLE_BUFFER,
                          probeUpdateGatherer, 15,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(lightTreeBufferNode, LightTreeBufferNodeConfig::LIGHT_TREE_BUFFER,
                          probeUpdateGatherer, 24,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(probeGridConfigNode, ProbeGridConfigNodeConfig::PROBE_GRID_CONFIG_BUFFER,
                          probeUpdateGatherer, 28,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    // Bindings 29/30 (probe atlases): descriptor binding here (plain gatherer, CURRENT_VIEW
    // handles) + the genuine write hazard declared via IMAGE_WRITE_ARRAY below (Inc4 M1's
    // mechanism — this pass writes BOTH atlases in the SAME dispatch, mirroring how
    // DirectLightingNode's reservoir write-array covers 2 buffers in one BUFFER_WRITE_ARRAY).
    //
    // Validator-found fix: MUST connect ProbeAtlasNodeConfig::CURRENT_VIEW (raw VkImageView), NOT
    // PROBE_ATLAS (IRenderTarget*) — IRenderTarget has no `conversion_type` (only an `operator
    // VkImageView()`), so a Resource holding an IRenderTarget* is typed PassThroughStorage and
    // GetDescriptorHandle() can never produce a VkImageView from it; vkUpdateDescriptorSets was
    // never actually called for these bindings (VUID-vkCmdDispatch-None-08114 on both). Same
    // precedent + role shape as DirectLighting/SpatialReuseShade's own binding-0 connection just
    // above (renderTargetNode's CURRENT_VIEW, not RENDER_TARGET, Execute-only — the hazard side is
    // declared separately via IMAGE_WRITE_ARRAY/probeAtlasGatherer below, same split those passes
    // use between their descriptor-binding Connect and their IMAGE_WRITE sync-slot Connect).
    batch.Connect(probeIrradianceAtlasNode, ProbeAtlasNodeConfig::CURRENT_VIEW,
                          probeUpdateGatherer, 29,
                          SlotRoleModifier(SlotRole::Execute));
    batch.Connect(probeVisibilityAtlasNode, ProbeAtlasNodeConfig::CURRENT_VIEW,
                          probeUpdateGatherer, 30,
                          SlotRoleModifier(SlotRole::Execute));

    // Binding 31 (Sampled Lighting Inc4 M4): DDGI leak-test gate debug SSBO — read-write
    // (the shader reads ddgiLeakGateEnabled/chebyshevTestEnabled/near-far probe indices +
    // farShadingPos the CPU sets, and writes gatheredLuma back), same Dependency|Execute
    // role shape every other read-then-write SSBO binding on this gatherer uses (e.g.
    // binding 28's ProbeGridConfig, also CPU-written then GPU-read every frame).
    batch.Connect(ddgiLeakGateDebugBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                          probeUpdateGatherer, 31,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    // --- ProbeUpdateNode (ComputeStageNode) common inputs — mirrors DirectLightingNode's own
    // shape. NOT swapchain-adjacent: no SWAPCHAIN_INFO connection (isConsumer=false set above),
    // IMAGE_WRITE_ARRAY carries the atlas writes instead (below), not the single-slot
    // IMAGE_WRITE DirectLighting/SpatialReuse use. ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  probeUpdateNode, ComputeStageNodeConfig::VULKAN_DEVICE_IN)
         .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL,
                  probeUpdateNode, ComputeStageNodeConfig::COMMAND_POOL)
         .Connect(probeUpdatePipeline, ComputePipelineNodeConfig::PIPELINE,
                  probeUpdateNode, ComputeStageNodeConfig::COMPUTE_PIPELINE)
         .Connect(probeUpdatePipeline, ComputePipelineNodeConfig::PIPELINE_LAYOUT,
                  probeUpdateNode, ComputeStageNodeConfig::PIPELINE_LAYOUT)
         .Connect(probeUpdateDescriptorSet, DescriptorSetNodeConfig::DESCRIPTOR_SETS,
                  probeUpdateNode, ComputeStageNodeConfig::DESCRIPTOR_SETS)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX,
                  probeUpdateNode, ComputeStageNodeConfig::IMAGE_INDEX)
         .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  probeUpdateNode, ComputeStageNodeConfig::CURRENT_FRAME_INDEX)
         .Connect(frameSyncNode, FrameSyncNodeConfig::IN_FLIGHT_FENCE,
                  probeUpdateNode, ComputeStageNodeConfig::IN_FLIGHT_FENCE)
         .Connect(frameSyncNode, FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY,
                  probeUpdateNode, ComputeStageNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY)
         .Connect(swapChainNode, SwapChainNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY,
                  probeUpdateNode, ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY)
         .Connect(probeUpdateShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  probeUpdateNode, ComputeStageNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(probeUpdatePushConstantGatherer, PushConstantGathererNodeConfig::PUSH_CONSTANT_DATA,
                  probeUpdateNode, ComputeStageNodeConfig::PUSH_CONSTANT_DATA)
         .Connect(probeUpdatePushConstantGatherer, PushConstantGathererNodeConfig::PUSH_CONSTANT_RANGES,
                  probeUpdateNode, ComputeStageNodeConfig::PUSH_CONSTANT_RANGES)
         .Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_SEMAPHORE,
                  probeUpdateNode, ComputeStageNodeConfig::TIMELINE_SEMAPHORE_IN)
         .Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_FRAME_BASE,
                  probeUpdateNode, ComputeStageNodeConfig::TIMELINE_FRAME_BASE_IN);

    // Sync slot: IMAGE_WRITE_ARRAY — the genuine write hazard on the persistent probe
    // atlases (this frame's ProbeUpdateNode write must be visible before any future
    // consumer, e.g. M5's shade-pass gather, reads it; also guards against overlapping
    // this pass's own writes across frames on the SAME persistent image, the identical
    // "hysteresis needs the prior write visible" shape AccumulationHistoryNode's own
    // historyImage sync already relies on). Fed via probeAtlasGatherer (Inc4 M2's own
    // gathering wiring above) rather than re-gathering here — one gatherer instance,
    // reused for both PreRegisterImageSlots(2)'s hazard-array shape and this pass's
    // actual consuming connection.
    batch.Connect(probeAtlasGatherer, ImageSyncGathererNodeConfig::IMAGE_ARRAY,
                  probeUpdateNode, ComputeStageNodeConfig::IMAGE_WRITE_ARRAY, SlotRoleModifier(SlotRole::Execute));

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
    // doc): establishes the SpatialReuse-before-Blit TOPOLOGY (Sampled Lighting Inc3 M5 — moved
    // from DirectLighting, which is no longer the render-target writer) so the scheduler's
    // groupId-order edge direction is correct (mirrors the sky-projection/UI ordering-edge
    // convention below).
    batch.Connect(spatialReuseNode, ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORE,
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
