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

using Vixen::Gpu::Light;
using Vixen::Gpu::LightingConfig;

// ---------------------------------------------------------------------------
// Light (std430, 32 bytes): direction_or_position@0 (vec3, stored as 3 floats
// in C++), kind@12, radiance@16 (vec3), range@28.
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
// LightingConfig (std430, 144 bytes): lightCount@0, ambientIntensity@4,
// lights[4]@16 (Light is 16-aligned via its Float3 members, so the array
// itself starts at offset 16, not 8 — the 8 bytes from @8..@16 are implicit
// std430 padding).
// ---------------------------------------------------------------------------

TEST(LightingConfigParity, LightingConfigStd430Layout) {
    EXPECT_EQ(sizeof(LightingConfig), 144u) << "LightingConfig std430 size drifted from the documented 144 B";
    EXPECT_EQ(offsetof(LightingConfig, lightCount), 0u);
    EXPECT_EQ(offsetof(LightingConfig, ambientIntensity), 4u);
    EXPECT_EQ(offsetof(LightingConfig, lights), 16u)
        << "lights[] must sit at offset 16 (std430 16-byte alignment of the nested Light struct)";
}

TEST(LightingConfigParity, LightsArrayStridesAtExactLightSize) {
    // Each element of lights[kMaxLightsInc0] must stride at exactly sizeof(Light)
    // with no inter-element padding (mirrors the OctreeConfig channels[] contract
    // and TierRef's array-stride proof).
    LightingConfig cfg{};
    const auto* p0 = reinterpret_cast<const uint8_t*>(&cfg.lights[0]);
    const auto* p1 = reinterpret_cast<const uint8_t*>(&cfg.lights[1]);
    EXPECT_EQ(static_cast<std::size_t>(p1 - p0), sizeof(Light));
    EXPECT_EQ(std::extent<decltype(cfg.lights)>::value, 4u)
        << "kMaxLightsInc0 must stay 4 — update this test deliberately if the cap changes";
}

TEST(LightingConfigParity, IsTriviallyCopyableStandardLayout) {
    EXPECT_TRUE(std::is_standard_layout_v<Light>);
    EXPECT_TRUE(std::is_trivially_copyable_v<Light>);
    EXPECT_TRUE(std::is_standard_layout_v<LightingConfig>);
    EXPECT_TRUE(std::is_trivially_copyable_v<LightingConfig>);
}
