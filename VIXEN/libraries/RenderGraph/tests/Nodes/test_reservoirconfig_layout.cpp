/**
 * @file test_reservoirconfig_layout.cpp
 * @brief Drift-guard: the generated `Vixen::Gpu::ReservoirConfig` C++ struct's
 *        byte layout matches its own codegen-emitted static_asserts.
 *
 * UNLIKE test_shadowconfig_sdi_parity.cpp / test_prevcameraconfig_sdi_parity.cpp
 * (which reflect a COMPILED SHADER's SPIR-V and compare against it), this is a
 * pure C++ struct-size/offsetof test with NO shader reflection — ReservoirConfig
 * is Sampled Lighting Inc3 M3 SCAFFOLDING for M4/M5 (RIS + temporal/spatial
 * reservoir reuse): no .comp shader binds or reads it yet (see
 * ReservoirConfig.cs's file header), so there is no compiled SPIR-V to reflect
 * against. This test instead pins the EXACT byte layout the codegen tool
 * produced (verified once against the generated header's own static_asserts)
 * so a future accidental hand-edit or schema drift is still caught, even
 * before a shader consumer exists. Once M4/M5 wire a shader binding, add a
 * genuine SPIR-V-reflection sibling test (mirroring ShadowConfig's) alongside
 * this one — do not remove this one, it still guards the C++ side.
 */

#include <gtest/gtest.h>

#include "Generated/ReservoirConfig.g.h"

#include <cstddef>

using Vixen::Gpu::ReservoirConfig;

TEST(ReservoirConfigLayout, StructSizeMatchesStd430) {
    // 7 fields (4 uint32 + 3 float, std430 scalar packing, no vec/array
    // members needing 16-byte alignment) = 28 bytes.
    EXPECT_EQ(sizeof(ReservoirConfig), 28u)
        << "ReservoirConfig must be 28 bytes (Inc3 M3 contract: 4x uint32 + 3x float, std430)";
}

TEST(ReservoirConfigLayout, FieldOffsetsMatchDeclarationOrder) {
    EXPECT_EQ(offsetof(ReservoirConfig, reservoirEnabled),      0u);
    EXPECT_EQ(offsetof(ReservoirConfig, candidateCount),        4u);
    EXPECT_EQ(offsetof(ReservoirConfig, spatialRadius),         8u);
    EXPECT_EQ(offsetof(ReservoirConfig, spatialCount),         12u);
    EXPECT_EQ(offsetof(ReservoirConfig, temporalCap),          16u);
    EXPECT_EQ(offsetof(ReservoirConfig, biasedModeEnabled),    20u);
    EXPECT_EQ(offsetof(ReservoirConfig, lightTreeCutThreshold),24u);
}

// A default-constructed (value-initialized) ReservoirConfig must be entirely
// zero — the M3 byte-identity escape hatch: reservoirEnabled=0 (and every
// other field) must be the natural, no-effort default a caller gets without
// explicitly authoring content (mirrors AccumulationConfig{}'s own
// enabled=0-by-default contract at ITS M1 landing).
TEST(ReservoirConfigLayout, ValueInitializedIsAllZero) {
    ReservoirConfig cfg{};
    EXPECT_EQ(cfg.reservoirEnabled, 0u);
    EXPECT_EQ(cfg.candidateCount, 0u);
    EXPECT_FLOAT_EQ(cfg.spatialRadius, 0.0f);
    EXPECT_EQ(cfg.spatialCount, 0u);
    EXPECT_EQ(cfg.temporalCap, 0u);
    EXPECT_EQ(cfg.biasedModeEnabled, 0u);
    EXPECT_FLOAT_EQ(cfg.lightTreeCutThreshold, 0.0f);
}
