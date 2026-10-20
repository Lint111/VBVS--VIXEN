// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
// Sampled Lighting Inc0 M3: light data wired via a generated [GpuStruct].

#include "Nodes/LightingConfigNode.h"
#include "Core/NodeRegistration.h"
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "Generated/LightingConfig.g.h"
#include "Look/LookResolver.h"
#include "VulkanDevice.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;

// Ring size = frames-in-flight (the value CURRENT_FRAME_INDEX cycles through).
const uint32_t LightingConfigNode::kRingSize = FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT;

namespace {

// Default content: the single directional light Lighting.glsl previously
// hardcoded (direction normalize(1,1,-1), white radiance, ambientIntensity
// 0.3) — reproducing it byte-for-byte through the data path is this
// milestone's whole gate (M2's AFTER capture must stay byte-identical).
Vixen::Gpu::LightingConfig MakeDefaultLightingConfig() {
    Vixen::Gpu::LightingConfig cfg{};
    cfg.lightCount       = 1u;
    cfg.ambientIntensity = 0.3f;

    const float dx = 1.0f, dy = 1.0f, dz = -1.0f;
    const float invLen = 1.0f / std::sqrt(dx * dx + dy * dy + dz * dz);

    cfg.lights[0].direction_or_positionX = dx * invLen;
    cfg.lights[0].direction_or_positionY = dy * invLen;
    cfg.lights[0].direction_or_positionZ = dz * invLen;
    cfg.lights[0].kind      = 0u;  // directional
    cfg.lights[0].radianceX = 1.0f;
    cfg.lights[0].radianceY = 1.0f;
    cfg.lights[0].radianceZ = 1.0f;
    cfg.lights[0].range     = 0.0f;  // unused for directional
    cfg.celSpillPurposeScales[0] = 1.0f;
    return cfg;
}

float FiniteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

float ClampFinite(float value, float fallback, float low, float high) {
    return std::clamp(FiniteOr(value, fallback), low, high);
}

float ReadClampedFloat(const LightingConfigNode& node, const char* parameter,
                       float fallback, float low, float high) {
    return ClampFinite(node.GetParameterValue<float>(parameter, fallback), fallback, low, high);
}

// M11.1: the Cornell demo authors its own light -- the ceiling area-emitter
// (body 5, CornellBoxSceneDefinition.h kLightEmissionIntensity), baked into the
// light-tree and lit via ReSTIR-direct + DDGI-indirect. This global default's
// directional light was never authored by the Cornell scene; left on, it adds
// an unwanted second light whose grazing shadow rays produced the M10 floor/
// wall blotching. Scoped to the Cornell demo env-gate only (both variants, so
// baked vs virtual A/B stays apples-to-apples) -- every other scene keeps the
// directional light unchanged. Ambient stays: it's a flat baseline term, not
// the blotching source (the shadow ray on the directional light is).
bool IsCornellDemo() {
    return std::getenv("VIXEN_DDGI_CORNELL_BAKED_DEMO") != nullptr ||
           std::getenv("VIXEN_DDGI_CORNELL_VIRTUAL_DEMO") != nullptr;
}

}  // namespace

// ====== LightingConfigNodeType ======

std::unique_ptr<NodeInstance> LightingConfigNodeType::CreateInstance(const std::string& n) const {
    return std::make_unique<LightingConfigNode>(n, const_cast<LightingConfigNodeType*>(this));
}

// ====== LightingConfigNode ======

LightingConfigNode::LightingConfigNode(const std::string& n, NodeType* t)
    : TypedNode<LightingConfigNodeConfig>(n, t)
{
}

void LightingConfigNode::TypedSetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_DEBUG("[LightingConfigNode] Setup (graph-scope initialization)");
}

void LightingConfigNode::TypedCompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("[LightingConfigNode] Compile START");

    SetDevice(ctx.In(LightingConfigNodeConfig::VULKAN_DEVICE_IN));
    if (!GetDevice()) {
        throw std::runtime_error("[LightingConfigNode] VULKAN_DEVICE_IN is null");
    }

    static constexpr VkDeviceSize kBufferSize = sizeof(Vixen::Gpu::LightingConfig);

    // FR-7-style: the ring buffers are persistent across recompile — only create once.
    if (!perFrame_.IsInitialized()) {
        perFrame_.Initialize(GetDevice(), kRingSize);
        for (uint32_t i = 0; i < kRingSize; ++i) {
            perFrame_.CreateStorageBuffer(i, kBufferSize);
        }
        NODE_LOG_INFO("[LightingConfigNode] Allocated ring of " +
                      std::to_string(kRingSize) + " storage buffers (" +
                      std::to_string(static_cast<uint64_t>(kBufferSize)) + " bytes each)");
    } else {
        NODE_LOG_INFO("[LightingConfigNode] Reusing persistent ring buffers across recompile");
    }

    // Publish an initial buffer (frame 0) so any compile-time descriptor wiring
    // has a valid handle before the first Execute.
    ctx.Out(LightingConfigNodeConfig::LIGHTING_CONFIG_BUFFER, perFrame_.GetUniformBuffer(0));

    NODE_LOG_INFO("[LightingConfigNode] Outputs published");
}

void LightingConfigNode::TypedExecuteImpl(TypedExecuteContext& ctx) {
    // Per-frame ring index from FrameSyncNode (clamp via modulo for safety).
    uint32_t frameIndex = ctx.In(LightingConfigNodeConfig::CURRENT_FRAME_INDEX) % kRingSize;

    // The resolver is the only source of look values; the whole record is re-uploaded each frame (208 B),
    // so layer changes take effect without graph rewiring.
    const ResolvedLook look = look_.Resolve();
    Vixen::Gpu::LightingConfig cfg = MakeDefaultLightingConfig();
    if (look.lights) {
        const LookLightSet& set = *look.lights;
        cfg = {};
        cfg.ambientIntensity = set.ambientIntensity;
        cfg.lightCount = std::min<uint32_t>(set.lightCount, static_cast<uint32_t>(std::size(cfg.lights)));
        for (uint32_t i = 0; i < cfg.lightCount; ++i) {
            cfg.lights[i] = set.lights[i];
            cfg.celSpillPurposeScales[i] = ClampFinite(set.celSpillPurposeScales[i], 1.0f, 0.0f, 4.0f);
        }
    } else if (IsCornellDemo()) {
        // Ceiling area-emitter (light-tree/ReSTIR/DDGI) is the Cornell scene's
        // sole light; drop the stray directional light but keep the ambient
        // baseline (not the blotching source -- see IsCornellDemo() comment).
        cfg.lightCount = 0u;
    }

    const uint32_t requestedMode = GetParameterValue<uint32_t>(
        LightingConfigNodeConfig::PARAM_SHADING_MODE,
        LightingConfigNodeConfig::SHADING_MODE_CEL);
    cfg.shadingMode = requestedMode == LightingConfigNodeConfig::SHADING_MODE_LAMBERT_GGX
        ? LightingConfigNodeConfig::SHADING_MODE_LAMBERT_GGX
        : LightingConfigNodeConfig::SHADING_MODE_CEL;
    cfg.celBandCount = std::clamp(GetParameterValue<uint32_t>(
        LightingConfigNodeConfig::PARAM_CEL_BAND_COUNT,
        LightingConfigNodeConfig::DEFAULT_CEL_BAND_COUNT), 2u, 5u);
    cfg.celShadowThreshold = ClampFinite(
        look.celShadowThreshold, LightingConfigNodeConfig::DEFAULT_CEL_SHADOW_THRESHOLD, 0.0f, 1.0f);
    cfg.celLitThreshold = ClampFinite(
        look.celLitThreshold, LightingConfigNodeConfig::DEFAULT_CEL_LIT_THRESHOLD, 0.0f, 1.0f);
    if (cfg.celLitThreshold <= cfg.celShadowThreshold) {
        cfg.celShadowThreshold = LightingConfigNodeConfig::DEFAULT_CEL_SHADOW_THRESHOLD;
        cfg.celLitThreshold = LightingConfigNodeConfig::DEFAULT_CEL_LIT_THRESHOLD;
    }
    cfg.celRampSoftness = ClampFinite(
        look.celRampSoftness, LightingConfigNodeConfig::DEFAULT_CEL_RAMP_SOFTNESS, 0.0f, 0.5f);
    cfg.celLitHueShiftDegrees = std::remainder(FiniteOr(GetParameterValue<float>(
        LightingConfigNodeConfig::PARAM_CEL_LIT_HUE_SHIFT_DEGREES,
        LightingConfigNodeConfig::DEFAULT_CEL_LIT_HUE_SHIFT_DEGREES),
        LightingConfigNodeConfig::DEFAULT_CEL_LIT_HUE_SHIFT_DEGREES), 360.0f);
    cfg.celShadowHueShiftDegrees = std::remainder(FiniteOr(GetParameterValue<float>(
        LightingConfigNodeConfig::PARAM_CEL_SHADOW_HUE_SHIFT_DEGREES,
        LightingConfigNodeConfig::DEFAULT_CEL_SHADOW_HUE_SHIFT_DEGREES),
        LightingConfigNodeConfig::DEFAULT_CEL_SHADOW_HUE_SHIFT_DEGREES), 360.0f);
    cfg.celBandFalloffStart = ReadClampedFloat(
        *this, LightingConfigNodeConfig::PARAM_CEL_BAND_FALLOFF_START,
        LightingConfigNodeConfig::DEFAULT_CEL_BAND_FALLOFF_START, 0.0f, 1000000.0f);
    cfg.celBandFalloffEnd = ReadClampedFloat(
        *this, LightingConfigNodeConfig::PARAM_CEL_BAND_FALLOFF_END,
        LightingConfigNodeConfig::DEFAULT_CEL_BAND_FALLOFF_END, 0.0f, 1000000.0f);
    if (cfg.celBandFalloffEnd <= cfg.celBandFalloffStart) {
        cfg.celBandFalloffStart = 0.0f;
        cfg.celBandFalloffEnd = 0.0f;
    }
    cfg.celLightSpillScale = ReadClampedFloat(
        *this, LightingConfigNodeConfig::PARAM_CEL_LIGHT_SPILL_SCALE,
        LightingConfigNodeConfig::DEFAULT_CEL_LIGHT_SPILL_SCALE, 0.0f, 0.25f);
    cfg.exposureCompensationEV = ClampFinite(
        look.exposureCompensationEV, LightingConfigNodeConfig::DEFAULT_EXPOSURE_COMPENSATION_EV,
        LightingConfigNodeConfig::MIN_EXPOSURE_COMPENSATION_EV,
        LightingConfigNodeConfig::MAX_EXPOSURE_COMPENSATION_EV);
    const auto& exposureDefaults = LightingConfigNodeConfig::EXPOSURE_PRESETS[0];
    cfg.exposureKeyValue = ClampFinite(look.exposureKeyValue, exposureDefaults.keyValue, 0.001f, 10.0f);
    cfg.exposureMinEV100 = ClampFinite(look.exposureMinEV100, exposureDefaults.minEV100, -32.0f, 32.0f);
    cfg.exposureMaxEV100 = ClampFinite(
        look.exposureMaxEV100, exposureDefaults.maxEV100, cfg.exposureMinEV100, 32.0f);
    cfg.exposureLowPercent = ClampFinite(look.exposureLowPercent, exposureDefaults.lowPercent, 0.0f, 100.0f);
    cfg.exposureHighPercent = ClampFinite(
        look.exposureHighPercent, exposureDefaults.highPercent, cfg.exposureLowPercent, 100.0f);
    cfg.exposureSpeedUp = ClampFinite(look.exposureSpeedUp, exposureDefaults.speedUp, 0.0f, 100.0f);
    cfg.exposureSpeedDown = ClampFinite(look.exposureSpeedDown, exposureDefaults.speedDown, 0.0f, 100.0f);
    cfg.lookSaturation = ClampFinite(look.saturation, 1.0f, 0.0f, 2.0f);
    cfg.presentationMode = look.presentationMode == 1u ? 1u : 0u;
    cfg.frameSeed = frameIndex;
    // Adaptation advances by this frame's step: a pinned step keeps captures
    // deterministic, otherwise the graph's frame time keeps it rate independent.
    const float fixedDeltaSeconds = ReadClampedFloat(
        *this, LightingConfigNodeConfig::PARAM_EXPOSURE_FIXED_DELTA_SECONDS, 0.0f,
        0.0f, LightingConfigNodeConfig::MAX_EXPOSURE_DELTA_SECONDS);
    const RenderGraph* graph = GetOwningGraph();
    const float frameDeltaSeconds = graph ? FiniteOr(graph->GetTime().GetDeltaTime(), 0.0f) : 0.0f;
    cfg.exposureDeltaSeconds = fixedDeltaSeconds > 0.0f
        ? fixedDeltaSeconds
        : std::clamp(frameDeltaSeconds, 0.0f, LightingConfigNodeConfig::MAX_EXPOSURE_DELTA_SECONDS);
    cfg.dimBandFloor = ClampFinite(look.dimBandFloor, LightingConfigNodeConfig::DEFAULT_DIM_BAND_FLOOR, 0.0f, 0.95f);
    cfg.skyMode = look.skyMode == LightingConfigNodeConfig::SKY_MODE_SOLID
        ? LightingConfigNodeConfig::SKY_MODE_SOLID
        : LightingConfigNodeConfig::SKY_MODE_GRADIENT;
    cfg.skyColorX = ClampFinite(look.skyColor[0], LightingConfigNodeConfig::DEFAULT_SKY_COLOR_R, 0.0f, 1000.0f);
    cfg.skyColorY = ClampFinite(look.skyColor[1], LightingConfigNodeConfig::DEFAULT_SKY_COLOR_G, 0.0f, 1000.0f);
    cfg.skyColorZ = ClampFinite(look.skyColor[2], LightingConfigNodeConfig::DEFAULT_SKY_COLOR_B, 0.0f, 1000.0f);
    cfg.skyGradientSlope = ClampFinite(
        look.skyGradientSlope, LightingConfigNodeConfig::DEFAULT_SKY_GRADIENT_SLOPE, 0.0f, 1.0f);

    // Upload into this frame's ring buffer (host-coherent: no flush needed).
    void* mapped = perFrame_.GetUniformBufferMapped(frameIndex);
    if (mapped) {
        std::memcpy(mapped, &cfg, sizeof(cfg));
    }

    // Emit THIS frame's buffer so the descriptor binds the freshly written data.
    ctx.Out(LightingConfigNodeConfig::LIGHTING_CONFIG_BUFFER, perFrame_.GetUniformBuffer(frameIndex));
}

void LightingConfigNode::TypedCleanupImpl(TypedCleanupContext& ctx) {
    // Persist across recompile; release only on final application teardown.
    // Keep persistent resources ONLY across a Recompile (the device survives). On DeviceLost the
    // device and every child object are gone — keeping them (the old '!= FinalTeardown' guard)
    // left stale handles that crashed the first post-recovery use/teardown (KI-004 class).
    if (ctx.reason == CleanupReason::Recompile) {
        NODE_LOG_INFO("[LightingConfigNode] Cleanup (recompile) - keeping persistent ring buffers");
        return;
    }

    NODE_LOG_INFO("[LightingConfigNode] Cleanup (final teardown) - destroying ring buffers");
    perFrame_.Cleanup();
}

} // namespace Vixen::RenderGraph

// Self-registration: registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::LightingConfigNodeType);
