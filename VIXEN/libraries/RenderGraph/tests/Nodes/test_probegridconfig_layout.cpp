/**
 * @file test_probegridconfig_layout.cpp
 * @brief Drift-guard: the generated `Vixen::Gpu::ProbeGridConfig` C++ struct's
 *        byte layout matches its own codegen-emitted static_asserts.
 *
 * UNLIKE test_shadowconfig_sdi_parity.cpp (which reflects a COMPILED SHADER's
 * SPIR-V and compares against it), this is a pure C++ struct-size/offsetof
 * test with NO shader reflection — ProbeGridConfig is Sampled Lighting Inc4
 * M2 SCAFFOLDING for M3-M6 (probe-update compute pass, Chebyshev visibility,
 * shade-pass gather): no .comp shader binds or reads it yet (see
 * ProbeGridConfig.cs's file header), so there is no compiled SPIR-V to
 * reflect against. This test instead pins the EXACT byte layout the codegen
 * tool produced (verified once against the generated header's own
 * static_asserts) so a future accidental hand-edit or schema drift is still
 * caught, even before a shader consumer exists. Once M3+ wires a shader
 * binding, add a genuine SPIR-V-reflection sibling test (mirroring
 * ShadowConfig's) alongside this one — do not remove this one, it still
 * guards the C++ side. Mirrors test_reservoirconfig_layout.cpp exactly.
 */

#include <gtest/gtest.h>

#include "Generated/ProbeGridConfig.g.h"

#include <cstddef>

using Vixen::Gpu::ProbeGridConfig;

TEST(ProbeGridConfigLayout, StructSizeMatchesStd430) {
    // 12 fields (7 uint32 + 5 float, std430 scalar packing, all fields
    // flattened scalars -- no vec/array members needing 16-byte alignment,
    // mirrors LightTreeBuffer.cs's own Float3-flattening precedent) = 48 bytes.
    EXPECT_EQ(sizeof(ProbeGridConfig), 48u)
        << "ProbeGridConfig must be 48 bytes (Inc4 M2 contract: 7x uint32 + 5x float, std430)";
}

TEST(ProbeGridConfigLayout, FieldOffsetsMatchDeclarationOrder) {
    EXPECT_EQ(offsetof(ProbeGridConfig, probeGridEnabled), 0u);
    EXPECT_EQ(offsetof(ProbeGridConfig, originX),           4u);
    EXPECT_EQ(offsetof(ProbeGridConfig, originY),           8u);
    EXPECT_EQ(offsetof(ProbeGridConfig, originZ),          12u);
    EXPECT_EQ(offsetof(ProbeGridConfig, spacingX),         16u);
    EXPECT_EQ(offsetof(ProbeGridConfig, spacingY),         20u);
    EXPECT_EQ(offsetof(ProbeGridConfig, spacingZ),         24u);
    EXPECT_EQ(offsetof(ProbeGridConfig, countX),           28u);
    EXPECT_EQ(offsetof(ProbeGridConfig, countY),           32u);
    EXPECT_EQ(offsetof(ProbeGridConfig, countZ),           36u);
    EXPECT_EQ(offsetof(ProbeGridConfig, raysPerProbe),     40u);
    EXPECT_EQ(offsetof(ProbeGridConfig, hysteresisRate),   44u);
}

// A default-constructed (value-initialized) ProbeGridConfig must be entirely
// zero — the M2 byte-identity escape hatch: probeGridEnabled=0 (and every
// other field) must be the natural, no-effort default a caller gets without
// explicitly authoring content (mirrors ReservoirConfig{}'s own
// reservoirEnabled=0-by-default contract at ITS M3 landing). Note the NODE's
// own runtime default (ProbeGridConfigNode.cpp's MakeDefaultProbeGridConfig)
// deliberately overrides origin/spacing/count/raysPerProbe/hysteresisRate to
// non-zero sane values — this test only pins the struct's zero-init contract,
// not the node's authored defaults.
TEST(ProbeGridConfigLayout, ValueInitializedIsAllZero) {
    ProbeGridConfig cfg{};
    EXPECT_EQ(cfg.probeGridEnabled, 0u);
    EXPECT_FLOAT_EQ(cfg.originX, 0.0f);
    EXPECT_FLOAT_EQ(cfg.originY, 0.0f);
    EXPECT_FLOAT_EQ(cfg.originZ, 0.0f);
    EXPECT_FLOAT_EQ(cfg.spacingX, 0.0f);
    EXPECT_FLOAT_EQ(cfg.spacingY, 0.0f);
    EXPECT_FLOAT_EQ(cfg.spacingZ, 0.0f);
    EXPECT_EQ(cfg.countX, 0u);
    EXPECT_EQ(cfg.countY, 0u);
    EXPECT_EQ(cfg.countZ, 0u);
    EXPECT_EQ(cfg.raysPerProbe, 0u);
    EXPECT_FLOAT_EQ(cfg.hysteresisRate, 0.0f);
}
