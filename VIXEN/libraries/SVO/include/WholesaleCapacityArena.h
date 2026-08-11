#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace Vixen::SVO {

// CPU mirror of the S5 payload arena.  Ranges are stable until retired after
// the last frame that can reference them; a later admission reuses a retired
// range before growing the arena.  The renderer may use the same offsets when
// this ledger is backed by segmented Vulkan buffers.
class WholesaleCapacityArena {
public:
    struct Range { uint64_t offset = 0, size = 0; };

    Range Acquire(uint64_t size) {
        if (size == 0) return {};
        for (size_t i = 0; i < free_.size(); ++i) {
            if (free_[i].size < size) continue;
            Range out{free_[i].offset, size};
            if (free_[i].size == size) free_.erase(free_.begin() + static_cast<std::ptrdiff_t>(i));
            else { free_[i].offset += size; free_[i].size -= size; }
            return out;
        }
        const Range out{capacity_, size};
        capacity_ += size;
        return out;
    }

    void Retire(Range range, uint64_t safeFrame) {
        if (range.size == 0) return;
        pending_.push_back({range, safeFrame});
    }

    void Reclaim(uint64_t completedFrame) {
        for (size_t i = 0; i < pending_.size();) {
            if (pending_[i].safeFrame > completedFrame) { ++i; continue; }
            free_.push_back(pending_[i].range);
            pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(i));
        }
        std::sort(free_.begin(), free_.end(), [](Range a, Range b) { return a.offset < b.offset; });
        std::vector<Range> merged;
        for (Range r : free_) {
            if (!merged.empty() && merged.back().offset + merged.back().size == r.offset)
                merged.back().size += r.size;
            else merged.push_back(r);
        }
        free_.swap(merged);
    }

    [[nodiscard]] uint64_t AllocatedCapacityBytes() const { return capacity_; }
    [[nodiscard]] uint64_t ReusableBytes() const {
        uint64_t result = 0;
        for (Range r : free_) result += r.size;
        return result;
    }
    [[nodiscard]] size_t PendingRetirements() const { return pending_.size(); }

private:
    struct Pending { Range range; uint64_t safeFrame; };
    uint64_t capacity_ = 0;
    std::vector<Range> free_;
    std::vector<Pending> pending_;
};

} // namespace Vixen::SVO
