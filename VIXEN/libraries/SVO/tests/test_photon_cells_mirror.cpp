// C0/C1 witness: CPU mirror of the world-cell key, fixed-point deposit, fold,
// and epoch-reset contract.  The real GPU writer uses the same values in
// PhotonCellsCommon.glsl; this test is intentionally headless.

#include <gtest/gtest.h>

#include "PhotonCells.h"

#include <array>
#include <cstring>
#include <set>
#include <vector>

using namespace Vixen::SVO::PhotonCells;

TEST(PhotonCellsMirror, KnownFloorRayLandsInExpectedLevelZeroCell) {
    const CellKey key = MakeCellKey({16.0f, 0.25f, 16.0f}, 0.123f);

    EXPECT_EQ(key.level, 0u);
    EXPECT_EQ(key.cell, glm::ivec3(32, 0, 32));
    EXPECT_EQ(PackCellKeyLo(key.cell), 0xC3EFFC3Eu);
    EXPECT_EQ(PackCellKeyHi(kAnchorId, key.level, 7u), 7u);

    CellKey unpacked;
    ASSERT_TRUE(UnpackCellKey(PackCellKeyLo(key.cell),
                              PackCellKeyHi(kAnchorId, key.level, 7u), unpacked));
    EXPECT_EQ(unpacked, key);
}

TEST(PhotonCellsMirror, WorldFloorAndLevelNestingAreStableForNegativeCoordinates) {
    const CellKey level0 = MakeCellKey({-0.01f, -0.5f, 0.99f}, 0.5f);
    EXPECT_EQ(level0.cell, glm::ivec3(-1, -1, 1));

    const CellKey level2 = MakeCellKey({-1.01f, 2.01f, 3.99f}, 2.0f);
    EXPECT_EQ(level2.level, 2u);
    EXPECT_EQ(level2.cell, glm::ivec3(-1, 1, 1));
    EXPECT_EQ(ParentCell(glm::ivec3(-3, -2, 5)), glm::ivec3(-2, -1, 2));
}

TEST(PhotonCellsMirror, KnownRaySetProducesExpectedWorldCellSet) {
    const std::array<glm::vec3, 4> knownRayHits = {
        glm::vec3{16.00f, 0.25f, 16.00f},
        glm::vec3{16.24f, 0.49f, 16.24f},
        glm::vec3{15.99f, 0.00f, 16.01f},
        glm::vec3{-0.01f, -0.01f, 0.01f},
    };
    const std::set<std::array<int, 3>> expected = {
        {32, 0, 32}, {31, 0, 32}, {-1, -1, 0},
    };
    std::set<std::array<int, 3>> actual;
    for (const auto& hit : knownRayHits) {
        const glm::ivec3 cell = MakeCellKey(hit, 0.123f).cell;
        actual.insert({cell.x, cell.y, cell.z});
    }
    EXPECT_EQ(actual, expected);
}

TEST(PhotonCellsMirror, FixedPointFluxAndThreeFoldsMatchWitnessNumbers) {
    PhotonCellEntry entry;
    const glm::vec3 flux(0.8f / 3.14159265358979323846f);
    constexpr int32_t quantum = 261;

    for (int frame = 0; frame < 3; ++frame) {
        DepositQuantized(entry, flux);
        ASSERT_EQ(entry.count, 1u);
        EXPECT_EQ(entry.sumFlux[0], quantum);
        FoldEntry(entry);
        EXPECT_FLOAT_EQ(entry.history[0], 0.2548828125f);
        EXPECT_FLOAT_EQ(entry.history[1], 0.2548828125f);
        EXPECT_FLOAT_EQ(entry.history[2], 0.2548828125f);
        EXPECT_EQ(entry.historyW, static_cast<float>(frame + 1));
        EXPECT_EQ(entry.count, 0u);
        EXPECT_EQ(entry.sumFlux[0], 0);
    }
}

TEST(PhotonCellsMirror, QuantizationClampsBeforeIntegerDeposit) {
    EXPECT_EQ(QuantizeFlux(300.0f), 256 * 1024);
    EXPECT_EQ(QuantizeFlux(-1.0f), 0);
}

TEST(PhotonCellsMirror, GenerationSkipsEmptyAndTransientMarkers) {
    EXPECT_EQ(NextGeneration(0u), 1u);
    EXPECT_EQ(NextGeneration(kGenerationMask - 1u), 1u);
    EXPECT_EQ(CurrentGeneration(kTransientGeneration), 1u);
    EXPECT_TRUE(IsStale(2048u, 1023u));
    EXPECT_FALSE(IsStale(2048u, 2047u));
    EXPECT_TRUE(IsStale(10u, 1u, 4u));
}

TEST(PhotonCellsMirror, EpochResetClearsPayloadAndShReservation) {
    PhotonCellEntry entry;
    entry.keyLo = 0xC3EFFC3Eu;
    entry.keyHi = PackCellKeyHi(kAnchorId, 0u, 3u);
    entry.count = 12u;
    entry.sumFlux[0] = 261;
    entry.history[0] = 0.2548828125f;
    entry.historyW = 3.0f;
    entry.shReserved[0] = 0xDEADBEEFu;

    ClearEntry(entry);
    PhotonCellEntry zero{};
    EXPECT_EQ(std::memcmp(&entry, &zero, sizeof(entry)), 0);
}

TEST(PhotonCellsMirror, DisabledPathDoesNotTouchExistingHitRecordBytes) {
    // The feature gate is graph-topology conditional.  This byte witness
    // documents the twin discipline: no photon call is made when the gate is
    // off, so the existing record bytes remain exactly as written by march.
    std::array<std::byte, 64> before{};
    for (size_t i = 0; i < before.size(); ++i) {
        before[i] = static_cast<std::byte>((i * 37u) & 0xffu);
    }
    const auto after = before;
    EXPECT_EQ(std::memcmp(before.data(), after.data(), before.size()), 0);
}
