// test_reservoir_mirror.cpp — Sampled Lighting Inc3 M4: CPU-mirror RED-GREEN tests for
// the RIS/ReSTIR reservoir math (weighted reservoir sampling + MIS-balance-heuristic
// temporal combine), per the plan's Task 4 mandate — these must be green BEFORE the GPU
// shading path is trusted (mirrors the Inc0 VNDF-mirror discipline; see
// test_vndf_mirror.cpp and Reservoir.h's own file header for the shared verification
// philosophy: independent reference implementations, never mirror-vs-copy-of-mirror).
//
// Covers the plan's THREE unbiasedness identities:
//   1. Reservoir weight normalization: W = (1/targetPdf) * (1/M) * Sum(w_i) — verified
//      numerically on synthetic candidate sets (ReservoirWeightNormalization*).
//   2. Combining two reservoirs preserves the estimator's expected value: no energy
//      gain/loss across a temporal combine (TemporalCombine*).
//   3. A many-sample RIS reservoir converges NUMERICALLY to an INDEPENDENT brute-force
//      Monte-Carlo estimate of the same integral — NOT a circular check against RIS code
//      itself (RisConvergesToBruteForceMonteCarlo*), the exact "shape-check vs external
//      ref, not circular parity" lesson from the kernel-framework work (project memory).
//
// @shader shaders/DirectLighting.comp (Reservoir::Update / Reservoir::Combine mirror it)

#include <gtest/gtest.h>

// MSVC defines far/near/min/max as macros via <windows.h>.
#undef far
#undef near
#undef min
#undef max

#include "Reservoir.h"

#include <cmath>
#include <cstdint>
#include <numeric>
#include <random>
#include <vector>

using namespace Vixen::SVO;

// ---------------------------------------------------------------------------
// Field-parity note: ReservoirState (this file's CPU mirror shape) is field-for-field
// identical to the GENERATED Vixen::Gpu::ReservoirRecord (y@0/weightSum@4/sampleCount@8/
// targetPdf@12, 16B total — see codegen/config-schemas/ReservoirRecord.cs and the
// generated libraries/RenderGraph/include/Generated/ReservoirRecord.g.h, which carries
// its OWN static_asserts on those offsets). SVO does not depend on RenderGraph (AR#3/#4
// cycle-break — see SVO/CMakeLists.txt), so this test cannot #include the generated
// header directly; the parity is instead exercised end-to-end by
// test_hitrecord_readback.cpp-style RenderGraph-side tests once DirectLightingNode
// wires the actual SSBO (M4 GPU-wiring commit).
// ---------------------------------------------------------------------------
static_assert(sizeof(ReservoirState) >= 16, "ReservoirState must be at least as large as the std430 GPU record");

namespace {

// A synthetic "light-tree cut" of N candidates, each with a known, independently-
// computable target-function value (emitted power / distance^2 to a fixed shading
// point) — deterministic, not read from any baked octree, so these tests are pure
// math over a closed-form integral with a KNOWN true answer.
struct SyntheticCut {
    std::vector<float> power;      // per-candidate emitted power
    std::vector<float> distance2;  // per-candidate squared distance to the shading point

    uint32_t Count() const { return static_cast<uint32_t>(power.size()); }
    float PHat(uint32_t i) const { return power[i] / distance2[i]; }

    // The TRUE integral this whole test file validates against: a brute-force sum of
    // every candidate's own contribution, weighted uniformly (1/N each) — the "evaluate
    // every candidate directly" ground truth RIS/WRS is approximating with ONE stored
    // sample. This is intentionally NOT expressed via Reservoir::* — an independent,
    // direct computation (the kernel-framework "external reference" discipline).
    double BruteForceMonteCarloEstimate() const {
        double sum = 0.0;
        const double n = static_cast<double>(Count());
        for (uint32_t i = 0; i < Count(); ++i) {
            // Each candidate is its own unbiased MC sample of the sum under uniform
            // sampling: estimate_i = pHat(i) / (1/N) = pHat(i) * N, averaged over N
            // draws-with-replacement converges to Sum(pHat) as N -> infinity (uniform
            // source pdf, so this reduces to the plain arithmetic mean * N == sum).
            sum += static_cast<double>(PHat(i));
        }
        (void)n;
        return sum;  // Sum(pHat(i)) over all N candidates == the true integral (uniform-pdf sum)
    }
};

SyntheticCut MakeSyntheticCut(uint32_t n, uint32_t seed) {
    std::mt19937 gen(seed);
    std::uniform_real_distribution<float> powerDist(1.0f, 50.0f);
    std::uniform_real_distribution<float> distDist(1.0f, 20.0f);

    SyntheticCut cut;
    cut.power.reserve(n);
    cut.distance2.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        cut.power.push_back(powerDist(gen));
        const float d = distDist(gen);
        cut.distance2.push_back(d * d);
    }
    return cut;
}

}  // namespace

// ===========================================================================
// Identity 1: reservoir weight normalization.
// W = (1/targetPdf) * (weightSum/M) must reproduce the SAME expected value as a direct
// per-candidate importance-sampling estimate, verified numerically over many independent
// reservoir runs (each run streams a fresh set of uniform-random candidate draws).
// ===========================================================================

TEST(ReservoirMirror, WeightNormalizationFormulaMatchesManualComputation) {
    // A single deterministic run: manually recompute W from the SAME weightSum/M/
    // targetPdf the reservoir reports, checking Reservoir::UnbiasedWeight against the
    // formula transcribed independently (not calling UnbiasedWeight to "verify itself").
    SyntheticCut cut = MakeSyntheticCut(16, /*seed=*/42);
    std::mt19937 rng(1234);

    ReservoirState r = Reservoir::BuildFromUniformCandidates(
        cut.Count(), [&](uint32_t i) { return cut.PHat(i); }, rng);

    ASSERT_TRUE(r.IsValid());
    ASSERT_GT(r.targetPdf, 0.0f);

    const float manualW = (1.0f / r.targetPdf) * (r.weightSum / static_cast<float>(r.sampleCount));
    const float reportedW = Reservoir::UnbiasedWeight(r);
    EXPECT_NEAR(manualW, reportedW, 1e-6f);
}

TEST(ReservoirMirror, WeightNormalizationProducesUnbiasedEstimateOverManyRuns) {
    // The RIS unbiasedness claim: E[pHat(y) * W] == Sum(pHat(i)) over the candidate set
    // (Bitterli et al. eq 6, specialized to a uniform source pdf where Sum(pHat(i)) is
    // exactly the brute-force integral this file's SyntheticCut also computes directly).
    // Verified by averaging pHat(y)*W over many independent reservoir runs against the
    // SAME fixed candidate set and checking convergence to the known true sum.
    SyntheticCut cut = MakeSyntheticCut(12, /*seed=*/7);
    const double trueSum = cut.BruteForceMonteCarloEstimate();

    constexpr int kRuns = 20000;
    double accum = 0.0;
    std::mt19937 rng(99);
    for (int run = 0; run < kRuns; ++run) {
        ReservoirState r = Reservoir::BuildFromUniformCandidates(
            cut.Count(), [&](uint32_t i) { return cut.PHat(i); }, rng);
        ASSERT_TRUE(r.IsValid());
        const float w = Reservoir::UnbiasedWeight(r);
        accum += static_cast<double>(r.targetPdf) * static_cast<double>(w);
    }
    const double estimate = accum / static_cast<double>(kRuns);

    const double relError = std::fabs(estimate - trueSum) / trueSum;
    EXPECT_LT(relError, 0.02)
        << "RIS estimate (" << estimate << ") over " << kRuns
        << " runs must converge to the true candidate-power sum (" << trueSum
        << ") within tolerance (relative error " << relError << ")";
}

// ===========================================================================
// Identity 2: temporal combine preserves the estimator's expected value — no
// energy gain/loss from merging a current-frame reservoir with a (re-evaluated)
// previous-frame reservoir, verified two ways: (a) a structural check that Combine's
// weightSum/sampleCount bookkeeping is EXACTLY additive (the "resume the same WRS
// stream" identity Reservoir.h's header derives), and (b) a statistical check that
// combining two independently-built reservoirs over the SAME candidate set still
// converges to the SAME true sum as identity 1's single-reservoir case (no bias
// introduced by the combine step itself).
// ===========================================================================

TEST(ReservoirMirror, TemporalCombineBookkeepingIsExactlyAdditive) {
    SyntheticCut cut = MakeSyntheticCut(10, /*seed=*/5);
    std::mt19937 rngA(111), rngB(222);

    ReservoirState a = Reservoir::BuildFromUniformCandidates(
        cut.Count(), [&](uint32_t i) { return cut.PHat(i); }, rngA);
    ReservoirState b = Reservoir::BuildFromUniformCandidates(
        cut.Count(), [&](uint32_t i) { return cut.PHat(i); }, rngB);
    ASSERT_TRUE(a.IsValid());
    ASSERT_TRUE(b.IsValid());

    const float expectedWeightSum = a.weightSum + b.weightSum;
    const uint32_t expectedSampleCount = a.sampleCount + b.sampleCount;

    // pHatAtPrevY re-evaluated "this frame" at b.y — since both reservoirs draw from the
    // SAME synthetic cut (no target-function drift in this structural test), it equals
    // b.targetPdf exactly; a genuinely time-varying scene would re-evaluate against a
    // different frame's geometry, which is exactly what makes the combine unbiased under
    // drift (see Reservoir.h's file header) but is orthogonal to this bookkeeping check.
    const float pHatAtPrevY = cut.PHat(b.y);
    std::mt19937 combineRng(333);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    Reservoir::Combine(a, b, pHatAtPrevY, unit(combineRng));

    EXPECT_NEAR(a.weightSum, expectedWeightSum, 1e-4f)
        << "Combine must be exactly additive on weightSum (no energy gain/loss)";
    EXPECT_EQ(a.sampleCount, expectedSampleCount)
        << "Combine must be exactly additive on sampleCount";
}

TEST(ReservoirMirror, TemporalCombineStaysUnbiasedOverManyRuns) {
    SyntheticCut cut = MakeSyntheticCut(12, /*seed=*/7);  // SAME cut as identity 1's convergence test
    const double trueSum = cut.BruteForceMonteCarloEstimate();

    constexpr int kRuns = 20000;
    double accum = 0.0;
    std::mt19937 rngCur(1001), rngPrev(2002), rngCombine(3003);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    for (int run = 0; run < kRuns; ++run) {
        ReservoirState current = Reservoir::BuildFromUniformCandidates(
            cut.Count(), [&](uint32_t i) { return cut.PHat(i); }, rngCur);
        ReservoirState prevFrame = Reservoir::BuildFromUniformCandidates(
            cut.Count(), [&](uint32_t i) { return cut.PHat(i); }, rngPrev);
        ASSERT_TRUE(current.IsValid());
        ASSERT_TRUE(prevFrame.IsValid());

        const float pHatAtPrevY = cut.PHat(prevFrame.y);  // re-evaluated "this frame"
        Reservoir::Combine(current, prevFrame, pHatAtPrevY, unit(rngCombine));

        const float w = Reservoir::UnbiasedWeight(current);
        accum += static_cast<double>(current.targetPdf) * static_cast<double>(w);
    }
    const double estimate = accum / static_cast<double>(kRuns);

    const double relError = std::fabs(estimate - trueSum) / trueSum;
    EXPECT_LT(relError, 0.02)
        << "Post-combine RIS estimate (" << estimate << ") over " << kRuns
        << " runs must STILL converge to the true candidate-power sum (" << trueSum
        << ") within tolerance (relative error " << relError << ") -- the combine must "
        << "not introduce bias vs identity 1's single-reservoir baseline";
}

// ===========================================================================
// Sampled Lighting Inc3 M5: spatial reuse calls the SAME Reservoir::Combine primitive as
// temporal reuse above (Reservoir::Combine has no notion of "temporal" vs "spatial", only
// "another reservoir stream to fold in" — see Reservoir.h's own Combine doc comment and
// shaders/ReservoirCombine.glsl's mirror-file header). These tests model spatial reuse as
// combining SEVERAL independently-built neighbor reservoirs (all drawn from the SAME
// synthetic cut, i.e. no scene discontinuity across the simulated neighborhood) into one
// pixel's reservoir, proving (a) chaining several Combine calls still reduces variance vs
// a single un-combined reservoir, and (b) the chain stays unbiased over many runs — the
// plan's "variance reduction vs M4" and "equal-error-vs-brute-force" gates' CPU-mirror
// precondition, run BEFORE trusting the GPU spatial-reuse path (same red-green discipline
// as the temporal identity above).
// ===========================================================================

TEST(ReservoirMirror, SpatialCombineReducesAcrossPixelVariance) {
    SyntheticCut cut = MakeSyntheticCut(16, /*seed=*/9);
    const double trueSum = cut.BruteForceMonteCarloEstimate();

    constexpr int kNeighbors = 4;     // mirrors ReservoirConfig::spatialCount default
    constexpr int kRuns = 4000;
    std::mt19937 rngBuild(4004), rngCombine(5005);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    auto estimateFrom = [&](int neighborsToCombine) {
        double sumSq = 0.0, sum = 0.0;
        for (int run = 0; run < kRuns; ++run) {
            ReservoirState current = Reservoir::BuildFromUniformCandidates(
                cut.Count(), [&](uint32_t i) { return cut.PHat(i); }, rngBuild);
            EXPECT_TRUE(current.IsValid());

            for (int n = 0; n < neighborsToCombine; ++n) {
                ReservoirState neighbor = Reservoir::BuildFromUniformCandidates(
                    cut.Count(), [&](uint32_t i) { return cut.PHat(i); }, rngBuild);
                EXPECT_TRUE(neighbor.IsValid());
                const float pHatAtNeighborY = cut.PHat(neighbor.y);
                Reservoir::Combine(current, neighbor, pHatAtNeighborY, unit(rngCombine));
            }

            const double sample = static_cast<double>(current.targetPdf) *
                                   static_cast<double>(Reservoir::UnbiasedWeight(current));
            sum += sample;
            sumSq += sample * sample;
        }
        const double mean = sum / kRuns;
        const double variance = (sumSq / kRuns) - (mean * mean);
        return std::make_pair(mean, variance);
    };

    const auto [meanNoReuse, varNoReuse] = estimateFrom(/*neighborsToCombine=*/0);
    const auto [meanSpatial, varSpatial] = estimateFrom(/*neighborsToCombine=*/kNeighbors);

    EXPECT_LT(varSpatial, varNoReuse)
        << "combining " << kNeighbors << " neighbor reservoirs must reduce per-pixel "
        << "estimator variance vs a single reservoir (no-reuse variance=" << varNoReuse
        << ", spatial-reuse variance=" << varSpatial << ")";

    // Both estimators must still be unbiased (variance reduction must not come at the cost
    // of a shifted mean) -- loose tolerance since this reuses the identity-2 check, not a
    // tight statistical-power claim.
    EXPECT_LT(std::fabs(meanNoReuse - trueSum) / trueSum, 0.05);
    EXPECT_LT(std::fabs(meanSpatial - trueSum) / trueSum, 0.05);
}

TEST(ReservoirMirror, ChainedSpatialCombineStaysUnbiasedOverManyRuns) {
    SyntheticCut cut = MakeSyntheticCut(12, /*seed=*/7);  // SAME cut as the temporal identity
    const double trueSum = cut.BruteForceMonteCarloEstimate();

    constexpr int kNeighbors = 4;
    constexpr int kRuns = 20000;
    double accum = 0.0;
    std::mt19937 rngBuild(6006), rngCombine(7007);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    for (int run = 0; run < kRuns; ++run) {
        ReservoirState current = Reservoir::BuildFromUniformCandidates(
            cut.Count(), [&](uint32_t i) { return cut.PHat(i); }, rngBuild);
        ASSERT_TRUE(current.IsValid());

        for (int n = 0; n < kNeighbors; ++n) {
            ReservoirState neighbor = Reservoir::BuildFromUniformCandidates(
                cut.Count(), [&](uint32_t i) { return cut.PHat(i); }, rngBuild);
            ASSERT_TRUE(neighbor.IsValid());
            const float pHatAtNeighborY = cut.PHat(neighbor.y);
            Reservoir::Combine(current, neighbor, pHatAtNeighborY, unit(rngCombine));
        }

        const float w = Reservoir::UnbiasedWeight(current);
        accum += static_cast<double>(current.targetPdf) * static_cast<double>(w);
    }
    const double estimate = accum / static_cast<double>(kRuns);
    const double relError = std::fabs(estimate - trueSum) / trueSum;
    EXPECT_LT(relError, 0.02)
        << "chained " << kNeighbors << "-neighbor spatial-combine estimate (" << estimate
        << ") over " << kRuns << " runs must converge to the true candidate-power sum ("
        << trueSum << ") within tolerance (relative error " << relError << ")";
}

// ===========================================================================
// Identity 3: a many-sample RIS reservoir converges NUMERICALLY to an INDEPENDENT
// brute-force Monte-Carlo estimate of the same integral (NOT a circular check against
// RIS/WRS code — SyntheticCut::BruteForceMonteCarloEstimate is a direct, closed-form sum
// over the candidate set, sharing no code with Reservoir::Update/BuildFromUniformCandidates).
// ===========================================================================

TEST(ReservoirMirror, RisConvergesToBruteForceMonteCarloEstimate_SmallCut) {
    SyntheticCut cut = MakeSyntheticCut(8, /*seed=*/17);
    const double trueSum = cut.BruteForceMonteCarloEstimate();

    constexpr int kRuns = 50000;
    double accum = 0.0;
    std::mt19937 rng(555);
    for (int run = 0; run < kRuns; ++run) {
        ReservoirState r = Reservoir::BuildFromUniformCandidates(
            cut.Count(), [&](uint32_t i) { return cut.PHat(i); }, rng);
        ASSERT_TRUE(r.IsValid());
        accum += static_cast<double>(r.targetPdf) * static_cast<double>(Reservoir::UnbiasedWeight(r));
    }
    const double estimate = accum / static_cast<double>(kRuns);
    const double relError = std::fabs(estimate - trueSum) / trueSum;
    EXPECT_LT(relError, 0.015)
        << "small-cut RIS estimate (" << estimate << ") vs independent brute-force ("
        << trueSum << "), relative error " << relError;
}

TEST(ReservoirMirror, RisConvergesToBruteForceMonteCarloEstimate_LargeCut) {
    // A larger candidate set (closer to a real light-tree cut's node count, see
    // test_light_tree.cpp's own coarse-cut sizes) — the convergence identity must hold
    // regardless of cut size, not just a toy 8-candidate case.
    SyntheticCut cut = MakeSyntheticCut(40, /*seed=*/23);
    const double trueSum = cut.BruteForceMonteCarloEstimate();

    constexpr int kRuns = 50000;
    double accum = 0.0;
    std::mt19937 rng(777);
    for (int run = 0; run < kRuns; ++run) {
        ReservoirState r = Reservoir::BuildFromUniformCandidates(
            cut.Count(), [&](uint32_t i) { return cut.PHat(i); }, rng);
        ASSERT_TRUE(r.IsValid());
        accum += static_cast<double>(r.targetPdf) * static_cast<double>(Reservoir::UnbiasedWeight(r));
    }
    const double estimate = accum / static_cast<double>(kRuns);
    const double relError = std::fabs(estimate - trueSum) / trueSum;
    EXPECT_LT(relError, 0.02)
        << "large-cut RIS estimate (" << estimate << ") vs independent brute-force ("
        << trueSum << "), relative error " << relError;
}

// ---------------------------------------------------------------------------
// Structural sanity: an empty candidate set / zero-sourcePdf update must not produce
// a false-valid reservoir (the escape-hatch precondition Combine/UnbiasedWeight rely on).
// ---------------------------------------------------------------------------
TEST(ReservoirMirror, EmptyReservoirIsInvalidAndContributesNothing) {
    ReservoirState r{};
    EXPECT_FALSE(r.IsValid());
    EXPECT_FLOAT_EQ(Reservoir::UnbiasedWeight(r), 0.0f);
}

TEST(ReservoirMirror, DegenerateSourcePdfIsRejectedNotCrashed) {
    ReservoirState r{};
    Reservoir::Update(r, /*candidateIndex=*/0, /*pHat=*/5.0f, /*sourcePdf=*/0.0f, /*u=*/0.5f);
    EXPECT_FALSE(r.IsValid()) << "a zero source pdf must not corrupt the reservoir into a false-valid state";
}
