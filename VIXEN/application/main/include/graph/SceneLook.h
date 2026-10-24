#pragma once

#include "Data/Nodes/LightingConfigNodeConfig.h"
#include "Data/Nodes/SkySphereNodeConfig.h"
#include "Look/LookResolver.h"
#include "Nodes/LightingConfigNode.h"
#include "Nodes/SkySphereNode.h"

#include <array>
#include <cstdint>
#include <utility>

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

// Scene-declared exposure (R670.1, R670.2). Auto lets the meter adapt inside a named
// LightingConfigNodeConfig::EXPOSURE_PRESETS window; Fixed bypasses the meter and exposes for
// fixedEV100 (scene units, the presets' EV100 scale). The default is the space preset: the
// default environment is bodies on a navy night sky, which must stay dark.
struct ExposureDeclaration {
    enum class Mode : uint32_t { Auto = 0, Fixed = 1 };
    Mode mode = Mode::Auto;
    const char* preset = "space";   // Auto only
    float fixedEV100 = 0.0f;        // Fixed only
    float compensationEV = 0.0f;    // artistic bias added after metering (either mode)
};

enum class PresentationMode : uint32_t {
    Canonical = 0,
    Presentation = 1,
};

// The default SceneLook is the application's built-in scenes (HUD, editor, starfield, Cornell):
// the node defaults above and the space exposure preset.
struct SceneLook {
    EnvironmentDeclaration environment;
    CelProfile cel;
    ExposureDeclaration exposure;
    float saturation = 1.0f;        // linear chroma scale before the tone curve: 0 greyscale, 1 unchanged, 2 doubled
    PresentationMode presentationMode = PresentationMode::Canonical;
};

// The exposure parameters a declaration resolves to; an unknown preset name falls back to
// "default", the node's own fallback.
inline Vixen::RenderGraph::LightingConfigNodeConfig::ExposurePreset ExposureSettings(
    const ExposureDeclaration& exposure) {
    using Cfg = Vixen::RenderGraph::LightingConfigNodeConfig;
    if (exposure.mode == ExposureDeclaration::Mode::Fixed) return Cfg::FixedExposure(exposure.fixedEV100);
    const Cfg::ExposurePreset* preset = Cfg::FindExposurePreset(exposure.preset);
    return preset ? *preset : Cfg::EXPOSURE_PRESETS[0];
}

// The exposure channels a declaration contributes. Fixed sets only the window: the speeds come from the
// layers below, and the resolver applies the meter's snap when the resolved window has zero width.
inline Vixen::RenderGraph::PartialLook ExposureLook(const ExposureDeclaration& exposure) {
    const auto settings = ExposureSettings(exposure);
    Vixen::RenderGraph::PartialLook look;
    look.exposureMinEV100 = settings.minEV100;
    look.exposureMaxEV100 = settings.maxEV100;
    look.exposureCompensationEV = exposure.compensationEV;
    if (exposure.mode == ExposureDeclaration::Mode::Fixed) return look;
    look.exposureKeyValue = settings.keyValue;
    look.exposureLowPercent = settings.lowPercent;
    look.exposureHighPercent = settings.highPercent;
    look.exposureSpeedUp = settings.speedUp;
    look.exposureSpeedDown = settings.speedDown;
    return look;
}

inline Vixen::RenderGraph::PartialLook SceneLookPartial(const SceneLook& look) {
    Vixen::RenderGraph::PartialLook partial = ExposureLook(look.exposure);
    partial.saturation = look.saturation;
    partial.presentationMode = static_cast<uint32_t>(look.presentationMode);
    partial.skyMode = static_cast<uint32_t>(look.environment.background);
    partial.skyColor = std::array<float, 3>{look.environment.color[0], look.environment.color[1],
                                            look.environment.color[2]};
    partial.skyGradientSlope = look.environment.gradientSlope;
    partial.celShadowThreshold = look.cel.shadowThreshold;
    partial.celLitThreshold = look.cel.litThreshold;
    partial.celRampSoftness = look.cel.rampSoftness;
    partial.dimBandFloor = look.cel.dimBandFloor;
    return partial;
}

// Scene layer slots: the declared look and the scene's lights are separate contributions.
inline constexpr uint32_t kSceneLookSlot = 0;
inline constexpr uint32_t kSceneLightsSlot = 1;

inline void SetSceneLookLayer(Vixen::RenderGraph::LightingConfigNode& lighting, const SceneLook& look) {
    Vixen::RenderGraph::LookContribution contribution;
    contribution.look = SceneLookPartial(look);
    lighting.Look().SetSlot(Vixen::RenderGraph::LookLayer::Scene, kSceneLookSlot, std::move(contribution));
}

inline void SetSceneLightsLayer(Vixen::RenderGraph::LightingConfigNode& lighting,
                                Vixen::RenderGraph::LookLightSet lights) {
    Vixen::RenderGraph::LookContribution contribution;
    contribution.look.lights = std::move(lights);
    lighting.Look().SetSlot(Vixen::RenderGraph::LookLayer::Scene, kSceneLightsSlot, std::move(contribution));
}

inline void ApplySceneLook(Vixen::RenderGraph::LightingConfigNode& lighting,
                           Vixen::RenderGraph::SkySphereNode* skySphere, const SceneLook& look) {
    SetSceneLookLayer(lighting, look);
    if (skySphere) {
        using Sky = Vixen::RenderGraph::SkySphereNodeConfig;
        skySphere->SetParameter(Sky::PARAM_ENABLED, look.environment.starfield ? 1u : 0u);
        skySphere->SetParameter(Sky::PARAM_SEED, look.environment.starfieldSeed);
        skySphere->SetParameter(Sky::PARAM_BRIGHTNESS, look.environment.starfieldBrightness);
    }
}

} // namespace Vixen::App
