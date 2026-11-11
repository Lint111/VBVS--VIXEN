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
#include "ConfiguredOptionalPath.h"
#include "Core/ShaderSourceDir.h"
#include "KernelDispatch/TaskExecutor.h"
#include <array>
#include <algorithm>  // std::clamp for the VIXEN_PROCEDURAL_UBER_DEMO N clamp
#include <cctype>    // std::isspace for whitespace-safe boolean env flags
#include <cmath>    // std::tan for the LOD ray-cone (raySizeCoef) computation
#include <cstdlib>  // Environment switches used by the existing optional render paths.
#include <fstream>  // Inc0 M5: read BodyInstanceRayMarch.comp's raw source for the recipe splice
#include <sstream>  // Inc0 M5: rdbuf() into a string for the splice
#include <iostream> // Round-5 [ComposedBackend] boot print (mirrors VoxelGridNode.cpp's [FarFieldCount])
#include <mutex>    // Baked-Perf M7 Task 7.1: serializes calls into Gaia's shared ChunkAllocator
#include <optional>
#include <stdexcept>
#include <thread>
#include <unordered_map>  // Baked-Perf M7 Task 7.2: ConcatenateSdfWithMips precomputed-serialize map
#include "Recipe/UberShaderSplice.h"  // Inc0 M5: SpliceProceduralRecipesIntoSource
#include "graph/CornellBoxSceneDefinition.h"
#include "graph/AntiAliasingModule.h"
#include "graph/UpscalerModules.h"
#include "graph/ReconstructionDepth.h"
#include "graph/ReconstructionMotion.h"
#include "graph/ReconstructionSampling.h"
#include "graph/ReconstructionExposure.h"
#include "graph/ReconstructionHistory.h"
#include "BakeArtifactCache.h"  // Baked-Perf M7 Task 7.4: bake-artifact disk cache
#include "Connection/ConnectionModifier.h"
#include "Connection/SdiStageWiring.h"  // Semantic-wiring S2: provider registry + SDI-driven stage wiring
#include "Nodes/SdiStageSynthesis.h"    // Semantic-wiring S2 synthesis: gatherer/descSet/pipeline plumbing from merged SDI
#include "ShaderFamily.h"               // Semantic-wiring S2 slice B: feature variants cached as shader families
#include "Connection/Modifiers/FieldExtractionModifier.h"
#include "Connection/Modifiers/AccumulationSortConfig.h"  // SEL-P3: accumulation-connect sort key (provider fan-in)
#include "MeshData.h"
#include "merged/BodyInstanceRayMarch-SDI.g.h"   // Semantic-wiring S1: feature-tagged merged SDI (ShaderInterface::*)
#include "merged/HiZDownsample-SDI.g.h"          // Semantic-wiring S1: B1 HiZ named binding/push constants
#include "merged/InstanceOcclusionCull-SDI.g.h"  // Semantic-wiring S1: B1 cull named binding/push constants
#include "merged/RecipeInstanceBucketing-SDI.g.h" // Semantic-wiring S1: bucketing named binding/push constants
#include "merged/DirectLighting-SDI.g.h"          // Semantic-wiring S1: lighting passes each cite their OWN interface
#include "merged/ExposureTonemapReference-SDI.g.h"
#include "Nodes/GeneratedSystemStage.h"            // renderbridge S1: generated GPU systems from the composition manifest
#include "merged/SpatialReuseShade-SDI.g.h"
#include "merged/ExposureMeter-SDI.g.h"
#include "merged/ExposureReduce-SDI.g.h"
#include "merged/ProbeGather-SDI.g.h"             // W1a: ProbeUpdate's megakernel split (gather/wave/apply)
#include "merged/ProbeApply-SDI.g.h"
#include "merged/ShadowRayTrace-SDI.g.h"
#include "merged/ShadowVisibilityWave-SDI.g.h"    // W1b: the derived-request shadow wave
#include "merged/SpatialReuseGather-SDI.g.h"      // W2a: the ReSTIR spatial fold (gather)
#include "merged/HitAccumCellShade-SDI.g.h"       // W3c-2: per-cell shade over the accumulation table
#include "merged/HitAccumulate-SDI.g.h"           // W-SPLIT: the re-split accumulate, standalone bindings
#include "merged/HitAccumClear-SDI.g.h"           // B2 (batch-26): table-wide epoch clear, standalone bindings
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
namespace ExposureReferenceSdi = ShaderInterface::ExposureTonemapReference;
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
#include "Data/Nodes/ProxyRasterStageNodeConfig.h"  // Raster-proxy B2 intermediate graphics submit
#include "Data/Nodes/RenderTargetNodeConfig.h"  // M4: render-scale decoupling offscreen target
#include "Data/Nodes/SelectionCoordinatorNodeConfig.h"
#include "Data/Nodes/ShadowConfigNodeConfig.h"  // Sampled Lighting Inc1 M4: ShadowConfig upload ring
#include "Data/Nodes/HitAccumParamsConfigNodeConfig.h"  // B2: hit-accumulate params upload ring
#include "Data/Nodes/PhotonCellParamsConfigNodeConfig.h"  // C0/C1: photon-cell params upload ring
#include "Data/Nodes/AccumulationConfigNodeConfig.h"   // Sampled Lighting Inc2 M1: AccumulationConfig upload ring
#include "Data/Nodes/AccumulationHistoryNodeConfig.h"  // Sampled Lighting Inc2 M1: persistent history image
#include "Data/Nodes/SceneRadianceNodeConfig.h"
#include "Data/Nodes/WorldPosHistoryNodeConfig.h"      // Sampled Lighting Inc3 M2: worldPos/depth companion history image (KI-023)
#include "Data/Nodes/PrevCameraConfigNodeConfig.h"     // Sampled Lighting Inc2 M3: prev-frame camera matrix upload ring
#include "Data/Nodes/ReservoirConfigNodeConfig.h"      // Sampled Lighting Inc3 M3: ReservoirConfig upload ring (M4/M5 scaffolding)
#include "Data/Nodes/LightTreeBufferNodeConfig.h"      // Sampled Lighting Inc3 M4: mip-cut light-tree upload ring
#include "Data/Nodes/MiningBeamBufferNodeConfig.h"
#include "Data/Nodes/ProbeGridConfigNodeConfig.h"      // Sampled Lighting Inc4 M2: ProbeGridConfig upload ring (M3-M6 scaffolding)
#include "Data/Nodes/ProbeAtlasNodeConfig.h"           // Sampled Lighting Inc4 M2: persistent DDGI probe atlas image
#include "Data/Nodes/SkySphereNodeConfig.h"             // T-1138: procedural background cache
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
#include "Nodes/PhotonCellParamsConfigNode.h"  // C0/C1: photon-cell params upload ring
#include "Nodes/PhotonCellTableNode.h"  // C0/C1: photon-cell table resource
#include "Nodes/PhotonCellPassNodes.h"  // C1: pluggable deposit/fold/clear passes
#include "Nodes/AccumulationConfigNode.h"   // Sampled Lighting Inc2 M1: AccumulationConfig upload ring
#include "Nodes/AccumulationHistoryNode.h"  // Sampled Lighting Inc2 M1: persistent history image
#include "Nodes/SceneRadianceNode.h"
#include "Nodes/WorldPosHistoryNode.h"      // Sampled Lighting Inc3 M2: worldPos/depth companion history image (KI-023)
#include "Nodes/PrevCameraConfigNode.h"     // Sampled Lighting Inc2 M3: prev-frame camera matrix upload ring
#include "Nodes/ReservoirConfigNode.h"      // Sampled Lighting Inc3 M3: ReservoirConfig upload ring (M4/M5 scaffolding)
#include "Nodes/LightTreeBufferNode.h"      // Sampled Lighting Inc3 M4: mip-cut light-tree upload ring
#include "Nodes/MiningBeamBufferNode.h"
#include "Nodes/ProbeGridConfigNode.h"      // Sampled Lighting Inc4 M2: ProbeGridConfig upload ring (M3-M6 scaffolding)
#include "Nodes/ProbeAtlasNode.h"           // Sampled Lighting Inc4 M2: persistent DDGI probe atlas image
#include "Nodes/SkySphereNode.h"            // T-1138: procedural background cache
#include "graph/SceneLook.h"                // R670: the built-in scenes' exposure declaration
#include "Nodes/DepthTargetNode.h"          // Raster-proxy B1 M4: occlusion depth ping-pong pair
#include "Nodes/ImageSyncGathererNode.h"    // Sampled Lighting Inc4 M1: variadic IRenderTarget* sync gatherer
#include "Nodes/LoopBridgeNode.h"
#include "Nodes/PickIdTargetNode.h"
#include "Nodes/PresentNode.h"
#include "Nodes/PushConstantGathererNode.h"
#include "Nodes/RaySizeCoefNode.h"  // M4: live LOD ray-cone recompute
#include "Nodes/RenderPassNode.h"
#include "Nodes/ProxyRasterStageNode.h"  // Raster-proxy B2 intermediate graphics submit
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
#ifdef VIXEN_SHADER_SOURCE_DIR
std::string ShaderSourceDir() { return Vixen::RenderGraph::ResolveShaderSourceDir(VIXEN_SHADER_SOURCE_DIR); }
#endif


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

bool PopulateSelectedDeviceCapabilities(VkInstance instance,
                                       Vixen::CapabilityGraph& capabilities) {
    capabilities.BuildStandardCapabilities();
    uint32_t deviceCount = 0;
    if (instance == VK_NULL_HANDLE ||
        vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr) != VK_SUCCESS ||
        deviceCount == 0) {
        return false;
    }
    std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
    if (vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices.data()) != VK_SUCCESS) {
        return false;
    }
    physicalDevices.resize(deviceCount);

    // Use DeviceNode's policy so the pre-compile module decision observes the same adapter that
    // the render graph will create a logical device for.
    const uint32_t selectedIndex = DeviceNode::SelectPhysicalDeviceIndex(
        physicalDevices, DeviceNodeConfig::GPU_INDEX_AUTO);
    capabilities.ObservePhysicalDevice(physicalDevices[selectedIndex]);
    return true;
}

std::string_view RuntimeOperatingSystem() noexcept {
#if defined(_WIN32)
    return "Windows";
#elif defined(__linux__)
    return "Linux";
#else
    return "unknown";
#endif
}

std::string_view UpscalerKindId(VulkanGraphApplication::UpscalerKind kind) noexcept {
    switch (kind) {
    case VulkanGraphApplication::UpscalerKind::Linear: return "linear";
    case VulkanGraphApplication::UpscalerKind::Fsr1: return "fsr1";
    case VulkanGraphApplication::UpscalerKind::Fsr3: return "fsr3";
    case VulkanGraphApplication::UpscalerKind::None: return "native";
    }
    return "native";
}

VulkanGraphApplication::UpscalerKind ParseUpscalerKind(std::string_view id) noexcept {
    if (id == "linear") return VulkanGraphApplication::UpscalerKind::Linear;
    if (id == "fsr1") return VulkanGraphApplication::UpscalerKind::Fsr1;
    if (id == "fsr3") return VulkanGraphApplication::UpscalerKind::Fsr3;
    return VulkanGraphApplication::UpscalerKind::None;
}

float DefaultUpscalerRenderScale(VulkanGraphApplication::UpscalerKind kind) noexcept {
    return kind == VulkanGraphApplication::UpscalerKind::None ? 1.0f : 2.0f / 3.0f;
}

bool envFlagEnabledOrDefault(const char* name, bool defaultValue) {
    const char* raw = std::getenv(name);
    if (raw == nullptr) return defaultValue;

    bool explicitValue = false;
    if (envFlagIsSet(name, explicitValue)) return explicitValue;
    return envFlagEnabled(name);
}

// Dynamic exposure config: the built-in scenes are the default SceneLook, so they contribute it as the
// scene layer (the space exposure preset). Environment overrides form the dev layer, which resolves above
// every scene: VIXEN_HDR_EXPOSURE_PRESET=<name> replaces all seven preset values and
// VIXEN_HDR_EXPOSURE_<PARAMETER>=<value> one of them (the node clamps it). Offscreen captures pin the
// adaptation step to 1/60 s so repeat runs are pixel-identical; VIXEN_HDR_EXPOSURE_DT=<seconds> overrides
// it (0 = graph frame time).
void ConfigureExposure(LightingConfigNode& lighting, bool offscreenPresentation, Vixen::Log::Logger& logger) {
    using Config = LightingConfigNodeConfig;
    Vixen::App::SetSceneLookLayer(lighting, Vixen::App::SceneLook{});
    Config::ExposurePreset preset = Vixen::App::ExposureSettings(Vixen::App::SceneLook{}.exposure);
    PartialLook dev;
    if (const char* name = std::getenv("VIXEN_HDR_EXPOSURE_PRESET"); name && *name) {
        if (const Config::ExposurePreset* found = Config::FindExposurePreset(name)) {
            preset = *found;
            dev.exposureKeyValue = found->keyValue;
            dev.exposureMinEV100 = found->minEV100;
            dev.exposureMaxEV100 = found->maxEV100;
            dev.exposureLowPercent = found->lowPercent;
            dev.exposureHighPercent = found->highPercent;
            dev.exposureSpeedUp = found->speedUp;
            dev.exposureSpeedDown = found->speedDown;
        } else {
            std::string known;
            for (const Config::ExposurePreset& candidate : Config::EXPOSURE_PRESETS)
                known += (known.empty() ? "" : ", ") + std::string(candidate.name);
            logger.Warning(std::string("[BuildRenderGraph] unknown VIXEN_HDR_EXPOSURE_PRESET '") + name +
                           "'; using '" + preset.name + "' (known: " + known + ")");
        }
    }
    const auto overrideExposure = [&dev](std::optional<float> PartialLook::*channel, const char* env) {
        if (const char* raw = std::getenv(env); raw && *raw) dev.*channel = std::strtof(raw, nullptr);
    };
    overrideExposure(&PartialLook::exposureKeyValue, "VIXEN_HDR_EXPOSURE_KEY_VALUE");
    overrideExposure(&PartialLook::exposureMinEV100, "VIXEN_HDR_EXPOSURE_MIN_EV100");
    overrideExposure(&PartialLook::exposureMaxEV100, "VIXEN_HDR_EXPOSURE_MAX_EV100");
    overrideExposure(&PartialLook::exposureLowPercent, "VIXEN_HDR_EXPOSURE_LOW_PERCENT");
    overrideExposure(&PartialLook::exposureHighPercent, "VIXEN_HDR_EXPOSURE_HIGH_PERCENT");
    overrideExposure(&PartialLook::exposureSpeedUp, "VIXEN_HDR_EXPOSURE_SPEED_UP");
    overrideExposure(&PartialLook::exposureSpeedDown, "VIXEN_HDR_EXPOSURE_SPEED_DOWN");
    LookContribution devContribution;
    devContribution.look = dev;
    lighting.Look().SetSlot(LookLayer::DevOverride, 0, std::move(devContribution));
    float fixedDeltaSeconds = offscreenPresentation ? 1.0f / 60.0f : 0.0f;
    if (const char* raw = std::getenv("VIXEN_HDR_EXPOSURE_DT"); raw && *raw) {
        fixedDeltaSeconds = std::strtof(raw, nullptr);
    }
    lighting.SetParameter(Config::PARAM_EXPOSURE_FIXED_DELTA_SECONDS, fixedDeltaSeconds);
    logger.Info("[BuildRenderGraph] HDR exposure preset '" + std::string(preset.name) +
                "', adaptation step " + (fixedDeltaSeconds > 0.0f
                    ? std::to_string(fixedDeltaSeconds) + " s (pinned)" : std::string("frame time")));
}

enum class RtLightingMode { Auto, Off, Force };

RtLightingMode ParseRtLightingMode() {
    const char* raw = std::getenv("VIXEN_RT_LIGHTING");
    if (raw == nullptr) return RtLightingMode::Auto;
    std::string value(raw);
    value.erase(std::remove_if(value.begin(), value.end(),
                               [](unsigned char c) { return std::isspace(c); }), value.end());
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value.empty() || value == "auto") return RtLightingMode::Auto;
    if (value == "off") return RtLightingMode::Off;
    if (value == "force") return RtLightingMode::Force;
    throw std::runtime_error("VIXEN_RT_LIGHTING must be one of auto, off, or force");
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

} // namespace


void VulkanGraphApplication::SelectNativeUpscalerForModulelessGraph() {
    // The production graph's native candidate passes every gate: it is compiled in, permitted
    // by every profile and pass, has no runtime capability, and owns no external context.
    RenderModuleEligibility native;
    native.compiled = true;
    native.permittedByRuntimeProfile = true;
    native.permittedByPass = true;
    native.runtimeSupported = true;
    native.inputCompatible = true;
    native.initialized = true;
    native.qualified = true;
    native.admittedByBudget = true;
    upscalerCandidatesForCompile_ = {{"native", native, true, false}};
    const RenderModuleSelectionResult selection =
        SelectRenderModuleProviderForInitialization(upscalerCandidatesForCompile_);
    selectedUpscalerProviderId_ = selection.selected ? std::string(selection.selected->id) : std::string{};
    selectedUpscalerHasRealContext_ = selection.selected != nullptr;
}

void VulkanGraphApplication::BuildRenderGraph() {
    upscalerPerfPasses_.clear();
    proceduralShaderLibNodes_.clear();
    if (!renderGraph) {
        mainLogger->Error("Cannot build render graph: RenderGraph not initialized");
        return;
    }

    // Resolve the machine profile before any render-target extent or dispatch is derived. The
    // profile supplies a performance default only when the host did not explicitly choose an
    // upscaler; VIXEN_UPSCALER and an explicit RenderPathConfig remain authoritative.
    Vixen::CapabilityGraph upscalerCapabilities;
    PopulateSelectedDeviceCapabilities(instanceObj.instance, upscalerCapabilities);
    auto upscalerRuntime = upscalerCapabilities.GetUpscalerCapabilityRecords().runtime;
    const auto* runtimeProfile = Vixen::ResolveUpscalerMachineProfile(
        upscalerRuntime, RuntimeOperatingSystem());
    upscalerRuntime.machineProfileId = runtimeProfile ? runtimeProfile->profileId : std::string{};
    upscalerCapabilities.SetUpscalerRuntimeFacts(std::move(upscalerRuntime));
    if (runtimeProfile && !std::getenv("VIXEN_UPSCALER") &&
        renderPathConfig_.upscaler == UpscalerKind::None &&
        runtimeProfile->defaultUpscaler != "native") {
        renderPathConfig_.upscaler = ParseUpscalerKind(runtimeProfile->defaultUpscaler);
    }
    if (!renderPathConfig_.renderScaleExplicit && renderPathConfig_.upscaler != UpscalerKind::None) {
        renderPathConfig_.renderScale = runtimeProfile &&
            runtimeProfile->defaultUpscaler == UpscalerKindId(renderPathConfig_.upscaler)
            ? runtimeProfile->defaultRenderScale
            : DefaultUpscalerRenderScale(renderPathConfig_.upscaler);
    }

    // The unattended HUD capture runner can exercise the production graph without DISPLAY or a
    // swapchain. Keep ordinary app launches on the window presentation path.
    if (envFlagEnabled("VIXEN_HUD_OFFSCREEN_CAPTURE")) {
        SetPresentationTarget(PresentationTarget::Offscreen);
    }

    const RenderPathConfig& renderPathConfig = GetRenderPathConfig();
    const bool hdrExposureEnabled = envFlagEnabledOrDefault("VIXEN_HDR_EXPOSURE", true);
#if defined(VIXEN_CAPTURE_TEST_HOOKS)
    // Explicit 0 selects the system tone map (same contract as VIXEN_HDR_EXPOSURE), not the reference.
    const bool exposureReference = envFlagEnabledOrDefault("VIXEN_EXPOSURE_TONEMAP_REFERENCE", false);
#else
    constexpr bool exposureReference = false;
#endif
    // Anti-aliasing module (R676): the same selection predicate as the upscaler, over the AA
    // backend table. `none` is the terminal pass-through and adds no work. It is chosen before the
    // generated systems are instantiated because a provider's work is one of them. The temporal
    // upscaler gate (M3) reads the requested upscaler: FSR 3 is the only history-holding one.
    const RenderModuleFrameInput antiAliasingInputs[] = {
        RenderModuleFrameInput::Color, RenderModuleFrameInput::Depth,
        RenderModuleFrameInput::MotionVectors, RenderModuleFrameInput::Jitter};
    const bool interactiveSampling =
        renderPathConfig.samplingPath == ReconstructionSamplingPath::InteractiveVisual;
    // Jittered sampling and its motion inputs exist only where the temporal path does (the
    // same conditions the temporal-sampling guard below enforces). The capture-test hand tone-map
    // reference replaces the generated systems, so it has no temporal resolve either.
    const bool temporalInputsAvailable = interactiveSampling && hdrExposureEnabled && !exposureReference &&
        !envFlagEnabled("VIXEN_B1_OCCLUSION_CULL") && !envFlagEnabled("VIXEN_B2_PROXY_PREPASS") &&
        !envFlagEnabled("VIXEN_DEBUG_CAPTURE");
    const AntiAliasingFrameDescription antiAliasingFrame{
        .presentationSampling = interactiveSampling,
        .temporalUpscalerSelected = renderPathConfig.upscaler == UpscalerKind::Fsr3,
        .colorSpace = hdrExposureEnabled ? AntiAliasingColorSpace::LinearHdr
                                         : AntiAliasingColorSpace::DisplayReferred,
        .colorFormat = hdrExposureEnabled ? RenderModuleImageFormat::Rgba16Float
                                          : RenderModuleImageFormat::Rgba8Unorm,
        // The interactive path can jitter and produce depth and motion for a provider that needs them.
        .availableFrameInputs = temporalInputsAvailable ? std::span<const RenderModuleFrameInput>(antiAliasingInputs)
                                                        : std::span<const RenderModuleFrameInput>(antiAliasingInputs, 1),
    };
    const auto antiAliasingCandidates =
        BuildAntiAliasingCandidates(renderPathConfig.antiAliasing, antiAliasingFrame);
    const RenderModuleSelectionResult antiAliasingSelection =
        SelectRenderModuleProviderForInitialization(antiAliasingCandidates);
    for (const auto& rejected : antiAliasingSelection.rejected) {
        const auto* backend = FindAntiAliasingBackend(rejected.id);
        const std::string_view passRefusal =
            backend ? AntiAliasingPassRefusal(*backend, antiAliasingFrame) : std::string_view{};
        if (mainLogger) {
            mainLogger->Info("[BuildRenderGraph] Anti-aliasing provider '" + std::string(rejected.id) +
                             "' rejected: " +
                             std::string(RenderModuleSelectionFailureText(rejected.reason)) +
                             (rejected.reason == RenderModuleSelectionFailureReason::NotPermittedByPass &&
                                      !passRefusal.empty()
                                  ? " (" + std::string(passRefusal) + ")"
                                  : std::string{}));
        }
    }
    selectedAntiAliasingProviderId_ = antiAliasingSelection.selected
        ? std::string(antiAliasingSelection.selected->id) : std::string("none");
    // The TAA resolves jittered samples through the engine motion vectors (R676.1).
    const bool taaSelected = selectedAntiAliasingProviderId_ == "taa";
    if (mainLogger) mainLogger->Info("[BuildRenderGraph] Anti-aliasing provider: " + selectedAntiAliasingProviderId_);

    const bool jitterEnabled = (renderPathConfig.temporalJitter || taaSelected ||
        renderPathConfig.upscaler == UpscalerKind::Fsr3) &&
        renderPathConfig.samplingPath == ReconstructionSamplingPath::InteractiveVisual;
    const bool reconstructionInputsEnabled = envFlagEnabled("VIXEN_RECONSTRUCTION_DEPTH") || jitterEnabled;
    // The capture executable is the only binary that registers this test module.
    // Keep its explicit opt-in out of every ordinary/default graph build.
    const bool testIndirectLightingEnabled =
        envFlagEnabled("VIXEN_LOOKDEV_TEST_INDIRECT_LIGHTING");
    if (jitterEnabled && (!hdrExposureEnabled || envFlagEnabled("VIXEN_B1_OCCLUSION_CULL") ||
        envFlagEnabled("VIXEN_B2_PROXY_PREPASS") || envFlagEnabled("VIXEN_DEBUG_CAPTURE")))
        throw std::invalid_argument("Temporal sampling requires HDR inputs and non-proxy traversal without trace capture");
    const bool hdrExposureFixed = hdrExposureEnabled &&
                                 envFlagEnabled("VIXEN_HDR_EXPOSURE_FIXED");
    const RtLightingMode rtLightingMode = ParseRtLightingMode();
    const bool waveSlotSwizzleEnabled = envFlagEnabled("VIXEN_WAVE_SLOT_SWIZZLE");
    // S0: opt into the UI-only RmlUi demo graph via env var, leaving the voxel path untouched.
    if (envFlagEnabled("VIXEN_UI_DEMO")) {
        mainLogger->Info("VIXEN_UI_DEMO set - building UI-only RmlUi demo graph");
        BuildUIGraph();
        SelectNativeUpscalerForModulelessGraph();
        return;
    }

    // AR#31: opt into the isolated instanced-cube raster demo via env var, leaving the
    // live voxel-compute path untouched.
    if (envFlagEnabled("VIXEN_INSTANCING_DEMO")) {
        mainLogger->Info("VIXEN_INSTANCING_DEMO set - building instanced-cube raster demo graph");
        BuildInstancingDemoGraph();
        SelectNativeUpscalerForModulelessGraph();
        return;
    }

    // AR#21 P4: opt into the isolated auto-sync FrameGraph demo via env var. Proves
    // buffer-hazard auto-synchronization (compute->compute->render->present in ONE
    // command buffer via PassGroupNode). Leaves the live voxel-compute path untouched.
    if (envFlagEnabled("VIXEN_AUTOSYNC_DEMO")) {
        mainLogger->Info("VIXEN_AUTOSYNC_DEMO set - building auto-sync FrameGraph demo graph");
        BuildAutoSyncDemoGraph();
        SelectNativeUpscalerForModulelessGraph();
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
        SelectNativeUpscalerForModulelessGraph();
        return;
    }

    mainLogger->Info("Building complete render pipeline with typed connections");

    const bool offscreenPresentation =
        GetPresentationTarget() == PresentationTarget::Offscreen;

    // ===================================================================
    // PHASE 1: Create all nodes
    // ===================================================================

    // --- Infrastructure Nodes ---
    NodeHandle instanceNode = renderGraph->AddNode<InstanceNodeType>( "main_instance");  // Phase 1.1
    NodeHandle windowNode{};
    NodeHandle swapChainNode{};
    NodeHandle offscreenTargetNode{};
    NodeHandle presentNode{};
    if (offscreenPresentation) {
        offscreenTargetNode = renderGraph->AddNode<RenderTargetNodeType>("main_offscreen_target");
    } else {
        windowNode = renderGraph->AddNode<WindowNodeType>("main_window");
        swapChainNode = renderGraph->AddNode<SwapChainNodeType>("main_swapchain");
        presentNode = renderGraph->AddNode<PresentNodeType>("present");
    }
    windowNode_ = windowNode;                        // null in the windowless path
    offscreenTargetNode_ = offscreenTargetNode;
    NodeHandle deviceNode = renderGraph->AddNode<DeviceNodeType>("main_device");
    offscreenDeviceNode_ = offscreenPresentation ? deviceNode : NodeHandle{};
    // Adapter-selection visibility: DeviceNode's NODE_LOG_INFO calls (incl. "[DeviceNode] Selected
    // GPU N: ...") are dead by default -- nodeLogger is constructed enabled=false (NodeInstance.cpp)
    // and nothing else opts this node in. Enable it so every session's GPU choice is on record.
    if (Logger* deviceLogger = renderGraph->GetInstance(deviceNode)->GetLogger()) {
        deviceLogger->SetEnabled(true);
        deviceLogger->SetTerminalOutput(true);
    }
    NodeHandle commandPoolNode = renderGraph->AddNode<CommandPoolNodeType>("main_cmd_pool");
    const NodeHandle presentationTargetNode = offscreenPresentation ? offscreenTargetNode : swapChainNode;
    ConnectionBatch batch(renderGraph);
    const auto targetContract = batch.GetPresentationTargetContract(presentationTargetNode);
    const bool usesWsi = targetContract.synchronization == Vixen::Vulkan::Resources::TargetSynchronization::WsiAcquirePresent;

    // --- Phase G: Compute Pipeline Nodes ---
    NodeHandle computeShaderLib = renderGraph->AddNode<ShaderLibraryNodeType>("compute_shader_lib");
    computeShaderLibNode_ = computeShaderLib;
    proceduralShaderLibNodes_.push_back(computeShaderLib);
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
    static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))->SetMotionGuidesEnabled(reconstructionInputsEnabled);
    bodyOctreeSceneNode_ = bodyOctreeSceneNode;      // store so SetBodyInstances() can forward to it

    // Sampled Lighting Inc0 M3: LightingConfig data (binding 16). Static default content
    // this increment (a single directional light matching Lighting.glsl's old hardcoded
    // default) uploaded per-frame through a PerFrameResources ring, mirroring
    // DynamicInstanceBufferNode's pattern.
    NodeHandle lightingConfigNode = renderGraph->AddNode<LightingConfigNodeType>("lighting_config");
    if (hdrExposureEnabled) {
        if (auto* lighting = static_cast<LightingConfigNode*>(renderGraph->GetInstance(lightingConfigNode))) {
            ConfigureExposure(*lighting, offscreenPresentation, *mainLogger);
        }
    }
#if defined(VIXEN_CAPTURE_TEST_HOOKS)
    // The native capture witness compares the preserved Lambert+GGX path with the
    // lane's untouched baseline. Product hosts select this same mode through the
    // node parameter; this environment hook exists only in BUILD_TESTS binaries.
    if (envFlagEnabled("VIXEN_TEST_CELSHADE_LAMBERT_GGX")) {
        if (auto* lighting = static_cast<LightingConfigNode*>(renderGraph->GetInstance(lightingConfigNode))) {
            lighting->SetParameter(LightingConfigNodeConfig::PARAM_SHADING_MODE,
                                   LightingConfigNodeConfig::SHADING_MODE_LAMBERT_GGX);
            const uint32_t selectedMode = lighting->GetParameterValue<uint32_t>(
                LightingConfigNodeConfig::PARAM_SHADING_MODE, UINT32_MAX);
            mainLogger->Info("[capture-test] shadingMode parameter=" + std::to_string(selectedMode));
        } else {
            mainLogger->Warning("[capture-test] lighting_config node instance unavailable for mode override");
        }
    }
#endif

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
    // spatial-reuse neighbor search, then shading and writing the scene radiance plus distinct
    // current history images. Own shaderLib/gatherer/
    // pushConstantGatherer/descSet/pipeline quintet — same "second compiled shader needs
    // its own instances" rationale as directLighting*'s own quintet (see that block's
    // comment above).
    NodeHandle spatialReuseShaderLib = renderGraph->AddNode<ShaderLibraryNodeType>("spatial_reuse_shader_lib");
    // Slice C: plumbing synthesized at the wire site; push-gatherer handle assigned there.
    NodeHandle spatialReusePushConstantGatherer{};
    NodeHandle spatialReuseNode = renderGraph->AddNode<ComputeStageNodeType>("spatial_reuse");
    // The stage the exposure chain (meter, else reduce) waits on: the scene radiance's writer, or
    // the AA provider's resolve when one runs.
    NodeHandle exposureInputProducer = spatialReuseNode;
    SubGraphProviderSelection testIndirectLightingProvider;
    if (testIndirectLightingEnabled) {
        Vixen::CapabilityGraph testModuleCapabilities;
        testModuleCapabilities.RegisterCapability(
            std::make_shared<Vixen::CompositeCapability>("Test:IndirectLighting"));
        testIndirectLightingProvider = renderGraph->InstantiateProviderForInput(
            testModuleCapabilities, spatialReuseNode,
            ComputeStageNodeConfig::INDIRECT_LIGHTING.index,
            "capture_test_indirect_lighting");
        if (!testIndirectLightingProvider.group.IsValid()) {
            throw std::runtime_error(
                "Test indirect-lighting capability did not select a registered provider group");
        }
    }
    // SpatialReuseShade reads the visibility bits that ShadowVisibilityWave writes into
    // HitRecordBuffer. Its shader descriptor is supplied through sceneProviders below; this
    // separate gatherer declares the GPU read hazard to ResourceAccessTracker.
    NodeHandle spatialReuseReadGatherer = renderGraph->AddNode<BufferSyncGathererNodeType>("spatial_reuse_read_gatherer");
    NodeHandle skySphereNode = renderGraph->AddNode<SkySphereNodeType>("sky_sphere");

    NodeHandle sceneRadianceNode = renderGraph->AddNode<SceneRadianceNodeType>("scene_radiance");
    NodeHandle exposureShaderLib{};
    NodeHandle exposureNode{};
    NodeHandle exposureMeterShaderLib{};
    NodeHandle exposureMeterNode{};
    NodeHandle exposureReduceShaderLib{};
    NodeHandle exposureReduceNode{};
    NodeHandle exposureMeterBuffer{};
    generatedSystems_ = {};
    if (hdrExposureEnabled) {
        // The vixen: frame-targets composition's generated systems (today the tone map) come from
        // its manifest; only the capture-test hand reference still builds its own stage.
        if (exposureReference) {
            exposureShaderLib = renderGraph->AddNode<ShaderLibraryNodeType>("exposure_tonemap_shader_lib");
            exposureNode = renderGraph->AddNode<ComputeStageNodeType>("exposure_tonemap");
        } else {
            std::vector<std::filesystem::path> manifestPaths = {
#ifdef VIXEN_SHADER_SOURCE_DIR
                ShaderSourceDir() + "/Generated/GpuSystems.manifest.json",
#endif
                "shaders/Generated/GpuSystems.manifest.json",
                "../shaders/Generated/GpuSystems.manifest.json",
            };
            generatedSystems_.Instantiate(renderGraph, GpuSystemManifest::Load(manifestPaths),
                [this](const GpuSystemEntry& entry) {
                    return AntiAliasingSystemEnabled(entry.className, selectedAntiAliasingProviderId_);
                });
        }
        exposureReduceShaderLib = renderGraph->AddNode<ShaderLibraryNodeType>("exposure_reduce_shader_lib");
        exposureReduceNode = renderGraph->AddNode<ComputeStageNodeType>("exposure_reduce");
        exposureMeterBuffer = renderGraph->AddNode<StorageBufferNodeType>("exposure_meter_buffer");
        if (!hdrExposureFixed) {
            exposureMeterShaderLib = renderGraph->AddNode<ShaderLibraryNodeType>("exposure_meter_shader_lib");
            exposureMeterNode = renderGraph->AddNode<ComputeStageNodeType>("exposure_meter");
        }
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
    // DirectLightingNode, before the optional sky-projection fixture and the UI composite, which
    // read the swapchain image.
    NodeHandle blitNode = renderGraph->AddNode<BlitNodeType>("render_target_blit");

    // The optional upscaler is instantiated later into the typed UpscalerModulePorts slot,
    // after render-target extent, shader providers, and stage-order inputs are available.
    std::optional<SubGraphHandle> upscalerModule;
    std::optional<SubGraphHandle> reconstructionMotionModule;
    std::optional<SubGraphHandle> reconstructionDepthModule;
    std::optional<SubGraphHandle> reconstructionHistoryModule;
    std::optional<SubGraphHandle> reconstructionExposureModule;

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

    // Empty and disabled by default; callers can update the beam list at runtime.
    NodeHandle miningBeamBufferNode =
        renderGraph->AddNode<MiningBeamBufferNodeType>("mining_beam_buffer");

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
    // HDR exposure reads the post-composite scene-radiance image that SpatialReuseShade writes
    // through its descriptor provider. Keep separate gatherers on the write and read sides so
    // ResourceAccessTracker can derive the same-frame storage-image hazard.
    NodeHandle spatialReuseSceneRadianceWriteGatherer = renderGraph->AddNode<ImageSyncGathererNodeType>("spatial_reuse_scene_radiance_write_gatherer");
    NodeHandle exposureSceneRadianceReadGatherer = renderGraph->AddNode<ImageSyncGathererNodeType>("exposure_scene_radiance_read_gatherer");

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

    // C0/C1: march-side photon writer.  This is a sibling cache, not a
    // modification of HitAccum and not a HitRecord extension.  The table is
    // allocated only when explicitly enabled; its zero-initialized storage
    // survives ordinary graph recompiles.  Writer #2 (staged march/proxy)
    // remains an intentional v2 seam.
    const auto photonCellConfiguration = PhotonCellParamsConfigNode::ReadConfiguration();
    const bool photonCellsEnabled = PhotonCellParamsConfigNode::FeatureEnabled();
    const bool photonCellsClearEnabled =
        photonCellsEnabled && photonCellConfiguration.clearRequested;
    NodeHandle photonCellTableBuffer{};
    NodeHandle photonCellParamsBuffer{};
    NodeHandle photonCellDepositShaderLib{}, photonCellDepositNode{};
    NodeHandle photonCellDepositReadGatherer{}, photonCellDepositWriteGatherer{};
    NodeHandle photonCellFoldShaderLib{}, photonCellFoldNode{};
    NodeHandle photonCellFoldReadGatherer{}, photonCellFoldWriteGatherer{};
    NodeHandle photonCellClearShaderLib{}, photonCellClearNode{};
    NodeHandle photonCellClearWriteGatherer{};
    if (photonCellsEnabled) {
        photonCellTableBuffer = renderGraph->AddNode<PhotonCellTableNodeType>(
            "photon_cell_table_buffer");
        photonCellParamsBuffer = renderGraph->AddNode<PhotonCellParamsConfigNodeType>(
            "photon_cell_params_buffer");
        photonCellDepositShaderLib = renderGraph->AddNode<ShaderLibraryNodeType>(
            "photon_cell_deposit_shader_lib");
        photonCellDepositNode = renderGraph->AddNode<PhotonCellDepositNodeType>(
            "photon_cell_deposit");
        photonCellDepositReadGatherer = renderGraph->AddNode<BufferSyncGathererNodeType>(
            "photon_cell_deposit_read_gatherer");
        photonCellDepositWriteGatherer = renderGraph->AddNode<BufferSyncGathererNodeType>(
            "photon_cell_deposit_write_gatherer");
        photonCellFoldShaderLib = renderGraph->AddNode<ShaderLibraryNodeType>(
            "photon_cell_fold_shader_lib");
        photonCellFoldNode = renderGraph->AddNode<PhotonCellFoldNodeType>(
            "photon_cell_fold");
        photonCellFoldReadGatherer = renderGraph->AddNode<BufferSyncGathererNodeType>(
            "photon_cell_fold_read_gatherer");
        photonCellFoldWriteGatherer = renderGraph->AddNode<BufferSyncGathererNodeType>(
            "photon_cell_fold_write_gatherer");
        if (photonCellsClearEnabled) {
            photonCellClearShaderLib = renderGraph->AddNode<ShaderLibraryNodeType>(
                "photon_cell_clear_shader_lib");
            photonCellClearNode = renderGraph->AddNode<PhotonCellClearNodeType>(
                "photon_cell_clear");
            photonCellClearWriteGatherer = renderGraph->AddNode<BufferSyncGathererNodeType>(
                "photon_cell_clear_write_gatherer");
        }
    }
    photonCellTableBuffer_ = photonCellTableBuffer;
    photonCellParamsBuffer_ = photonCellParamsBuffer;

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
    if (hitAccumResolveEnabled) {
        hitAccumCellRadianceBuffer = renderGraph->AddNode<StorageBufferNodeType>("hit_accum_cell_radiance_buffer");
        hitAccumCellShadeShaderLib = renderGraph->AddNode<ShaderLibraryNodeType>("hit_accum_cell_shade_shader_lib");
        hitAccumCellShadeNode      = renderGraph->AddNode<ComputeStageNodeType>("hit_accum_cell_shade");
        // Reads: the table (orders wave→cell-shade on the shared resource).
        // Writes: the cell-radiance buffer.
        hitAccumCellShadeReadGatherer  = renderGraph->AddNode<BufferSyncGathererNodeType>("hit_accum_cell_shade_read_gatherer");
        hitAccumCellShadeWriteGatherer = renderGraph->AddNode<BufferSyncGathererNodeType>("hit_accum_cell_shade_write_gatherer");
        // W-LEAN L3: NO standalone resolve — the composite is SpatialReuseShade's
        // own VIXEN_SRS_CELL_RESOLVE axis. The shared SpatialReuse read gatherer also
        // carries the fold's optional {cellRadiance, table} inputs.
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
    // same reason. One shared buffer, wired to all four gatherers below (the march's
    // descriptorGatherer + the three synthesized lighting desc-gatherers) -- its low six words
    // are populated by the generation-keyed CPU publisher while B1 owns the high six words.
    // The remaining 256-byte capacity preserves the existing table shape and test convention.
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

    // --- Raster-proxy B2: same-frame interval/order pre-pass (default OFF) ---
    // Owner ruling 0dw: proxies accelerate, geometry occludes. This graphics
    // pass writes only the compact candidate SSBO; B1's marched-depth HiZ stays
    // the sole cull source and is deliberately not rewired here.
    const char* b2ProxyPrepassEnv = std::getenv("VIXEN_B2_PROXY_PREPASS");
    const bool b2ProxyPrepassEnabled =
        b2ProxyPrepassEnv && std::string_view(b2ProxyPrepassEnv) != "0" &&
        std::string_view(b2ProxyPrepassEnv) != "off";
    const char* b2ComputeWriterEnv = std::getenv("VIXEN_B2_PROXY_PREPASS_COMPUTE");
    const bool b2ForceComputeWriter =
        b2ComputeWriterEnv && std::string_view(b2ComputeWriterEnv) != "0" &&
        std::string_view(b2ComputeWriterEnv) != "off";
    NodeHandle b2CandidateBuffer{}, b2ShaderLib{}, b2DescGatherer{};
    NodeHandle b2DescriptorSet{}, b2RenderPass{}, b2Framebuffer{};
    NodeHandle b2Pipeline{}, b2Stage{}, b2WriteGatherer{}, b2ReadGatherer{};
    NodeHandle b2ComputeShaderLib{}, b2ComputeDescGatherer{};
    NodeHandle b2ComputeDescriptorSet{}, b2ComputePipeline{};
    if (b2ProxyPrepassEnabled) {
        b2CandidateBuffer = renderGraph->AddNode<StorageBufferNodeType>("b2_proxy_interval_buffer");
        b2ShaderLib = renderGraph->AddNode<ShaderLibraryNodeType>("b2_proxy_shader_lib");
        b2DescGatherer = renderGraph->AddNode<DescriptorResourceGathererNodeType>("b2_proxy_desc_gatherer");
        b2DescriptorSet = renderGraph->AddNode<DescriptorSetNodeType>("b2_proxy_descriptors");
        b2RenderPass = renderGraph->AddNode<RenderPassNodeType>("b2_proxy_render_pass");
        b2Framebuffer = renderGraph->AddNode<FramebufferNodeType>("b2_proxy_framebuffer");
        b2Pipeline = renderGraph->AddNode<GraphicsPipelineNodeType>("b2_proxy_pipeline");
        b2Stage = renderGraph->AddNode<ProxyRasterStageNodeType>("b2_proxy_raster_stage");
        b2WriteGatherer = renderGraph->AddNode<BufferSyncGathererNodeType>("b2_proxy_write_gatherer");
        b2ReadGatherer = renderGraph->AddNode<BufferSyncGathererNodeType>("b2_proxy_read_gatherer");
        b2ComputeShaderLib = renderGraph->AddNode<ShaderLibraryNodeType>(
            "b2_proxy_compute_shader_lib");
        b2ComputeDescGatherer = renderGraph->AddNode<DescriptorResourceGathererNodeType>(
            "b2_proxy_compute_desc_gatherer");
        b2ComputeDescriptorSet = renderGraph->AddNode<DescriptorSetNodeType>(
            "b2_proxy_compute_descriptors");
        b2ComputePipeline = renderGraph->AddNode<ComputePipelineNodeType>(
            "b2_proxy_compute_pipeline");
    }

    // --- Input Node ---
    NodeHandle inputNode = renderGraph->AddNode<InputNodeType>("input_handler");
    inputNode_ = inputNode;                          // store for Update()'s live ProcessPendingInput() lookup

    // --- Pick ID Target (AR#35 GPU picking: RG32_UINT storage-image ring at binding 9) ---
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

    // --- Sky-projection fixture pass (Tiered ESVO Inc1 M3: address-derived sky points) ---
    // This synthetic fixture is available to focused tests with VIXEN_SKY_PROJECTION_FIXTURE=1.
    // Keep its render-pass/framebuffer/node triple out of ordinary frames and captures by default.
    const bool skyProjectionFixtureEnabled =
        envFlagEnabledOrDefault("VIXEN_SKY_PROJECTION_FIXTURE", false);
    NodeHandle skyProjectionRenderPassNode{};
    NodeHandle skyProjectionFramebufferNode{};
    NodeHandle skyProjectionNode{};
    skyProjectionNode_ = {};
    if (skyProjectionFixtureEnabled) {
        // A color-only graphics pass layered between the compute output and UI/HUD composite.
        // The UI remains the last pass and transitions the swapchain image to PresentSrc.
        skyProjectionRenderPassNode = renderGraph->AddNode<RenderPassNodeType>("sky_projection_render_pass");
        skyProjectionFramebufferNode = renderGraph->AddNode<FramebufferNodeType>("sky_projection_framebuffer");
        skyProjectionNode = renderGraph->AddNode<SkyProjectionNodeType>("sky_projection");
        skyProjectionNode_ = skyProjectionNode;       // stored for potential live lookup
        // Node loggers default disabled; expose the fixture's computed direction/magnitude values
        // when a focused test explicitly opts in.
        if (auto* skyInst = renderGraph->GetInstance(skyProjectionNode)) {
            if (auto* sl = skyInst->GetLogger()) { sl->SetEnabled(true); sl->SetTerminalOutput(true); }
        }
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

    // Windowed and offscreen graphs share the same render stages. Only the terminal target and
    // its WSI setup differ; target usage derives from consumers and public readback.
    auto* instance = static_cast<InstanceNode*>(renderGraph->GetInstance(instanceNode));
    instance->SetParameter(InstanceNodeConfig::PARAM_ENABLE_PRESENTATION, usesWsi);
    if (usesWsi) {
        auto* window = static_cast<WindowNode*>(renderGraph->GetInstance(windowNode));
        window->SetParameter(WindowNodeConfig::PARAM_WIDTH, static_cast<uint32_t>(width));
        window->SetParameter(WindowNodeConfig::PARAM_HEIGHT, static_cast<uint32_t>(height));
    } else {
        auto* target = static_cast<RenderTargetNode*>(renderGraph->GetInstance(offscreenTargetNode));
        target->SetParameter(RenderTargetNodeConfig::PARAM_WIDTH, static_cast<uint32_t>(width));
        target->SetParameter(RenderTargetNodeConfig::PARAM_HEIGHT, static_cast<uint32_t>(height));
        target->SetParameter(RenderTargetNodeConfig::PARAM_IMAGE_COUNT,
                             FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT);
    }

    // Device parameters (auto-select: prefers a discrete GPU over integrated)
    auto* device = static_cast<DeviceNode*>(renderGraph->GetInstance(deviceNode));
    device->SetParameter(DeviceNodeConfig::PARAM_GPU_INDEX, DeviceNodeConfig::GPU_INDEX_AUTO);
    device->SetParameter(DeviceNodeConfig::PARAM_ENABLE_PRESENTATION, usesWsi);

    // Render scale belongs to the render-path configuration and affects the dispatch extent only.
    const float renderScale = renderPathConfig.renderScale;
    auto* renderTarget = static_cast<RenderTargetNode*>(renderGraph->GetInstance(renderTargetNode));
    renderTarget->SetParameter(RenderTargetNodeConfig::PARAM_SCALE, renderScale);
    const VkExtent2D renderExtent = RenderTargetNode::ComputeFollowExtent(
        {static_cast<uint32_t>(width), static_cast<uint32_t>(height)}, renderScale);
    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] Render-path scale=" + std::to_string(renderScale) +
                         " (1.0 = full resolution)");
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

    // T-1138 defaults off to preserve the existing background exactly. Enable at runtime by
    // setting `enabled` on the `sky_sphere` node; seed and brightness are live node parameters.
    auto* skySphere = static_cast<SkySphereNode*>(renderGraph->GetInstance(skySphereNode));
    skySphere->SetParameter(SkySphereNodeConfig::PARAM_WIDTH, 1024u);
    skySphere->SetParameter(SkySphereNodeConfig::PARAM_HEIGHT, 512u);
    skySphere->SetParameter(SkySphereNodeConfig::PARAM_FORMAT,
        static_cast<uint32_t>(VK_FORMAT_R16G16B16A16_SFLOAT));
    skySphere->SetParameter(SkySphereNodeConfig::PARAM_ENABLED, 0u);
    skySphere->SetParameter(SkySphereNodeConfig::PARAM_SEED, 0x5a17f13du);
    skySphere->SetParameter(SkySphereNodeConfig::PARAM_BRIGHTNESS, 0.45f);
    skySphere->SetParameter(SkySphereNodeConfig::PARAM_REFRESH_CADENCE_FRAMES, 0u);

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

    if (hdrExposureEnabled) {
        // ExposureHistogram.glsl: 16-byte ExposureMeterState, then 128 uint histogram bins.
        constexpr uint64_t exposureMeterBytes = 16ull + 4ull * 128ull;
        static_cast<StorageBufferNode*>(renderGraph->GetInstance(exposureMeterBuffer))
            ->SetParameter(StorageBufferNodeConfig::PARAM_SIZE_BYTES,
                           static_cast<uint32_t>(exposureMeterBytes));
    }

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
    // fixed-size convention just above). The first six words are bucket ownership and the next
    // six are B1 camera visibility; the remaining zeroed words preserve the existing headroom.
    auto* instanceSkipMaskInst = static_cast<StorageBufferNode*>(renderGraph->GetInstance(instanceSkipMaskBuffer));
    instanceSkipMaskInst->SetParameter(StorageBufferNodeConfig::PARAM_SIZE_BYTES, 256u);
    instanceSkipMaskInst->SetParameter(StorageBufferNodeConfig::PARAM_FRAME_RING_SIZE,
                                       FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT);

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
        boundSphereInst->SetParameter(StorageBufferNodeConfig::PARAM_FRAME_RING_SIZE,
                                       FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT);

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
            kRecipeBucketingMaxBuckets * 12u * (1u +
                ((static_cast<uint32_t>(width) + 8u * DispatchPass::BoundedGroupEdge - 1u) /
                    (8u * DispatchPass::BoundedGroupEdge)) *
                ((static_cast<uint32_t>(height) + 8u * DispatchPass::BoundedGroupEdge - 1u) /
                    (8u * DispatchPass::BoundedGroupEdge))));

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
        bucketMetaInst->SetParameter(StorageBufferNodeConfig::PARAM_FRAME_RING_SIZE,
                                     FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT);

        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Info("[BuildRenderGraph] Recipe bucketing SSBOs sized: maxBuckets=" +
                             std::to_string(kRecipeBucketingMaxBuckets) + " maxMembersPerBucket=" +
                             std::to_string(kRecipeBucketingMaxMembersPerBucket));
        }

        // Mode literals: mode 1 (init) -> mode 0 (bucket) -> mode 2 (finalize), matching
        // RecipeInstanceBucketing.comp's own push-constant `mode` semantics exactly.
        static_cast<ConstantNode*>(renderGraph->GetInstance(recipeBucketingModeInitConstant))->SetValue<uint32_t>(1u);
        static_cast<ConstantNode*>(renderGraph->GetInstance(recipeBucketingModeBucketConstant))->SetValue<uint32_t>(0u);
        static_cast<ConstantNode*>(renderGraph->GetInstance(recipeBucketingModeFinalConstant))->SetValue<uint32_t>((DispatchPass::BoundedGroupEdge << 8u) | 3u);
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

    // Raster-proxy B1 M4: occlusion-probe parameters. Depth/tile extents use the same
    // canonical ceil-rounded render extent as the lighting dispatches (a live window resize
    // rebuilds the graph; renderScale is process-fixed).
    if (b1OcclusionCullEnabled) {
        const uint32_t b1SrcW = renderExtent.width;
        const uint32_t b1SrcH = renderExtent.height;
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

    if (b2ProxyPrepassEnabled) {
        // One 32-byte compact record per render pixel. TransferDst is required
        // for the stage's single all-zero vkCmdFillBuffer clear.
        auto* candidate = static_cast<StorageBufferNode*>(
            renderGraph->GetInstance(b2CandidateBuffer));
        candidate->SetParameter(StorageBufferNodeConfig::PARAM_BYTES_PER_PIXEL, 32u);
        candidate->SetParameter(StorageBufferNodeConfig::PARAM_EXTRA_USAGE_FLAGS,
                                static_cast<uint32_t>(VK_BUFFER_USAGE_TRANSFER_DST_BIT));

        auto* pass = static_cast<RenderPassNode*>(renderGraph->GetInstance(b2RenderPass));
        pass->SetParameter(RenderPassNodeConfig::PARAM_COLOR_LOAD_OP, AttachmentLoadOp::DontCare);
        pass->SetParameter(RenderPassNodeConfig::PARAM_COLOR_STORE_OP, AttachmentStoreOp::DontCare);
        pass->SetParameter(RenderPassNodeConfig::PARAM_INITIAL_LAYOUT, ImageLayout::Undefined);
        pass->SetParameter(RenderPassNodeConfig::PARAM_FINAL_LAYOUT, ImageLayout::General);
        pass->SetParameter(RenderPassNodeConfig::PARAM_SAMPLES, 1u);

        static_cast<FramebufferNode*>(renderGraph->GetInstance(b2Framebuffer))
            ->SetParameter(FramebufferNodeConfig::PARAM_LAYERS, 1u);

        auto* pipeline = static_cast<GraphicsPipelineNode*>(renderGraph->GetInstance(b2Pipeline));
        pipeline->SetParameter(GraphicsPipelineNodeConfig::ENABLE_DEPTH_TEST, false);
        pipeline->SetParameter(GraphicsPipelineNodeConfig::ENABLE_DEPTH_WRITE, false);
        pipeline->SetParameter(GraphicsPipelineNodeConfig::ENABLE_VERTEX_INPUT, false);
        pipeline->SetParameter(GraphicsPipelineNodeConfig::CULL_MODE, std::string("None"));
        pipeline->SetParameter(GraphicsPipelineNodeConfig::POLYGON_MODE, std::string("Fill"));
        pipeline->SetParameter(GraphicsPipelineNodeConfig::TOPOLOGY, std::string("TriangleList"));
        pipeline->SetParameter(GraphicsPipelineNodeConfig::FRONT_FACE,
                               std::string("CounterClockwise"));
        pipeline->SetParameter(GraphicsPipelineNodeConfig::REQUIRED_DEVICE_CAPABILITY,
                               std::string("DeviceFeature:fragmentStoresAndAtomics"));

        auto* stage = static_cast<ProxyRasterStageNode*>(renderGraph->GetInstance(b2Stage));
        stage->SetParameter(ProxyRasterStageNodeConfig::PARAM_FORCE_COMPUTE_WRITER,
                            b2ForceComputeWriter);
        if (auto logger = stage->GetLogger()) {
            logger->SetEnabled(true);
            logger->SetTerminalOutput(true);
        }

        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Info(
                std::string("[BuildRenderGraph] B2 proxy pre-pass enabled: 32 B/pixel union interval + 192-bit candidate mask; writer=") +
                (b2ForceComputeWriter ? "compute (forced)" : "CapabilityGraph-selected"));
        }
    }

    // Critique R6: no vkDeviceWaitIdle per present. The frame is already paced by the
    // per-flight in-flight fences + imageAvailable/renderComplete semaphores (+ per-image
    // present fences when VK_EXT_swapchain_maintenance1 is available — SwapChainNode waits
    // them after acquire). The full device drain serialized every frame and dominated frame
    // time through the WSLg paravirtualized device (~186ms/frame measured at 500x500).
    if (usesWsi) {
        auto* present = static_cast<PresentNode*>(renderGraph->GetInstance(presentNode));
        present->SetParameter(PresentNodeConfig::WAIT_FOR_IDLE, false);
    }

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
                ShaderSourceDir() + "/" + shaderName,
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
                    mainLogger->Error("[BuildRenderGraph] VIXEN_SHADER_SOURCE_DIR: " + ShaderSourceDir());
#endif
                }
                throw std::runtime_error(std::string(shaderName) + " not found - check shader search paths");
            }

            std::ifstream compFile(compPath);
            std::ostringstream compBuf;
            compBuf << compFile.rdbuf();
            std::string rawSource = compBuf.str();

            // R768: the production uber shader routes GlyphOutline through the packed binding
            // 52 helper from RecipeGlyphBindings.glsl. Strip the unreachable generated typed
            // glyph helper from this one source assembly so the driver does not compile the
            // fixed-capacity aggregate in parallel with the live packed path. The canonical
            // generated file remains untouched for specialized shaders and its codegen drift
            // gate; this is a production-source composition choice, not a second kernel.
            constexpr std::string_view sdfInclude =
                "#include \"recipe/SdfCoreKernels.g.glsl\"";
            std::filesystem::path sdfKernelPath;
#ifdef VIXEN_SVO_SHADER_SOURCE_DIR
            sdfKernelPath = std::filesystem::path(VIXEN_SVO_SHADER_SOURCE_DIR) /
                            "recipe" / "SdfCoreKernels.g.glsl";
#else
            sdfKernelPath = std::filesystem::path("libraries/SVO/shaders/recipe/SdfCoreKernels.g.glsl");
#endif
            if (std::filesystem::exists(sdfKernelPath)) {
                std::ifstream sdfFile(sdfKernelPath);
                std::ostringstream sdfBuf;
                sdfBuf << sdfFile.rdbuf();
                const std::string compactSdfCore =
                    Vixen::SVO::Recipe::WithoutGlyphOutlineKernel(sdfBuf.str());
                const std::size_t includePos = rawSource.find(sdfInclude);
                if (!compactSdfCore.empty() && includePos != std::string::npos) {
                    rawSource.replace(includePos, sdfInclude.size(), compactSdfCore);
                }
            }

            std::string splicedSource;
            std::vector<float> occupancyGridBlob;
            std::vector<Yeroket::Sdf::Kernels::GlyphOutlineInstance> glyphOutlineBlob;
            try {
                splicedSource = Vixen::SVO::Recipe::SpliceProceduralRecipesIntoSource(
                    rawSource, proceduralRecipes_, &occupancyGridBlob, &glyphOutlineBlob);
            } catch (const std::exception& e) {
                if (mainLogger && mainLogger->IsEnabled()) {
                    mainLogger->Error(std::string("[BuildRenderGraph] procedural recipe splice failed: ") + e.what());
                }
                throw;
            }
            if (auto* bodyScene = static_cast<Vixen::RenderGraph::BodyOctreeSceneNode*>(
                    renderGraph->GetInstance(bodyOctreeSceneNode_))) {
                bodyScene->SetOccupancyGrid(std::move(occupancyGridBlob));
                bodyScene->SetRecipeChunkBoxes(envFlagEnabled("VIXEN_RECIPE_CHUNK_CULL")
                    ? Vixen::SVO::Recipe::DeriveRecipeChunkBoxes(proceduralRecipes_)
                    : std::vector<Vixen::SVO::Recipe::RecipeChunkAabb>{});
                bodyScene->SetGlyphOutlineInstances(std::move(glyphOutlineBlob));
                bodyScene->SetRecipeEmitterShapes(Vixen::SVO::BuildRecipeEmitterShapes(proceduralRecipes_));
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

            // Stored-shell normals are an interface-invariant shader feature:
            // the compact oct8 payload reuses the existing channelPool binding.
            // Keep the define in the canonical family source so the analytic
            // gradient remains the exact flag-off twin.
            if (envFlagEnabled("VIXEN_SHELL_NORMALS")) {
                const size_t firstNewlineNormals = splicedSource.find('\n');
                const std::string normalsDefine = "#define VIXEN_SHELL_NORMALS 1\n";
                if (firstNewlineNormals == std::string::npos) {
                    splicedSource += "\n" + normalsDefine;
                } else {
                    splicedSource.insert(firstNewlineNormals + 1, normalsDefine);
                }
                if (mainLogger && mainLogger->IsEnabled()) {
                    mainLogger->Info("[BuildRenderGraph] VIXEN_SHELL_NORMALS: compact stored-shell normal payload ENGAGED");
                }
            }

            // Capture-only isolation switch: route every SDF trilinear cell
            // through eight independently resolved voxel taps. The ordinary
            // path keeps its contiguous same-brick fast path; both paths use
            // the same brick map, distances and unallocated-brick sentinel.
            if (envFlagEnabled("VIXEN_SDF_FORCE_SLOW_TRILINEAR")) {
                const size_t firstNewlineSlowSdf = splicedSource.find('\n');
                const std::string slowSdfDefine = "#define VIXEN_SDF_FORCE_SLOW_TRILINEAR 1\n";
                if (firstNewlineSlowSdf == std::string::npos) {
                    splicedSource += "\n" + slowSdfDefine;
                } else {
                    splicedSource.insert(firstNewlineSlowSdf + 1, slowSdfDefine);
                }
                if (mainLogger && mainLogger->IsEnabled()) {
                    mainLogger->Info("[BuildRenderGraph] VIXEN_SDF_FORCE_SLOW_TRILINEAR: all taps resolve through brick lookup");
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
    if (reconstructionInputsEnabled) marchShaderFeatures.push_back(kFeatureReconstructionInputs.define);
    if (jitterEnabled) marchShaderFeatures.push_back(kFeaturePrimaryJitter.define);
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
    if (b2ProxyPrepassEnabled) {
        marchShaderFeatures.push_back(kFeatureB2ProxyPrepass.define);
    }
    if (envFlagEnabled("VIXEN_SHELL_NORMALS")) {
        marchShaderFeatures.push_back(kFeatureShellNormals.define);
    }
    if (envFlagEnabled("VIXEN_RECIPE_CHUNK_CULL")) {
        marchShaderFeatures.push_back(kFeatureRecipeChunkCull.define);
        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Info("[BuildRenderGraph] VIXEN_RECIPE_CHUNK_CULL: procedural surface-shell chunk TLAS/software culling ENGAGED");
        }
    }
    if (envFlagEnabled("VIXEN_BRICKMAP_DEBUG")) {  // W-BRICKMAP Gate-B bisection
        marchShaderFeatures.push_back(kFeatureBrickmapDebug.define);
    }
    // W-RTQUERY Slice A: VK_KHR_ray_query per-brick-AABB TLAS backend (third search
    // backend alongside ESVO/DDA -- see kFeatureRtQueryTraversal's header comment).
    // Developer A/B override for the ray-query tier. Normal composed traversal
    // selects this feature only through the CapabilityGraph below.
    if (envFlagEnabled("VIXEN_RTQUERY_TRAVERSAL")) {
        marchShaderFeatures.push_back(kFeatureRtQueryTraversal.define);
        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Info("[BuildRenderGraph] VIXEN_RTQUERY_TRAVERSAL: VK_KHR_ray_query TLAS backend ENGAGED (FORMAT_STORED_SDF instances route through traverseRayQueryWorld)");
        }
    }
    // W-COMPOSED: the role ruling made code -- RT-traversal / DDA-leaf / ESVO-data
    // are complementary tiers of ONE traversal (docs/plans/2026-08-04-wavefront-
    // recipe-shading.md, "USER RULING: composed traversal"), not rival backends.
    // R505: composed traversal is the default. The near-field search phase is
    // selected by CAPABILITY -- RT when the device has RTXCapabilities.rayQuery,
    // DDA (the software traversal substitute) otherwise -- pushing EITHER
    // kFeatureRtQueryTraversal OR kFeatureComposedDdaFallback, never both, so
    // TraceWorld.glsl's existing
    // #ifdef/#elif chain (no new GLSL branching) picks the same backend the SDI
    // wiring below wires bindings for. The capability is only known once the
    // device exists (DeviceNode::CompileImpl, which runs before this shader
    // library's own CompileImpl per graph dependency order) -- deferred into the
    // RegisterShaderBuilder lambda below instead of decided here, where the
    // VulkanDevice doesn't exist yet. VIXEN_COMPOSED_TRAVERSAL is retained as an
    // explicit A/B override; the unset default is ON.
    // Scene demand still controls whether the graph contains far-field structures;
    // it no longer gates the composed traversal backend.
    const bool sceneRequestsFarFieldTraversal =
        envFlagEnabled("VIXEN_TIER_OBSERVABLE_STRUCTURE");
    bool composedTraversalOverride = false;
    const bool hasComposedTraversalOverride =
        envFlagIsSet("VIXEN_COMPOSED_TRAVERSAL", composedTraversalOverride);
    const bool composedTraversalRequested = hasComposedTraversalOverride
        ? composedTraversalOverride : true;
    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info(std::string("[BuildRenderGraph] composed traversal request: ") +
                         (composedTraversalRequested ? "ON" : "OFF") +
                         (hasComposedTraversalOverride
                              ? (composedTraversalOverride
                                     ? "; VIXEN_COMPOSED_TRAVERSAL=1 A/B override"
                                     : "; VIXEN_COMPOSED_TRAVERSAL=0 A/B override")
                              : "; default-on") +
                         (sceneRequestsFarFieldTraversal
                              ? "; far-field/orbital scene demand"
                              : "; no far-field/orbital scene demand"));
    }
    if (hasComposedTraversalOverride && mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] VIXEN_COMPOSED_TRAVERSAL A/B override resolved; "
                         "when enabled, the near-field backend is selected by device capability");
    }

    computeShaderLibNode->RegisterShaderBuilder(
        [this, marchFamily, marchShaderFeatures, deviceNode, composedTraversalRequested, rtLightingMode]
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
            const auto useRayQuery = [&] {
                composedUsesRtQuery = true;
                if (mainLogger && mainLogger->IsEnabled()) {
                    mainLogger->Info("[BuildRenderGraph] VIXEN_COMPOSED_TRAVERSAL resolved: "
                                     "RT-traversal (CapabilityGraph: RayQueryLighting available)");
                }
                resolvedFeatures.push_back(kFeatureRtQueryTraversal.define);
            };
            const auto useGridDda = [&] {
                if (rtLightingMode == RtLightingMode::Force) {
                    throw std::runtime_error(
                        "VIXEN_RT_LIGHTING=force requested, but CapabilityGraph has no RayQueryLighting capability");
                }
                composedUsesRtQuery = false;
                if (mainLogger && mainLogger->IsEnabled()) {
                    mainLogger->Info("[BuildRenderGraph] VIXEN_COMPOSED_TRAVERSAL resolved: "
                                     "grid-DDA (software traversal substitute -- rayQuery unavailable)");
                }
                resolvedFeatures.push_back(kFeatureComposedDdaFallback.define);
            };
            if (vulkanDevice) {
                Vixen::WithConfiguredOptionalPath<Vixen::BuildCapabilityId::RayQueryLighting>(
                    vulkanDevice->GetCapabilityGraph(), rtLightingMode != RtLightingMode::Off,
                    useRayQuery, useGridDda);
            } else {
                useGridDda();
            }
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
            std::cout << "[ComposedBackend] " << (composedUsesRtQuery ? "RTQUERY" : "DDA") << std::endl;
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
               .AddIncludePath(ShaderSourceDir())
#endif
               // Inc0 M5: BodyInstanceRayMarch.comp #includes "recipe/SdfCoreKernels.g.glsl"
               // under libraries/SVO/shaders — a different tree than the paths above.
               .AddSvoShaderIncludePaths();
#ifdef VIXEN_SVO_SHADER_SOURCE_DIR
        builder.AddIncludePath(VIXEN_SVO_SHADER_SOURCE_DIR);
#endif
        return builder;
    });

    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] Registered VoxelRayMarch shader builder");
    }

    // Phase G: Compute dispatch parameters
    auto* dispatch = static_cast<ComputeDispatchNode*>(renderGraph->GetInstance(computeDispatch));
    // R493: the primary pass follows the offscreen render extent rather than the
    // presentation target. Keep the canonical scale-1 floor tiling byte-identical;
    // reduced extents use ceiling division so the last partial workgroup covers the
    // target's rounded-up edge pixels (the shader already bounds-checks imageSize).
    uint32_t dispatchX = renderScale == 1.0f
        ? width / 8
        : (renderExtent.width + 7u) / 8u;
    uint32_t dispatchY = renderScale == 1.0f
        ? height / 8
        : (renderExtent.height + 7u) / 8u;
    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] Setting primary dispatch dims: " + std::to_string(dispatchX) + "x" + std::to_string(dispatchY) + "x1 (render extent " + std::to_string(renderExtent.width) + "x" + std::to_string(renderExtent.height) + ", presentation " + std::to_string(width) + "x" + std::to_string(height) + ")");
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
    const auto makeLightingFamily = [this](const std::string& shaderName,
                                            const std::string& programName) {
        ShaderManagement::ShaderFamily::Config cfg;
        cfg.name = programName;
        cfg.stage = ShaderManagement::ShaderStage::Compute;
        return std::make_shared<ShaderManagement::ShaderFamily>(cfg,
            [this, shaderName]() {
                std::vector<std::filesystem::path> possiblePaths = {
#ifdef VIXEN_SHADER_SOURCE_DIR
                    ShaderSourceDir() + "/" + shaderName,
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
                if (source.find("// VIXEN_UBER_RECIPE_SPLICE_MARKER") != std::string::npos) {
                    // Every traversal consumer compiles the registry's same emitted
                    // fields and grid offsets. The march owns the shared grid upload.
                    source = Vixen::SVO::Recipe::SpliceProceduralRecipesIntoSource(
                        source, proceduralRecipes_, nullptr);
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
    if (envFlagEnabled("VIXEN_RECIPE_CHUNK_CULL")) {
        lightingShaderFeatures.push_back(kFeatureRecipeChunkCull.define);
    }
    if (envFlagEnabled("VIXEN_POLICY_STENCIL") && envFlagEnabled("VIXEN_POLICY_STENCIL_TILES")) {
        // E11-T1: only ShadowVisibilityWave.comp #includes PolicyStencilTileBuffer;
        // every other lighting family just gets an inert #define (same shape as
        // kFeaturePolicyStencil's own broad share above).
        lightingShaderFeatures.push_back(kFeaturePolicyStencilTiles.define);
    }
    const auto registerLightingFamilyWithFeatures = [this](
        NodeHandle libHandle,
        std::shared_ptr<ShaderManagement::ShaderFamily> family,
        std::vector<std::string> shaderFeatures) {
        proceduralShaderLibNodes_.push_back(libHandle);
        auto* libNode = static_cast<ShaderLibraryNode*>(renderGraph->GetInstance(libHandle));
        libNode->RegisterShaderBuilder([this, family, shaderFeatures](int vulkanVer, int spirvVer) {
            auto builder = family->MakeBuilder(shaderFeatures);
            builder.SetTargetVulkanVersion(vulkanVer)
                   .SetTargetSpirvVersion(spirvVer)
                   .AddIncludePath("shaders")
                   .AddIncludePath("../shaders")
#ifdef VIXEN_SHADER_SOURCE_DIR
                   .AddIncludePath(ShaderSourceDir())
#endif
                   .AddSvoShaderIncludePaths();
#ifdef VIXEN_SVO_SHADER_SOURCE_DIR
            builder.AddIncludePath(VIXEN_SVO_SHADER_SOURCE_DIR);
#endif
            return builder;
        });
    };
    const auto registerLightingFamily = [registerLightingFamilyWithFeatures, lightingShaderFeatures](
                                            NodeHandle libHandle,
                                            std::shared_ptr<ShaderManagement::ShaderFamily> family) {
        registerLightingFamilyWithFeatures(libHandle, std::move(family), lightingShaderFeatures);
    };
    // rtperf S1: the shadow wave is the first lighting consumer selected by
    // the RayQueryLighting capability. Its interface plan is kept in lock-step
    // with the feature vector used by SynthesizeComputeStage below. The graph is
    // constructed before DeviceNode compiles, so the shader builder resolves the
    // capability from the live device at the deferred shader-library compile point.
    auto waveShaderFeatures = lightingShaderFeatures;
    if (waveSlotSwizzleEnabled) {
        waveShaderFeatures.push_back(kFeatureWaveSlotSwizzle.define);
    }
    const auto resolveWaveFeatures = [this, deviceNode, rtLightingMode](
                                         std::vector<std::string> features) {
        auto* deviceNodeInst = static_cast<DeviceNode*>(renderGraph->GetInstance(deviceNode));
        auto* vulkanDevice = deviceNodeInst ? deviceNodeInst->GetVulkanDevice() : nullptr;
        bool useRayQuery = false;
        const auto useRayQueryPath = [&] {
            useRayQuery = true;
            features.push_back(kFeatureRayQueryLighting.define);
        };
        const auto useIndependentPath = [&] {
            if (rtLightingMode == RtLightingMode::Force) {
                throw std::runtime_error(
                    "VIXEN_RT_LIGHTING=force requested, but CapabilityGraph has no RayQueryLighting capability");
            }
        };
        if (vulkanDevice) {
            Vixen::WithConfiguredOptionalPath<Vixen::BuildCapabilityId::RayQueryLighting>(
                vulkanDevice->GetCapabilityGraph(), rtLightingMode != RtLightingMode::Off,
                useRayQueryPath, useIndependentPath);
        } else {
            useIndependentPath();
        }
        return std::pair<std::vector<std::string>, bool>{std::move(features), useRayQuery};
    };
    const auto registerWaveLightingFamily = [this, waveShaderFeatures, resolveWaveFeatures](
                                                NodeHandle libHandle,
                                                std::shared_ptr<ShaderManagement::ShaderFamily> family) {
        proceduralShaderLibNodes_.push_back(libHandle);
        auto* libNode = static_cast<ShaderLibraryNode*>(renderGraph->GetInstance(libHandle));
        libNode->RegisterShaderBuilder([this, family, waveShaderFeatures, resolveWaveFeatures](int vulkanVer, int spirvVer) {
            auto [resolvedFeatures, useRayQuery] = resolveWaveFeatures(waveShaderFeatures);
            auto builder = family->MakeBuilder(resolvedFeatures);
            builder.SetTargetVulkanVersion(vulkanVer)
                   .SetTargetSpirvVersion(useRayQuery ? std::max(spirvVer, 140) : spirvVer)
                   .AddIncludePath("shaders")
                   .AddIncludePath("../shaders")
#ifdef VIXEN_SHADER_SOURCE_DIR
                   .AddIncludePath(ShaderSourceDir())
#endif
                   .AddSvoShaderIncludePaths();
#ifdef VIXEN_SVO_SHADER_SOURCE_DIR
            builder.AddIncludePath(VIXEN_SVO_SHADER_SOURCE_DIR);
#endif
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
    auto srsShaderFeatures = lightingShaderFeatures;
    srsShaderFeatures.push_back(kFeatureMiningBeam.define);
    if (hitAccumResolveEnabled) srsShaderFeatures.push_back(kFeatureSrsCellResolve.define);
    registerLightingFamilyWithFeatures(
        spatialReuseShaderLib, makeLightingFamily("SpatialReuseShade.comp", "SpatialReuseShade"),
        std::move(srsShaderFeatures));

    const auto registerComputeFamily = [graph = renderGraph](
        NodeHandle libHandle, std::shared_ptr<ShaderManagement::ShaderFamily> family) {
        static_cast<ShaderLibraryNode*>(graph->GetInstance(libHandle))
            ->RegisterShaderBuilder([family](int vulkanVer, int spirvVer) {
                auto builder = family->MakeBuilder({});
                builder.SetTargetVulkanVersion(vulkanVer)
                       .SetTargetSpirvVersion(spirvVer)
                       .AddIncludePath("shaders")
                       .AddIncludePath("../shaders");
#ifdef VIXEN_SHADER_SOURCE_DIR
                builder.AddIncludePath(ShaderSourceDir());
#endif
                return builder;
            });
    };
    if (hdrExposureEnabled && exposureReference) {
        registerComputeFamily(exposureShaderLib,
            makeLightingFamily("Reference/ExposureTonemapReference.comp", "ExposureTonemapReference"));
    }
    // The runner compiles each generated system's GLSL through the same family path.
    generatedSystems_.RegisterShaders([&](NodeHandle libHandle, const std::string& glsl, const std::string& program) {
        registerComputeFamily(libHandle, makeLightingFamily("Generated/" + glsl, program));
    });
    auto exposureMeterFamily = makeLightingFamily("ExposureMeter.comp", "ExposureMeter");
    if (hdrExposureEnabled && !hdrExposureFixed) static_cast<ShaderLibraryNode*>(renderGraph->GetInstance(exposureMeterShaderLib))
        ->RegisterShaderBuilder([this, exposureMeterFamily](int vulkanVer, int spirvVer) {
            auto builder = exposureMeterFamily->MakeBuilder({});
            builder.SetTargetVulkanVersion(vulkanVer)
                   .SetTargetSpirvVersion(spirvVer)
                   .AddIncludePath("shaders")
                   .AddIncludePath("../shaders");
#ifdef VIXEN_SHADER_SOURCE_DIR
            builder.AddIncludePath(ShaderSourceDir());
#endif
            return builder;
        });
    auto exposureReduceFamily = makeLightingFamily("ExposureReduce.comp", "ExposureReduce");
    const std::vector<std::string> exposureReduceShaderFeatures = hdrExposureFixed
        ? std::vector<std::string>{kFeatureHdrExposureFixed.define}
        : std::vector<std::string>{};
    if (hdrExposureEnabled) static_cast<ShaderLibraryNode*>(renderGraph->GetInstance(exposureReduceShaderLib))
        ->RegisterShaderBuilder([this, exposureReduceFamily, exposureReduceShaderFeatures](int vulkanVer, int spirvVer) {
            auto builder = exposureReduceFamily->MakeBuilder(exposureReduceShaderFeatures);
            builder.SetTargetVulkanVersion(vulkanVer)
                   .SetTargetSpirvVersion(spirvVer)
                   .AddIncludePath("shaders")
                   .AddIncludePath("../shaders");
#ifdef VIXEN_SHADER_SOURCE_DIR
            builder.AddIncludePath(ShaderSourceDir());
#endif
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
    registerWaveLightingFamily(shadowVisibilityWaveShaderLib,
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
    if (photonCellsEnabled) {
        static_cast<PhotonCellDepositNode*>(
            renderGraph->GetInstance(photonCellDepositNode))->RegisterShader(
                *static_cast<ShaderLibraryNode*>(
                    renderGraph->GetInstance(photonCellDepositShaderLib)));
        static_cast<PhotonCellFoldNode*>(
            renderGraph->GetInstance(photonCellFoldNode))->RegisterShader(
                *static_cast<ShaderLibraryNode*>(
                    renderGraph->GetInstance(photonCellFoldShaderLib)));
        if (photonCellsClearEnabled) {
            static_cast<PhotonCellClearNode*>(
                renderGraph->GetInstance(photonCellClearNode))->RegisterShader(
                    *static_cast<ShaderLibraryNode*>(
                        renderGraph->GetInstance(photonCellClearShaderLib)));
        }
    }
    // W3c-2: the resolve pair. The cell shade is a SceneBindings consumer AND
    // an analytic-field consumer — its family source gets the SAME uber-recipe
    // splice the march's does (its marker precedes SceneBindings, the shader's
    // own ordering comment), so spliced sdfRecipe_<id> fields are tappable at
    // cell scale; its builder therefore carries the SVO include paths for
    // recipe/SdfCoreKernels.g.glsl. The resolve is a standalone-bindings shader
    // (plain lighting-family registration; the shared feature set is inert for
    // it — the ProbeApply precedent).
    if (hitAccumResolveEnabled) {
        auto cellShadeFamily = makeLightingFamily("HitAccumCellShade.comp", "HitAccumCellShade");
        proceduralShaderLibNodes_.push_back(hitAccumCellShadeShaderLib);
        auto* cellLibNode = static_cast<ShaderLibraryNode*>(renderGraph->GetInstance(hitAccumCellShadeShaderLib));
        cellLibNode->RegisterShaderBuilder([this, cellShadeFamily, lightingShaderFeatures](int vulkanVer, int spirvVer) {
            auto builder = cellShadeFamily->MakeBuilder(lightingShaderFeatures);
            builder.SetTargetVulkanVersion(vulkanVer)
                   .SetTargetSpirvVersion(spirvVer)
                   .AddIncludePath("shaders")
                   .AddIncludePath("../shaders")
#ifdef VIXEN_SHADER_SOURCE_DIR
                   .AddIncludePath(ShaderSourceDir())
#endif
                   .AddSvoShaderIncludePaths();
#ifdef VIXEN_SVO_SHADER_SOURCE_DIR
            builder.AddIncludePath(VIXEN_SVO_SHADER_SOURCE_DIR);
#endif
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
        if (waveSlotSwizzleEnabled) {
            waveReservoirFeatures.push_back(kFeatureWaveSlotSwizzle.define);
        }
        proceduralShaderLibNodes_.push_back(waveReservoirShaderLib);
        auto* waveResLibNode = static_cast<ShaderLibraryNode*>(renderGraph->GetInstance(waveReservoirShaderLib));
        waveResLibNode->RegisterShaderBuilder([this, waveReservoirFamily, waveReservoirFeatures, resolveWaveFeatures](int vulkanVer, int spirvVer) {
            auto [resolvedFeatures, useRayQuery] = resolveWaveFeatures(waveReservoirFeatures);
            auto builder = waveReservoirFamily->MakeBuilder(resolvedFeatures);
            builder.SetTargetVulkanVersion(vulkanVer)
                   .SetTargetSpirvVersion(useRayQuery ? std::max(spirvVer, 140) : spirvVer)
                   .AddIncludePath("shaders")
                   .AddIncludePath("../shaders")
#ifdef VIXEN_SHADER_SOURCE_DIR
                   .AddIncludePath(ShaderSourceDir())
#endif
                   .AddSvoShaderIncludePaths();
#ifdef VIXEN_SVO_SHADER_SOURCE_DIR
            builder.AddIncludePath(VIXEN_SVO_SHADER_SOURCE_DIR);
#endif
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
                ShaderSourceDir() + "/" + shaderName,
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
                   .AddIncludePath(ShaderSourceDir())
#endif
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
                        ShaderSourceDir() + "/" + shaderName,
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
                       .AddIncludePath("../shaders");
#ifdef VIXEN_SHADER_SOURCE_DIR
                builder.AddIncludePath(ShaderSourceDir());
#endif
                return builder;
            });
        };
        registerFamily(b1HizShaderLib,  makeB1Family("HiZDownsample.comp",         "HiZDownsample"));
        registerFamily(b1CullShaderLib, makeB1Family("InstanceOcclusionCull.comp", "InstanceOcclusionCull"));
    }

    if (b2ProxyPrepassEnabled) {
        auto* shaderLib = static_cast<ShaderLibraryNode*>(renderGraph->GetInstance(b2ShaderLib));
        shaderLib->RegisterShaderBuilder([this](int vulkanVer, int spirvVer) {
            std::filesystem::path vertexPath;
            std::filesystem::path fragmentPath;
            const std::vector<std::filesystem::path> roots = {
#ifdef VIXEN_SHADER_SOURCE_DIR
                ShaderSourceDir(),
#endif
                "shaders", "../shaders"
            };
            for (const auto& root : roots) {
                const auto candidateVertex = root / "ProxyIntervalPrepass.vert";
                const auto candidateFragment = root / "ProxyIntervalPrepass.frag";
                if (std::filesystem::exists(candidateVertex) &&
                    std::filesystem::exists(candidateFragment)) {
                    vertexPath = candidateVertex;
                    fragmentPath = candidateFragment;
                    break;
                }
            }
            if (vertexPath.empty()) {
                throw std::runtime_error("B2 proxy raster shaders not found - check shader search paths");
            }

            ShaderManagement::ShaderBundleBuilder builder;
            builder.SetProgramName("ProxyIntervalPrepass")
                   .SetTargetVulkanVersion(vulkanVer)
                   .SetTargetSpirvVersion(spirvVer)
                   .AddIncludePath("shaders")
                   .AddIncludePath("../shaders")
#ifdef VIXEN_SHADER_SOURCE_DIR
                   .AddIncludePath(ShaderSourceDir())
#endif
                   .AddStageFromFile(ShaderManagement::ShaderStage::Vertex, vertexPath, "main")
                   .AddStageFromFile(ShaderManagement::ShaderStage::Fragment, fragmentPath, "main");
            return builder;
        });

        auto* computeShaderLib = static_cast<ShaderLibraryNode*>(
            renderGraph->GetInstance(b2ComputeShaderLib));
        computeShaderLib->RegisterShaderBuilder([this](int vulkanVer, int spirvVer) {
            std::filesystem::path computePath;
            const std::vector<std::filesystem::path> roots = {
#ifdef VIXEN_SHADER_SOURCE_DIR
                ShaderSourceDir(),
#endif
                "shaders", "../shaders"
            };
            for (const auto& root : roots) {
                const auto candidate = root / "ProxyIntervalPrepass.comp";
                if (std::filesystem::exists(candidate)) {
                    computePath = candidate;
                    break;
                }
            }
            if (computePath.empty()) {
                throw std::runtime_error(
                    "B2 proxy compute shader not found - check shader search paths");
            }

            ShaderManagement::ShaderBundleBuilder builder;
            builder.SetProgramName("ProxyIntervalPrepassCompute")
                   .SetPipelineType(ShaderManagement::PipelineTypeConstraint::Compute)
                   .SetTargetVulkanVersion(vulkanVer)
                   .SetTargetSpirvVersion(spirvVer)
                   .AddIncludePath("shaders")
                   .AddIncludePath("../shaders")
#ifdef VIXEN_SHADER_SOURCE_DIR
                   .AddIncludePath(ShaderSourceDir())
#endif
                   .AddStageFromFile(ShaderManagement::ShaderStage::Compute,
                                     computePath, "main");
            return builder;
        });
    }

    // DirectLightingNode: NOT swapchain-adjacent (isConsumer=false — no WSI, no fence, no
    // PRESENT_SRC). Sampled Lighting Inc3 M5: this pass no longer owns IMAGE_WRITE (moved to
    // SpatialReuseNode below — DirectLighting.comp is now a pure buffer producer, see that
    // shader's own file header), so its dispatch dims can no longer live-derive from an
    // IMAGE_WRITE target. Set EXPLICITLY from the render-target's build-time extent instead —
    // mirrors ComputeDispatchNode's own march dispatch dims (set from `width`/`height`, static
    // since graph-build), NOT a regression: DirectLightingNode's own pre-M5 "live from
    // IMAGE_WRITE" derivation existed to track render-path scale (fixed at graph build, read
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
        (renderExtent.width + 7) / 8;
    uint32_t directLightingDispatchY =
        (renderExtent.height + 7) / 8;
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
    // pixel count. The measured swizzle variant permutes the 64 lanes within
    // each group, while waveSlot() preserves the same linear slot identity.
    // Same build-time-extent derivation as DirectLighting immediately above
    // (render-path scale fixed at graph build; dims never live-resize).
    // Dispatches unconditionally — the march always produces records, the
    // shade pass always consumes bits; there is no off-path.
    {
        const uint32_t wavePixelsX = renderExtent.width;
        const uint32_t wavePixelsY = renderExtent.height;
        const uint32_t linearWaveDispatchX =
            (wavePixelsX * wavePixelsY + 63u) / 64u;
        const uint32_t waveDispatchX = linearWaveDispatchX;
        const uint32_t waveDispatchY = 1u;
        auto* shadowVisibilityWave = static_cast<ComputeStageNode*>(renderGraph->GetInstance(shadowVisibilityWaveNode));
        shadowVisibilityWave->SetParameter(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);
        shadowVisibilityWave->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_X, waveDispatchX);
        shadowVisibilityWave->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Y, waveDispatchY);
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
            accumulateStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_X, linearWaveDispatchX);
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
            waveReservoir->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Y, waveDispatchY);
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

        // C0/C1: fixed 131072-entry photon table and 1D deposit/fold dispatches.
        // Deposit walks the same hit-record extent as the shadow wave; fold
        // walks the table.  Both are FrameCompute work, not BlockingIO or a
        // background budget, and the clear is explicitly on-demand only.
        if (photonCellsEnabled) {
            static_cast<PhotonCellDepositNode*>(
                renderGraph->GetInstance(photonCellDepositNode))->ConfigureForRecordCount(
                    wavePixelsX * wavePixelsY);
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
    // extent every Execute — the same render-path scale derivation DirectLightingNode used
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
    // SpatialReuseShade reads HitRecordBuffer plus, in the optional cell-resolve path,
    // {cellRadiance, hitAccumTable}. One gatherer feeds the stage's single BUFFER_READ_ARRAY.
    static_cast<BufferSyncGathererNode*>(renderGraph->GetInstance(spatialReuseReadGatherer))
        ->PreRegisterBufferSlots(hitAccumResolveEnabled ? 3u : 1u);
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
    if (photonCellsEnabled) {
        static_cast<BufferSyncGathererNode*>(renderGraph->GetInstance(photonCellDepositReadGatherer))->PreRegisterBufferSlots(1);
        static_cast<BufferSyncGathererNode*>(renderGraph->GetInstance(photonCellDepositWriteGatherer))->PreRegisterBufferSlots(1);
        static_cast<BufferSyncGathererNode*>(renderGraph->GetInstance(photonCellFoldReadGatherer))->PreRegisterBufferSlots(1);
        static_cast<BufferSyncGathererNode*>(renderGraph->GetInstance(photonCellFoldWriteGatherer))->PreRegisterBufferSlots(1);
        if (photonCellsClearEnabled) {
            static_cast<BufferSyncGathererNode*>(renderGraph->GetInstance(photonCellClearWriteGatherer))->PreRegisterBufferSlots(1);
        }
    }
    // W3c-2 / W-LEAN L3: cell shade reads {table}, writes {cellRadiance};
    // the shade's fold reads {cellRadiance, table} via SRS's first buffer
    // read gatherer (records were already its own).
    if (hitAccumResolveEnabled) {
        static_cast<BufferSyncGathererNode*>(renderGraph->GetInstance(hitAccumCellShadeReadGatherer))->PreRegisterBufferSlots(1);
        static_cast<BufferSyncGathererNode*>(renderGraph->GetInstance(hitAccumCellShadeWriteGatherer))->PreRegisterBufferSlots(1);
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

    // Read-side image gatherer: two DDGI atlases and the sky cache, all read by SpatialReuseShade.
    static_cast<ImageSyncGathererNode*>(renderGraph->GetInstance(spatialReuseProbeAtlasReadGatherer))->PreRegisterImageSlots(3);
    if (hdrExposureEnabled && !reconstructionInputsEnabled) {
        static_cast<ImageSyncGathererNode*>(renderGraph->GetInstance(spatialReuseSceneRadianceWriteGatherer))->PreRegisterImageSlots(1);
    }
    if (hdrExposureEnabled && exposureReference) {
        static_cast<ImageSyncGathererNode*>(renderGraph->GetInstance(exposureSceneRadianceReadGatherer))->PreRegisterImageSlots(1);
    }

    // Raster-proxy B1 M4: one tile image each side (HiZ write / cull read), one skip-mask
    // buffer entry on the cull's write side.
    if (b1OcclusionCullEnabled) {
        static_cast<ImageSyncGathererNode*>(renderGraph->GetInstance(b1HizTileWriteGatherer))->PreRegisterImageSlots(1);
        static_cast<ImageSyncGathererNode*>(renderGraph->GetInstance(b1CullTileReadGatherer))->PreRegisterImageSlots(1);
        static_cast<BufferSyncGathererNode*>(renderGraph->GetInstance(b1CullMaskWriteGatherer))->PreRegisterBufferSlots(1);
    }
    if (b2ProxyPrepassEnabled) {
        static_cast<BufferSyncGathererNode*>(renderGraph->GetInstance(b2WriteGatherer))
            ->PreRegisterBufferSlots(1);
        static_cast<BufferSyncGathererNode*>(renderGraph->GetInstance(b2ReadGatherer))
            ->PreRegisterBufferSlots(1);
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
    ConfigureLightingShadowDispatches(
        probeGatherNode, shadowRayTraceNode, probeApplyNode,
        shadowRayRequestBuffer, shadowRayResultBuffer, probeRayPayloadBuffer,
        kShadowRaySlotCount, kShadowRayRequestStrideBytes,
        kShadowRayResultStrideBytes, kProbeRayPayloadStrideBytes);
    // Baked-Perf M4 Task 4.3's PARAM_DISPATCH_ENABLED is set at the END of this function --
    // see directLighting's own identical note above for why (Cornell's force-enable of
    // VIXEN_PROBE_GRID_CONFIG_ENABLED runs later in this SAME function).

    // BlitNode: mirrors ComputeDispatchNode's own M4 PARAM_LEAVE_IMAGE_IN_GENERAL=true (set
    // below beside uiComposite's own parameters) — the UI composite still owns the final
    // GENERAL->PRESENT_SRC transition, whether the optional fixture is present or not.
    auto* blit = static_cast<BlitNode*>(renderGraph->GetInstance(blitNode));
    blit->SetParameter(BlitNodeConfig::PARAM_LEAVE_IMAGE_IN_GENERAL, true);

    // Ray marching: Camera parameters
    BuildRecipeSvoGraph(cameraNode, voxelGridNode, bodyOctreeSceneNode, lightTreeBufferNode,
                        lightingConfigNode, kRaymarchCameraFovDegrees);
    ConfigureRenderGraphDiagnostics(voxelGridNode, descriptorGatherer, computeDispatch,
                                    pushConstantGatherer, debugCaptureNode,
                                    envFlagEnabled("VIXEN_DEBUG_CAPTURE"));
    BuildPresentationGraph(computeDispatch, skyProjectionFixtureEnabled,
                           skyProjectionRenderPassNode, skyProjectionFramebufferNode,
                           uiRenderPassNode, uiFramebufferNode, uiCompositeNode, usesWsi);

    // ===================================================================
    // PHASE 3: Wire connections using TypedConnection API
    // ===================================================================

    using namespace Vixen::RenderGraph;

    mainLogger->Info("Wiring node connections using TypedConnection API");

    // Use ConnectionBatch for atomic registration
    auto connectPresentationTargetInfo = [&](NodeHandle targetNode, auto targetSlot) -> ConnectionBatch& {
        return batch.ConnectOutput(presentationTargetNode, targetContract.target, targetNode, targetSlot);
    };
    auto connectPresentationImageIndex = [&](NodeHandle targetNode, auto targetSlot) -> ConnectionBatch& {
        return batch.ConnectOutput(presentationTargetNode, targetContract.imageIndex, targetNode, targetSlot);
    };

    // --- Instance → Device connection (Phase 1.1: Dependency injection) ---
    batch.Connect(instanceNode, InstanceNodeConfig::INSTANCE,
                  deviceNode, DeviceNodeConfig::INSTANCE_IN);

    if (offscreenPresentation) {
        batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      offscreenTargetNode, RenderTargetNodeConfig::VULKAN_DEVICE_IN);
    } else {
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
    }

    // --- Device → FrameSync connection (Phase 0.2) ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  frameSyncNode, FrameSyncNodeConfig::VULKAN_DEVICE);

    if (usesWsi) {
        // --- FrameSync → SwapChain connections (Phase 0.4) ---
        batch.Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                      swapChainNode, SwapChainNodeConfig::CURRENT_FRAME_INDEX);
        // Per-image in-flight fence tracking: SwapChainNode records this per-flight fence against
        // the acquired image and waits before reusing the image's resources.
        batch.Connect(frameSyncNode, FrameSyncNodeConfig::IN_FLIGHT_FENCE,
                      swapChainNode, SwapChainNodeConfig::IN_FLIGHT_FENCE);
        batch.Connect(frameSyncNode, FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY,
                      swapChainNode, SwapChainNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY);
    }

    // --- Device → CommandPool connection ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  commandPoolNode, CommandPoolNodeConfig::VULKAN_DEVICE_IN);

    if (usesWsi) {
        // --- Device/SwapChain/UI → Present connections ---
        batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      presentNode, PresentNodeConfig::VULKAN_DEVICE_IN)
             .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_HANDLE,
                      presentNode, PresentNodeConfig::SWAPCHAIN)
             .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX,
                      presentNode, PresentNodeConfig::IMAGE_INDEX)
             .Connect(uiCompositeNode, UIRenderNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                      presentNode, PresentNodeConfig::RENDER_COMPLETE_SEMAPHORE);
    }


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

    if (usesWsi) {
        batch.Connect(swapChainNode, SwapChainNodeConfig::PRESENT_FENCES_ARRAY,
                      presentNode, PresentNodeConfig::PRESENT_FENCE_ARRAY);
    }

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
        // from the shader's own merged-SDI member table. The table bindings use
        // persistent frame rings where CPU-owned (Dependency|Execute); the instance buffer
        // reuses bodyOctreeSceneNode's own INSTANCE_BUFFER (the SAME buffer the
        // march reads), the 9-10 pair is Load-Tier Contract M2's precision tier.
        SdiProviderRegistry bucketingProviders;
        bucketingProviders.Provide("BodyInstanceBuffer", bodyOctreeSceneNode,
                                   BodyOctreeSceneNodeConfig::INSTANCE_BUFFER,
                                   SlotRole::Dependency | SlotRole::Execute);
        bucketingProviders.Provide("BodyInstanceTransformBuffer", bodyOctreeSceneNode,
                                   BodyOctreeSceneNodeConfig::INSTANCE_TRANSFORM_BUFFER,
                                   SlotRole::Dependency | SlotRole::Execute);
        bucketingProviders.Provide("RecipeBoundSphereBuffer", recipeBoundSphereBuffer,
                                   StorageBufferNodeConfig::FRAME_STORAGE_BUFFER,
                                   SlotRole::Dependency | SlotRole::Execute);
        const struct { const char* name; NodeHandle node; } bucketingSsbos[] = {
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
                                             presentationTargetNode, frameSyncNode};
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
                      recipeSpecializedDispatch, MultiDispatchNodeConfig::COMMAND_POOL);
        connectPresentationTargetInfo(recipeSpecializedDispatch, MultiDispatchNodeConfig::SWAPCHAIN_INFO);
        connectPresentationImageIndex(recipeSpecializedDispatch, MultiDispatchNodeConfig::IMAGE_INDEX);
        batch.Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                      recipeSpecializedDispatch, MultiDispatchNodeConfig::CURRENT_FRAME_INDEX)
             .Connect(frameSyncNode, FrameSyncNodeConfig::IN_FLIGHT_FENCE,
                      recipeSpecializedDispatch, MultiDispatchNodeConfig::IN_FLIGHT_FENCE)
             .Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_SEMAPHORE,
                      recipeSpecializedDispatch, MultiDispatchNodeConfig::TIMELINE_SEMAPHORE_IN)
             .Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_FRAME_BASE,
                      recipeSpecializedDispatch, MultiDispatchNodeConfig::TIMELINE_FRAME_BASE_IN);
        if (usesWsi) {
            batch.Connect(frameSyncNode, FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY,
                          recipeSpecializedDispatch, MultiDispatchNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY)
                 .Connect(swapChainNode, SwapChainNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY,
                          recipeSpecializedDispatch, MultiDispatchNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY);
        }

        // instanceSkipMaskBuffer is populated by PreTick (VulkanGraphApplication.cpp) through its
        // persistent frame-indexed mapping. Its frame-selected output is explicitly wired into
        // every consumer below; B1 retains ownership of the high mask words.
    }

    // --- Raster-proxy B1 M4: occlusion-probe chain connections (VIXEN_B1_OCCLUSION_CULL) ---
    // Frame order baked by resource hazards alone: HiZ writes the tile image the cull reads
    // (ImageSync pair) and the cull writes the HIGH camera-visibility region of the split
    // skip-mask buffer every existing reader already has in its read arrays — so HiZ → cull
    // → march/lighting without any hand-rolled edges.
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
                                      presentationTargetNode, frameSyncNode};

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
        b1Providers.Provide("BodyInstanceTransformBuffer", bodyOctreeSceneNode,
                            BodyOctreeSceneNodeConfig::INSTANCE_TRANSFORM_BUFFER,
                            SlotRole::Dependency | SlotRole::Execute);
        b1Providers.Provide("OctreeConfigsSSBO", bodyOctreeSceneNode,
                            BodyOctreeSceneNodeConfig::OCTREE_CONFIG_BUFFER,
                            SlotRole::Dependency | SlotRole::Execute);
        b1Providers.Provide("InstanceSkipMaskBuffer", instanceSkipMaskBuffer,
                            StorageBufferNodeConfig::FRAME_STORAGE_BUFFER,
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
        // the cull writes the HIGH camera-visibility region of the skip-mask buffer the
        // primary march + lighting passes already read; bucket ownership stays in LOW words.
        batch.Connect(b1HizTileImage, ProbeAtlasNodeConfig::PROBE_ATLAS,
                      b1HizTileWriteGatherer, 0, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(b1HizTileWriteGatherer, ImageSyncGathererNodeConfig::IMAGE_ARRAY,
                      b1HizStage, ComputeStageNodeConfig::IMAGE_WRITE_ARRAY, SlotRoleModifier(SlotRole::Execute));
        batch.Connect(b1HizTileImage, ProbeAtlasNodeConfig::PROBE_ATLAS,
                      b1CullTileReadGatherer, 0, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(b1CullTileReadGatherer, ImageSyncGathererNodeConfig::IMAGE_ARRAY,
                      b1CullStage, ComputeStageNodeConfig::IMAGE_READ_ARRAY, SlotRoleModifier(SlotRole::Execute));
        batch.Connect(instanceSkipMaskBuffer, StorageBufferNodeConfig::FRAME_STORAGE_BUFFER,
                      b1CullMaskWriteGatherer, 0, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(b1CullMaskWriteGatherer, BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                      b1CullStage, ComputeStageNodeConfig::BUFFER_WRITE_ARRAY, SlotRoleModifier(SlotRole::Execute));
    }

    if (b2ProxyPrepassEnabled) {
        // Extent-sized 32 B/pixel candidate buffer.
        batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      b2CandidateBuffer, StorageBufferNodeConfig::VULKAN_DEVICE_IN)
             .Connect(renderTargetNode, RenderTargetNodeConfig::RENDER_TARGET,
                      b2CandidateBuffer, StorageBufferNodeConfig::SWAPCHAIN_INFO);

        // Reflection-driven graphics descriptors: live proxy pool/count owner,
        // front-to-back instances, octree transforms, and compact output.
        batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      b2ShaderLib, ShaderLibraryNodeConfig::VULKAN_DEVICE_IN)
             .Connect(b2ShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                      b2DescGatherer, DescriptorResourceGathererNodeConfig::SHADER_DATA_BUNDLE);
        batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::PROXY_AABB_BUFFER,
                      b2DescGatherer, 0,
                      SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::INSTANCE_BUFFER,
                      b2DescGatherer, 1,
                      SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::OCTREE_CONFIG_BUFFER,
                      b2DescGatherer, 2,
                      SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::INSTANCE_TRANSFORM_BUFFER,
                      b2DescGatherer, 4,
                      SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(b2CandidateBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                      b2DescGatherer, 3,
                      SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

        batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      b2DescriptorSet, DescriptorSetNodeConfig::VULKAN_DEVICE_IN)
             .Connect(b2ShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                      b2DescriptorSet, DescriptorSetNodeConfig::SHADER_DATA_BUNDLE)
             .Connect(b2DescGatherer, DescriptorResourceGathererNodeConfig::DESCRIPTOR_RESOURCES,
                      b2DescriptorSet, DescriptorSetNodeConfig::DESCRIPTOR_RESOURCES)
             .Connect(renderTargetNode, RenderTargetNodeConfig::RENDER_TARGET,
                      b2DescriptorSet, DescriptorSetNodeConfig::SWAPCHAIN_INFO)
             .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                      b2DescriptorSet, DescriptorSetNodeConfig::CURRENT_FRAME_INDEX);
        connectPresentationImageIndex(b2DescriptorSet, DescriptorSetNodeConfig::IMAGE_INDEX);

        // Compute-writer twin: identical four logical resources, reflected for
        // compute-stage visibility in its own descriptor layout.
        batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      b2ComputeShaderLib, ShaderLibraryNodeConfig::VULKAN_DEVICE_IN)
             .Connect(b2ComputeShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                      b2ComputeDescGatherer,
                      DescriptorResourceGathererNodeConfig::SHADER_DATA_BUNDLE);
        batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::PROXY_AABB_BUFFER,
                      b2ComputeDescGatherer, 0,
                      SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::INSTANCE_BUFFER,
                      b2ComputeDescGatherer, 1,
                      SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::OCTREE_CONFIG_BUFFER,
                      b2ComputeDescGatherer, 2,
                      SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::INSTANCE_TRANSFORM_BUFFER,
                      b2ComputeDescGatherer, 4,
                      SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(b2CandidateBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                      b2ComputeDescGatherer, 3,
                      SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      b2ComputeDescriptorSet, DescriptorSetNodeConfig::VULKAN_DEVICE_IN)
             .Connect(b2ComputeShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                      b2ComputeDescriptorSet, DescriptorSetNodeConfig::SHADER_DATA_BUNDLE)
             .Connect(b2ComputeDescGatherer,
                      DescriptorResourceGathererNodeConfig::DESCRIPTOR_RESOURCES,
                      b2ComputeDescriptorSet, DescriptorSetNodeConfig::DESCRIPTOR_RESOURCES)
             .Connect(renderTargetNode, RenderTargetNodeConfig::RENDER_TARGET,
                      b2ComputeDescriptorSet, DescriptorSetNodeConfig::SWAPCHAIN_INFO)
             .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                      b2ComputeDescriptorSet, DescriptorSetNodeConfig::CURRENT_FRAME_INDEX);
        connectPresentationImageIndex(b2ComputeDescriptorSet, DescriptorSetNodeConfig::IMAGE_INDEX);

        // Offscreen render pass/framebuffer. No depth: every intersected proxy
        // contributes to the mask; proxy depth is never an occlusion source.
        batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      b2RenderPass, RenderPassNodeConfig::VULKAN_DEVICE_IN)
             .Connect(renderTargetNode, RenderTargetNodeConfig::RENDER_TARGET,
                      b2RenderPass, RenderPassNodeConfig::SWAPCHAIN_INFO);
        batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      b2Framebuffer, FramebufferNodeConfig::VULKAN_DEVICE_IN)
             .Connect(b2RenderPass, RenderPassNodeConfig::RENDER_PASS,
                      b2Framebuffer, FramebufferNodeConfig::RENDER_PASS)
             .Connect(renderTargetNode, RenderTargetNodeConfig::RENDER_TARGET,
                      b2Framebuffer, FramebufferNodeConfig::SWAPCHAIN_INFO);
        batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      b2Pipeline, GraphicsPipelineNodeConfig::VULKAN_DEVICE_IN)
             .Connect(b2ShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                      b2Pipeline, GraphicsPipelineNodeConfig::SHADER_DATA_BUNDLE)
             .Connect(b2RenderPass, RenderPassNodeConfig::RENDER_PASS,
                      b2Pipeline, GraphicsPipelineNodeConfig::RENDER_PASS)
             .Connect(b2DescriptorSet, DescriptorSetNodeConfig::DESCRIPTOR_SET_LAYOUT,
                      b2Pipeline, GraphicsPipelineNodeConfig::DESCRIPTOR_SET_LAYOUT);
        batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      b2ComputePipeline, ComputePipelineNodeConfig::VULKAN_DEVICE_IN)
             .Connect(b2ComputeShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                      b2ComputePipeline, ComputePipelineNodeConfig::SHADER_DATA_BUNDLE)
             .Connect(b2ComputeDescriptorSet, DescriptorSetNodeConfig::DESCRIPTOR_SET_LAYOUT,
                      b2ComputePipeline, ComputePipelineNodeConfig::DESCRIPTOR_SET_LAYOUT);

        batch.Connect(b2RenderPass, RenderPassNodeConfig::RENDER_PASS,
                      b2Stage, ProxyRasterStageNodeConfig::RENDER_PASS)
             .Connect(b2Framebuffer, FramebufferNodeConfig::FRAMEBUFFERS,
                      b2Stage, ProxyRasterStageNodeConfig::FRAMEBUFFERS)
             .Connect(b2Pipeline, GraphicsPipelineNodeConfig::PIPELINE,
                      b2Stage, ProxyRasterStageNodeConfig::PIPELINE)
             .Connect(b2Pipeline, GraphicsPipelineNodeConfig::PIPELINE_LAYOUT,
                      b2Stage, ProxyRasterStageNodeConfig::PIPELINE_LAYOUT)
             .Connect(b2DescriptorSet, DescriptorSetNodeConfig::DESCRIPTOR_SETS,
                      b2Stage, ProxyRasterStageNodeConfig::DESCRIPTOR_SETS)
             .Connect(renderTargetNode, RenderTargetNodeConfig::RENDER_TARGET,
                      b2Stage, ProxyRasterStageNodeConfig::COLOR_TARGET,
                      SlotRoleModifier(SlotRole::Execute))
             .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL,
                      b2Stage, ProxyRasterStageNodeConfig::COMMAND_POOL)
             .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      b2Stage, ProxyRasterStageNodeConfig::VULKAN_DEVICE)
             .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                      b2Stage, ProxyRasterStageNodeConfig::CURRENT_FRAME_INDEX)
             .Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_SEMAPHORE,
                      b2Stage, ProxyRasterStageNodeConfig::TIMELINE_SEMAPHORE_IN)
             .Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_FRAME_BASE,
                      b2Stage, ProxyRasterStageNodeConfig::TIMELINE_FRAME_BASE_IN)
             .Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                      b2Stage, ProxyRasterStageNodeConfig::CAMERA_DATA,
                      SlotRoleModifier(SlotRole::Execute))
             .Connect(cameraNode, CameraNodeConfig::CURRENT_VIEW_PROJ,
                      b2Stage, ProxyRasterStageNodeConfig::CURRENT_VIEW_PROJ,
                      SlotRoleModifier(SlotRole::Execute))
             .Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::PROXY_AABB_COUNT,
                      b2Stage, ProxyRasterStageNodeConfig::PROXY_AABB_COUNT,
                      SlotRoleModifier(SlotRole::Execute))
             .Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::INSTANCE_COUNT,
                      b2Stage, ProxyRasterStageNodeConfig::INSTANCE_COUNT,
                      SlotRoleModifier(SlotRole::Execute))
             .Connect(b2ComputePipeline, ComputePipelineNodeConfig::PIPELINE,
                      b2Stage, ProxyRasterStageNodeConfig::COMPUTE_PIPELINE)
             .Connect(b2ComputePipeline, ComputePipelineNodeConfig::PIPELINE_LAYOUT,
                      b2Stage, ProxyRasterStageNodeConfig::COMPUTE_PIPELINE_LAYOUT)
             .Connect(b2ComputeDescriptorSet, DescriptorSetNodeConfig::DESCRIPTOR_SETS,
                      b2Stage, ProxyRasterStageNodeConfig::COMPUTE_DESCRIPTOR_SETS);

        // Same buffer on both gatherers establishes the real shader-write ->
        // compute-read hazard. BUFFER_OUT adds the topological producer order.
        batch.Connect(b2CandidateBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                      b2WriteGatherer, 0,
                      SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(b2WriteGatherer, BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                      b2Stage, ProxyRasterStageNodeConfig::BUFFER_WRITE_ARRAY,
                      SlotRoleModifier(SlotRole::Execute));
        batch.Connect(b2CandidateBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                      b2ReadGatherer, 0,
                      SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(b2ReadGatherer, BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                      computeDispatch, ComputeDispatchNodeConfig::BUFFER_READ_ARRAY,
                      SlotRoleModifier(SlotRole::Execute));
        batch.Connect(b2Stage, ProxyRasterStageNodeConfig::BUFFER_OUT,
                      computeDispatch, ComputeDispatchNodeConfig::ORDERING_WAIT_BUFFER);
    }

    // --- Ray Marching Resource Connections ---
    // Camera node connections
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  cameraNode, CameraNodeConfig::VULKAN_DEVICE_IN);
    connectPresentationTargetInfo(cameraNode, CameraNodeConfig::SWAPCHAIN_PUBLIC);
    connectPresentationImageIndex(cameraNode, CameraNodeConfig::IMAGE_INDEX);
    batch.Connect(inputNode, InputNodeConfig::INPUT_STATE,
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
         .ConnectOutput(renderTargetNode,
                  SlotInfo::FromOutputSlot<RenderTargetNodeConfig::WIDTH_OUT_Slot>("WIDTH_OUT"),
                  voxelSelectionProviderNode, VoxelSelectionProviderNodeConfig::VIEWPORT_WIDTH)
         .ConnectOutput(renderTargetNode,
                  SlotInfo::FromOutputSlot<RenderTargetNodeConfig::HEIGHT_OUT_Slot>("HEIGHT_OUT"),
                  voxelSelectionProviderNode, VoxelSelectionProviderNodeConfig::VIEWPORT_HEIGHT);
    if (usesWsi) {
        // Cursor and click positions are window pixels; the pick image is render-extent.
        batch.Connect(windowNode, WindowNodeConfig::WIDTH_OUT,
                      voxelSelectionProviderNode, VoxelSelectionProviderNodeConfig::PRESENTATION_WIDTH)
             .Connect(windowNode, WindowNodeConfig::HEIGHT_OUT,
                      voxelSelectionProviderNode, VoxelSelectionProviderNodeConfig::PRESENTATION_HEIGHT);
    }

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

    // Pick ID target (AR#35 GPU picking): allocate the RG32_UINT storage-image ring sized to the
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
         .ConnectOutput(renderTargetNode,
                  SlotInfo::FromOutputSlot<RenderTargetNodeConfig::WIDTH_OUT_Slot>("WIDTH_OUT"),
                  pickIdTargetNode, PickIdTargetNodeConfig::WIDTH)
         .ConnectOutput(renderTargetNode,
                  SlotInfo::FromOutputSlot<RenderTargetNodeConfig::HEIGHT_OUT_Slot>("HEIGHT_OUT"),
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

    // C0/C1: photon table is a fixed zeroed SSBO; its params are a persistent
    // frame ring written by PreTick.  These connections do not exist when the
    // feature is off, preserving the default graph census and byte path.
    if (photonCellsEnabled) {
        batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      photonCellTableBuffer, StorageBufferNodeConfig::VULKAN_DEVICE_IN)
             .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      photonCellParamsBuffer, PhotonCellParamsConfigNodeConfig::VULKAN_DEVICE_IN)
             .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                      photonCellParamsBuffer, PhotonCellParamsConfigNodeConfig::CURRENT_FRAME_INDEX);
    }

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
    // The resolve inputs ride this per-frame record (R674.2): the extent the jitter and motion are
    // measured against, and the selected AA provider's history-weight ceiling (0: no resolve).
    {
        auto* prevCamera = static_cast<PrevCameraConfigNode*>(renderGraph->GetInstance(prevCameraConfigNode));
        prevCamera->SetParameter(PrevCameraConfigNodeConfig::PARAM_INPUT_WIDTH, renderExtent.width);
        prevCamera->SetParameter(PrevCameraConfigNodeConfig::PARAM_INPUT_HEIGHT, renderExtent.height);
        prevCamera->SetParameter(PrevCameraConfigNodeConfig::PARAM_HISTORY_WEIGHT,
                                 selectedAntiAliasingProviderId_ == "taa"
                                     ? renderPathConfig.antiAliasingHistoryWeight : 0.0f);
    }

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

    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  miningBeamBufferNode, MiningBeamBufferNodeConfig::VULKAN_DEVICE_IN)
         .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  miningBeamBufferNode, MiningBeamBufferNodeConfig::CURRENT_FRAME_INDEX);

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

    // T-1138: the cache uses a fixed world-space source-independent image. The optional
    // STAR_LIST slot is deliberately unconnected while owner question Q7 remains open.
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  skySphereNode, SkySphereNodeConfig::VULKAN_DEVICE_IN)
         .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL,
                  skySphereNode, SkySphereNodeConfig::COMMAND_POOL);

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
    batch.Connect(skySphereNode, SkySphereNodeConfig::SKY_SPHERE,
                  spatialReuseProbeAtlasReadGatherer, 2,
                  SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

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

    if (hdrExposureEnabled) {
        batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      exposureMeterBuffer, StorageBufferNodeConfig::VULKAN_DEVICE_IN);
    }

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
                  instanceSkipMaskBuffer, StorageBufferNodeConfig::VULKAN_DEVICE_IN)
         .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  instanceSkipMaskBuffer, StorageBufferNodeConfig::CURRENT_FRAME_INDEX);

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
             .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                      recipeBoundSphereBuffer, StorageBufferNodeConfig::CURRENT_FRAME_INDEX)
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
                      recipeBucketMetaBuffer, StorageBufferNodeConfig::VULKAN_DEVICE_IN)
             .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                      recipeBucketMetaBuffer, StorageBufferNodeConfig::CURRENT_FRAME_INDEX);
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
    if (b2ProxyPrepassEnabled) {
        batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::PROXY_AABB_COUNT,
                      pushConstantGatherer, MarchSdi::Push::proxyAabbCount::INDEX,
                      SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    }

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
    // imageSize() (never imageStore'd post-split — PARAM_WRITES_NO_IMAGE);
    // its layout still comes from march_image_writes below.
    // InstanceSkipMask has low bucket-ownership words [0..5] and high B1
    // camera-visibility words [6..11]. The primary march reads both regions;
    // TraceWorldShadow reads only the low region. The zeroed placeholder makes
    // both regions a true no-op until their producer fills them.
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
    sceneProviders.Provide("BodyInstanceTransformBuffer", bodyOctreeSceneNode,
                           BodyOctreeSceneNodeConfig::INSTANCE_TRANSFORM_BUFFER, kSceneRoles);
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
    // Binding 43 is an ANONYMOUS buffer block: its members live at global scope, so
    // the block name cannot equal a member name (glslang rejects it), and the SDI
    // reflector keys an anonymous block by its BLOCK name -- the only identifier it
    // has. The provider therefore carries the block name, not the member array name.
    sceneProviders.Provide("RtQueryProxyAabbBuffer", bodyOctreeSceneNode,
                           BodyOctreeSceneNodeConfig::PROXY_AABB_BUFFER, kSceneRoles);
    sceneProviders.Provide("RecipeChunkAabbBuffer", bodyOctreeSceneNode,
                           BodyOctreeSceneNodeConfig::RECIPE_CHUNK_BUFFER, kSceneRoles);
    sceneProviders.Provide("historyImage", accumulationHistoryNode,
                           AccumulationHistoryNodeConfig::HISTORY_IMAGE_VIEW,
                           SlotRole::Execute);
    sceneProviders.Provide("OccupancyGridBuffer", bodyOctreeSceneNode,
                           BodyOctreeSceneNodeConfig::OCTREE_OCCUPANCYGRID_BUFFER, kSceneRoles);
    sceneProviders.Provide("GlyphOutlineBuffer", bodyOctreeSceneNode,
                           BodyOctreeSceneNodeConfig::GLYPH_OUTLINE_BUFFER, kSceneRoles);
    sceneProviders.Provide("GlyphOutlineFlatBuffer", bodyOctreeSceneNode,
                           BodyOctreeSceneNodeConfig::GLYPH_OUTLINE_FLAT_BUFFER, kSceneRoles);
    sceneProviders.Provide("RayTraceBuffer", voxelGridNode,
                           VoxelGridNodeConfig::DEBUG_CAPTURE_BUFFER,
                           SlotRole::Dependency | SlotRole::Execute | SlotRole::Debug);
    sceneProviders.Provide("idOutputImage", pickIdTargetNode,
                           PickIdTargetNodeConfig::ID_IMAGE_VIEW, SlotRole::Execute);
    // SpatialReuseShade writes the fp16 intermediate only when the optional tonemap pass is
    // present. With HDR exposure disabled, preserve the legacy direct-to-rgba8 render target:
    // it is also the image declared by IMAGE_WRITE and consumed by BlitNode.
    if (hdrExposureEnabled) {
        sceneProviders.Provide("sceneRadianceImage", sceneRadianceNode,
                               SceneRadianceNodeConfig::CURRENT_HISTORY_IMAGE_VIEW, SlotRole::Execute);
    } else {
        sceneProviders.Provide("sceneRadianceImage", renderTargetNode,
                               RenderTargetNodeConfig::CURRENT_VIEW, SlotRole::Execute);
    }
    sceneProviders.Provide("sceneRadianceHistory", accumulationHistoryNode,
                           AccumulationHistoryNodeConfig::HISTORY_IMAGE_VIEW, SlotRole::Execute);
    // ExposureMeter consumes the complete post-accumulation scene-linear signal, including
    // beams. ExposureTonemapSystem consumes the matching current scene-radiance image so the
    // same-frame beam is displayed without feeding the beam into temporal history input.
    if (hdrExposureEnabled) {
        sceneProviders.Provide("sceneRadianceHistoryOutput", accumulationHistoryNode,
                               AccumulationHistoryNodeConfig::CURRENT_HISTORY_IMAGE_VIEW,
                               SlotRole::Execute);
        sceneProviders.Provide("sceneRadianceHistoryOutput_current", sceneRadianceNode,
                               SceneRadianceNodeConfig::CURRENT_HISTORY_IMAGE_VIEW, SlotRole::Execute);
    } else {
        sceneProviders.Provide("sceneRadianceHistoryOutput", accumulationHistoryNode,
                               AccumulationHistoryNodeConfig::CURRENT_HISTORY_IMAGE_VIEW,
                               SlotRole::Execute);
        sceneProviders.Provide("sceneRadianceHistoryOutput_current", accumulationHistoryNode,
                               AccumulationHistoryNodeConfig::CURRENT_HISTORY_IMAGE_VIEW,
                               SlotRole::Execute);
    }
    sceneProviders.Provide("_ExtentX", renderTargetNode,
                           RenderTargetNodeConfig::WIDTH_OUT, SlotRole::Execute);
    sceneProviders.Provide("_ExtentY", renderTargetNode,
                           RenderTargetNodeConfig::HEIGHT_OUT, SlotRole::Execute);
    if (hdrExposureEnabled) {
        sceneProviders.Provide("result", exposureMeterBuffer,
                               StorageBufferNodeConfig::STORAGE_BUFFER, SlotRole::Execute);
        sceneProviders.Provide("meter", exposureMeterBuffer,
                               StorageBufferNodeConfig::STORAGE_BUFFER, SlotRole::Execute);
    }
    // Legacy march interface remains registered for the flag-off/identity path;
    // the HDR seam has a distinct name in SpatialReuseShade.
    sceneProviders.Provide("outputImage", renderTargetNode,
                           RenderTargetNodeConfig::CURRENT_VIEW, SlotRole::Execute);
    sceneProviders.Provide("outputImage_current", renderTargetNode,
                           RenderTargetNodeConfig::CURRENT_VIEW, SlotRole::Execute);
    sceneProviders.Provide("InstanceSkipMaskBuffer", instanceSkipMaskBuffer,
                           StorageBufferNodeConfig::FRAME_STORAGE_BUFFER, kSceneRoles);
    if (b2ProxyPrepassEnabled) {
        sceneProviders.Provide("ProxyIntervalBuffer", b2CandidateBuffer,
                               StorageBufferNodeConfig::STORAGE_BUFFER, kSceneRoles);
    }
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
    // Every storage image the march binds writeonly is declared here, so its
    // IMAGE_WRITE_ARRAY moves each one's CURRENT ring image to GENERAL before
    // the dispatch. outputImage (slot 0) is bound only for imageSize(), but
    // SpatialReuse's own transition runs after the march: without this entry
    // each ring image's first frame binds it in UNDEFINED
    // (VUID-vkCmdDraw-None-09600 at submit).
    NodeHandle marchImageWrites = renderGraph->AddNode<ImageSyncGathererNodeType>("march_image_writes");
    static_cast<ImageSyncGathererNode*>(renderGraph->GetInstance(marchImageWrites))
        ->PreRegisterImageSlots(reconstructionInputsEnabled ? 3 : 1);
    batch.Connect(renderTargetNode, RenderTargetNodeConfig::RENDER_TARGET,
                  marchImageWrites, 0, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute))
         .Connect(marchImageWrites, ImageSyncGathererNodeConfig::IMAGE_ARRAY,
                  computeDispatch, ComputeDispatchNodeConfig::IMAGE_WRITE_ARRAY,
                  SlotRoleModifier(SlotRole::Execute));
    if (reconstructionInputsEnabled) {
        reconstructionMotionModule = renderGraph->Instantiate<ReconstructionMotionModule>(
            "render_path/reconstruction_motion", ReconstructionMotionParams{
                &batch, deviceNode, renderTargetNode, marchImageWrites, 1});
        sceneProviders.ProvideSubGraphOutput("motionVectorImage", *reconstructionMotionModule,
            ReconstructionMotionPorts::MOTION_VIEW.index, SlotRole::Execute);
        sceneProviders.ProvideSubGraphOutput("motionValidityImage", *reconstructionMotionModule,
            ReconstructionMotionPorts::VALIDITY_VIEW.index, SlotRole::Execute);
        sceneProviders.Provide("BodyInstanceMotionBuffer", bodyOctreeSceneNode,
            BodyOctreeSceneNodeConfig::INSTANCE_MOTION_BUFFER, kSceneRoles);
        marchFeatures.Enable(kFeatureReconstructionInputs);
    }

    if (b1OcclusionCullEnabled) {
        marchFeatures.Enable(kFeatureB1OcclusionCull);
        sceneProviders.Provide("depthDistanceImage", b1DepthTarget,
                               DepthTargetNodeConfig::DEPTH_WRITE_VIEW,
                               SlotRole::Execute);
    }
    if (b2ProxyPrepassEnabled) {
        marchFeatures.Enable(kFeatureB2ProxyPrepass);
    }
    if (envFlagEnabled("VIXEN_SHELL_NORMALS")) {
        marchFeatures.Enable(kFeatureShellNormals);
    }
    if (envFlagEnabled("VIXEN_RECIPE_CHUNK_CULL")) {
        marchFeatures.Enable(kFeatureRecipeChunkCull);
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

    std::optional<SubGraphHandle> samplingGroup;
    if (jitterEnabled) {
        samplingGroup = renderGraph->Instantiate<ReconstructionSamplingModule>(
            "render_path/primary_sampling", ReconstructionSamplingParams{
                hitRecordBufferNode, prevCameraConfigNode});
        sceneProviders.ProvideSubGraphOutput("HitRecordBuffer", *samplingGroup,
            ReconstructionSamplingPorts::VISUAL_HITS.index, kSceneRoles, VisualInputAcceptance::VisualOnly);
    }
    batch.Connect(renderTargetNode, RenderTargetNodeConfig::RENDER_TARGET,
                  computeDispatch, ComputeDispatchNodeConfig::DISPATCH_EXTENT_SOURCE);

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
    connectPresentationTargetInfo(computeDescriptorSet, DescriptorSetNodeConfig::SWAPCHAIN_INFO);
    connectPresentationImageIndex(computeDescriptorSet, DescriptorSetNodeConfig::IMAGE_INDEX);
    batch.Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
         // Frame-index the descriptor SET OBJECTS (sync-reuse fix): set ring == flight ring the
         // per-flight fence guards. Same source that feeds computeDispatch's CURRENT_FRAME_INDEX.
                  computeDescriptorSet, DescriptorSetNodeConfig::CURRENT_FRAME_INDEX);
    // REMOVED DUPLICATE: descriptorGatherer -> computeDescriptorSet DESCRIPTOR_RESOURCES (already connected at line 919-920)
    connectPresentationTargetInfo(computeDispatch, ComputeDispatchNodeConfig::SWAPCHAIN_INFO);
    connectPresentationImageIndex(computeDispatch, ComputeDispatchNodeConfig::IMAGE_INDEX);

    // M4: render-scale decoupling. The offscreen render target follows the swapchain's extent
    // (EXTENT_SOURCE), scaled by PARAM_SCALE (set above from RenderPathConfig); it rides the
    // standard resize->recompile cascade — no per-frame extent checks anywhere in this wiring.
    // Sampled Lighting Inc3 M1 (KI-018): computeDispatch (the march) is DELIBERATELY NOT wired to
    // RENDER_TARGET_INFO anymore — it no longer writes or blits the render target (DirectLighting
    // does, via its own IMAGE_WRITE sync slot further below); wiring RENDER_TARGET_INFO here would
    // fire ComputeDispatchNode's blit path PREMATURELY, before DirectLighting has shaded anything.
    // See PARAM_WRITES_NO_IMAGE (set above) for how the march's now-untouched SWAPCHAIN_INFO image
    // is kept safe without RENDER_TARGET_INFO.
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  renderTargetNode, RenderTargetNodeConfig::VULKAN_DEVICE_IN);
    connectPresentationTargetInfo(renderTargetNode, RenderTargetNodeConfig::EXTENT_SOURCE);
    // M4.3: raySizeCoef derives from the render target's live height (rank 6) — rides the same
    // resize->recompile cascade as the render target itself.
    batch.Connect(renderTargetNode, RenderTargetNodeConfig::HEIGHT_OUT,
                  raySizeCoefNode, RaySizeCoefNodeConfig::HEIGHT);

    // Sync connections
    batch.Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  computeDispatch, ComputeDispatchNodeConfig::CURRENT_FRAME_INDEX)
         .Connect(frameSyncNode, FrameSyncNodeConfig::IN_FLIGHT_FENCE,
                  computeDispatch, ComputeDispatchNodeConfig::IN_FLIGHT_FENCE)
         // P5b M1: wire FrameSyncNode timeline primitives into ComputeDispatchNode
         .Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_SEMAPHORE,
                  computeDispatch, ComputeDispatchNodeConfig::TIMELINE_SEMAPHORE_IN)
         .Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_FRAME_BASE,
                  computeDispatch, ComputeDispatchNodeConfig::TIMELINE_FRAME_BASE_IN);
    if (usesWsi) {
        batch.Connect(frameSyncNode, FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY,
                      computeDispatch, ComputeDispatchNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY)
             .Connect(swapChainNode, SwapChainNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY,
                      computeDispatch, ComputeDispatchNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY);
    }

    // REMOVED DUPLICATE: computeDispatch -> present RENDER_COMPLETE_SEMAPHORE (already connected at line 894-895)

    // ===================================================================
    // Optional sky-projection fixture wiring. When enabled it draws between compute and UI;
    // otherwise the UI composite follows the blit directly.
    // ===================================================================

    if (skyProjectionFixtureEnabled) {
        batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                      skyProjectionRenderPassNode, RenderPassNodeConfig::VULKAN_DEVICE_IN);
        connectPresentationTargetInfo(skyProjectionRenderPassNode, RenderPassNodeConfig::SWAPCHAIN_INFO);

        batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, skyProjectionFramebufferNode, FramebufferNodeConfig::VULKAN_DEVICE_IN)
             .Connect(skyProjectionRenderPassNode, RenderPassNodeConfig::RENDER_PASS, skyProjectionFramebufferNode, FramebufferNodeConfig::RENDER_PASS);
        connectPresentationTargetInfo(skyProjectionFramebufferNode, FramebufferNodeConfig::SWAPCHAIN_INFO);

        // DATA-role inputs (device/cmdpool) and DRAW-role inputs (swapchain/camera/render-pass/
        // framebuffer/frame-index/fence/timeline) mirror the existing composite-node wiring.
        batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, skyProjectionNode, SkyProjectionNodeConfig::VULKAN_DEVICE_IN)
             .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL, skyProjectionNode, SkyProjectionNodeConfig::COMMAND_POOL);
        connectPresentationTargetInfo(skyProjectionNode, SkyProjectionNodeConfig::SWAPCHAIN_INFO);
        batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA, skyProjectionNode, SkyProjectionNodeConfig::CAMERA_DATA)
             .Connect(skyProjectionRenderPassNode, RenderPassNodeConfig::RENDER_PASS, skyProjectionNode, SkyProjectionNodeConfig::RENDER_PASS)
             .Connect(skyProjectionFramebufferNode, FramebufferNodeConfig::FRAMEBUFFERS, skyProjectionNode, SkyProjectionNodeConfig::FRAMEBUFFERS);
        connectPresentationImageIndex(skyProjectionNode, SkyProjectionNodeConfig::IMAGE_INDEX);
        batch.Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX, skyProjectionNode, SkyProjectionNodeConfig::CURRENT_FRAME_INDEX)
             .Connect(frameSyncNode, FrameSyncNodeConfig::IN_FLIGHT_FENCE, skyProjectionNode, SkyProjectionNodeConfig::IN_FLIGHT_FENCE)
             .Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_SEMAPHORE, skyProjectionNode, SkyProjectionNodeConfig::TIMELINE_SEMAPHORE_IN)
             .Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_FRAME_BASE, skyProjectionNode, SkyProjectionNodeConfig::TIMELINE_FRAME_BASE_IN);
        // This pass is not first in the composite pipeline, so it does not wait on WSI acquire.
        // The timeline edge orders it after compute, matching UIRenderNode's composite convention.
    }

    // ===================================================================
    // UI composite pass: HUD render pass over the compute output, before present.
    // Mirrors BuildUIGraph's RenderPassNode → FramebufferNode → UIRenderNode shape, but the render pass
    // LOADs (initial=General, from the compute) instead of clearing, and the UI node runs in composite
    // mode (waits on the compute→UI handoff, signals its own present semaphore, owns the frame fence).
    // ===================================================================

    // UI render pass: device + swapchain format. (Color-only; no depth → LOAD/initial=General set above.)
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  uiRenderPassNode, RenderPassNodeConfig::VULKAN_DEVICE_IN);
    connectPresentationTargetInfo(uiRenderPassNode, RenderPassNodeConfig::SWAPCHAIN_INFO);

    // UI framebuffers: wrap each swapchain image view against the UI render pass.
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, uiFramebufferNode, FramebufferNodeConfig::VULKAN_DEVICE_IN)
         .Connect(uiRenderPassNode, RenderPassNodeConfig::RENDER_PASS, uiFramebufferNode, FramebufferNodeConfig::RENDER_PASS);
    connectPresentationTargetInfo(uiFramebufferNode, FramebufferNodeConfig::SWAPCHAIN_INFO);

    // UIRenderNode (composite) inputs — mirrors BuildUIGraph's UIRenderNode wiring.
    connectPresentationTargetInfo(uiCompositeNode, UIRenderNodeConfig::SWAPCHAIN_INFO);
    batch.Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL, uiCompositeNode, UIRenderNodeConfig::COMMAND_POOL)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, uiCompositeNode, UIRenderNodeConfig::VULKAN_DEVICE)
         .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX, uiCompositeNode, UIRenderNodeConfig::CURRENT_FRAME_INDEX)
         .Connect(frameSyncNode, FrameSyncNodeConfig::IN_FLIGHT_FENCE, uiCompositeNode, UIRenderNodeConfig::IN_FLIGHT_FENCE)
         .Connect(uiRenderPassNode, RenderPassNodeConfig::RENDER_PASS, uiCompositeNode, UIRenderNodeConfig::RENDER_PASS)
         .Connect(uiFramebufferNode, FramebufferNodeConfig::FRAMEBUFFERS, uiCompositeNode, UIRenderNodeConfig::FRAMEBUFFERS)
         // P5b M1: wire FrameSyncNode timeline primitives into UIRenderNode (consumer waits on edges)
         .Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_SEMAPHORE, uiCompositeNode, UIRenderNodeConfig::TIMELINE_SEMAPHORE_IN)
         .Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_FRAME_BASE, uiCompositeNode, UIRenderNodeConfig::TIMELINE_FRAME_BASE_IN);
    connectPresentationImageIndex(uiCompositeNode, UIRenderNodeConfig::IMAGE_INDEX);
    if (usesWsi) {
        batch.Connect(frameSyncNode, FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY,
                      uiCompositeNode, UIRenderNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY)
             .Connect(swapChainNode, SwapChainNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY,
                      uiCompositeNode, UIRenderNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY);
    }

    // ===================================================================
    // Sampled Lighting Inc3 M1 (KI-018): DirectLightingNode + BlitNode wiring. The chain is now
    // march (HitRecord+pick-ID only) -> DirectLighting (shades, writes the render target) ->
    // BlitNode (blits render target -> swapchain) -> [optional sky fixture] -> UI -> present.
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
    sceneProviders.Provide("worldPosHistoryOutput", worldPosHistoryNode,
                           WorldPosHistoryNodeConfig::CURRENT_WORLDPOS_IMAGE_VIEW,
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
                                        presentationTargetNode, frameSyncNode};
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
    // Provenance preserved: previous-frame history images are read by this pass, while the paired
    // current-frame images are its write-only outputs — Execute-only, with no in-dispatch image
    // aliasing. Reservoir A/B are READ here
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
    sceneProviders.Provide("skySphereImage", skySphereNode,
                           SkySphereNodeConfig::CURRENT_VIEW, SlotRole::Execute);
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
    srsSdiFeatures.Enable(kFeatureMiningBeam);
    if (testIndirectLightingProvider.group.IsValid()) {
        batch.Connect(testIndirectLightingProvider.group,
                      testIndirectLightingProvider.outputPort,
                      spatialReuseNode, ComputeStageNodeConfig::INDIRECT_LIGHTING.index);
        sceneProviders.ProvideSubGraphOutput(
            "moduleIndirectIrradiance", testIndirectLightingProvider.group,
            testIndirectLightingProvider.outputPort,
            SlotRole::Dependency | SlotRole::Execute);
    }
    if (hitAccumEnabled) {
        sceneProviders.Provide("HitAccumTable", hitAccumTableBuffer,
                               StorageBufferNodeConfig::STORAGE_BUFFER, SlotRole::Execute);
        sceneProviders.Provide("HitAccumParamsSSBO", hitAccumParamsBuffer,
                               HitAccumParamsConfigNodeConfig::HIT_ACCUM_PARAMS_BUFFER, SlotRole::Execute);
    }
    if (photonCellsEnabled) {
        sceneProviders.Provide("PhotonCellTable", photonCellTableBuffer,
                               StorageBufferNodeConfig::STORAGE_BUFFER,
                               SlotRole::Dependency | SlotRole::Execute);
        sceneProviders.Provide("PhotonCellParamsSSBO", photonCellParamsBuffer,
                               PhotonCellParamsConfigNodeConfig::PHOTON_CELL_PARAMS_BUFFER,
                               SlotRole::Dependency | SlotRole::Execute);
    }
    if (hitAccumResolveEnabled) {
        srsSdiFeatures.Enable(kFeatureSrsCellResolve);
        sceneProviders.Provide("HitAccumCellRadiance", hitAccumCellRadianceBuffer,
                               StorageBufferNodeConfig::STORAGE_BUFFER, SlotRole::Execute);
    }
    sceneProviders.Provide("MiningBeamBodyInstanceBuffer", bodyOctreeSceneNode,
                           BodyOctreeSceneNodeConfig::INSTANCE_BUFFER,
                           SlotRole::Dependency | SlotRole::Execute);
    sceneProviders.Provide("MiningBeamInstanceTransformBuffer", bodyOctreeSceneNode,
                           BodyOctreeSceneNodeConfig::INSTANCE_TRANSFORM_BUFFER,
                           SlotRole::Dependency | SlotRole::Execute);
    sceneProviders.Provide("MiningBeamBufferSSBO", miningBeamBufferNode,
                           MiningBeamBufferNodeConfig::MINING_BEAM_BUFFER,
                           SlotRole::Dependency | SlotRole::Execute);
    const auto reuseSynth = SynthesizeComputeStage<ReuseSdi::Metadata, ReuseSdi::MEMBERS>(
        renderGraph, batch, "spatial_reuse", spatialReuseShaderLib,
        spatialReuseNode, lightingCommon, sceneProviders, srsSdiFeatures,
        SdiWireSet::DescriptorsOnly);
    spatialReusePushConstantGatherer = reuseSynth.pushGatherer;
    if (testIndirectLightingProvider.group.IsValid()) {
        sceneProviders.Apply("moduleIndirectIrradiance", batch,
                             spatialReusePushConstantGatherer,
                             ReuseSdi::Push::moduleIndirectIrradiance::INDEX);
    }
    CensusStageFromSdi<ReuseSdi::Metadata, ReuseSdi::MEMBERS>(
        sdiHazardCensus_, spatialReuseNode, sceneProviders, srsSdiFeatures,
        &sdiCensusExclusions);

    const auto registerModuleShaderFamily = [this, graph = renderGraph](
        NodeHandle libHandle, std::shared_ptr<ShaderManagement::ShaderFamily> family) {
        auto* libNode = static_cast<ShaderLibraryNode*>(graph->GetInstance(libHandle));
        libNode->RegisterShaderBuilder([this, family](int vulkanVer, int spirvVer) {
            auto builder = family->MakeBuilder({});
            builder.SetTargetVulkanVersion(vulkanVer)
                   .SetTargetSpirvVersion(spirvVer)
                   .AddIncludePath("shaders")
                   .AddIncludePath("../shaders");
#ifdef VIXEN_SHADER_SOURCE_DIR
            builder.AddIncludePath(ShaderSourceDir());
#endif
            return builder;
        });
    };

    const auto registerPassMetric = [this](std::string name, NodeHandle node) {
        upscalerPerfPasses_.push_back({std::move(name), node});
    };
    // The reconstruction depth guide is created ahead of the frame registry: the TAA reads it
    // as FrameTargets.ViewDepth (nearest-surface motion at silhouettes).
    if (reconstructionInputsEnabled) {
        ReconstructionDepthParams params;
        params.batch = &batch;
        params.common = lightingCommon;
        params.sourceTarget = renderTargetNode;
        params.camera = cameraNode;
        params.hitRecords = hitRecordBufferNode;
        params.hitProducer = computeDispatch;
        params.makeShaderFamily = makeLightingFamily;
        params.registerShaderFamily = registerModuleShaderFamily;
        params.registerPassMetric = registerPassMetric;
        reconstructionDepthModule = renderGraph->Instantiate<ReconstructionDepthModule>(
            "render_path/reconstruction_inputs", params);
        sceneProviders.ProvideSubGraphOutput("viewDepthImage", *reconstructionDepthModule,
            ReconstructionDepthPorts::VIEW_DEPTH_VIEW.index, SlotRole::Execute);
        sceneProviders.ProvideSubGraphOutput("normalizedDepthImage", *reconstructionDepthModule,
            ReconstructionDepthPorts::NORMALIZED_DEPTH_VIEW.index, SlotRole::Execute);
    }

    if (hdrExposureEnabled) {
    // Hand passes register the resources the generated systems read under their schema
    // identity (R680.4); the runner allocates the rest from the manifest's allocations, then
    // binds, orders and dispatches every manifest system.
    const SchemaIdentity sceneRadiance{"frame_targets", "SceneRadianceHistoryOutput"};
    const SchemaIdentity resolvedRadiance{"frame_targets", "ResolvedRadiance"};
    const SchemaIdentity outputImage{"frame_targets", "OutputImage"};
    const SchemaIdentity exposureMeter{"Vixen.Codegen.Gpu.ExposureMeterState", "*"};
    SchemaResourceRegistry frameResources;
    // The exposure meter reads the resolved radiance: the scene radiance itself with `none`, the
    // TAA's current resolve with `taa` (R676.1: the TAA resolves before the exposure meter).
    SdiProviderRegistry meterProviders = sceneProviders;
    if (!exposureReference) {
        frameResources.Provide(sceneRadiance, sceneRadianceNode,
                               SceneRadianceNodeConfig::CURRENT_HISTORY_IMAGE_VIEW, SlotRole::Execute);
        frameResources.ProvideImage(sceneRadiance, sceneRadianceNode, SceneRadianceNodeConfig::CURRENT_HISTORY_TARGET);
        frameResources.ProvideProducer(sceneRadiance, spatialReuseNode);
        if (!taaSelected) {
            // With the `none` AA provider the resolve the meter and tone map read is the scene radiance.
            frameResources.Provide(resolvedRadiance, sceneRadianceNode,
                                   SceneRadianceNodeConfig::CURRENT_HISTORY_IMAGE_VIEW, SlotRole::Execute);
            frameResources.ProvideImage(resolvedRadiance, sceneRadianceNode, SceneRadianceNodeConfig::CURRENT_HISTORY_TARGET);
        }
        if (reconstructionMotionModule) {
            frameResources.ProvideSubGraph({"frame_targets", "MotionVectors"}, *reconstructionMotionModule,
                                           ReconstructionMotionPorts::MOTION_VIEW.index,
                                           ReconstructionMotionPorts::MOTION.index, SlotRole::Execute);
        }
        if (reconstructionDepthModule) {
            frameResources.ProvideSubGraph({"frame_targets", "ViewDepth"}, *reconstructionDepthModule,
                                           ReconstructionDepthPorts::VIEW_DEPTH_VIEW.index,
                                           ReconstructionDepthPorts::VIEW_DEPTH.index, SlotRole::Execute);
        }
        frameResources.Provide({"Vixen.Gpu.PrevCameraConfig", "*"}, prevCameraConfigNode,
                               PrevCameraConfigNodeConfig::PREV_CAMERA_CONFIG_BUFFER, kSceneRoles);
        frameResources.Provide(outputImage, renderTargetNode, RenderTargetNodeConfig::CURRENT_VIEW, SlotRole::Execute);
        frameResources.ProvideImage(outputImage, renderTargetNode, RenderTargetNodeConfig::RENDER_TARGET);
        frameResources.Provide(exposureMeter, exposureMeterBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                               SlotRole::Execute);
        // With `taa` the exposure chain itself waits on the resolve, so the tone map waits on the chain.
        frameResources.ProvideProducer(exposureMeter, exposureReduceNode, taaSelected);
        frameResources.Provide({"Vixen.Gpu.LightingConfig", "*"}, lightingConfigNode,
                               LightingConfigNodeConfig::LIGHTING_CONFIG_BUFFER, kSceneRoles);
        frameResources.ProvideExtent("presentation", 0, renderTargetNode, RenderTargetNodeConfig::WIDTH_OUT,
                                     renderExtent.width);
        frameResources.ProvideExtent("presentation", 1, renderTargetNode, RenderTargetNodeConfig::HEIGHT_OUT,
                                     renderExtent.height);
        frameResources.ProvideExtentTarget("presentation", renderTargetNode, RenderTargetNodeConfig::RENDER_TARGET);
        generatedSystems_.Allocate(renderGraph, batch, lightingCommon, frameResources);
        if (taaSelected) {
            meterProviders.Provide("sceneRadianceHistoryOutput", *generatedSystems_.Allocation(resolvedRadiance),
                                   AccumulationHistoryNodeConfig::CURRENT_HISTORY_IMAGE_VIEW, SlotRole::Execute);
        }
    }
    if (!hdrExposureFixed) {
        const auto meterSynth = SynthesizeComputeStage<ShaderInterface::ExposureMeter::Metadata,
                                                         ShaderInterface::ExposureMeter::MEMBERS>(
            renderGraph, batch, "exposure_meter", exposureMeterShaderLib, exposureMeterNode,
            lightingCommon, meterProviders, {});
        (void)meterSynth;
        auto* meterStage = static_cast<ComputeStageNode*>(renderGraph->GetInstance(exposureMeterNode));
        meterStage->SetParameter(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);
        meterStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_X, (renderExtent.width + 15u) / 16u);
        meterStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Y, (renderExtent.height + 15u) / 16u);
        meterStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Z, 1u);
    }
    SdiFeatureSet exposureReduceFeatures;
    if (hdrExposureFixed) exposureReduceFeatures.Enable(kFeatureHdrExposureFixed);
    const auto reduceSynth = SynthesizeComputeStage<ShaderInterface::ExposureReduce::Metadata,
                                                     ShaderInterface::ExposureReduce::MEMBERS>(
        renderGraph, batch, "exposure_reduce", exposureReduceShaderLib, exposureReduceNode,
        lightingCommon, sceneProviders, exposureReduceFeatures);
    (void)reduceSynth;
    auto* reduceStage = static_cast<ComputeStageNode*>(renderGraph->GetInstance(exposureReduceNode));
    reduceStage->SetParameter(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);
    reduceStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_X, 1u);
    reduceStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Y, 1u);
    reduceStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Z, 1u);
    if (exposureReference) {
        const auto exposureSynth = SynthesizeComputeStage<ExposureReferenceSdi::Metadata,
                                                           ExposureReferenceSdi::MEMBERS>(
            renderGraph, batch, "exposure_tonemap", exposureShaderLib, exposureNode,
            lightingCommon, sceneProviders, {});
        (void)exposureSynth;
        auto* exposureStage = static_cast<ComputeStageNode*>(renderGraph->GetInstance(exposureNode));
        exposureStage->SetParameter(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);
        exposureStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_X, (renderExtent.width + 7u) / 8u);
        exposureStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Y, (renderExtent.height + 7u) / 8u);
        exposureStage->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Z, 1u);
    } else {
        generatedSystems_.Wire(renderGraph, batch, lightingCommon, frameResources);
        // Hand consumers of the display image order after its last generated writer.
        exposureNode = *generatedSystems_.LastWriter(outputImage);
        // The exposure chain reads the resolve: with `taa` it starts after the TAA stage.
        if (taaSelected) exposureInputProducer = *generatedSystems_.LastWriter(resolvedRadiance);
    }
    }


    if (reconstructionInputsEnabled) {
        ReconstructionHistoryParams historyParams;
        historyParams.batch = &batch;
        historyParams.common = lightingCommon;
        historyParams.sourceTarget = renderTargetNode;
        historyParams.camera = cameraNode;
        historyParams.samplingPath = jitterEnabled ? ReconstructionSamplingPath::InteractiveVisual : ReconstructionSamplingPath::CanonicalCapture;
        if (hdrExposureEnabled) {
            historyParams.sceneRadiance = sceneRadianceNode;
            historyParams.sceneProducer = spatialReuseNode;
            historyParams.makeShaderFamily = makeLightingFamily;
            historyParams.registerShaderFamily = registerModuleShaderFamily;
            historyParams.registerPassMetric = registerPassMetric;
        }
        reconstructionHistoryModule = renderGraph->Instantiate<ReconstructionHistoryModule>(
            "render_path/reconstruction_history", historyParams);
        if (samplingGroup) {
            renderGraph->Connect(*reconstructionHistoryModule, ReconstructionHistoryPorts::JITTER,
                                 *samplingGroup, ReconstructionSamplingPorts::JITTER);
            // Jittered picks vote over the last jitter cycle; the motion targets reset that history.
            renderGraph->Connect(*reconstructionHistoryModule, ReconstructionHistoryPorts::JITTER,
                                 voxelSelectionProviderNode, VoxelSelectionProviderNodeConfig::JITTER);
            if (reconstructionMotionModule) {
                renderGraph->Connect(*reconstructionMotionModule, ReconstructionMotionPorts::MOTION,
                                     voxelSelectionProviderNode, VoxelSelectionProviderNodeConfig::MOTION_TARGET);
                renderGraph->Connect(*reconstructionMotionModule, ReconstructionMotionPorts::VALIDITY,
                                     voxelSelectionProviderNode, VoxelSelectionProviderNodeConfig::VALIDITY_TARGET);
            }
        }
        sceneProviders.ProvideSubGraphOutput("reconstructionJitter", *reconstructionHistoryModule,
            ReconstructionHistoryPorts::JITTER.index, SlotRole::Execute, VisualInputAcceptance::VisualOnly);
        sceneProviders.ProvideSubGraphOutput("reconstructionHistoryA", *reconstructionHistoryModule,
            ReconstructionHistoryPorts::HISTORY_A_VIEW.index, SlotRole::Execute);
        sceneProviders.ProvideSubGraphOutput("reconstructionHistoryB", *reconstructionHistoryModule,
            ReconstructionHistoryPorts::HISTORY_B_VIEW.index, SlotRole::Execute);
        sceneProviders.ProvideSubGraphOutput("reconstructionHistoryReset", *reconstructionHistoryModule,
            ReconstructionHistoryPorts::RESET.index, SlotRole::Execute);
        sceneProviders.ProvideSubGraphOutput("reconstructionHistoryValid", *reconstructionHistoryModule,
            ReconstructionHistoryPorts::VALID.index, SlotRole::Execute);
        sceneProviders.ProvideSubGraphOutput("reconstructionHistoryWriteIndex", *reconstructionHistoryModule,
            ReconstructionHistoryPorts::WRITE_INDEX.index, SlotRole::Execute);
        if (hdrExposureEnabled) {
            ReconstructionExposureParams exposure;
            exposure.batch = &batch;
            exposure.common = lightingCommon;
            exposure.providers = sceneProviders;
            exposure.meterBuffer = exposureMeterBuffer;
            exposure.meterProducer = exposureReduceNode;
            exposure.makeShaderFamily = makeLightingFamily;
            exposure.registerShaderFamily = registerModuleShaderFamily;
            exposure.registerPassMetric = registerPassMetric;
            reconstructionExposureModule = renderGraph->Instantiate<ReconstructionExposureModule>(
                "render_path/reconstruction_exposure", exposure);
            sceneProviders.ProvideSubGraphOutput("exposureImage", *reconstructionExposureModule,
                ReconstructionExposurePorts::EXPOSURE_VIEW.index, SlotRole::Execute);
        }
    }

    std::array<std::string_view, 3> requestedUpscalerOrder{};
    size_t requestedUpscalerCount = 0;
    if (renderPathConfig.upscaler == UpscalerKind::Fsr3) {
        requestedUpscalerOrder = {"fsr3", "fsr1", "native"};
        requestedUpscalerCount = 3;
    } else if (renderPathConfig.upscaler == UpscalerKind::Fsr1) {
        requestedUpscalerOrder = {"fsr1", "linear", "native"};
        requestedUpscalerCount = 3;
    } else if (renderPathConfig.upscaler == UpscalerKind::Linear) {
        requestedUpscalerOrder = {"linear", "native", ""};
        requestedUpscalerCount = 2;
    } else {
        requestedUpscalerOrder = {"native", "", ""};
        requestedUpscalerCount = 1;
    }
    if (renderPathConfig.upscaler == UpscalerKind::Fsr3 &&
        FindUpscalerBackend("fsr3") == nullptr) {
        if (mainLogger) {
            mainLogger->Warning(
                "[BuildRenderGraph] Requested upscaler 'fsr3' is unavailable in this build; "
                "falling back to available providers");
        }
        requestedUpscalerOrder = {"fsr1", "linear", "native"};
        requestedUpscalerCount = 3;
    }

    std::vector<RenderModuleFrameInput> availableUpscalerInputs{RenderModuleFrameInput::Color};
    if (reconstructionInputsEnabled) {
        availableUpscalerInputs.push_back(RenderModuleFrameInput::Depth);
        availableUpscalerInputs.push_back(RenderModuleFrameInput::MotionVectors);
        availableUpscalerInputs.push_back(RenderModuleFrameInput::Jitter);
        if (hdrExposureEnabled) availableUpscalerInputs.push_back(RenderModuleFrameInput::Exposure);
    }

    // Provider choice happens while assembling the graph, before DeviceNode creates a live
    // VulkanDevice. The profile facts were resolved at function entry, before render extents,
    // so the same record query now also owns the default render-path selection.
    auto upscalerPolicy = upscalerCapabilities.GetUpscalerCapabilityRecords().policy;
#if defined(VIXEN_CAPTURE_TEST_HOOKS)
    const bool fsr3QualificationCapture = offscreenPresentation &&
        renderPathConfig.upscaler == UpscalerKind::Fsr3 &&
        std::getenv("VIXEN_FSR3_QUALIFICATION_CAPTURE") != nullptr;
#else
    constexpr bool fsr3QualificationCapture = false;
#endif
    upscalerPolicy.viewMode = !jitterEnabled && renderPathConfig.renderScale == 1.0f && renderPathConfig.upscaler == UpscalerKind::None
        ? Vixen::UpscalerViewMode::Canonical
        : Vixen::UpscalerViewMode::Relaxed;
    upscalerPolicy.outputExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    upscalerPolicy.role = fsr3QualificationCapture
        ? Vixen::UpscalerWorkRole::Qualification
        : offscreenPresentation ? Vixen::UpscalerWorkRole::HeadlessCapture
                                : Vixen::UpscalerWorkRole::Interactive;
    upscalerCapabilities.SetUpscalerPolicyRecord(upscalerPolicy);
    const auto& upscalerRecords = upscalerCapabilities.GetUpscalerCapabilityRecords();
    if (mainLogger && runtimeProfile) {
        mainLogger->Info("[BuildRenderGraph] Resolved upscaler machine profile '" +
                         runtimeProfile->profileId + "' for '" +
                         upscalerRecords.runtime.physicalDevice + "'");
    } else if (mainLogger) {
        mainLogger->Warning("[BuildRenderGraph] No upscaler machine profile matched the selected "
                            "adapter; optional providers are held back");
    }

    const UpscalerFrameDescription upscalerFrame{
        {renderExtent.width, renderExtent.height},
        {static_cast<uint32_t>(width), static_cast<uint32_t>(height)},
        RenderModuleImageFormat::Rgba8Unorm,
        RenderModuleImageFormat::Rgba8Unorm,
        availableUpscalerInputs
    };
    std::vector<RenderModuleProviderCandidate> upscalerCandidates;
    std::vector<const UpscalerBackendDescriptor*> orderedUpscalerBackends;
    upscalerCandidates.reserve(requestedUpscalerCount);
    orderedUpscalerBackends.reserve(requestedUpscalerCount);
    for (size_t candidateIndex = 0; candidateIndex < requestedUpscalerCount; ++candidateIndex) {
        const UpscalerBackendDescriptor* backend =
            FindUpscalerBackend(requestedUpscalerOrder[candidateIndex]);
        if (!backend || !backend->contract) {
            continue;
        }

        RenderModuleEligibility eligibility;
        const std::string_view compiledAdapter = backend->nativeFallback
            ? std::string_view{"Upscaler.Native"}
            : backend->moduleTypeName;
        eligibility.compiled = backend->compiled && !compiledAdapter.empty() &&
            std::find(upscalerRecords.build.compiledAdapters.begin(),
                      upscalerRecords.build.compiledAdapters.end(), compiledAdapter) !=
                upscalerRecords.build.compiledAdapters.end();
        eligibility.permittedByRuntimeProfile = backend->nativeFallback ||
            (runtimeProfile &&
             std::find(runtimeProfile->providerCompileSet.begin(),
                       runtimeProfile->providerCompileSet.end(), compiledAdapter) !=
                 runtimeProfile->providerCompileSet.end());
        // Entries in this ordered list are the providers the render pass permits, followed by
        // the required native fallback.
        eligibility.permittedByPass = backend->nativeFallback ||
            upscalerRecords.policy.viewMode == Vixen::UpscalerViewMode::Relaxed;
        eligibility.runtimeSupported = backend->runtimeCapability.empty() ||
            upscalerCapabilities.ResolveOptionalPath(
                std::string(backend->runtimeCapability), true) ==
                Vixen::CapabilityPath::CapabilityEnabled;
        const bool policyExtentMatches =
            (upscalerRecords.policy.outputExtent.width == 0 ||
             upscalerRecords.policy.outputExtent.width == upscalerFrame.outputExtent.width) &&
            (upscalerRecords.policy.outputExtent.height == 0 ||
             upscalerRecords.policy.outputExtent.height == upscalerFrame.outputExtent.height);
        const bool historyPolicySupported = upscalerRecords.policy.historyCount == 0 ||
            backend->contract->retainsHistory;
        eligibility.inputCompatible = policyExtentMatches && historyPolicySupported &&
            SupportsUpscalerFrame(*backend->contract, upscalerFrame);
        // Optional modules are provisional until their graph compile has created their
        // provider context. The native path has no external context to initialize.
        eligibility.initialized = backend->nativeFallback;
        const bool passedQualification =
            upscalerRecords.qualification.status == Vixen::UpscalerQualificationStatus::Qualified &&
            upscalerRecords.qualification.parity == Vixen::UpscalerParityResult::Passed;
        const bool unqualifiedRelaxedPath =
            upscalerRecords.policy.viewMode == Vixen::UpscalerViewMode::Relaxed &&
            (upscalerRecords.qualification.status ==
                 Vixen::UpscalerQualificationStatus::UnqualifiedNoRunner ||
             upscalerRecords.qualification.status == Vixen::UpscalerQualificationStatus::Unqualified);
        const bool allowRelaxedUnqualified = backend->id != "fsr3" && unqualifiedRelaxedPath;
        eligibility.qualified = backend->nativeFallback || passedQualification ||
            allowRelaxedUnqualified;
        if (!backend->nativeFallback && allowRelaxedUnqualified && mainLogger) {
            mainLogger->Warning("[BuildRenderGraph] Upscaler provider '" +
                                std::string(backend->id) +
                                "' is unqualified for native parity; using it only for the "
                                "requested relaxed view.");
        }
        // Zero means no declared memory ceiling. A finite allowance cannot admit an optional
        // provider until its allocation requirements are available to this predicate.
        eligibility.admittedByBudget = backend->nativeFallback ||
            upscalerRecords.policy.memoryAllowanceBytes == 0;

        const bool initializationFailed = std::find(
            upscalerInitializationFailures_.begin(), upscalerInitializationFailures_.end(),
            backend->id) != upscalerInitializationFailures_.end();
        upscalerCandidates.push_back(
            {backend->id, eligibility, backend->nativeFallback, initializationFailed});
        orderedUpscalerBackends.push_back(backend);
    }

    const RenderModuleSelectionResult upscalerSelection =
        SelectRenderModuleProviderForInitialization(upscalerCandidates);
    for (const auto& rejected : upscalerSelection.rejected) {
        const UpscalerBackendDescriptor* backend = FindUpscalerBackend(rejected.id);
        const std::string variantSuggestion =
            backend && !backend->runtimeCapability.empty()
                ? "a device variant that includes " + std::string(backend->runtimeCapability)
                : "a device variant that permits this backend";
        if (mainLogger) {
            mainLogger->Warning("[BuildRenderGraph] Upscaler provider '" +
                                std::string(rejected.id) + "' rejected: " +
                                std::string(RenderModuleSelectionFailureText(rejected.reason)) +
                                "; trying the next provider; suggest " + variantSuggestion);
        }
    }

    const UpscalerBackendDescriptor* selectedUpscalerBackend = nullptr;
    if (upscalerSelection.selected) {
        const auto selectedIndex = static_cast<size_t>(
            upscalerSelection.selected - upscalerCandidates.data());
        if (selectedIndex < orderedUpscalerBackends.size()) {
            selectedUpscalerBackend = orderedUpscalerBackends[selectedIndex];
        }
    }
    if (!selectedUpscalerBackend) {
        selectedUpscalerBackend = FindUpscalerBackend("native");
        if (mainLogger) {
            mainLogger->Warning("[BuildRenderGraph] No upscaler provider passed selection; "
                                "using the native render path");
        }
    }
#if defined(VIXEN_CAPTURE_TEST_HOOKS)
    if (fsr3QualificationCapture) {
        const auto candidate = std::find_if(upscalerCandidates.begin(), upscalerCandidates.end(),
            [](const auto& value) { return value.id == "fsr3"; });
        if (candidate != upscalerCandidates.end() && candidate->eligibility.compiled &&
            candidate->eligibility.permittedByPass &&
            candidate->eligibility.runtimeSupported &&
            candidate->eligibility.inputCompatible &&
            !candidate->initializationFailed &&
            candidate->eligibility.admittedByBudget && !candidate->eligibility.qualified) {
            selectedUpscalerBackend = FindUpscalerBackend("fsr3");
            candidate->eligibility.qualified = true; // explicit qualification-capture gate bypass
            if (mainLogger) {
                mainLogger->Warning(
                    "[BuildRenderGraph] Qualification capture is exercising unqualified FSR 3.1.4; "
                    "ordinary upscaler selection remains fail-closed");
            }
        } else if (mainLogger) {
            mainLogger->Warning(
                "[BuildRenderGraph] FSR 3.1.4 qualification capture could not bypass any gate "
                "other than qualification");
        }
    }
#endif

    upscalerCandidatesForCompile_ = upscalerCandidates;
    selectedUpscalerProviderId_ = selectedUpscalerBackend
        ? std::string(selectedUpscalerBackend->id) : std::string("native");
    selectedUpscalerHasRealContext_ = selectedUpscalerBackend &&
        selectedUpscalerBackend->nativeFallback;

    if (selectedUpscalerBackend && !selectedUpscalerBackend->nativeFallback) {
        UpscalerModuleBuildContext params;
        params.batch = &batch;
        params.common = lightingCommon;
        params.sceneProviders = sceneProviders;
        params.sourceTarget = renderTargetNode;
        params.renderExtent = renderExtent;
        params.presentationExtent = {static_cast<uint32_t>(width),
                                     static_cast<uint32_t>(height)};
        params.renderScale = renderScale;
        if (const auto* camera = static_cast<const CameraNode*>(renderGraph->GetInstance(cameraNode))) {
            params.cameraNearPlane = camera->GetNearPlane();
            params.cameraFarPlane = camera->GetFarPlane();
        }
        params.makeShaderFamily = [&makeLightingFamily](const std::string& sourceFile,
                                                        const std::string& familyName) {
            return makeLightingFamily(sourceFile, familyName);
        };
        params.registerShaderFamily = registerModuleShaderFamily;
        params.recordExecution = [this](std::string columnName, NodeHandle node) {
            upscalerPerfPasses_.push_back({std::move(columnName), node});
        };
        params.providerInitializationReported = [this](RenderModuleProviderInitializationResult result) {
            selectedUpscalerHasRealContext_ = result.initialized;
            reportedUpscalerInitialization_ = std::move(result);
        };
        upscalerModule = renderGraph->InstantiateRegistered(
            std::string(selectedUpscalerBackend->moduleTypeName), "render_path/upscaler",
            std::any(std::move(params)));

        renderGraph->Connect(renderTargetNode, RenderTargetNodeConfig::RENDER_TARGET,
                             *upscalerModule, UpscalerModulePorts::SOURCE_TARGET);
        const NodeHandle orderingSource = hdrExposureEnabled ? exposureNode : spatialReuseNode;
        renderGraph->Connect(orderingSource, ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                             *upscalerModule, UpscalerModulePorts::ORDERING_WAIT);
        renderGraph->Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                             *upscalerModule, UpscalerModulePorts::DEVICE);
        if (offscreenPresentation) {
            renderGraph->Connect(offscreenTargetNode, RenderTargetNodeConfig::RENDER_TARGET,
                                 *upscalerModule, UpscalerModulePorts::PRESENTATION_TARGET);
        } else {
            renderGraph->Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                                 *upscalerModule, UpscalerModulePorts::PRESENTATION_TARGET);
        }
        if (selectedUpscalerBackend->id == "fsr3") {
            if (!reconstructionMotionModule || !reconstructionDepthModule ||
                !reconstructionHistoryModule || !reconstructionExposureModule) {
                throw std::logic_error("FSR 3.1.4 selection requires all shared temporal inputs");
            }
            renderGraph->Connect(*reconstructionDepthModule,
                                 ReconstructionDepthPorts::NORMALIZED_DEPTH,
                                 *upscalerModule, UpscalerModulePorts::DEPTH_TARGET);
            renderGraph->Connect(*reconstructionMotionModule,
                                 ReconstructionMotionPorts::MOTION,
                                 *upscalerModule, UpscalerModulePorts::MOTION_TARGET);
            renderGraph->Connect(*reconstructionExposureModule,
                                 ReconstructionExposurePorts::EXPOSURE,
                                 *upscalerModule, UpscalerModulePorts::EXPOSURE_TARGET);
            renderGraph->Connect(*reconstructionHistoryModule,
                                 ReconstructionHistoryPorts::JITTER,
                                 *upscalerModule, UpscalerModulePorts::JITTER);
            renderGraph->Connect(*reconstructionHistoryModule,
                                 ReconstructionHistoryPorts::RESET,
                                 *upscalerModule, UpscalerModulePorts::HISTORY_RESET);
            renderGraph->Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                                 *upscalerModule, UpscalerModulePorts::CAMERA_DATA);
        }
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

    // The shade consumes the per-pixel visibility mask that ShadowVisibilityWave writes into
    // HitRecordBuffer. Descriptor binding alone does not declare this hazard; the shared read
    // gatherer gives FrameSyncScheduler the same Resource* as the march and wave gatherers.
    batch.Connect(hitRecordBufferNode, StorageBufferNodeConfig::STORAGE_BUFFER,
                  spatialReuseReadGatherer, 0, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(spatialReuseReadGatherer, BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                  spatialReuseNode, ComputeStageNodeConfig::BUFFER_READ_ARRAY, SlotRoleModifier(SlotRole::Execute));

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
        // The generated tone map's image hazards are wired by its runner from the manifest.
        if (exposureReference) {
            batch.Connect(renderTargetNode, RenderTargetNodeConfig::RENDER_TARGET,
                          exposureNode, ComputeStageNodeConfig::IMAGE_WRITE, SlotRoleModifier(SlotRole::Execute));
        }
        if (!reconstructionInputsEnabled) {
            batch.Connect(sceneRadianceNode, SceneRadianceNodeConfig::CURRENT_HISTORY_TARGET,
                          spatialReuseSceneRadianceWriteGatherer, 0,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute))
                 .Connect(spatialReuseSceneRadianceWriteGatherer, ImageSyncGathererNodeConfig::IMAGE_ARRAY,
                          spatialReuseNode, ComputeStageNodeConfig::IMAGE_WRITE_ARRAY,
                          SlotRoleModifier(SlotRole::Execute));
        }
        if (exposureReference) {
            batch.Connect(sceneRadianceNode, SceneRadianceNodeConfig::CURRENT_HISTORY_TARGET,
                          exposureSceneRadianceReadGatherer, 0,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute))
                 .Connect(exposureSceneRadianceReadGatherer, ImageSyncGathererNodeConfig::IMAGE_ARRAY,
                          exposureNode, ComputeStageNodeConfig::IMAGE_READ_ARRAY,
                          SlotRoleModifier(SlotRole::Execute));
        }
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

    // C0/C1: standalone photon family.  Each node owns its engine-side
    // manifest (no schema/codegen change); descriptor resources still flow
    // through the same provider registry and synthesis helper as every other
    // compute family.  Deposit is the only writer delivered in this lane.
    if (photonCellsEnabled) {
        const auto depositSynth = SynthesizeComputeStage<PhotonCellDepositNode::Metadata,
                                                         PhotonCellDepositNode::MEMBERS>(
            renderGraph, batch, "photon_cell_deposit", photonCellDepositShaderLib,
            photonCellDepositNode, lightingCommon, sceneProviders, {});
        (void)depositSynth;
        CensusStageFromSdi<PhotonCellDepositNode::Metadata, PhotonCellDepositNode::MEMBERS>(
            sdiHazardCensus_, photonCellDepositNode, sceneProviders, sdiNoFeatures,
            &sdiCensusExclusions);

        const auto foldSynth = SynthesizeComputeStage<PhotonCellFoldNode::Metadata,
                                                       PhotonCellFoldNode::MEMBERS>(
            renderGraph, batch, "photon_cell_fold", photonCellFoldShaderLib,
            photonCellFoldNode, lightingCommon, sceneProviders, {});
        (void)foldSynth;
        CensusStageFromSdi<PhotonCellFoldNode::Metadata, PhotonCellFoldNode::MEMBERS>(
            sdiHazardCensus_, photonCellFoldNode, sceneProviders, sdiNoFeatures,
            &sdiCensusExclusions);

        if (photonCellsClearEnabled) {
            const auto clearSynth = SynthesizeComputeStage<PhotonCellClearNode::Metadata,
                                                           PhotonCellClearNode::MEMBERS>(
                renderGraph, batch, "photon_cell_clear", photonCellClearShaderLib,
                photonCellClearNode, lightingCommon, sceneProviders, {});
            (void)clearSynth;
            CensusStageFromSdi<PhotonCellClearNode::Metadata, PhotonCellClearNode::MEMBERS>(
                sdiHazardCensus_, photonCellClearNode, sceneProviders, sdiNoFeatures,
                &sdiCensusExclusions);
        }
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
    if (rtLightingMode != RtLightingMode::Off) {
        waveSdiFeatures.Enable(kFeatureRayQueryLighting);
    }
    if (envFlagEnabled("VIXEN_RECIPE_CHUNK_CULL")) {
        waveSdiFeatures.Enable(kFeatureRecipeChunkCull);
    }
    if (waveSlotSwizzleEnabled) {
        waveSdiFeatures.Enable(kFeatureWaveSlotSwizzle);
    }
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
    // R716: the camera's fov/aspect give the wave the camera pixel cone, so a shadow ray
    // marches procedural bodies at its shaded pixel's footprint (ShadowVisibilityWave.comp).
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          shadowVisibilityWavePushConstantGatherer, WaveSdi::Push::fov::INDEX,
                          ExtractField(&CameraData::fov, SlotRole::Execute));
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          shadowVisibilityWavePushConstantGatherer, WaveSdi::Push::aspect::INDEX,
                          ExtractField(&CameraData::aspect, SlotRole::Execute));

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
        if (rtLightingMode != RtLightingMode::Off) {
            waveReservoirSdiFeatures.Enable(kFeatureRayQueryLighting);
        }
        if (waveSlotSwizzleEnabled) {
            waveReservoirSdiFeatures.Enable(kFeatureWaveSlotSwizzle);
        }
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
        // W-LEAN L3: append the shade fold's {cellRadiance, table} reads after
        // HitRecordBuffer in the shared SRS read gatherer.
        batch.Connect(hitAccumCellRadianceBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                      spatialReuseReadGatherer, 1, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(hitAccumTableBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                      spatialReuseReadGatherer, 2, SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
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

    // C1: march-side deposit consumes final visibility bits from the wave and
    // writes only the photon table.  Fold reads/RMWs that table afterward.
    // Explicit ordering is retained alongside the buffer declarations because
    // the shared Resource* gatherers declare hazards but do not by themselves
    // order two independent ComputeStage submits.
    if (photonCellsEnabled) {
        batch.Connect(hitRecordBufferNode, StorageBufferNodeConfig::STORAGE_BUFFER,
                      photonCellDepositReadGatherer, 0,
                      SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(photonCellDepositReadGatherer,
                      BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                      photonCellDepositNode, ComputeStageNodeConfig::BUFFER_READ_ARRAY,
                      SlotRoleModifier(SlotRole::Execute));
        batch.Connect(photonCellTableBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                      photonCellDepositWriteGatherer, 0,
                      SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(photonCellDepositWriteGatherer,
                      BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                      photonCellDepositNode, ComputeStageNodeConfig::BUFFER_WRITE_ARRAY,
                      SlotRoleModifier(SlotRole::Execute));

        batch.Connect(photonCellTableBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                      photonCellFoldReadGatherer, 0,
                      SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(photonCellFoldReadGatherer,
                      BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                      photonCellFoldNode, ComputeStageNodeConfig::BUFFER_READ_ARRAY,
                      SlotRoleModifier(SlotRole::Execute));
        batch.Connect(photonCellTableBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                      photonCellFoldWriteGatherer, 0,
                      SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        batch.Connect(photonCellFoldWriteGatherer,
                      BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                      photonCellFoldNode, ComputeStageNodeConfig::BUFFER_WRITE_ARRAY,
                      SlotRoleModifier(SlotRole::Execute));

        batch.Connect(shadowVisibilityWaveNode,
                      ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                      photonCellDepositNode,
                      ComputeStageNodeConfig::ORDERING_WAIT_SEMAPHORE);
        batch.Connect(photonCellDepositNode,
                      ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                      photonCellFoldNode,
                      ComputeStageNodeConfig::ORDERING_WAIT_SEMAPHORE);

        if (photonCellsClearEnabled) {
            batch.Connect(photonCellTableBuffer, StorageBufferNodeConfig::STORAGE_BUFFER,
                          photonCellClearWriteGatherer, 0,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
            batch.Connect(photonCellClearWriteGatherer,
                          BufferSyncGathererNodeConfig::BUFFER_ARRAY,
                          photonCellClearNode, ComputeStageNodeConfig::BUFFER_WRITE_ARRAY,
                          SlotRoleModifier(SlotRole::Execute));
            batch.Connect(photonCellClearNode,
                          ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                          photonCellDepositNode,
                          ComputeStageNodeConfig::ORDERING_WAIT_SEMAPHORE);
        }
    }

    // W1b cross-dispatch hazards: the shadow wave read-modify-writes
    // HitRecordBuffer between the march and its lighting readers. The shared
    // Resource* gatherers register the GPU hazards; ordering-only connections
    // separately establish the required march → wave → SpatialReuse topology.
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

    // In the default path, the march is the HitRecordBuffer producer and the wave is its RMW
    // consumer. The optional accumulate pass occupies the wave's ordering-wait slot when enabled.
    if (!hitAccumEnabled) {
        batch.Connect(computeDispatch, ComputeDispatchNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                      shadowVisibilityWaveNode, ComputeStageNodeConfig::ORDERING_WAIT_SEMAPHORE);
    }

    // SpatialReuseShade consumes this frame's visibility bits. When the optional cell shade
    // exists, the wave → cell-shade → SRS chain below supplies this order; otherwise the
    // direct ordering edge keeps the shade after the wave for every topology tie-break.
    if (!hitAccumResolveEnabled) {
        batch.Connect(shadowVisibilityWaveNode, ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                      spatialReuseNode, ComputeStageNodeConfig::ORDERING_WAIT_SEMAPHORE);
    }

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
                  blitNode, BlitNodeConfig::COMMAND_POOL);
    connectPresentationTargetInfo(blitNode, BlitNodeConfig::SWAPCHAIN_INFO);
    connectPresentationImageIndex(blitNode, BlitNodeConfig::IMAGE_INDEX);
    batch.Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  blitNode, BlitNodeConfig::CURRENT_FRAME_INDEX)
         .Connect(frameSyncNode, FrameSyncNodeConfig::IN_FLIGHT_FENCE,
                  blitNode, BlitNodeConfig::IN_FLIGHT_FENCE)
         .Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_SEMAPHORE,
                  blitNode, BlitNodeConfig::TIMELINE_SEMAPHORE_IN)
         .Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_FRAME_BASE,
                  blitNode, BlitNodeConfig::TIMELINE_FRAME_BASE_IN);
    if (usesWsi) {
        batch.Connect(swapChainNode, SwapChainNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY,
                      blitNode, BlitNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY)
         // Baked-Perf M6 Task 6.1 (audit E2): BlitNode is now the real first swapchain-
         // touching submit on the writesNoImage march path (the march no longer waits
         // imageAvailable itself — see ComputeDispatchWaitsForSwapchainAcquire's doc
         // comment), so it must consume the acquire wait BlitNodeConfig's own
         // IMAGE_AVAILABLE_SEMAPHORES_ARRAY slot already anticipated for exactly this.
         .Connect(frameSyncNode, FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY,
                  blitNode, BlitNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY);
    }
    // IMAGE_READ is the required native fallback declared by BlitNodeConfig; the optional
    // UPSCALED_IMAGE_READ input overrides it when the selected typed upscaler module is present.
    // Keep the fallback bound for both paths so graph validation doesn't depend on which module
    // was selected.
    batch.Connect(renderTargetNode, RenderTargetNodeConfig::RENDER_TARGET,
                  blitNode, BlitNodeConfig::IMAGE_READ, SlotRoleModifier(SlotRole::Execute));
    if (upscalerModule.has_value()) {
        renderGraph->Connect(*upscalerModule, UpscalerModulePorts::UPSCALED_TARGET,
                             blitNode, BlitNodeConfig::UPSCALED_IMAGE_READ);
        renderGraph->Connect(*upscalerModule, UpscalerModulePorts::RENDER_COMPLETE,
                             blitNode, BlitNodeConfig::ORDERING_WAIT_SEMAPHORE);
    }
    // Ordering-only edge (BlitNode never waits it — see BlitNodeConfig's ORDERING_WAIT_SEMAPHORE
    // doc): establishes the SpatialReuse-before-Blit TOPOLOGY (Sampled Lighting Inc3 M5 — moved
    // from DirectLighting, which is no longer the render-target writer) so the scheduler's
    // groupId-order edge direction is correct (mirrors the sky-projection/UI ordering-edge
    // convention below). W-LEAN L3: SRS is the sole render-target writer again
    // (the composite is its own gated tail — no standalone resolve stage).
    if (hdrExposureEnabled) {
        if (hdrExposureFixed) {
            batch.Connect(exposureInputProducer, ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                          exposureReduceNode, ComputeStageNodeConfig::ORDERING_WAIT_SEMAPHORE);
        } else {
            batch.Connect(exposureInputProducer, ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                          exposureMeterNode, ComputeStageNodeConfig::ORDERING_WAIT_SEMAPHORE)
                 .Connect(exposureMeterNode, ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                          exposureReduceNode, ComputeStageNodeConfig::ORDERING_WAIT_SEMAPHORE);
        }
        if (exposureReference) {
            batch.Connect(exposureReduceNode, ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                          exposureNode, ComputeStageNodeConfig::ORDERING_WAIT_SEMAPHORE);
        }
    }
    if (!upscalerModule.has_value() && hdrExposureEnabled) {
        batch.Connect(exposureNode, ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                      blitNode, BlitNodeConfig::ORDERING_WAIT_SEMAPHORE);
    } else if (!upscalerModule.has_value()) {
        batch.Connect(spatialReuseNode, ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                      blitNode, BlitNodeConfig::ORDERING_WAIT_SEMAPHORE);
    }

    // P5b M3 (extended for Tiered ESVO Inc1 M3; Sampled Lighting Inc3 M1: chain now runs through
    // DirectLighting + BlitNode first): the march->DirectLighting->Blit->[optional sky fixture]->UI
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
    // march(HitRecord)->DirectLighting(GENERAL)->Blit(GENERAL)->[optional sky fixture]->UI(GENERAL)
    // timeline edges tag the UI group as present (its render pass owns GENERAL->PRESENT_SRC)
    // and order every pass. WSI acquire (march waits imageAvailable) and present (UI signals
    // uiComplete) remain binary.
    if (skyProjectionFixtureEnabled) {
        batch.Connect(blitNode, BlitNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                      skyProjectionNode, SkyProjectionNodeConfig::COMPOSITE_WAIT_SEMAPHORE);
        batch.Connect(skyProjectionNode, SkyProjectionNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                      uiCompositeNode, UIRenderNodeConfig::COMPOSITE_WAIT_SEMAPHORE);
    } else {
        // Preserve the direct blit-to-UI ordering edge when the fixture triple is absent.
        batch.Connect(blitNode, BlitNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                      uiCompositeNode, UIRenderNodeConfig::COMPOSITE_WAIT_SEMAPHORE);
    }

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
    FinalizeLightingShadowDispatches(directLightingNode, probeGatherNode,
                                     shadowRayTraceNode, probeApplyNode);
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
    // Atomically register all connections
    size_t connectionCount = batch.GetConnectionCount();
    mainLogger->Info("Registering " + std::to_string(connectionCount) + " connections...");
    batch.RegisterAll();

    mainLogger->Info("Successfully wired " + std::to_string(connectionCount) + " connections");

    mainLogger->Info("Complete render pipeline built with " + std::to_string(renderGraph->GetNodeCount()) + " nodes");
}
