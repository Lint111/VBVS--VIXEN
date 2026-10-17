#pragma once

#include "ShellOctreeGpu.h"
#include <unordered_map>
#include <vector>

namespace Vixen::SVO {

// Producer-side payload: TKindRef is the source kind's generated Ref, copied
// from its row. generation is the store's slot generation, never a render index.
// The producer retains this typed identity; the GPU only needs correspondence.
template<class TKindRef>
struct BodyInstanceSource {
    TKindRef sourceRef;
    uint64_t generation;
    BodyInstanceGpu instance;
};

// One cache per producer/viewport, advanced once for each rendered frame. This
// stores previous values under supplied identities; it does not allocate IDs.
template<class TKindRef, class THash = std::hash<TKindRef>>
class InstanceMotionHistory {
public:
    std::vector<BodyInstanceGpu> ResolveFrame(const std::vector<BodyInstanceSource<TKindRef>>& rows) {
        std::unordered_map<TKindRef, Snapshot, THash> next;
        std::unordered_map<TKindRef, size_t, THash> occurrences;
        for (const auto& row : rows) ++occurrences[row.sourceRef];
        std::vector<BodyInstanceGpu> result;
        result.reserve(rows.size());
        for (const auto& row : rows) {
            auto instance = row.instance;
            instance.motion.valid = 0u;
            instance.motion.previousLocalToWorld = ToMat4(instance.transform.localToWorld);
            const auto previous = previous_.find(row.sourceRef);
            const bool unique = occurrences[row.sourceRef] == 1u;
            if (unique && previous != previous_.end() &&
                previous->second.generation == row.generation &&
                std::memcmp(&previous->second.material, &instance.material, sizeof(instance.material)) == 0) {
                instance.motion.previousLocalToWorld = ToMat4(previous->second.localToWorld);
                instance.motion.valid = 1u;
            }
            if (unique) next.emplace(row.sourceRef, Snapshot{
                row.generation, instance.transform.localToWorld, instance.material});
            result.push_back(instance);
        }
        previous_ = std::move(next); // Deleted identities cannot regain stale correspondence.
        return result;
    }

    void Reset() { previous_.clear(); }

private:
    struct Snapshot {
        uint64_t generation;
        Affine3x4Gpu localToWorld;
        BodyInstanceMaterialGpu material;
    };
    std::unordered_map<TKindRef, Snapshot, THash> previous_;
};

} // namespace Vixen::SVO
