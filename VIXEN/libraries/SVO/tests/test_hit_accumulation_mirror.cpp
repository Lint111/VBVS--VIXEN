// test_hit_accumulation_mirror.cpp — Wavefront W3a: red-green the
// (recipeId, cell@mip) accumulation math BEFORE any GPU path exists
// (HitAccumulation.h is the specification; the slice plan's own TDD
// prescription; same discipline as test_reservoir_mirror.cpp).

#include <gtest/gtest.h>
#include "HitAccumulation.h"

#include <glm/glm.hpp>
#include <random>

using namespace Vixen::SVO::HitAccum;

namespace {
constexpr float kDetail0 = 0.25f;  // detailSize0 for every test (arbitrary, fixed)

HitSample MakeSample(glm::vec3 pos, glm::vec3 dir, float dist, glm::vec3 tp) {
    HitSample s;
    s.worldPos = pos;
    s.direction = glm::normalize(dir);
    s.distance = dist;
    s.throughput = tp;
    return s;
}
}  // namespace

// --- Mip selection -----------------------------------------------------------

TEST(HitAccumMirror, SelectMip_ZeroWhenFootprintWithinDetail) {
    EXPECT_EQ(SelectMip(0.0f, kDetail0), 0u);
    EXPECT_EQ(SelectMip(kDetail0 * 0.5f, kDetail0), 0u);
    EXPECT_EQ(SelectMip(kDetail0, kDetail0), 0u);  // exactly at detail -> still per-ray
}

TEST(HitAccumMirror, SelectMip_CeilLog2Growth) {
    EXPECT_EQ(SelectMip(kDetail0 * 2.0f, kDetail0), 1u);   // exactly 2x -> mip 1
    EXPECT_EQ(SelectMip(kDetail0 * 2.1f, kDetail0), 2u);   // just past 2x -> mip 2
    EXPECT_EQ(SelectMip(kDetail0 * 4.0f, kDetail0), 2u);   // exactly 4x -> mip 2
    EXPECT_EQ(SelectMip(kDetail0 * 4.1f, kDetail0), 3u);   // ceil(log2 16.4/4)... 4.1x -> mip 3
    EXPECT_EQ(SelectMip(kDetail0 * 1024.0f, kDetail0), 10u);
}

TEST(HitAccumMirror, CellSize_DoublesPerMip) {
    EXPECT_FLOAT_EQ(CellSize(0u, kDetail0), kDetail0);
    EXPECT_FLOAT_EQ(CellSize(1u, kDetail0), kDetail0 * 2.0f);
    EXPECT_FLOAT_EQ(CellSize(5u, kDetail0), kDetail0 * 32.0f);
}

// --- Keying ------------------------------------------------------------------

TEST(HitAccumMirror, CellKey_StableWithinCell_SplitsAcrossBoundary) {
    const float footprint = kDetail0 * 4.0f;  // mip 2, cellSize = 1.0
    const CellKey a = MakeCellKey(7u, {0.10f, 0.20f, 0.30f}, footprint, kDetail0);
    const CellKey b = MakeCellKey(7u, {0.90f, 0.80f, 0.70f}, footprint, kDetail0);  // same 1.0-cell
    const CellKey c = MakeCellKey(7u, {1.10f, 0.20f, 0.30f}, footprint, kDetail0);  // next cell in +x

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_EQ(a.mip, 2u);
    EXPECT_EQ(a.cell, glm::ivec3(0, 0, 0));
    EXPECT_EQ(c.cell, glm::ivec3(1, 0, 0));
}

TEST(HitAccumMirror, CellKey_NegativeCoordinatesFloorCorrectly) {
    const float footprint = kDetail0 * 4.0f;  // cellSize = 1.0
    const CellKey k = MakeCellKey(1u, {-0.25f, -1.75f, 2.5f}, footprint, kDetail0);
    EXPECT_EQ(k.cell, glm::ivec3(-1, -2, 2));  // floor, never trunc-toward-zero
}

TEST(HitAccumMirror, CellKey_RecipeIsCategorical_NeverShared) {
    const float footprint = kDetail0 * 4.0f;
    const glm::vec3 p{0.5f, 0.5f, 0.5f};
    const CellKey r2 = MakeCellKey(2u, p, footprint, kDetail0);
    const CellKey r3 = MakeCellKey(3u, p, footprint, kDetail0);
    EXPECT_NE(r2, r3);  // a cell spanning recipes splits by construction
}

// --- Accumulate / Resolve ----------------------------------------------------

TEST(HitAccumMirror, SingleSample_ResolvesToItselfWithinQuantum) {
    const float footprint = kDetail0 * 4.0f;  // mip 2, cellSize 1.0
    const glm::vec3 pos{0.37f, 0.62f, 0.11f};
    const glm::vec3 dir = glm::normalize(glm::vec3{0.3f, 0.9f, -0.2f});
    const HitSample s = MakeSample(pos, dir, 12.5f, {0.8f, 0.6f, 0.4f});
    const CellKey key = MakeCellKey(5u, pos, footprint, kDetail0);

    AccumEntry e;
    Accumulate(e, s, key, kDetail0);
    ASSERT_EQ(e.count, 1u);

    const ResolvedCell r = Resolve(e, key, kDetail0);
    const float cellSize = CellSize(key.mip, kDetail0);
    const float posQuantum = cellSize / static_cast<float>(kPosScale);
    EXPECT_NEAR(r.avgPos.x, pos.x, posQuantum);
    EXPECT_NEAR(r.avgPos.y, pos.y, posQuantum);
    EXPECT_NEAR(r.avgPos.z, pos.z, posQuantum);
    const float dirQuantum = 1.0f / static_cast<float>(kDirScale);
    EXPECT_NEAR(r.avgDir.x, dir.x, dirQuantum);
    EXPECT_NEAR(r.avgDir.y, dir.y, dirQuantum);
    EXPECT_NEAR(r.avgDir.z, dir.z, dirQuantum);
    EXPECT_NEAR(r.avgDistance, 12.5f, posQuantum);
    EXPECT_NEAR(r.avgThroughput.x, 0.8f, 1.0f / static_cast<float>(kThroughputScale));
    EXPECT_NEAR(r.toksvigFactor, 1.0f, 2.0f * dirQuantum);  // one sample -> full agreement
}

TEST(HitAccumMirror, Merge_ExactlyAssociativeAndCommutative) {
    // The load-bearing GPU property: integer sums mean WAVE ORDER CANNOT change
    // the result. Verified bit-for-bit, not approximately.
    const float footprint = kDetail0 * 8.0f;
    std::mt19937 rng(0x37A0u);  // deterministic
    std::uniform_real_distribution<float> u(-0.99f, 0.99f);

    const CellKey key = MakeCellKey(9u, {0.1f, 0.1f, 0.1f}, footprint, kDetail0);
    auto randSample = [&]() {
        return MakeSample({u(rng) + 0.5f, u(rng) + 0.5f, u(rng) + 0.5f},
                          {u(rng), u(rng), u(rng)}, 5.0f + u(rng), {0.5f, 0.5f, 0.5f});
    };

    AccumEntry a, b, c;
    for (int i = 0; i < 7; ++i)  Accumulate(a, randSample(), key, kDetail0);
    for (int i = 0; i < 11; ++i) Accumulate(b, randSample(), key, kDetail0);
    for (int i = 0; i < 3; ++i)  Accumulate(c, randSample(), key, kDetail0);

    // (a+b)+c
    AccumEntry ab = a; MergeEntries(ab, b);
    AccumEntry ab_c = ab; MergeEntries(ab_c, c);
    // a+(b+c)
    AccumEntry bc = b; MergeEntries(bc, c);
    AccumEntry a_bc = a; MergeEntries(a_bc, bc);
    // b+a (commutative)
    AccumEntry ba = b; MergeEntries(ba, a);
    AccumEntry ab2 = a; MergeEntries(ab2, b);

    EXPECT_EQ(ab_c.count, a_bc.count);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(ab_c.sumPosQ[i], a_bc.sumPosQ[i]);
        EXPECT_EQ(ab_c.sumDirQ[i], a_bc.sumDirQ[i]);
        EXPECT_EQ(ab_c.sumThroughputQ[i], a_bc.sumThroughputQ[i]);
        EXPECT_EQ(ab2.sumPosQ[i], ba.sumPosQ[i]);
        EXPECT_EQ(ab2.sumDirQ[i], ba.sumDirQ[i]);
    }
    EXPECT_EQ(ab_c.sumDistQ, a_bc.sumDistQ);
    EXPECT_EQ(ab2.sumDistQ, ba.sumDistQ);
}

TEST(HitAccumMirror, ManySamples_AverageWithinQuantumOfTrueMean) {
    const float footprint = kDetail0 * 4.0f;  // cellSize 1.0
    const CellKey key = MakeCellKey(4u, {0.5f, 0.5f, 0.5f}, footprint, kDetail0);
    AccumEntry e;
    // Two known samples -> exact mean known.
    Accumulate(e, MakeSample({0.25f, 0.50f, 0.75f}, {0, 1, 0}, 10.0f, {1, 0, 0}), key, kDetail0);
    Accumulate(e, MakeSample({0.75f, 0.50f, 0.25f}, {0, 1, 0}, 20.0f, {0, 1, 0}), key, kDetail0);
    const ResolvedCell r = Resolve(e, key, kDetail0);
    const float posQuantum = CellSize(key.mip, kDetail0) / static_cast<float>(kPosScale);
    EXPECT_NEAR(r.avgPos.x, 0.5f, posQuantum);
    EXPECT_NEAR(r.avgPos.z, 0.5f, posQuantum);
    EXPECT_NEAR(r.avgDistance, 15.0f, posQuantum * 32.0f);  // distance quantum scaled by range, generous
    EXPECT_NEAR(r.avgThroughput.x, 0.5f, 1.0f / static_cast<float>(kThroughputScale));
    EXPECT_NEAR(r.toksvigFactor, 1.0f, 4.0f / static_cast<float>(kDirScale));  // same dir -> agreement
}

// --- Toksvig -----------------------------------------------------------------

TEST(HitAccumMirror, Toksvig_PerfectAgreementNeverWidens) {
    EXPECT_FLOAT_EQ(ToksvigWiden(0.3f, 1.0f), 0.3f);
    EXPECT_FLOAT_EQ(ToksvigWiden(0.9f, 1.0f), 0.9f);
}

TEST(HitAccumMirror, Toksvig_MonotoneInDisagreement_NeverNarrows) {
    const float base = 0.3f;
    float prev = ToksvigWiden(base, 1.0f);
    for (float ft = 0.95f; ft > 0.05f; ft -= 0.1f) {
        const float w = ToksvigWiden(base, ft);
        EXPECT_GE(w, prev - 1e-6f) << "widen must be monotone as agreement drops (ft=" << ft << ")";
        EXPECT_GE(w, base) << "widen must never narrow below base";
        EXPECT_LE(w, 1.0f) << "roughness stays clamped to 1";
        prev = w;
    }
}

// --- Two-word packed key (W3b rev 2 — sized by the diag's range finding) ----

TEST(HitAccumMirror, PackKey2_RoundTripsInRange) {
    const CellKey key{200u, 9u, glm::ivec3(-500, 0, 480)};
    const glm::ivec3 anchor(0, 0, 0);
    const uint32_t lo = PackCellKeyLo(key.cell, anchor);
    const uint32_t hi = PackCellKeyHi(key.recipeId, key.mip);
    ASSERT_NE(lo, 0u);  // 0 is the GPU empty sentinel — a valid cell NEVER packs to 0
    CellKey back;
    ASSERT_TRUE(UnpackCellKey(lo, hi, anchor, back));
    EXPECT_EQ(back, key);
}

TEST(HitAccumMirror, PackKey2_EngagingDistanceSpan_MustPack) {
    // THE regression test for the diag's finding: a hit at its mip's engaging
    // distance, at the frustum EDGE, with the real PRIMARY cone coefficient
    // (2·tan((fovYRad/height)·0.5) ≈ 0.001571 at 500 px / 45°). Span at
    // engagement ≈ 2·tan(fov/2)/coef ≈ 527 cells across — the single-word
    // design failed-soft on ALL of these; frustum-center anchoring + 10-bit
    // deltas must hold them.
    const float coef = 2.0f * std::tan((glm::radians(45.0f) / 500.0f) * 0.5f);
    const float detail = 0.02f;
    const glm::vec3 camPos(0.0f);
    const glm::vec3 camForward(0.0f, 0.0f, 1.0f);
    const float t = 40.0f;
    const float footprint = t * coef;                       // ≈ 0.063
    const uint32_t mip = SelectMip(footprint, detail);      // ≥ 1
    ASSERT_GE(mip, 1u);
    const float cellSize = CellSize(mip, detail);
    // Frustum edge at distance t (45° vertical fov): offset ≈ t·tan(22.5°).
    const float edge = t * std::tan(glm::radians(22.5f));
    const glm::vec3 hitEdge = camPos + camForward * t + glm::vec3(edge, 0.0f, 0.0f);
    const glm::ivec3 cell = glm::ivec3(glm::floor(hitEdge / cellSize));
    const glm::ivec3 anchor = AnchorCell(camPos, camForward, coef, detail, mip);
    const uint32_t lo = PackCellKeyLo(cell, anchor);
    ASSERT_NE(lo, 0u) << "frustum-edge cell at engaging distance must pack (span finding)";
    CellKey back;
    ASSERT_TRUE(UnpackCellKey(lo, PackCellKeyHi(3u, mip), anchor, back));
    EXPECT_EQ(back.cell, cell);
}

TEST(HitAccumMirror, PackKey2_OutOfRangeFailsSoft) {
    const glm::ivec3 anchor(0, 0, 0);
    EXPECT_EQ(PackCellKeyLo(glm::ivec3(512, 0, 0), anchor), 0u);
    EXPECT_EQ(PackCellKeyLo(glm::ivec3(0, -512, 0), anchor), 0u);
    EXPECT_NE(PackCellKeyLo(glm::ivec3(511, -511, 511), anchor), 0u);
    // recipeId past 8 bits or mip past 4 bits fail in the HI word:
    EXPECT_EQ(PackCellKeyHi(300u, 2u), 0u);
    EXPECT_EQ(PackCellKeyHi(1u, 17u), 0u);
}

TEST(HitAccumMirror, PackKey2_DistinctKeysDistinctWords) {
    const glm::ivec3 anchor(0, 0, 0);
    const uint32_t loA = PackCellKeyLo(glm::ivec3(1, 2, 3), anchor);
    const uint32_t loB = PackCellKeyLo(glm::ivec3(1, 2, 4), anchor);
    EXPECT_NE(loA, 0u);
    EXPECT_NE(loA, loB);
    const uint32_t hiA = PackCellKeyHi(1u, 2u);
    const uint32_t hiB = PackCellKeyHi(2u, 2u);
    const uint32_t hiC = PackCellKeyHi(1u, 3u);
    EXPECT_NE(hiA, hiB);
    EXPECT_NE(hiA, hiC);
}

TEST(HitAccumMirror, Toksvig_DisagreementShortensAvgDir_EndToEnd) {
    // Two opposing-ish directions in one cell -> |avgDir| < 1 -> widened roughness.
    const float footprint = kDetail0 * 4.0f;
    const CellKey key = MakeCellKey(6u, {0.5f, 0.5f, 0.5f}, footprint, kDetail0);
    AccumEntry e;
    Accumulate(e, MakeSample({0.4f, 0.5f, 0.5f}, {1, 0.2f, 0}, 5.0f, {1, 1, 1}), key, kDetail0);
    Accumulate(e, MakeSample({0.6f, 0.5f, 0.5f}, {-1, 0.2f, 0}, 5.0f, {1, 1, 1}), key, kDetail0);
    const ResolvedCell r = Resolve(e, key, kDetail0);
    EXPECT_LT(r.toksvigFactor, 0.5f);  // strong disagreement
    EXPECT_GT(ToksvigWiden(0.3f, r.toksvigFactor), 0.3f + 0.1f);  // real widen, not epsilon
}
