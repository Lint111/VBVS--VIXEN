#include "Selection/SelectionSet.h"

namespace Vixen::RenderGraph {

void SelectionSet::apply(SelectionModifier modifier, SelectionId id) {
    switch (modifier) {
        case SelectionModifier::Replace:
            // Set becomes exactly {id}.
            m_ids.clear();
            m_ids.insert(id);
            break;

        case SelectionModifier::Add:
            // Accumulate (no-op if already present).
            m_ids.insert(id);
            break;

        case SelectionModifier::Toggle: {
            // Flip membership: present → remove, absent → insert.
            const auto it = m_ids.find(id);
            if (it != m_ids.end()) {
                m_ids.erase(it);
            } else {
                m_ids.insert(id);
            }
            break;
        }

        case SelectionModifier::Range:
            // TODO: true range/span selection needs an anchor + ordering and
            // belongs with the coordinator. Until then behave like Add so the
            // id is never silently dropped.
            m_ids.insert(id);
            break;
    }
}

bool SelectionSet::contains(SelectionId id) const {
    return m_ids.find(id) != m_ids.end();
}

void SelectionSet::clear() {
    m_ids.clear();
}

} // namespace Vixen::RenderGraph
