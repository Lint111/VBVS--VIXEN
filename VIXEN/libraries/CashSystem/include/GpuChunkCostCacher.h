#pragma once

#include "CacheCodec.h"
#include "CacheKeyHasher.h"
#include "TypedCacher.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace CashSystem {

/**
 * The measured linear envelope used by RenderGraph::TiledDispatch.
 *
 * This is deliberately a CashSystem resource rather than a task-profile field: dispatch cost
 * belongs to the complete scene configuration, not merely to a device and a workgroup extent.
 */
struct GpuChunkCostCalibration {
    std::uint64_t anchorUnits = 0;
    double unitNs = 0.0;
    double fixedNs = 0.0;
    double meanUnitNs = 0.0;
    double meanFixedNs = 0.0;
    // R892.2: the two launch scales that measured the per-command latency (mean and slowest full
    // tile of each), so a warm start sizes its tiles from frame one. Zero: not measured.
    std::uint64_t currentUnits = 0;
    double currentMeanNs = 0.0;
    double currentWorstNs = 0.0;
    std::uint64_t referenceUnits = 0;
    double referenceMeanNs = 0.0;
    double referenceWorstNs = 0.0;
};

/**
 * Identity of one learned dispatch envelope.
 *
 * The key is scene identity, pass identity, the complete dispatch extent, the selected render
 * quality/profile, and the physical device. Calibration values are payload only and are never
 * part of the key.
 */
struct GpuChunkCostCacheCreateInfo {
    std::string sceneIdentity;
    std::string passIdentity;
    std::uint32_t extentX = 0;
    std::uint32_t extentY = 0;
    std::uint32_t extentZ = 0;
    std::string qualityProfile;
    std::uint64_t deviceIdentity = 0;
    GpuChunkCostCalibration calibration;
};

[[nodiscard]] inline std::uint64_t ComputeGpuChunkCostCacheKey(
    const GpuChunkCostCacheCreateInfo& ci) {
    CacheKeyHasher hasher;
    hasher.Add(std::string_view(ci.sceneIdentity));
    hasher.Add(std::string_view(ci.passIdentity));
    hasher.Add(ci.extentX).Add(ci.extentY).Add(ci.extentZ);
    hasher.Add(std::string_view(ci.qualityProfile));
    hasher.Add(ci.deviceIdentity);
    return hasher.Finalize();
}

/**
 * Device-scoped CashSystem cacher for per-scene GPU dispatch envelopes.
 *
 * The cacher owns persistence through the normal CacherBase envelope and MainCacher device
 * registry. It has no Vulkan resource to destroy; its device dependency exists to keep one
 * device's measurements from being reused on another adapter.
 */
class GpuChunkCostCacher
    : public TypedCacher<GpuChunkCostCalibration, GpuChunkCostCacheCreateInfo> {
public:
    using Base = TypedCacher<GpuChunkCostCalibration, GpuChunkCostCacheCreateInfo>;
    using PtrT = typename Base::PtrT;

    std::string_view name() const noexcept override { return "GpuChunkCostCacher"; }

    [[nodiscard]] PtrT Lookup(const GpuChunkCostCacheCreateInfo& ci) {
        return std::static_pointer_cast<GpuChunkCostCalibration>(Get(ComputeKey(ci)));
    }

    void Store(const GpuChunkCostCacheCreateInfo& ci,
               const GpuChunkCostCalibration& calibration) {
        if (const PtrT existing = Lookup(ci)) {
            *existing = calibration;
            return;
        }
        GpuChunkCostCacheCreateInfo stored = ci;
        stored.calibration = calibration;
        (void)GetOrCreate(stored);
    }

    [[nodiscard]] static std::uint64_t KeyForTests(const GpuChunkCostCacheCreateInfo& ci) {
        return ComputeGpuChunkCostCacheKey(ci);
    }

protected:
    static constexpr std::uint32_t kVersion = 2;

    PtrT Create(const GpuChunkCostCacheCreateInfo& ci) override {
        return std::make_shared<GpuChunkCostCalibration>(ci.calibration);
    }

    std::uint64_t ComputeKey(const GpuChunkCostCacheCreateInfo& ci) const override {
        return ComputeGpuChunkCostCacheKey(ci);
    }

    bool SerializePayloadToFile(const std::filesystem::path& path) const override {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) return false;

        CacheWriter writer(out);
        writer.WritePod(kVersion);
        const auto entries = Snapshot();
        writer.WritePod(static_cast<std::uint64_t>(entries.size()));
        for (const auto& entry : entries) {
            const auto& ci = entry.ci;
            const auto& calibration = *entry.resource;
            writer.WriteString(ci.sceneIdentity);
            writer.WriteString(ci.passIdentity);
            writer.WritePod(ci.extentX);
            writer.WritePod(ci.extentY);
            writer.WritePod(ci.extentZ);
            writer.WriteString(ci.qualityProfile);
            writer.WritePod(ci.deviceIdentity);
            writer.WritePod(calibration.anchorUnits);
            writer.WritePod(calibration.unitNs);
            writer.WritePod(calibration.fixedNs);
            writer.WritePod(calibration.meanUnitNs);
            writer.WritePod(calibration.meanFixedNs);
            writer.WritePod(calibration.currentUnits);
            writer.WritePod(calibration.currentMeanNs);
            writer.WritePod(calibration.currentWorstNs);
            writer.WritePod(calibration.referenceUnits);
            writer.WritePod(calibration.referenceMeanNs);
            writer.WritePod(calibration.referenceWorstNs);
        }
        return writer.Ok();
    }

    bool DeserializePayloadFromFile(const std::filesystem::path& path, void* /*device*/) override {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;

        CacheReader reader(in);
        std::uint32_t version = 0;
        std::uint64_t count = 0;
        constexpr std::uint64_t kMaxEntries = 1u << 20;
        constexpr std::size_t kMaxIdentityLength = 4096;
        // Version 1 records carry no launch scales; they load with the scales unmeasured.
        if (!reader.ReadPod(version) || (version != kVersion && version != 1) ||
            !reader.ReadPod(count) || count > kMaxEntries) {
            return false;
        }

        for (std::uint64_t i = 0; i < count; ++i) {
            GpuChunkCostCacheCreateInfo ci;
            GpuChunkCostCalibration calibration;
            if (!reader.ReadString(ci.sceneIdentity, kMaxIdentityLength) ||
                !reader.ReadString(ci.passIdentity, kMaxIdentityLength) ||
                !reader.ReadPod(ci.extentX) || !reader.ReadPod(ci.extentY) ||
                !reader.ReadPod(ci.extentZ) ||
                !reader.ReadString(ci.qualityProfile, kMaxIdentityLength) ||
                !reader.ReadPod(ci.deviceIdentity) ||
                !reader.ReadPod(calibration.anchorUnits) ||
                !reader.ReadPod(calibration.unitNs) ||
                !reader.ReadPod(calibration.fixedNs) ||
                !reader.ReadPod(calibration.meanUnitNs) ||
                !reader.ReadPod(calibration.meanFixedNs)) {
                return false;
            }
            if (version >= 2 &&
                (!reader.ReadPod(calibration.currentUnits) || !reader.ReadPod(calibration.currentMeanNs) ||
                 !reader.ReadPod(calibration.currentWorstNs) || !reader.ReadPod(calibration.referenceUnits) ||
                 !reader.ReadPod(calibration.referenceMeanNs) || !reader.ReadPod(calibration.referenceWorstNs))) {
                return false;
            }
            ci.calibration = calibration;
            Publish(ComputeKey(ci), ci, std::make_shared<GpuChunkCostCalibration>(calibration));
        }
        return reader.Ok();
    }
};

} // namespace CashSystem
