/**
 * @file test_lightingconfig_parity.cpp
 * @brief Drift-guard: the generated lighting and scene-look records match
 *        their documented std430 byte layouts.
 *
 * Sampled Lighting Inc0 M1: lights become generated, drift-guarded data types (C++
 * and GLSL from one C# [GpuStruct] source — see codegen/config-schemas/LightingConfig.cs).
 * There is no shader consumer wired yet (Lighting.glsl / BodyInstanceRayMarch.comp are
 * explicitly out of scope for this milestone), so unlike test_octree_config_sdi_parity
 * (which reflects a compiled SPIR-V shader), this test asserts the generated C++ header's
 * own static_asserts hold — i.e. it re-proves offsetof/sizeof directly, so a hand-edit of
 * the generated header (which must never happen — see the kernel-framework skill's "don't
 * hand-edit .g.h" rule) or a future schema change that isn't regenerated is caught here
 * too, not just at compile time via the header's own static_asserts.
 *
 * When the next milestone wires a real shader consumer, add a SPIR-V-reflection sibling
 * test mirroring test_octree_config_sdi_parity's pattern (reflect the compiled shader,
 * compare per-field offsets) — this test's job is the schema/codegen contract, not the
 * shader wiring.
 */

#include <gtest/gtest.h>

#include "Generated/LightingConfig.g.h"
#include "Generated/SceneLookSettings.g.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

using Vixen::Gpu::Light;
using Vixen::Gpu::LightingSettings;
using Vixen::Gpu::SceneLookSettings;

// ---------------------------------------------------------------------------
// Light (std430, 32 bytes): direction_or_position@0 (vec3, stored as 3 floats
// in C++), kind@12, radiance@16 (vec3), range@28. The legacy array stride is
// preserved; cel spill-purpose scales live in a parallel LightingConfig array.
// ---------------------------------------------------------------------------

TEST(LightingConfigParity, LightStd430Layout) {
    EXPECT_EQ(sizeof(Light), 32u) << "Light std430 size drifted from the documented 32 B";
    EXPECT_EQ(offsetof(Light, direction_or_positionX), 0u);
    EXPECT_EQ(offsetof(Light, direction_or_positionY), 4u);
    EXPECT_EQ(offsetof(Light, direction_or_positionZ), 8u);
    EXPECT_EQ(offsetof(Light, kind), 12u);
    EXPECT_EQ(offsetof(Light, radianceX), 16u);
    EXPECT_EQ(offsetof(Light, radianceY), 20u);
    EXPECT_EQ(offsetof(Light, radianceZ), 24u);
    EXPECT_EQ(offsetof(Light, range), 28u);
}

// ---------------------------------------------------------------------------
// LightingSettings (std430, 240 bytes): lightCount@0, ambientIntensity@4,
// lights[4]@16, per-light spill-purpose scales@144, cel/sky settings@160.
// ---------------------------------------------------------------------------

TEST(LightingConfigParity, LightingSettingsStd430Layout) {
    EXPECT_EQ(sizeof(LightingSettings), 240u);
    EXPECT_EQ(offsetof(LightingSettings, lightCount), 0u);
    EXPECT_EQ(offsetof(LightingSettings, ambientIntensity), 4u);
    EXPECT_EQ(offsetof(LightingSettings, lights), 16u)
        << "lights[] must sit at offset 16 (std430 16-byte alignment of the nested Light struct)";
    EXPECT_EQ(offsetof(LightingSettings, celSpillPurposeScales), 144u);
    EXPECT_EQ(offsetof(LightingSettings, shadingMode), 160u);
    EXPECT_EQ(offsetof(LightingSettings, celBandCount), 164u);
    EXPECT_EQ(offsetof(LightingSettings, celShadowThreshold), 168u);
    EXPECT_EQ(offsetof(LightingSettings, celLitThreshold), 172u);
    EXPECT_EQ(offsetof(LightingSettings, celRampSoftness), 176u);
    EXPECT_EQ(offsetof(LightingSettings, celLitHueShiftDegrees), 180u);
    EXPECT_EQ(offsetof(LightingSettings, celShadowHueShiftDegrees), 184u);
    EXPECT_EQ(offsetof(LightingSettings, celBandFalloffStart), 188u);
    EXPECT_EQ(offsetof(LightingSettings, celBandFalloffEnd), 192u);
    EXPECT_EQ(offsetof(LightingSettings, celLightSpillScale), 196u);
    EXPECT_EQ(offsetof(LightingSettings, dimBandFloor), 200u);
    EXPECT_EQ(offsetof(LightingSettings, skyColorX), 208u);
    EXPECT_EQ(offsetof(LightingSettings, skyMode), 220u);
    EXPECT_EQ(offsetof(LightingSettings, skyGradientSlope), 224u);
}

TEST(LightingConfigParity, SceneLookSettingsStd430Layout) {
    EXPECT_EQ(sizeof(SceneLookSettings), 44u);
    EXPECT_EQ(offsetof(SceneLookSettings, exposureCompensationEV), 0u);
    EXPECT_EQ(offsetof(SceneLookSettings, exposureKeyValue), 4u);
    EXPECT_EQ(offsetof(SceneLookSettings, exposureMinEV100), 8u);
    EXPECT_EQ(offsetof(SceneLookSettings, exposureMaxEV100), 12u);
    EXPECT_EQ(offsetof(SceneLookSettings, exposureLowPercent), 16u);
    EXPECT_EQ(offsetof(SceneLookSettings, exposureHighPercent), 20u);
    EXPECT_EQ(offsetof(SceneLookSettings, exposureSpeedUp), 24u);
    EXPECT_EQ(offsetof(SceneLookSettings, exposureSpeedDown), 28u);
    EXPECT_EQ(offsetof(SceneLookSettings, exposureDeltaSeconds), 32u);
    EXPECT_EQ(offsetof(SceneLookSettings, lookSaturation), 36u);
    EXPECT_EQ(offsetof(SceneLookSettings, presentationMode), 40u);
}

TEST(LightingConfigParity, LightsArrayStridesAtExactLightSize) {
    // Each element of lights[kMaxLightsInc0] must stride at exactly sizeof(Light)
    // with no inter-element padding (mirrors the OctreeConfig channels[] contract
    // and TierRef's array-stride proof).
    LightingSettings cfg{};
    const auto* p0 = reinterpret_cast<const uint8_t*>(&cfg.lights[0]);
    const auto* p1 = reinterpret_cast<const uint8_t*>(&cfg.lights[1]);
    EXPECT_EQ(static_cast<std::size_t>(p1 - p0), sizeof(Light));
    EXPECT_EQ(std::extent<decltype(cfg.lights)>::value, 4u)
        << "kMaxLightsInc0 must stay 4 — update this test deliberately if the cap changes";
}

TEST(LightingConfigParity, IsTriviallyCopyableStandardLayout) {
    EXPECT_TRUE(std::is_standard_layout_v<Light>);
    EXPECT_TRUE(std::is_trivially_copyable_v<Light>);
    EXPECT_TRUE(std::is_standard_layout_v<LightingSettings>);
    EXPECT_TRUE(std::is_trivially_copyable_v<LightingSettings>);
    EXPECT_TRUE(std::is_standard_layout_v<SceneLookSettings>);
    EXPECT_TRUE(std::is_trivially_copyable_v<SceneLookSettings>);
}
