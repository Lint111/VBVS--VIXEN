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
    [[nodiscard]] bool Calibrated() const { return anchorUnits_ != 0; }
    [[nodiscard]] uint64_t AnchorUnits() const { return anchorUnits_; }
    [[nodiscard]] double UnitNs() const { return unitNs_; }
    [[nodiscard]] double FixedNs() const { return fixedNs_; }
    void Import(uint64_t anchorUnits, double unitNs, double fixedNs) {
        if (!anchorUnits || !(unitNs > 0) || !(fixedNs >= 0)) return;
        anchorUnits_ = anchorUnits;
        unitNs_ = unitNs;
        fixedNs_ = fixedNs;
        meanUnitNs_ = unitNs;  // the persisted record keeps only the conservative cost
        meanSamples_ = 1;
    }
    // Projected time of one untiled dispatch of `units`, from the average full-tile cost.
    [[nodiscard]] double WholePassNs(uint64_t units) const {
        return anchorUnits_ ? fixedNs_ + meanUnitNs_ * units : 0.0;
    }
    // Issue-time mean used by QueueTimeline's retire predictor. A cold producer returns zero so
    // SubmitRecord can fall back to its conservative upper-bound entry.
    [[nodiscard]] uint64_t MeanEstimate(uint64_t units) const {
        return anchorUnits_ ? static_cast<uint64_t>(std::ceil(fixedNs_ + meanUnitNs_ * units)) : 0;
    }
    uint64_t Estimate(uint64_t units) const {
        return anchorUnits_ ? static_cast<uint64_t>(std::ceil(fixedNs_ + unitNs_ * units)) : 0;
    }
    uint32_t Edge(uint64_t budgetNs, uint32_t maximum, uint32_t cold,
                  const WholePass* native = nullptr) const {
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
        const double throughputEdge = std::sqrt(budgetNs * 0.95 / (native ? meanUnitNs_ : unitNs_));
        // A latency floor must not hide an over-budget full-tile observation.
        const double edge = fixedNs_ < budgetNs * 0.95
            ? throughputEdge
            : std::min(throughputEdge, std::sqrt(double(anchorUnits_)));
        return std::max(floor, static_cast<uint32_t>(std::min(double(maximum), edge)));
    }
private:
    // Samples averaged into meanUnitNs_: about one frame of full tiles.
    static constexpr uint32_t kMeanWindow = 64;
    uint64_t anchorUnits_ = 0;
    double unitNs_ = 0, fixedNs_ = 0, meanUnitNs_ = 0;
    uint32_t meanSamples_ = 0;
};
}
