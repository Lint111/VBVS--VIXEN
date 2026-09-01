#pragma once

#include "TypedCacher.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string_view>
#include <vector>

namespace CashSystem {

// These layouts are shared by the CPU publisher and the bucketing/specialized shaders.
// Keeping them in the cacher's data contract prevents the upload path from defining a second,
// subtly different table layout.
struct RecipeBoundSphereGpu {
    float center[3] = {};
    float radius = 0.0f;
    float relaxation = 0.0f;
    float gateFootprintThreshold = 0.0f;
    float precisionFootprintThreshold = 0.0f;
    float _pad = 0.0f;
};
static_assert(sizeof(RecipeBoundSphereGpu) == 32, "RecipeBoundSphereGpu must be 32 bytes");

struct RecipeBucketMetaGpu {
    std::uint32_t memberCount = 0;
    std::uint32_t rectMinX = 0;
    std::uint32_t rectMinY = 0;
    float boundRadius = 0.0f;
    float stepRelaxation = 0.0f;
    std::uint32_t _pad[3] = {};
};
static_assert(sizeof(RecipeBucketMetaGpu) == 32, "RecipeBucketMetaGpu must be 32 bytes");

inline constexpr std::size_t kRecipeBucketTableCapacity = 256;
inline constexpr std::size_t kRecipeBucketMaskWordCapacity = 64;
inline constexpr std::size_t kRecipeBucketCpuMaskWordCount = 6;

struct RecipeBucketSnapshot {
    std::uint64_t registryGeneration = 0;
    std::uint64_t instanceGeneration = 0;
    std::array<RecipeBoundSphereGpu, kRecipeBucketTableCapacity> boundSpheres{};
    std::array<RecipeBucketMetaGpu, kRecipeBucketTableCapacity> bucketMeta{};
    std::array<std::uint32_t, kRecipeBucketMaskWordCapacity> instanceSkipMask{};
};

struct RecipeBucketCacheCreateInfo {
    std::uint64_t registryGeneration = 0;
    std::uint64_t instanceGeneration = 0;
    std::array<RecipeBoundSphereGpu, kRecipeBucketTableCapacity> boundSpheres{};
    std::array<RecipeBucketMetaGpu, kRecipeBucketTableCapacity> bucketMeta{};
    std::array<std::uint32_t, kRecipeBucketMaskWordCapacity> instanceSkipMask{};
};

struct RecipeBucketByteRange {
    std::size_t offset = 0;
    std::size_t size = 0;
};

struct RecipeBucketDirtyRanges {
    std::vector<RecipeBucketByteRange> boundSpheres;
    std::vector<RecipeBucketByteRange> bucketMeta;
    std::vector<RecipeBucketByteRange> instanceSkipMask;

    [[nodiscard]] bool Empty() const noexcept {
        return boundSpheres.empty() && bucketMeta.empty() && instanceSkipMask.empty();
    }
};

template<typename T, std::size_t N>
inline std::vector<RecipeBucketByteRange> ComputeRecipeBucketDirtyRanges(
    const std::array<T, N>& previous,
    const std::array<T, N>& current,
    std::size_t elementCount = N) {
    elementCount = elementCount < N ? elementCount : N;
    std::vector<RecipeBucketByteRange> ranges;
    std::size_t index = 0;
    while (index < elementCount) {
        if (std::memcmp(&previous[index], &current[index], sizeof(T)) == 0) {
            ++index;
            continue;
        }
        const std::size_t first = index++;
        while (index < elementCount &&
               std::memcmp(&previous[index], &current[index], sizeof(T)) != 0) {
            ++index;
        }
        ranges.push_back({first * sizeof(T), (index - first) * sizeof(T)});
    }
    return ranges;
}

inline RecipeBucketDirtyRanges ComputeRecipeBucketDirtyRanges(
    const RecipeBucketSnapshot& previous,
    const RecipeBucketSnapshot& current) {
    RecipeBucketDirtyRanges ranges;
    ranges.boundSpheres = ComputeRecipeBucketDirtyRanges(
        previous.boundSpheres, current.boundSpheres);
    ranges.bucketMeta = ComputeRecipeBucketDirtyRanges(
        previous.bucketMeta, current.bucketMeta);
    // The high mask words belong to the B1 camera-visibility writer. The CPU bucket publisher
    // owns only the low six words, so it must never clear or upload the rest of this table.
    ranges.instanceSkipMask = ComputeRecipeBucketDirtyRanges(
        previous.instanceSkipMask, current.instanceSkipMask,
        kRecipeBucketCpuMaskWordCount);
    return ranges;
}

/**
 * Generation-keyed CPU snapshot cacher for the three recipe-bucketing tables.
 *
 * This intentionally follows CashSystem's MainCacher/TypedCacher path. The generation pair is
 * the cache key; the fixed-size value contract makes the cached snapshot immutable from the
 * publisher's perspective and gives every frame slot the same canonical bytes to compare.
 */
class RecipeBucketDataCacher
    : public TypedCacher<RecipeBucketSnapshot, RecipeBucketCacheCreateInfo> {
public:
    RecipeBucketDataCacher() = default;
    ~RecipeBucketDataCacher() override = default;

    std::string_view name() const noexcept override { return "RecipeBucketDataCacher"; }

protected:
    PtrT Create(const RecipeBucketCacheCreateInfo& ci) override {
        auto snapshot = std::make_shared<RecipeBucketSnapshot>();
        snapshot->registryGeneration = ci.registryGeneration;
        snapshot->instanceGeneration = ci.instanceGeneration;
        snapshot->boundSpheres = ci.boundSpheres;
        snapshot->bucketMeta = ci.bucketMeta;
        snapshot->instanceSkipMask = ci.instanceSkipMask;
        return snapshot;
    }

    std::uint64_t ComputeKey(const RecipeBucketCacheCreateInfo& ci) const override {
        // A generation pair is the contract: registry changes affect bound spheres and instance
        // membership changes affect mask/meta. The payload is deliberately not a second key.
        std::uint64_t key = ci.registryGeneration + 0x9e3779b97f4a7c15ULL;
        key ^= ci.instanceGeneration + 0x9e3779b97f4a7c15ULL +
               (key << 6) + (key >> 2);
        return key;
    }
};

} // namespace CashSystem
