#pragma once

#include "Selection/SelectionCandidate.h"
#include <vector>

namespace Vixen::RenderGraph {

/**
 * @brief Pick the winning provider candidate from the MultiConnect fan-in.
 *
 * The cross-provider occlusion rule used by the SelectionCoordinatorNode: among the
 * candidates that HIT, the winner is the one with the highest `priority` (UI occludes
 * world); ties are broken by the smallest `depth` (nearer wins). Candidates with
 * `hit == false` (a provider off the click edge, or a miss) are ignored.
 *
 * Pure, allocation-free, and the single source of truth for the resolution rule — the
 * coordinator calls this, and it is unit-tested directly (so the rule is verified
 * without a graph/device).
 *
 * @return pointer to the winning candidate within `candidates`, or nullptr if none hit.
 *         The pointer is valid for the lifetime of the passed container.
 */
inline const SelectionCandidate* pickBestCandidate(const std::vector<SelectionCandidate>& candidates) {
    const SelectionCandidate* best = nullptr;
    for (const SelectionCandidate& c : candidates) {
        if (!c.hit) {
            continue;
        }
        if (!best ||
            c.priority > best->priority ||
            (c.priority == best->priority && c.depth < best->depth)) {
            best = &c;
        }
    }
    return best;
}

} // namespace Vixen::RenderGraph
