/**
 * @file test_lightingconfig_parity.cpp
 * @brief Drift-guard: the generated `Vixen::Gpu::Light` / `Vixen::Gpu::LightingConfig`
 *        C++ structs match their documented std430 byte layout.
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

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <iterator>

using Vixen::Gpu::Light;
using Vixen::Gpu::LightingConfig;

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
// LightingConfig keeps the 32-byte Light stride and the parallel purpose
// array. Its capacity is declared by the schema, never mirrored in this test.
// ---------------------------------------------------------------------------

TEST(LightingConfigParity, LightingConfigStd430Layout) {
    constexpr size_t count = std::extent_v<decltype(LightingConfig::lights)>;
    constexpr size_t purposeOffset = 16u + count * sizeof(Light);
    constexpr size_t settingsOffset = purposeOffset + count * sizeof(float);
    EXPECT_EQ(sizeof(LightingConfig), settingsOffset + 48u);
    EXPECT_EQ(offsetof(LightingConfig, lightCount), 0u);
    EXPECT_EQ(offsetof(LightingConfig, ambientIntensity), 4u);
    EXPECT_EQ(offsetof(LightingConfig, lights), 16u);
    EXPECT_EQ(offsetof(LightingConfig, celSpillPurposeScales), purposeOffset);
    EXPECT_EQ(offsetof(LightingConfig, shadingMode), settingsOffset);
    EXPECT_EQ(offsetof(LightingConfig, celBandCount), settingsOffset + 4u);
    EXPECT_EQ(offsetof(LightingConfig, celShadowThreshold), settingsOffset + 8u);
    EXPECT_EQ(offsetof(LightingConfig, celLitThreshold), settingsOffset + 12u);
    EXPECT_EQ(offsetof(LightingConfig, celRampSoftness), settingsOffset + 16u);
    EXPECT_EQ(offsetof(LightingConfig, celLitHueShiftDegrees), settingsOffset + 20u);
    EXPECT_EQ(offsetof(LightingConfig, celShadowHueShiftDegrees), settingsOffset + 24u);
    EXPECT_EQ(offsetof(LightingConfig, celBandFalloffStart), settingsOffset + 28u);
    EXPECT_EQ(offsetof(LightingConfig, celBandFalloffEnd), settingsOffset + 32u);
    EXPECT_EQ(offsetof(LightingConfig, celLightSpillScale), settingsOffset + 36u);
    EXPECT_EQ(offsetof(LightingConfig, exposureCompensationEV), settingsOffset + 40u);
}

TEST(LightingConfigParity, LightsArrayStridesAtExactLightSize) {
    LightingConfig cfg{};
    const auto* p0 = reinterpret_cast<const uint8_t*>(&cfg.lights[0]);
    const auto* p1 = reinterpret_cast<const uint8_t*>(&cfg.lights[1]);
    EXPECT_EQ(static_cast<std::size_t>(p1 - p0), sizeof(Light));
    EXPECT_EQ(std::size(cfg.lights), std::size(cfg.celSpillPurposeScales));
}

TEST(LightingConfigParity, IsTriviallyCopyableStandardLayout) {
    EXPECT_TRUE(std::is_standard_layout_v<Light>);
    EXPECT_TRUE(std::is_trivially_copyable_v<Light>);
    EXPECT_TRUE(std::is_standard_layout_v<LightingConfig>);
    EXPECT_TRUE(std::is_trivially_copyable_v<LightingConfig>);
}
