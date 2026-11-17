#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace Vixen::RenderGraph {
// Conservative affine envelope over the producer's measured command history.
// Only a full tile can establish throughput. Clipped tiles and batch gaps add
// fixed latency; dividing them by tiny areas must not amplify subdivision.
class GpuChunkCost {
public:
    // Native-device untiling rule: a pass whose whole projected time fits `windowNs` runs as
    // one dispatch; a longer one is tiled by its average (not worst) full-tile cost.
    struct WholePass {
        uint64_t units;
        uint64_t windowNs;
        /// How the pass ran in its previous measured plan. A known shape adds hysteresis (R892.2): one
        /// dispatch stays one dispatch until it measures over the window by kHysteresis, and a tiled pass
        /// returns to one dispatch only when its projected one-dispatch time is under the window by the
        /// same margin. `None` (a cold launch, or a caller that keeps no state) is the plain window test.
        enum class Shape : uint8_t { None, Whole, Tiled } previous = Shape::None;
    };
    /// One measured launch scale: the mean and the slowest full tile of a frame at `units` workgroups.
    struct Scale {
        uint64_t units = 0;
        double meanNs = 0;
        double worstNs = 0;
    };
    /// R892.2 margin, shared by the whole-pass window and by the tile size a measured plan keeps.
    static constexpr double kHysteresis = 0.25;
    void Observe(uint64_t units, uint64_t fullTileUnits, double elapsedNs) {
        if (!units || elapsedNs <= 0) return;
        const double upper = elapsedNs * 1.1;
        const double expectedPerUnit = elapsedNs / units;
        if (units == fullTileUnits && units > anchorUnits_) {
            const double next = upper / units;
            fixedNs_ += std::max(0.0, unitNs_ - next) * anchorUnits_;
            unitNs_ = next;
            anchorUnits_ = units;
            meanUnitNs_ = expectedPerUnit;
            meanSamples_ = 1;
        } else if (units == anchorUnits_ || units == fullTileUnits) {
            // One dense tile sets the conservative max above; the mean tracks typical cost
            // without the safety factor. Safety belongs to Estimate/Edge, not prediction.
            meanSamples_ = std::min<uint32_t>(meanSamples_ + 1, kMeanWindow);
            meanUnitNs_ += (expectedPerUnit - meanUnitNs_) / meanSamples_;
            // A full tile at any launch scale measures throughput; a smaller full
            // tile that runs over budget must not be filed as fixed latency.
            unitNs_ = std::max(unitNs_, upper / units);
        } else {
            fixedNs_ = std::max(fixedNs_, upper - unitNs_ * units);
        }
    }
    void ObserveBatch(uint64_t units, uint64_t commands, double elapsedNs) {
        if (commands && elapsedNs > 0) {
            fixedNs_ = std::max(fixedNs_, (elapsedNs * 1.1 - unitNs_ * units) / commands);
            const double expectedFixed = std::max(0.0, elapsedNs - meanUnitNs_ * units) / commands;
            meanFixedSamples_ = std::min<uint32_t>(meanFixedSamples_ + 1, kMeanWindow);
            meanFixedNs_ += (expectedFixed - meanFixedNs_) / meanFixedSamples_;
        }
    }
    // R503/R888.1: the learned envelope is persisted per scene configuration and imported at start.
    //
    // The full tiles of one frame at one launch scale: their mean and their slowest. Two scales at least
    // 2x apart give the per-command latency, the part of a command's cost that does not shrink with its
    // size (launch, queue turnaround, and the tail of a tile that waits for its slowest workgroups). Kept:
    // the scale measured last and the last one at least 2x from it, so a pass refining down from a
    // coarse launch and a pass probing up from the floor both know the latency after one step, and the
    // pair follows the pass as its tiles change. A new scale within 2x of both points replaces neither
    // except the last one (it then remains 2x from the other).
    void ObserveScale(uint64_t units, double meanNs, double worstNs = 0.0) {
        if (!units || !(meanNs > 0)) return;
        const Scale next{units, meanNs, std::max(worstNs, meanNs)};
        const auto blend = [&](Scale& point) {
            point.meanNs = 0.7 * point.meanNs + 0.3 * next.meanNs;
            point.worstNs = 0.7 * point.worstNs + 0.3 * next.worstNs;
        };
        if (units == current_.units) {
            blend(current_);
        } else if (units == reference_.units) {
            blend(reference_);
            std::swap(current_, reference_);
        } else if (!current_.units || Apart(units, current_.units)) {
            reference_ = current_;
            current_ = next;
        } else if (!reference_.units || Apart(units, reference_.units)) {
            current_ = next;
        }
    }
    [[nodiscard]] bool ScalesMeasured() const {
        return current_.units && reference_.units && Apart(current_.units, reference_.units);
    }
    // Per-command latency, 0 until two scales >= 2x apart have been measured: the intercept of the
    // slowest full tile's cost against its size. The slowest tile, not the mean, because the command
    // that holds the queue longest is the one the budget bounds (R761); in an even pass both agree, in
    // an uneven one (the march) the mean hides the tail every tile pays for its slowest workgroups.
    [[nodiscard]] double LaunchLatencyNs() const {
        if (!ScalesMeasured()) return 0.0;
        const Scale& small = Small();
        const Scale& large = Large();
        const double slope = (large.worstNs - small.worstNs) / double(large.units - small.units);
        return std::max(0.0, small.worstNs - slope * double(small.units));
    }
    // Projected time of one untiled dispatch of `units`: the measurement itself once a dispatch of that
    // size ran; otherwise the line through the two measured scales' means, which counts the per-command
    // latency once (a sum of tile times counts it once a tile, and made a pass near the window flip
    // between one dispatch and hundreds of tiles); before two scales exist, the average full-tile cost.
    [[nodiscard]] double OneDispatchNs(uint64_t units) const {
        if (units == current_.units) return current_.meanNs;
        if (units == reference_.units) return reference_.meanNs;
        if (!ScalesMeasured()) return WholePassNs(units);
        const Scale& small = Small();
        const Scale& large = Large();
        const double slope = (large.meanNs - small.meanNs) / double(large.units - small.units);
        return std::max(0.0, small.meanNs + slope * (double(units) - double(small.units)));
    }
    // R761: tiles are sized from the mean, but the slowest tile is what holds the queue. On a native
    // device no tile may be projected (line through the two scales' slowest tiles) longer than a pass
    // is allowed to run as one dispatch (the window plus the hysteresis margin), so an uneven pass does
    // not trade its total for a submission near the guard.
    [[nodiscard]] double WorstTileEdge(const WholePass& pass, uint32_t floor) const {
        const Scale& small = Small();
        const Scale& large = Large();
        const double slope = (large.worstNs - small.worstNs) / double(large.units - small.units);
        if (!(slope > 0.0)) return double(std::numeric_limits<uint32_t>::max());
        const double limit = double(pass.windowNs) * (1.0 + kHysteresis) * 0.95;
        const double units = (limit - LaunchLatencyNs()) / slope;
        return std::max(double(floor), std::sqrt(std::max(0.0, units)));
    }
    // The whole-pass window test, with hysteresis once the previous shape is known.
    [[nodiscard]] bool FitsWindow(const WholePass& pass) const {
        const double margin = pass.previous == WholePass::Shape::Whole   ? 1.0 + kHysteresis
                            : pass.previous == WholePass::Shape::Tiled   ? 1.0 - kHysteresis
                                                                         : 1.0;
        return OneDispatchNs(pass.units) <= double(pass.windowNs) * margin;
    }
    // A budget the device cannot honour: even the cold-launch tile costs more. Splitting further only
    // multiplies the latency, so the budget is raised to kLatencyAmortization times the measured latency
    // (never lowered; capped well inside the device-timeout window). Devices whose latency is a small
    // fraction of the budget are untouched.
    [[nodiscard]] uint64_t SubmissionBudgetNs(uint64_t budgetNs) const {
        const double amortized = std::min(LaunchLatencyNs() * kLatencyAmortization, kMaxAmortizedBudgetNs);
        return std::max(budgetNs, static_cast<uint64_t>(amortized));
    }
    [[nodiscard]] bool Calibrated() const { return anchorUnits_ != 0; }
    [[nodiscard]] uint64_t AnchorUnits() const { return anchorUnits_; }
    [[nodiscard]] double UnitNs() const { return unitNs_; }
    [[nodiscard]] double FixedNs() const { return fixedNs_; }
    [[nodiscard]] double MeanUnitNs() const { return meanUnitNs_; }
    [[nodiscard]] double MeanFixedNs() const { return meanFixedNs_; }
    [[nodiscard]] const Scale& CurrentScale() const { return current_; }
    [[nodiscard]] const Scale& ReferenceScale() const { return reference_; }
    void Import(uint64_t anchorUnits, double unitNs, double fixedNs, double meanUnitNs = 0.0,
                double meanFixedNs = 0.0) {
        if (!anchorUnits || !(unitNs > 0) || !(fixedNs >= 0)) return;
        anchorUnits_ = anchorUnits;
        unitNs_ = unitNs;
        fixedNs_ = fixedNs;
        // Records written before the expected mean was persisted stored the 1.1 envelope here;
        // remove that decision margin once on import so prediction remains unbiased.
        meanUnitNs_ = meanUnitNs > 0 && meanUnitNs <= unitNs ? meanUnitNs : unitNs / 1.1;
        meanFixedNs_ = meanFixedNs >= 0 ? meanFixedNs : 0.0;
        meanSamples_ = 1;
        meanFixedSamples_ = meanFixedNs_ > 0 ? 1 : 0;
    }
    // The two measured scales of a persisted envelope; only a valid pair is taken (a partial record
    // re-measures them).
    void ImportScales(const Scale& current, const Scale& reference) {
        const auto valid = [](const Scale& p) { return p.units && p.meanNs > 0 && p.worstNs >= p.meanNs; };
        if (!valid(current) || !valid(reference) || !Apart(current.units, reference.units)) return;
        current_ = current;
        reference_ = reference;
    }
    // Projected time of one untiled dispatch of `units`, from the average full-tile cost.
    [[nodiscard]] double WholePassNs(uint64_t units) const {
        return anchorUnits_ ? meanFixedNs_ + meanUnitNs_ * units : 0.0;
    }
    // Issue-time mean used by QueueTimeline's retire predictor. A cold producer returns zero so
    // SubmitRecord can fall back to its conservative upper-bound entry.
    [[nodiscard]] uint64_t MeanEstimate(uint64_t units) const {
        return anchorUnits_ ? static_cast<uint64_t>(std::ceil(meanFixedNs_ + meanUnitNs_ * units)) : 0;
    }
    uint64_t Estimate(uint64_t units) const {
        return anchorUnits_ ? static_cast<uint64_t>(std::ceil(fixedNs_ + unitNs_ * units)) : 0;
    }
    // One rule for every device. `native` (set only for devices that untile) lets a pass whose whole
    // projected time fits the window run as one dispatch. Otherwise, and on translated devices, the
    // tile is sized from the average full-tile cost against the latency-raised submission budget.
    // `launch` is the edge of an unmeasured pass when the caller derives a coarser prior than the
    // floor (R861 cold split); 0 launches at the floor. It never lowers the floor and never
    // applies once a tile has been measured.
    uint32_t Edge(uint64_t requestedBudgetNs, uint32_t maximum, uint32_t cold,
                  const WholePass* native = nullptr, uint32_t launch = 0) const {
        const uint64_t budgetNs = SubmissionBudgetNs(requestedBudgetNs);
        // Never subdivide below the cold launch: every command carries a launch
        // floor, so below that scale more commands only add floors (Dozen collapsed
        // to 3,969 commands and lost the device). The submission gate still
        // reports any shortfall at the floor.
        const uint32_t floor = std::max(1u, std::min(maximum, cold));
        if (!anchorUnits_) return launch ? std::max(floor, std::min(maximum, launch)) : floor;
        if (native && FitsWindow(*native))
            return std::max(1u, maximum);
        // A measured fixed-latency floor cannot be repaired by splitting work.
        // Retain the measured launch scale instead of creating ever more commands;
        // the observed submission gate still reports an unattainable budget.
        // The full-tile observation already includes command latency. Extra
        // fixed allowance protects clipped commands and batch packing; subtracting
        // it again for subdivision makes a latency spike create more submissions.
        // Budget unattainable at the cold launch and fewer than two scales measured: probe the scale twice
        // the floor's edge (4x its area). From the floor launch this is the step up; from a coarse first
        // measurement it is the refinement whose pair gives the latency at once (ObserveScale).
        if (!ScalesMeasured() && unitNs_ * double(floor) * double(floor) > double(requestedBudgetNs))
            return std::min(maximum, 2 * floor);
        // Tiling is a placement decision, so it keeps the existing 1.1 conservative envelope at
        // this decision point. The stored mean remains unbiased for prediction/whole-pass timing;
        // it must not silently remove the correctness margin from the edge chosen here.
        const double conservativeMeanUnitNs = meanUnitNs_ * 1.1;
        const double throughputEdge = std::sqrt(budgetNs * 0.95 / conservativeMeanUnitNs);
        const double measuredEdge = std::sqrt(double(anchorUnits_));
        // With the latency measured, the budget already accounts for it: grow toward the budget, at
        // most kGrowth times the edge of the largest tile measured so far, so a coarser tile is reached
        // by measured steps, and on a native device never past the tile whose slowest instance would run
        // longer than one dispatch of a whole pass may (WorstTileEdge). Otherwise a latency floor must
        // not hide an over-budget full-tile observation.
        const double edge = ScalesMeasured()
            ? std::min({throughputEdge, kGrowth * measuredEdge, native ? WorstTileEdge(*native, floor) : throughputEdge})
            : fixedNs_ < budgetNs * 0.95 ? throughputEdge
            : std::min(throughputEdge, measuredEdge);
        return std::max(floor, static_cast<uint32_t>(std::min(double(maximum), edge)));
    }
private:
    // Samples averaged into meanUnitNs_: about one frame of full tiles.
    static constexpr uint32_t kMeanWindow = 64;
    static constexpr double kGrowth = 4.0;                   // edge step once the latency is measured
    static bool Apart(uint64_t a, uint64_t b) { return a >= 2 * b || b >= 2 * a; }
    const Scale& Small() const { return current_.units < reference_.units ? current_ : reference_; }
    const Scale& Large() const { return current_.units < reference_.units ? reference_ : current_; }
    static constexpr double kLatencyAmortization = 16.0;     // a command's size keeps latency under ~6%
    static constexpr double kMaxAmortizedBudgetNs = 200'000'000.0;
    uint64_t anchorUnits_ = 0;
    double unitNs_ = 0, fixedNs_ = 0, meanUnitNs_ = 0, meanFixedNs_ = 0;
    uint32_t meanSamples_ = 0;
    uint32_t meanFixedSamples_ = 0;
    Scale current_, reference_;
};
}
