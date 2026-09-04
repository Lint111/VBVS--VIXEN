#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace Vixen::SVO {

// A byte range in one of the two buffers that make up a residency admission.
// Keeping the range independent of Vulkan lets the producer compute partial
// invalidation before it chooses the upload backend.
struct BrickConfigByteRange {
    std::size_t offset = 0;
    std::size_t size = 0;
};

struct BrickConfigDirtyRanges {
    std::vector<BrickConfigByteRange> bricks;
    std::vector<BrickConfigByteRange> configs;

    [[nodiscard]] bool Empty() const noexcept {
        return bricks.empty() && configs.empty();
    }
};

// Tracks publication rather than ownership of the payload. A generation is
// submitted at most once; a later generation replaces the pending ranges.
// Ranges remain pending until the producer has successfully queued the ordered
// batch, so a failed staging reservation can be retried without losing dirt.
struct BrickConfigGenerationState {
    std::uint64_t generation = 0;
    std::uint64_t submittedGeneration = 0;
    BrickConfigDirtyRanges pending;

    void Stage(std::uint64_t newGeneration, BrickConfigDirtyRanges ranges) {
        if (generation == newGeneration) {
            return;
        }
        generation = newGeneration;
        pending = std::move(ranges);
    }

    [[nodiscard]] bool NeedsSubmission(std::uint64_t currentGeneration) const noexcept {
        return generation == currentGeneration && generation != submittedGeneration &&
               !pending.Empty();
    }

    void MarkSubmitted(std::uint64_t submitted) noexcept {
        if (generation == submitted) {
            submittedGeneration = submitted;
            pending = {};
        }
    }
};

} // namespace Vixen::SVO
