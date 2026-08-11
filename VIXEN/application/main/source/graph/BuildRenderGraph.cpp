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
#include <cctype>    // std::isspace for whitespace-safe boolean env flags
#include <cmath>    // std::tan for the LOD ray-cone (raySizeCoef) computation
#include <cstdlib>  // std::strtof for the VIXEN_RENDER_SCALE env parse (M4)
#include <fstream>  // Inc0 M5: read BodyInstanceRayMarch.comp's raw source for the recipe splice
#include <sstream>  // Inc0 M5: rdbuf() into a string for the splice
#include <future>   // Baked-Perf M7 Task 7.1: std::async per-body parallel bake
#include <iostream> // Round-5 [ComposedBackend] boot print (mirrors VoxelGridNode.cpp's [FarFieldCount])
#include <mutex>    // Baked-Perf M7 Task 7.1: serializes calls into Gaia's shared ChunkAllocator
#include <unordered_map>  // Baked-Perf M7 Task 7.2: ConcatenateSdfWithMips precomputed-serialize map
#include "Recipe/UberShaderSplice.h"  // Inc0 M5: SpliceProceduralRecipesIntoSource
#include "graph/CornellBoxSceneDefinition.h"  // Sampled Lighting Cornell Box Demo M1: shared scene-definition constants (M1+M2 both read this verbatim)
#include "BakeArtifactCache.h"  // Baked-Perf M7 Task 7.4: bake-artifact disk cache
#include "Connection/ConnectionModifier.h"
#include "Connection/SdiStageWiring.h"  // Semantic-wiring S2: provider registry + SDI-driven stage wiring
#include "Nodes/SdiStageSynthesis.h"    // Semantic-wiring S2 synthesis: gatherer/descSet/pipeline plumbing from merged SDI
#include "ShaderFamily.h"               // Semantic-wiring S2 slice B: feature variants cached as shader families
#include "Connection/Modifiers/FieldExtractionModifier.h"
#include "Connection/Modifiers/AccumulationSortConfig.h"  // SEL-P3: accumulation-connect sort key (provider fan-in)
#include "Core/NodeRegistration.h"
#include "MeshData.h"
#include "merged/BodyInstanceRayMarch-SDI.h"   // Semantic-wiring S1: feature-tagged merged SDI (ShaderInterface::*)
#include "merged/HiZDownsample-SDI.h"          // Semantic-wiring S1: B1 HiZ named binding/push constants
#include "merged/InstanceOcclusionCull-SDI.h"  // Semantic-wiring S1: B1 cull named binding/push constants
#include "merged/RecipeInstanceBucketing-SDI.h" // Semantic-wiring S1: bucketing named binding/push constants
#include "merged/DirectLighting-SDI.h"          // Semantic-wiring S1: lighting passes each cite their OWN interface
#include "merged/SpatialReuseShade-SDI.h"
#include "merged/ExposureTonemap-SDI.h"
#include "merged/ProbeGather-SDI.h"             // W1a: ProbeUpdate's megakernel split (gather/wave/apply)
#include "merged/ProbeApply-SDI.h"
#include "merged/ShadowRayTrace-SDI.h"
#include "merged/ShadowVisibilityWave-SDI.h"    // W1b: the derived-request shadow wave
#include "merged/SpatialReuseGather-SDI.h"      // W2a: the ReSTIR spatial fold (gather)
#include "merged/HitAccumCellShade-SDI.h"       // W3c-2: per-cell shade over the accumulation table
#include "merged/HitAccumulate-SDI.h"           // W-SPLIT: the re-split accumulate, standalone bindings
#include "merged/HitAccumClear-SDI.h"           // B2 (batch-26): table-wide epoch clear, standalone bindings
// W-LEAN L3: HitAccumResolve-SDI.h RETIRED — the resolve is SpatialReuseShade's
// own VIXEN_SRS_CELL_RESOLVE axis now (gated bindings 36-39 in ReuseSdi).

// Semantic-wiring S1: short aliases for the merged-SDI namespaces used at many
// Connect sites below (drift-gated by ctest sdi_merged_drift_check). The three
// lighting passes share the march's push layout TODAY (verified via the merged
// headers), but each cites its own interface so divergence breaks at regen —
// not silently at runtime like the old borrowed VoxelRayMarch:: constants.
namespace MarchSdi = ShaderInterface::BodyInstanceRayMarch;
namespace BucketSdi = ShaderInterface::RecipeInstanceBucketing;
namespace DirectSdi = ShaderInterface::DirectLighting;
namespace ReuseSdi = ShaderInterface::SpatialReuseShade;
namespace GatherSdi = ShaderInterface::ProbeGather;
namespace ApplySdi = ShaderInterface::ProbeApply;
namespace ShadowSdi = ShaderInterface::ShadowRayTrace;
namespace WaveSdi = ShaderInterface::ShadowVisibilityWave;
namespace SrgSdi = ShaderInterface::SpatialReuseGather;  // W2a: the ReSTIR spatial fold (GatherSdi = ProbeGather, taken)
namespace CellShadeSdi = ShaderInterface::HitAccumCellShade;  // W3c-2
namespace AccumSdi = ShaderInterface::HitAccumulate;  // W-SPLIT
namespace ClearSdi = ShaderInterface::HitAccumClear;  // B2 (batch-26): table-wide epoch clear
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
#include "Data/Nodes/HitAccumParamsConfigNodeConfig.h"  // B2: hit-accumulate params upload ring
#include "Data/Nodes/AccumulationConfigNodeConfig.h"   // Sampled Lighting Inc2 M1: AccumulationConfig upload ring
#include "Data/Nodes/AccumulationHistoryNodeConfig.h"  // Sampled Lighting Inc2 M1: persistent history image
#include "Data/Nodes/SceneRadianceNodeConfig.h"
#include "Data/Nodes/WorldPosHistoryNodeConfig.h"      // Sampled Lighting Inc3 M2: worldPos/depth companion history image (KI-023)
#include "Data/Nodes/PrevCameraConfigNodeConfig.h"     // Sampled Lighting Inc2 M3: prev-frame camera matrix upload ring
#include "Data/Nodes/ReservoirConfigNodeConfig.h"      // Sampled Lighting Inc3 M3: ReservoirConfig upload ring (M4/M5 scaffolding)
#include "Data/Nodes/LightTreeBufferNodeConfig.h"      // Sampled Lighting Inc3 M4: mip-cut light-tree upload ring
#include "Data/Nodes/ProbeGridConfigNodeConfig.h"      // Sampled Lighting Inc4 M2: ProbeGridConfig upload ring (M3-M6 scaffolding)
#include "Data/Nodes/ProbeAtlasNodeConfig.h"           // Sampled Lighting Inc4 M2: persistent DDGI probe atlas image
#include "Data/Nodes/ImageSyncGathererNodeConfig.h"    // Sampled Lighting Inc4 M1: variadic IRenderTarget* sync gatherer
#include "Data/Nodes/StorageBufferNodeConfig.h"        // Sampled Lighting Inc3 M4: reservoir CURRENT/PREVIOUS ping-pong SSBOs
#include "Data/Nodes/MultiDispatchNodeConfig.h"        // Recipe-Live-App-Bucketed-Dispatch Inc4 M3: specialized-pipeline indirect dispatch
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
#include "Nodes/HitAccumParamsConfigNode.h"  // B2: hit-accumulate params upload ring
#include "Nodes/AccumulationConfigNode.h"   // Sampled Lighting Inc2 M1: AccumulationConfig upload ring
#include "Nodes/AccumulationHistoryNode.h"  // Sampled Lighting Inc2 M1: persistent history image
#include "Nodes/SceneRadianceNode.h"
#include "Nodes/WorldPosHistoryNode.h"      // Sampled Lighting Inc3 M2: worldPos/depth companion history image (KI-023)
#include "Nodes/PrevCameraConfigNode.h"     // Sampled Lighting Inc2 M3: prev-frame camera matrix upload ring
#include "Nodes/ReservoirConfigNode.h"      // Sampled Lighting Inc3 M3: ReservoirConfig upload ring (M4/M5 scaffolding)
#include "Nodes/LightTreeBufferNode.h"      // Sampled Lighting Inc3 M4: mip-cut light-tree upload ring
#include "Nodes/ProbeGridConfigNode.h"      // Sampled Lighting Inc4 M2: ProbeGridConfig upload ring (M3-M6 scaffolding)
#include "Nodes/ProbeAtlasNode.h"           // Sampled Lighting Inc4 M2: persistent DDGI probe atlas image
#include "Nodes/DepthTargetNode.h"          // Raster-proxy B1 M4: occlusion depth ping-pong pair
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
#include "Nodes/MultiDispatchNode.h"  // Recipe-Live-App-Bucketed-Dispatch Inc4 M3: specialized-pipeline indirect dispatch
#include "Nodes/SwapChainNode.h"
#include "Nodes/TextureLoaderNode.h"
#include "Nodes/UIRenderNode.h"  // S0: composite-HUD render node (RmlUi) — AFTER BodyOctreeSceneNode.h
#include "Nodes/UISelectionProviderNode.h"  // SEL-P3: UI-domain selection provider (RmlUi hit-test)
#include "Nodes/VertexBufferNode.h"
#include "Nodes/VoxelGridNode.h"
#include "Nodes/VoxelSelectionProviderNode.h"
#include "Nodes/WindowNode.h"

namespace {

bool envFlagEnabled(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr) return false;
    for (; *value != '\0'; ++value) {
        if (!std::isspace(static_cast<unsigned char>(*value))) return true;
    }
    return false;
}

bool envFlagIsSet(const char* name, bool& value) {
    const char* raw = std::getenv(name);
    if (raw == nullptr) return false;
    while (*raw != '\0' && std::isspace(static_cast<unsigned char>(*raw))) ++raw;
    if (*raw == '0') {
        value = false;
        return true;
    }
    if (*raw == '1') {
        value = true;
        return true;
    }
    return false;
}

// Baked-perf-pipeline M2 (audit D1, Task 2.1): reads a shader source file and, when
// VIXEN_DEBUG_CAPTURE is set, injects "#define VIXEN_GPU_TRACE_HOOKS 1\n" -- the same
// textual-#define-injection technique Vixen::SVO::Recipe::SpliceProceduralRecipesIntoSource
// already uses for VIXEN_UBER_RECIPE_SPLICED (UberShaderSplice.h), reused here because
// ShaderBundleBuilder::SetStageDefines cannot drive a new #ifdef (it substitutes existing
// token occurrences, it does not inject a #define line -- see BodyInstanceRayMarch.comp's
// registration above for the fuller citation). Used to convert DirectLighting.comp/
// SpatialReuseShade.comp/ProbeUpdate.comp from AddStageFromFile (which reads the file
// internally, leaving no C++-side string to prepend into) to AddStage(source text) --
// mechanically identical to what AddStageFromFile does internally (ShaderBundleBuilder.cpp),
// so this preserves #include-path resolution (still driven by the explicit AddIncludePath
// calls at each site, not by sourcePath) and behavior for every existing (non-gated) shader
// text. Throws std::runtime_error if the file cannot be read, matching each call site's
// existing empty-compPath error-handling contract.
//
// CORRECTNESS: every shader in this codebase starts with "#version 460" as its literal
// FIRST line -- GLSL requires #version to be the first non-whitespace line in the
// translation unit (glslang: "'#version' : must occur first in shader"), so naively
// prepending the #define at position 0 pushes #version to line 2 and fails compilation
// (caught live: ProbeUpdate.comp failed exactly this way on first real end-to-end run
// with VIXEN_DEBUG_CAPTURE=1, glslc's own -D flag masked this because it doesn't touch
// the source text at all -- unlike this C++-side prepend). Insert the #define after the
// FIRST LINE instead (i.e. right after #version), which is always legal for a #define.
std::string ReadShaderSourceWithTraceHooksGate(const std::filesystem::path& compPath,
                                                const char* shaderName) {
    std::ifstream file(compPath);
    if (!file) {
        throw std::runtime_error(std::string(shaderName) + " could not be opened at " + compPath.string());
    }
    std::ostringstream buf;
    buf << file.rdbuf();
    std::string source = buf.str();
    if (envFlagEnabled("VIXEN_DEBUG_CAPTURE")) {
        const size_t firstNewline = source.find('\n');
        const std::string defineLine = "#define VIXEN_GPU_TRACE_HOOKS 1\n";
        if (firstNewline == std::string::npos) {
            source += "\n" + defineLine;
        } else {
            source.insert(firstNewline + 1, defineLine);
        }
    }
    return source;
}

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
    const bool hdrExposureEnabled = envFlagEnabled("VIXEN_HDR_EXPOSURE");
    if (!renderGraph) {
        mainLogger->Error("Cannot build render graph: RenderGraph not initialized");
        return;
    }

    // S0: opt into the UI-only RmlUi demo graph via env var, leaving the voxel path untouched.
    if (envFlagEnabled("VIXEN_UI_DEMO")) {
        mainLogger->Info("VIXEN_UI_DEMO set - building UI-only RmlUi demo graph");
        BuildUIGraph();
        return;
    }

    // AR#31: opt into the isolated instanced-cube raster demo via env var, leaving the
    // live voxel-compute path untouched.
    if (envFlagEnabled("VIXEN_INSTANCING_DEMO")) {
        mainLogger->Info("VIXEN_INSTANCING_DEMO set - building instanced-cube raster demo graph");
        BuildInstancingDemoGraph();
        return;
    }

    // AR#21 P4: opt into the isolated auto-sync FrameGraph demo via env var. Proves
    // buffer-hazard auto-synchronization (compute->compute->render->present in ONE
    // command buffer via PassGroupNode). Leaves the live voxel-compute path untouched.
    if (envFlagEnabled("VIXEN_AUTOSYNC_DEMO")) {
        mainLogger->Info("VIXEN_AUTOSYNC_DEMO set - building auto-sync FrameGraph demo graph");
        BuildAutoSyncDemoGraph();
        return;
    }

    // AR#21 P5b M2: opt into the multi-submit fan-in demo via env var. Proves
    // TIMELINE-ONLY ordering across separate compute submits: 2 independent producer
    // compute submits write 2 buffers, 1 consumer compute submit waits BOTH via 2 baked
    // timeline edges (NO binary handoff between them) + writes the swapchain. Leaves the
    // live voxel-compute path untouched.
    if (envFlagEnabled("VIXEN_FANIN_DEMO")) {
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
    // Adapter-selection visibility: DeviceNode's NODE_LOG_INFO calls (incl. "[DeviceNode] Selected
    // GPU N: ...") are dead by default -- nodeLogger is constructed enabled=false (NodeInstance.cpp)
    // and nothing else opts this node in. Enable it so every session's GPU choice is on record.
    if (Logger* deviceLogger = renderGraph->GetInstance(deviceNode)->GetLogger()) {
        deviceLogger->SetEnabled(true);
        deviceLogger->SetTerminalOutput(true);
    }
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
    // Slice C: desc-gatherer/descriptor-set/pipeline are synthesized at the
    // wire site (SynthesizeComputeStage, names direct_lighting_*); only the
    // push-gatherer handle is kept — the hand push-field connects target it.
    NodeHandle directLightingPushConstantGatherer{};
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
    // Slice C: plumbing synthesized at the wire site; push-gatherer handle assigned there.
    NodeHandle spatialReusePushConstantGatherer{};
    NodeHandle spatialReuseNode = renderGraph->AddNode<ComputeStageNodeType>("spatial_reuse");

    NodeHandle sceneRadianceNode = renderGraph->AddNode<SceneRadianceNodeType>("scene_radiance");
    NodeHandle exposureShaderLib{};
    NodeHandle exposureNode{};
    if (hdrExposureEnabled) {
        exposureShaderLib = renderGraph->AddNode<ShaderLibraryNodeType>("exposure_tonemap_shader_lib");
        exposureNode = renderGraph->AddNode<ComputeStageNodeType>("exposure_tonemap");
    }

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
    // W2a: the read-side gatherer moved with the fold — SpatialReuseGather owns
    // the A/B neighbor-array read hazard now (created only when the reservoir
    // path is enabled; see the W2a node block below).

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

    // W1a (wavefront epoch): the shadow-ray queue's slot geometry, mirroring
    // ProbeUpdateCommon.glsl's PROBE_UPDATE_MAX_RAYS_PER_PROBE and
    // ProbeUpdateShadowSlot — fixed-slot addressing over the FULL grid, so the
    // buffers are amortization-factor-independent (see that include's own
    // slot-addressing note on the accepted stale-slot re-trace waste).
    constexpr uint32_t kProbeUpdateMaxRaysPerProbe = 256u;
    constexpr uint32_t kShadowRaySlotCount =
        kProbeGridDefaultCountX * kProbeGridDefaultCountY * kProbeGridDefaultCountZ * kProbeUpdateMaxRaysPerProbe;
    constexpr uint32_t kShadowRayRequestStrideBytes = 32u;  // ShadowRayQueue.glsl's std430 ShadowRayRequest
    constexpr uint32_t kShadowRayResultStrideBytes  = 4u;   // bare uint per slot
    constexpr uint32_t kProbeRayPayloadStrideBytes  = 16u;  // ProbeUpdateCommon.glsl's std430 ProbeRayPayload

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
    // W1a (wavefront epoch): ProbeUpdate's megakernel split into THREE stages —
    // probe_gather (primary rays + WRS light pick + shadow-ray request emission),
    // shadow_ray_trace (the traversal WAVE answering the fixed-slot queue), and
    // probe_apply (visibility consumption + reduction + atlas writes). Each is a
    // separately compiled program with its own shaderLib + synthesized plumbing
    // (the "second compiled shader needs its own instances" rationale, now ×3).
    // gather/wave trace the scene (SceneBindings.glsl consumers); apply binds no
    // scene at all — it is the tiny kernel the split exists to create.
    NodeHandle probeGatherShaderLib = renderGraph->AddNode<ShaderLibraryNodeType>("probe_gather_shader_lib");
    NodeHandle probeGatherPushConstantGatherer{};  // synthesized at the wire site
    NodeHandle probeGatherNode = renderGraph->AddNode<ComputeStageNodeType>("probe_gather");
    NodeHandle shadowRayTraceShaderLib = renderGraph->AddNode<ShaderLibraryNodeType>("shadow_ray_trace_shader_lib");
    NodeHandle shadowRayTracePushConstantGatherer{};  // synthesized at the wire site
    NodeHandle shadowRayTraceNode = renderGraph->AddNode<ComputeStageNodeType>("shadow_ray_trace");
    NodeHandle probeApplyShaderLib = renderGraph->AddNode<ShaderLibraryNodeType>("probe_apply_shader_lib");
    NodeHandle probeApplyNode = renderGraph->AddNode<ComputeStageNodeType>("probe_apply");

    // W1a queue/payload buffers (fixed-size, slot-addressed — see the
    // kShadowRaySlotCount constants above): requests gather→wave, results
    // wave→apply, payloads gather→apply. Sized CPU-side below (PARAM_SIZE_BYTES,
    // the ddgiLeakGateDebugBuffer precedent — not extent-driven).
    NodeHandle shadowRayRequestBuffer = renderGraph->AddNode<StorageBufferNodeType>("shadow_ray_request_buffer");
    NodeHandle shadowRayResultBuffer  = renderGraph->AddNode<StorageBufferNodeType>("shadow_ray_result_buffer");
    NodeHandle probeRayPayloadBuffer  = renderGraph->AddNode<StorageBufferNodeType>("probe_ray_payload_buffer");

    // W1a cross-dispatch hazard gatherers (the BUFFER_WRITE_ARRAY/BUFFER_READ_ARRAY
    // "two gatherer instances, same underlying source" shape — see
    // spatialReuseReservoirReadGatherer's own precedent comment above): shared
    // Resource* identity between a writer-side and reader-side gatherer is what
    // lets the scheduler bake the gather→wave→apply SyncEdges.
    NodeHandle probeGatherWriteGatherer   = renderGraph->AddNode<BufferSyncGathererNodeType>("probe_gather_write_gatherer");
    NodeHandle shadowRayTraceReadGatherer  = renderGraph->AddNode<BufferSyncGathererNodeType>("shadow_ray_trace_read_gatherer");
    NodeHandle shadowRayTraceWriteGatherer = renderGraph->AddNode<BufferSyncGathererNodeType>("shadow_ray_trace_write_gatherer");
    NodeHandle probeApplyReadGatherer      = renderGraph->AddNode<BufferSyncGathererNodeType>("probe_apply_read_gatherer");

    // W1b (wavefront epoch): the DERIVED-REQUEST shadow wave — answers the
    // production per-pixel-per-light shadow question into HitRecord._pad0[2]
    // between the march (record producer) and the shade pass (record
    // consumer), so SpatialReuseShade's default variant never traces. NO
    // request/result buffers exist (the user bandwidth ruling: derivable rays
    // ship answers in resident records) — the wave's only data traffic is a
    // read-modify-write of the hit-record buffer it shares with march/DL/SRS,
    // declared via its own read+write gatherer pair on that ONE resource.
    NodeHandle shadowVisibilityWaveShaderLib = renderGraph->AddNode<ShaderLibraryNodeType>("shadow_visibility_wave_shader_lib");
    NodeHandle shadowVisibilityWavePushConstantGatherer{};  // synthesized at the wire site
    NodeHandle shadowVisibilityWaveNode = renderGraph->AddNode<ComputeStageNodeType>("shadow_visibility_wave");
    NodeHandle shadowVisibilityWaveReadGatherer  = renderGraph->AddNode<BufferSyncGathererNodeType>("shadow_visibility_wave_read_gatherer");
    NodeHandle shadowVisibilityWaveWriteGatherer = renderGraph->AddNode<BufferSyncGathererNodeType>("shadow_visibility_wave_write_gatherer");

    // W2a (wavefront epoch): the ReSTIR seam — SpatialReuseGather (the spatial
    // fold, split out of the shade; ALL spatial RNG) + the wave's RESERVOIR
    // PHASE (a second dispatch of the same program as its own compiled
    // variant, answering _pad0[2] bit 4 post-gather). B1's conditional-creation
    // shape: the reservoir path opt-in (ResolveReservoirEnabled — the SAME
    // accessor that fills ReservoirConfig.reservoirEnabled) creates the whole
    // node set or none of it, so the default path's graph is UNCHANGED, not
    // merely idle.
    const bool reservoirPathEnabled = ResolveReservoirEnabled();
    NodeHandle spatialReuseGatherShaderLib{}, spatialReuseGatherNode{}, spatialReuseGatherPushGatherer{};
    NodeHandle spatialReuseGatherReadGatherer{}, spatialReuseGatherWriteGatherer{};
    NodeHandle waveReservoirShaderLib{}, waveReservoirNode{};
    NodeHandle waveReservoirReadGatherer{}, waveReservoirWriteGatherer{};
    NodeHandle gatherWidthConstant{}, gatherHeightConstant{};
    if (reservoirPathEnabled) {
        spatialReuseGatherShaderLib = renderGraph->AddNode<ShaderLibraryNodeType>("spatial_reuse_gather_shader_lib");
        spatialReuseGatherNode      = renderGraph->AddNode<ComputeStageNodeType>("spatial_reuse_gather");
        // Reads: reservoirBufferA/B (the neighbor-array hazard that used to be
        // the shade's — it moved here with the fold) + hitRecordBuffer
        // (similarity rejects). Writes: the combined-reservoir publish.
        spatialReuseGatherReadGatherer  = renderGraph->AddNode<BufferSyncGathererNodeType>("spatial_reuse_gather_read_gatherer");
        spatialReuseGatherWriteGatherer = renderGraph->AddNode<BufferSyncGathererNodeType>("spatial_reuse_gather_write_gatherer");
        waveReservoirShaderLib = renderGraph->AddNode<ShaderLibraryNodeType>("shadow_visibility_wave_reservoir_shader_lib");
        waveReservoirNode      = renderGraph->AddNode<ComputeStageNodeType>("shadow_visibility_wave_reservoir");
        // Reads: combined reservoir (+ the hit-record RMW's read half).
        // Writes: the hit-record RMW's write half (bit 4).
        waveReservoirReadGatherer  = renderGraph->AddNode<BufferSyncGathererNodeType>("shadow_visibility_wave_reservoir_read_gatherer");
        waveReservoirWriteGatherer = renderGraph->AddNode<BufferSyncGathererNodeType>("shadow_visibility_wave_reservoir_write_gatherer");
    }

    // W3b (wavefront epoch): the (recipeId, cell@mip) hit-accumulation pass —
    // clear + accumulate stages from ONE HitAccumulate.comp family (mode push
    // selector, the bucketing multi-mode precedent) over a fixed-capacity hash
    // table. NO CONSUMER exists yet (W3c's resolve is first), so frame output
    // is identical by construction; the pass's own gate is the
    // VIXEN_HIT_ACCUM_PROBE_LOG readback cross-validating table totals against
    // the CPU mirror (HitAccumulation.h) over the same records. B1-shape
    // conditional creation on VIXEN_HIT_ACCUM (default off): zero nodes when
    // unset.
    const bool hitAccumEnabled = envFlagEnabled("VIXEN_HIT_ACCUM");
    hitAccumEnabled_ = hitAccumEnabled;
    // B2 determinism slice: VIXEN_HIT_ACCUM_DIAG_FRAME=<n> -- sample the diag
    // readback (RunHitAccumDiagReadback) at a fixed frame n instead of only at
    // shutdown (PostTick, VulkanGraphApplication.cpp). 0/unset = disabled.
    if (const char* diagFrameEnv = std::getenv("VIXEN_HIT_ACCUM_DIAG_FRAME")) {
        hitAccumDiagFrame_ = static_cast<uint64_t>(std::strtoull(diagFrameEnv, nullptr, 10));
    }
    // B2 (docs/plans/2026-08-04-wavefront-recipe-shading.md): shared-memory
    // same-key pre-merge within the accumulate pass's own 64-wide workgroup —
    // flag-off keeps HitAccumulate.comp's compiled variant byte-identical
    // (B1's conditional-creation shape, same as VIXEN_HIT_ACCUM_RESOLVE below).
    const bool hitAccumPremergeEnabled =
        hitAccumEnabled && envFlagEnabled("VIXEN_HIT_ACCUM_PREMERGE");
    // B2 (batch-27): the table-wide clear (batch-26) is OFF by default now.
    // The diag readback already scans a single named epoch (VulkanGraphApplication
    // .cpp's maxEpoch filter) -- for diag purposes the clear is redundant. Worse,
    // its unconditional every-frame dispatch wipes the SAMPLED frame's own rows
    // before the deferred readback runs (a later in-flight frame's clear beats the
    // scan), which is what drove batch-26's occupied=0. Opt back in with
    // VIXEN_HIT_ACCUM_CLEAR once a correct (epoch-conditional or fenced) clear is
    // designed; until then ClaimAccumSlot's lazy per-slot reclaim is the only
    // reclaim mechanism (accepted: counts/occupied-slot settles below CPU-predict's
    // 2.46 under multi-epoch staleness, not a diag-readback concern).
    const bool hitAccumClearEnabled =
        hitAccumEnabled && envFlagEnabled("VIXEN_HIT_ACCUM_CLEAR");
    NodeHandle hitAccumTableBuffer{};
    NodeHandle hitAccumParamsBuffer{};
    NodeHandle hitAccumClearShaderLib{}, hitAccumClearNode{};
    NodeHandle hitAccumClearWriteGatherer{};
    NodeHandle hitAccumAccumulateShaderLib{}, hitAccumAccumulateNode{};
    NodeHandle hitAccumAccumulateReadGatherer{}, hitAccumAccumulateWriteGatherer{};
    if (hitAccumEnabled) {
        hitAccumTableBuffer = renderGraph->AddNode<StorageBufferNodeType>("hit_accum_table_buffer");
        // W-SPLIT: the accumulate tail is back to its OWN dispatch — W3c-1's
        // fusion into the shadow wave (VIXEN_HIT_ACCUM_FUSED) cost MORE than
        // the ~2 ms record re-read it was meant to avoid (measured +7-8 ms
        // structural, the wave's traversal+claim machinery sharing one
        // kernel). HitAccumulate.comp is standalone-bindings (no
        // SceneBindings, no traversal — the leanness is the point). Per-frame
        // params (epoch, primary cone, detail, camera) ride ONE host-written
        // 48-byte buffer, written each PreTick — NEVER push constants (they
        // bake at record time per ring slot; the W3b finding).
        // B2 (batch-23): ring-buffered — was a single un-ringed StorageBufferNode
        // (batch-22 root cause: k frames in flight shared one epoch stamp).
        // Mirrors ShadowConfigNode/PrevCameraConfigNode's PerFrameResources ring.
        hitAccumParamsBuffer = renderGraph->AddNode<HitAccumParamsConfigNodeType>("hit_accum_params_buffer");
        // B2 (batch-26, gated OFF by default batch-27, VIXEN_HIT_ACCUM_CLEAR):
        // table-wide epoch clear. Its EVERY-FRAME unconditional dispatch wipes
        // ALL slots regardless of epoch (including the just-sampled frame's own
        // rows, since it carries no epoch filter) -- a later in-flight frame's
        // clear can run before the deferred diag readback scans, which is what
        // produced batch-26's occupied=0. Node creation is skipped entirely
        // unless hitAccumClearEnabled; ClaimAccumSlot's lazy per-slot reclaim
        // (HitAccumulate.comp) remains the only reclaim path by default.
        if (hitAccumClearEnabled) {
            hitAccumClearShaderLib = renderGraph->AddNode<ShaderLibraryNodeType>("hit_accum_clear_shader_lib");
            hitAccumClearNode      = renderGraph->AddNode<ComputeStageNodeType>("hit_accum_clear");
            hitAccumClearWriteGatherer = renderGraph->AddNode<BufferSyncGathererNodeType>("hit_accum_clear_write_gatherer");
        }
        hitAccumAccumulateShaderLib = renderGraph->AddNode<ShaderLibraryNodeType>("hit_accum_accumulate_shader_lib");
        hitAccumAccumulateNode      = renderGraph->AddNode<ComputeStageNodeType>("hit_accum_accumulate");
        // Reads: hitRecordBuffer. Writes: {table, hitRecordBuffer} — it RMWs
        // the record's flags word (the W-LEAN L1 stamp, moved here from the
        // wave) AND writes the accumulation table.
        hitAccumAccumulateReadGatherer  = renderGraph->AddNode<BufferSyncGathererNodeType>("hit_accum_accumulate_read_gatherer");
        hitAccumAccumulateWriteGatherer = renderGraph->AddNode<BufferSyncGathererNodeType>("hit_accum_accumulate_write_gatherer");
    }
    hitAccumParamsBuffer_ = hitAccumParamsBuffer;
    hitAccumTableBuffer_ = hitAccumTableBuffer;

    // W3c-2 (wavefront epoch): the resolve — per-CELL shade over the table +
    // the per-pixel composite AFTER SpatialReuseShade, BEFORE the blit. The
    // epic's FIRST deliberately-lossy path, so it is its OWN opt-in on top of
    // the accumulation (VIXEN_HIT_ACCUM_RESOLVE, default off): flag-off keeps
    // the graph — and the frames — bit-identical (B1's conditional-creation
    // shape, again).
    const bool hitAccumResolveEnabled =
        hitAccumEnabled && envFlagEnabled("VIXEN_HIT_ACCUM_RESOLVE");
    hitAccumResolveEnabled_ = hitAccumResolveEnabled;
    NodeHandle hitAccumCellRadianceBuffer{};
    NodeHandle hitAccumCellShadeShaderLib{}, hitAccumCellShadeNode{};
    NodeHandle hitAccumCellShadeReadGatherer{}, hitAccumCellShadeWriteGatherer{};
    NodeHandle spatialReuseCellReadGatherer{};
    if (hitAccumResolveEnabled) {
        hitAccumCellRadianceBuffer = renderGraph->AddNode<StorageBufferNodeType>("hit_accum_cell_radiance_buffer");
        hitAccumCellShadeShaderLib = renderGraph->AddNode<ShaderLibraryNodeType>("hit_accum_cell_shade_shader_lib");
        hitAccumCellShadeNode      = renderGraph->AddNode<ComputeStageNodeType>("hit_accum_cell_shade");
        // Reads: the table (orders wave→cell-shade on the shared resource).
        // Writes: the cell-radiance buffer.
        hitAccumCellShadeReadGatherer  = renderGraph->AddNode<BufferSyncGathererNodeType>("hit_accum_cell_shade_read_gatherer");
        hitAccumCellShadeWriteGatherer = renderGraph->AddNode<BufferSyncGathererNodeType>("hit_accum_cell_shade_write_gatherer");
        // W-LEAN L3: NO standalone resolve — the composite is SpatialReuseShade's
        // own VIXEN_SRS_CELL_RESOLVE axis. SRS gains its first buffer READ
        // gatherer for the fold's inputs {cellRadiance, table}.
        spatialReuseCellReadGatherer = renderGraph->AddNode<BufferSyncGathererNodeType>("spatial_reuse_cell_read_gatherer");
    }
    hitAccumCellRadianceBuffer_ = hitAccumCellRadianceBuffer;

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

    // Recipe-Live-App-Bucketed-Dispatch Inc4 M1 fix round: InstanceSkipMaskBuffer (binding 35,
    // shaders/SceneBindings.glsl) placeholder SSBO. `SceneBindings.glsl` is #included
    // unconditionally by FOUR production shaders (BodyInstanceRayMarch.comp, DirectLighting.comp,
    // ProbeUpdate.comp, SpatialReuseShade.comp) -- SPIR-V reflection (SPIRVReflection.cpp, no
    // dead-code-elimination pass anywhere in the shader build) marks binding 35 as a REQUIRED
    // descriptor in all four compiled shaders' reflected sets regardless of whether that shader's
    // own code ever calls isInstanceSkipped(), exactly like ddgiLeakGateDebugBuffer above already
    // gets wired into three separate gatherers (directLighting/probeUpdate/spatialReuse) for the
    // same reason. One shared placeholder buffer, wired to all four gatherers below (the march's
    // descriptorGatherer + the three synthesized lighting desc-gatherers) -- never populated with real
    // skip data this milestone (M3's job), same 256-byte zeroed convention as this feature's own 11
    // GTest harnesses (test_body_instance_raymarch_render.cpp et al.) use for their own default-case
    // placeholder, so production and tests share one no-op convention instead of two.
    NodeHandle instanceSkipMaskBuffer = renderGraph->AddNode<StorageBufferNodeType>("instance_skip_mask_buffer");

    // E11-T1: per-8x8-tile policy word buffer (VIXEN_POLICY_STENCIL_TILES). Fixed-size
    // (NO SWAPCHAIN_INFO), same shape as instanceSkipMaskBuffer above — 500x500 (the
    // established test resolution) / 8x8 workgroups = 63x63 = 3,969 tiles, one uint
    // each (PARAM_SIZE_BYTES set below). BodyInstanceRayMarch writes one word per
    // workgroup (tile index = gl_WorkGroupID); ShadowVisibilityWave reads it to skip
    // evaluator work for source axes provably absent tile-wide.
    NodeHandle policyStencilTileBuffer = renderGraph->AddNode<StorageBufferNodeType>("policy_stencil_tile_buffer");
    // KI-052 fix (E12-T1): march-side write-hazard gatherer for policyStencilTileBuffer,
    // feeding computeDispatch's new BUFFER_WRITE_ARRAY slot (see ComputeDispatchNodeConfig.h)
    // so ResourceAccessTracker records the march as a writer of this Resource* — paired with
    // shadowVisibilityWaveReadGatherer's matching read entry (added below) to bake the
    // previously-missing march->wave SyncEdge.
    NodeHandle policyStencilTileWriteGatherer = renderGraph->AddNode<BufferSyncGathererNodeType>("policy_stencil_tile_write_gatherer");

    // Recipe-Live-App-Bucketed-Dispatch Inc4 M3: gated behind VIXEN_RECIPE_BUCKETED_DISPATCH
    // (opt-in, following the VIXEN_PROCEDURAL_UBER_DEMO/VIXEN_UI_DEMO convention -- read once here
    // so every node-creation/wiring decision below is consistent for this whole function's build).
    // When UNSET (default): none of the nodes below are created at all -- the graph builds EXACTLY
    // as it did pre-M3, satisfying the milestone's own "flag-unset is a genuine, provable no-op"
    // bar at the strongest level (no new node, not just an inert one).
    const bool recipeBucketedDispatchEnabled = envFlagEnabled("VIXEN_RECIPE_BUCKETED_DISPATCH");
    recipeBucketedDispatchEnabled_ = recipeBucketedDispatchEnabled;  // stored for PreTick's live orchestration
    // W2b: the identity bucket-shade skeleton rides the bucketed-dispatch
    // machinery (BucketMeta/indirect commands/MultiDispatchNode), so it
    // REQUIRES that flag — shade-without-buckets has nothing to dispatch over.
    bucketedShadeEnabled_ = recipeBucketedDispatchEnabled && envFlagEnabled("VIXEN_BUCKETED_SHADE");

    NodeHandle recipeBucketCountBuffer{};
    NodeHandle recipeBucketIndicesBuffer{};
    NodeHandle recipeBucketCoverageMinXBuffer{};
    NodeHandle recipeBucketCoverageMinYBuffer{};
    NodeHandle recipeBucketCoverageMaxXBuffer{};
    NodeHandle recipeBucketCoverageMaxYBuffer{};
    NodeHandle recipeBucketIndirectCommandBuffer{};
    NodeHandle recipePrecisionBucketCountBuffer{};   // Load-Tier Contract M2 (precision tier)
    NodeHandle recipePrecisionBucketIndicesBuffer{}; // Load-Tier Contract M2 (precision tier)
    NodeHandle recipeBoundSphereBuffer{};
    NodeHandle recipeBucketingShaderLib{};
    NodeHandle recipeBucketingModeInit{};   // mode==1: reset extrema/counts
    NodeHandle recipeBucketingModeBucket{}; // mode==0: bucket + coverage (one thread per instance)
    NodeHandle recipeBucketingModeFinal{};  // mode==2: emit per-bucket indirect command
    NodeHandle recipeBucketingBufSyncW{};   // BufferSyncGathererNode: mode0/1's write set
    NodeHandle recipeBucketingBufSyncR{};   // BufferSyncGathererNode: mode2's read set (coverage)
    NodeHandle recipeBucketMetaBuffer{};     // shared per-recipeId BucketMeta SSBO (specialized shader binding 3)
    NodeHandle recipeSpecializedDispatch{}; // MultiDispatchNode (Inc4 M3-extended, indirect dispatch)
    NodeHandle recipeBucketingModeInitConstant{};
    NodeHandle recipeBucketingModeBucketConstant{};
    NodeHandle recipeBucketingModeFinalConstant{};
    NodeHandle recipeBucketingMaxBucketsConstant{};
    NodeHandle recipeBucketingMaxMembersConstant{};
    NodeHandle recipeBucketingViewProjConstant{};

    if (recipeBucketedDispatchEnabled) {
        mainLogger->Info("[BuildRenderGraph] VIXEN_RECIPE_BUCKETED_DISPATCH set -- wiring the "
                         "bucketing pre-pass + specialized-pipeline indirect dispatch into the "
                         "real graph (Recipe-Live-App-Bucketed-Dispatch Inc4 M3)");

        // --- Bucketing pre-pass SSBOs (shaders/RecipeInstanceBucketing.comp bindings 0-8) ---
        // Binding 0 (BodyInstanceBuffer) reuses bodyOctreeSceneNode's own INSTANCE_BUFFER --
        // no new node needed, same buffer the march already reads at its own binding 10.
        recipeBoundSphereBuffer        = renderGraph->AddNode<StorageBufferNodeType>("recipe_bound_sphere_buffer");
        recipeBucketCountBuffer        = renderGraph->AddNode<StorageBufferNodeType>("recipe_bucket_count_buffer");
        recipeBucketIndicesBuffer      = renderGraph->AddNode<StorageBufferNodeType>("recipe_bucket_indices_buffer");
        recipeBucketCoverageMinXBuffer = renderGraph->AddNode<StorageBufferNodeType>("recipe_bucket_coverage_minx_buffer");
        recipeBucketCoverageMinYBuffer = renderGraph->AddNode<StorageBufferNodeType>("recipe_bucket_coverage_miny_buffer");
        recipeBucketCoverageMaxXBuffer = renderGraph->AddNode<StorageBufferNodeType>("recipe_bucket_coverage_maxx_buffer");
        recipeBucketCoverageMaxYBuffer = renderGraph->AddNode<StorageBufferNodeType>("recipe_bucket_coverage_maxy_buffer");
        recipeBucketIndirectCommandBuffer = renderGraph->AddNode<StorageBufferNodeType>("recipe_bucket_indirect_command_buffer");
        // Load-Tier Contract M2 (precision tier): bindings 9-10, the precision-tier sub-bucket
        // pair, additive to the recipe-only bucket above (see RecipeInstanceBucketing.comp's
        // PrecisionBucketCountBuffer/PrecisionBucketIndicesBuffer declaration comment).
        recipePrecisionBucketCountBuffer   = renderGraph->AddNode<StorageBufferNodeType>("recipe_precision_bucket_count_buffer");
        recipePrecisionBucketIndicesBuffer = renderGraph->AddNode<StorageBufferNodeType>("recipe_precision_bucket_indices_buffer");

        // --- Bucketing shader lib (self-contained binding namespace 0-8, independent from the
        // march's chain). Only shader identity is authored; the desc-gatherer/descriptor-set/
        // pipeline chain is SYNTHESIZED from the shader's merged SDI at the wire site below. ---
        recipeBucketingShaderLib    = renderGraph->AddNode<ShaderLibraryNodeType>("recipe_bucketing_shader_lib");
        // Three ComputeStageNode instances (mode 1 -> mode 0 -> mode 2), same shader/pipeline/
        // descriptor set, distinguished only by their own PUSH_CONSTANT_DATA (mode field) and
        // dispatch dims -- each is self-submitting (proven ComputeStageNode pattern, matches
        // directLightingNode/probeUpdateNode) so no new submit machinery is needed for these 3.
        // Auto-sync (BufferSyncGathererNode, below) bakes the required mode1->mode0->mode2 memory
        // barriers instead of hand-rolled vkCmdPipelineBarrier2 calls.
        recipeBucketingModeInit   = renderGraph->AddNode<ComputeStageNodeType>("recipe_bucketing_mode_init");
        recipeBucketingModeBucket = renderGraph->AddNode<ComputeStageNodeType>("recipe_bucketing_mode_bucket");
        recipeBucketingModeFinal  = renderGraph->AddNode<ComputeStageNodeType>("recipe_bucketing_mode_final");
        recipeBucketingBufSyncW = renderGraph->AddNode<BufferSyncGathererNodeType>("recipe_bucketing_bufsync_write");
        recipeBucketingBufSyncR = renderGraph->AddNode<BufferSyncGathererNodeType>("recipe_bucketing_bufsync_read");

        // Each mode stage gets its own ConstantNode supplying the `mode` literal -- mirrors
        // raySizeBiasConstant's "ConstantNode feeds a fixed literal into a reflected
        // push-constant field" convention. (Each stage's own PushConstantGathererNode -- the
        // `mode` field differs per stage -- is synthesized by SynthesizeComputeStageGroup at
        // the wire site, under the stages' historical *_pc_gatherer names.)
        recipeBucketingModeInitConstant   = renderGraph->AddNode<ConstantNodeType>("recipe_bucketing_mode_init_constant");
        recipeBucketingModeBucketConstant = renderGraph->AddNode<ConstantNodeType>("recipe_bucketing_mode_bucket_constant");
        recipeBucketingModeFinalConstant  = renderGraph->AddNode<ConstantNodeType>("recipe_bucketing_mode_final_constant");
        recipeBucketingMaxBucketsConstant = renderGraph->AddNode<ConstantNodeType>("recipe_bucketing_max_buckets_constant");
        recipeBucketingMaxMembersConstant = renderGraph->AddNode<ConstantNodeType>("recipe_bucketing_max_members_constant");
        // viewProj needs a BY-VALUE (not const glm::mat4&) source for PushConstantGathererNode's
        // generic ExtractResourceAs<T>()/GetHandle<T>() path -- CameraNodeConfig::CURRENT_VIEW_PROJ
        // is reference-typed (ConstRefTag), which is a producer/consumer Resource-tag mismatch
        // against a variadic field's ValueTag expectation (throws std::bad_any_cast at Execute --
        // found live during this milestone's own gate run). A ConstantNode set every frame from
        // PreTick (via CameraNode::GetCurrentViewProj(), a new by-value accessor) sidesteps this:
        // ConstantNode::SetValue<T> always stores via ValueTag, matching what the gatherer expects.
        recipeBucketingViewProjConstant = renderGraph->AddNode<ConstantNodeType>("recipe_bucketing_view_proj_constant");

        // --- Specialized per-recipe indirect dispatch (runtime-compiled GLSL per hot recipeId,
        // EmitSpecializedRecipeComputeShader -- see PreTick's per-frame orchestration). Unlike
        // the bucketing shader above (a fixed source file, known at graph-build time), a
        // per-recipeId specialized shader's source doesn't exist until a scene has actually
        // registered that recipe -- there is no single fixed shader for a ShaderLibraryNode's
        // RegisterShaderBuilder to return. So this path deliberately does NOT use the static
        // ShaderLibraryNode->DescriptorResourceGathererNode->DescriptorSetNode->
        // ComputePipelineNode chain at all: PreTick compiles each hot recipeId's shader via
        // ShaderCompiler+ShaderBundleBuilder::AddStageFromSpirv (reflection without a node),
        // builds its descriptor set/pipeline layout via CashSystem's reflection-driven
        // BuildDescriptorSetLayoutFromReflection/ExtractPushConstantsFromReflection free
        // functions, and creates the VkPipeline via ComputePipelineCacher::GetOrCreate directly
        // -- all fully outside RenderGraph's node system (confirmed safe: RenderGraph forbids
        // AddNode/ConnectNodes after Compile(), but building Vulkan objects OUTSIDE the graph at
        // any time, then handing the resulting handles to an already-compiled node's imperative
        // API, is exactly MultiDispatchNode::QueueDispatch's own designed use case -- DispatchPass
        // is plain Vulkan handles with no provenance requirement). recipeSpecializedDispatch is
        // the only node instance needed for this; PreTick queues its DispatchPasses every frame.
        recipeBucketMetaBuffer = renderGraph->AddNode<StorageBufferNodeType>("recipe_bucket_meta_buffer");
        recipeSpecializedDispatch = renderGraph->AddNode<MultiDispatchNodeType>("recipe_specialized_dispatch");
    }
    recipeBucketCountBuffer_ = recipeBucketCountBuffer;
    recipeBucketIndicesBuffer_ = recipeBucketIndicesBuffer;
    recipeBucketCoverageMinXBuffer_ = recipeBucketCoverageMinXBuffer;
    recipeBucketCoverageMinYBuffer_ = recipeBucketCoverageMinYBuffer;
    recipeBucketCoverageMaxXBuffer_ = recipeBucketCoverageMaxXBuffer;
    recipeBucketCoverageMaxYBuffer_ = recipeBucketCoverageMaxYBuffer;
    recipeBucketIndirectCommandBuffer_ = recipeBucketIndirectCommandBuffer;
    recipePrecisionBucketCountBuffer_ = recipePrecisionBucketCountBuffer;
    recipePrecisionBucketIndicesBuffer_ = recipePrecisionBucketIndicesBuffer;
    recipeBoundSphereBuffer_ = recipeBoundSphereBuffer;
    recipeBucketMetaBuffer_ = recipeBucketMetaBuffer;
    recipeBucketingViewProjConstant_ = recipeBucketingViewProjConstant;
    recipeSpecializedDispatch_ = recipeSpecializedDispatch;
    instanceSkipMaskBuffer_ = instanceSkipMaskBuffer;

    // --- Raster-proxy B1: occlusion-probe chain — DEFAULT ON (ruling 2026-08-04) ---
    // Stable, plain-compute (no optional device capability), measured win (march
    // −7.9% mean on occluded scenes, cpu median −50%; ~0 on occlusion-free ones) —
    // so it is the default, not a feature flag. Feature flags are for heavy-drain /
    // optional-capability / debug variants; VIXEN_B1_OCCLUSION_CULL=0 (or "off")
    // remains as the OPT-OUT kill switch — the A/B harness's off leg, a debug use.
    // Any other value (incl. the legacy "=1") keeps it on. Opted out ⇒ zero B1
    // nodes created and the march shader drops binding 36 (same one-bool gate
    // M1-M4 built: depth ping-pong pair, HiZ tile image, reduce + cull quintets).
    const char* b1OcclusionCullEnv = std::getenv("VIXEN_B1_OCCLUSION_CULL");
    const bool b1OcclusionCullEnabled =
        !(b1OcclusionCullEnv && (std::string_view(b1OcclusionCullEnv) == "0" ||
                                 std::string_view(b1OcclusionCullEnv) == "off"));
    b1OcclusionCullEnabled_ = b1OcclusionCullEnabled;
    NodeHandle b1DepthTarget{};
    NodeHandle b1HizTileImage{};
    NodeHandle b1HizShaderLib{}, b1HizDescGatherer{}, b1HizPushGatherer{};
    NodeHandle b1HizDescriptorSet{}, b1HizPipeline{}, b1HizStage{};
    NodeHandle b1CullShaderLib{}, b1CullDescGatherer{}, b1CullPushGatherer{};
    NodeHandle b1CullDescriptorSet{}, b1CullPipeline{}, b1CullStage{};
    NodeHandle b1HizTileWriteGatherer{}, b1CullTileReadGatherer{}, b1CullMaskWriteGatherer{};
    NodeHandle b1CullPrevViewProjConstant{}, b1CullPrevCamPosConstant{}, b1CullDimsConstant{};
    if (b1OcclusionCullEnabled) {
        b1DepthTarget  = renderGraph->AddNode<DepthTargetNodeType>("b1_depth_target");
        b1HizTileImage = renderGraph->AddNode<ProbeAtlasNodeType>("b1_hiz_tile_image");

        // Semantic-wiring S2 synthesis: only the AUTHORED halves are created
        // here — shader identity (shader-lib) + dispatch (stage). The
        // gatherer/descriptor-set/pipeline plumbing nodes are synthesized by
        // SynthesizeComputeStage at the connect section below.
        b1HizShaderLib     = renderGraph->AddNode<ShaderLibraryNodeType>("b1_hiz_shader_lib");
        b1HizStage         = renderGraph->AddNode<ComputeStageNodeType>("b1_hiz_downsample");
        b1CullShaderLib     = renderGraph->AddNode<ShaderLibraryNodeType>("b1_cull_shader_lib");
        b1CullStage         = renderGraph->AddNode<ComputeStageNodeType>("b1_instance_occlusion_cull");

        // Same-frame HiZ(write)→cull(read) hazard on the tile image: the established
        // two-gatherer-instances pattern (probeAtlasGatherer precedent). The cull's
        // skip-mask write gets its own 1-entry write gatherer, which orders the cull
        // before EVERY existing skip-mask reader (march + lighting passes).
        b1HizTileWriteGatherer = renderGraph->AddNode<ImageSyncGathererNodeType>("b1_hiz_tile_write_gatherer");
        b1CullTileReadGatherer = renderGraph->AddNode<ImageSyncGathererNodeType>("b1_cull_tile_read_gatherer");
        b1CullMaskWriteGatherer = renderGraph->AddNode<BufferSyncGathererNodeType>("b1_cull_mask_write_gatherer");

        // Push-constant feeds: prevViewProj/prevCamPos are ONE-FRAME-DELAYED values PreTick
        // refreshes (RunB1OcclusionCullPreTick) — same ConstantNode-not-direct-wire rationale
        // as recipeBucketingViewProjConstant (reference-typed camera outputs cannot feed a
        // variadic push-constant field by value). dims = {srcW, srcH, instanceCount, 0}.
        b1CullPrevViewProjConstant = renderGraph->AddNode<ConstantNodeType>("b1_cull_prev_view_proj");
        b1CullPrevCamPosConstant   = renderGraph->AddNode<ConstantNodeType>("b1_cull_prev_cam_pos");
        b1CullDimsConstant         = renderGraph->AddNode<ConstantNodeType>("b1_cull_dims");
        b1CullPrevViewProjConstant_ = b1CullPrevViewProjConstant;
        b1CullPrevCamPosConstant_   = b1CullPrevCamPosConstant;
        b1CullDimsConstant_         = b1CullDimsConstant;
    }

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

    // Regime-3 (cosmic accumulation) first slice, deep-field-mip-policy design doc: cosmicK
    // push-constant literal ("footprint >= K*cell" promotes a ray to the transmittance-
    // accumulation walk). Push constant (not baked) so a later slice can sweep it without a
    // rebuild -- same "ConstantNode + env override" lever as tierCrossingLodCoefOverrideConstant/
    // secondaryRaySizeCoefConstant above. Default 4.0; VIXEN_REGIME3_K overrides. Value is inert
    // (read by the shader, but VIXEN_REGIME3-gated) when the flag is off.
    NodeHandle regime3KConstant = renderGraph->AddNode<ConstantNodeType>("regime3_cosmic_k");

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

    // Baked-Perf M4b Task 4b.2: secondary-ray (shadow/probe) LOD coefficient. A SEPARATE
    // literal ConstantNode (same "direct literal, not an FOV bump" lever as
    // tierCrossingLodCoefOverrideConstant above -- see that node's own comment for why FOV
    // scaling is too weak) feeding ONLY the DirectLighting/SpatialReuse/ProbeUpdate push-
    // constant gatherers' field 8, so shadow and probe rays hit the existing screen-space LOD
    // gate (SceneBindings.glsl raySizeCoef>0 checks) at a coefficient calibrated for "occluder/
    // GI coarse-enough" rather than primary rays' pixel-accurate footprint threshold. The
    // primary gatherer keeps its own unrelated connection to raySizeCoefNode (line ~4561) --
    // untouched by this node, so same_path stays hash-equal by construction (no shared wire).
    NodeHandle secondaryRaySizeCoefConstant = renderGraph->AddNode<ConstantNodeType>("secondary_ray_size_coef");

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

    // Recipe-Live-App-Bucketed-Dispatch Inc4 M1 fix round: InstanceSkipMaskBuffer (binding 35) --
    // fixed 256-byte zeroed placeholder, not extent-driven (mirrors ddgiLeakGateDebugBuffer's own
    // fixed-size convention just above). 256 bytes matches this feature's 11 GTest harnesses'
    // default placeholder size exactly (see test_body_instance_raymarch_render.cpp's
    // dummySkipMask), so isInstanceSkipped()'s skipMask.length()==64 (256B / 4B-per-uint) no-op
    // path is identical in production and in every test.
    auto* instanceSkipMaskInst = static_cast<StorageBufferNode*>(renderGraph->GetInstance(instanceSkipMaskBuffer));
    instanceSkipMaskInst->SetParameter(StorageBufferNodeConfig::PARAM_SIZE_BYTES, 256u);

    // E11-T1: PolicyStencilTileBuffer (binding 41) -- fixed-size, not extent-driven
    // (same convention as instanceSkipMaskBuffer/ddgiLeakGateDebugBuffer above).
    // ponytail: sized for the 500x500 established test resolution's 63x63=3,969
    // 8x8-workgroup tile grid, not derived from the live swapchain extent (the
    // shared StorageBufferNode type's extent-driven mode is per-pixel bytes only,
    // not per-tile) -- if VIXEN_WINDOW_WIDTH/HEIGHT push the tile grid past 63x63,
    // grow kPolicyStencilMaxTilesPerAxis. The shader clamps writes to this bound.
    constexpr uint32_t kPolicyStencilMaxTilesPerAxis = 63u;
    auto* policyStencilTileInst = static_cast<StorageBufferNode*>(renderGraph->GetInstance(policyStencilTileBuffer));
    policyStencilTileInst->SetParameter(StorageBufferNodeConfig::PARAM_SIZE_BYTES,
        (2u + kPolicyStencilMaxTilesPerAxis * kPolicyStencilMaxTilesPerAxis) * 4u);  // +2: header words (see SceneBindings.glsl)

    // Recipe-Live-App-Bucketed-Dispatch Inc4 M3: bucketing-pass SSBO sizing, matching
    // shaders/RecipeInstanceBucketing.comp's own push-constant caps (kRecipeMaxBuckets,
    // kRecipeMaxMembersPerBucket below) so every buffer's byte size agrees with what the
    // shader's own indexing math expects. Stored as app-lifetime members (not locals) since
    // PreTick's per-frame orchestration re-derives the same caps from these same constants.
    if (recipeBucketedDispatchEnabled) {
        auto* boundSphereInst = static_cast<StorageBufferNode*>(renderGraph->GetInstance(recipeBoundSphereBuffer));
        boundSphereInst->SetParameter(StorageBufferNodeConfig::PARAM_SIZE_BYTES,
            kRecipeBucketingMaxBuckets * 32u);  // RecipeBoundSphere: 32B (vec3+float+float+float+pad[2],
                                                // Load-Tier Contract M1 added gateFootprintThreshold)

        auto* bucketCountInst = static_cast<StorageBufferNode*>(renderGraph->GetInstance(recipeBucketCountBuffer));
        bucketCountInst->SetParameter(StorageBufferNodeConfig::PARAM_SIZE_BYTES,
            kRecipeBucketingMaxBuckets * 4u);  // uint per bucket

        auto* bucketIndicesInst = static_cast<StorageBufferNode*>(renderGraph->GetInstance(recipeBucketIndicesBuffer));
        bucketIndicesInst->SetParameter(StorageBufferNodeConfig::PARAM_SIZE_BYTES,
            kRecipeBucketingMaxBuckets * kRecipeBucketingMaxMembersPerBucket * 4u);  // uint per [bucket][slot]

        auto* covMinXInst = static_cast<StorageBufferNode*>(renderGraph->GetInstance(recipeBucketCoverageMinXBuffer));
        covMinXInst->SetParameter(StorageBufferNodeConfig::PARAM_SIZE_BYTES, kRecipeBucketingMaxBuckets * 4u);
        auto* covMinYInst = static_cast<StorageBufferNode*>(renderGraph->GetInstance(recipeBucketCoverageMinYBuffer));
        covMinYInst->SetParameter(StorageBufferNodeConfig::PARAM_SIZE_BYTES, kRecipeBucketingMaxBuckets * 4u);
        auto* covMaxXInst = static_cast<StorageBufferNode*>(renderGraph->GetInstance(recipeBucketCoverageMaxXBuffer));
        covMaxXInst->SetParameter(StorageBufferNodeConfig::PARAM_SIZE_BYTES, kRecipeBucketingMaxBuckets * 4u);
        auto* covMaxYInst = static_cast<StorageBufferNode*>(renderGraph->GetInstance(recipeBucketCoverageMaxYBuffer));
        covMaxYInst->SetParameter(StorageBufferNodeConfig::PARAM_SIZE_BYTES, kRecipeBucketingMaxBuckets * 4u);

        auto* indirectCmdInst = static_cast<StorageBufferNode*>(renderGraph->GetInstance(recipeBucketIndirectCommandBuffer));
        indirectCmdInst->SetParameter(StorageBufferNodeConfig::PARAM_SIZE_BYTES,
            kRecipeBucketingMaxBuckets * 12u);  // VkDispatchIndirectCommand: 3x uint32, per bucket

        // Load-Tier Contract M2 (precision tier): the precision sub-bucket pair is compound-keyed
        // recipeId*2+tier (see RecipeInstanceBucketing.comp's PrecisionBucketCountBuffer comment),
        // so both buffers are sized to TWICE the plain recipe-bucket buffers above.
        auto* precisionCountInst = static_cast<StorageBufferNode*>(renderGraph->GetInstance(recipePrecisionBucketCountBuffer));
        precisionCountInst->SetParameter(StorageBufferNodeConfig::PARAM_SIZE_BYTES,
            kRecipeBucketingMaxBuckets * 2u * 4u);  // uint per (recipeId, tier) sub-bucket

        auto* precisionIndicesInst = static_cast<StorageBufferNode*>(renderGraph->GetInstance(recipePrecisionBucketIndicesBuffer));
        precisionIndicesInst->SetParameter(StorageBufferNodeConfig::PARAM_SIZE_BYTES,
            kRecipeBucketingMaxBuckets * 2u * kRecipeBucketingMaxMembersPerBucket * 4u);  // uint per [sub-bucket][slot]
        // The specialized dispatch's own vkCmdDispatchIndirect (PreTick's QueueDispatch) reads
        // this buffer directly -- needs VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT in addition to the
        // STORAGE_BUFFER_BIT the bucketing shader's mode==2 pass writes it with (found live via
        // VUID-vkCmdDispatchIndirect-buffer-02709 during this milestone's own gate run).
        indirectCmdInst->SetParameter(StorageBufferNodeConfig::PARAM_EXTRA_USAGE_FLAGS,
            static_cast<uint32_t>(VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT));

        // SpecializedRecipeShaderGlsl.h's BucketMeta struct: 32B/entry (matches BucketMetaCpu's
        // own static_assert in the Inc2/3 test harnesses), one entry per recipeId.
        auto* bucketMetaInst = static_cast<StorageBufferNode*>(renderGraph->GetInstance(recipeBucketMetaBuffer));
        bucketMetaInst->SetParameter(StorageBufferNodeConfig::PARAM_SIZE_BYTES, kRecipeBucketingMaxBuckets * 32u);

        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Info("[BuildRenderGraph] Recipe bucketing SSBOs sized: maxBuckets=" +
                             std::to_string(kRecipeBucketingMaxBuckets) + " maxMembersPerBucket=" +
                             std::to_string(kRecipeBucketingMaxMembersPerBucket));
        }

        // Mode literals: mode 1 (init) -> mode 0 (bucket) -> mode 2 (finalize), matching
        // RecipeInstanceBucketing.comp's own push-constant `mode` semantics exactly.
        static_cast<ConstantNode*>(renderGraph->GetInstance(recipeBucketingModeInitConstant))->SetValue<uint32_t>(1u);
        static_cast<ConstantNode*>(renderGraph->GetInstance(recipeBucketingModeBucketConstant))->SetValue<uint32_t>(0u);
        static_cast<ConstantNode*>(renderGraph->GetInstance(recipeBucketingModeFinalConstant))->SetValue<uint32_t>(2u);
        static_cast<ConstantNode*>(renderGraph->GetInstance(recipeBucketingMaxBucketsConstant))->SetValue<uint32_t>(kRecipeBucketingMaxBuckets);
        static_cast<ConstantNode*>(renderGraph->GetInstance(recipeBucketingMaxMembersConstant))->SetValue<uint32_t>(kRecipeBucketingMaxMembersPerBucket);
        // Default-initialized to identity; PreTick overwrites this every frame with the live
        // camera view-proj (see RunRecipeBucketedDispatchPreTick) before this frame's dispatch.
        static_cast<ConstantNode*>(renderGraph->GetInstance(recipeBucketingViewProjConstant))->SetValue<glm::mat4>(glm::mat4(1.0f));

        // mode-init/mode-final dispatch one thread per BUCKET SLOT (fixed at graph-build time);
        // mode-bucket dispatches one thread per INSTANCE, sized generously fixed
        // (kRecipeBucketingMaxMembersPerBucket * kRecipeBucketingMaxBuckets/local_size_x is
        // overkill -- use a simpler, still-safely-oversized fixed bound: the shader's own
        // `if (gid >= pc.instanceCount) return;` guard makes any dispatch >= actual instance
        // count safe, so a fixed generous cap avoids needing a live-instance-count-driven
        // dispatch dim, which ComputeStageNode's fixed PARAM_DISPATCH_X/Y/Z does not support).
        constexpr uint32_t kRecipeBucketingLocalSizeX = 64;  // matches shader's local_size_x
        constexpr uint32_t kRecipeBucketingMaxInstancesDispatch = 4096;  // generous fixed cap
        auto* modeInitStage = static_cast<ComputeStageNode*>(renderGraph->GetInstance(recipeBucketingModeInit));
        modeInitStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_X,
            (kRecipeBucketingMaxBuckets + kRecipeBucketingLocalSizeX - 1u) / kRecipeBucketingLocalSizeX);
        modeInitStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Y, 1u);
        modeInitStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Z, 1u);

        auto* modeBucketStage = static_cast<ComputeStageNode*>(renderGraph->GetInstance(recipeBucketingModeBucket));
        modeBucketStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_X,
            (kRecipeBucketingMaxInstancesDispatch + kRecipeBucketingLocalSizeX - 1u) / kRecipeBucketingLocalSizeX);
        modeBucketStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Y, 1u);
        modeBucketStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Z, 1u);

        auto* modeFinalStage = static_cast<ComputeStageNode*>(renderGraph->GetInstance(recipeBucketingModeFinal));
        modeFinalStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_X,
            (kRecipeBucketingMaxBuckets + kRecipeBucketingLocalSizeX - 1u) / kRecipeBucketingLocalSizeX);
        modeFinalStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Y, 1u);
        modeFinalStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Z, 1u);
    }

    // Raster-proxy B1 M4: occlusion-probe parameters. Depth/tile extents are fixed at
    // graph build from the same width*renderScale derivation the lighting dispatch dims
    // use (a live window resize rebuilds the graph; renderScale is process-fixed).
    if (b1OcclusionCullEnabled) {
        const uint32_t b1SrcW = static_cast<uint32_t>(width * renderScale);
        const uint32_t b1SrcH = static_cast<uint32_t>(height * renderScale);
        const uint32_t b1TilesX = (b1SrcW + 15u) / 16u;   // HiZDownsampleMirror::HiZTileCount
        const uint32_t b1TilesY = (b1SrcH + 15u) / 16u;
        b1SrcWidth_  = b1SrcW;
        b1SrcHeight_ = b1SrcH;

        auto* b1TileImage = static_cast<ProbeAtlasNode*>(renderGraph->GetInstance(b1HizTileImage));
        b1TileImage->SetParameter(ProbeAtlasNodeConfig::PARAM_WIDTH,  b1TilesX);
        b1TileImage->SetParameter(ProbeAtlasNodeConfig::PARAM_HEIGHT, b1TilesY);
        b1TileImage->SetParameter(ProbeAtlasNodeConfig::PARAM_FORMAT,
            static_cast<uint32_t>(VK_FORMAT_R32_SFLOAT));

        // 8x8 local size (HiZDownsample.comp), thread per output tile texel.
        auto* b1HizStageInst = static_cast<ComputeStageNode*>(renderGraph->GetInstance(b1HizStage));
        b1HizStageInst->SetParameter(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);
        b1HizStageInst->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_X, (b1TilesX + 7u) / 8u);
        b1HizStageInst->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Y, (b1TilesY + 7u) / 8u);
        b1HizStageInst->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Z, 1u);

        // One 64-thread group covers the 192-instance cap's 6 skip words (InstanceOcclusionCull.comp).
        auto* b1CullStageInst = static_cast<ComputeStageNode*>(renderGraph->GetInstance(b1CullStage));
        b1CullStageInst->SetParameter(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);
        b1CullStageInst->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_X, 1u);
        b1CullStageInst->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Y, 1u);
        b1CullStageInst->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Z, 1u);

        // Frame-0 sane values; PreTick refreshes every frame (one-frame-delayed camera).
        static_cast<ConstantNode*>(renderGraph->GetInstance(b1CullPrevViewProjConstant))->SetValue<glm::mat4>(glm::mat4(1.0f));
        static_cast<ConstantNode*>(renderGraph->GetInstance(b1CullPrevCamPosConstant))->SetValue<glm::vec4>(glm::vec4(0.0f));
        static_cast<ConstantNode*>(renderGraph->GetInstance(b1CullDimsConstant))->SetValue<glm::uvec4>(glm::uvec4(b1SrcW, b1SrcH, 0u, 0u));

        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Info("[BuildRenderGraph] B1 occlusion probe enabled: depth " +
                             std::to_string(b1SrcW) + "x" + std::to_string(b1SrcH) +
                             " ping-pong, HiZ tiles " + std::to_string(b1TilesX) + "x" +
                             std::to_string(b1TilesY));
        }
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

    // Regime-3 cosmicK: default 4.0, VIXEN_REGIME3_K env overrides (sweep without a rebuild).
    {
        float regime3KValue = 4.0f;
        if (const char* regime3KEnv = std::getenv("VIXEN_REGIME3_K")) {
            regime3KValue = std::strtof(regime3KEnv, nullptr);
        }
        auto* regime3KConst = static_cast<ConstantNode*>(renderGraph->GetInstance(regime3KConstant));
        regime3KConst->SetValue<float>(regime3KValue);
    }

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

    // Baked-Perf M4b Task 4b.2: secondary-ray raySizeCoef value. Default 0.05 -- roughly two
    // orders of magnitude coarser than the primary ray's live coefficient (~5.6e-4 at 45deg
    // FOV/1440p) -- picked to trip the existing SceneBindings.glsl screen-space LOD gate
    // (tv_max*raySizeCoef+raySizeBias >= scale_exp2, LOCAL [1,2)-frame units, same formula
    // primary rays use) once a shadow/probe ray's traversal reaches a node whose local extent
    // is a small fraction of the octree root, WITHOUT being so large it trips on the
    // ROOT-level node itself (scale_exp2=1.0 at the root; 0.05*tv_max must stay < 1.0 for any
    // tv_max under ~20, comfortably covering this scene's [1,2) traversal range) -- i.e. coarse
    // occluders/GI, not "every shadow ray mip-shades the first node it touches." Tunable via
    // VIXEN_SECONDARY_RAY_SIZE_COEF for the M4b Task 4b.3 A/B pass without a rebuild.
    {
        float secondaryRaySizeCoefValue = 0.05f;
        if (const char* secondaryCoefEnv = std::getenv("VIXEN_SECONDARY_RAY_SIZE_COEF")) {
            secondaryRaySizeCoefValue = std::strtof(secondaryCoefEnv, nullptr);
        }
        auto* secondaryRaySizeCoefConst =
            static_cast<ConstantNode*>(renderGraph->GetInstance(secondaryRaySizeCoefConstant));
        secondaryRaySizeCoefConst->SetValue<float>(secondaryRaySizeCoefValue);
        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Info("[BuildRenderGraph] M4b: secondary-ray (shadow/probe) raySizeCoef=" +
                              std::to_string(secondaryRaySizeCoefValue) +
                              " (VIXEN_SECONDARY_RAY_SIZE_COEF env; default 0.05)");
        }
    }

    auto* frameSync = static_cast<FrameSyncNode*>(renderGraph->GetInstance(frameSyncNode));

    // Voxel ray marching compute shader (VoxelRayMarch.comp)
    // Load from pre-compiled shaders in build directory
    auto* computeShaderLibNode = static_cast<ShaderLibraryNode*>(renderGraph->GetInstance(computeShaderLib));

    // M-wire Task 8: use the instanced shell-octree ray-march shader (BodyInstanceRayMarch.comp).
    // Replaces VoxelRayMarch_Compressed.comp. Bindings 1/2/3/5 come from BodyOctreeSceneNode;
    // binding 10 = per-body instance SSBO; bindings 4/8 = debug/counters from voxelGridNode.
    //
    // Semantic-wiring S2 slice B (march adoption): the march is a ShaderFamily.
    // The SourceProvider owns the CONTENT pipeline — path resolve, raw read,
    // procedural-recipe splice (Inc0 M5: AddStage source text, not
    // AddStageFromFile; #include resolution rides the explicit include paths)
    // + the occupancy-grid side effect (M6 Task 13, pushed to
    // BodyOctreeSceneNode, re-derived per member build like before). The
    // FEATURE axis is typed at the registration site below; the family
    // applies the canonical after-#version splice. Ordering note: the old
    // LIFO hand-splices produced #version, B1, TRACE — the family's sorted
    // canonical order produces the SAME bytes, so every variant's cache key
    // is preserved. (Shader counters remain compiled OUT unconditionally —
    // see ShaderCounters.glsl's #ifdef; SetStageDefines cannot drive #ifdefs.)
    auto marchFamily = std::make_shared<ShaderManagement::ShaderFamily>(
        ShaderManagement::ShaderFamily::Config{"BodyInstanceRayMarch",
                                              ShaderManagement::ShaderStage::Compute},
        [this]() -> std::string {
            constexpr const char* shaderName = "BodyInstanceRayMarch.comp";
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
                if (mainLogger && mainLogger->IsEnabled()) {
                    mainLogger->Error("[BuildRenderGraph] " + std::string(shaderName) + " not found in search paths");
                    mainLogger->Error("[BuildRenderGraph] Current working directory: " + std::filesystem::current_path().string());
#ifdef VIXEN_SHADER_SOURCE_DIR
                    mainLogger->Error("[BuildRenderGraph] VIXEN_SHADER_SOURCE_DIR: " VIXEN_SHADER_SOURCE_DIR);
#endif
                }
                throw std::runtime_error(std::string(shaderName) + " not found - check shader search paths");
            }

            std::ifstream compFile(compPath);
            std::ostringstream compBuf;
            compBuf << compFile.rdbuf();
            const std::string rawSource = compBuf.str();

            std::string splicedSource;
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

            // Round 11 (far-field dispatch attribution): tag this shader as the
            // primary march so SceneBindings.glsl's g_dispatchTag routes its
            // far-field counters into index 0 (TraceBufferHeader::
            // farFieldCountByTag etc, DebugRaySample.h) -- ProbeGather.comp is
            // TraceWorld's only other caller and is left untagged (falls to
            // index 1). Same textual-#define-after-#version technique as
            // ReadShaderSourceWithTraceHooksGate's VIXEN_GPU_TRACE_HOOKS (this
            // shader doesn't route through that helper, hence the inline splice
            // here instead). Unconditional -- always-on, not env-gated, since a
            // compile-time tag is free at runtime and only affects debug-counter
            // bucketing.
            {
                const size_t firstNewline = splicedSource.find('\n');
                const std::string tagDefine = "#define VIXEN_DISPATCH_IS_PRIMARY_MARCH 1\n";
                if (firstNewline == std::string::npos) {
                    splicedSource += "\n" + tagDefine;
                } else {
                    splicedSource.insert(firstNewline + 1, tagDefine);
                }
            }

            // Batch-29 (deep-field mip-accessor policy, regime-2 level ladder):
            // VIXEN_MIP_POLICY=1 env -> shader define, same textual-#define-
            // after-#version splice as VIXEN_DISPATCH_IS_PRIMARY_MARCH just
            // above. Gates descendToNodeOrdinal's level-selection loop
            // (ESVOTraversal.glsl) onto the shared mipPolicyLevel function
            // (SVOTypes.glsl) instead of the per-hop single-brick-rung crossing
            // check; flag-off leaves that loop's source byte-identical to
            // pre-batch-29.
            if (envFlagEnabled("VIXEN_MIP_POLICY")) {
                const size_t firstNewline = splicedSource.find('\n');
                const std::string mipPolicyDefine = "#define VIXEN_MIP_POLICY 1\n";
                if (firstNewline == std::string::npos) {
                    splicedSource += "\n" + mipPolicyDefine;
                } else {
                    splicedSource.insert(firstNewline + 1, mipPolicyDefine);
                }
                if (mainLogger && mainLogger->IsEnabled()) {
                    mainLogger->Info("[BuildRenderGraph] VIXEN_MIP_POLICY: regime-2 level-ladder policy ENGAGED "
                                      "(descendToNodeOrdinal consults mipPolicyLevel instead of the single brick rung)");
                }

                // Deep-field-mip-policy design doc, regime 3 (cosmic accumulation) first slice:
                // VIXEN_REGIME3=1 env -> shader define, requires VIXEN_MIP_POLICY (regime 3 reuses
                // its ladder machinery -- descendToNodeOrdinal/shadeFromMipSample -- so it cannot be
                // meaningfully engaged alone). Off by default; flag-off leaves the entry dispatch's
                // source byte-identical to pre-regime-3.
                if (envFlagEnabled("VIXEN_REGIME3")) {
                    const size_t firstNewlineR3 = splicedSource.find('\n');
                    const std::string regime3Define = "#define VIXEN_REGIME3 1\n";
                    if (firstNewlineR3 == std::string::npos) {
                        splicedSource += "\n" + regime3Define;
                    } else {
                        splicedSource.insert(firstNewlineR3 + 1, regime3Define);
                    }
                    if (mainLogger && mainLogger->IsEnabled()) {
                        mainLogger->Info("[BuildRenderGraph] VIXEN_REGIME3: cosmic-accumulation entry-dispatch walk ENGAGED "
                                          "(footprint >= K*cell promotes to a transmittance accumulator, K=pc.cosmicK)");
                    }

                    // Batch-48 level-floor probe: force the regime-3 walk's sampled
                    // ladder level to be at least L while leaving the default source
                    // and production path unchanged. The shader applies this only to
                    // the walk's footprint before it calls descendToNodeOrdinal;
                    // that keeps the canonical mipPolicyLevel selection in one place.
                    if (const char* levelFloorEnv = std::getenv("VIXEN_REGIME3_LEVEL_FLOOR")) {
                        char* parseEnd = nullptr;
                        const long parsedLevelFloor = std::strtol(levelFloorEnv, &parseEnd, 10);
                        if (parseEnd != levelFloorEnv && *parseEnd == '\0' && parsedLevelFloor >= 0) {
                            const size_t firstNewlineFloor = splicedSource.find('\n');
                            const std::string levelFloorDefine =
                                "#define VIXEN_REGIME3_LEVEL_FLOOR " + std::to_string(parsedLevelFloor) + "\n";
                            if (firstNewlineFloor == std::string::npos) {
                                splicedSource += "\n" + levelFloorDefine;
                            } else {
                                splicedSource.insert(firstNewlineFloor + 1, levelFloorDefine);
                            }
                            if (mainLogger && mainLogger->IsEnabled()) {
                                mainLogger->Info("[BuildRenderGraph] VIXEN_REGIME3_LEVEL_FLOOR=" +
                                                  std::to_string(parsedLevelFloor) +
                                                  ": regime-3 walk minimum sampled level ENGAGED");
                            }
                        } else if (mainLogger && mainLogger->IsEnabled()) {
                            mainLogger->Warning("[BuildRenderGraph] VIXEN_REGIME3_LEVEL_FLOOR ignored: expected a non-negative integer");
                        }
                    }
                }

            }

            // Compositing slice part 2 (cross-instance transmittance fill): VIXEN_REGIME3_COMPOSITE=1
            // env -> shader define. Only meaningful on top of VIXEN_REGIME3 (the walk's residual T
            // global is never written away from its 1.0 default otherwise) -- deliberately NOT gated
            // on VIXEN_REGIME3 OR VIXEN_MIP_POLICY (batch-42 validator: the splice was accidentally
            // nested inside the VIXEN_MIP_POLICY block, contradicting this very comment and making
            // the composite-alone neutrality control VACUOUS -- it must inject the define so the
            // inertness being tested is the RUNTIME no-op, not the absence of the code).
            if (envFlagEnabled("VIXEN_REGIME3_COMPOSITE")) {
                const size_t firstNewlineR3c = splicedSource.find('\n');
                const std::string regime3CompositeDefine = "#define VIXEN_REGIME3_COMPOSITE 1\n";
                if (firstNewlineR3c == std::string::npos) {
                    splicedSource += "\n" + regime3CompositeDefine;
                } else {
                    splicedSource.insert(firstNewlineR3c + 1, regime3CompositeDefine);
                }
                if (mainLogger && mainLogger->IsEnabled()) {
                    mainLogger->Info("[BuildRenderGraph] VIXEN_REGIME3_COMPOSITE: cross-instance transmittance fill ENGAGED "
                                      "(regime-3 winner's residual T composites with the same-pass second-nearest candidate)");
                }
            }

            if (mainLogger && mainLogger->IsEnabled()) {
                mainLogger->Info("[BuildRenderGraph] Using BodyInstanceRayMarch shader: " + compPath.string() +
                                 " (" + std::to_string(proceduralRecipes_.Ids().size()) + " procedural recipes spliced)");
                mainLogger->Info("[BuildRenderGraph] Octree buffers at bindings 1/2/3/5 (BodyOctreeSceneNode); instances at binding 10");
            }
            return splicedSource;
        });

    // The march's feature axis, typed — the SAME env knobs, decided in ONE
    // visible place instead of buried string splices (VIXEN_DEBUG_CAPTURE is
    // the single end-to-end trace-recording toggle: CPU readback + GPU
    // writes; audit D1 Task 2.1).
    std::vector<std::string> marchShaderFeatures;
    if (envFlagEnabled("VIXEN_DEBUG_CAPTURE")) {
        marchShaderFeatures.push_back(kFeatureGpuTraceHooks.define);
    }
    if (envFlagEnabled("VIXEN_COMPOSITION_COUNTERS")) {
        marchShaderFeatures.push_back(kFeatureCompositionCounters.define);
    }
    const bool policyStencilTilesEnabled =
        envFlagEnabled("VIXEN_POLICY_STENCIL") && envFlagEnabled("VIXEN_POLICY_STENCIL_TILES");
    if (envFlagEnabled("VIXEN_POLICY_STENCIL")) {
        marchShaderFeatures.push_back(kFeaturePolicyStencil.define);
        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Info("[BuildRenderGraph] VIXEN_POLICY_STENCIL: primary policy-stencil materialization ENGAGED");
        }
    }
    if (policyStencilTilesEnabled) {
        marchShaderFeatures.push_back(kFeaturePolicyStencilTiles.define);
        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Info("[BuildRenderGraph] VIXEN_POLICY_STENCIL_TILES: tile reduction + evaluator-skip ENGAGED");
        }
    }
    if (b1OcclusionCullEnabled) {  // one source of truth — the default-on gate above
        marchShaderFeatures.push_back(kFeatureB1OcclusionCull.define);
    }
    if (envFlagEnabled("VIXEN_BRICKMAP_TRAVERSAL")) {  // W-BRICKMAP Slice 2 A/B gate
        marchShaderFeatures.push_back(kFeatureBrickmapTraversal.define);
        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Info("[BuildRenderGraph] VIXEN_BRICKMAP_TRAVERSAL: coarse-grid DDA backend ENGAGED (FORMAT_STORED_SDF instances route through traverseCoarseGridInstancedSdf)");
        }
    }
    if (envFlagEnabled("VIXEN_BRICKMAP_DEBUG")) {  // W-BRICKMAP Gate-B bisection
        marchShaderFeatures.push_back(kFeatureBrickmapDebug.define);
    }
    // W-RTQUERY Slice A: VK_KHR_ray_query per-brick-AABB TLAS backend (third search
    // backend alongside ESVO/DDA -- see kFeatureRtQueryTraversal's header comment).
    // Own env var, independent of VIXEN_BRICKMAP_TRAVERSAL; TraceWorld.glsl gives RTQUERY
    // precedence when both are set. Kept as its own if-block (not folded into the
    // VIXEN_BRICKMAP_TRAVERSAL block above) so this hunk stays a minimal, independently
    // reviewable addition.
    if (envFlagEnabled("VIXEN_RTQUERY_TRAVERSAL")) {
        marchShaderFeatures.push_back(kFeatureRtQueryTraversal.define);
        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Info("[BuildRenderGraph] VIXEN_RTQUERY_TRAVERSAL: VK_KHR_ray_query TLAS backend ENGAGED (FORMAT_STORED_SDF instances route through traverseRayQueryWorld)");
        }
    }
    // W-COMPOSED: the role ruling made code -- RT-traversal / DDA-leaf / ESVO-data
    // are complementary tiers of ONE traversal (docs/plans/2026-08-04-wavefront-
    // recipe-shading.md, "USER RULING: composed traversal"), not rival backends.
    // VIXEN_COMPOSED_TRAVERSAL picks the near-field search phase by CAPABILITY --
    // RT when the device has RTXCapabilities.rayQuery, DDA (the software
    // traversal substitute) otherwise -- pushing EITHER kFeatureRtQueryTraversal
    // OR kFeatureBrickmapTraversal, never both, so TraceWorld.glsl's existing
    // #ifdef/#elif chain (no new GLSL branching) picks the same backend the SDI
    // wiring below wires bindings for. The capability is only known once the
    // device exists (DeviceNode::CompileImpl, which runs before this shader
    // library's own CompileImpl per graph dependency order) -- deferred into the
    // RegisterShaderBuilder lambda below instead of decided here, where the
    // VulkanDevice doesn't exist yet. The three single-backend env vars above are
    // untouched by this block; composed is a fourth, additive axis.
    // The orbital-structure admission is the current far-field scene-demand seam. Keep this
    // predicate at graph construction, upstream of BodyOctreeSceneNode's TLAS owner, so the
    // composed identity is selected for the same scene that can produce far-field mip samples.
    const bool sceneRequestsFarFieldTraversal =
        envFlagEnabled("VIXEN_TIER_OBSERVABLE_STRUCTURE");
    bool composedTraversalOverride = false;
    const bool hasComposedTraversalOverride =
        envFlagIsSet("VIXEN_COMPOSED_TRAVERSAL", composedTraversalOverride);
    const bool composedTraversalRequested = hasComposedTraversalOverride
        ? composedTraversalOverride : sceneRequestsFarFieldTraversal;
    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info(std::string("[BuildRenderGraph] composed traversal predicate: ") +
                         (sceneRequestsFarFieldTraversal ? "ON (far-field/orbital scene demand)"
                                                         : "OFF (no far-field/orbital scene demand)") +
                         (hasComposedTraversalOverride
                              ? (composedTraversalOverride ? "; VIXEN_COMPOSED_TRAVERSAL=1 override"
                                                           : "; VIXEN_COMPOSED_TRAVERSAL=0 override")
                              : "; no override"));
    }
    if (hasComposedTraversalOverride && mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] VIXEN_COMPOSED_TRAVERSAL override resolved -- near-field backend "
                          "(RT vs DDA) resolved by device capability at shader-compile time");
    }

    computeShaderLibNode->RegisterShaderBuilder(
        [this, marchFamily, marchShaderFeatures, deviceNode, composedTraversalRequested]
        (int vulkanVer, int spirvVer) {
        // Composed-traversal capability resolution: the VulkanDevice is live by
        // now (this lambda runs from ShaderLibraryNode::CompileImpl, which reads
        // its own VULKAN_DEVICE_IN input -- populated by DeviceNode::CompileImpl,
        // an upstream dependency that has already run). Mirrors the same
        // RTXCapabilities.rayQuery check BodyOctreeSceneNode::EnsureRtQueryTlasBuilt
        // uses at TLAS-build time, so both sites agree on device support. Local
        // copy of marchShaderFeatures (not mutating the captured vector) since
        // this builder can re-run on device recompilation -- a mutable lambda
        // would accumulate the pushed define on every re-invocation.
        std::vector<std::string> resolvedFeatures = marchShaderFeatures;
        bool composedUsesRtQuery = false;
        if (composedTraversalRequested) {
            auto* deviceNodeInst = static_cast<DeviceNode*>(renderGraph->GetInstance(deviceNode));
            Vixen::Vulkan::Resources::VulkanDevice* vulkanDevice =
                deviceNodeInst ? deviceNodeInst->GetVulkanDevice() : nullptr;
            const bool hasRayQuery = vulkanDevice &&
                vulkanDevice->GetRTXCapabilities().supported &&
                vulkanDevice->GetRTXCapabilities().rayQuery;
            composedUsesRtQuery = hasRayQuery;
            if (mainLogger && mainLogger->IsEnabled()) {
                mainLogger->Info(std::string("[BuildRenderGraph] VIXEN_COMPOSED_TRAVERSAL resolved: ") +
                                  (hasRayQuery ? "RT-traversal (RTXCapabilities.rayQuery available)"
                                               : "grid-DDA (software traversal substitute -- rayQuery unavailable)"));
            }
            resolvedFeatures.push_back(
                hasRayQuery ? kFeatureRtQueryTraversal.define : kFeatureBrickmapTraversal.define);
            // Also push the composed identity define itself -- gates the far-field
            // (footprint > brick) tier in SceneBindings.glsl/RayQueryTraversal.glsl,
            // which is additive to whichever near-field backend define was just
            // selected above (that define alone only picks the TraceWorld.glsl
            // dispatch branch, per the existing #ifdef/#elif chain).
            resolvedFeatures.push_back(kFeatureComposedTraversal.define);
            // Round-5 mandatory observability: which near-field backend the composed
            // path actually resolved to, on std::cout (not mainLogger, which is
            // disabled by default for this node) -- matches the [FarFieldCount]
            // precedent (VoxelGridNode.cpp) so a boot log always answers "which
            // #ifdef branch is live" without re-deriving it from device caps.
            std::cout << "[ComposedBackend] " << (hasRayQuery ? "RTQUERY" : "BRICKMAP") << std::endl;
        }
        auto builder = marchFamily->MakeBuilder(resolvedFeatures);
        // W-RTQUERY Slice A: GL_EXT_ray_query needs SPIR-V 1.4+ (rayQueryEXT opaque type +
        // the rayQuery* built-ins). The device-reported spirvVer is already >=140 whenever
        // VIXEN_RTQUERY_TRAVERSAL can actually be engaged (RTXCapabilities.rayQuery requires
        // the same VK_KHR_spirv_1_4 extension GetRTXExtensions() already requires for RT
        // generally -- see VulkanDevice.cpp), so this is a defensive floor, not a real bump
        // on any device this feature runs on; a flag-off compile never touches spirvVer.
        const int effectiveSpirvVer =
            (envFlagEnabled("VIXEN_RTQUERY_TRAVERSAL") || composedUsesRtQuery)
                ? std::max(spirvVer, 140) : spirvVer;
        builder.SetTargetVulkanVersion(vulkanVer)
               .SetTargetSpirvVersion(effectiveSpirvVer)
               .AddIncludePath("shaders")
               .AddIncludePath("../shaders")
#ifdef VIXEN_SHADER_SOURCE_DIR
               .AddIncludePath(VIXEN_SHADER_SOURCE_DIR)
#endif
               // Inc0 M5: BodyInstanceRayMarch.comp #includes "recipe/SdfCoreKernels.glsl"
               // under libraries/SVO/shaders — a different tree than the paths above.
               .AddIncludePath("libraries/SVO/shaders")
               .AddIncludePath("../libraries/SVO/shaders")
#ifdef VIXEN_SVO_SHADER_SOURCE_DIR
               .AddIncludePath(VIXEN_SVO_SHADER_SOURCE_DIR)
#endif
               .EnableCaching(&shaderCacheManager_);
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

    // Sampled Lighting Inc3 M1 (KI-018) → semantic-wiring slice C: the three lighting
    // shaders are ShaderFamilies — the SOURCE recipe (search-path resolve + raw read +
    // the two M10 shadow-debug env splices) declared once, the trace axis TYPED at the
    // registration site with the same vocabulary the wiring layer uses. All three
    // #include shaders/SceneBindings.glsl so the compiled programs stay byte-identical
    // on the shared portion. Cache keys are byte-identical to the old hand builders in
    // every mode except capture+shadow-debug COMBINED: the canonical feature splice
    // lands directly after #version, before the provider's M10 inserts, where the old
    // LIFO hand order put TRACE last — same defines, different byte order, so that one
    // debug-debug combo mints a distinct cache entry (functionally identical GLSL).
    const auto makeLightingFamily = [](const char* shaderName, const char* programName) {
        ShaderManagement::ShaderFamily::Config cfg;
        cfg.name = programName;
        cfg.stage = ShaderManagement::ShaderStage::Compute;
        return std::make_shared<ShaderManagement::ShaderFamily>(cfg,
            [shaderName]() {
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
                    throw std::runtime_error(std::string(shaderName) +
                                             " not found - check shader search paths");
                }
                std::ifstream in(compPath);
                std::stringstream buf;
                buf << in.rdbuf();
                std::string source = buf.str();
                // M10 shadow diagnostic (env-gated, off by default): inject
                // `#define VIXEN_SHADOW_DBG 1` + target-pixel coords right after
                // #version. Only SpatialReuseShade.comp has the arming block that
                // acts on it (the other lighting shaders get a harmless unused
                // define). This is a CONTENT-level env splice, so it lives in the
                // SourceProvider (march precedent) and NOT on the typed feature
                // axis — the PX/PY defines carry VALUES; features are bare defines.
                if (const char* pxEnv = std::getenv("VIXEN_SHADOW_DBG_PX")) {
                    if (const char* pyEnv = std::getenv("VIXEN_SHADOW_DBG_PY")) {
                        const std::string dbgDefines =
                            "#define VIXEN_SHADOW_DBG 1\n"
                            "#define VIXEN_SHADOW_DBG_PX " + std::string(pxEnv) + "\n"
                            "#define VIXEN_SHADOW_DBG_PY " + std::string(pyEnv) + "\n";
                        const size_t fnl = source.find('\n');
                        if (fnl == std::string::npos) source += "\n" + dbgDefines;
                        else source.insert(fnl + 1, dbgDefines);
                    }
                }
                // M10 causation A/B (env-gated, off by default): disables the coarse
                // mip-coverage any-hit occlusion paths (SceneBindings.glsl) so a shadow
                // ray only reports occlusion on a real SDF/DDA leaf crossing.
                if (envFlagEnabled("VIXEN_SHADOW_NO_MIP_ANYHIT")) {
                    const std::string noMipDef = "#define VIXEN_SHADOW_NO_MIP_ANYHIT 1\n";
                    const size_t fnl2 = source.find('\n');
                    if (fnl2 == std::string::npos) source += "\n" + noMipDef;
                    else source.insert(fnl2 + 1, noMipDef);
                }
                return source;
            });
    };
    std::vector<std::string> lightingShaderFeatures;
    if (envFlagEnabled("VIXEN_DEBUG_CAPTURE")) {
        lightingShaderFeatures.push_back(kFeatureGpuTraceHooks.define);
    }
    if (envFlagEnabled("VIXEN_COMPOSITION_COUNTERS")) {
        lightingShaderFeatures.push_back(kFeatureCompositionCounters.define);
    }
    if (envFlagEnabled("VIXEN_POLICY_STENCIL")) {
        lightingShaderFeatures.push_back(kFeaturePolicyStencil.define);
    }
    if (envFlagEnabled("VIXEN_POLICY_STENCIL") && envFlagEnabled("VIXEN_POLICY_STENCIL_TILES")) {
        // E11-T1: only ShadowVisibilityWave.comp #includes PolicyStencilTileBuffer;
        // every other lighting family just gets an inert #define (same shape as
        // kFeaturePolicyStencil's own broad share above).
        lightingShaderFeatures.push_back(kFeaturePolicyStencilTiles.define);
    }
    const auto registerLightingFamily = [this, lightingShaderFeatures](
                                            NodeHandle libHandle,
                                            std::shared_ptr<ShaderManagement::ShaderFamily> family) {
        auto* libNode = static_cast<ShaderLibraryNode*>(renderGraph->GetInstance(libHandle));
        libNode->RegisterShaderBuilder([this, family, lightingShaderFeatures](int vulkanVer, int spirvVer) {
            auto builder = family->MakeBuilder(lightingShaderFeatures);
            builder.SetTargetVulkanVersion(vulkanVer)
                   .SetTargetSpirvVersion(spirvVer)
                   .AddIncludePath("shaders")
                   .AddIncludePath("../shaders")
#ifdef VIXEN_SHADER_SOURCE_DIR
                   .AddIncludePath(VIXEN_SHADER_SOURCE_DIR)
#endif
                   .EnableCaching(&shaderCacheManager_);
            return builder;
        });
    };
    registerLightingFamily(directLightingShaderLib,
                           makeLightingFamily("DirectLighting.comp", "DirectLighting"));

    // Sampled Lighting Inc3 M5: SpatialReuseShade.comp — family-registered (see the
    // lighting-family block above; this shader owns the M10 VIXEN_SHADOW_DBG arming block).
    // W-LEAN L3: under the resolve opt-in the shade compiles its CELL-RESOLVE
    // variant (the retired standalone HitAccumResolve stage as a tail — the
    // wave's own dual-registration shape).
    if (hitAccumResolveEnabled) {
        auto srsFusedFamily = makeLightingFamily("SpatialReuseShade.comp", "SpatialReuseShade");
        auto srsFusedFeatures = lightingShaderFeatures;
        srsFusedFeatures.push_back(kFeatureSrsCellResolve.define);
        auto* srsLibNode = static_cast<ShaderLibraryNode*>(renderGraph->GetInstance(spatialReuseShaderLib));
        srsLibNode->RegisterShaderBuilder([this, srsFusedFamily, srsFusedFeatures](int vulkanVer, int spirvVer) {
            auto builder = srsFusedFamily->MakeBuilder(srsFusedFeatures);
            builder.SetTargetVulkanVersion(vulkanVer)
                   .SetTargetSpirvVersion(spirvVer)
                   .AddIncludePath("shaders")
                   .AddIncludePath("../shaders")
#ifdef VIXEN_SHADER_SOURCE_DIR
                   .AddIncludePath(VIXEN_SHADER_SOURCE_DIR)
#endif
                   .EnableCaching(&shaderCacheManager_);
            return builder;
        });
    } else {
        registerLightingFamily(spatialReuseShaderLib,
                               makeLightingFamily("SpatialReuseShade.comp", "SpatialReuseShade"));
    }

    auto exposureFamily = makeLightingFamily("ExposureTonemap.comp", "ExposureTonemap");
    if (hdrExposureEnabled) static_cast<ShaderLibraryNode*>(renderGraph->GetInstance(exposureShaderLib))
        ->RegisterShaderBuilder([this, exposureFamily](int vulkanVer, int spirvVer) {
            auto builder = exposureFamily->MakeBuilder({});
            builder.SetTargetVulkanVersion(vulkanVer)
                   .SetTargetSpirvVersion(spirvVer)
                   .AddIncludePath("shaders")
                   .AddIncludePath("../shaders")
#ifdef VIXEN_SHADER_SOURCE_DIR
                   .AddIncludePath(VIXEN_SHADER_SOURCE_DIR)
#endif
                   .EnableCaching(&shaderCacheManager_);
            return builder;
        });

    // W1a: the ProbeUpdate split's three compiled programs, each family-registered
    // (see the lighting-family block above). ProbeGather + ShadowRayTrace include
    // SceneBindings.glsl and carry the trace-hooks variant axis exactly like the
    // merged shader did; ProbeApply is single-variant (no scene includes) — the
    // shared lighting feature set is simply inert for it.
    registerLightingFamily(probeGatherShaderLib,
                           makeLightingFamily("ProbeGather.comp", "ProbeGather"));
    registerLightingFamily(shadowRayTraceShaderLib,
                           makeLightingFamily("ShadowRayTrace.comp", "ShadowRayTrace"));
    registerLightingFamily(probeApplyShaderLib,
                           makeLightingFamily("ProbeApply.comp", "ProbeApply"));
    // W1b: the derived-request shadow wave (SceneBindings consumer — carries
    // the trace-hooks variant axis like every traversal pass). W-SPLIT: the
    // wave is unconditionally plain again — VIXEN_HIT_ACCUM_FUSED retired,
    // the accumulate tail moved to its own dispatch below.
    registerLightingFamily(shadowVisibilityWaveShaderLib,
                           makeLightingFamily("ShadowVisibilityWave.comp", "ShadowVisibilityWave"));
    // W-SPLIT: the re-split accumulate — standalone bindings (no
    // SceneBindings, no trace-hooks axis; the shared lighting feature set is
    // inert for it, same as ProbeApply/RecipeInstanceBucketing above).
    if (hitAccumEnabled) {
        // B2 (batch-26, gated OFF by default batch-27): the table-wide clear —
        // same standalone-bindings shape as the accumulate pass, registered
        // ahead of it (topology orders clear -> accumulate below). Only
        // register when the node was actually created (hitAccumClearEnabled).
        if (hitAccumClearEnabled) {
            registerLightingFamily(hitAccumClearShaderLib,
                                   makeLightingFamily("HitAccumClear.comp", "HitAccumClear"));
        }
        registerLightingFamily(hitAccumAccumulateShaderLib,
                               makeLightingFamily("HitAccumulate.comp", "HitAccumulate"));
    }
    // W3c-2: the resolve pair. The cell shade is a SceneBindings consumer AND
    // an analytic-field consumer — its family source gets the SAME uber-recipe
    // splice the march's does (its marker precedes SceneBindings, the shader's
    // own ordering comment), so spliced sdfRecipe_<id> fields are tappable at
    // cell scale; its builder therefore carries the SVO include paths for
    // recipe/SdfCoreKernels.glsl. The resolve is a standalone-bindings shader
    // (plain lighting-family registration; the shared feature set is inert for
    // it — the ProbeApply precedent).
    if (hitAccumResolveEnabled) {
        ShaderManagement::ShaderFamily::Config cellCfg;
        cellCfg.name = "HitAccumCellShade";
        cellCfg.stage = ShaderManagement::ShaderStage::Compute;
        auto cellShadeFamily = std::make_shared<ShaderManagement::ShaderFamily>(cellCfg,
            [this]() {
                std::vector<std::filesystem::path> possiblePaths = {
#ifdef VIXEN_SHADER_SOURCE_DIR
                    std::filesystem::path(std::string(VIXEN_SHADER_SOURCE_DIR) + "/HitAccumCellShade.comp"),
#endif
                    std::filesystem::path("shaders/HitAccumCellShade.comp"),
                    std::filesystem::path("../shaders/HitAccumCellShade.comp"),
                };
                std::filesystem::path compPath;
                for (const auto& p : possiblePaths) {
                    if (std::filesystem::exists(p)) { compPath = p; break; }
                }
                if (compPath.empty()) {
                    throw std::runtime_error("HitAccumCellShade.comp not found - check shader search paths");
                }
                std::ifstream in(compPath);
                std::stringstream buf;
                buf << in.rdbuf();
                // The march's splice, second consumer — nullptr blob: the
                // march's own provider owns SetOccupancyGrid (a second move
                // of identical content would be redundant, not wrong).
                return Vixen::SVO::Recipe::SpliceProceduralRecipesIntoSource(
                    buf.str(), proceduralRecipes_, nullptr);
            });
        auto* cellLibNode = static_cast<ShaderLibraryNode*>(renderGraph->GetInstance(hitAccumCellShadeShaderLib));
        cellLibNode->RegisterShaderBuilder([this, cellShadeFamily, lightingShaderFeatures](int vulkanVer, int spirvVer) {
            auto builder = cellShadeFamily->MakeBuilder(lightingShaderFeatures);
            builder.SetTargetVulkanVersion(vulkanVer)
                   .SetTargetSpirvVersion(spirvVer)
                   .AddIncludePath("shaders")
                   .AddIncludePath("../shaders")
#ifdef VIXEN_SHADER_SOURCE_DIR
                   .AddIncludePath(VIXEN_SHADER_SOURCE_DIR)
#endif
                   .AddIncludePath("libraries/SVO/shaders")
                   .AddIncludePath("../libraries/SVO/shaders")
#ifdef VIXEN_SVO_SHADER_SOURCE_DIR
                   .AddIncludePath(VIXEN_SVO_SHADER_SOURCE_DIR)
#endif
                   .EnableCaching(&shaderCacheManager_);
            return builder;
        });
    }
    // W2a (reservoir path opt-in): the fold pass + the wave's reservoir-phase
    // variant. The gather is a standalone-binding shader but rides the SAME
    // lighting family maker (plain file, same search paths; the shared feature
    // set is inert for it — it has no trace-hooks axis). The reservoir-phase
    // lib registers the SAME ShadowVisibilityWave family with the phase define
    // appended — one program, two compiled variants, two pipelines.
    if (reservoirPathEnabled) {
        registerLightingFamily(spatialReuseGatherShaderLib,
                               makeLightingFamily("SpatialReuseGather.comp", "SpatialReuseGather"));
        auto waveReservoirFamily = makeLightingFamily("ShadowVisibilityWave.comp", "ShadowVisibilityWave");
        auto waveReservoirFeatures = lightingShaderFeatures;
        waveReservoirFeatures.push_back(kFeatureWaveReservoirPhase.define);
        auto* waveResLibNode = static_cast<ShaderLibraryNode*>(renderGraph->GetInstance(waveReservoirShaderLib));
        waveResLibNode->RegisterShaderBuilder([this, waveReservoirFamily, waveReservoirFeatures](int vulkanVer, int spirvVer) {
            auto builder = waveReservoirFamily->MakeBuilder(waveReservoirFeatures);
            builder.SetTargetVulkanVersion(vulkanVer)
                   .SetTargetSpirvVersion(spirvVer)
                   .AddIncludePath("shaders")
                   .AddIncludePath("../shaders")
#ifdef VIXEN_SHADER_SOURCE_DIR
                   .AddIncludePath(VIXEN_SHADER_SOURCE_DIR)
#endif
                   .EnableCaching(&shaderCacheManager_);
            return builder;
        });
    }

    // Recipe-Live-App-Bucketed-Dispatch Inc4 M3: RecipeInstanceBucketing.comp registration.
    // Same search-path pattern as DirectLighting.comp/ProbeUpdate.comp above -- a plain static
    // shader FILE (unlike the march's own recipe-splice path), so no per-recipe text splicing
    // is needed here; the shader's own bindings 0-8 are self-contained (does not #include
    // SceneBindings.glsl), so this shader-lib instance is entirely independent of the main
    // march's descriptor namespace.
    if (recipeBucketedDispatchEnabled) {
        auto* recipeBucketingShaderLibNode = static_cast<ShaderLibraryNode*>(renderGraph->GetInstance(recipeBucketingShaderLib));
        recipeBucketingShaderLibNode->RegisterShaderBuilder([this](int vulkanVer, int spirvVer) {
            ShaderManagement::ShaderBundleBuilder builder;
            constexpr const char* shaderName = "RecipeInstanceBucketing.comp";
            constexpr const char* programName = "RecipeInstanceBucketing";
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
            const std::string source = ReadShaderSourceWithTraceHooksGate(compPath, shaderName);
            builder.SetProgramName(programName)
                   .SetPipelineType(ShaderManagement::PipelineTypeConstraint::Compute)
                   .SetTargetVulkanVersion(vulkanVer)
                   .SetTargetSpirvVersion(spirvVer)
                   .AddIncludePath("shaders")
                   .AddIncludePath("../shaders")
#ifdef VIXEN_SHADER_SOURCE_DIR
                   .AddIncludePath(VIXEN_SHADER_SOURCE_DIR)
#endif
                   .EnableCaching(&shaderCacheManager_)
                   .AddStage(ShaderManagement::ShaderStage::Compute, source, "main");
            return builder;
        });
    }

    // Raster-proxy B1 M4: HiZDownsample.comp + InstanceOcclusionCull.comp registration —
    // both plain static shader FILES with self-contained bindings (neither #includes
    // SceneBindings.glsl), same search-path template as RecipeInstanceBucketing.comp above.
    // InstanceOcclusionCull.comp #includes "Generated/OctreeConfig.glsl", resolved by the
    // same AddIncludePath("shaders") set every builder here already carries.
    if (b1OcclusionCullEnabled) {
        // Semantic-wiring S2 slice B (app adoption): each shader is a
        // ShaderFamily — the SOURCE recipe declared once (raw file read, NO
        // env splicing inside), the feature axis TYPED at the registration
        // site. The env decision that used to hide inside
        // ReadShaderSourceWithTraceHooksGate is now one visible line building
        // the feature list from the same vocabulary the wiring layer uses;
        // the family applies the identical after-#version splice, so cache
        // keys are byte-identical to the old path.
        const auto makeB1Family = [](const char* shaderName, const char* programName) {
            ShaderManagement::ShaderFamily::Config cfg;
            cfg.name = programName;
            cfg.stage = ShaderManagement::ShaderStage::Compute;
            return std::make_shared<ShaderManagement::ShaderFamily>(cfg,
                [shaderName]() {
                    std::vector<std::filesystem::path> possiblePaths = {
#ifdef VIXEN_SHADER_SOURCE_DIR
                        std::string(VIXEN_SHADER_SOURCE_DIR) + "/" + shaderName,
#endif
                        std::string("shaders/") + shaderName,
                        std::string("../shaders/") + shaderName,
                        shaderName
                    };
                    for (const auto& path : possiblePaths) {
                        if (std::filesystem::exists(path)) {
                            std::ifstream in(path);
                            std::stringstream buf;
                            buf << in.rdbuf();
                            return buf.str();
                        }
                    }
                    throw std::runtime_error(std::string(shaderName) +
                                             " not found - check shader search paths");
                });
        };
        std::vector<std::string> b1ShaderFeatures;
        if (envFlagEnabled("VIXEN_DEBUG_CAPTURE")) {
            b1ShaderFeatures.push_back(kFeatureGpuTraceHooks.define);
        }
        const auto registerFamily = [this, b1ShaderFeatures](
                                        NodeHandle libHandle,
                                        std::shared_ptr<ShaderManagement::ShaderFamily> family) {
            auto* libNode = static_cast<ShaderLibraryNode*>(renderGraph->GetInstance(libHandle));
            libNode->RegisterShaderBuilder([this, family, b1ShaderFeatures](int vulkanVer, int spirvVer) {
                auto builder = family->MakeBuilder(b1ShaderFeatures);
                builder.SetTargetVulkanVersion(vulkanVer)
                       .SetTargetSpirvVersion(spirvVer)
                       .AddIncludePath("shaders")
                       .AddIncludePath("../shaders")
#ifdef VIXEN_SHADER_SOURCE_DIR
                       .AddIncludePath(VIXEN_SHADER_SOURCE_DIR)
#endif
                       .EnableCaching(&shaderCacheManager_);
                return builder;
            });
        };
        registerFamily(b1HizShaderLib,  makeB1Family("HiZDownsample.comp",         "HiZDownsample"));
        registerFamily(b1CullShaderLib, makeB1Family("InstanceOcclusionCull.comp", "InstanceOcclusionCull"));
    }

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
    // Baked-Perf M4 Task 4.3's PARAM_DISPATCH_ENABLED is set at the END of this function (see the
    // block right before batch.RegisterAll()), NOT here -- several VIXEN_*_DEMO blocks further
    // down force-enable VIXEN_PROBE_GRID_CONFIG_ENABLED via _putenv_s AFTER this point in the
    // SAME function, so reading ResolveProbeGridEnabled()/ResolveReservoirEnabled() this early
    // would see the pre-demo-override default and wire the wrong value (found live: Cornell's
    // force-enable landed too late, permanently disabling probe_update's dispatch on the demo
    // that most needs it).

    // W1b: the derived-request shadow wave — 1D over the hit-record buffer's
    // pixel count (slot = linear pixel index; the shader bounds itself on
    // hitRecords.length(), so the ceil over-dispatch is a harmless tail).
    // Same build-time-extent derivation as DirectLighting immediately above
    // (VIXEN_RENDER_SCALE fixed at process start; dims never live-resize).
    // Dispatches unconditionally — the march always produces records, the
    // shade pass always consumes bits; there is no off-path.
    {
        const uint32_t wavePixelsX = static_cast<uint32_t>(width * renderScale);
        const uint32_t wavePixelsY = static_cast<uint32_t>(height * renderScale);
        const uint32_t waveDispatchX = (wavePixelsX * wavePixelsY + 63u) / 64u;
        auto* shadowVisibilityWave = static_cast<ComputeStageNode*>(renderGraph->GetInstance(shadowVisibilityWaveNode));
        shadowVisibilityWave->SetParameter(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);
        shadowVisibilityWave->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_X, waveDispatchX);
        shadowVisibilityWave->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Y, 1u);
        shadowVisibilityWave->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Z, 1u);

        // B2 (batch-26, gated OFF by default batch-27): the table-wide clear —
        // 1D over kHitAccumTableCapacity (65536/64 = 1024 workgroups; the
        // cell-shade dispatch's own capacity-sized shape,
        // VulkanGraphApplication::kHitAccumTableCapacity). When enabled it
        // runs before the accumulate pass (topology below) so every slot
        // starts this epoch genuinely empty.
        if (hitAccumClearEnabled) {
            auto* clearStage = static_cast<ComputeStageNode*>(renderGraph->GetInstance(hitAccumClearNode));
            clearStage->SetParameter(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);
            clearStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_X, kHitAccumTableCapacity / 64u);
            clearStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Y, 1u);
            clearStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Z, 1u);
        }

        // W-SPLIT: the accumulate pass — SAME 1D dispatch shape as the wave
        // (it walks the identical hit-record slot count, just earlier in the
        // frame). Exists whenever hitAccumEnabled (not just resolve — the
        // shutdown diag needs it regardless of whether the resolve composite
        // is on).
        if (hitAccumEnabled) {
            auto* accumulateStage = static_cast<ComputeStageNode*>(renderGraph->GetInstance(hitAccumAccumulateNode));
            accumulateStage->SetParameter(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);
            accumulateStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_X, waveDispatchX);
            accumulateStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Y, 1u);
            accumulateStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Z, 1u);
        }

        // W2a (reservoir path opt-in): the gather is 2D over the same scaled
        // extent (8×8 workgroups, the shade's own shape — its fold logic is
        // pixel-2D); the reservoir-phase wave is 1D over the same slot count
        // as the analytic phase. Both dispatch only when the path is enabled —
        // when disabled these nodes don't exist at all (B1 shape).
        if (reservoirPathEnabled) {
            auto* gatherStage = static_cast<ComputeStageNode*>(renderGraph->GetInstance(spatialReuseGatherNode));
            gatherStage->SetParameter(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);
            gatherStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_X, (wavePixelsX + 7u) / 8u);
            gatherStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Y, (wavePixelsY + 7u) / 8u);
            gatherStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Z, 1u);
            auto* waveReservoir = static_cast<ComputeStageNode*>(renderGraph->GetInstance(waveReservoirNode));
            waveReservoir->SetParameter(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);
            waveReservoir->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_X, waveDispatchX);
            waveReservoir->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Y, 1u);
            waveReservoir->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Z, 1u);
            // The gather's own push block (imgWidth/imgHeight — ReservoirConfig
            // carries no extent and the pass binds no image to derive from).
            gatherWidthConstant  = renderGraph->AddNode<ConstantNodeType>("spatial_reuse_gather_width_constant");
            gatherHeightConstant = renderGraph->AddNode<ConstantNodeType>("spatial_reuse_gather_height_constant");
            static_cast<ConstantNode*>(renderGraph->GetInstance(gatherWidthConstant))->SetValue<uint32_t>(wavePixelsX);
            static_cast<ConstantNode*>(renderGraph->GetInstance(gatherHeightConstant))->SetValue<uint32_t>(wavePixelsY);
        }

        // W3b: clear is 1D over the table (capacity/64, exact — capacity is a
        // multiple of 64); accumulate is 1D over the pixel slots (the wave's
        // own dispatch). Constants: mode selectors, extents, detailSize0; the
        // camera-position constant is (re)set every frame from PreTick.
        if (hitAccumEnabled) {
            // Size (48 bytes, HitAccumParams: uint,3 floats,2 vec4) is now fixed
            // inside HitAccumParamsConfigNode::CompileImpl — no PARAM_SIZE_BYTES
            // to set (B2 ring conversion, batch-23).
            // Engagement threshold: env-overridable (the primary cone coef is
            // tan-based ~0.0016 at 500 px, so default scenes need a SMALL
            // detail to engage — the Dozen readout's finding #1; the diag boot
            // sets e.g. 0.02 to make its numeric gate non-vacuous).
            float hitAccumDetail = kHitAccumDetailSize0;
            if (const char* detailEnv = std::getenv("VIXEN_HIT_ACCUM_DETAIL")) {
                const float parsed = static_cast<float>(std::atof(detailEnv));
                if (parsed > 0.0f) hitAccumDetail = parsed;
            }
            hitAccumDetailSize0_ = hitAccumDetail;
            // W3c-3: temporal-absorption EMA alpha (rides camPos.w — see the
            // PreTick write). Default 0.25 absorbs the duplicate-partition
            // scatter over ~4 frames; 1.0 disables (bit-old behavior).
            if (const char* temporalEnv = std::getenv("VIXEN_HIT_ACCUM_TEMPORAL")) {
                const float parsed = static_cast<float>(std::atof(temporalEnv));
                if (parsed > 0.0f && parsed <= 1.0f) hitAccumTemporalAlpha_ = parsed;
            }
            // W-LEAN L1 (requires the resolve — without a composite the
            // per-pixel bits ARE the image): skip per-pixel shadow traces for
            // pixels the composite fully replaces (w == 1).
            hitAccumLeanEnabled_ = hitAccumResolveEnabled &&
                                   envFlagEnabled("VIXEN_HIT_ACCUM_LEAN");
            auto* tableInst = static_cast<StorageBufferNode*>(renderGraph->GetInstance(hitAccumTableBuffer));
            // uint32_t, matching the ValueTag the node reads (the ddgiLeakGateDebug
            // precedent's own `60u` — a uint64_t here stores a mismatched tag and
            // reads back as 0, found live at the W3b gate).
            tableInst->SetParameter(StorageBufferNodeConfig::PARAM_SIZE_BYTES,
                                    kHitAccumTableCapacity * kHitAccumEntryBytes);
        }

        // W3c-2 / W-LEAN L3: cell radiance (vec4/slot) + the cell-shade
        // dispatch — 1D over the FULL table (capacity/64 exact — cheaper than
        // compaction+indirect at 64Ki slots, per the Dozen readout's
        // orchestration-cost finding). The composite is SpatialReuseShade's
        // own gated tail (no standalone stage, no extra dispatch).
        if (hitAccumResolveEnabled) {
            static_cast<StorageBufferNode*>(renderGraph->GetInstance(hitAccumCellRadianceBuffer))
                ->SetParameter(StorageBufferNodeConfig::PARAM_SIZE_BYTES,
                               kHitAccumTableCapacity * 16u);  // vec4 std430
            auto* cellShadeStage = static_cast<ComputeStageNode*>(renderGraph->GetInstance(hitAccumCellShadeNode));
            cellShadeStage->SetParameter(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);
            cellShadeStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_X, kHitAccumTableCapacity / 64u);
            cellShadeStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Y, 1u);
            cellShadeStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Z, 1u);
        }
    }

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
    // W1a: gather writes {requests, payloads}; wave reads {requests} writes {results};
    // apply reads {results, payloads}.
    static_cast<BufferSyncGathererNode*>(renderGraph->GetInstance(probeGatherWriteGatherer))->PreRegisterBufferSlots(2);
    static_cast<BufferSyncGathererNode*>(renderGraph->GetInstance(shadowRayTraceReadGatherer))->PreRegisterBufferSlots(1);
    static_cast<BufferSyncGathererNode*>(renderGraph->GetInstance(shadowRayTraceWriteGatherer))->PreRegisterBufferSlots(1);
    static_cast<BufferSyncGathererNode*>(renderGraph->GetInstance(probeApplyReadGatherer))->PreRegisterBufferSlots(2);
    // W1b: the shadow wave reads {hitRecordBuffer} and writes {hitRecordBuffer}
    // (an RMW of one word — but the hazard model tracks whole resources).
    // W-SPLIT: 2-wide again — {hitRecordBuffer, policyStencilTileBuffer} (KI-052 fix,
    // E12-T1). policyStencilTileBuffer is wired unconditionally (same convention as its
    // provider registration below), matching the buffer's own always-created, only-
    // exercised-under-VIXEN_POLICY_STENCIL_TILES no-op-when-idle shape (E11-T1 §7) — a
    // baked barrier on a buffer neither side touches this frame costs nothing observable.
    static_cast<BufferSyncGathererNode*>(renderGraph->GetInstance(shadowVisibilityWaveReadGatherer))->PreRegisterBufferSlots(2);
    static_cast<BufferSyncGathererNode*>(renderGraph->GetInstance(shadowVisibilityWaveWriteGatherer))->PreRegisterBufferSlots(1);
    // KI-052 fix (E12-T1): march-side write gatherer, 1 entry (policyStencilTileBuffer only
    // — HitRecordBuffer keeps using computeDispatch's original singular BUFFER_WRITE slot,
    // unmigrated, exactly as the DirectLighting hazard comment below already documents).
    static_cast<BufferSyncGathererNode*>(renderGraph->GetInstance(policyStencilTileWriteGatherer))->PreRegisterBufferSlots(1);
    // B2 (batch-26, gated OFF by default batch-27): the clear pass writes
    // {table} only (no reads).
    if (hitAccumClearEnabled) {
        static_cast<BufferSyncGathererNode*>(renderGraph->GetInstance(hitAccumClearWriteGatherer))->PreRegisterBufferSlots(1);
    }
    // W-SPLIT: the accumulate pass reads {hitRecordBuffer}, writes {table,
    // hitRecordBuffer} (it RMWs the record's flags word AND writes the table).
    if (hitAccumEnabled) {
        static_cast<BufferSyncGathererNode*>(renderGraph->GetInstance(hitAccumAccumulateReadGatherer))->PreRegisterBufferSlots(1);
        static_cast<BufferSyncGathererNode*>(renderGraph->GetInstance(hitAccumAccumulateWriteGatherer))->PreRegisterBufferSlots(2);
    }
    // W3c-2 / W-LEAN L3: cell shade reads {table}, writes {cellRadiance};
    // the shade's fold reads {cellRadiance, table} via SRS's first buffer
    // read gatherer (records were already its own).
    if (hitAccumResolveEnabled) {
        static_cast<BufferSyncGathererNode*>(renderGraph->GetInstance(hitAccumCellShadeReadGatherer))->PreRegisterBufferSlots(1);
        static_cast<BufferSyncGathererNode*>(renderGraph->GetInstance(hitAccumCellShadeWriteGatherer))->PreRegisterBufferSlots(1);
        static_cast<BufferSyncGathererNode*>(renderGraph->GetInstance(spatialReuseCellReadGatherer))->PreRegisterBufferSlots(2);
    }
    // W2a (reservoir path opt-in): gather reads {reservoirA, reservoirB,
    // hitRecords} (the A/B neighbor-array hazard moved here from the shade)
    // and writes {combinedReservoir}; the reservoir-phase wave reads
    // {combinedReservoir, hitRecords} and writes {hitRecords} (bit-4 RMW).
    if (reservoirPathEnabled) {
        static_cast<BufferSyncGathererNode*>(renderGraph->GetInstance(spatialReuseGatherReadGatherer))->PreRegisterBufferSlots(3);
        static_cast<BufferSyncGathererNode*>(renderGraph->GetInstance(spatialReuseGatherWriteGatherer))->PreRegisterBufferSlots(1);
        static_cast<BufferSyncGathererNode*>(renderGraph->GetInstance(waveReservoirReadGatherer))->PreRegisterBufferSlots(2);
        static_cast<BufferSyncGathererNode*>(renderGraph->GetInstance(waveReservoirWriteGatherer))->PreRegisterBufferSlots(1);
    }

    // Sampled Lighting Inc4 M2: probe atlas image-array gatherer -- 2 entries (irradiance +
    // visibility atlas, see probeAtlasGatherer's own declaration comment above).
    static_cast<ImageSyncGathererNode*>(renderGraph->GetInstance(probeAtlasGatherer))->PreRegisterImageSlots(2);

    // Sampled Lighting Inc4 M5: read-side atlas gatherer -- same 2 entries, feeding
    // spatialReuseNode's IMAGE_READ_ARRAY (see spatialReuseProbeAtlasReadGatherer's own
    // declaration comment above).
    static_cast<ImageSyncGathererNode*>(renderGraph->GetInstance(spatialReuseProbeAtlasReadGatherer))->PreRegisterImageSlots(2);

    // Raster-proxy B1 M4: one tile image each side (HiZ write / cull read), one skip-mask
    // buffer entry on the cull's write side.
    if (b1OcclusionCullEnabled) {
        static_cast<ImageSyncGathererNode*>(renderGraph->GetInstance(b1HizTileWriteGatherer))->PreRegisterImageSlots(1);
        static_cast<ImageSyncGathererNode*>(renderGraph->GetInstance(b1CullTileReadGatherer))->PreRegisterImageSlots(1);
        static_cast<BufferSyncGathererNode*>(renderGraph->GetInstance(b1CullMaskWriteGatherer))->PreRegisterBufferSlots(1);
    }

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
    // W1a: the wave answers the FULL fixed-slot queue every tick (stale slots
    // included — the measured-before-optimized waste ProbeUpdateCommon.glsl's
    // slot-addressing note documents), so its dispatch is amortization-
    // INDEPENDENT: slotCount / local_size_x(64), an exact division since the
    // slot count is probeCount * 256.
    const uint32_t kShadowRayTraceDispatchX = kShadowRaySlotCount / 64u;
    mainLogger->Info("[BuildRenderGraph] ProbeGather/Apply sparse dispatch (Inc6 M1 shape): probeCount=" +
                      std::to_string(kProbeUpdateDefaultProbeCount) + " amortizationFactor=" +
                      std::to_string(kDdgiAmortizationFactor) + " -> dispatchX=" +
                      std::to_string(kProbeUpdateDispatchX) + "; ShadowRayTrace waveDispatchX=" +
                      std::to_string(kShadowRayTraceDispatchX) + " (slots=" +
                      std::to_string(kShadowRaySlotCount) + ")");
    auto* probeGather = static_cast<ComputeStageNode*>(renderGraph->GetInstance(probeGatherNode));
    probeGather->SetParameter(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);
    probeGather->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_X, kProbeUpdateDispatchX);
    probeGather->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Y, 1u);
    probeGather->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Z, 1u);
    auto* shadowRayTrace = static_cast<ComputeStageNode*>(renderGraph->GetInstance(shadowRayTraceNode));
    shadowRayTrace->SetParameter(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);
    shadowRayTrace->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_X, kShadowRayTraceDispatchX);
    shadowRayTrace->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Y, 1u);
    shadowRayTrace->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Z, 1u);
    auto* probeApply = static_cast<ComputeStageNode*>(renderGraph->GetInstance(probeApplyNode));
    probeApply->SetParameter(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);
    probeApply->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_X, kProbeUpdateDispatchX);
    probeApply->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Y, 1u);
    probeApply->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Z, 1u);

    // W1a queue/payload buffer sizing — fixed slot geometry (see the constants'
    // own comment beside the atlas dimensions above).
    static_cast<StorageBufferNode*>(renderGraph->GetInstance(shadowRayRequestBuffer))
        ->SetParameter(StorageBufferNodeConfig::PARAM_SIZE_BYTES, kShadowRaySlotCount * kShadowRayRequestStrideBytes);
    static_cast<StorageBufferNode*>(renderGraph->GetInstance(shadowRayResultBuffer))
        ->SetParameter(StorageBufferNodeConfig::PARAM_SIZE_BYTES, kShadowRaySlotCount * kShadowRayResultStrideBytes);
    static_cast<StorageBufferNode*>(renderGraph->GetInstance(probeRayPayloadBuffer))
        ->SetParameter(StorageBufferNodeConfig::PARAM_SIZE_BYTES, kShadowRaySlotCount * kProbeRayPayloadStrideBytes);
    // Baked-Perf M4 Task 4.3's PARAM_DISPATCH_ENABLED is set at the END of this function --
    // see directLighting's own identical note above for why (Cornell's force-enable of
    // VIXEN_PROBE_GRID_CONFIG_ENABLED runs later in this SAME function).

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
    if (envFlagEnabled("VIXEN_TIER_M8_FLIGHT_DEMO")) {
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
    if (envFlagEnabled("VIXEN_TIER_ZOOM_DEMO")) {
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
    if (envFlagEnabled("VIXEN_TIER_EARTH_ZOOM_DEMO") || envFlagEnabled("VIXEN_TIER_EARTH_ZOOM_SCRIPT")) {
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
    if (envFlagEnabled("VIXEN_TIER_OBSERVABLE_DEMO")) {
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
    if (envFlagEnabled("VIXEN_TIER_M8_EARTH_DEMO") && !envFlagEnabled("VIXEN_TIER_M8_FLIGHT_DEMO")) {
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_X, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Y, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Z, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_DISTANCE, 236.0f);
        mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_M8_EARTH_DEMO: orbitCenter set to demo "
                          "body's world center (64,64,64) so the scripted zoom actually orbits the body");
    } else if (envFlagEnabled("VIXEN_TIER_M8_FLIGHT_DEMO")) {
        mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_M8_FLIGHT_DEMO: skipping orbit-param "
                          "wiring -- camera stays in FIXED mode so the Task 19 scripted "
                          "SetPositionForTest flight path is authoritative, not overridden by "
                          "an orbit-mode recompute every frame");
    }
    // Recipe-Diversity-Stress-Scene Inc6 M2: camera preset for VIXEN_RECIPE_DIVERSITY_STRESS_DEMO's
    // spatial grid (centered on world (64,64,64), same convention as every other demo above).
    // CameraNode::kOrbitDistanceMax (120.0, a hard clamp -- SetParameter/EngageOrbit both clamp
    // into it) means the WHOLE grid cannot be framed in one shot at N's upper end (a 14x14 grid
    // at N=192 spans ~156-234 world units per side depending on kGridSpacing, well beyond what
    // 120 units of orbit distance can frame at the shared 45-degree FOV) -- this is a real,
    // accepted limitation, not a bug: the live-run gate only requires visually confirming SOME
    // bodies at genuinely distinct positions with distinct shapes, not literally all N in one
    // screenshot (no single frame could show 250 legible distinct shapes regardless of framing).
    // Pitch angled downward (looking down at the grid from above-and-outside, not a flat
    // horizontal view into a wall of overlapping-in-screen-space spheres) gives the widest
    // legible view of the grid's near corner within the distance cap.
    if (envFlagEnabled("VIXEN_RECIPE_DIVERSITY_STRESS_DEMO")) {
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_X, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Y, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Z, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_DISTANCE, 118.0f);  // near kOrbitDistanceMax
        camera->SetParameter(CameraNodeConfig::PARAM_PITCH, 0.9f);  // ~51 degrees, looking down at the grid
        mainLogger->Info("[BuildRenderGraph] VIXEN_RECIPE_DIVERSITY_STRESS_DEMO: orbitCenter set to "
                          "(64,64,64), orbitDistance=118 (near the 120 cap), pitch=0.9rad -- frames "
                          "the grid's near region from above; the full grid may extend beyond frame "
                          "at high N, which the live-run gate accounts for");
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
    // M6b Task 6b.1/6b.2: also applies to VIXEN_DDGI_CORNELL_HYBRID_DEMO and
    // VIXEN_DDGI_CORNELL_MIXED_DEMO (mixed-provider variants) -- same shared Cornell box
    // geometry/camera-preset source, only the per-body provider assignment differs. Missing
    // this branch (found live, M6b round 1) leaves the camera on the unrelated legacy
    // VoxelGridNode PRESET-1 default, pointed away from the actual scene -- symptom was a
    // near-total scene miss (3279/250000 hits) that looked like a rendering bug but was
    // purely a wrong camera transform.
    if (envFlagEnabled("VIXEN_DDGI_CORNELL_BAKED_DEMO") || envFlagEnabled("VIXEN_DDGI_CORNELL_VIRTUAL_DEMO") ||
        envFlagEnabled("VIXEN_DDGI_CORNELL_HYBRID_DEMO") || envFlagEnabled("VIXEN_DDGI_CORNELL_MIXED_DEMO")) {
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
        if (envFlagEnabled("VIXEN_TIER_CROSSING_DEMO")) {
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
                const bool forceNonResident = envFlagEnabled("VIXEN_TIER_CROSSING_NONRESIDENT")
                                            || envFlagEnabled("VIXEN_TIER_ZOOM_DEMO");

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
                // Recipe-Parameterization M2 Task 6: seed a real per-instance varying value
                // (not the previous all-zero placeholder) so a future ReadParam-using recipe
                // (M3's live demo) has something live to read; none of these N demo programs
                // use ReadParam yet, so this is inert today but proves the SSBO carries a
                // genuinely varying value end-to-end per instance.
                inst.recipeParams[0] = static_cast<float>(i % 10);
                uberBodies.push_back(inst);
            }

            // Recipe-Parameterization M3 Task 8: append ONE genuinely ReadParam-driven body —
            // program = { sphere(center, kReadParamDemoBaseRadius), ReadParam(0), MathSub }, i.e.
            // sd = |p-center| - kReadParamDemoBaseRadius - params[0] (MathSub is non-commutative
            // a-b; stack order [sphereSD, readParam] gives a=sphereSD, b=params[0]). Subtracting a
            // runtime-read scalar from a baked sphere's distance field is exactly a radius offset,
            // so this body's rendered radius = kReadParamDemoBaseRadius + recipeParams[0], varied
            // live in PreTick() (VIXEN_PROCEDURAL_UBER_DEMO's own sweep, gated on the SAME env var
            // — see PreTick's "Recipe-Parameterization M3 Task 8" block). recipeId = 2+n is the
            // next free id after the N structurally-varied bodies just registered above.
            // ReadParam/MathSub are deliberately NOT on the Lipschitz occupancy-grid whitelist
            // (RecipeOccupancy.h) — DeriveOccupancyGrid declines gracefully (occupancyGridDim=0,
            // documented non-hazard), and are NOT on DeriveConservativeBounds's whitelist either,
            // so boundCenter/boundRadius are authored explicitly here (must cover the full sweep
            // range, not just the base radius) rather than left for derivation to guess wrong.
            {
                constexpr float kReadParamDemoBaseRadius = 6.0f;
                constexpr float kReadParamDemoSweepMax   = 3.0f;  // PreTick sweeps params[0] in [-max,+max]
                const uint32_t readParamRecipeId = static_cast<uint32_t>(2 + n);
                // X-offset from body 0's own center (64,64,kBaseZ) — NOT kBaseZ-kSpacingZ (a
                // negative-Z placement that landed behind the camera/outside the frustum in the
                // first authored version of this block, confirmed by a live capture showing zero
                // visible change — fixed by placing this body at the SAME Z as the already-
                // confirmed-visible body 0, offset sideways so the two don't overlap).
                const glm::vec3 rpCenter(64.0f + 14.0f, 64.0f, kBaseZ);

                SdfInstruction rpSphere{}; rpSphere.opCode = (uint8_t)SdfOpCode::Sphere;
                rpSphere.data[0] = rpCenter.x; rpSphere.data[1] = rpCenter.y; rpSphere.data[2] = rpCenter.z;
                rpSphere.data[3] = kReadParamDemoBaseRadius;
                SdfInstruction rpRead{}; rpRead.opCode = (uint8_t)SdfOpCode::ReadParam;
                rpRead.paramMask = 1; rpRead.data[0] = 0.0f;  // index 0 into recipeParams[]
                SdfInstruction rpSub{}; rpSub.opCode = (uint8_t)SdfOpCode::MathSub;

                Vixen::SVO::RecipeRegistry::RecipeEntry rpEntry{};
                rpEntry.bytecode    = { rpSphere, rpRead, rpSub };
                rpEntry.boundCenter = rpCenter;
                rpEntry.boundRadius = kReadParamDemoBaseRadius + kReadParamDemoSweepMax + 2.0f;  // margin

                auto rpRegResult = RegisterProceduralRecipe(readParamRecipeId, rpEntry);
                if (rpRegResult != Vixen::SVO::RecipeRegistry::RegisterResult::Ok) {
                    mainLogger->Error("[BuildRenderGraph] VIXEN_PROCEDURAL_UBER_DEMO: "
                                     "RegisterProceduralRecipe(ReadParam demo, " +
                                     std::to_string(readParamRecipeId) + ") failed, code " +
                                     std::to_string(static_cast<int>(rpRegResult)));
                } else {
                    Vixen::SVO::BodyInstanceGpu rpInst{};
                    rpInst.renderScale  = 1.0f;   // unused by Procedural
                    rpInst.color[0] = 1.0f; rpInst.color[1] = 0.85f; rpInst.color[2] = 0.2f;  // gold tint, visually distinct
                    rpInst.octreeIndex  = 0u;     // unused by Procedural
                    rpInst.providerKind = 1u;     // PROVIDER_PROCEDURAL
                    rpInst.recipeId     = readParamRecipeId;
                    rpInst.recipeParams[0] = 0.0f;  // PreTick's sweep overwrites this every frame
                    uberBodies.push_back(rpInst);
                    mainLogger->Info("[BuildRenderGraph] VIXEN_PROCEDURAL_UBER_DEMO: registered "
                                     "ReadParam demo body recipeId=" + std::to_string(readParamRecipeId) +
                                     " baseRadius=" + std::to_string(kReadParamDemoBaseRadius));
                }
            }

            if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                bodyScene->SetInstances(std::move(uberBodies));
                mainLogger->Info("[BuildRenderGraph] VIXEN_PROCEDURAL_UBER_DEMO: seeded " +
                                 std::to_string(n) + " zero-bake procedural body instances "
                                 "(0 BakeSdfWorld/BuildSdfBodyOctree calls for these bodies)");
            }
        } else if (envFlagEnabled("VIXEN_RECIPE_HOT_COLD_DEMO")) {
            // Recipe-Live-App-Bucketed-Dispatch Inc4 M3: live-gate scene construction --
            // registers a mix of "hot" recipes (>= kRecipeBucketingHotnessThreshold instances
            // each, so VIXEN_RECIPE_BUCKETED_DISPATCH's bucketing pass promotes them) and "cold"
            // recipes (1-2 instances each, staying below threshold, handled by tier-0 only).
            // Reuses VIXEN_PROCEDURAL_UBER_DEMO's own zero-bake construction convention
            // (RegisterProceduralRecipe + BodyOctreeSceneNode::SetInstances, no octree bake) --
            // this demo's own scope is just "produce a real hot+cold instance-count
            // distribution," not a new construction mechanism.
            // M4 perf-measurement note: population mix is overridable via 4 optional env vars
            // (VIXEN_RECIPE_HOT_COLD_DEMO_{HOT_RECIPES,COLD_RECIPES,HOT_INSTANCES,COLD_INSTANCES}),
            // each defaulting to the exact value M3 shipped (3/3/6/2) when unset -- so the M3 gate
            // and every prior milestone's documented scene remain byte-identical by default. This
            // is scene-construction flexibility only, not a change to the bucketing/compositing/
            // dispatch mechanism itself (out of scope for M4 per its own prompt).
            auto envOr = [](const char* name, int fallback) -> int {
                const char* v = std::getenv(name);
                if (!v) return fallback;
                int parsed = std::atoi(v);
                return parsed > 0 ? parsed : fallback;
            };
            const int kHotRecipeCount    = envOr("VIXEN_RECIPE_HOT_COLD_DEMO_HOT_RECIPES", 3);
            const int kColdRecipeCount   = envOr("VIXEN_RECIPE_HOT_COLD_DEMO_COLD_RECIPES", 3);
            const int kInstancesPerHot   = envOr("VIXEN_RECIPE_HOT_COLD_DEMO_HOT_INSTANCES", 6);
            const int kInstancesPerCold  = envOr("VIXEN_RECIPE_HOT_COLD_DEMO_COLD_INSTANCES", 2);
            constexpr float kSpacingX = 30.0f;
            constexpr float kSpacingZ = 40.0f;
            constexpr float kBaseZ    = 30.0f;
            // Fixed-size palettes, indexed modulo when a M4 measurement run asks for more
            // recipes than the original M3 demo's 3+3 -- color repeats across indices past 3,
            // which is fine for an FPS measurement (colors are a visual-attribution aid only).
            static const glm::vec3 kHotColors[3] = {
                glm::vec3(1.00f, 0.30f, 0.30f),  // hot recipe 0: red
                glm::vec3(0.30f, 1.00f, 0.30f),  // hot recipe 1: green
                glm::vec3(0.30f, 0.45f, 1.00f),  // hot recipe 2: blue
            };
            static const glm::vec3 kColdColors[3] = {
                glm::vec3(1.00f, 1.00f, 0.30f),  // cold recipe 0: yellow
                glm::vec3(1.00f, 0.30f, 1.00f),  // cold recipe 1: magenta
                glm::vec3(0.30f, 1.00f, 1.00f),  // cold recipe 2: cyan
            };

            std::vector<Vixen::SVO::BodyInstanceGpu> hotColdBodies;
            uint32_t nextRecipeId = 2;
            int totalInstances = 0;

            auto registerAndSeed = [&](uint32_t recipeId, const glm::vec3& tint, float centerX, int instanceCount) {
                using Vixen::SVO::Recipe::SdfOpCode;
                using Vixen::SVO::Recipe::SdfInstruction;
                SdfInstruction sphere{};
                sphere.opCode = static_cast<uint8_t>(SdfOpCode::Sphere);
                sphere.data[0] = centerX; sphere.data[1] = 64.0f; sphere.data[2] = kBaseZ;
                sphere.data[3] = 6.0f;

                Vixen::SVO::RecipeRegistry::RecipeEntry entry{};
                entry.bytecode = { sphere };
                entry.boundCenter = glm::vec3(centerX, 64.0f, kBaseZ);
                entry.boundRadius = 8.0f;

                auto regResult = RegisterProceduralRecipe(recipeId, entry);
                if (regResult != Vixen::SVO::RecipeRegistry::RegisterResult::Ok) {
                    mainLogger->Error("[BuildRenderGraph] VIXEN_RECIPE_HOT_COLD_DEMO: "
                                     "RegisterProceduralRecipe(" + std::to_string(recipeId) +
                                     ") failed, code " + std::to_string(static_cast<int>(regResult)));
                    return;
                }
                for (int k = 0; k < instanceCount; ++k) {
                    Vixen::SVO::BodyInstanceGpu inst{};
                    inst.renderScale = 1.0f;
                    inst.color[0] = tint.x; inst.color[1] = tint.y; inst.color[2] = tint.z;
                    inst.octreeIndex = 0u;
                    inst.providerKind = 1u;  // PROVIDER_PROCEDURAL
                    inst.recipeId = recipeId;
                    // Each instance's own world Z offset so N>1 instances of one recipe are
                    // screen-space-separated rather than perfectly overlapping (still all within
                    // the recipe's shared boundCenter/boundRadius footprint above -- overlapping
                    // bound SPHERES are fine, that's just conservative bucketing coverage).
                    inst.worldPos[0] = 0.0f; inst.worldPos[1] = 0.0f;
                    inst.worldPos[2] = static_cast<float>(k) * 3.0f;
                    hotColdBodies.push_back(inst);
                    ++totalInstances;
                }
            };

            for (int h = 0; h < kHotRecipeCount; ++h) {
                registerAndSeed(nextRecipeId++, kHotColors[h % 3], 40.0f + static_cast<float>(h) * kSpacingX, kInstancesPerHot);
            }
            for (int c = 0; c < kColdRecipeCount; ++c) {
                registerAndSeed(nextRecipeId++, kColdColors[c % 3], 40.0f + static_cast<float>(kHotRecipeCount + c) * kSpacingX, kInstancesPerCold);
            }

            if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                bodyScene->SetInstances(std::move(hotColdBodies));
                mainLogger->Info("[BuildRenderGraph] VIXEN_RECIPE_HOT_COLD_DEMO: seeded " +
                                 std::to_string(totalInstances) + " instances across " +
                                 std::to_string(kHotRecipeCount) + " hot recipes (" +
                                 std::to_string(kInstancesPerHot) + " instances each) + " +
                                 std::to_string(kColdRecipeCount) + " cold recipes (" +
                                 std::to_string(kInstancesPerCold) + " instances each)");
            }
        } else if (const char* diversityDemoEnv = std::getenv("VIXEN_RECIPE_DIVERSITY_STRESS_DEMO")) {
            // Recipe-Diversity-Stress-Scene Inc6 M2 — generalizes M1's hand-authored
            // [ReadParamFloat3, DeclarePosition, <resolve segment>] contract (commit 754442d1)
            // from ONE recipe to N=20-250 genuinely-diverging, spatially-distributed recipes,
            // rendered live. A NEW gated demo (not an extension of VIXEN_PROCEDURAL_UBER_DEMO
            // in place), per the M2 prompt's own "your call, document which" instruction — kept
            // separate so the uber-demo's own N=100/500 measurement baseline (stacked-Z-line,
            // no DeclarePosition) stays byte-identical for anyone still using it as a reference.
            //
            // Recipe-diversity generation: reuses VIXEN_PROCEDURAL_UBER_DEMO's OWN shape/CSG/
            // modifier product generator verbatim (same {sphere/box/torus} x {6 extra CSG ops}
            // x {none/Round/Onion} cycle, same legacy-3 byte-for-byte preservation for i<3) --
            // per the plan doc's own Risk: "do not let recipe-diversity generation collapse
            // into N copies with different literals." The ONLY change from the uber-demo's
            // generator is that every program here is prefixed with the meta segment
            // [ReadParamFloat3(idx=0), DeclarePosition] (M1's proven mechanism) instead of
            // baking each shape's own world-space center into its data[] literals -- position
            // is now genuinely ReadParam-sourced, not a baked constant, exactly the gap M1's
            // prototype closed. Every generated shape instruction below is therefore authored
            // in BODY-LOCAL space (center at the origin), consistent with DeclarePosition's
            // eval-time translation (curPos -= declaredPos) applying BEFORE the resolve segment
            // walks -- see SdfRecipeCodegenGlsl.h's DeclarePosition case / SdfRecipeEval.h's
            // mirror.
            //
            // NOTE on the production wiring gap M1 left open: UberShaderSplice.h's
            // SpliceProceduralRecipesIntoSource (the actual production emitter this demo's
            // recipes go through, via BodyInstanceRayMarch.comp's VIXEN_UBER_RECIPE_SPLICE_MARKER)
            // used to call EmitProceduralFieldFunctionGlsl with emitDeclaredPositionOutParam
            // always false — M1's out-param plumbing was only proven in a standalone test shader
            // (test_recipe_declared_position_render.cpp / test_recipe_glsl_numerical_parity.cpp's
            // own ComposeComputeShaderWithDeclaredPosition), not the real evalRecipeField() switch
            // TraceWorld.glsl actually calls; a DeclarePosition-using recipe would have failed
            // EmitProceduralFieldFunctionGlsl's own assert (Debug) or emitted GLSL referencing an
            // undeclared `declaredPos` identifier (Release). Fixed centrally in
            // UberShaderSplice.h (not here): SpliceProceduralRecipesIntoSource now detects
            // per-recipe whether its bytecode contains DeclarePosition and, only for those
            // recipes, emits with emitDeclaredPositionOutParam=true and supplies a throwaway
            // local (`unusedDeclaredPos`) at evalRecipeField's call site — every other recipe
            // (every pre-Inc6 demo, and any Inc6 recipe that happens not to use DeclarePosition)
            // keeps the original 3-arg call shape byte-identical. This is a correctness fix
            // required for ANY DeclarePosition-using recipe to compile in production, not new
            // machinery scoped to this demo alone.
            constexpr int kMaxDiversityN = 250;  // plan doc's own documented N ceiling for Inc6
            const int requestedDivN = std::atoi(diversityDemoEnv);
            const int diversityN = std::clamp(requestedDivN <= 0 ? 20 : requestedDivN, 1, kMaxDiversityN);

            // --- 192-total-instance ceiling decision (documented per M2 prompt's requirement) ---
            // TraceWorld.glsl's tier-0 march hard-clamps numInstances = clamp(pc.instanceCount, 0,
            // 3*64) = 192 total body instances, across ALL recipes combined. Decision: register
            // ALL `diversityN` distinct recipe PROGRAMS (registration itself has no hard cap --
            // RecipeRegistry is an unbounded std::map, per this plan's own §1 Grounding), but
            // instantiate exactly ONE BodyInstanceGpu per registered recipe, i.e.
            // instantiatedInstanceCount == registeredRecipeCount == diversityN, for the WHOLE
            // documented N=20-250 range. This is option (b) from the M2 prompt ("cap actually-
            // instantiated recipes at 192 and document that as the real ceiling for this scene's
            // N range") specialized further: since 250 <= 192 is FALSE, N is explicitly clamped
            // to min(diversityN, 192) for INSTANTIATION even though up to kMaxDiversityN=250
            // distinct programs could in principle be registered -- chosen over option (a)
            // (register 250, instantiate a rotating subset <=192) because a static subset-
            // selection policy is exactly the kind of extra machinery ("which/how chosen") the
            // prompt warns adds scope for no M2 benefit; M4's later sweep can choose to run at
            // N=192 as its practical top end (already comfortably inside "approaching but below
            // the documented N=500 driver-hang territory" per the plan's own framing) without
            // this milestone inventing a rotation scheme it would just have to redo later.
            // 1:1 registered-recipe:instantiated-instance is also the SIMPLEST correct shape for
            // BOTH ends of the range (per the prompt's own "pick the simplest approach" framing):
            // it needs no per-N branching between "multiple instances per recipe at low N" and
            // "fewer instances per recipe at high N" -- every recipe in this demo is genuinely
            // UNIQUE (that's the entire point of "diversity"), so there is no meaningful sense in
            // which cloning multiple instances of the SAME recipe would add more diversity value;
            // it would only add instance COUNT, which is not what this milestone is measuring.
            const int diversityInstantiated = std::min(diversityN, 192);
            if (diversityInstantiated < diversityN) {
                mainLogger->Info("[BuildRenderGraph] VIXEN_RECIPE_DIVERSITY_STRESS_DEMO: "
                                 "requested N=" + std::to_string(diversityN) + " exceeds the "
                                 "192-instance tier-0 ceiling -- registering " +
                                 std::to_string(diversityN) + " distinct recipe programs but "
                                 "instantiating only the first " + std::to_string(diversityInstantiated));
            }
            mainLogger->Info("[BuildRenderGraph] VIXEN_RECIPE_DIVERSITY_STRESS_DEMO: registering " +
                             std::to_string(diversityN) + " diverse ReadParamFloat3/DeclarePosition "
                             "recipes, instantiating " + std::to_string(diversityInstantiated));

            // --- Spatial distribution: 2D grid in the XZ plane (Y fixed), spacing large enough
            // that neighboring bodies' bound spheres (radius kGridBoundRadius below) don't
            // overlap. ceil(sqrt(N)) columns x however many rows needed, centered on the SAME
            // world-space region the other demos use (around world (64,64,*)) so this demo's
            // camera preset (below) has a well-known scene extent to frame.
            constexpr float kGridSpacing     = 30.0f;  // > 2*kGridBoundRadius, no bound-sphere overlap
            constexpr float kGridBoundRadius = 12.0f;  // matches uber-demo's own non-legacy bound radius
            constexpr float kGridBaseY       = 64.0f;
            // M3: per-instance runtime-mutated shape parameter (recipeParams[3], see the
            // ReadParam/MathSub program suffix above) sweeps in [-kShapeParamSweepMax,
            // +kShapeParamSweepMax] every frame (VulkanGraphApplication::PreTick) -- boundRadius
            // must cover the full swept range, same reasoning as the uber-demo's own ReadParam
            // body (kReadParamDemoBaseRadius + kReadParamDemoSweepMax + margin).
            constexpr float kShapeParamSweepMax = 3.0f;
            // M3: a subset of instances also gets its DECLARED POSITION animated every frame
            // (orbiting around its own grid slot) -- exercising the spatial contract's own
            // stated value proposition (a moving, ReadParam-sourced position) rather than only
            // an unrelated shape parameter, per the M3 prompt's own framing ("the actual point
            // of the milestone"). Every 4th instance (index % 4 == 0) is picked: a meaningful
            // fraction (~25%) without animating literally every body, so the live-run capture
            // can visually distinguish "still" grid neighbors from "orbiting" ones at both N
            // extremes -- documented here, not left implicit.
            constexpr int kAnimatedPositionStride = 4;
            const int gridCols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(diversityN)))));
            const int gridRows = (diversityN + gridCols - 1) / gridCols;
            const float gridWidth  = static_cast<float>(gridCols - 1) * kGridSpacing;
            const float gridDepth  = static_cast<float>(gridRows - 1) * kGridSpacing;
            const float gridOriginX = 64.0f - gridWidth  * 0.5f;
            const float gridOriginZ = 64.0f - gridDepth * 0.5f;

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
            const SdfOpCode kExtraCsgOps[] = {
                SdfOpCode::Union, SdfOpCode::Subtract, SdfOpCode::Intersect,
                SdfOpCode::SmoothIntersect, SdfOpCode::Xor, SdfOpCode::SmoothMax,
            };
            const glm::vec3 kColors[3] = {
                glm::vec3(1.00f, 0.55f, 0.55f),
                glm::vec3(0.55f, 1.00f, 0.55f),
                glm::vec3(0.55f, 0.70f, 1.00f),
            };

            std::vector<Vixen::SVO::BodyInstanceGpu> diversityBodies;
            diversityBodies.reserve(static_cast<size_t>(diversityInstantiated));
            int registeredCount = 0;
            for (int i = 0; i < diversityN; ++i) {
                const uint32_t recipeId = static_cast<uint32_t>(2 + i);
                const int col = i % gridCols;
                const int row = i / gridCols;
                const glm::vec3 declaredPos(
                    gridOriginX + kGridSpacing * static_cast<float>(col),
                    kGridBaseY,
                    gridOriginZ + kGridSpacing * static_cast<float>(row));

                // Resolve segment: SAME shape/CSG/modifier generator as
                // VIXEN_PROCEDURAL_UBER_DEMO, authored in BODY-LOCAL space (center at origin) --
                // DeclarePosition's eval-time translation (curPos -= declaredPos) supplies the
                // world placement, so these shape literals must NOT also bake a world center.
                std::vector<SdfInstruction> resolveProg;
                const int shape = i % 3;
                if (i < 3) {
                    if (shape == 0) {
                        resolveProg = { sphereInstr(glm::vec3(0.0f), 8.0f) };
                    } else if (shape == 1) {
                        resolveProg = { boxInstr(glm::vec3(6.0f, 6.0f, 6.0f)),
                                        sphereInstr(glm::vec3(4.0f, 0.0f, 0.0f), 5.0f),
                                        combineInstr(SdfOpCode::SmoothUnion, 1.5f) };
                    } else {
                        resolveProg = { sphereInstr(glm::vec3(0.0f), 9.0f),
                                        boxInstr(glm::vec3(5.0f, 5.0f, 5.0f)),
                                        roundInstr(1.0f),
                                        combineInstr(SdfOpCode::SmoothSubtract, 1.0f) };
                    }
                } else {
                    const int leaf = (i / 3) % 3;
                    const SdfOpCode csgOp = kExtraCsgOps[i % 6];
                    const int modSel = (i / 6) % 3;
                    const float k = 0.5f + 0.1f * static_cast<float>(i % 7);
                    const float r1 = 6.0f + 0.05f * static_cast<float>(i % 40);
                    const float r2 = 3.0f + 0.03f * static_cast<float>(i % 30);

                    if (leaf == 0) {
                        resolveProg = { sphereInstr(glm::vec3(0.0f), r1),
                                        sphereInstr(glm::vec3(r2 * 0.4f, 0.0f, 0.0f), r2),
                                        combineInstr(csgOp, k) };
                    } else if (leaf == 1) {
                        resolveProg = { boxInstr(glm::vec3(r1, r1 * 0.8f, r1 * 0.6f)),
                                        sphereInstr(glm::vec3(r2 * 0.3f, 0.0f, 0.0f), r2),
                                        combineInstr(csgOp, k) };
                    } else {
                        resolveProg = { torusInstr(r1, r2 * 0.4f),
                                        boxInstr(glm::vec3(r2, r2, r2)),
                                        combineInstr(csgOp, k) };
                    }
                    if (modSel == 1) {
                        resolveProg.push_back(roundInstr(0.3f + 0.02f * static_cast<float>(i % 10)));
                    } else if (modSel == 2) {
                        resolveProg.push_back(onionInstr(0.2f + 0.02f * static_cast<float>(i % 10)));
                    }
                }

                // Meta segment (M1's proven contract): ReadParamFloat3(idx=0) pushes
                // recipeParams[0..2] as a float3, DeclarePosition pops it, assigns the GLSL
                // out-param, and translates the sample point for the resolve segment that
                // follows -- NOT a baked literal, per the M2 prompt's own "this is the actual
                // point of the milestone" framing.
                SdfInstruction readPos{}; readPos.opCode = (uint8_t)SdfOpCode::ReadParamFloat3;
                readPos.paramMask = 1; readPos.data[0] = 0.0f;
                SdfInstruction declarePos{}; declarePos.opCode = (uint8_t)SdfOpCode::DeclarePosition;

                // M3: every instance also gets a genuinely runtime-mutated SHAPE parameter --
                // recipeParams[3] (a scalar, ReadParam idx=3; idx 0 is reserved by the float3
                // declared-position read above, so idx=3 lands past that 3-slot range with no
                // aliasing), subtracted from the resolve segment's own final SDF value via
                // MathSub (a-b, a=resolveSegment result, b=params[3]) -- the exact same shape
                // (sphere - ReadParam) VIXEN_PROCEDURAL_UBER_DEMO's single swept-radius body
                // already proves (Recipe-Parameterization P4 M3 Task 8), just applied uniformly
                // after an arbitrary CSG/modifier resolve segment instead of a bare sphere. This
                // is genuinely consumed (perturbs the rendered iso-surface every frame via
                // VulkanGraphApplication::PreTick's sweep below), not a disconnected value.
                SdfInstruction readShapeParam{}; readShapeParam.opCode = (uint8_t)SdfOpCode::ReadParam;
                readShapeParam.paramMask = 1; readShapeParam.data[0] = 3.0f;
                SdfInstruction subShapeParam{}; subShapeParam.opCode = (uint8_t)SdfOpCode::MathSub;

                std::vector<SdfInstruction> prog;
                prog.reserve(resolveProg.size() + 4);
                prog.push_back(readPos);
                prog.push_back(declarePos);
                prog.insert(prog.end(), resolveProg.begin(), resolveProg.end());
                prog.push_back(readShapeParam);
                prog.push_back(subShapeParam);

                Vixen::SVO::RecipeRegistry::RecipeEntry entry{};
                entry.bytecode = std::move(prog);
                // boundCenter/boundRadius are AUTHORED explicitly (matches this instance's own
                // declared grid position) rather than derived -- ReadParamFloat3/DeclarePosition
                // are not on DeriveConservativeBounds's whitelist (same reasoning
                // VIXEN_PROCEDURAL_UBER_DEMO's own ReadParam demo body comment gives), and per
                // the M2 prompt's own scope boundary this milestone does NOT touch RecipeEntry's
                // boundCenter/boundRadius handling or attempt to resolve AABB coexistence -- it
                // just authors the existing fields with this instance's placement, the same
                // mechanism every prior demo already uses for a non-whitelisted program.
                entry.boundCenter = declaredPos;
                // Margin covers both the M3 shape-param sweep (kShapeParamSweepMax) and, for the
                // animated-position subset, the orbit radius (kOrbitRadius, defined below near
                // the PreTick-mirrored sweep constants) -- boundRadius must bound the instance's
                // full swept footprint, not just its resting grid position.
                entry.boundRadius = kGridBoundRadius + kShapeParamSweepMax + 6.0f;

                auto regResult = RegisterProceduralRecipe(recipeId, entry);
                if (regResult != Vixen::SVO::RecipeRegistry::RegisterResult::Ok) {
                    mainLogger->Error("[BuildRenderGraph] VIXEN_RECIPE_DIVERSITY_STRESS_DEMO: "
                                     "RegisterProceduralRecipe(" + std::to_string(recipeId) +
                                     ") failed, code " + std::to_string(static_cast<int>(regResult)));
                    continue;
                }
                ++registeredCount;

                if (i < diversityInstantiated) {
                    Vixen::SVO::BodyInstanceGpu inst{};
                    inst.worldPos[0] = 0.0f; inst.worldPos[1] = 0.0f; inst.worldPos[2] = 0.0f;  // unused: field samples world p directly
                    inst.renderScale = 1.0f;   // unused by Procedural
                    const glm::vec3& tint = kColors[shape];
                    inst.color[0] = tint.x; inst.color[1] = tint.y; inst.color[2] = tint.z;
                    inst.octreeIndex = 0u;    // unused by Procedural
                    inst.providerKind = 1u;   // PROVIDER_PROCEDURAL
                    inst.recipeId = recipeId;
                    // recipeParams[0..2] = the declared world position, sourced via
                    // ReadParamFloat3(idx=0) inside the recipe above -- M2 supplies this ONCE at
                    // scene setup (a static-per-run spatial layout, per the prompt's own "does
                    // not need to animate every frame" scope note); M3 will later mutate this
                    // same field per-frame via the identical SetInstances() path, not a new one.
                    inst.recipeParams[0] = declaredPos.x;
                    inst.recipeParams[1] = declaredPos.y;
                    inst.recipeParams[2] = declaredPos.z;
                    inst.recipeParams[3] = 0.0f;  // M3: PreTick's per-frame sweep overwrites this
                    diversityBodies.push_back(inst);
                }
            }

            if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                bodyScene->SetInstances(std::move(diversityBodies));
                mainLogger->Info("[BuildRenderGraph] VIXEN_RECIPE_DIVERSITY_STRESS_DEMO: registered " +
                                 std::to_string(registeredCount) + "/" + std::to_string(diversityN) +
                                 " distinct recipe programs, seeded " +
                                 std::to_string(diversityInstantiated) + " body instances on a " +
                                 std::to_string(gridCols) + "x" + std::to_string(gridRows) + " grid "
                                 "(spacing=" + std::to_string(kGridSpacing) + ")");
            }
        } else if (envFlagEnabled("VIXEN_TIER_CHAIN_DEMO")) {
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
        } else if (envFlagEnabled("VIXEN_TIER_EARTH_DEMO")) {
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
                    if (envFlagEnabled("VIXEN_TIER_EARTH_ZOOM_DEMO")) {
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
        } else if (envFlagEnabled("VIXEN_TIER_OBSERVABLE_DEMO")) {
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

                // E7-T2/E8-T1: optional 4th sibling body, NOT tier-linked (no TierRef into
                // it, so it cannot disturb the T0/T1/T2 chain above) -- a large, independently
                // instanced SDF body whose OWN intra-tree mip ladder gives the plateau fixture
                // levels a small R=0.1/kBrickDepth=3 tree structurally cannot (verified empirically:
                // a 720-frame plateau capture against T0/T1/T2 alone produced level=0 at every
                // hold including the 120wu far ceiling -- perf/e7-t2-multirung-fixture-design.md
                // Sec.2's own pre-authorized contingency). Gated separately from
                // VIXEN_TIER_OBSERVABLE_PLATEAU_DEMO so a plain VIXEN_TIER_OBSERVABLE_DEMO boot
                // (existing gates, existing goldens) is byte-for-byte unaffected.
                const bool obsAddFarSibling = std::getenv("VIXEN_TIER_OBSERVABLE_FAR_SIBLING") != nullptr;
                Vixen::SVO::SdfBodyOctree obsFarBody;
                Vixen::SVO::SerializedOctree obsFarSer;
                if (obsAddFarSibling) {
                    // Large radius + coarse voxel band (kBand=8.0) relative to kN=32 so the
                    // resulting octree is genuinely deep (unlike the tiny R=0.1 T0/T1/T2
                    // bodies) -- gives mipPolicyLevel's coarse->fine ladder walk room to
                    // resolve level>0 at distance. Centered far from (64,64,64) so it never
                    // occludes/interferes with the T0/T1/T2 cluster's own on-axis framing.
                    // kFarN sweep history (each value's MEASURED MipReadBytes levels, the
                    // pre-registered instrument, not the looser PolicyLevelHistogram):
                    //   32  -> {0,1}      128 -> {0,2,3}      256 -> {0,3,4}
                    //   384 (band=8)  -> {0} (regression)     384 (band=16) -> {0} (regression)
                    // NOT a monotonic lever: level count/identity shifts non-linearly with
                    // kFarN, and does not simply accumulate toward the full union. 256 is
                    // the best result found (3 distinct levels: {0,3,4}) after a 4-value
                    // sweep; reverted to it rather than continuing an unprincipled parameter
                    // search (orchestrator flag: an 8th signature-adjudication rebuild pass
                    // is judgment-call territory, not "check the arithmetic").
                    // E9-T1: kN/renderScale overridable for the predict-then-measure sweep
                    // (perf/e9-t1-level-targeting-report.md) -- unset envs reproduce the
                    // literal defaults below byte-for-byte (same std::getenv+strtof/strtol
                    // pattern as VIXEN_TIER_OBSERVABLE_DISTANCE, BuildRenderGraph.cpp:2619).
                    int   kFarN      = 256;
                    if (const char* farNEnv = std::getenv("VIXEN_TIER_OBSERVABLE_FAR_N")) {
                        long parsed = std::strtol(farNEnv, nullptr, 10);
                        if (parsed > 0) kFarN = static_cast<int>(parsed);
                    }
                    float kFarRadius = 12.0f;
                    if (const char* farRadiusEnv = std::getenv("VIXEN_TIER_OBSERVABLE_FAR_RADIUS")) {
                        float parsed = std::strtof(farRadiusEnv, nullptr);
                        if (parsed > 0.0f) kFarRadius = parsed;
                    }
                    constexpr float kFarBand   = 8.0f;
                    const glm::vec3 kFarCenter(16.0f, 16.0f, 16.0f);
                    Vixen::SVO::RecipeParams farRp{};
                    farRp.radius = kFarRadius;
                    Vixen::SVO::SdfBakeResult farBaked = Vixen::SVO::BakeRecipeToSdfWorld(
                        Vixen::SVO::RECIPE_SPHERE, kFarCenter, farRp, kFarN, kFarBand);
                    obsFarBody = Vixen::SVO::BuildSdfBodyOctree(farBaked, kBrickDepth);
                    obsFarSer = Vixen::SVO::SerializeSdf(obsFarBody);
                    if (const Vixen::SVO::Octree* octFar = obsFarBody.octree->getOctree()) {
                        Vixen::SVO::BakeAndAttachMipPool(*octFar, obsFarSer);
                    }
                    mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_OBSERVABLE_FAR_SIBLING: "
                                      "baked large non-tiered sibling body (radius=" + std::to_string(kFarRadius)
                                      + ", kN=" + std::to_string(kFarN) + ", band=" + std::to_string(kFarBand) + ")");
                }

                // E8-T1 second attempt: body 3 (above) only ever resolved to levels {3,4} at the
                // holds where its far-field gate engaged at all (hop1_19.89wu through far_120wu;
                // the two nearest holds never reach it -- it sits ~83wu from the T0/T1/T2
                // cluster). A SECOND, smaller/shallower/CLOSER sibling (smaller kN -> shallower
                // farFieldDescentDepth ceiling per SVOTypes.glsl, placed nearer the cluster so
                // the currently-uncovered near/mid holds can reach it) targets the missing
                // low-level band (1-2) directly rather than re-sweeping kFarN on the one body.
                const bool obsAddNearSibling = std::getenv("VIXEN_TIER_OBSERVABLE_NEAR_SIBLING") != nullptr;
                Vixen::SVO::SdfBodyOctree obsNearBody;
                Vixen::SVO::SerializedOctree obsNearSer;
                if (obsAddNearSibling) {
                    constexpr int   kNearN      = 48;   // bpa=6, ceilDepth=findMSB(6)=2 -- shallow on purpose
                    constexpr float kNearRadius = 5.0f;
                    constexpr float kNearBand   = 4.0f;
                    // Placed along the SAME camera axis as T0/T1/T2 but offset so it doesn't
                    // occlude the cluster, at a distance the near/mid holds (10-40wu) can reach.
                    const glm::vec3 kNearCenter(40.0f, 40.0f, 40.0f);
                    Vixen::SVO::RecipeParams nearRp{};
                    nearRp.radius = kNearRadius;
                    Vixen::SVO::SdfBakeResult nearBaked = Vixen::SVO::BakeRecipeToSdfWorld(
                        Vixen::SVO::RECIPE_SPHERE, kNearCenter, nearRp, kNearN, kNearBand);
                    obsNearBody = Vixen::SVO::BuildSdfBodyOctree(nearBaked, kBrickDepth);
                    obsNearSer = Vixen::SVO::SerializeSdf(obsNearBody);
                    if (const Vixen::SVO::Octree* octNear = obsNearBody.octree->getOctree()) {
                        Vixen::SVO::BakeAndAttachMipPool(*octNear, obsNearSer);
                    }
                    mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_OBSERVABLE_NEAR_SIBLING: "
                                      "baked small non-tiered sibling body (radius=" + std::to_string(kNearRadius)
                                      + ", kN=" + std::to_string(kNearN) + ", band=" + std::to_string(kNearBand) + ")");
                }
                // E10-T1: optional 5th sibling body, the "orbital structure" fixture
                // (perf/e10-t1-orbital-fixture-report.md) -- a coarse, recognizable voxel
                // structure (slab base + tower, hand-carved SDF via BakeSdfWorld's EvalFn,
                // NOT RECIPE_SPHERE) sized/placed per the E9-T1 recipe
                // (perf/e9-t1-level-targeting-report.md Sec.6/8.2) so its far-field samples
                // land on target level L=depth=3 at the far orbital hold (>=120wu). Gated
                // separately from FAR_SIBLING/NEAR_SIBLING so a plain VIXEN_TIER_OBSERVABLE_DEMO
                // boot (existing gates, existing goldens) stays byte-for-byte unaffected.
                const bool obsAddStructure = std::getenv("VIXEN_TIER_OBSERVABLE_STRUCTURE") != nullptr;
                Vixen::SVO::SdfBodyOctree obsStructBody;
                Vixen::SVO::SerializedOctree obsStructSer;
                if (obsAddStructure) {
                    // Recipe arithmetic (E9-T1 Sec.6/8.2, occFrac well above floor -> top
                    // level reaches depth itself):
                    //   kN=64, brickSide=8 -> bpa=ceil(64/8)=8, depth=findMSB(8)=3.
                    //   Structure solid volume (slab 20x20x6 + tower 8x8x24, grid cells) =
                    //   2400+1536=3936 -> occFrac=3936/64^3=0.01501 >> 0.0005 floor.
                    //   -> predicted top level L=depth=3, plus 1-2 rungs below (D-series
                    //   pattern): predicted set {1,2,3}.
                    constexpr int   kStructN         = 64;
                    constexpr float kStructBand      = 8.0f;
                    const glm::vec3 kStructCenter(32.0f, 32.0f, 32.0f);
                    // Slab base: 20x20x6 cells centered in X/Z, sitting on the grid floor
                    // in Y. Tower: 8x8x24 cells centered on the slab, rising well above
                    // it -- together a recognizable silhouette (a compact building cluster),
                    // not a sphere fill, so the far-field render reads as a STRUCTURE.
                    const glm::vec3 kSlabHalf(10.0f, 3.0f, 10.0f);
                    const glm::vec3 kSlabCenter = kStructCenter + glm::vec3(0.0f, -kStructN * 0.5f + 3.0f, 0.0f);
                    const glm::vec3 kTowerHalf(4.0f, 12.0f, 4.0f);
                    const glm::vec3 kTowerCenter = kSlabCenter + glm::vec3(0.0f, kSlabHalf.y + kTowerHalf.y, 0.0f);
                    auto boxSdf = [](const glm::vec3& p, const glm::vec3& c, const glm::vec3& half) {
                        const glm::vec3 q = glm::abs(p - c) - half;
                        const glm::vec3 qMax = glm::max(q, glm::vec3(0.0f));
                        return glm::length(qMax) + std::min(std::max(q.x, std::max(q.y, q.z)), 0.0f);
                    };
                    auto structEval = [&](const glm::vec3& p) {
                        return std::min(boxSdf(p, kSlabCenter, kSlabHalf), boxSdf(p, kTowerCenter, kTowerHalf));
                    };
                    // E14-T1: derived emission pattern for the orbital structure.  This is
                    // deliberately sparse: three vertical window bands on the tower's front
                    // and rear faces, plus a lit strip around the slab's top edge.  The field is
                    // authored into the existing per-voxel emission channel; no light source or
                    // alternate shading mechanism is introduced.
                    // E15-T1: the carving itself is now gated on
                    // VIXEN_TIER_OBSERVABLE_STRUCTURE_EMISSIVE (E14/E15 briefs both assumed this
                    // gate already existed; the staged E14 diff left the carving unconditional
                    // under VIXEN_TIER_OBSERVABLE_STRUCTURE alone -- so both "on" and "off"
                    // captures baked the identical emissive octree and produced a vacuous,
                    // byte-identical A/B). With the gate unset, every voxel's emission value is
                    // 0.0 -- same value BakeSdfWorld's own NoEmission default would produce --
                    // so the unset path stays exactly the pre-E14 structure bake.
                    const bool obsStructureEmissive =
                        std::getenv("VIXEN_TIER_OBSERVABLE_STRUCTURE_EMISSIVE") != nullptr;
                    auto structEmission = [&](const glm::vec3& p) -> float {
                        if (!obsStructureEmissive) return 0.0f;
                        const glm::vec3 towerLocal = glm::abs(p - kTowerCenter);
                        const glm::vec3 slabLocal = glm::abs(p - kSlabCenter);
                        // E15-T1: widened from Z-only (front/rear) to all 4 tower side faces --
                        // the ~120wu far orbital hold's camera angle at the capture frame this
                        // task's protocol uses (frame 700) views the tower's X-normal side face,
                        // which the E14 diff's Z-only band never touched, so no emissive voxel
                        // was ever in view regardless of intensity (verified: a 62.5x intensity
                        // bump, 8.0f->500.0f, produced a byte-identical capture -- the ray simply
                        // never hit an emissive voxel, not a dilution/quantization shortfall).
                        const bool towerWindowBand =
                            towerLocal.y <= kTowerHalf.y &&
                            ((towerLocal.x <= kTowerHalf.x && towerLocal.z >= kTowerHalf.z - 1.0f) ||
                             (towerLocal.z <= kTowerHalf.z && towerLocal.x >= kTowerHalf.x - 1.0f)) &&
                            (std::fmod(std::floor(towerLocal.y), 6.0f) < 2.0f);
                        const bool slabEdgeStrip =
                            slabLocal.y >= kSlabHalf.y - 1.0f &&
                            slabLocal.x <= kSlabHalf.x && slabLocal.z <= kSlabHalf.z &&
                            (slabLocal.x >= kSlabHalf.x - 1.0f || slabLocal.z >= kSlabHalf.z - 1.0f);
                        // E15-T1: bumped from the E14 diff's original 8.0f -- at 120wu far-field
                        // hold, SEM_EMISSION's mean-filtered mip propagation (FilterMipMean,
                        // MipSample.h) dilutes the sparse window-band pattern (~1/3 of the
                        // tower's occupied surface voxels per band cycle) across mostly-
                        // non-emissive neighbors at every mip level, and the result is then
                        // scaled by the structure instance's own dim material tint
                        // (obsStructInst.color below) before reaching the pixel -- 8.0f's
                        // diluted+scaled contribution measured byte-identical to emissive-off
                        // in an 8-bit capture (verified: both legs' hud_capture_700.png md5
                        // 2828123a4ebbd752e24f5a89345d6177). 500.0f is chosen to clear 8-bit
                        // quantization even after that dilution/scaling chain, additively
                        // (outColor += bestColor * emission, SpatialReuseShade.comp) ahead of
                        // whatever tonemap/clamp the output chain applies.
                        return (towerWindowBand || slabEdgeStrip) ? 500.0f : 0.0f;
                    };
                    Vixen::SVO::SdfBakeResult structBaked = Vixen::SVO::BakeSdfWorld(
                        structEval, kStructCenter, kStructN, kStructBand, kBrickDepth,
                        structEmission);
                    obsStructBody = Vixen::SVO::BuildSdfBodyOctree(structBaked, kBrickDepth);
                    obsStructSer = Vixen::SVO::SerializeSdf(obsStructBody);
                    if (const Vixen::SVO::Octree* octStruct = obsStructBody.octree->getOctree()) {
                        Vixen::SVO::BakeAndAttachMipPool(*octStruct, obsStructSer);
                    }
                    mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_OBSERVABLE_STRUCTURE: "
                                      "baked slab+tower structure body (kN=" + std::to_string(kStructN)
                                      + ", band=" + std::to_string(kStructBand) + ")");
                }

                // Build a DENSE octree list so each sibling's octreeIndex is always its real
                // position regardless of which subset of {far,near,structure} siblings is
                // enabled -- avoids an empty/default SerializedOctree ever being concatenated
                // as a real (zero-node) tree.
                std::vector<Vixen::SVO::SerializedOctree*> obsOctList = {&obsT0Ser, &obsT1Ser, &obsT2Ser};
                int obsFarOctreeIndex = -1, obsNearOctreeIndex = -1, obsStructOctreeIndex = -1;
                if (obsAddFarSibling)  { obsFarOctreeIndex  = static_cast<int>(obsOctList.size()); obsOctList.push_back(&obsFarSer); }
                if (obsAddNearSibling) { obsNearOctreeIndex = static_cast<int>(obsOctList.size()); obsOctList.push_back(&obsNearSer); }
                if (obsAddStructure)   { obsStructOctreeIndex = static_cast<int>(obsOctList.size()); obsOctList.push_back(&obsStructSer); }
                const int obsTreeCount = static_cast<int>(obsOctList.size());

                Vixen::SVO::ConcatenatedOctrees obsCat;
                obsCat.count = obsTreeCount;
                obsCat.configs.resize(obsTreeCount);
                obsCat.nodeCounts.resize(obsTreeCount);
                obsCat.brickCounts.resize(obsTreeCount);
                obsCat.tierRefCounts.resize(obsTreeCount);

                Vixen::SVO::SerializedOctree** obsOcts = obsOctList.data();
                uint32_t obsNodeBase = 0, obsBrickBase = 0, obsPoolBase = 0, obsTierRefBase = 0, obsMipPoolBase = 0;
                for (int k = 0; k < obsTreeCount; ++k) {
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

                    std::vector<Vixen::SVO::BodyInstanceGpu> obsInstances{obsInst};
                    if (obsAddFarSibling) {
                        // Same orbit center (64,64,64) as the T0/T1/T2 cluster, so the
                        // existing camera sweep samples it too, but a much larger
                        // renderScale -- the far-ceiling hold (120wu) sees a genuinely
                        // production-shaped body instead of the 1.0wu T0/T1/T2 cluster's
                        // own already-tiny world footprint.
                        float kFarRenderScale = 4.8f;
                        if (const char* farRsEnv = std::getenv("VIXEN_TIER_OBSERVABLE_FAR_RENDER_SCALE")) {
                            float parsed = std::strtof(farRsEnv, nullptr);
                            if (parsed > 0.0f) kFarRenderScale = parsed;
                        }
                        const float kFarHalf = 5.0f * kFarRenderScale;
                        Vixen::SVO::BodyInstanceGpu obsFarInst{};
                        obsFarInst.worldPos[0]  = 64.0f - kFarHalf;
                        obsFarInst.worldPos[1]  = 64.0f - kFarHalf;
                        obsFarInst.worldPos[2]  = 64.0f - kFarHalf;
                        obsFarInst.renderScale  = kFarRenderScale;
                        obsFarInst.color[0]     = 1.0f;
                        obsFarInst.color[1]     = 0.5f;
                        obsFarInst.color[2]     = 0.0f;
                        obsFarInst.octreeIndex  = static_cast<uint32_t>(obsFarOctreeIndex);
                        obsFarInst.providerKind = 0u;
                        obsFarInst.recipeId     = 0u;
                        obsInstances.push_back(obsFarInst);
                    }
                    if (obsAddNearSibling) {
                        // Closer than the far sibling, on the SAME orbit-center axis, so the
                        // currently-uncovered near/mid holds (below_hop1_10wu, hop1_19.89wu,
                        // mid_40wu) can reach its far-field gate.
                        constexpr float kNearInstRenderScale = 2.0f;
                        constexpr float kNearInstHalf = 5.0f * kNearInstRenderScale;
                        Vixen::SVO::BodyInstanceGpu obsNearInst{};
                        obsNearInst.worldPos[0]  = 64.0f - kNearInstHalf;
                        obsNearInst.worldPos[1]  = 64.0f - kNearInstHalf;
                        obsNearInst.worldPos[2]  = 64.0f - kNearInstHalf;
                        obsNearInst.renderScale  = kNearInstRenderScale;
                        obsNearInst.color[0]     = 0.5f;
                        obsNearInst.color[1]     = 0.0f;
                        obsNearInst.color[2]     = 1.0f;
                        obsNearInst.octreeIndex  = static_cast<uint32_t>(obsNearOctreeIndex);
                        obsNearInst.providerKind = 0u;
                        obsNearInst.recipeId     = 0u;
                        obsInstances.push_back(obsNearInst);
                    }
                    if (obsAddStructure) {
                        // E10-T1: placed at the SAME orbit center (64,64,64) as every other
                        // sibling. Per ShellOctreeGpu.h:281 (world = worldPos + (local *
                        // kWorldGridSize) * renderScale), the octree's own local space is a
                        // FIXED [0,kWorldGridSize=10) unit cube regardless of kN -- so the
                        // half-extent is always 5.0*renderScale (matching every other sibling
                        // body here), NOT a function of kStructN. renderScale chosen so
                        // brickWorldSize*2^L (L=3, the predicted top level) is comfortably
                        // inside the far hold's footprint (>=120wu) -- same renderScale as the
                        // far sibling (4.8), reusing the orbital-scale distance already proven
                        // to reach level>=2 in E8-T1/E9-T1.
                        constexpr float kStructRenderScale = 4.8f;
                        constexpr float kStructHalf = 5.0f * kStructRenderScale;
                        Vixen::SVO::BodyInstanceGpu obsStructInst{};
                        obsStructInst.worldPos[0]  = 64.0f - kStructHalf;
                        obsStructInst.worldPos[1]  = 64.0f - kStructHalf;
                        obsStructInst.worldPos[2]  = 64.0f - kStructHalf;
                        obsStructInst.renderScale  = kStructRenderScale;
                        obsStructInst.color[0]     = 0.0f;
                        obsStructInst.color[1]     = 1.0f;
                        obsStructInst.color[2]     = 1.0f;
                        obsStructInst.octreeIndex  = static_cast<uint32_t>(obsStructOctreeIndex);
                        obsStructInst.providerKind = 0u;
                        obsStructInst.recipeId     = 0u;
                        obsInstances.push_back(obsStructInst);
                    }
                    bodyScene->SetInstances(std::move(obsInstances));
                    mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_OBSERVABLE_DEMO: T0 leaf ("
                                  + std::to_string(obsT0MarkDescIdx) + "," + std::to_string(obsT0MarkOctant)
                                  + ") -> T1 octree1 (childScale=0.25); T1 leaf (" + std::to_string(obsT1MarkDescIdx) + ","
                                  + std::to_string(obsT1MarkOctant) + ") -> T2 octree2 (childScale=0.25); "
                                  "predicted hop0~=79.58wu, hop1~=19.89wu, both <1.1deg off-axis");
                }
            } else {
                mainLogger->Error("[BuildRenderGraph] VIXEN_TIER_OBSERVABLE_DEMO: no camera-facing leaf found in T0 or T1 — demo scene not built");
            }
        } else if (envFlagEnabled("VIXEN_RESTIR_GATE_DEMO")) {
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
        } else if (envFlagEnabled("VIXEN_TIER_M8_EARTH_DEMO")) {
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
        } else if (envFlagEnabled("VIXEN_SHADOW_DEMO")) {
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
        } else if (envFlagEnabled("VIXEN_DDGI_CORNELL_BAKED_DEMO")) {
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
            // Baked-Perf M1 Task 1.2 (grid-unit contract fix): the shared shader march
            // (StoredSdf.glsl marchBrickSdf) sphere-traces in GRID-VOXEL arc-length,
            // stepping s += d*(1/sqrt(3)) and treating the stored Density AS a grid-voxel
            // distance (its gradient w.r.t. the grid must be 1). evalRecipe(world) returns
            // a TRUE WORLD-space distance -- its gradient w.r.t. the grid coordinate is
            // 1/subdiv (world = bodyWorldCenter + (pRaw-n/2)/subdiv), so storing it
            // unscaled understeps the march by subdiv x (4x for kWallSubdiv=4 walls) and
            // bakes the occupancy band subdiv x too thick (Task 1.3 fixes the band
            // separately). Multiplying by subdiv here converts the stored Density to a
            // grid-unit distance, matching the grid-unit convention SdfRecipes.h's
            // evalSdf/BakeRecipeToSdfWorld (the OTHER bake path, and
            // test_stored_sdf_march_mirror) already use -- so the shared march is
            // correct for ALL bodies with NO shader change. subdiv=1 bodies (light/
            // sphere/box, kSmallSubdiv) are byte-identical (*1 is a no-op).
            //
            // Proven ~3.8x FPS win on fix/baked-sdf-perf-rootfix (see
            // Baked-SDF-Perf-Rootfix-2026-07.md); that attempt alone made bodies 5/6/7
            // vanish from the [CornellDiag] instIdx map because of the brickLookupBase
            // mis-addressing bug (Task 1.1, landed first and validated separately) --
            // NOT because of this multiply. Do not revert this without re-checking 1.1.
            auto makeWorldSpaceEval = [](const std::vector<SdfInstruction>& prog, glm::vec3 bodyWorldCenter, int n, int subdiv) {
                return [&prog, bodyWorldCenter, n, subdiv](const glm::vec3& pRaw) {
                    const glm::vec3 world = bodyWorldCenter + (pRaw - glm::vec3(static_cast<float>(n) * 0.5f)) / static_cast<float>(subdiv);
                    return Vixen::SVO::Recipe::evalRecipe(prog.data(), static_cast<uint32_t>(prog.size()), world) * static_cast<float>(subdiv);
                };
            };
            // Bake each body with a FLAT WHITE per-voxel color (Cornell M3 round 7 fix):
            // BakeSdfWorld's DEFAULT per-voxel color is a debug rainbow (SdfBake.h
            // DefaultBandColor), which the STORED shading path multiplies by inst.color
            // (TraceWorld.glsl: bestColor = hitColor*inst.color) -- rendering every wall as
            // garish rainbow*tint blotches instead of a flat per-wall color. Baking WHITE
            // (1,1,1) makes hitColor*inst.color == inst.color, i.e. the STORED path now
            // yields EXACTLY the same flat per-wall tint the virtual/PROCEDURAL variant
            // already shows (bestColor = inst.color; that path has no baked voxel channel).
            // White (not the per-body tint) is deliberate: the tint is already applied ONCE
            // via inst.color, so baking the tint too would square it (darken). Passing the
            // color as the ColorFn (rather than overwriting the serialized channelPool
            // post-bake) fixes BOTH the brick channelPool and the mip pool consistently, and
            // touches the shared BakeSdfWorld only via its additive, default-preserving
            // ColorFn parameter (the rainbow default is still what every other caller/test gets).
            // SDF Bake Box-Tight Region M2: worldHalfExtent (optional, sentinel (0,0,0) = full
            // cube, matching SdfBake.h's own bakeRegion(0,0,0)-means-default convention) is the
            // body's TRUE per-axis half-extent in WORLD units (thin on a wall's thickness axis,
            // wide on its span axes). makeWorldSpaceEval maps grid coord n/2 -> the body's true
            // world center (see that lambda's own header comment), so the body's occupied region
            // is centered on grid n/2, not on the bakeRegion window's own center -- bakeRegion
            // itself is an ORIGIN-ANCHORED [0,region) window (SdfBake.h's contract), so the
            // region upper bound must cover n/2 + (worldHalfExtent+kBand)*subdiv on every axis
            // (there is no room to shrink the LOW side of an origin-anchored window when the
            // occupied span is centered at n/2, only the high side beyond what's occupied).
            // Rounded up to the next brick boundary (brickSide=8) since bakeRegion snaps to
            // whole bricks anyway (SdfBake.h's own occupancy/active-brick arrays are brick-
            // granular) — a non-brick-aligned bound buys nothing and just obscures the real
            // brick count in a diagnostic dump.
            // Baked-Perf M7 Task 7.1: serializes calls into Gaia's process-wide
            // ChunkAllocator singleton (see the header comment at the parallel-bake launch
            // site below for the full root-cause writeup) -- a function-local static so every
            // caller of this render-graph-build function (only ever invoked once per process
            // launch, but a static is the standard "exactly one, lazily initialized, no
            // separate declaration site to keep in sync" idiom for this) shares the same lock.
            static std::mutex g_gaiaChunkAllocatorMutex;

            auto bakeWorldSpaceBody = [&](const std::vector<SdfInstruction>& prog, glm::vec3 bodyWorldCenter, int n, int subdiv,
                                          glm::vec3 worldHalfExtent = glm::vec3(0.0f)) {
                glm::ivec3 bakeRegion(0);
                if (worldHalfExtent != glm::vec3(0.0f)) {
                    constexpr int kBrickSide = 8;
                    for (int axis = 0; axis < 3; ++axis) {
                        const float hi = static_cast<float>(n) * 0.5f +
                                         (worldHalfExtent[axis] + kBand) * static_cast<float>(subdiv);
                        const int hiRoundedUp = ((static_cast<int>(std::ceil(hi)) + kBrickSide - 1) / kBrickSide) * kBrickSide;
                        bakeRegion[axis] = std::min(hiRoundedUp, n);
                    }
                }
                std::lock_guard<std::mutex> gaiaLock(g_gaiaChunkAllocatorMutex);
                Vixen::SVO::SdfBakeResult baked = Vixen::SVO::BakeSdfWorld(
                    makeWorldSpaceEval(prog, bodyWorldCenter, n, subdiv), bodyWorldCenter, n, kBand,
                    3, Vixen::SVO::NoEmission,
                    [](const glm::vec3&) { return glm::vec3(1.0f); },
                    bakeRegion);
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
            // non-blocking cosmetic concern).
            //
            // Baked-Perf M5 Task 5.5 (small-body bake resolution bump): kSmallN was 16 (pow2)
            // -- at subdiv=1 that is ~1 voxel/world-unit across the light/sphere/box bodies'
            // full span, the COARSEST normals in the scene (every downstream light term is
            // NdotL-gated -- Brdf.glsl / ProbeUpdate.comp -- so a coarse/faceted normal field
            // reads as visibly near-black vs the virtual/procedural path's exact analytic
            // normal). Doubled to 32 (still pow2, no BuildSdfBodyOctree round-up surprise --
            // see that function's own pow2-round-up comment) for ~2 voxels/world-unit: no
            // box-tight bakeRegion is passed for these 3 bodies (sphere/box/light bake their
            // FULL grid, unlike the walls), so n alone determines sampling resolution here --
            // a clean resolution bump with no other side effect on bake geometry/placement.
            // kSmallSubdiv stays 1: unlike the walls (which need subdiv to resolve a THIN
            // slab-shaped body), these bodies are roughly cube-shaped at a scale n already
            // resolves reasonably -- doubling n directly (rather than adding subdiv) is the
            // simpler lever and keeps bodyWorldPos/bodyRenderScale's grid<->world mapping
            // unchanged in form (just a different n).
            constexpr int kWallSubdiv = 4;
            constexpr int kWallN   = 128;  // power of two (was 112 -- see the pow2 note above)
            constexpr int kSmallN  = 32;   // was 16 -- Task 5.5 normals-resolution bump
            constexpr int kSmallSubdiv = 1;

            std::vector<CornellWorldSpaceBody> bodies = BuildCornellWorldSpaceBodies();
            // bodies[]: leftWall, rightWall, backWall, floor, ceiling, light, sphereObj, boxObj (fixed order, see BuildCornellWorldSpaceBodies)

            // SDF Bake Box-Tight Region M2: per-wall TRUE world-space half-extents, mirroring
            // BuildCornellWorldSpaceBodies's own CornellWorldBoxAt(...) half-extent args exactly
            // (kWallThickness=1, kWallSpan=kBoxHalfExtent+kWallThickness=11, kZWideHalfExtent=
            // kBoxHalfExtent+kWallThickness*0.5=10.5) -- each wall is thin on exactly ONE axis,
            // wide on the other two, so passing these into bakeWorldSpaceBody's worldHalfExtent
            // shrinks the bake region on the thin axis only (the wide axes are already close to
            // the full kWallN cube's own span and see little/no reduction -- see this milestone's
            // Progress Log entry for the measured before/after brick counts).
            const float kWallThicknessHalf = 1.0f;              // CornellBoxSceneDefinition.h::kWallThickness
            const float kWallSpanHalf      = 11.0f;             // kBoxHalfExtent(10) + kWallThickness(1)
            const float kZWideHalf         = 10.5f;              // kBoxHalfExtent(10) + kWallThickness*0.5

            // Baked-Perf M7 Task 7.4: bake-artifact disk cache. Design note:
            // Vixen-Docs/01-Architecture/Baked-Perf-Fix-Pipeline-Plan-2026-07.md's
            // "Task 7.4 design note" section (written before this code). Key = FNV-1a
            // hash over every input that changes the bake OUTPUT: each body's SdfInstruction
            // program bytes + worldCenter/n/subdiv/worldHalfExtent, plus the shared kBand and
            // the light body's own emission intensity -- exactly the parameter list
            // bakeWorldSpaceBody/the light's direct BakeSdfWorld call take. A HIT skips the
            // entire 8-body bake AND the concat/mip-bake/light-tree-cut pass below (a warm
            // boot should reach file-load speed, not just skip the GaiaVoxelWorld bake).
            const glm::vec3 kNoHalfExtent(0.0f);
            Vixen::SVO::BakeArtifactKeyBuilder keyBuilder;
            auto addBodyKeyInputs = [&](const CornellWorldSpaceBody& b, int n, int subdiv, glm::vec3 halfExtent) {
                keyBuilder.addProgram(std::span<const SdfInstruction>(b.prog.data(), b.prog.size()));
                keyBuilder.addVec3(b.worldCenter);
                keyBuilder.addI32(n);
                keyBuilder.addI32(subdiv);
                keyBuilder.addVec3(halfExtent);
            };
            addBodyKeyInputs(bodies[0], kWallN, kWallSubdiv, glm::vec3(kWallThicknessHalf, kWallSpanHalf, kZWideHalf));
            addBodyKeyInputs(bodies[1], kWallN, kWallSubdiv, glm::vec3(kWallThicknessHalf, kWallSpanHalf, kZWideHalf));
            addBodyKeyInputs(bodies[2], kWallN, kWallSubdiv, glm::vec3(kWallSpanHalf, kWallSpanHalf, kWallThicknessHalf));
            addBodyKeyInputs(bodies[3], kWallN, kWallSubdiv, glm::vec3(kWallSpanHalf, kWallThicknessHalf, kZWideHalf));
            addBodyKeyInputs(bodies[4], kWallN, kWallSubdiv, glm::vec3(kWallSpanHalf, kWallThicknessHalf, kZWideHalf));
            addBodyKeyInputs(bodies[5], kSmallN, kSmallSubdiv, kNoHalfExtent);  // light
            addBodyKeyInputs(bodies[6], kSmallN, kSmallSubdiv, kNoHalfExtent);  // sphereObj
            addBodyKeyInputs(bodies[7], kSmallN, kSmallSubdiv, kNoHalfExtent);  // boxObj
            keyBuilder.addFloat(kBand);
            keyBuilder.addFloat(kLightEmissionIntensity);
            const std::string bakeArtifactKey = keyBuilder.hexKey();

            std::optional<Vixen::SVO::BakeArtifactBundle> cachedBundle =
                Vixen::SVO::LoadBakeArtifact(bakeArtifactKey);
            if (cachedBundle.has_value()) {
                mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_BAKED_DEMO: bake-artifact "
                                  "cache HIT (key=" + bakeArtifactKey + ") -- skipping bake+concat+"
                                  "light-tree-cut, loading from disk");
            } else {
                mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_BAKED_DEMO: bake-artifact "
                                  "cache MISS (key=" + bakeArtifactKey + ") -- baking fresh");
            }

            // kLightWorldPos/kLightRenderScale are pure functions of bodies[5]/kSmallN/
            // kSmallSubdiv -- needed by the instance-seeding tail regardless of cache hit/miss,
            // so computed once up front rather than duplicated in both branches.
            const glm::vec3 kLightWorldCenter = bodies[5].worldCenter;
            const glm::vec3 kLightWorldPos = bodyWorldPos(kLightWorldCenter, kSmallN, kSmallSubdiv);
            const float kLightRenderScale = bodyRenderScale(kSmallN, kSmallSubdiv);

            // Converged outputs: populated either from the cache (HIT, no bake at all) or
            // from a fresh bake+concat+light-tree-cut (MISS, then stored to the cache below
            // for the NEXT boot). Everything from the occupancy stats through SetRecipePool/
            // SetLightTreeCut reads only these two (plus bakeOk, which gates that whole tail
            // exactly like the pre-M7 code's own "lightOct == nullptr -> skip everything"
            // control flow did), so the hit/miss branches converge before any GPU-facing
            // state is touched.
            Vixen::SVO::ConcatenatedOctrees cat;
            std::vector<Vixen::SVO::LightTreeNode> worldCut;
            bool bakeOk = true;

            if (cachedBundle.has_value()) {
                cat = std::move(cachedBundle->cat);
                worldCut = std::move(cachedBundle->lightTreeCut);
            } else {

            // Baked-Perf M7 Task 7.1: the 8 bodies bake independently -- each owns its own
            // GaiaVoxelWorld (its own gaia::ecs::World instance, SdfBake.h's BakeSdfWorld
            // constructs a fresh one per call) and its own AttributeRegistry/LaineKarrasOctree
            // (SdfBake.h's SdfBodyOctree bundle). A FIRST thread-safety pass (component IDs,
            // Gaia's opt-in ThreadPool, Logger globals) found no shared mutable state and
            // looked safe -- but the FIRST unlocked live run of this parallelization crashed
            // every single time with "Assertion failed: (m_nextFreeBlock < m_blockCnt &&
            // \"Block allocator recycle list broken!\")" (gaia.h ~18958). Root cause: every
            // World's entity/chunk creation (BakeSdfWorld's createVoxelsBatch) and octree
            // rebuild() ultimately allocate/free ECS chunks through
            // gaia::ecs::ChunkAllocator::get() -- a process-WIDE singleton (gaia.h ~18873,
            // `using ChunkAllocator = core::dyn_singleton<detail::ChunkAllocatorImpl>`) whose
            // per-page block-recycle free-list (MemoryPage::m_nextFreeBlock/m_freeBlocks) has
            // NO internal synchronization. This is exactly the "shared allocator" hazard this
            // task's own instructions called out to check for -- the static read of
            // ComponentRegistry/GaiaVoxelWorld.h alone missed it because the singleton lives
            // in gaia.h's own chunk-management internals, not in this codebase's wrapper
            // layer; only a live run surfaced it.
            // FIX: g_gaiaChunkAllocatorMutex (function-local static below) serializes the
            // ENTIRE per-body bake+build call (bakeWorldSpaceBody's lock_guard, and the
            // light body's own lambda) rather than attempting to lock only the specific
            // internal Gaia call sites that touch ChunkAllocator -- World construction,
            // component registration, createVoxelsBatch, AND rebuild() all reach into Gaia's
            // chunk internals at various points, and mis-scoping the lock to miss one of them
            // would silently reintroduce the same corruption. This still yields real overlap:
            // the CPU-bound per-voxel evalRecipe scan (BakeSdfWorld's occupancy + full-brick
            // passes -- pure computation, no Gaia calls, audit-doc-confirmed dominant cost of
            // the bake) for whichever bodies haven't yet reached the lock runs concurrently
            // with whichever ONE body currently holds it during its own allocation phase.
            // (mainLogger->Info/Error calls deliberately stay OUTSIDE the parallel section below --
            // Logger's own logEntries vector is NOT internally synchronized, only the two atomics
            // are, so logging from worker threads would race; every existing log call already sat
            // on the main thread after all bakes complete, so this is unchanged.)
            auto leftWallFut  = std::async(std::launch::async, bakeWorldSpaceBody, std::cref(bodies[0].prog), bodies[0].worldCenter, kWallN, kWallSubdiv,
                                            glm::vec3(kWallThicknessHalf, kWallSpanHalf, kZWideHalf));
            auto rightWallFut = std::async(std::launch::async, bakeWorldSpaceBody, std::cref(bodies[1].prog), bodies[1].worldCenter, kWallN, kWallSubdiv,
                                            glm::vec3(kWallThicknessHalf, kWallSpanHalf, kZWideHalf));
            auto backWallFut  = std::async(std::launch::async, bakeWorldSpaceBody, std::cref(bodies[2].prog), bodies[2].worldCenter, kWallN, kWallSubdiv,
                                            glm::vec3(kWallSpanHalf, kWallSpanHalf, kWallThicknessHalf));
            auto floorFut     = std::async(std::launch::async, bakeWorldSpaceBody, std::cref(bodies[3].prog), bodies[3].worldCenter, kWallN, kWallSubdiv,
                                            glm::vec3(kWallSpanHalf, kWallThicknessHalf, kZWideHalf));
            auto ceilingFut   = std::async(std::launch::async, bakeWorldSpaceBody, std::cref(bodies[4].prog), bodies[4].worldCenter, kWallN, kWallSubdiv,
                                            glm::vec3(kWallSpanHalf, kWallThicknessHalf, kZWideHalf));
            auto sphereObjFut = std::async(std::launch::async, bakeWorldSpaceBody, std::cref(bodies[6].prog), bodies[6].worldCenter, kSmallN, kSmallSubdiv,
                                            glm::vec3(0.0f));
            auto boxObjFut    = std::async(std::launch::async, bakeWorldSpaceBody, std::cref(bodies[7].prog), bodies[7].worldCenter, kSmallN, kSmallSubdiv,
                                            glm::vec3(0.0f));

            // Light body: baked WITH emission (constant intensity across its whole volume —
            // the ceiling-recessed box IS the emitter, no separate "emissive surface only"
            // distinction at this milestone's fidelity). Same world-space-eval adapter as every
            // other body, plus an EmitFn (BakeSdfWorld's own generic EmitFn template param).
            // Runs on its own async task too -- BakeSdfWorld+BuildSdfBodyOctree together are the
            // SAME self-contained bake-and-build sequence bakeWorldSpaceBody wraps for the other
            // 7 bodies, just with an explicit EmitFn instead of the default NoEmission.
            // (kLightWorldCenter is declared above, before the cache hit/miss branch --
            // needed by both branches.)
            auto lightFut = std::async(std::launch::async, [&]() {
                std::lock_guard<std::mutex> gaiaLock(g_gaiaChunkAllocatorMutex);
                Vixen::SVO::SdfBakeResult lightBaked = Vixen::SVO::BakeSdfWorld(
                    makeWorldSpaceEval(bodies[5].prog, kLightWorldCenter, kSmallN, kSmallSubdiv),
                    kLightWorldCenter, kSmallN, kBand, 3,
                    [](const glm::vec3&) { return kLightEmissionIntensity; },
                    [](const glm::vec3&) { return glm::vec3(1.0f); });  // flat white, not the debug rainbow (tint applied once via inst.color; see bakeWorldSpaceBody)
                return Vixen::SVO::BuildSdfBodyOctree(lightBaked, 3);
            });

            // Join point: .get() blocks until each body's bake completes. Order of the .get()
            // calls doesn't affect wall time (all 8 tasks were already launched above); this
            // sequence just matches the original single-threaded assignment order so downstream
            // code (octreesForCat, instances[]) is untouched.
            Vixen::SVO::SdfBodyOctree leftWallBody   = leftWallFut.get();
            Vixen::SVO::SdfBodyOctree rightWallBody  = rightWallFut.get();
            Vixen::SVO::SdfBodyOctree backWallBody   = backWallFut.get();
            Vixen::SVO::SdfBodyOctree floorBody      = floorFut.get();
            Vixen::SVO::SdfBodyOctree ceilingBody    = ceilingFut.get();
            Vixen::SVO::SdfBodyOctree sphereObjBody  = sphereObjFut.get();
            Vixen::SVO::SdfBodyOctree boxObjBody     = boxObjFut.get();
            Vixen::SVO::SdfBodyOctree lightBody      = lightFut.get();

            // (kLightWorldPos/kLightRenderScale declared above, before the cache branch.)

            const Vixen::SVO::Octree* lightOct = lightBody.octree->getOctree();
            if (lightOct == nullptr) {
                mainLogger->Error("[BuildRenderGraph] VIXEN_DDGI_CORNELL_BAKED_DEMO: light body octree is null -- scene not built");
                bakeOk = false;  // skip the occupancy/instance-seeding tail below, same as the pre-M7 control flow
            } else {
                // Baked-Perf M7 Task 7.2 (audit F5): the light body used to be serialized
                // AND mip-baked here, then mip-baked a SECOND time (BakeMipPool called again
                // just to get a MipPool object for BuildLightTreeCut, discarding the first
                // bake BakeAndAttachMipPool already did internally), then serialized a THIRD
                // time inside ConcatenateSdfWithMips's own per-body loop below. BakeMipPool is
                // pure (same octree + same already-serialized channelPool in, same MipPool
                // out), so baking once and reusing the result for both BuildLightTreeCut and
                // the concat pass is byte-identical, not an approximation.
                Vixen::SVO::SerializedOctree lightSer = Vixen::SVO::SerializeSdf(lightBody);
                Vixen::SVO::MipPool lightMipPool = Vixen::SVO::BakeMipPool(*lightOct, lightSer);
                lightSer.mipPool = Vixen::SVO::SerializeMipPool(lightMipPool);

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
                // (worldCut is the OUTER-scope vector declared above the cache branch -- both
                // the HIT and MISS paths populate the same variable so the tail below is shared.)
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
                // Task 0.3 (Baked-Content Perf Audit F2): the TEMP DIAG block that used to sit
                // here (root-causing invisible walls, now resolved) re-serialized all 8 bodies via
                // SerializeSdf a SECOND time (ConcatenateSdfWithMips below does its own real
                // serialization pass) purely to log nodeCount/brickCount/bounds -- 23.0 s measured
                // on a fresh boot. Those counts are derived by WALKING the octree during
                // serialization (descriptors.size()/brickViews.size() in ShellOctreeGpu.h), not
                // stored as an O(1) field on Octree itself, so there is no cheap equivalent to
                // preserve -- deleted rather than fabricating an approximate substitute.
                //
                // Baked-Perf M7 Task 7.6: brick dedup by reference. Pre-serialize ALL 8 bodies
                // (reusing Task 7.2's SerializeSdfWithMips helper -- the light body's own
                // lightSer above is ALREADY exactly this, computed once), run DedupBricks on
                // each body's own SerializedOctree (dedup is scoped per-octree -- see
                // ShellOctreeGpu.h's own header comment on DedupBricks for why cross-octree
                // dedup would need a bigger addressing change), then hand ALL 8 as precomputed
                // entries to ConcatenateSdfWithMips so it serializes nothing itself and just
                // concatenates the already-deduplicated per-body arrays. This kills BOTH
                // intra-body duplication (a flat wall's identical interior bricks) and
                // inter-body duplication WITHIN each body's own dedup pass; true cross-body
                // sharing (the 5 walls' bricks against EACH OTHER) is out of scope for this
                // pass -- see the DedupBricks header comment.
                std::unordered_map<size_t, Vixen::SVO::SerializedOctree> precomputedSer;
                uint32_t totalOriginalBricks = 0;
                uint32_t totalDedupedBricks  = 0;
                for (size_t k = 0; k < octreesForCat.size(); ++k) {
                    Vixen::SVO::SerializedOctree ser = (k == 5)
                        ? lightSer  // already serialized+mip-baked above; copy, don't redo the work
                        : Vixen::SVO::SerializeSdfWithMips(*octreesForCat[k]);
                    const Vixen::SVO::BrickDedupResult dedup = Vixen::SVO::DedupBricks(ser);
                    totalOriginalBricks += dedup.originalBrickCount;
                    totalDedupedBricks  += dedup.dedupedBrickCount;
                    precomputedSer.emplace(k, std::move(ser));
                }
                const float overallDedupRatio = totalDedupedBricks > 0
                    ? static_cast<float>(totalOriginalBricks) / static_cast<float>(totalDedupedBricks)
                    : 1.0f;
                mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_BAKED_DEMO: brick dedup -- "
                                  "originalBricks=" + std::to_string(totalOriginalBricks) +
                                  " dedupedBricks=" + std::to_string(totalDedupedBricks) +
                                  " ratio=" + std::to_string(overallDedupRatio));

                // (cat is the OUTER-scope ConcatenatedOctrees declared above the cache branch.)
                cat = Vixen::SVO::ConcatenateSdfWithMips(octreesForCat, precomputedSer);

                // Baked-Perf M7 Task 7.4: store the fresh bake to the disk cache for the NEXT
                // boot. Store failures are logged but non-fatal (this boot already has a good
                // `cat`/`worldCut` in hand; only a future boot loses the warm-start benefit).
                Vixen::SVO::BakeArtifactBundle bundleToStore;
                bundleToStore.cat = cat;              // copy -- `cat` is still needed below (SetRecipePool moves it later)
                bundleToStore.lightTreeCut = worldCut; // copy -- `worldCut` is still needed below (SetLightTreeCut reads it)
                if (Vixen::SVO::StoreBakeArtifact(bakeArtifactKey, bundleToStore)) {
                    mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_BAKED_DEMO: bake-artifact "
                                      "cache STORE ok (key=" + bakeArtifactKey + ")");
                } else {
                    mainLogger->Warning("[BuildRenderGraph] VIXEN_DDGI_CORNELL_BAKED_DEMO: bake-artifact "
                                          "cache STORE failed (key=" + bakeArtifactKey + ") -- next boot will bake fresh again");
                }
                }  // else (bake MISS)
            }

            // Baked-Perf M7: everything below runs for BOTH a cache HIT (cat/worldCut loaded
            // from disk) and a fresh-bake MISS (cat/worldCut just computed above) -- gated on
            // bakeOk, which mirrors the pre-M7 code's own "lightOct == nullptr -> skip
            // everything downstream" control flow (now the only way this can be false is the
            // MISS branch's own lightOct-null check, since a cache HIT has no octree to fail).
            if (bakeOk) {
                // Baked-Perf M7 Task 7.5: per-body occupancy stats -- occupied bricks /
                // bounding-volume bricks (bpa^3), and voxel fill ratio (occupied voxels /
                // (brickCount*512)) within allocated bricks. This is the data M8 needs to
                // decide, per-body, whether a dense 3D texture (which only wins where fill
                // ratio is near 1.0) is worth it -- a genuinely sparse body loses both
                // inter-brick sparseness and the sparse-mip hierarchy to a dense texture.
                // Reads ONLY `cat` (bricksPerAxis via cat.configs[k], occupied counts via
                // cat.brickCounts[k]/cat.occupiedVoxelCounts[k]) so it works identically on a
                // cache HIT (no live octree objects exist in that path) and a MISS.
                // Log line format: [CornellDiag] mirrors this demo's existing diagnostic
                // block naming convention (instIdx map, OOB counter) rather than inventing
                // a new tag.
                {
                    static const char* kBodyNames[8] = {
                        "leftWall", "rightWall", "backWall", "floor", "ceiling",
                        "light", "sphereObj", "boxObj",
                    };
                    std::ostringstream occJson;
                    occJson << "{\n";
                    for (size_t k = 0; k < 8; ++k) {
                        const uint64_t bpa = cat.configs[k].bricksPerAxis;
                        const uint64_t boundingVolumeBricks = bpa * bpa * bpa;
                        const uint64_t occupiedBricks = cat.brickCounts[k];
                        const uint64_t occupiedVoxels = cat.occupiedVoxelCounts[k];
                        const uint64_t voxelsInOccupiedBricks =
                            occupiedBricks * Vixen::SVO::SerializedOctree::kVoxelsPerBrick;
                        const double brickOccupancyRatio = boundingVolumeBricks > 0
                            ? static_cast<double>(occupiedBricks) / static_cast<double>(boundingVolumeBricks) : 0.0;
                        const double voxelFillRatio = voxelsInOccupiedBricks > 0
                            ? static_cast<double>(occupiedVoxels) / static_cast<double>(voxelsInOccupiedBricks) : 0.0;
                        mainLogger->Info("[CornellDiag] occupancy body=" + std::string(kBodyNames[k]) +
                                          " occupiedBricks=" + std::to_string(occupiedBricks) +
                                          " boundingVolumeBricks=" + std::to_string(boundingVolumeBricks) +
                                          " brickOccupancyRatio=" + std::to_string(brickOccupancyRatio) +
                                          " occupiedVoxels=" + std::to_string(occupiedVoxels) +
                                          " voxelFillRatio=" + std::to_string(voxelFillRatio));
                        occJson << "  \"" << kBodyNames[k] << "\": {"
                                << "\"occupiedBricks\": " << occupiedBricks << ", "
                                << "\"boundingVolumeBricks\": " << boundingVolumeBricks << ", "
                                << "\"brickOccupancyRatio\": " << brickOccupancyRatio << ", "
                                << "\"occupiedVoxels\": " << occupiedVoxels << ", "
                                << "\"voxelFillRatio\": " << voxelFillRatio << "}"
                                << (k + 1 < 8 ? ",\n" : "\n");
                    }
                    occJson << "}\n";
                    // Env-var-driven output path (VIXEN_OCCUPANCY_JSON), matching the existing
                    // VIXEN_PERF_CSV/VIXEN_HUD_CAPTURE_DIR convention (PerfCsvWriter.cpp) rather
                    // than a cwd-relative literal -- the app's cwd at launch (VIXEN\, per every
                    // temp_bench\*.bat's own `cd /d %VIXEN_ROOT%\VIXEN`) is NOT the worktree root
                    // temp_bench\ lives under, so a bare "temp_bench/occupancy.json" silently
                    // misses (caught live: this file did not exist after the first bake run).
                    // Falls back to the historical relative path if the env var is unset, so a
                    // bare manual launch from the repo root still gets a file, just not
                    // necessarily where the bench scripts expect it.
                    const char* occPathEnv = std::getenv("VIXEN_OCCUPANCY_JSON");
                    const std::string occPath = occPathEnv ? occPathEnv : "temp_bench/occupancy.json";
                    std::ofstream occFile(occPath);
                    if (occFile) {
                        occFile << occJson.str();
                    } else {
                        mainLogger->Warning("[BuildRenderGraph] VIXEN_DDGI_CORNELL_BAKED_DEMO: "
                                              "could not write occupancy JSON to " + occPath);
                    }
                }

                // M11.2: emissionIntensity defaults to 0.0 (every non-emissive body) and rides
                // in recipeParams[3] -- STORED instances never touch recipeParams (PROVIDER_STORED
                // is never read by TraceWorld.glsl's recipeParams accesses, all gated on
                // PROVIDER_PROCEDURAL), so the slot is genuinely free. See TraceWorld.glsl's
                // WorldHit.emission field for the shader-side read.
                auto makeInstance = [&](uint32_t octreeIdx, glm::vec3 color, glm::vec3 worldPos,
                                        float renderScale, float emissionIntensity = 0.0f) {
                    Vixen::SVO::BodyInstanceGpu inst{};
                    inst.worldPos[0] = worldPos.x;
                    inst.worldPos[1] = worldPos.y;
                    inst.worldPos[2] = worldPos.z;
                    inst.renderScale = renderScale;
                    inst.color[0] = color.x; inst.color[1] = color.y; inst.color[2] = color.z;
                    inst.octreeIndex = octreeIdx;
                    inst.providerKind = 0u;  // PROVIDER_STORED
                    inst.recipeId = 0u;
                    inst.recipeParams[3] = emissionIntensity;
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
                    makeInstance(5u, bodies[5].color, kLightWorldPos,                                            kLightRenderScale, kLightEmissionIntensity),  // light
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
        } else if (envFlagEnabled("VIXEN_DDGI_CORNELL_VIRTUAL_DEMO")) {
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
                // M11.2: recipeParams[3..5] are unconditionally zero-initialized above and
                // never written elsewhere in this loop (every body here "samples world p
                // directly", per this block's own header comment) -- genuinely spare, so the
                // light body's emission intensity rides in recipeParams[3] rather than a new
                // BodyInstance field. Every other body stays 0.0 (non-emissive).
                if (std::string(b.name) == "light") {
                    inst.recipeParams[3] = kLightEmissionIntensity;
                }
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
        } else if (envFlagEnabled("VIXEN_DDGI_CORNELL_HYBRID_DEMO")) {
            // Sampled Lighting — Cornell Box GI Reference Scene, M6b Task 6b.1 (hybrid
            // provider variant, "hole in the wall"). Plan: Baked-Perf-Fix-Pipeline-Plan-2026-07.md
            // Milestone M6b.
            //
            // Structurally this is M2's virtual/zero-bake variant (7 bodies PROVIDER_PROCEDURAL,
            // zero bake calls) with exactly ONE body -- rightWall -- flipped to PROVIDER_STORED,
            // whose bake is a MODIFIED shape (the rightWall RoundedBox minus a Cylinder, so light
            // can pass through a visible hole). This needs zero new engine machinery: the CSG
            // Subtract opcode (SdfOpCode::Subtract) and BakeSdfWorld (the same bake path M1's
            // baked variant already uses per-body) already support an arbitrary instruction
            // program -- this block just authors a 2-instruction program (RoundedBox, Cylinder,
            // Subtract) instead of M1's 1-instruction (RoundedBox only) program for that one body.
            //
            // Geometry/color/camera SAME shared source as every other Cornell variant
            // (CornellBoxSceneDefinition.h via BuildCornellWorldSpaceBodies()) so the hole is the
            // ONLY visual difference from the virtual/baked baselines.
            using namespace Vixen::App::CornellBox;
            using Vixen::SVO::Recipe::SdfInstruction;
            mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_HYBRID_DEMO: building the Cornell "
                              "box GI reference scene (hybrid variant: rightWall PROVIDER_STORED with "
                              "a baked hole, other 7 bodies PROVIDER_PROCEDURAL)");

            std::vector<CornellWorldSpaceBody> worldBodies = BuildCornellWorldSpaceBodies();
            // worldBodies[]: leftWall(0), rightWall(1), backWall(2), floor(3), ceiling(4), light(5),
            // sphereObj(6), boxObj(7) -- fixed order, see BuildCornellWorldSpaceBodies.
            constexpr size_t kHoleWallIdx = 1;  // rightWall

            // --- rightWall: baked, MODIFIED shape (wall minus a through-hole cylinder) ---
            //
            // SdfOpCode::Cylinder (SdfRecipeEval.h / generated/SdfCoreKernels.g.hpp
            // SdfCore_Cylinder) is NOT a position/axis-carrying primitive like RoundedBox/Sphere --
            // it evaluates directly against the current `pos` with its bore axis HARD-FIXED to
            // local Y (SdfCore_Cylinder: radial test on p.x/p.z, height test on p.y) and has no
            // data[4..6] center field at all. To place and orient it, wrap it in a
            // SdfOpCode::Transform/RestorePos domain-transform pair (the same push/pop position-
            // stack mechanism Twist/Bend/RepeatInfinite use -- SdfRecipeEval.h's Transform case):
            // Transform pushes `pos` into the cylinder's local frame via
            // pos' = invRot*(pos-translation)*invScale (SdfCore_Transform), the Cylinder opcode
            // then evaluates in that local frame, and RestorePos pops back to world space
            // afterward for the following Subtract to combine correctly with the wall's own
            // world-space RoundedBox distance.
            //
            // rightWall is thin on X (CornellWorldBoxAt(kRightWallWorldCenter, vec3(kWallThickness,
            // kWallSpan, kZWideHalfExtent), ...) -- BuildCornellWorldSpaceBodies), so the hole must
            // bore through world X to be a see-through hole rather than a shallow dimple. Since the
            // cylinder's own bore axis is fixed to LOCAL Y, invRot must rotate world X onto local Y:
            // a +90 degree rotation about Z maps (1,0,0) -> (0,1,0) exactly (verified numerically:
            // quaternion (qv=(0,0,sin45),qw=cos45) applied to SdfCore_Transform's rotation formula
            // sends world X to local Y with zero residual on X/Z). translation = the wall's own
            // world center (Transform's data[0..2]), so the cylinder is centered on the wall.
            const float kHoleRadius = 3.0f;                    // world units -- clearly visible, well inside the wall's span
            const float kHoleHalfLen = kWallThickness * 2.0f;  // bores fully through the wall's thin (X) axis with margin
            const glm::vec3& kHoleWallCenter = worldBodies[kHoleWallIdx].worldCenter;

            SdfInstruction transformIn{};
            transformIn.opCode = static_cast<uint8_t>(SdfOpCode::Transform);
            transformIn.data[0] = kHoleWallCenter.x;  // translation (Data0.xyz)
            transformIn.data[1] = kHoleWallCenter.y;
            transformIn.data[2] = kHoleWallCenter.z;
            // invRot xyzw: +90deg about Z, world X -> local Y (see derivation above)
            transformIn.data[4] = 0.0f;
            transformIn.data[5] = 0.0f;
            transformIn.data[6] = 0.70710678f;   // sin(45deg)
            transformIn.data[7] = 0.70710678f;   // cos(45deg)
            transformIn.data[8]  = 1.0f;  // invScale (no scaling)
            transformIn.data[9]  = 1.0f;
            transformIn.data[10] = 1.0f;
            transformIn.data[11] = 1.0f;  // distScale (rotation/translation only -- no distance rescale)

            SdfInstruction holeCylinder{};
            holeCylinder.opCode = static_cast<uint8_t>(SdfOpCode::Cylinder);
            holeCylinder.data[0] = kHoleHalfLen;  // halfHeight (bore axis, local Y)
            holeCylinder.data[1] = kHoleRadius;   // radius

            SdfInstruction restorePosOp{};
            restorePosOp.opCode = static_cast<uint8_t>(SdfOpCode::RestorePos);

            SdfInstruction subtractOp{};
            subtractOp.opCode = static_cast<uint8_t>(SdfOpCode::Subtract);

            std::vector<SdfInstruction> holedWallProg = worldBodies[kHoleWallIdx].prog;  // [RoundedBox]
            holedWallProg.push_back(transformIn);
            holedWallProg.push_back(holeCylinder);
            holedWallProg.push_back(restorePosOp);
            holedWallProg.push_back(subtractOp);
            // stack after: RoundedBox(wall, world space), Cylinder(hole, transformed into world
            // space by RestorePos) -> Subtract -> wall minus hole.

            constexpr float kBand = 2.0f;
            constexpr float kWorldGridSize = 10.0f;  // ShellOctreeGpu.h's fixed octree-local->world span
            constexpr int kWallSubdiv = 4;
            constexpr int kWallN = 128;  // power of two -- see M1 baked variant's own pow2 note

            auto bodyWorldPos = [](glm::vec3 bodyWorldCenter, int n, int subdiv) {
                return bodyWorldCenter - glm::vec3(static_cast<float>(n) / (2.0f * static_cast<float>(subdiv)));
            };
            auto bodyRenderScale = [](int n, int subdiv) {
                return static_cast<float>(n) / (static_cast<float>(subdiv) * kWorldGridSize);
            };
            auto makeWorldSpaceEval = [](const std::vector<SdfInstruction>& prog, glm::vec3 bodyWorldCenter, int n, int subdiv) {
                return [&prog, bodyWorldCenter, n, subdiv](const glm::vec3& pRaw) {
                    const glm::vec3 world = bodyWorldCenter + (pRaw - glm::vec3(static_cast<float>(n) * 0.5f)) / static_cast<float>(subdiv);
                    return Vixen::SVO::Recipe::evalRecipe(prog.data(), static_cast<uint32_t>(prog.size()), world) * static_cast<float>(subdiv);
                };
            };

            Vixen::SVO::SdfBakeResult holedWallBaked = Vixen::SVO::BakeSdfWorld(
                makeWorldSpaceEval(holedWallProg, worldBodies[kHoleWallIdx].worldCenter, kWallN, kWallSubdiv),
                worldBodies[kHoleWallIdx].worldCenter, kWallN, kBand, 3, Vixen::SVO::NoEmission,
                [](const glm::vec3&) { return glm::vec3(1.0f); });
            Vixen::SVO::SdfBodyOctree holedWallBody = Vixen::SVO::BuildSdfBodyOctree(holedWallBaked, 3);

            const Vixen::SVO::Octree* holedWallOct = holedWallBody.octree->getOctree();
            if (holedWallOct == nullptr) {
                mainLogger->Error("[BuildRenderGraph] VIXEN_DDGI_CORNELL_HYBRID_DEMO: holed rightWall "
                                   "octree is null -- scene not built");
            } else {
                std::vector<const Vixen::SVO::SdfBodyOctree*> octreesForCat = { &holedWallBody };
                Vixen::SVO::ConcatenatedOctrees cat = Vixen::SVO::ConcatenateSdfWithMips(octreesForCat);

                if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                    bodyScene->SetRecipePool(std::move(cat));
                    bodyScene->RequestBrickResidency(true);  // same eager-residency requirement as M1's baked variant

                    // --- Assemble all 8 instances: rightWall STORED (octreeIndex 0, the only
                    // entry in cat), the other 7 PROCEDURAL ---
                    std::vector<Vixen::SVO::BodyInstanceGpu> instances;
                    instances.reserve(worldBodies.size());
                    bool allRegistered = true;
                    for (size_t i = 0; i < worldBodies.size(); ++i) {
                        const CornellWorldSpaceBody& b = worldBodies[i];
                        Vixen::SVO::BodyInstanceGpu inst{};
                        inst.color[0] = b.color.x; inst.color[1] = b.color.y; inst.color[2] = b.color.z;

                        if (i == kHoleWallIdx) {
                            const glm::vec3 wp = bodyWorldPos(b.worldCenter, kWallN, kWallSubdiv);
                            inst.worldPos[0] = wp.x; inst.worldPos[1] = wp.y; inst.worldPos[2] = wp.z;
                            inst.renderScale = bodyRenderScale(kWallN, kWallSubdiv);
                            inst.octreeIndex = 0u;
                            inst.providerKind = 0u;  // PROVIDER_STORED
                            inst.recipeId = 0u;
                        } else {
                            const uint32_t recipeId = static_cast<uint32_t>(2 + i);  // 2..9, mirrors M2's own convention
                            Vixen::SVO::RecipeRegistry::RecipeEntry entry{};
                            entry.bytecode = b.prog;
                            entry.boundCenter = b.worldCenter;
                            entry.boundRadius = b.boundRadius;
                            auto regResult = RegisterProceduralRecipe(recipeId, entry);
                            if (regResult != Vixen::SVO::RecipeRegistry::RegisterResult::Ok) {
                                mainLogger->Error(std::string("[BuildRenderGraph] VIXEN_DDGI_CORNELL_HYBRID_DEMO: "
                                                 "RegisterProceduralRecipe(") + b.name + ") failed, code " +
                                                 std::to_string(static_cast<int>(regResult)));
                                allRegistered = false;
                            }
                            inst.worldPos[0] = 0.0f; inst.worldPos[1] = 0.0f; inst.worldPos[2] = 0.0f;  // unused: field samples world p directly
                            inst.renderScale = 1.0f;  // unused by Procedural
                            inst.octreeIndex = 0u;    // unused by Procedural
                            inst.providerKind = 1u;   // PROVIDER_PROCEDURAL
                            inst.recipeId = recipeId;
                        }
                        instances.push_back(inst);
                    }
                    bodyScene->SetInstances(std::move(instances));
                    mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_HYBRID_DEMO: seeded 8 body "
                                      "instances (rightWall PROVIDER_STORED w/ hole, 7 PROVIDER_PROCEDURAL), "
                                      "allRegistered=" + (allRegistered ? std::string("true") : std::string("false")));
                }
            }

            // Light-tree cut: SAME side-bake mechanism as M2's virtual variant (light-tree cut is
            // structurally baked-content-only regardless of how bodies render -- see that block's
            // own header comment). The light BODY's visible pixels render via PROVIDER_PROCEDURAL
            // above like every other non-holed body; this bake feeds ONLY the DDGI light-tree cut.
            {
                constexpr int kLightBakeN = 16;
                const glm::vec3 kLightBakeCenter(static_cast<float>(kLightBakeN) * 0.5f);
                std::vector<SdfInstruction> lightLocalProg = {
                    CornellWorldBoxAt(kLightBakeCenter, kLightHalfExtent, 0.05f)
                };
                Vixen::SVO::SdfBakeResult lightBaked = Vixen::SVO::BakeRecipeInstructionsToSdfWorldWithEmission(
                    lightLocalProg.data(), static_cast<uint32_t>(lightLocalProg.size()), kLightBakeCenter,
                    kLightBakeN, kBand,
                    [](const glm::vec3&) { return kLightEmissionIntensity; });
                Vixen::SVO::SdfBodyOctree lightBody = Vixen::SVO::BuildSdfBodyOctree(lightBaked, 3);

                const Vixen::SVO::Octree* lightOct = lightBody.octree->getOctree();
                if (lightOct == nullptr) {
                    mainLogger->Error("[BuildRenderGraph] VIXEN_DDGI_CORNELL_HYBRID_DEMO: light-tree "
                                       "side-bake octree is null -- no bounce lighting from the ceiling light");
                } else {
                    Vixen::SVO::SerializedOctree lightSer = Vixen::SVO::SerializeSdf(lightBody);
                    Vixen::SVO::BakeAndAttachMipPool(*lightOct, lightSer);
                    Vixen::SVO::MipPool lightMipPool = Vixen::SVO::BakeMipPool(*lightOct, lightSer);

                    Vixen::SVO::LightTreeCutParams cutParams;
                    cutParams.powerThreshold = 0.001f;
                    std::vector<Vixen::SVO::LightTreeNode> cut =
                        Vixen::SVO::BuildLightTreeCut(*lightOct, lightSer, lightMipPool, kLightBakeN, cutParams);

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
                    mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_HYBRID_DEMO: light-tree "
                                      "side-bake cut=" + std::to_string(cut.size()) + " nodes");
                }
            }

#if defined(_WIN32)
            _putenv_s("VIXEN_PROBE_GRID_CONFIG_ENABLED", "1");
#else
            setenv("VIXEN_PROBE_GRID_CONFIG_ENABLED", "1", 1);
#endif
            mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_HYBRID_DEMO: force-enabled "
                              "VIXEN_PROBE_GRID_CONFIG_ENABLED=1");
        } else if (envFlagEnabled("VIXEN_DDGI_CORNELL_MIXED_DEMO")) {
            // Sampled Lighting — Cornell Box GI Reference Scene, M6b Task 6b.2 (mixed-provider
            // split). Plan: Baked-Perf-Fix-Pipeline-Plan-2026-07.md Milestone M6b.
            //
            // Two configs, selected by the env var's VALUE:
            //   VIXEN_DDGI_CORNELL_MIXED_DEMO=walls_stored   -- 5 walls PROVIDER_STORED (baked,
            //     UNMODIFIED shape, unlike 6b.1's hole), light+sphereObj+boxObj PROVIDER_PROCEDURAL.
            //   VIXEN_DDGI_CORNELL_MIXED_DEMO=objects_stored (or any other/empty value) -- the
            //     INVERSE: light+sphereObj+boxObj PROVIDER_STORED, 5 walls PROVIDER_PROCEDURAL.
            //
            // Mechanically this is 6b.1's hybrid pattern generalized to an arbitrary index SET
            // instead of a single hard-coded body: bake every body in the STORED set (mirroring
            // M1's bakeWorldSpaceBody/light-emission bake), register every body in the
            // PROCEDURAL set (mirroring M2's RegisterProceduralRecipe loop), concatenate only the
            // STORED subset via ConcatenateSdfWithMips (never all 8 -- an empty STORED set would
            // pass an empty vector, which ConcatenateSdfWithMips accepts). The light-tree cut is
            // ALWAYS derived from its own explicitly-scoped side bake regardless of which set the
            // light body's VISIBLE geometry is in (same "light-tree cut is baked-content-only"
            // architectural boundary M2's own header comment establishes) -- so, unusually, the
            // walls_stored config's light body appears in BOTH bake and procedural registration
            // for different purposes (register for pixels, side-bake for the DDGI cut), matching
            // M2's own light handling exactly.
            using namespace Vixen::App::CornellBox;
            using Vixen::SVO::Recipe::SdfInstruction;
            const char* mixedMode = std::getenv("VIXEN_DDGI_CORNELL_MIXED_DEMO");
            const bool wallsStored = (std::string(mixedMode) == "walls_stored");
            mainLogger->Info(std::string("[BuildRenderGraph] VIXEN_DDGI_CORNELL_MIXED_DEMO: building the "
                              "Cornell box GI reference scene (mixed-provider variant: ") +
                              (wallsStored ? "walls STORED, objects+light PROCEDURAL)" :
                                             "objects+light STORED, walls PROCEDURAL)"));

            std::vector<CornellWorldSpaceBody> worldBodies = BuildCornellWorldSpaceBodies();
            // worldBodies[]: leftWall(0), rightWall(1), backWall(2), floor(3), ceiling(4), light(5),
            // sphereObj(6), boxObj(7) -- fixed order, see BuildCornellWorldSpaceBodies.
            const std::vector<size_t> wallIdx    = {0, 1, 2, 3, 4};
            const std::vector<size_t> objectIdx  = {5, 6, 7};  // light, sphereObj, boxObj
            const std::vector<size_t>& storedIdx     = wallsStored ? wallIdx   : objectIdx;
            const std::vector<size_t>& proceduralIdx = wallsStored ? objectIdx : wallIdx;

            constexpr float kBand = 2.0f;
            constexpr float kWorldGridSize = 10.0f;  // ShellOctreeGpu.h's fixed octree-local->world span
            constexpr int kWallSubdiv = 4;
            constexpr int kWallN = 128;   // power of two -- see M1 baked variant's own pow2 note
            constexpr int kSmallN = 32;
            constexpr int kSmallSubdiv = 1;

            auto bodyWorldPos = [](glm::vec3 bodyWorldCenter, int n, int subdiv) {
                return bodyWorldCenter - glm::vec3(static_cast<float>(n) / (2.0f * static_cast<float>(subdiv)));
            };
            auto bodyRenderScale = [](int n, int subdiv) {
                return static_cast<float>(n) / (static_cast<float>(subdiv) * kWorldGridSize);
            };
            auto makeWorldSpaceEval = [](const std::vector<SdfInstruction>& prog, glm::vec3 bodyWorldCenter, int n, int subdiv) {
                return [&prog, bodyWorldCenter, n, subdiv](const glm::vec3& pRaw) {
                    const glm::vec3 world = bodyWorldCenter + (pRaw - glm::vec3(static_cast<float>(n) * 0.5f)) / static_cast<float>(subdiv);
                    return Vixen::SVO::Recipe::evalRecipe(prog.data(), static_cast<uint32_t>(prog.size()), world) * static_cast<float>(subdiv);
                };
            };
            // n/subdiv per body: walls need kWallN/kWallSubdiv (thin-slab resolution), light/
            // sphere/box need kSmallN/kSmallSubdiv (cube-ish bodies) -- same split M1's baked
            // variant uses, generalized to whichever bodies land in the STORED set here.
            auto gridParamsFor = [&](size_t i) {
                return (i <= 4) ? std::make_pair(kWallN, kWallSubdiv) : std::make_pair(kSmallN, kSmallSubdiv);
            };

            std::vector<Vixen::SVO::SdfBodyOctree> storedOctrees;  // owns storage for octreesForCat below
            storedOctrees.reserve(storedIdx.size());
            std::vector<uint32_t> storedOctreeIndexOf(worldBodies.size(), 0xFFFFFFFFu);  // body idx -> octreesForCat slot
            for (size_t i : storedIdx) {
                const CornellWorldSpaceBody& b = worldBodies[i];
                auto [n, subdiv] = gridParamsFor(i);
                Vixen::SVO::SdfBakeResult baked = Vixen::SVO::BakeSdfWorld(
                    makeWorldSpaceEval(b.prog, b.worldCenter, n, subdiv), b.worldCenter, n, kBand, 3,
                    Vixen::SVO::NoEmission, [](const glm::vec3&) { return glm::vec3(1.0f); });
                storedOctreeIndexOf[i] = static_cast<uint32_t>(storedOctrees.size());
                storedOctrees.push_back(Vixen::SVO::BuildSdfBodyOctree(baked, 3));
            }

            std::vector<const Vixen::SVO::SdfBodyOctree*> octreesForCat;
            octreesForCat.reserve(storedOctrees.size());
            for (const auto& oct : storedOctrees) octreesForCat.push_back(&oct);
            Vixen::SVO::ConcatenatedOctrees cat = Vixen::SVO::ConcatenateSdfWithMips(octreesForCat);

            if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                bodyScene->SetRecipePool(std::move(cat));
                bodyScene->RequestBrickResidency(true);  // same eager-residency requirement as M1/6b.1

                std::vector<Vixen::SVO::BodyInstanceGpu> instances;
                instances.reserve(worldBodies.size());
                bool allRegistered = true;
                std::vector<bool> isStored(worldBodies.size(), false);
                for (size_t i : storedIdx) isStored[i] = true;

                for (size_t i = 0; i < worldBodies.size(); ++i) {
                    const CornellWorldSpaceBody& b = worldBodies[i];
                    Vixen::SVO::BodyInstanceGpu inst{};
                    inst.color[0] = b.color.x; inst.color[1] = b.color.y; inst.color[2] = b.color.z;

                    if (isStored[i]) {
                        auto [n, subdiv] = gridParamsFor(i);
                        const glm::vec3 wp = bodyWorldPos(b.worldCenter, n, subdiv);
                        inst.worldPos[0] = wp.x; inst.worldPos[1] = wp.y; inst.worldPos[2] = wp.z;
                        inst.renderScale = bodyRenderScale(n, subdiv);
                        inst.octreeIndex = storedOctreeIndexOf[i];
                        inst.providerKind = 0u;  // PROVIDER_STORED
                        inst.recipeId = 0u;
                    } else {
                        const uint32_t recipeId = static_cast<uint32_t>(2 + i);  // 2..9, mirrors M2's own convention
                        Vixen::SVO::RecipeRegistry::RecipeEntry entry{};
                        entry.bytecode = b.prog;
                        entry.boundCenter = b.worldCenter;
                        entry.boundRadius = b.boundRadius;
                        auto regResult = RegisterProceduralRecipe(recipeId, entry);
                        if (regResult != Vixen::SVO::RecipeRegistry::RegisterResult::Ok) {
                            mainLogger->Error(std::string("[BuildRenderGraph] VIXEN_DDGI_CORNELL_MIXED_DEMO: "
                                             "RegisterProceduralRecipe(") + b.name + ") failed, code " +
                                             std::to_string(static_cast<int>(regResult)));
                            allRegistered = false;
                        }
                        inst.worldPos[0] = 0.0f; inst.worldPos[1] = 0.0f; inst.worldPos[2] = 0.0f;  // unused: field samples world p directly
                        inst.renderScale = 1.0f;  // unused by Procedural
                        inst.octreeIndex = 0u;    // unused by Procedural
                        inst.providerKind = 1u;   // PROVIDER_PROCEDURAL
                        inst.recipeId = recipeId;
                    }
                    instances.push_back(inst);
                }
                bodyScene->SetInstances(std::move(instances));
                mainLogger->Info(std::string("[BuildRenderGraph] VIXEN_DDGI_CORNELL_MIXED_DEMO: seeded 8 body "
                                  "instances (") + std::to_string(storedIdx.size()) + " PROVIDER_STORED, " +
                                  std::to_string(proceduralIdx.size()) + " PROVIDER_PROCEDURAL), allRegistered=" +
                                  (allRegistered ? std::string("true") : std::string("false")));
            }

            // Light-tree cut: SAME side-bake mechanism as 6b.1/M2 regardless of which set the
            // light body's visible geometry is in -- structurally baked-content-only (see M2's
            // own header comment).
            {
                constexpr int kLightBakeN = 16;
                const glm::vec3 kLightBakeCenter(static_cast<float>(kLightBakeN) * 0.5f);
                std::vector<SdfInstruction> lightLocalProg = {
                    CornellWorldBoxAt(kLightBakeCenter, kLightHalfExtent, 0.05f)
                };
                Vixen::SVO::SdfBakeResult lightBaked = Vixen::SVO::BakeRecipeInstructionsToSdfWorldWithEmission(
                    lightLocalProg.data(), static_cast<uint32_t>(lightLocalProg.size()), kLightBakeCenter,
                    kLightBakeN, kBand,
                    [](const glm::vec3&) { return kLightEmissionIntensity; });
                Vixen::SVO::SdfBodyOctree lightBody = Vixen::SVO::BuildSdfBodyOctree(lightBaked, 3);

                const Vixen::SVO::Octree* lightOct = lightBody.octree->getOctree();
                if (lightOct == nullptr) {
                    mainLogger->Error("[BuildRenderGraph] VIXEN_DDGI_CORNELL_MIXED_DEMO: light-tree "
                                       "side-bake octree is null -- no bounce lighting from the ceiling light");
                } else {
                    Vixen::SVO::SerializedOctree lightSer = Vixen::SVO::SerializeSdf(lightBody);
                    Vixen::SVO::BakeAndAttachMipPool(*lightOct, lightSer);
                    Vixen::SVO::MipPool lightMipPool = Vixen::SVO::BakeMipPool(*lightOct, lightSer);

                    Vixen::SVO::LightTreeCutParams cutParams;
                    cutParams.powerThreshold = 0.001f;
                    std::vector<Vixen::SVO::LightTreeNode> cut =
                        Vixen::SVO::BuildLightTreeCut(*lightOct, lightSer, lightMipPool, kLightBakeN, cutParams);

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
                    mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_MIXED_DEMO: light-tree "
                                      "side-bake cut=" + std::to_string(cut.size()) + " nodes");
                }
            }

#if defined(_WIN32)
            _putenv_s("VIXEN_PROBE_GRID_CONFIG_ENABLED", "1");
#else
            setenv("VIXEN_PROBE_GRID_CONFIG_ENABLED", "1", 1);
#endif
            mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_MIXED_DEMO: force-enabled "
                              "VIXEN_PROBE_GRID_CONFIG_ENABLED=1");
        } else if (envFlagEnabled("VIXEN_DDGI_LEAK_GATE_DEMO") || envFlagEnabled("VIXEN_DDGI_EDIT_LOOP_DEMO")) {
            // Sampled Lighting Inc4 M6 reuses this EXACT scene (geometry, probe placement,
            // near/far indices) for the edit-loop responsiveness gate when
            // VIXEN_DDGI_EDIT_LOOP_DEMO=1 -- same "don't invent a new mechanism" discipline
            // M4's own gate used relative to VIXEN_RESTIR_GATE_DEMO. The only behavioral
            // difference (isEditLoopMode below): the light-tree cut is built EMPTY at scene-
            // construction time (the source starts "off") and the REAL cut is stashed via
            // g_ddgiEditLoopWorldCut for VulkanGraphApplication.cpp's readback hook to flip in
            // live at a chosen tick -- a genuine mid-run scene-content edit, not a restart.
            const bool isEditLoopMode = envFlagEnabled("VIXEN_DDGI_EDIT_LOOP_DEMO");

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
            // SDF Bake Box-Tight Region M2: this body was investigated as this plan's own
            // second named validation case, but a box-tight bakeRegion computed honestly for
            // its ACTUAL geometry is a no-op here -- NOT applied, deliberately, not an
            // oversight. M1's bakeRegion is an ORIGIN-ANCHORED [0,region) window (SdfBake.h);
            // it can only trim occupied bricks off the HIGH end of a grid axis. This wall's
            // thin axis (X, half-extent 1.0 + kBand 2.0 = 3.0) is occupied at grid X~[19,25)
            // because kWallCenter.x=22 sits near the HIGH end of the kN=32 grid, not centered
            // at n/2=16 -- so the occupied range's own high edge (25, rounded to the brick
            // boundary at 32) already equals the FULL grid width; there is no low-side region
            // to trim away. The other two axes (Y/Z, half-extent 15) are occupied almost the
            // entire [0,32) span by design ("a real wall, not a small block", see this block's
            // own header comment) -- also no room to shrink. Recentering kWallCenter to n/2
            // would change this body's WORLD position (both bodies here share ONE grid->world
            // transform, sourceWorldPos/kRenderScale below -- there is no per-body compensating
            // offset the way Cornell's bodyWorldPos/bodyRenderScale gives each wall its own
            // placement), which would desync the near/far probe indices this scene's geometry
            // was carefully hand-tuned against (see the header comment above) -- out of this
            // milestone's scope. Verified by computing bakeRegion per axis for this body's real
            // half-extents: all three axes round up to the full kN=32, i.e. zero brick
            // reduction, confirmed by hand and left unapplied rather than landing dead plumbing
            // with no measured effect. Cornell's walls (bakeWorldSpaceBody above) DO benefit --
            // their bodies are each individually world-placed (bodyWorldPos/bodyRenderScale per
            // body), so recentering the bake grid on each wall's own true center was already the
            // existing convention, not a new risk.
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
        } else if (envFlagEnabled("VIXEN_BRICKMAP_SCENE")) {
            // W-BRICKMAP Slice 2 round 3 RETARGET: the default (env-unset) fallback below is
            // Procedural (providerKind=1, no octree at all -- see its own comment), so an A/B
            // against it is a no-op by construction. The coarse-grid DDA backend now serves
            // FORMAT_STORED_SDF (round-3 root-cause finding: brickGridLookup/brickLookupBase
            // are only populated by the SDF serialization path, ShellOctreeGpu.h's
            // SerializeSdf/ConcatenateSdf -- a FORMAT_BINARY octree's brickLookup binding is a
            // 1-byte placeholder, so round 2's FORMAT_BINARY-only scope-down read it out of
            // bounds). BodyOctreeSceneNode::EnsureOctreesBuilt bakes 3 Stored-SDF octrees when
            // EITHER VIXEN_STORED_SDF_DEMO or VIXEN_BRICKMAP_SCENE is set (same bake, shared) --
            // this branch just points instances at them (providerKind=0/PROVIDER_STORED,
            // formatId==FORMAT_STORED_SDF), reusing VIXEN_STORED_SDF_DEMO's placeStored geometry
            // convention verbatim (kSdfN=64 grid, renderScale=0.75, same transform math).
            // Gated on its OWN env var (VIXEN_BRICKMAP_SCENE), separate from
            // VIXEN_BRICKMAP_TRAVERSAL (the shader-backend toggle) -- an A/B run needs
            // the SAME scene on both OFF and ON boots, varying only the traversal
            // backend; run_brickmap_ab.bat sets both for every boot.
            // BATCH-18/POST-CLOSURE CORRECTION: kHalf below used to be 32*renderScale (a
            // "grid-[0,64] scales directly by renderScale" mental model) -- that is NOT what
            // the actual transform composes (BodyOctreeSceneNode::EnsureRtQueryTlasBuilt /
            // TraceWorld.glsl): world = worldPos + renderScale * (octreeConfig.localToWorld *
            // local[0,1]^3), and localToWorld is a FIXED scale(kWorldGridSize=10) unrelated to
            // the 64-grid resolution -- confirmed by the OTHER demo block in this same file
            // (VIXEN_DDGI_LEAK_GATE_DEMO, ~line 2825), which already uses the correct
            // `kHalf = 5.0f * kRenderScale` form (= 0.5*kWorldGridSize*renderScale) and whose
            // own comment derives it from an [RtTlasInst] corner-transform dump. The body's TRUE
            // world half-extent is renderScale*kWorldGridSize/2 = 0.75*10/2 = 3.75wu (full
            // extent 7.5wu), not 32*0.75=24wu. The old 24wu offset shifted every body ~20.25wu
            // off its intended center per axis (0.5*kWorldGridSize*(32/5 - 1) = 20.25) -- nearly
            // 3 body-diameters, well past "visible": VALUE FIXED, not just the comment. Example
            // (center (64,64,64)): old worldPos (40,40,40) -> new worldPos (60.25,60.25,60.25).
            // This moves every VIXEN_BRICKMAP_SCENE body -- the flag-off identity hash
            // (0bbf6e47..., batch 19) is now STALE; re-baseline required (see ledger).
            constexpr float kRenderScale = 0.75f;
            // Deep-field-mip-policy regime-3 divergence placement. The stored-SDF
            // brick is the mip-policy leaf here: kWorldGridSize/8*renderScale =
            // 0.9375 world units. At the smoke window's measured 500 px height,
            // the live RaySizeCoefNode formula gives coef=0.001570797 and bias=0,
            // so level 2 (leaf*4) begins at 2387.32 world units. Keep the default
            // scene untouched; the FAR gate adds a 10% margin and also moves the
            // existing kind-5 body behind the sparse shell for the composite leg.
            const bool sparseBodyEnabled = envFlagEnabled("VIXEN_SPARSE_BODY");
            const bool sparseBodyFarEnabled = sparseBodyEnabled &&
                                              envFlagEnabled("VIXEN_SPARSE_BODY_FAR");
            // Batch 47 / NEBULA: a second gate on top of VIXEN_SPARSE_BODY.
            // The historical near and FAR placements remain byte-identical
            // when this flag is absent.
            const bool sparseBodyNebulaEnabled = sparseBodyEnabled &&
                                                  envFlagEnabled("VIXEN_SPARSE_BODY_NEBULA");
            // Batch 50 / KI-047 residual: a THIRD independent gate on top of
            // VIXEN_SPARSE_BODY. Sol's V1.3 finding (sol-b49-validation.md) was
            // that the regime-3 composite blend genuinely EXECUTES at
            // residualT=0.015625 (BodyInstanceRayMarch.comp:255's gate passes),
            // but frames stayed byte-identical because TraceWorld.glsl's same-pass
            // second-nearest candidate (secondColor, :642/:672) is never populated
            // -- the 3 seeded near bodies (D~612/800/1200wu) sit on distinct,
            // non-overlapping camera rays. This flag adds a 7th body directly
            // BEHIND the existing D~612wu body (octreeIdx 3, world (200,64,-297))
            // on the SAME ray, so a ray through the near body's silhouette also
            // finds the far one as TraceWorld's real "lost isCloserHit but still a
            // hit" second candidate. Historical near/FAR/NEBULA placements are
            // untouched when this flag is unset (byte-identical, criterion C1).
            const bool sparseBodyOverlapEnabled = sparseBodyEnabled &&
                                                   envFlagEnabled("VIXEN_SPARSE_BODY_OVERLAP");
            constexpr float kSmokeFrameHeight = 500.0f;
            constexpr float kCameraFovDegrees = 45.0f;
            constexpr float kPi = 3.14159265358979323846f;
            const float sparseRaySizeCoef = 2.0f * std::tan(
                (kCameraFovDegrees * kPi / 180.0f / kSmokeFrameHeight) * 0.5f);
            const float sparseLevel2Footprint = (10.0f / 8.0f) * kRenderScale * 4.0f;
            const float sparseLevel2Distance = sparseLevel2Footprint / sparseRaySizeCoef;
            const float sparseFarDistance = sparseLevel2Distance * 1.10f;
            // NEBULA derivation (batch 47): the local bake remains 8^3 bricks,
            // so one brick is (10/8)*scale world units and a level-2 node spans
            // 4 bricks. With scale=128, level 2 starts at
            //   ((10/8)*128*4) / 0.001570797 = 407,436wu;
            // the 10% placement margin is 448,180wu. The same scale makes the
            // kind-6 shell diameter (52/64)*10*128 = 1,040wu, or only 1.48px
            // at that distance. Scaling the instance scales both the object and
            // its cells, so this ratio cannot improve; the measured bar outcome
            // is reported rather than hidden in scene authoring.
            constexpr float kSparseNebulaRenderScale = 128.0f;
            const float sparseNebulaLevel2Footprint = (10.0f / 8.0f) *
                                                       kSparseNebulaRenderScale * 4.0f;
            const float sparseNebulaLevel2Distance = sparseNebulaLevel2Footprint / sparseRaySizeCoef;
            const float sparseNebulaDistance = sparseNebulaLevel2Distance * 1.10f;
            const glm::vec3 sparseFarCamera(64.0f, 64.0f, 300.0f);
            const glm::vec3 sparseFarDirection = glm::normalize(glm::vec3(220.0f, 0.0f, -1170.0f));
            const glm::vec3 sparseFarCenter = sparseFarCamera + sparseFarDirection * sparseFarDistance;
            const glm::vec3 behindSparseFarCenter = sparseFarCamera + sparseFarDirection * (sparseFarDistance + 400.0f);
            const glm::vec3 sparseNebulaCenter = sparseFarCamera + sparseFarDirection * sparseNebulaDistance;
            // Keep the kind-5 sphere behind the nebula's ~520wu front shell
            // surface on the shared camera ray. The old FAR separation remains
            // +400wu; the scaled control uses +700wu.
            const glm::vec3 behindSparseNebulaCenter =
                sparseFarCamera + sparseFarDirection * (sparseNebulaDistance + 700.0f);
            const bool sparseBodyFarPlacementEnabled = sparseBodyFarEnabled || sparseBodyNebulaEnabled;
            // VIXEN_SPARSE_BODY_OVERLAP placement: same ray as the round-12 D~612wu
            // body (octreeIdx 3, world center (200,64,-297); camera at
            // (64,64,300) looking -Z per round-12's convention). Extending
            // 150wu further along that exact camera->near-body direction lands
            // the new body's CENTER on the near body's own ray, at D~762wu --
            // a genuine near/behind overlap pair, not a parallel/offset one.
            //
            // ⚠ FIRST ATTEMPT (both bodies at kRenderScale=0.75) measured a
            // byte-identical frame vs the non-overlap baseline: the far body's
            // angular radius (0.282deg at D~762wu) was SMALLER than the near
            // body's (0.351deg at D~612wu) and both are centered on the exact
            // same ray direction, so the far body's silhouette sat entirely
            // INSIDE the near body's -- every ray that could hit the far body
            // hit the near one first, and TraceWorld's "lost isCloserHit but
            // still a real hit" branch (the only thing that populates
            // secondColor) never fired for ANY pixel. Fix: give the BEHIND
            // body a larger renderScale (kOverlapBehindRenderScale=1.5, vs the
            // near body's 0.75) so its silhouette's angular radius
            // (~0.53deg) exceeds the near body's and visibly rims out around
            // it -- guaranteeing rays exist that miss the near body's disc but
            // hit the far body's, the genuine "second candidate" case.
            constexpr glm::vec3 kOverlapNearBodyCenter(200.0f, 64.0f, -297.0f);
            const glm::vec3 overlapRayDir =
                glm::normalize(kOverlapNearBodyCenter - sparseFarCamera);
            constexpr float kOverlapBehindExtraDistance = 150.0f;
            const glm::vec3 overlapBehindCenter =
                kOverlapNearBodyCenter + overlapRayDir * kOverlapBehindExtraDistance;
            constexpr float kOverlapBehindRenderScale = 1.5f;  // 2x the near body's 0.75

            auto placeStoredSdf = [&](float cx, float cy, float cz,
                                       float r, float g, float b,
                                       uint32_t octreeIdx,
                                       float renderScale) {
                Vixen::SVO::BodyInstanceGpu inst{};
                inst.worldPos[0]  = cx - 5.0f * renderScale;
                inst.worldPos[1]  = cy - 5.0f * renderScale;
                inst.worldPos[2]  = cz - 5.0f * renderScale;
                inst.renderScale  = renderScale;
                inst.color[0]     = r;
                inst.color[1]     = g;
                inst.color[2]     = b;
                inst.octreeIndex  = octreeIdx;
                inst.providerKind = 0u;  // PROVIDER_STORED: octree path (formatId==FORMAT_STORED_SDF here)
                inst.recipeId     = 0u;
                return inst;
            };
            // Round 12 (far-field default-coef parity, plan ledger 2026-08-04
            // "ROUND 12"): the 3 near bodies above sit at D≈507-539wu camera
            // distance -- below the TRUE default-coef tier-crossing distance
            // (596.75wu, closed by arithmetic in Batch 11) -- so this scene could
            // never fire the far-field gate at default coef. Add a 4th body past
            // it. Camera geometry for this scene is FIXED (no PARAM_ORBIT_* is
            // set for VIXEN_BRICKMAP_SCENE): position (64,64,300), yaw=0/pitch=0,
            // which looks toward -Z (BuildRenderGraph.cpp's PRESET-1 comment) --
            // so "far" means SMALLER z, not larger (an earlier version of this
            // edit placed the body at z=+800, behind the camera and invisible;
            // confirmed by a zero-pixel-diff boot against the pre-edit 3-body
            // baseline). x=200,z=-297 -> forward dist 597wu, lateral offset
            // 136wu, D=612.3wu > 597wu crossing, and comfortably inside the
            // 45-deg-FOV cone at that depth (max lateral ~247wu, ~111wu margin)
            // and clear of the HUD band (x69-332/y286-312). octreeIdx=3 is
            // BodyOctreeSceneNode::EnsureOctreesBuilt's VIXEN_BRICKMAP_SCENE-only
            // 4th Stored-SDF bake (kind 3, plain sphere). Near bodies are
            // untouched -- in-frame internal control for the near-region parity leg.
            // ROUND-18 STEP 5 (multi-distance close): 2 more in-cone far bodies, WELL past
            // the 597wu tier-crossing, D~800/D~1200, margins >30% off the FOV cone edge
            // (half-FOV ~22.48deg derived from round-12's own 247wu@597wu figure). Camera
            // fixed at (64,64,300) looking -Z, so "far" = smaller z (round-12 convention).
            // D~800: fwd=780wu, lateral=150wu -> max_lateral=322.8wu, margin=53.5%, D=794.3wu.
            // D~1200: fwd=1170wu, lateral=220wu -> max_lateral=484.2wu, margin=54.6%, D=1190.5wu.
            // octreeIdx 4/5 are EnsureOctreesBuilt's round-18 kinds (plain sphere, same recipe).
            std::vector<Vixen::SVO::BodyInstanceGpu> brickmapBodies = {
                placeStoredSdf( 14.0f,  64.0f,   64.0f, 1.00f, 0.95f, 0.85f, 0u, kRenderScale),
                placeStoredSdf( 64.0f,  64.0f,   64.0f, 0.55f, 0.75f, 1.00f, 1u, kRenderScale),
                placeStoredSdf(114.0f,  64.0f,   64.0f, 0.85f, 0.90f, 1.00f, 2u, kRenderScale),
                placeStoredSdf(200.0f,  64.0f, -297.0f, 1.00f, 0.60f, 0.20f, 3u, kRenderScale),
                placeStoredSdf(214.0f,  64.0f, -480.0f, 0.90f, 0.40f, 0.90f, 4u, kRenderScale),
                placeStoredSdf(sparseBodyFarPlacementEnabled
                                   ? (sparseBodyNebulaEnabled ? behindSparseNebulaCenter.x : behindSparseFarCenter.x)
                                   : 284.0f,
                               64.0f,
                               sparseBodyFarPlacementEnabled
                                   ? (sparseBodyNebulaEnabled ? behindSparseNebulaCenter.z : behindSparseFarCenter.z)
                                   : -870.0f,
                               0.40f, 0.90f, 0.60f, 5u, kRenderScale),
            };
            // Deep-field-mip-policy regime-3 divergence scene (2026-08-08),
            // env-gated (VIXEN_SPARSE_BODY=1, default off -- the hard identity gate):
            // the kind-6 scattered shell is near at the historical D~675wu placement
            // unless VIXEN_SPARSE_BODY_FAR is also enabled. FAR moves it to the
            // derived level-2+ distance and moves kind 5 behind it on the same
            // in-cone ray, preserving actual sparse-shell-over-background layering.
            if (sparseBodyEnabled) {
                const glm::vec3 sparseCenter = sparseBodyNebulaEnabled
                    ? sparseNebulaCenter
                    : sparseBodyFarEnabled
                        ? sparseFarCenter
                        : glm::vec3(191.5f, 64.0f, -363.0f);
                brickmapBodies.push_back(
                    placeStoredSdf(sparseCenter.x, sparseCenter.y, sparseCenter.z,
                                   0.20f, 0.90f, 0.95f, 6u,
                                   sparseBodyNebulaEnabled ? kSparseNebulaRenderScale : kRenderScale));
            }
            // VIXEN_SPARSE_BODY_OVERLAP: a 7th body, BRIGHT (color at/near 1.0 --
            // quantization bar per sol-b49-validation.md V1.3: residualT=0.015625
            // needs behindColor >= ~0.251 to move even one 8-bit LSB; this uses
            // 1.0 -- flat white -- well clear of that bar), placed directly
            // behind the D~612wu body on the SAME camera ray (see
            // overlapBehindCenter derivation above). octreeIdx 7 is
            // EnsureOctreesBuilt's batch-50 kind (plain sphere, same recipe as
            // the other round-18/sparse-shell kinds).
            if (sparseBodyOverlapEnabled) {
                brickmapBodies.push_back(
                    placeStoredSdf(overlapBehindCenter.x, overlapBehindCenter.y, overlapBehindCenter.z,
                                   1.00f, 1.00f, 1.00f, 7u, kOverlapBehindRenderScale));
            }
            if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                bodyScene->SetInstances(std::move(brickmapBodies));
                mainLogger->Info("[BuildRenderGraph] VIXEN_BRICKMAP_SCENE: seeded " +
                                  std::to_string(bodyScene->GetInstances().size()) +
                                  " FORMAT_STORED_SDF body instances "
                                  "(round 12: +1 far body D~612wu; round 18: +2 more D~800/D~1200wu; "
                                  "VIXEN_SPARSE_BODY: +1 scattered-shell occluder; FAR=" +
                                  (sparseBodyNebulaEnabled ? "nebula-level2+" :
                                   sparseBodyFarEnabled ? "level2+" : "off") +
                                  "; OVERLAP=" + (sparseBodyOverlapEnabled ? "on" : "off") + ")");
            }
        } else if (envFlagEnabled("VIXEN_STORED_SDF_DEMO")) {
            // VIXEN_STORED_SDF_DEMO — Stored-SDF bodies (Increment 2, M5 Task 10).
            // EnsureOctreesBuilt has baked 3 SdfBodyOctrees (kinds 0/1/2) via ConcatenateSdf,
            // setting configs[k].formatId = STORED_SDF and populating the sdfBricks /
            // brickGridLookup buffers (bindings 11/12). Instances use providerKind=0 (STORED)
            // and octreeIndex=0/1/2 to select the per-kind OctreeConfig.
            //
            // Transform convention (binary-shell / marchStoredSdf AABB), BATCH-18/POST-CLOSURE
            // CORRECTED: the world span of the instance transform is renderScale *
            // octreeConfig.localToWorld * local[0,1]^3, and localToWorld is a FIXED
            // scale(kWorldGridSize=10) INDEPENDENT of the 64-grid resolution -- so kHalf is
            // 0.5*kWorldGridSize*renderScale = 5*renderScale, NOT 32*renderScale (that formula
            // wrongly assumed the [0,64] grid-voxel index scales directly by renderScale; see
            // the matching correction + derivation at the VIXEN_BRICKMAP_SCENE block above,
            // and the already-correct `5.0f * kRenderScale` reference form at the
            // VIXEN_DDGI_LEAK_GATE_DEMO block, ~line 2825).
            //   renderScale = 0.75       — the world half-extent of the [0,10] localToWorld span
            //   kHalf       = 5 * 0.75 = 3.75   (TRUE half-extent; full extent 7.5wu)
            //   worldPos    = center - 3.75
            //     → de-instance transform: instOrigin = (rayOrigin - worldPos) / renderScale
            //       maps a ray at world center to the octree's local center.
            //
            // Body centers in world space (same spread as the Procedural seed so the
            // default camera (X=64, Z=300, looking -Z) frames all three):
            //   left   center = (14, 64, 64)  → worldPos = (14-3.75, 64-3.75, 64-3.75) = (10.25, 60.25, 60.25)
            //   centre center = (64, 64, 64)  → worldPos = (64-3.75, 64-3.75, 64-3.75) = (60.25, 60.25, 60.25)
            //   right  center = (114,64, 64)  → worldPos = (114-3.75,64-3.75, 64-3.75) = (110.25,60.25, 60.25)
            // (previously, with the wrong kHalf=24: (-10,40,40) / (40,40,40) / (90,40,40) --
            // every body was ~20.25wu off its intended center per axis. VALUE FIXED.)
            constexpr float kRenderScale = 0.75f;
            constexpr float kHalf        = 5.0f * kRenderScale;  // = 3.75f -- TRUE half of the [0,10] localToWorld span

            auto placeStored = [&](float cx, float cy, float cz,
                                   float r, float g, float b,
                                   uint32_t octreeIdx) {
                Vixen::SVO::BodyInstanceGpu inst{};
                inst.worldPos[0]  = cx - kHalf;  // worldPos = center - 3.75 per axis
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

    // Task 0.2 (Baked-Content Perf Audit D2): default OFF -- the every-10th-frame combination of
    // this auto-export AND RayTraceBuffer's own captureEnabled_ (RayTraceBuffer.h, default now
    // also false) is what drives DebugBufferReaderNode's blocking vkWaitForFences(UINT64_MAX)
    // pipeline drain + JSON export, which perturbs every perf bench. VIXEN_DEBUG_CAPTURE=1
    // re-enables both for an actual debugging session (mirrors the VIXEN_* env-knob convention
    // used throughout this file). The [CornellDiag] tick-150 instIdx-map diagnostic
    // (VulkanGraphApplication.cpp) is UNAFFECTED -- it reads hit_record_buffer directly via its
    // own vkDeviceWaitIdle + MapForReadback, with no dependency on this node's capture path.
    const bool debugCaptureEnabled = envFlagEnabled("VIXEN_DEBUG_CAPTURE");
    auto* debugCapture = static_cast<DebugBufferReaderNode*>(renderGraph->GetInstance(debugCaptureNode));
    debugCapture->SetParameter(DebugBufferReaderNodeConfig::PARAM_MAX_SAMPLES, 1000u);
    debugCapture->SetParameter(DebugBufferReaderNodeConfig::PARAM_AUTO_EXPORT, debugCaptureEnabled);
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

    // Step-6 M-ui: mount the relational building-inspector fragment as a SECOND document on the same
    // composite UI node, via the IUiCompositionHost seam (routed through the gaia-free bridge for the
    // same robin_hood/ODR reason as WireHudView). The context doesn't exist until the node's first
    // compile, so Mount PARKS the request and realizes it on the first frame (RealizePendingMounts);
    // buildingMount_ is the returned handle (used by the M-ui gate's alive check). The HUD document is
    // byte-untouched by this — it's an additive second mount.
    buildingMount_ = Vixen::App::MountBuildingInspector(*uiComposite, *buildingInspectorView_);
    mainLogger->Info("Mounted building-inspector fragment (M-ui), handle " + std::to_string(buildingMount_));

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

    // --- Recipe-Live-App-Bucketed-Dispatch Inc4 M3: bucketing pre-pass + specialized dispatch ---
    if (recipeBucketedDispatchEnabled) {
        // Semantic-wiring S2: providers once — every descriptor member resolved
        // from the shader's own merged-SDI member table. All eleven bindings are
        // persistent storage buffers (Dependency|Execute); the instance buffer
        // reuses bodyOctreeSceneNode's own INSTANCE_BUFFER (the SAME buffer the
        // march reads), the 9-10 pair is Load-Tier Contract M2's precision tier.
        SdiProviderRegistry bucketingProviders;
        bucketingProviders.Provide("BodyInstanceBuffer", bodyOctreeSceneNode,
                                   BodyOctreeSceneNodeConfig::INSTANCE_BUFFER,
                                   SlotRole::Dependency | SlotRole::Execute);
        const struct { const char* name; NodeHandle node; } bucketingSsbos[] = {
            {"RecipeBoundSphereBuffer",     recipeBoundSphereBuffer},
            {"BucketCountBuffer",           recipeBucketCountBuffer},
            {"BucketIndicesBuffer",         recipeBucketIndicesBuffer},
            {"BucketCoverageMinXBuffer",    recipeBucketCoverageMinXBuffer},
            {"BucketCoverageMinYBuffer",    recipeBucketCoverageMinYBuffer},
            {"BucketCoverageMaxXBuffer",    recipeBucketCoverageMaxXBuffer},
            {"BucketCoverageMaxYBuffer",    recipeBucketCoverageMaxYBuffer},
            {"BucketIndirectCommandBuffer", recipeBucketIndirectCommandBuffer},
            {"PrecisionBucketCountBuffer",  recipePrecisionBucketCountBuffer},
            {"PrecisionBucketIndicesBuffer", recipePrecisionBucketIndicesBuffer},
        };
        for (const auto& p : bucketingSsbos) {
            bucketingProviders.Provide(p.name, p.node,
                                       StorageBufferNodeConfig::STORAGE_BUFFER,
                                       SlotRole::Dependency | SlotRole::Execute);
        }
        // Push providers (shared by all three mode stages; `mode` itself is the
        // per-stage overlay below). raySizeCoef mirrors the march's own
        // tier-crossing-LOD-override branch so this gate agrees with what the
        // march actually uses for LOD this frame.
        bucketingProviders.Provide("viewProj", recipeBucketingViewProjConstant,
                                   ConstantNodeConfig::OUTPUT, SlotRole::Execute);
        bucketingProviders.Provide("instanceCount", bodyOctreeSceneNode,
                                   BodyOctreeSceneNodeConfig::INSTANCE_COUNT,
                                   SlotRole::Dependency | SlotRole::Execute);
        bucketingProviders.Provide("maxBuckets", recipeBucketingMaxBucketsConstant,
                                   ConstantNodeConfig::OUTPUT, SlotRole::Execute);
        bucketingProviders.Provide("maxMembersPerBucket", recipeBucketingMaxMembersConstant,
                                   ConstantNodeConfig::OUTPUT, SlotRole::Execute);
        bucketingProviders.Provide("screenWidth", renderTargetNode,
                                   RenderTargetNodeConfig::WIDTH_OUT,
                                   SlotRole::Dependency | SlotRole::Execute);
        bucketingProviders.Provide("screenHeight", renderTargetNode,
                                   RenderTargetNodeConfig::HEIGHT_OUT,
                                   SlotRole::Dependency | SlotRole::Execute);
        if (tierCrossingLodCoefOverrideActive) {
            bucketingProviders.Provide("raySizeCoef", tierCrossingLodCoefOverrideConstant,
                                       ConstantNodeConfig::OUTPUT, SlotRole::Execute);
        } else {
            bucketingProviders.Provide("raySizeCoef", raySizeCoefNode,
                                       RaySizeCoefNodeConfig::RAY_SIZE_COEF,
                                       SlotRole::Dependency | SlotRole::Execute);
        }
        bucketingProviders.Provide("raySizeBias", raySizeBiasConstant,
                                   ConstantNodeConfig::OUTPUT, SlotRole::Execute);
        // cameraPos extracts a field from CAMERA_DATA — the custom escape hatch.
        bucketingProviders.ProvideCustom("cameraPos",
            [cameraNode](ConnectionBatch& b, NodeHandle target, uint32_t slot) {
                b.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA, target, slot,
                          ExtractField(&CameraData::cameraPos, SlotRole::Execute));
            });

        // GROUP synthesis (semantic-wiring slice C): ONE shared desc-gatherer/
        // descriptor-set/pipeline chain off the shader lib serving all three
        // mode stages -- the shader's push-constant `mode` field, not a
        // different pipeline, selects behavior; the stages are producers
        // (PARAM_IS_CONSUMER=false at their dispatch-dims site: none touch the
        // swapchain). Descriptors wire ONCE from the shared registry; each
        // stage's push gatherer (created under its historical *_pc_gatherer
        // name) wires with that stage's own `mode` constant overlaid. The
        // chain/commons/push plumbing the hand blocks wrote here is emitted
        // by the helper; the sync-hazard gatherers below stay authored (S3).
        const SdiStageCommon bucketingCommon{deviceNode, commandPoolNode,
                                             swapChainNode, frameSyncNode};
        const auto provideMode = [](NodeHandle modeConstant) {
            return [modeConstant](SdiProviderRegistry& r) {
                r.Provide("mode", modeConstant,
                          ConstantNodeConfig::OUTPUT, SlotRole::Execute);
            };
        };
        SynthesizeComputeStageGroup<BucketSdi::Metadata, BucketSdi::MEMBERS>(
            renderGraph, batch, "recipe_bucketing", recipeBucketingShaderLib,
            {
                {recipeBucketingModeInit, "recipe_bucketing_mode_init_pc_gatherer",
                 provideMode(recipeBucketingModeInitConstant)},
                {recipeBucketingModeBucket, "recipe_bucketing_mode_bucket_pc_gatherer",
                 provideMode(recipeBucketingModeBucketConstant)},
                {recipeBucketingModeFinal, "recipe_bucketing_mode_final_pc_gatherer",
                 provideMode(recipeBucketingModeFinalConstant)},
            },
            bucketingCommon, bucketingProviders, {});

        // S3 observer: census the three mode stages. Known-underivable case —
        // the shared interface's SSBOs are plain read-write, so the derivation
        // will propose all-pairs edges where the hand W/R split encodes
        // per-`mode` roles; the report exists to MEASURE exactly this.
        {
            const SdiFeatureSet bucketingNoFeatures;
            for (NodeHandle censusStage : {recipeBucketingModeInit,
                                           recipeBucketingModeBucket,
                                           recipeBucketingModeFinal}) {
                CensusStageFromSdi<BucketSdi::Metadata, BucketSdi::MEMBERS>(
                    sdiHazardCensus_, censusStage, bucketingProviders,
                    bucketingNoFeatures);
            }
        }

        // Auto-sync hazard declarations: mode-init/mode-bucket WRITE the counters/indices/
        // coverage buffers; mode-final READS the (by-then-final) coverage extrema and WRITES
        // the indirect-command buffer. Declaring these bakes the required
        // modeInit -> modeBucket -> modeFinal memory-visibility edges (FrameSyncScheduler),
        // exactly the "auto-sync P4" mechanism DirectLighting->SpatialReuse's own reservoir
        // hand-off (above) already relies on -- no hand-rolled vkCmdPipelineBarrier2 needed.
        batch.Connect(recipeBucketCountBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                      recipeBucketingBufSyncW, 0, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(recipeBucketIndicesBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                      recipeBucketingBufSyncW, 1, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(recipeBucketCoverageMinXBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                      recipeBucketingBufSyncW, 2, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(recipeBucketCoverageMinYBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                      recipeBucketingBufSyncW, 3, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(recipeBucketCoverageMaxXBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                      recipeBucketingBufSyncW, 4, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(recipeBucketCoverageMaxYBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                      recipeBucketingBufSyncW, 5, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        // Load-Tier Contract M2 (precision tier): mode-init/mode-bucket also WRITE the precision
        // sub-bucket pair (same hazard-declaration reasoning as the plain recipe bucket above).
        batch.Connect(recipePrecisionBucketCountBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                      recipeBucketingBufSyncW, 6, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(recipePrecisionBucketIndicesBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                      recipeBucketingBufSyncW, 7, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(recipeBucketingBufSyncW, BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                      recipeBucketingModeBucket, ComputeStageNodeConfig::BUFFER_WRITE_ARRAY,
                      SlotRoleModifier(SlotRole::Execute));

        batch.Connect(recipeBucketCoverageMinXBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                      recipeBucketingBufSyncR, 0, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(recipeBucketCoverageMinYBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                      recipeBucketingBufSyncR, 1, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(recipeBucketCoverageMaxXBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                      recipeBucketingBufSyncR, 2, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(recipeBucketCoverageMaxYBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                      recipeBucketingBufSyncR, 3, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(recipeBucketingBufSyncR, BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                      recipeBucketingModeFinal, ComputeStageNodeConfig::BUFFER_READ_ARRAY,
                      SlotRoleModifier(SlotRole::Execute));
        // mode-final also WRITES the indirect-command buffer -- a second write-array gatherer
        // on the SAME modeFinal node would collide with BUFFER_WRITE_ARRAY's single-slot shape
        // used by modeBucket above; instead declare it via a dedicated 1-entry gatherer.
        NodeHandle recipeBucketingFinalWriteGatherer =
            renderGraph->AddNode<BufferSyncGathererNodeType>("recipe_bucketing_final_write_gatherer");
        batch.Connect(recipeBucketIndirectCommandBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                      recipeBucketingFinalWriteGatherer, 0, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(recipeBucketingFinalWriteGatherer, BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                      recipeBucketingModeFinal, ComputeStageNodeConfig::BUFFER_WRITE_ARRAY,
                      SlotRoleModifier(SlotRole::Execute));

        // --- Specialized per-recipe indirect dispatch (VkPipeline/VkDescriptorSet built
        // outside the graph by PreTick per hot recipeId -- see the node-creation comment above
        // for why no static ShaderLibraryNode/DescriptorSetNode/ComputePipelineNode chain exists
        // for this path) ---
        batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      recipeSpecializedDispatch, MultiDispatchNodeConfig::VULKAN_DEVICE_IN)
             .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL,
                      recipeSpecializedDispatch, MultiDispatchNodeConfig::COMMAND_POOL)
             .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                      recipeSpecializedDispatch, MultiDispatchNodeConfig::SWAPCHAIN_INFO)
             .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX,
                      recipeSpecializedDispatch, MultiDispatchNodeConfig::IMAGE_INDEX)
             .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                      recipeSpecializedDispatch, MultiDispatchNodeConfig::CURRENT_FRAME_INDEX)
             .Connect(frameSyncNode, FrameSyncNodeConfig::IN_FLIGHT_FENCE,
                      recipeSpecializedDispatch, MultiDispatchNodeConfig::IN_FLIGHT_FENCE)
             .Connect(frameSyncNode, FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY,
                      recipeSpecializedDispatch, MultiDispatchNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY)
             .Connect(swapChainNode, SwapChainNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY,
                      recipeSpecializedDispatch, MultiDispatchNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY)
             .Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_SEMAPHORE,
                      recipeSpecializedDispatch, MultiDispatchNodeConfig::TIMELINE_SEMAPHORE_IN)
             .Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_FRAME_BASE,
                      recipeSpecializedDispatch, MultiDispatchNodeConfig::TIMELINE_FRAME_BASE_IN);

        // instanceSkipMaskBuffer is populated with REAL content for the first time by PreTick
        // (VulkanGraphApplication.cpp) when this flag is set -- no additional graph wiring is
        // needed here since Inc4 M1's fix round already wired binding 35 into all 4 gatherers;
        // only the buffer's CONTENT changes (via MapForReadback/UnmapReadback, same "de-facto
        // upload" pattern the DDGI leak-gate debug buffer already uses).
    }

    // --- Raster-proxy B1 M4: occlusion-probe chain connections (VIXEN_B1_OCCLUSION_CULL) ---
    // Frame order baked by resource hazards alone: HiZ writes the tile image the cull reads
    // (ImageSync pair) and the cull writes the skip-mask buffer every existing reader already
    // has in its read arrays — so HiZ → cull → march/lighting without any hand-rolled edges.
    // The depth ping-pong needs NO edges at all (distinct VkImage per parity slot).
    if (b1OcclusionCullEnabled) {
        // Depth ping-pong provider: extent follows the render target, parity follows FrameSync.
        batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      b1DepthTarget, DepthTargetNodeConfig::VULKAN_DEVICE_IN)
             .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL,
                      b1DepthTarget, DepthTargetNodeConfig::COMMAND_POOL)
             .Connect(renderTargetNode, RenderTargetNodeConfig::WIDTH_OUT,
                      b1DepthTarget, DepthTargetNodeConfig::WIDTH,
                      SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute))
             .Connect(renderTargetNode, RenderTargetNodeConfig::HEIGHT_OUT,
                      b1DepthTarget, DepthTargetNodeConfig::HEIGHT,
                      SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute))
             .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                      b1DepthTarget, DepthTargetNodeConfig::CURRENT_FRAME_INDEX);

        // Tile image provider (a generic ProbeAtlasNode instance, R32F tilesX x tilesY).
        batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      b1HizTileImage, ProbeAtlasNodeConfig::VULKAN_DEVICE_IN)
             .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL,
                      b1HizTileImage, ProbeAtlasNodeConfig::COMMAND_POOL);

        // Semantic-wiring S2 synthesis: the entire gatherer/descriptor-set/
        // pipeline plumbing of both B1 stages — node creation, quintet chain,
        // stage common inputs, push plumbing, AND the SDI member wires — is
        // synthesized from each stage's merged SDI (below, after the
        // providers are registered).
        const SdiStageCommon b1Common{deviceNode, commandPoolNode,
                                      swapChainNode, frameSyncNode};

        // Semantic-wiring S1: slot indices come from the feature-tagged merged SDI
        // (generated/sdi/merged/*-SDI.h) — names, not hand-written numbers. The
        // constants are drift-gated by ctest sdi_merged_drift_check.
        namespace HizSdi = ShaderInterface::HiZDownsample;
        namespace CullSdi = ShaderInterface::InstanceOcclusionCull;

        // Semantic-wiring S2: the shader is the declaration — the builder declares
        // only PROVIDERS (shader member name -> world node output + roles), once,
        // regardless of how many stages consume them (the tile view feeds BOTH the
        // HiZ writer and the cull reader from one registration). WireStageFromSdi
        // walks each stage's merged-SDI MEMBERS table and emits the exact Connects
        // the hand block used to write; an unmatched member is a configure-time
        // hard error naming the shader, the member, and the candidates.
        SdiProviderRegistry b1Providers;
        // HiZ inputs: last frame's depth (re-emitted per frame, Execute role like
        // the march's pickId binding) + the render target's live extent.
        b1Providers.Provide("srcDepthImage", b1DepthTarget,
                            DepthTargetNodeConfig::DEPTH_READ_VIEW, SlotRole::Execute);
        b1Providers.Provide("srcWidth", renderTargetNode,
                            RenderTargetNodeConfig::WIDTH_OUT,
                            SlotRole::Dependency | SlotRole::Execute);
        b1Providers.Provide("srcHeight", renderTargetNode,
                            RenderTargetNodeConfig::HEIGHT_OUT,
                            SlotRole::Dependency | SlotRole::Execute);
        // The tile image's view: written by HiZ, read by the cull — one provider.
        b1Providers.Provide("tileMaxImage", b1HizTileImage,
                            ProbeAtlasNodeConfig::CURRENT_VIEW, SlotRole::Execute);
        // Cull inputs: instances + configs (the march's own sources), the EXISTING
        // skip-mask buffer, and the ONE-FRAME-DELAYED camera ConstantNodes that
        // RunB1OcclusionCullPreTick refreshes.
        b1Providers.Provide("BodyInstanceBuffer", bodyOctreeSceneNode,
                            BodyOctreeSceneNodeConfig::INSTANCE_BUFFER,
                            SlotRole::Dependency | SlotRole::Execute);
        b1Providers.Provide("OctreeConfigsSSBO", bodyOctreeSceneNode,
                            BodyOctreeSceneNodeConfig::OCTREE_CONFIG_BUFFER,
                            SlotRole::Dependency | SlotRole::Execute);
        b1Providers.Provide("InstanceSkipMaskBuffer", instanceSkipMaskBuffer,
                            StorageBufferNodeConfig::STORAGE_BUFFER,
                            SlotRole::Dependency | SlotRole::Execute);
        b1Providers.Provide("prevViewProj", b1CullPrevViewProjConstant,
                            ConstantNodeConfig::OUTPUT, SlotRole::Execute);
        b1Providers.Provide("prevCamPos", b1CullPrevCamPosConstant,
                            ConstantNodeConfig::OUTPUT, SlotRole::Execute);
        b1Providers.Provide("dims", b1CullDimsConstant,
                            ConstantNodeConfig::OUTPUT, SlotRole::Execute);

        // Typed feature set: identity is the GLSL define, requirements (when a
        // feature gains any) reference the CapabilityGraph. Both B1 interfaces
        // are unconditional today; the set matters the moment either shader
        // grows a feature-gated member.
        SdiFeatureSet b1Features;
        b1Features.Enable(kFeatureB1OcclusionCull);
        const auto hizSynth = SynthesizeComputeStage<HizSdi::Metadata, HizSdi::MEMBERS>(
            renderGraph, batch, "b1_hiz",
            b1HizShaderLib, b1HizStage, b1Common, b1Providers, b1Features);
        b1HizDescGatherer  = hizSynth.descGatherer;
        b1HizPushGatherer  = hizSynth.pushGatherer;
        b1HizDescriptorSet = hizSynth.descriptorSet;
        b1HizPipeline      = hizSynth.pipeline;
        const auto cullSynth = SynthesizeComputeStage<CullSdi::Metadata, CullSdi::MEMBERS>(
            renderGraph, batch, "b1_cull",
            b1CullShaderLib, b1CullStage, b1Common, b1Providers, b1Features);
        b1CullDescGatherer  = cullSynth.descGatherer;
        b1CullPushGatherer  = cullSynth.pushGatherer;
        b1CullDescriptorSet = cullSynth.descriptorSet;
        b1CullPipeline      = cullSynth.pipeline;

        // S3 observer: census both B1 stages' members (access x provider
        // source) for the derived-hazard report (VIXEN_SDI_HAZARD_REPORT).
        CensusStageFromSdi<HizSdi::Metadata, HizSdi::MEMBERS>(
            sdiHazardCensus_, b1HizStage, b1Providers, b1Features);
        CensusStageFromSdi<CullSdi::Metadata, CullSdi::MEMBERS>(
            sdiHazardCensus_, b1CullStage, b1Providers, b1Features);

        // Ordering hazards: HiZ writes the tile image the cull reads (same-frame RAW), and
        // the cull writes the skip-mask buffer the march + lighting passes already read.
        batch.Connect(b1HizTileImage, ProbeAtlasNodeConfig::PROBE_ATLAS,
                      b1HizTileWriteGatherer, 0, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(b1HizTileWriteGatherer, ImageSyncGathererNodeConfig::IMAGE_ARRAY,
                      b1HizStage, ComputeStageNodeConfig::IMAGE_WRITE_ARRAY, SlotRoleModifier(SlotRole::Execute));
        batch.Connect(b1HizTileImage, ProbeAtlasNodeConfig::PROBE_ATLAS,
                      b1CullTileReadGatherer, 0, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(b1CullTileReadGatherer, ImageSyncGathererNodeConfig::IMAGE_ARRAY,
                      b1CullStage, ComputeStageNodeConfig::IMAGE_READ_ARRAY, SlotRoleModifier(SlotRole::Execute));
        batch.Connect(instanceSkipMaskBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                      b1CullMaskWriteGatherer, 0, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(b1CullMaskWriteGatherer, BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                      b1CullStage, ComputeStageNodeConfig::BUFFER_WRITE_ARRAY, SlotRoleModifier(SlotRole::Execute));
    }

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

    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  sceneRadianceNode, SceneRadianceNodeConfig::VULKAN_DEVICE_IN)
         .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL,
                  sceneRadianceNode, SceneRadianceNodeConfig::COMMAND_POOL)
         .Connect(renderTargetNode, RenderTargetNodeConfig::WIDTH_OUT,
                  sceneRadianceNode, SceneRadianceNodeConfig::WIDTH)
         .Connect(renderTargetNode, RenderTargetNodeConfig::HEIGHT_OUT,
                  sceneRadianceNode, SceneRadianceNodeConfig::HEIGHT);

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
    // against probeApplyNode's IMAGE_WRITE_ARRAY writer (W1a: the split's atlas
    // writer, formerly probeUpdateNode) on those SAME atlas Resource*s.
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

    // W3b: the hit-accumulation table — device only, NO SWAPCHAIN_INFO (fixed
    // PARAM_SIZE_BYTES sizing; capacity is the shader's own constant, not
    // extent-driven). The W1a missing-vulkan_device lesson, applied at
    // creation time rather than found at boot.
    if (hitAccumEnabled) {
        batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      hitAccumTableBuffer, StorageBufferNodeConfig::VULKAN_DEVICE_IN);
        // B2 (batch-23): hitAccumParamsBuffer is now a HitAccumParamsConfigNode
        // (ring-buffered) — same device + CURRENT_FRAME_INDEX wiring pattern as
        // shadowConfigNode above.
        batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      hitAccumParamsBuffer, HitAccumParamsConfigNodeConfig::VULKAN_DEVICE_IN)
             .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                      hitAccumParamsBuffer, HitAccumParamsConfigNodeConfig::CURRENT_FRAME_INDEX);
    }
    // W3c-2: the cell-radiance buffer — same shape (device only, fixed size).
    if (hitAccumResolveEnabled) {
        batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      hitAccumCellRadianceBuffer, StorageBufferNodeConfig::VULKAN_DEVICE_IN);
    }

    // Sampled Lighting Inc4 M4: DDGI leak-test gate debug buffer — device only, NO
    // SWAPCHAIN_INFO connection (fixed PARAM_SIZE_BYTES sizing set above, not extent-driven).
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  ddgiLeakGateDebugBuffer, StorageBufferNodeConfig::VULKAN_DEVICE_IN);

    // Recipe-Live-App-Bucketed-Dispatch Inc4 M1 fix round: instance skip mask buffer — device
    // only, NO SWAPCHAIN_INFO connection (fixed PARAM_SIZE_BYTES sizing set above, same shape as
    // ddgiLeakGateDebugBuffer immediately above).
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  instanceSkipMaskBuffer, StorageBufferNodeConfig::VULKAN_DEVICE_IN);

    // E11-T1: PolicyStencilTileBuffer — device only, same fixed-size (NO
    // SWAPCHAIN_INFO) shape as instanceSkipMaskBuffer immediately above.
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  policyStencilTileBuffer, StorageBufferNodeConfig::VULKAN_DEVICE_IN);

    // W1a: shadow-ray queue/payload buffers — device only, same fixed-size (NO
    // SWAPCHAIN_INFO) shape as ddgiLeakGateDebugBuffer above (slot-geometry
    // PARAM_SIZE_BYTES sizing set at the dispatch section). NOTE: this block is
    // the historical miss-spot — the Load-Tier M2 pair landed creations/
    // providers/hazards but skipped THIS device connect and aborted at graph
    // validation exactly like these three did on W1a's first boot.
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  shadowRayRequestBuffer, StorageBufferNodeConfig::VULKAN_DEVICE_IN);
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  shadowRayResultBuffer, StorageBufferNodeConfig::VULKAN_DEVICE_IN);
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  probeRayPayloadBuffer, StorageBufferNodeConfig::VULKAN_DEVICE_IN);

    // Recipe-Live-App-Bucketed-Dispatch Inc4 M3: bucketing-pass SSBOs — device only, same
    // fixed-size (NO SWAPCHAIN_INFO) shape as instanceSkipMaskBuffer/ddgiLeakGateDebugBuffer
    // above (all sized by kRecipeBucketingMaxBuckets, not by swapchain extent). Includes the
    // Load-Tier M2 precision pair — MISSED here when M2 landed (creations/providers/hazards
    // had it, this block didn't), which made every flag-on boot abort at graph validation
    // ("recipe_precision_bucket_count_buffer missing vulkan_device"); found + fixed slice C.
    if (recipeBucketedDispatchEnabled) {
        batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      recipeBoundSphereBuffer, StorageBufferNodeConfig::VULKAN_DEVICE_IN)
             .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      recipeBucketCountBuffer, StorageBufferNodeConfig::VULKAN_DEVICE_IN)
             .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      recipeBucketIndicesBuffer, StorageBufferNodeConfig::VULKAN_DEVICE_IN)
             .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      recipeBucketCoverageMinXBuffer, StorageBufferNodeConfig::VULKAN_DEVICE_IN)
             .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      recipeBucketCoverageMinYBuffer, StorageBufferNodeConfig::VULKAN_DEVICE_IN)
             .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      recipeBucketCoverageMaxXBuffer, StorageBufferNodeConfig::VULKAN_DEVICE_IN)
             .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      recipeBucketCoverageMaxYBuffer, StorageBufferNodeConfig::VULKAN_DEVICE_IN)
             .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      recipeBucketIndirectCommandBuffer, StorageBufferNodeConfig::VULKAN_DEVICE_IN)
             .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      recipePrecisionBucketCountBuffer, StorageBufferNodeConfig::VULKAN_DEVICE_IN)
             .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      recipePrecisionBucketIndicesBuffer, StorageBufferNodeConfig::VULKAN_DEVICE_IN)
             .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      recipeBucketMetaBuffer, StorageBufferNodeConfig::VULKAN_DEVICE_IN);
    }

    // Connect push constant fields to push constant gatherer using member extraction
    // CameraNode now outputs a CameraData struct, so we can extract individual fields
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          pushConstantGatherer, MarchSdi::Push::cameraPos::INDEX,  // vec3 cameraPos
                          ExtractField(&CameraData::cameraPos, SlotRole::Execute));  // Mark as Execute-only

    // Note: time field (index 1) NOT connected - will be filled with zero by gatherer
    // This will trigger a warning log but shader will receive valid (zero) value
    // TODO: Connect actual time source when animation is needed

    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          pushConstantGatherer, MarchSdi::Push::cameraDir::INDEX,  // vec3 cameraDir
                          ExtractField(&CameraData::cameraDir, SlotRole::Execute));  // Mark as Execute-only
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          pushConstantGatherer, MarchSdi::Push::fov::INDEX,  // float fov
                          ExtractField(&CameraData::fov, SlotRole::Execute));  // Mark as Execute-only
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          pushConstantGatherer, MarchSdi::Push::cameraUp::INDEX,  // vec3 cameraUp
                          ExtractField(&CameraData::cameraUp, SlotRole::Execute));  // Mark as Execute-only
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          pushConstantGatherer, MarchSdi::Push::aspect::INDEX,  // float aspect
                          ExtractField(&CameraData::aspect, SlotRole::Execute));  // Mark as Execute-only
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          pushConstantGatherer, MarchSdi::Push::cameraRight::INDEX,  // vec3 cameraRight
                          ExtractField(&CameraData::cameraRight, SlotRole::Execute));  // Mark as Execute-only

    // Connect debugMode from InputState to push constant gatherer for debug visualization
    // Press 0-9 keys to switch between visualization modes at runtime
    batch.Connect(inputNode, InputNodeConfig::INPUT_STATE,
                          pushConstantGatherer, MarchSdi::Push::debugMode::INDEX,  // int debugMode
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
                              pushConstantGatherer, MarchSdi::Push::raySizeCoef::INDEX,  // push constant field 8: float raySizeCoef
                              SlotRoleModifier(SlotRole::Execute));
    } else {
        batch.Connect(raySizeCoefNode, RaySizeCoefNodeConfig::RAY_SIZE_COEF,
                              pushConstantGatherer, MarchSdi::Push::raySizeCoef::INDEX,  // push constant field 8: float raySizeCoef
                              SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    }
    // raySizeBias (binding 9): LOD origin cone size; 0.0 for pinhole camera.
    batch.Connect(raySizeBiasConstant, ConstantNodeConfig::OUTPUT,
                          pushConstantGatherer, MarchSdi::Push::raySizeBias::INDEX,  // push constant field 9: float raySizeBias
                          SlotRoleModifier(SlotRole::Execute));
    // instanceCount (binding 10): number of valid entries in bodyInstances[]; from BodyOctreeSceneNode.
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::INSTANCE_COUNT,
                          pushConstantGatherer, MarchSdi::Push::instanceCount::INDEX,  // push constant field 10: int instanceCount
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    // debugTargetPixel (binding 11): TEMP DEBUG — last left-click pixel, so the ray-trace debug
    // buffer (TraceRecording.glsl) force-captures that exact ray regardless of DEBUG_GRID_SPACING.
    batch.Connect(inputNode, InputNodeConfig::INPUT_STATE,
                          pushConstantGatherer, MarchSdi::Push::debugTargetPixel::INDEX,  // push constant field 11: ivec2 debugTargetPixel
                          ExtractField(&InputState::lastClickPixel, SlotRole::Execute));
    // accumFrameCount (binding 12, Sampled Lighting Inc2 M2): consecutive-static-camera frame
    // counter from AccumulationConfigNode's own reset-on-motion tracking; drives the shader's
    // converging-1/N accumulate-seam alpha.
    batch.Connect(accumulationConfigNode, AccumulationConfigNodeConfig::FRAME_COUNTER,
                          pushConstantGatherer, MarchSdi::Push::accumFrameCount::INDEX,  // push constant field 12: uint accumFrameCount
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    // cosmicK (binding 13, regime-3 cosmic accumulation first slice): the K threshold literal.
    // Primary march gatherer only in this slice -- regime 3 is not yet wired into the RT/composed
    // or secondary-ray shader programs (their PushConstantGathererNode instances below stay
    // unconnected for this field; harmless, VIXEN_REGIME3 is off there too).
    batch.Connect(regime3KConstant, ConstantNodeConfig::OUTPUT,
                          pushConstantGatherer, MarchSdi::Push::cosmicK::INDEX,  // push constant field 13: float cosmicK
                          SlotRoleModifier(SlotRole::Execute));

    // Connect ray marching resources to the descriptor gatherer using the merged-SDI
    // named constants (MarchSdi::Bind::*, generated/sdi/merged/BodyInstanceRayMarch-SDI.h).
    // Binding 0: outputImage - Transient (Execute-only), others are Persistent (Dependency|Execute)
    // Binding 0: outputImage — M4: now the offscreen render target's view, wired further down
    // (beside the rest of the M4 render-target connections) once renderTargetNode exists in scope.

    // M-wire Task 8: bindings 1/2/3/5 now come from BodyOctreeSceneNode (sparse shell octrees).
    // Slot names are identical to VoxelGridNode's octree outputs (by design in BodyOctreeSceneNodeConfig).
    // The shader's esvoNodes/brickData/materials/OctreeConfigsUBO at these bindings are now the
    // concatenated per-kind shell octrees, NOT the dense 128^3 grid.

    // Semantic-wiring S2: the SHARED scene-provider registry, declared here at
    // its FIRST consumer (the march) and extended by the lighting sections
    // below. Every provider is a shader-declared member name -> world node
    // output + roles, registered once regardless of how many passes consume it.
    //
    // Provenance preserved from the hand block: bindings 1/2/3/5 come from
    // BodyOctreeSceneNode (concatenated per-kind SHELL octrees, not the dense
    // grid — M-wire Task 8). RayTraceBuffer stays on voxelGridNode (it owns
    // the debug-capture buffer; VoxelGridNode stays in-graph for this).
    // ChannelPool/BrickLookup are the Surface-Shell COMPACT pool + remap
    // (drop-in swap; placeholder 1-byte for binary/procedural bodies).
    // idOutputImage/historyImage/outputImage are per-frame re-emitted views
    // (Execute-only); outputImage is bound to the march SOLELY for
    // imageSize() (never imageStore'd post-split — PARAM_WRITES_NO_IMAGE).
    // InstanceSkipMask is read unconditionally by isInstanceSkipped(); the
    // zeroed placeholder makes it a true no-op until the cull fills it.
    // Binding 8 (ShaderCounters) is compiled out of the shader unconditionally
    // — no member, no wire (the original binding-8 lesson: wiring an
    // undeclared binding is a validation error).
    SdiProviderRegistry sceneProviders;
    const auto kSceneRoles = SlotRole::Dependency | SlotRole::Execute;
    sceneProviders.Provide("ESVOBuffer", bodyOctreeSceneNode,
                           BodyOctreeSceneNodeConfig::OCTREE_NODES_BUFFER, kSceneRoles);
    sceneProviders.Provide("BrickBuffer", bodyOctreeSceneNode,
                           BodyOctreeSceneNodeConfig::OCTREE_BRICKS_BUFFER, kSceneRoles);
    sceneProviders.Provide("MaterialBuffer", bodyOctreeSceneNode,
                           BodyOctreeSceneNodeConfig::OCTREE_MATERIALS_BUFFER, kSceneRoles);
    sceneProviders.Provide("OctreeConfigsSSBO", bodyOctreeSceneNode,
                           BodyOctreeSceneNodeConfig::OCTREE_CONFIG_BUFFER, kSceneRoles);
    sceneProviders.Provide("BodyInstanceBuffer", bodyOctreeSceneNode,
                           BodyOctreeSceneNodeConfig::INSTANCE_BUFFER, kSceneRoles);
    sceneProviders.Provide("ChannelPoolBuffer", bodyOctreeSceneNode,
                           BodyOctreeSceneNodeConfig::SHELL_DATA_BUFFER, kSceneRoles);
    sceneProviders.Provide("BrickLookupBuffer", bodyOctreeSceneNode,
                           BodyOctreeSceneNodeConfig::SHELL_LOOKUP_BUFFER, kSceneRoles);
    sceneProviders.Provide("MipPoolBuffer", bodyOctreeSceneNode,
                           BodyOctreeSceneNodeConfig::OCTREE_MIPPOOL_BUFFER, kSceneRoles);
    sceneProviders.Provide("TierRefTableBuffer", bodyOctreeSceneNode,
                           BodyOctreeSceneNodeConfig::OCTREE_TIERREFTABLE_BUFFER, kSceneRoles);
    // W-RTQUERY Slice A: TLAS for the ray_query traversal backend (binding 40,
    // RayQueryTraversal.glsl). Provider name MUST match the GLSL identifier exactly
    // (same convention as "depthDistanceImage" below matching its own GLSL name) --
    // the SDI reflector keys members by the shader-side identifier, not an arbitrary
    // label. VK_NULL_HANDLE placeholder when VIXEN_RTQUERY_TRAVERSAL is unset or the
    // device lacks RTXCapabilities.rayQuery (BodyOctreeSceneNode::EnsureRtQueryTlasBuilt's
    // guard); the member itself is feature-filtered out of the plan entirely unless
    // marchFeatures.Enable(kFeatureRtQueryTraversal) below is also set, so this
    // registration is inert (never consulted) on a flag-off build.
    sceneProviders.Provide("rtQueryTlas", bodyOctreeSceneNode,
                           BodyOctreeSceneNodeConfig::RTQUERY_TLAS, kSceneRoles);
    sceneProviders.Provide("historyImage", accumulationHistoryNode,
                           AccumulationHistoryNodeConfig::HISTORY_IMAGE_VIEW,
                           SlotRole::Execute);
    sceneProviders.Provide("OccupancyGridBuffer", bodyOctreeSceneNode,
                           BodyOctreeSceneNodeConfig::OCTREE_OCCUPANCYGRID_BUFFER, kSceneRoles);
    sceneProviders.Provide("RayTraceBuffer", voxelGridNode,
                           VoxelGridNodeConfig::DEBUG_CAPTURE_BUFFER,
                           SlotRole::Dependency | SlotRole::Execute | SlotRole::Debug);
    sceneProviders.Provide("idOutputImage", pickIdTargetNode,
                           PickIdTargetNodeConfig::ID_IMAGE_VIEW, SlotRole::Execute);
    sceneProviders.Provide("sceneRadianceImage", sceneRadianceNode,
                           SceneRadianceNodeConfig::HISTORY_IMAGE_VIEW, SlotRole::Execute);
    sceneProviders.Provide("sceneRadianceHistory", accumulationHistoryNode,
                           AccumulationHistoryNodeConfig::HISTORY_IMAGE_VIEW, SlotRole::Execute);
    // Legacy march interface remains registered for the flag-off/identity path;
    // the HDR seam has a distinct name in SpatialReuseShade.
    sceneProviders.Provide("outputImage", renderTargetNode,
                           RenderTargetNodeConfig::CURRENT_VIEW, SlotRole::Execute);
    sceneProviders.Provide("InstanceSkipMaskBuffer", instanceSkipMaskBuffer,
                           StorageBufferNodeConfig::STORAGE_BUFFER, kSceneRoles);
    // E11-T1: written by BodyInstanceRayMarch (tile reduction), read by
    // ShadowVisibilityWave (evaluator-skip) -- same read-write-shared shape as
    // HitRecordBuffer below, gated by VIXEN_POLICY_STENCIL_TILES's feature set. This
    // Provide() call is purely a descriptor-binding registration (the SDI wiring path) --
    // it does NOT feed ResourceAccessTracker. KI-052 fix (E12-T1): the actual hazard
    // registration is the SEPARATE policyStencilTileWriteGatherer/shadowVisibilityWave-
    // ReadGatherer wiring below (search "KI-052 fix"), which is what bakes the march->wave
    // SyncEdge this Provide() call alone never could.
    sceneProviders.Provide("PolicyStencilTileBuffer", policyStencilTileBuffer,
                           StorageBufferNodeConfig::STORAGE_BUFFER, kSceneRoles);
    sceneProviders.Provide("LightingConfigSSBO", lightingConfigNode,
                           LightingConfigNodeConfig::LIGHTING_CONFIG_BUFFER, kSceneRoles);
    sceneProviders.Provide("HitRecordBuffer", hitRecordBufferNode,
                           StorageBufferNodeConfig::STORAGE_BUFFER, kSceneRoles);
    sceneProviders.Provide("ShadowConfigSSBO", shadowConfigNode,
                           ShadowConfigNodeConfig::SHADOW_CONFIG_BUFFER, kSceneRoles);
    sceneProviders.Provide("AccumulationConfigSSBO", accumulationConfigNode,
                           AccumulationConfigNodeConfig::ACCUMULATION_CONFIG_BUFFER, kSceneRoles);
    sceneProviders.Provide("PrevCameraConfigSSBO", prevCameraConfigNode,
                           PrevCameraConfigNodeConfig::PREV_CAMERA_CONFIG_BUFFER, kSceneRoles);

    // The march's feature set: B1 enables the gated depthDistanceImage member
    // (binding 36) AND registers its provider — the builder-side `if` now
    // gates only the FEATURE + provider; the Connect itself is derived from
    // the member table (the epoch's original promise for this exact site).
    SdiFeatureSet marchFeatures;
    if (b1OcclusionCullEnabled) {
        marchFeatures.Enable(kFeatureB1OcclusionCull);
        sceneProviders.Provide("depthDistanceImage", b1DepthTarget,
                               DepthTargetNodeConfig::DEPTH_WRITE_VIEW,
                               SlotRole::Execute);
    }
    // W-RTQUERY Slice A: gate the rtQueryTlas member (binding 40) the SAME way
    // B1's depthDistanceImage gates binding 36 above -- the merged-SDI MEMBERS table
    // is feature-filtered, so marchFeatures must carry this flag or the plan silently
    // omits the member (RtQueryTLAS's provider registration above then goes unused,
    // not an error) even though marchShaderFeatures already compiled the variant in.
    // W-COMPOSED: whether the composed builder (RegisterShaderBuilder lambda
    // above) actually picks RT or DDA isn't known until Compile (device
    // capability); wiring the binding here (graph-construction time, before
    // Compile) can't wait for that answer. Wire rtQueryTlas whenever composed
    // traversal is REQUESTED, same as the plain VIXEN_RTQUERY_TRAVERSAL gate --
    // an unused binding when composed resolves to DDA is inert (same precedent
    // as the comment above: provider registered-but-unused is not an error).
    if (envFlagEnabled("VIXEN_RTQUERY_TRAVERSAL") || composedTraversalRequested) {
        marchFeatures.Enable(kFeatureRtQueryTraversal);
    }
    // E11-T1: gates the PolicyStencilTileBuffer member (binding 41) the same
    // way B1's depthDistanceImage gates binding 36 above -- marchShaderFeatures
    // (shader compile) and marchFeatures (SDI wire plan) must both carry this
    // flag or the compiled variant and the wire plan disagree.
    if (policyStencilTilesEnabled) {
        marchFeatures.Enable(kFeaturePolicyStencil);
        marchFeatures.Enable(kFeaturePolicyStencilTiles);
    }

    WireStageFromSdi<MarchSdi::Metadata, MarchSdi::MEMBERS>(
        batch, sceneProviders, descriptorGatherer,
        /*pushGatherer (unused in this scope)*/ descriptorGatherer,
        marchFeatures, SdiWireSet::DescriptorsOnly);

    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] March descriptors wired from merged SDI (sceneProviders registry)");
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

    // DirectLighting's descriptor bindings: the scene SSBOs (octree/brick/instance/shell/mip/
    // tier-ref) are READ-ONLY in both the march and DirectLighting — read-read is not a hazard, so
    // they're wired the same way as the march's own gatherer (plain DescriptorResourceGathererNode
    // bindings, no sync slot needed), mirroring bindings 1/2/3/5/10/11/12/13/15/16/18/19/20/21
    // above. Binding 17 (HitRecord) is the genuine cross-submit hazard — wired below via the sync
    // slots, not here. Binding 0 (outputImage) is the genuine write hazard — also wired below via
    // IMAGE_WRITE, not here (it needs the render target's CURRENT view, same as the march's own
    // binding-0 wiring further up).
    // Semantic-wiring S2: the SHARED scene-provider registry — every member of
    // DirectLighting's interface resolved by its shader-declared name from the
    // merged SDI (binding-number divergence vs the march — LightingConfig 16
    // vs 17, HitRecord 17 vs 18 — is absorbed by the names).
    //
    // Provenance preserved from the hand block: InstanceSkipMask is REQUIRED
    // here because DirectLighting.comp #includes SceneBindings.glsl (KI-018)
    // even though this shader never calls isInstanceSkipped(). worldPosImage
    // (KI-023) is the accumulate seam's self-contained read-then-write-back —
    // Execute-only, no sync slot. Reservoir A/B are BOTH always bound; the
    // shader picks current-vs-previous via reservoirConfig.frameParity&1 (the
    // M5 cross-dispatch hazard is declared separately via the sync gatherers,
    // per the descriptor-vs-sync-slot split). outputImage is read-only here
    // since M5 (imageSize() only — SpatialReuse owns the write). HitRecord's
    // cross-submit hazard is likewise declared via the sync-slot pair below.
    // The old binding-20 historyImage wire is GONE — the M5 pass split moved
    // historyImage to SpatialReuseShade (DirectLighting.comp:84) and DL's
    // compiled interface has no binding 20; the member table simply has no
    // such member (the dead wire the strict contract flagged).
    sceneProviders.Provide("ESVOBuffer", bodyOctreeSceneNode,
                           BodyOctreeSceneNodeConfig::OCTREE_NODES_BUFFER, kSceneRoles);
    sceneProviders.Provide("worldPosHistoryImage", worldPosHistoryNode,
                           WorldPosHistoryNodeConfig::WORLDPOS_IMAGE_VIEW,
                           SlotRole::Execute);
    sceneProviders.Provide("ReservoirConfigSSBO", reservoirConfigNode,
                           ReservoirConfigNodeConfig::RESERVOIR_CONFIG_BUFFER,
                           SlotRole::Dependency | SlotRole::Execute);
    sceneProviders.Provide("LightTreeBufferSSBO", lightTreeBufferNode,
                           LightTreeBufferNodeConfig::LIGHT_TREE_BUFFER,
                           SlotRole::Dependency | SlotRole::Execute);
    sceneProviders.Provide("ReservoirBufferA", reservoirBufferA,
                           StorageBufferNodeConfig::STORAGE_BUFFER,
                           SlotRole::Dependency | SlotRole::Execute);
    sceneProviders.Provide("ReservoirBufferB", reservoirBufferB,
                           StorageBufferNodeConfig::STORAGE_BUFFER,
                           SlotRole::Dependency | SlotRole::Execute);

    // Slice C: the whole quintet chain + stage commons + push plumbing is
    // synthesized (the hand blocks this section wrote are gone); descriptors
    // wire from the registry, PUSH stays hand-wired below pending the #18
    // `time` ruling. Feature set deliberately EMPTY: the one trace-gated
    // member (InstanceIterDebugBuffer, binding 14) has no app-side owner —
    // under VIXEN_DEBUG_CAPTURE it falls through to the DescriptorSetNode's
    // placeholder fallback exactly as before (byte-identical both modes);
    // providing a real buffer is the capture path's own future slice.
    const SdiStageCommon lightingCommon{deviceNode, commandPoolNode,
                                        swapChainNode, frameSyncNode};
    const auto dlSynth = SynthesizeComputeStage<DirectSdi::Metadata, DirectSdi::MEMBERS>(
        renderGraph, batch, "direct_lighting", directLightingShaderLib,
        directLightingNode, lightingCommon, sceneProviders, {},
        SdiWireSet::DescriptorsOnly);
    directLightingPushConstantGatherer = dlSynth.pushGatherer;

    // S3 observer: census the lighting stages (same empty feature set their
    // wire calls use — the binding-14 member is feature-gated OUT of the walk).
    // RayTraceBuffer is a measured false-positive class: deliberately
    // unconditional plain-`buffer` (variant-independent wiring) whose writes
    // exist only under VIXEN_GPU_TRACE_HOOKS — qualifier reflection reads RW
    // in every variant, so the census would derive spurious all-pairs edges.
    // Excluded by annotation; surfaced in the report's OPAQUE lines.
    const SdiFeatureSet sdiNoFeatures;
    const std::map<std::string, std::string> sdiCensusExclusions = {
        {"RayTraceBuffer", "trace-plumbing: define-gated writes"},
    };
    CensusStageFromSdi<DirectSdi::Metadata, DirectSdi::MEMBERS>(
        sdiHazardCensus_, directLightingNode, sceneProviders, sdiNoFeatures,
        &sdiCensusExclusions);

    // DirectLighting's own push-constant gatherer (synthesized above): SAME field sources as the
    // march's own gatherer (cameraPos/time/cameraDir/fov/cameraUp/aspect/cameraRight/debugMode/
    // raySizeCoef/raySizeBias/instanceCount/debugTargetPixel/accumFrameCount) — DirectLighting.comp
    // declares the identical PushConstants block (shared via SceneBindings.glsl), but glslang
    // reflects push-constant RANGES per-COMPILED-shader (dead-code-eliminated fields differ), so a
    // second compiled program needs its own PushConstantGathererNode instance, not the march's.
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          directLightingPushConstantGatherer, DirectSdi::Push::cameraPos::INDEX,
                          ExtractField(&CameraData::cameraPos, SlotRole::Execute));
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          directLightingPushConstantGatherer, DirectSdi::Push::cameraDir::INDEX,
                          ExtractField(&CameraData::cameraDir, SlotRole::Execute));
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          directLightingPushConstantGatherer, DirectSdi::Push::fov::INDEX,
                          ExtractField(&CameraData::fov, SlotRole::Execute));
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          directLightingPushConstantGatherer, DirectSdi::Push::cameraUp::INDEX,
                          ExtractField(&CameraData::cameraUp, SlotRole::Execute));
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          directLightingPushConstantGatherer, DirectSdi::Push::aspect::INDEX,
                          ExtractField(&CameraData::aspect, SlotRole::Execute));
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          directLightingPushConstantGatherer, DirectSdi::Push::cameraRight::INDEX,
                          ExtractField(&CameraData::cameraRight, SlotRole::Execute));
    batch.Connect(inputNode, InputNodeConfig::INPUT_STATE,
                          directLightingPushConstantGatherer, DirectSdi::Push::debugMode::INDEX,
                          ExtractField(&InputState::debugMode, SlotRole::Execute));
    // Baked-Perf M4b Task 4b.2: DirectLighting.comp issues shadow rays (TraceWorldShadow, M4's
    // any-hit chain) -- feed the secondary-ray coefficient instead of mirroring the primary
    // gatherer's raySizeCoefNode, so shadow-ray occlusion tests reach the mip-fallback path
    // (SceneBindings.glsl any-hit LOD gate) at a coarser threshold than primary rays. The
    // tier-crossing debug override still wins when active (same precedence as before this
    // change) -- it is a whole-scene debug knob, orthogonal to per-ray-type policy.
    if (tierCrossingLodCoefOverrideActive) {
        batch.Connect(tierCrossingLodCoefOverrideConstant, ConstantNodeConfig::OUTPUT,
                              directLightingPushConstantGatherer, 8,
                              SlotRoleModifier(SlotRole::Execute));
    } else {
        batch.Connect(secondaryRaySizeCoefConstant, ConstantNodeConfig::OUTPUT,
                              directLightingPushConstantGatherer, 8,
                              SlotRoleModifier(SlotRole::Execute));
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

    // ===================================================================
    // Sampled Lighting Inc3 M5: SpatialReuseNode wiring — the second half of the pass
    // split (spatial reservoir reuse + shade, owns outputImage/historyImage/
    // worldPosHistoryImage). Mirrors DirectLightingNode's own descriptor-path/push-
    // constant/gatherer-binding shape exactly (same scene SSBOs, same config buffers);
    // only the reservoir-buffer ROLE (read here vs write in DirectLighting) and the
    // image-write ownership differ.
    // ===================================================================

    // Semantic-wiring S2: SpatialReuseShade wires from the SAME sceneProviders
    // registry DirectLighting uses — its six pass-specific members extend the
    // registry here; everything shared resolves to the identical providers.
    //
    // Provenance preserved: historyImage + worldPosHistoryImage are owned
    // (read AND write) by this pass since the M5 split — Execute-only,
    // self-contained read/write-in-one-dispatch. Reservoir A/B are READ here
    // (DirectLighting is the sole writer; the cross-dispatch hazard is
    // declared via the array-hazard gatherer pair, not the descriptor). The
    // probe atlases/grid config are READ (probe_apply writes since W1a; hazard
    // via IMAGE_READ_ARRAY; CURRENT_VIEW per KI-028 — an IRenderTarget* can never
    // populate a descriptor slot). SpatialReservoirDebugBuffer is the M6
    // gate's host-readback-only instrument; DDGILeakGateDebugShadeSSBO is the
    // shared M5 live-gate buffer ProbeApply also binds at 31. HitRecord is a
    // SECOND reader of the already-synced march write (read-after-read).
    sceneProviders.Provide("SpatialReservoirDebugBuffer", spatialReservoirDebugBuffer,
                           StorageBufferNodeConfig::STORAGE_BUFFER,
                           SlotRole::Dependency | SlotRole::Execute);
    sceneProviders.Provide("probeIrradianceAtlasRead", probeIrradianceAtlasNode,
                           ProbeAtlasNodeConfig::CURRENT_VIEW, SlotRole::Execute);
    sceneProviders.Provide("probeVisibilityAtlasRead", probeVisibilityAtlasNode,
                           ProbeAtlasNodeConfig::CURRENT_VIEW, SlotRole::Execute);
    sceneProviders.Provide("ProbeGridConfigReadSSBO", probeGridConfigNode,
                           ProbeGridConfigNodeConfig::PROBE_GRID_CONFIG_BUFFER,
                           SlotRole::Dependency | SlotRole::Execute);
    sceneProviders.Provide("DDGILeakGateDebugShadeSSBO", ddgiLeakGateDebugBuffer,
                           StorageBufferNodeConfig::STORAGE_BUFFER,
                           SlotRole::Dependency | SlotRole::Execute);

    // Slice C: quintet chain + stage commons + push plumbing synthesized;
    // descriptors from the registry (same empty feature set as DL — the
    // binding-14 placeholder tolerance is deliberate); PUSH hand-wired below.
    // W-LEAN L3: the hit-accum providers land HERE — BEFORE the FIRST consumer
    // synthesis (the shade's cell-resolve fold; the wave synthesizes later) —
    // the providers-before-synthesis rule, third application.
    SdiFeatureSet srsSdiFeatures;
    if (hitAccumEnabled) {
        sceneProviders.Provide("HitAccumTable", hitAccumTableBuffer,
                               StorageBufferNodeConfig::STORAGE_BUFFER, SlotRole::Execute);
        sceneProviders.Provide("HitAccumParamsSSBO", hitAccumParamsBuffer,
                               HitAccumParamsConfigNodeConfig::HIT_ACCUM_PARAMS_BUFFER, SlotRole::Execute);
    }
    if (hitAccumResolveEnabled) {
        srsSdiFeatures.Enable(kFeatureSrsCellResolve);
        sceneProviders.Provide("HitAccumCellRadiance", hitAccumCellRadianceBuffer,
                               StorageBufferNodeConfig::STORAGE_BUFFER, SlotRole::Execute);
    }
    const auto reuseSynth = SynthesizeComputeStage<ReuseSdi::Metadata, ReuseSdi::MEMBERS>(
        renderGraph, batch, "spatial_reuse", spatialReuseShaderLib,
        spatialReuseNode, lightingCommon, sceneProviders, srsSdiFeatures,
        SdiWireSet::DescriptorsOnly);
    spatialReusePushConstantGatherer = reuseSynth.pushGatherer;
    CensusStageFromSdi<ReuseSdi::Metadata, ReuseSdi::MEMBERS>(
        sdiHazardCensus_, spatialReuseNode, sceneProviders, srsSdiFeatures,
        &sdiCensusExclusions);

    if (hdrExposureEnabled) {
    const auto exposureSynth = SynthesizeComputeStage<ShaderInterface::ExposureTonemap::Metadata,
                                                       ShaderInterface::ExposureTonemap::MEMBERS>(
        renderGraph, batch, "exposure_tonemap", exposureShaderLib, exposureNode,
        lightingCommon, sceneProviders, {});
    (void)exposureSynth;
    auto* exposureStage = static_cast<ComputeStageNode*>(renderGraph->GetInstance(exposureNode));
    exposureStage->SetParameter(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);
    exposureStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_X, 0u);
    exposureStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Y, 0u);
    exposureStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Z, 1u);
    }

    // SpatialReuse's own push-constant gatherer (synthesized above): SAME field sources as
    // DirectLighting's own gatherer (a third compiled program still needs its own reflected
    // push-constant ranges).
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          spatialReusePushConstantGatherer, ReuseSdi::Push::cameraPos::INDEX,
                          ExtractField(&CameraData::cameraPos, SlotRole::Execute));
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          spatialReusePushConstantGatherer, ReuseSdi::Push::cameraDir::INDEX,
                          ExtractField(&CameraData::cameraDir, SlotRole::Execute));
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          spatialReusePushConstantGatherer, ReuseSdi::Push::fov::INDEX,
                          ExtractField(&CameraData::fov, SlotRole::Execute));
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          spatialReusePushConstantGatherer, ReuseSdi::Push::cameraUp::INDEX,
                          ExtractField(&CameraData::cameraUp, SlotRole::Execute));
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          spatialReusePushConstantGatherer, ReuseSdi::Push::aspect::INDEX,
                          ExtractField(&CameraData::aspect, SlotRole::Execute));
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          spatialReusePushConstantGatherer, ReuseSdi::Push::cameraRight::INDEX,
                          ExtractField(&CameraData::cameraRight, SlotRole::Execute));
    batch.Connect(inputNode, InputNodeConfig::INPUT_STATE,
                          spatialReusePushConstantGatherer, ReuseSdi::Push::debugMode::INDEX,
                          ExtractField(&InputState::debugMode, SlotRole::Execute));
    // Baked-Perf M4b Task 4b.2: SpatialReuseShade.comp also issues shadow rays (TraceWorldShadow,
    // ReSTIR spatial-reuse shade pass) -- same secondary-ray coefficient as DirectLighting above,
    // not the primary gatherer's raySizeCoefNode. See that block's comment for rationale.
    if (tierCrossingLodCoefOverrideActive) {
        batch.Connect(tierCrossingLodCoefOverrideConstant, ConstantNodeConfig::OUTPUT,
                              spatialReusePushConstantGatherer, 8,
                              SlotRoleModifier(SlotRole::Execute));
    } else {
        batch.Connect(secondaryRaySizeCoefConstant, ConstantNodeConfig::OUTPUT,
                              spatialReusePushConstantGatherer, 8,
                              SlotRoleModifier(SlotRole::Execute));
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

    // W2a: the A/B read side moved to SpatialReuseGather with the fold — the
    // shade no longer declares those bindings at all (SDI 17→15). The gather's
    // own read gatherer (wired in the reservoir-path block below) carries the
    // neighbor-array hazard now; when the path is off, NOTHING reads A/B and
    // no read edge exists (DL's writes keep their gatherer — the buffers
    // persist either way).

    // Render target: SpatialReuseNode's IMAGE_WRITE <-> BlitNode's IMAGE_READ (wired below), same
    // renderTargetNode::RENDER_TARGET Resource* on both — bakes the SpatialReuse->BlitNode edge
    // (moved from DirectLightingNode, M5 — SpatialReuseNode is the genuine outputImage writer now).
    batch.Connect(renderTargetNode, RenderTargetNodeConfig::RENDER_TARGET,
                  spatialReuseNode, ComputeStageNodeConfig::IMAGE_WRITE, SlotRoleModifier(SlotRole::Execute));
    if (hdrExposureEnabled) {
        batch.Connect(renderTargetNode, RenderTargetNodeConfig::RENDER_TARGET,
                      exposureNode, ComputeStageNodeConfig::IMAGE_WRITE, SlotRoleModifier(SlotRole::Execute));
    }

    // Sampled Lighting Inc4 M5: SpatialReuseNode's IMAGE_READ_ARRAY <-> probeApplyNode's
    // IMAGE_WRITE_ARRAY (wired further below; W1a moved the atlas writer from the retired
    // probe_update megakernel to the split's apply half), same two ProbeAtlasNode Resource*s
    // on both sides (via each side's own ImageSyncGathererNode instance) — bakes the genuine
    // probe_apply(write)->spatialReuseNode(read) cross-dispatch SyncEdge this milestone's
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

    // Semantic-wiring S2: ProbeUpdate wires from the SAME sceneProviders
    // registry — the leanest of the four consumers (NO HitRecord/reservoir/
    // outputImage members: the pass's disjointness from the direct/ReSTIR
    // path is structural, visible in its own merged member table).
    //
    // Provenance preserved: probe atlases at 29/30 are the WRITE side —
    // CURRENT_VIEW (raw VkImageView), never PROBE_ATLAS (IRenderTarget* has
    // no conversion_type; validator-found VUID-08114), Execute-only with the
    // write hazard declared via IMAGE_WRITE_ARRAY. ProbeGridConfigSSBO@28 and
    // DDGILeakGateDebugSSBO@31 are CPU-written-GPU-read (and 31 written back:
    // gatheredLuma) — Dependency|Execute. These names differ from
    // SpatialReuseShade's read-side blocks (ProbeGridConfigReadSSBO@34,
    // DDGILeakGateDebugShadeSSBO@31→sharing the same buffer node): per-shader
    // block names, same providers where the resource is the same.
    sceneProviders.Provide("ProbeGridConfigSSBO", probeGridConfigNode,
                           ProbeGridConfigNodeConfig::PROBE_GRID_CONFIG_BUFFER,
                           SlotRole::Dependency | SlotRole::Execute);
    sceneProviders.Provide("probeIrradianceAtlas", probeIrradianceAtlasNode,
                           ProbeAtlasNodeConfig::CURRENT_VIEW, SlotRole::Execute);
    sceneProviders.Provide("probeVisibilityAtlas", probeVisibilityAtlasNode,
                           ProbeAtlasNodeConfig::CURRENT_VIEW, SlotRole::Execute);
    sceneProviders.Provide("DDGILeakGateDebugSSBO", ddgiLeakGateDebugBuffer,
                           StorageBufferNodeConfig::STORAGE_BUFFER,
                           SlotRole::Dependency | SlotRole::Execute);
    // W1a queue/payload buffers under their per-shader block names — each
    // stage's SDI walk binds only the blocks it declares, with the per-stage
    // access the merged SDI reflected (gather: requests/payloads writeonly;
    // wave: requests readonly, results writeonly; apply: results/payloads
    // readonly).
    sceneProviders.Provide("ShadowRayRequestBuffer", shadowRayRequestBuffer,
                           StorageBufferNodeConfig::STORAGE_BUFFER,
                           SlotRole::Dependency | SlotRole::Execute);
    sceneProviders.Provide("ShadowRayResultBuffer", shadowRayResultBuffer,
                           StorageBufferNodeConfig::STORAGE_BUFFER,
                           SlotRole::Dependency | SlotRole::Execute);
    sceneProviders.Provide("ProbeRayPayloadBuffer", probeRayPayloadBuffer,
                           StorageBufferNodeConfig::STORAGE_BUFFER,
                           SlotRole::Dependency | SlotRole::Execute);

    // W1a: the split's three stages, each synthesized from its OWN merged SDI.
    // gather/wave: DescriptorsOnly — their SceneBindings push block stays
    // hand-wired below (the pending-#18 `time` rationale, same as DL/
    // SpatialReuse). apply: full wire — its SDI has ZERO push members (no
    // SceneBindings include), so the synthesized push gatherer reflects an
    // empty block and the stage pushes nothing (SetPushConstants' empty-data
    // early-out); the first zero-push synthesized stage, by design.
    const auto gatherSynth = SynthesizeComputeStage<GatherSdi::Metadata, GatherSdi::MEMBERS>(
        renderGraph, batch, "probe_gather", probeGatherShaderLib,
        probeGatherNode, lightingCommon, sceneProviders, {},
        SdiWireSet::DescriptorsOnly);
    probeGatherPushConstantGatherer = gatherSynth.pushGatherer;
    CensusStageFromSdi<GatherSdi::Metadata, GatherSdi::MEMBERS>(
        sdiHazardCensus_, probeGatherNode, sceneProviders, sdiNoFeatures,
        &sdiCensusExclusions);
    const auto shadowSynth = SynthesizeComputeStage<ShadowSdi::Metadata, ShadowSdi::MEMBERS>(
        renderGraph, batch, "shadow_ray_trace", shadowRayTraceShaderLib,
        shadowRayTraceNode, lightingCommon, sceneProviders, {},
        SdiWireSet::DescriptorsOnly);
    shadowRayTracePushConstantGatherer = shadowSynth.pushGatherer;
    CensusStageFromSdi<ShadowSdi::Metadata, ShadowSdi::MEMBERS>(
        sdiHazardCensus_, shadowRayTraceNode, sceneProviders, sdiNoFeatures,
        &sdiCensusExclusions);
    const auto applySynth = SynthesizeComputeStage<ApplySdi::Metadata, ApplySdi::MEMBERS>(
        renderGraph, batch, "probe_apply", probeApplyShaderLib,
        probeApplyNode, lightingCommon, sceneProviders, {});
    (void)applySynth;  // zero push members; nothing left to hand-wire
    CensusStageFromSdi<ApplySdi::Metadata, ApplySdi::MEMBERS>(
        sdiHazardCensus_, probeApplyNode, sceneProviders, sdiNoFeatures,
        &sdiCensusExclusions);
    // B2 (batch-26, gated OFF by default batch-27, VIXEN_HIT_ACCUM_CLEAR): the
    // table-wide clear — standalone bindings, its ONLY member is HitAccumTable
    // (already has a registry provider from the SRS synthesis site, same
    // provider the accumulate pass below consumes). When enabled it runs FIRST
    // in the frame (topology below, ahead of accumulate). Node handles are
    // invalid unless hitAccumClearEnabled (see the creation site above), so
    // this dispatch must be gated the same way.
    if (hitAccumClearEnabled) {
        const auto clearSynth = SynthesizeComputeStage<ClearSdi::Metadata, ClearSdi::MEMBERS>(
            renderGraph, batch, "hit_accum_clear", hitAccumClearShaderLib,
            hitAccumClearNode, lightingCommon, sceneProviders, {});
        (void)clearSynth;  // zero push members; nothing left to hand-wire
        CensusStageFromSdi<ClearSdi::Metadata, ClearSdi::MEMBERS>(
            sdiHazardCensus_, hitAccumClearNode, sceneProviders, sdiNoFeatures,
            &sdiCensusExclusions);
    }
    // W-SPLIT: the re-split accumulate — standalone bindings, full wire (no
    // SceneBindings push block to hand-wire; its SDI has zero push members,
    // the ProbeApply/RecipeInstanceBucketing precedent). Every member
    // (HitRecordBuffer, BodyInstanceBuffer, HitAccumTable, HitAccumParamsSSBO)
    // already has a registry provider — HitRecordBuffer/BodyInstanceBuffer
    // from the march's own scene providers above, HitAccumTable/
    // HitAccumParamsSSBO from the SRS synthesis site (the providers-before-
    // synthesis rule — this consumer runs after that site, same as the
    // wave used to). Runs FIRST in the frame (topology below): it classifies
    // W-LEAN L1 and writes the table before the wave ever reads the flag bit.
    if (hitAccumEnabled) {
        // B2: pre-merge is its OWN opt-in on top of VIXEN_HIT_ACCUM (default
        // off) — flag-off keeps this stage's compiled variant byte-identical
        // (same B1 conditional-creation shape as VIXEN_HIT_ACCUM_RESOLVE).
        SdiFeatureSet accumSdiFeatures;
        if (hitAccumPremergeEnabled) {
            accumSdiFeatures.Enable(kFeatureHitAccumPremerge);
        }
        const auto accumSynth = SynthesizeComputeStage<AccumSdi::Metadata, AccumSdi::MEMBERS>(
            renderGraph, batch, "hit_accum_accumulate", hitAccumAccumulateShaderLib,
            hitAccumAccumulateNode, lightingCommon, sceneProviders, accumSdiFeatures);
        (void)accumSynth;  // zero push members; nothing left to hand-wire
        CensusStageFromSdi<AccumSdi::Metadata, AccumSdi::MEMBERS>(
            sdiHazardCensus_, hitAccumAccumulateNode, sceneProviders, sdiNoFeatures,
            &sdiCensusExclusions);
    }
    // W1b: the shadow wave — every one of its members (the scene set via
    // SceneBindings, LightingConfigSSBO/HitRecordBuffer/ShadowConfigSSBO)
    // already has a registry provider; DescriptorsOnly because its
    // SceneBindings push block is hand-wired below like every tracing stage.
    // W-SPLIT: unconditionally plain now — VIXEN_HIT_ACCUM_FUSED retired, the
    // table write moved to the accumulate pass's own gatherers above.
    // E11-T1: gates the PolicyStencilTileBuffer member (binding 41), same
    // rule as marchFeatures above -- must mirror lightingShaderFeatures'
    // VIXEN_POLICY_STENCIL_TILES define or the compiled variant and the wire
    // plan disagree.
    SdiFeatureSet waveSdiFeatures;
    if (policyStencilTilesEnabled) {
        waveSdiFeatures.Enable(kFeaturePolicyStencil);
        waveSdiFeatures.Enable(kFeaturePolicyStencilTiles);
    }
    const auto waveSynth = SynthesizeComputeStage<WaveSdi::Metadata, WaveSdi::MEMBERS>(
        renderGraph, batch, "shadow_visibility_wave", shadowVisibilityWaveShaderLib,
        shadowVisibilityWaveNode, lightingCommon, sceneProviders, waveSdiFeatures,
        SdiWireSet::DescriptorsOnly);
    shadowVisibilityWavePushConstantGatherer = waveSynth.pushGatherer;
    CensusStageFromSdi<WaveSdi::Metadata, WaveSdi::MEMBERS>(
        sdiHazardCensus_, shadowVisibilityWaveNode, sceneProviders, waveSdiFeatures,
        &sdiCensusExclusions);

    // Both TRACING stages consume SceneBindings.glsl's PushConstants block and
    // need the same three live fields the merged ProbeUpdate needed — the M4/
    // M4b live-gate findings preserved verbatim: instanceCount (field 10)
    // bounds TraceWorld/TraceWorldShadow's instance loop (`numInstances =
    // clamp(pc.instanceCount, 0, 3*64)`), so an unconnected/zero value makes
    // EVERY ray a guaranteed miss regardless of scene content; raySizeCoef/
    // raySizeBias (fields 8/9) feed TraceWorldShadow's any-hit LOD gate (the
    // secondary-ray coefficient, not the primary gatherer's raySizeCoefNode).
    // ProbeApply traces nothing and has no push block at all.
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::INSTANCE_COUNT,
                          probeGatherPushConstantGatherer, GatherSdi::Push::instanceCount::INDEX,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(secondaryRaySizeCoefConstant, ConstantNodeConfig::OUTPUT,
                          probeGatherPushConstantGatherer, GatherSdi::Push::raySizeCoef::INDEX,
                          SlotRoleModifier(SlotRole::Execute));
    batch.Connect(raySizeBiasConstant, ConstantNodeConfig::OUTPUT,
                          probeGatherPushConstantGatherer, GatherSdi::Push::raySizeBias::INDEX,
                          SlotRoleModifier(SlotRole::Execute));
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::INSTANCE_COUNT,
                          shadowRayTracePushConstantGatherer, ShadowSdi::Push::instanceCount::INDEX,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(secondaryRaySizeCoefConstant, ConstantNodeConfig::OUTPUT,
                          shadowRayTracePushConstantGatherer, ShadowSdi::Push::raySizeCoef::INDEX,
                          SlotRoleModifier(SlotRole::Execute));
    batch.Connect(raySizeBiasConstant, ConstantNodeConfig::OUTPUT,
                          shadowRayTracePushConstantGatherer, ShadowSdi::Push::raySizeBias::INDEX,
                          SlotRoleModifier(SlotRole::Execute));
    // W1b: the shadow-visibility wave traces too — same three live fields
    // (instanceCount bounds the instance loop; raySizeCoef/raySizeBias feed
    // the any-hit LOD gate — the M4/M4b findings preserved a third time).
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::INSTANCE_COUNT,
                          shadowVisibilityWavePushConstantGatherer, WaveSdi::Push::instanceCount::INDEX,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(secondaryRaySizeCoefConstant, ConstantNodeConfig::OUTPUT,
                          shadowVisibilityWavePushConstantGatherer, WaveSdi::Push::raySizeCoef::INDEX,
                          SlotRoleModifier(SlotRole::Execute));
    batch.Connect(raySizeBiasConstant, ConstantNodeConfig::OUTPUT,
                          shadowVisibilityWavePushConstantGatherer, WaveSdi::Push::raySizeBias::INDEX,
                          SlotRoleModifier(SlotRole::Execute));

    // ------------------------------------------------------------------
    // W2a (reservoir path opt-in): SpatialReuseGather + the wave's
    // reservoir-phase dispatch — synthesis, census, push, hazards. Every
    // provider these stages need already exists (the shade used them all);
    // the fold's A/B neighbor-array hazard arrives HERE with its new owner.
    // Ordering is carried by DECLARED edges end-to-end on the opt-in chain:
    // DL(writes A/B) → gather(reads A/B, writes combined) → wave-reservoir
    // (reads combined, RMWs hit records) → shade (reads records, submission-
    // serialized — the standing march-chain NOT-baked class).
    // ------------------------------------------------------------------
    if (reservoirPathEnabled) {
        const auto gatherSynth = SynthesizeComputeStage<SrgSdi::Metadata, SrgSdi::MEMBERS>(
            renderGraph, batch, "spatial_reuse_gather", spatialReuseGatherShaderLib,
            spatialReuseGatherNode, lightingCommon, sceneProviders, {},
            SdiWireSet::DescriptorsOnly);
        spatialReuseGatherPushGatherer = gatherSynth.pushGatherer;
        CensusStageFromSdi<SrgSdi::Metadata, SrgSdi::MEMBERS>(
            sdiHazardCensus_, spatialReuseGatherNode, sceneProviders, sdiNoFeatures,
            &sdiCensusExclusions);

        SdiFeatureSet waveReservoirSdiFeatures;
        waveReservoirSdiFeatures.Enable(kFeatureWaveReservoirPhase);
        const auto waveResSynth = SynthesizeComputeStage<WaveSdi::Metadata, WaveSdi::MEMBERS>(
            renderGraph, batch, "shadow_visibility_wave_reservoir", waveReservoirShaderLib,
            waveReservoirNode, lightingCommon, sceneProviders, waveReservoirSdiFeatures,
            SdiWireSet::DescriptorsOnly);
        CensusStageFromSdi<WaveSdi::Metadata, WaveSdi::MEMBERS>(
            sdiHazardCensus_, waveReservoirNode, sceneProviders, waveReservoirSdiFeatures,
            &sdiCensusExclusions);

        // The gather's OWN push block: imgWidth/imgHeight from the build-time
        // scaled extent (same values its dispatch dims derive from above).
        batch.Connect(gatherWidthConstant, ConstantNodeConfig::OUTPUT,
                      spatialReuseGatherPushGatherer, SrgSdi::Push::imgWidth::INDEX,
                      SlotRoleModifier(SlotRole::Execute));
        batch.Connect(gatherHeightConstant, ConstantNodeConfig::OUTPUT,
                      spatialReuseGatherPushGatherer, SrgSdi::Push::imgHeight::INDEX,
                      SlotRoleModifier(SlotRole::Execute));
        // The reservoir-phase wave traces — same three live SceneBindings pc
        // fields as every tracing stage (the M4/M4b findings, a fourth time).
        batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::INSTANCE_COUNT,
                      waveResSynth.pushGatherer, WaveSdi::Push::instanceCount::INDEX,
                      SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(secondaryRaySizeCoefConstant, ConstantNodeConfig::OUTPUT,
                      waveResSynth.pushGatherer, WaveSdi::Push::raySizeCoef::INDEX,
                      SlotRoleModifier(SlotRole::Execute));
        batch.Connect(raySizeBiasConstant, ConstantNodeConfig::OUTPUT,
                      waveResSynth.pushGatherer, WaveSdi::Push::raySizeBias::INDEX,
                      SlotRoleModifier(SlotRole::Execute));

        // Hazards. Gather: reads {A, B, hitRecords}, writes {combined}.
        batch.Connect(reservoirBufferA, StorageBufferNodeConfig::STORAGE_BUFFER,
                      spatialReuseGatherReadGatherer, 0, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(reservoirBufferB, StorageBufferNodeConfig::STORAGE_BUFFER,
                      spatialReuseGatherReadGatherer, 1, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(hitRecordBufferNode, StorageBufferNodeConfig::STORAGE_BUFFER,
                      spatialReuseGatherReadGatherer, 2, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(spatialReuseGatherReadGatherer, BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                      spatialReuseGatherNode, ComputeStageNodeConfig::BUFFER_READ_ARRAY, SlotRoleModifier(SlotRole::Execute));
        batch.Connect(spatialReservoirDebugBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                      spatialReuseGatherWriteGatherer, 0, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(spatialReuseGatherWriteGatherer, BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                      spatialReuseGatherNode, ComputeStageNodeConfig::BUFFER_WRITE_ARRAY, SlotRoleModifier(SlotRole::Execute));
        // Wave reservoir phase: reads {combined, hitRecords}, writes {hitRecords}.
        batch.Connect(spatialReservoirDebugBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                      waveReservoirReadGatherer, 0, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(hitRecordBufferNode, StorageBufferNodeConfig::STORAGE_BUFFER,
                      waveReservoirReadGatherer, 1, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(waveReservoirReadGatherer, BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                      waveReservoirNode, ComputeStageNodeConfig::BUFFER_READ_ARRAY, SlotRoleModifier(SlotRole::Execute));
        batch.Connect(hitRecordBufferNode, StorageBufferNodeConfig::STORAGE_BUFFER,
                      waveReservoirWriteGatherer, 0, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(waveReservoirWriteGatherer, BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                      waveReservoirNode, ComputeStageNodeConfig::BUFFER_WRITE_ARRAY, SlotRoleModifier(SlotRole::Execute));
    }

    // ------------------------------------------------------------------
    // W3c-2 (resolve opt-in): cell shade + resolve composite — synthesis,
    // census, push, hazards. Every member except the cell-radiance buffer
    // already has a registry provider (the wave's scene set + table/params at
    // the wave's own provider site above — the providers-before-synthesis
    // rule, found live at W3c-1).
    // ------------------------------------------------------------------
    if (hitAccumResolveEnabled) {
        // (HitAccumCellRadiance provided at the SRS synthesis site above.)
        const auto cellShadeSynth = SynthesizeComputeStage<CellShadeSdi::Metadata, CellShadeSdi::MEMBERS>(
            renderGraph, batch, "hit_accum_cell_shade", hitAccumCellShadeShaderLib,
            hitAccumCellShadeNode, lightingCommon, sceneProviders, {},
            SdiWireSet::DescriptorsOnly);
        CensusStageFromSdi<CellShadeSdi::Metadata, CellShadeSdi::MEMBERS>(
            sdiHazardCensus_, hitAccumCellShadeNode, sceneProviders, sdiNoFeatures,
            &sdiCensusExclusions);

        // The cell shade traces (TraceWorldShadow) — the same three live
        // SceneBindings pc fields as every tracing stage (the M4/M4b findings,
        // a fifth time).
        batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::INSTANCE_COUNT,
                      cellShadeSynth.pushGatherer, CellShadeSdi::Push::instanceCount::INDEX,
                      SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(secondaryRaySizeCoefConstant, ConstantNodeConfig::OUTPUT,
                      cellShadeSynth.pushGatherer, CellShadeSdi::Push::raySizeCoef::INDEX,
                      SlotRoleModifier(SlotRole::Execute));
        batch.Connect(raySizeBiasConstant, ConstantNodeConfig::OUTPUT,
                      cellShadeSynth.pushGatherer, CellShadeSdi::Push::raySizeBias::INDEX,
                      SlotRoleModifier(SlotRole::Execute));

        // Hazards. Cell shade: reads {table} — the wave (its writer) orders
        // ahead on the shared Resource*; writes {cellRadiance}.
        batch.Connect(hitAccumTableBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                      hitAccumCellShadeReadGatherer, 0, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(hitAccumCellShadeReadGatherer, BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                      hitAccumCellShadeNode, ComputeStageNodeConfig::BUFFER_READ_ARRAY, SlotRoleModifier(SlotRole::Execute));
        batch.Connect(hitAccumCellRadianceBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                      hitAccumCellShadeWriteGatherer, 0, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(hitAccumCellShadeWriteGatherer, BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                      hitAccumCellShadeNode, ComputeStageNodeConfig::BUFFER_WRITE_ARRAY, SlotRoleModifier(SlotRole::Execute));
        // W-LEAN L3: the shade's fold reads {cellRadiance, table} — SRS's
        // first buffer read gatherer (records were already its own class).
        batch.Connect(hitAccumCellRadianceBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                      spatialReuseCellReadGatherer, 0, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(hitAccumTableBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                      spatialReuseCellReadGatherer, 1, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(spatialReuseCellReadGatherer, BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                      spatialReuseNode, ComputeStageNodeConfig::BUFFER_READ_ARRAY, SlotRoleModifier(SlotRole::Execute));
        // Ordering-only edges (the Blit ORDERING_WAIT_SEMAPHORE convention —
        // found live at W3c-2's first boot: shared-Resource* hazards do NOT
        // order two stages). The chain wave → cell_shade → SRS pins: the cell
        // shade sees THIS frame's epoch-stamped table, and the shade's fold
        // reads THIS frame's cell radiance.
        batch.Connect(shadowVisibilityWaveNode, ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                      hitAccumCellShadeNode, ComputeStageNodeConfig::ORDERING_WAIT_SEMAPHORE);
        batch.Connect(hitAccumCellShadeNode, ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                      spatialReuseNode, ComputeStageNodeConfig::ORDERING_WAIT_SEMAPHORE);
    }

    // ------------------------------------------------------------------
    // W3b (hit-accumulation opt-in): clear + accumulate synthesis, push,
    // hazards. Providers: HitRecordBuffer exists; BodyInstanceBuffer rides
    // bodyOctreeSceneNode's own INSTANCE_BUFFER (the bucketing providers'
    // idiom); HitAccumTable is the new fixed-size buffer node.
    // ------------------------------------------------------------------
    // W1a cross-dispatch buffer hazards: writer-side and reader-side gatherers
    // over the SAME StorageBufferNode outputs (shared Resource* identity via
    // Resource::hazardConstituents_ is what the scheduler bakes SyncEdges from —
    // the reservoir ping-pong precedent above). The chain this declares:
    //   probe_gather(write: requests+payloads) → shadow_ray_trace(read: requests)
    //   shadow_ray_trace(write: results)       → probe_apply(read: results+payloads)
    // which orders gather→wave→apply and carries the next-frame WAR back-edges
    // on the same resources.
    batch.Connect(shadowRayRequestBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                  probeGatherWriteGatherer, 0, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(probeRayPayloadBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                  probeGatherWriteGatherer, 1, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(probeGatherWriteGatherer, BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                  probeGatherNode, ComputeStageNodeConfig::BUFFER_WRITE_ARRAY, SlotRoleModifier(SlotRole::Execute));
    batch.Connect(shadowRayRequestBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                  shadowRayTraceReadGatherer, 0, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(shadowRayTraceReadGatherer, BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                  shadowRayTraceNode, ComputeStageNodeConfig::BUFFER_READ_ARRAY, SlotRoleModifier(SlotRole::Execute));
    batch.Connect(shadowRayResultBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                  shadowRayTraceWriteGatherer, 0, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(shadowRayTraceWriteGatherer, BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                  shadowRayTraceNode, ComputeStageNodeConfig::BUFFER_WRITE_ARRAY, SlotRoleModifier(SlotRole::Execute));
    batch.Connect(shadowRayResultBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                  probeApplyReadGatherer, 0, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(probeRayPayloadBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                  probeApplyReadGatherer, 1, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(probeApplyReadGatherer, BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                  probeApplyNode, ComputeStageNodeConfig::BUFFER_READ_ARRAY, SlotRoleModifier(SlotRole::Execute));

    // B2 (batch-26, gated OFF by default batch-27): the clear pass's own
    // hazard — writes {hitAccumTable} only (no reads; it unconditionally
    // zeroes every slot). Ordering-only edge pins clear -> accumulate
    // (accumulate's own slot-21 ORDERING_WAIT_SEMAPHORE input was otherwise
    // unused — each ComputeStageNode instance owns its own slot 21, so this
    // does not collide with the accumulate -> wave edge below).
    if (hitAccumClearEnabled) {
        batch.Connect(hitAccumTableBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                      hitAccumClearWriteGatherer, 0, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(hitAccumClearWriteGatherer, BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                      hitAccumClearNode, ComputeStageNodeConfig::BUFFER_WRITE_ARRAY, SlotRoleModifier(SlotRole::Execute));
        batch.Connect(hitAccumClearNode, ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                      hitAccumAccumulateNode, ComputeStageNodeConfig::ORDERING_WAIT_SEMAPHORE);
    }

    // W-SPLIT: the accumulate pass's own hazards — reads {hitRecordBuffer},
    // writes {hitAccumTable, hitRecordBuffer} (the RMW of the flags word AND
    // the table). Runs BEFORE the wave (topology below): the wave's analytic
    // phase only ever READS the flag bit this pass stamps.
    if (hitAccumEnabled) {
        batch.Connect(hitRecordBufferNode, StorageBufferNodeConfig::STORAGE_BUFFER,
                      hitAccumAccumulateReadGatherer, 0, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(hitAccumAccumulateReadGatherer, BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                      hitAccumAccumulateNode, ComputeStageNodeConfig::BUFFER_READ_ARRAY, SlotRoleModifier(SlotRole::Execute));
        batch.Connect(hitAccumTableBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                      hitAccumAccumulateWriteGatherer, 0, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(hitRecordBufferNode, StorageBufferNodeConfig::STORAGE_BUFFER,
                      hitAccumAccumulateWriteGatherer, 1, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(hitAccumAccumulateWriteGatherer, BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                      hitAccumAccumulateNode, ComputeStageNodeConfig::BUFFER_WRITE_ARRAY, SlotRoleModifier(SlotRole::Execute));
        // Ordering-only edge (the Blit ORDERING_WAIT_SEMAPHORE convention —
        // shared-Resource* hazards do NOT order two stages, the W3c-2 finding
        // reused here): pins accumulate → wave, so the frame runs clear →
        // accumulate → wave → cell_shade → SRS (cell_shade's own ordering
        // source is the wave, which transitively follows accumulate — no
        // edge needed there). The wave's slot-21 ORDERING_WAIT_SEMAPHORE
        // input was otherwise unused.
        batch.Connect(hitAccumAccumulateNode, ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                      shadowVisibilityWaveNode, ComputeStageNodeConfig::ORDERING_WAIT_SEMAPHORE);
    }

    // W1b cross-dispatch hazards: the shadow wave read-modify-writes the
    // hit-record buffer BETWEEN the march (writer) and DL/SpatialReuse
    // (readers) — declared as its own read+write gatherer pair on that ONE
    // shared resource (both-slots-on-one-stage: bucketing's modeFinal
    // precedent). The scheduler orders march→wave→readers on the shared
    // Resource* exactly as it orders the reservoir ping-pong today; verified
    // by the SCHED identity dump + the frame-hash gate (an unordered wave
    // reads-before-march or writes-after-shade would break the hash loudly).
    batch.Connect(hitRecordBufferNode, StorageBufferNodeConfig::STORAGE_BUFFER,
                  shadowVisibilityWaveReadGatherer, 0, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    // KI-052 fix (E12-T1): PolicyStencilTileBuffer's read side — pairs with
    // policyStencilTileWriteGatherer's entry into computeDispatch's BUFFER_WRITE_ARRAY
    // below, on the SAME policyStencilTileBuffer::STORAGE_BUFFER Resource*, so
    // ResourceAccessTracker::AddNode finally correlates the march's write against the
    // wave's read and FrameSyncScheduler bakes the previously-missing SyncEdge.
    batch.Connect(policyStencilTileBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                  shadowVisibilityWaveReadGatherer, 1, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(shadowVisibilityWaveReadGatherer, BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                  shadowVisibilityWaveNode, ComputeStageNodeConfig::BUFFER_READ_ARRAY, SlotRoleModifier(SlotRole::Execute));
    batch.Connect(hitRecordBufferNode, StorageBufferNodeConfig::STORAGE_BUFFER,
                  shadowVisibilityWaveWriteGatherer, 0, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(shadowVisibilityWaveWriteGatherer, BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                  shadowVisibilityWaveNode, ComputeStageNodeConfig::BUFFER_WRITE_ARRAY, SlotRoleModifier(SlotRole::Execute));

    // KI-052 fix (E12-T1): the march's write side — policyStencilTileWriteGatherer feeds
    // computeDispatch's new BUFFER_WRITE_ARRAY slot (ComputeDispatchNodeConfig.h). This is
    // the ONLY producer-side registration of PolicyStencilTileBuffer as a tracked hazard
    // resource; previously it was registered ONLY through sceneProviders.Provide (a
    // descriptor-binding mechanism ResourceAccessTracker never walks), which is exactly
    // KI-052's root cause.
    batch.Connect(policyStencilTileBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                  policyStencilTileWriteGatherer, 0, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(policyStencilTileWriteGatherer, BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                  computeDispatch, ComputeDispatchNodeConfig::BUFFER_WRITE_ARRAY, SlotRoleModifier(SlotRole::Execute));

    // Sync slot: IMAGE_WRITE_ARRAY — the genuine write hazard on the persistent probe
    // atlases, now owned by probe_apply (W1a: the frame's ONLY atlas writer — this
    // frame's write must be visible before any future consumer, e.g. the shade-pass
    // gather, reads it; also guards this pass's own writes across frames on the SAME
    // persistent image, the identical "hysteresis needs the prior write visible"
    // shape AccumulationHistoryNode's historyImage sync already relies on). Fed via
    // probeAtlasGatherer (Inc4 M2's own gathering wiring above) rather than
    // re-gathering here — one gatherer instance, reused for both
    // PreRegisterImageSlots(2)'s hazard-array shape and this pass's actual
    // consuming connection.
    batch.Connect(probeAtlasGatherer, ImageSyncGathererNodeConfig::IMAGE_ARRAY,
                  probeApplyNode, ComputeStageNodeConfig::IMAGE_WRITE_ARRAY, SlotRoleModifier(SlotRole::Execute));

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
                  blitNode, BlitNodeConfig::TIMELINE_FRAME_BASE_IN)
         // Baked-Perf M6 Task 6.1 (audit E2): BlitNode is now the real first swapchain-
         // touching submit on the writesNoImage march path (the march no longer waits
         // imageAvailable itself — see ComputeDispatchWaitsForSwapchainAcquire's doc
         // comment), so it must consume the acquire wait BlitNodeConfig's own
         // IMAGE_AVAILABLE_SEMAPHORES_ARRAY slot already anticipated for exactly this.
         .Connect(frameSyncNode, FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY,
                  blitNode, BlitNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY);
    batch.Connect(renderTargetNode, RenderTargetNodeConfig::RENDER_TARGET,
                  blitNode, BlitNodeConfig::IMAGE_READ, SlotRoleModifier(SlotRole::Execute));
    // Ordering-only edge (BlitNode never waits it — see BlitNodeConfig's ORDERING_WAIT_SEMAPHORE
    // doc): establishes the SpatialReuse-before-Blit TOPOLOGY (Sampled Lighting Inc3 M5 — moved
    // from DirectLighting, which is no longer the render-target writer) so the scheduler's
    // groupId-order edge direction is correct (mirrors the sky-projection/UI ordering-edge
    // convention below). W-LEAN L3: SRS is the sole render-target writer again
    // (the composite is its own gated tail — no standalone resolve stage).
    if (hdrExposureEnabled) {
        batch.Connect(spatialReuseNode, ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                      exposureNode, ComputeStageNodeConfig::ORDERING_WAIT_SEMAPHORE);
        batch.Connect(exposureNode, ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                      blitNode, BlitNodeConfig::ORDERING_WAIT_SEMAPHORE);
    } else {
        batch.Connect(spatialReuseNode, ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                      blitNode, BlitNodeConfig::ORDERING_WAIT_SEMAPHORE);
    }

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
    // composite (writesNoImage + leaveImageInGeneral), and Baked-Perf M6 Task 6.3 (audit E4) fixed
    // BlitNode to match: it used to unconditionally signal a real renderComplete even in composite
    // mode (an orphaned per-image binary semaphore nothing ever waited, re-signalled illegally the
    // next time this image index came around — VUID-vkQueueSubmit2-semaphore-03868), so now its
    // output here is also VK_NULL_HANDLE in composite mode. SkyProjectionNode never WAITS its
    // COMPOSITE_WAIT_SEMAPHORE input, and UIRenderNode no longer waits compositeWait either (the M3
    // binary handoff was dropped from its submit). With the edges in the right direction the scheduler bakes
    // march(HitRecord)->DirectLighting(GENERAL)->Blit(GENERAL)->sky-projection(GENERAL)->UI(GENERAL)
    // timeline edges, tags the UI group as present (its render pass owns GENERAL->PRESENT_SRC,
    // unchanged), and the timeline alone — not a binary handoff — orders every pass. WSI acquire
    // (march waits imageAvailable) and present (UI signals its uiComplete) stay binary, unchanged.
    batch.Connect(blitNode, BlitNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                  skyProjectionNode, SkyProjectionNodeConfig::COMPOSITE_WAIT_SEMAPHORE);
    batch.Connect(skyProjectionNode, SkyProjectionNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                  uiCompositeNode, UIRenderNodeConfig::COMPOSITE_WAIT_SEMAPHORE);

    // Baked-Perf M4 Task 4.3 (audit C8 + inventory #1/#2): generic no-op dispatch guard,
    // wired HERE (the end of this function) rather than beside each node's OTHER
    // parameters above -- several VIXEN_*_DEMO blocks between here and there force-enable
    // VIXEN_PROBE_GRID_CONFIG_ENABLED/VIXEN_RESERVOIR_CONFIG_ENABLED via _putenv_s for
    // their own scene setup (e.g. VIXEN_DDGI_CORNELL_BAKED_DEMO's "probe grid must be on
    // for this demo's own point" block), so ResolveProbeGridEnabled()/
    // ResolveReservoirEnabled() must be evaluated AFTER every such override has had a
    // chance to run, not at the point each node's other parameters happen to be set.
    //
    // direct_lighting: SpatialReuseShade.comp's own comment documents reservoirEnabled==0
    // (the shipped default; nothing in this codebase enables it) as a byte-identity no-op
    // for its whole ReSTIR block -- today a DEAD full-screen dispatch + submit runs every
    // frame on every path regardless. ResolveReservoirEnabled() is the SAME accessor
    // ReservoirConfigNode's own per-frame GPU upload calls, so this CPU-side skip can
    // never disagree with the GPU-side config it is skipping around.
    directLighting->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_ENABLED,
                                  ResolveReservoirEnabled());
    // probe_gather/shadow_ray_trace/probe_apply: same reasoning, gated on
    // probeGridEnabled instead (the shade-pass consumer, SpatialReuseShade.comp,
    // documents the identical "probeGridEnabled==0 skips this block entirely"
    // escape hatch; gather/apply carry the same shader-side early-out).
    // ResolveProbeGridEnabled() is the SAME accessor ProbeGridConfigNode's own
    // per-frame GPU upload calls, so a demo's force-enable (Cornell, both baked
    // and virtual variants -- see their own VIXEN_PROBE_GRID_CONFIG_ENABLED
    // _putenv_s blocks above) keeps the split dispatching exactly as before;
    // only the default-boot (probe grid off) path skips it. The WAVE is gated
    // too: it has no config binding of its own (by design — no scene, no
    // config), so this CPU-side skip is the only thing stopping it re-tracing
    // stale queue slots on a disabled grid.
    probeGather->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_ENABLED,
                               ResolveProbeGridEnabled());
    shadowRayTrace->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_ENABLED,
                                  ResolveProbeGridEnabled());
    probeApply->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_ENABLED,
                              ResolveProbeGridEnabled());

    // Atomically register all connections
    size_t connectionCount = batch.GetConnectionCount();
    mainLogger->Info("Registering " + std::to_string(connectionCount) + " connections...");
    batch.RegisterAll();

    mainLogger->Info("Successfully wired " + std::to_string(connectionCount) + " connections");

    mainLogger->Info("Complete render pipeline built with " + std::to_string(renderGraph->GetNodeCount()) + " nodes");
}
