#pragma once

#include "Selection/SelectionId.h"
#include <cstddef>
#include <unordered_set>

namespace Vixen::RenderGraph {

/**
 * @brief The durable set of currently-selected ids (engine-side state).
 *
 * Owned by the SelectionCoordinator (next phase). On each pick the coordinator
 * calls apply() with the input modifier and the winning hit's id; consumers
 * subscribe to SelectionChangedEvent to observe the result. Pure engine logic —
 * no Vulkan, no device, fully unit-testable.
 *
 * Backed by std::unordered_set<SelectionId> using the std::hash<SelectionId>
 * specialization (see SelectionId.h).
 */
class SelectionSet {
public:
    /**
     * @brief Combine a single id into the set per the given modifier.
     *
     * Semantics:
     *   - Replace : clear the set, then insert `id` (the set becomes exactly {id}).
     *   - Add     : insert `id` (existing members kept; no-op if already present).
     *   - Toggle  : if `id` is present remove it, otherwise insert it (flip).
     *   - Range   : treated as Add for now. TODO: real range/span selection needs
     *               an anchor + ordering and belongs with the coordinator; until
     *               then Range behaves like Add so it never silently drops the id.
     */
    void apply(SelectionModifier modifier, SelectionId id);

    /// @return true iff `id` is currently in the set.
    bool contains(SelectionId id) const;

    /// Remove all ids (set becomes empty).
    void clear();

    /// @return the underlying set of selected ids (read-only view).
    const std::unordered_set<SelectionId>& ids() const { return m_ids; }

    /// @return number of selected ids.
    std::size_t size() const { return m_ids.size(); }

    /// @return true iff nothing is selected.
    bool empty() const { return m_ids.empty(); }

private:
    std::unordered_set<SelectionId> m_ids;
};

} // namespace Vixen::RenderGraph
