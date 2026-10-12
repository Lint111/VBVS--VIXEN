#pragma once

#include "Data/Nodes/LightingConfigNodeConfig.h"
#include "Data/Nodes/SkySphereNodeConfig.h"
#include "Nodes/LightingConfigNode.h"
#include "Nodes/SkySphereNode.h"

#include <cstdint>

namespace Vixen::App {

// Scene-declared environment (R507.3): the background a ray sees when it misses every body.
// The gradient and solid colour ride the LightingConfig slot; the starfield is the existing
// SkySphereNode (T-1138) layered additively on top.
struct EnvironmentDeclaration {
    enum class Background : uint32_t { Gradient = 0, Solid = 1 };
    Background background = Background::Gradient;
    float color[3] = {0.02f, 0.02f, 0.06f};
    float gradientSlope = 0.4f;     // Gradient only: colour * (1 - screenUvY * slope)
    bool starfield = false;
    uint32_t starfieldSeed = 0x5a17f13du;
    float starfieldBrightness = 0.45f;
};

// Scene-declared cel profile (R507.3): thresholds are N.L values, the floor is the band-0 value
// as a fraction of the lit band (0 = black shadow step).
struct CelProfile {
    float shadowThreshold = 0.16f;
    float litThreshold = 0.86f;
    float rampSoftness = 0.08f;
    float dimBandFloor = 0.0f;
};

struct SceneLook {
    EnvironmentDeclaration environment;
    CelProfile cel;
};

inline void ApplySceneLook(Vixen::RenderGraph::LightingConfigNode& lighting,
                           Vixen::RenderGraph::SkySphereNode* skySphere, const SceneLook& look) {
    using Cfg = Vixen::RenderGraph::LightingConfigNodeConfig;
    lighting.SetParameter(Cfg::PARAM_SKY_MODE, static_cast<uint32_t>(look.environment.background));
    lighting.SetParameter(Cfg::PARAM_SKY_COLOR_R, look.environment.color[0]);
    lighting.SetParameter(Cfg::PARAM_SKY_COLOR_G, look.environment.color[1]);
    lighting.SetParameter(Cfg::PARAM_SKY_COLOR_B, look.environment.color[2]);
    lighting.SetParameter(Cfg::PARAM_SKY_GRADIENT_SLOPE, look.environment.gradientSlope);
    lighting.SetParameter(Cfg::PARAM_CEL_SHADOW_THRESHOLD, look.cel.shadowThreshold);
    lighting.SetParameter(Cfg::PARAM_CEL_LIT_THRESHOLD, look.cel.litThreshold);
    lighting.SetParameter(Cfg::PARAM_CEL_RAMP_SOFTNESS, look.cel.rampSoftness);
    lighting.SetParameter(Cfg::PARAM_DIM_BAND_FLOOR, look.cel.dimBandFloor);
    if (skySphere) {
        using Sky = Vixen::RenderGraph::SkySphereNodeConfig;
        skySphere->SetParameter(Sky::PARAM_ENABLED, look.environment.starfield ? 1u : 0u);
        skySphere->SetParameter(Sky::PARAM_SEED, look.environment.starfieldSeed);
        skySphere->SetParameter(Sky::PARAM_BRIGHTNESS, look.environment.starfieldBrightness);
    }
}

} // namespace Vixen::App
