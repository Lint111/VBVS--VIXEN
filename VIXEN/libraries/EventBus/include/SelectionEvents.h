#pragma once

#include "Message.h"
#include "SelectionId.h"
#include <cstdint>
#include <vector>

namespace Vixen::EventBus {

/**
 * @brief Selection-set changed event (generalizes PickResultEvent).
 *
 * Broadcast by the SelectionCoordinator whenever the durable SelectionSet
 * changes (after a modifier — Replace/Add/Toggle/Range — is applied). Unlike
 * the fire-and-forget per-pick PickResultEvent, this carries the WHOLE current
 * selection so consumers (highlight, UI, gameplay) can render/react to the new
 * state without tracking deltas themselves.
 *
 * `selection` is a snapshot of the set's ids at broadcast time. `primary` is
 * the most-recently affected id (the click target), or kInvalidSelectionId when
 * the selection became empty (e.g. Replace on a miss / clear). `count` mirrors
 * selection.size() for cheap inspection.
 *
 * SelectionId lives in this low-level library (see SelectionId.h) precisely so
 * this event can carry it without EventBus depending on RenderGraph.
 *
 * Ctor mirrors PickResultEvent's style: sender first, then payload.
 */
struct SelectionChangedEvent : public BaseEventMessage {
    static constexpr MessageType TYPE = AUTO_MESSAGE_TYPE();
    static constexpr EventCategory CATEGORY = EventCategory::ApplicationState;

    std::vector<SelectionId> selection;  ///< Snapshot of the full current selection set.
    SelectionId primary;                 ///< Most-recently affected id (kInvalidSelectionId if now empty).
    uint32_t count;                      ///< selection.size() (convenience mirror).

    SelectionChangedEvent(
        SenderID sender,
        std::vector<SelectionId> currentSelection,
        SelectionId primaryId = kInvalidSelectionId
    )
        : BaseEventMessage(CATEGORY, TYPE, sender)
        , selection(std::move(currentSelection))
        , primary(primaryId)
        , count(static_cast<uint32_t>(selection.size()))
    {}
};

} // namespace Vixen::EventBus
