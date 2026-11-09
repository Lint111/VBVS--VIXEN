#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

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
    };
    void Observe(uint64_t units, uint64_t fullTileUnits, double elapsedNs) {
        if (!units || elapsedNs <= 0) return;
        const double upper = elapsedNs * 1.1;
        if (units == fullTileUnits && units > anchorUnits_) {
            const double next = upper / units;
            fixedNs_ += std::max(0.0, unitNs_ - next) * anchorUnits_;
            unitNs_ = next;
            anchorUnits_ = units;
            meanUnitNs_ = next;
            meanSamples_ = 1;
        } else if (units == anchorUnits_ || units == fullTileUnits) {
            // One dense tile sets the conservative max above; the mean tracks typical cost.
            meanSamples_ = std::min<uint32_t>(meanSamples_ + 1, kMeanWindow);
            meanUnitNs_ += (upper / units - meanUnitNs_) / meanSamples_;
            // A full tile at any launch scale measures throughput; a smaller full
            // tile that runs over budget must not be filed as fixed latency.
            unitNs_ = std::max(unitNs_, upper / units);
        } else {
            fixedNs_ = std::max(fixedNs_, upper - unitNs_ * units);
        }
    }
    void ObserveBatch(uint64_t units, uint64_t commands, double elapsedNs) {
        if (commands && elapsedNs > 0)
            fixedNs_ = std::max(fixedNs_, (elapsedNs * 1.1 - unitNs_ * units) / commands);
    }
    // R503: the learned envelope is persisted per device and extent through the
    // node's task profile, so a run starts warm. Plain values, no new store.
    // Mean cost of the full tiles of one frame at one launch scale. Two scales at least 2x apart give
    // the device's per-command latency (the intercept of cost vs. workgroups): the part of every
    // command that does not shrink with its size (launch, tail under-occupancy, queue turnaround).
    void ObserveScale(uint64_t units, double meanNs) {
        if (!units || meanNs <= 0) return;
        const auto blend = [&](Scale& point) {
            point.ns = point.units == units && point.ns > 0 ? 0.7 * point.ns + 0.3 * meanNs : meanNs;
            point.units = units;
        };
        if (!small_.units || units <= small_.units) blend(small_);
        else if (!large_.units || units >= large_.units) blend(large_);
    }
    // Per-command latency, 0 until two scales >= 2x apart have been measured.
    [[nodiscard]] double LaunchLatencyNs() const {
        if (!small_.units || large_.units < 2 * small_.units) return 0.0;
        const double slope = (large_.ns - small_.ns) / double(large_.units - small_.units);
        return std::max(0.0, small_.ns - slope * double(small_.units));
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
    void Import(uint64_t anchorUnits, double unitNs, double fixedNs, double meanUnitNs = 0.0) {
        if (!anchorUnits || !(unitNs > 0) || !(fixedNs >= 0)) return;
        anchorUnits_ = anchorUnits;
        unitNs_ = unitNs;
        fixedNs_ = fixedNs;
        // Records written before the mean was persisted fall back to the conservative cost.
        meanUnitNs_ = meanUnitNs > 0 && meanUnitNs <= unitNs ? meanUnitNs : unitNs;
        meanSamples_ = 1;
    }
    // Projected time of one untiled dispatch of `units`, from the average full-tile cost.
    [[nodiscard]] double WholePassNs(uint64_t units) const {
        return anchorUnits_ ? fixedNs_ + meanUnitNs_ * units : 0.0;
    }
    uint64_t Estimate(uint64_t units) const {
        return anchorUnits_ ? static_cast<uint64_t>(std::ceil(fixedNs_ + unitNs_ * units)) : 0;
    }
    // One rule for every device. `native` (set only for devices that untile) lets a pass whose whole
    // projected time fits the window run as one dispatch. Otherwise, and on translated devices, the
    // tile is sized from the average full-tile cost against the latency-raised submission budget.
    uint32_t Edge(uint64_t requestedBudgetNs, uint32_t maximum, uint32_t cold,
                  const WholePass* native = nullptr) const {
        const uint64_t budgetNs = SubmissionBudgetNs(requestedBudgetNs);
        // Never subdivide below the cold launch: every command carries a launch
        // floor, so below that scale more commands only add floors (Dozen collapsed
        // to 3,969 commands and lost the device). The submission gate still
        // reports any shortfall at the floor.
        const uint32_t floor = std::max(1u, std::min(maximum, cold));
        if (!anchorUnits_) return floor;
        if (native && WholePassNs(native->units) <= double(native->windowNs))
            return std::max(1u, maximum);
        // A measured fixed-latency floor cannot be repaired by splitting work.
        // Retain the measured launch scale instead of creating ever more commands;
        // the observed submission gate still reports an unattainable budget.
        // The full-tile observation already includes command latency. Extra
        // fixed allowance protects clipped commands and batch packing; subtracting
        // it again for subdivision makes a latency spike create more submissions.
        // Budget unattainable at the cold launch and no latency measured yet: probe one scale up (4x area)
        // so the next frames can tell latency from throughput.
        if (LaunchLatencyNs() == 0.0 && unitNs_ * double(floor) * double(floor) > double(requestedBudgetNs) &&
            large_.units < 2 * small_.units + 1)
            return std::min(maximum, 2 * floor);
        const double throughputEdge = std::sqrt(budgetNs * 0.95 / meanUnitNs_);
        // A latency floor must not hide an over-budget full-tile observation.
        const double edge = fixedNs_ < budgetNs * 0.95
            ? throughputEdge
            : std::min(throughputEdge, std::sqrt(double(anchorUnits_)));
        return std::max(floor, static_cast<uint32_t>(std::min(double(maximum), edge)));
    }
private:
    // Samples averaged into meanUnitNs_: about one frame of full tiles.
    static constexpr uint32_t kMeanWindow = 64;
    struct Scale { uint64_t units = 0; double ns = 0; };
    static constexpr double kLatencyAmortization = 16.0;     // a command's size keeps latency under ~6%
    static constexpr double kMaxAmortizedBudgetNs = 200'000'000.0;
    uint64_t anchorUnits_ = 0;
    double unitNs_ = 0, fixedNs_ = 0, meanUnitNs_ = 0;
    uint32_t meanSamples_ = 0;
    Scale small_, large_;
};
}
