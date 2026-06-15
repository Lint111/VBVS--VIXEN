#pragma once

// ============================================================================
// Selection/SelectionId.h — RenderGraph-side access to the selection identity.
// ============================================================================
//
// The CANONICAL definition of SelectionId / ProviderKind / kInvalidSelectionId
// lives in the EventBus library (libraries/EventBus/include/SelectionId.h),
// NOT here. Reason (dependency direction): SelectionChangedEvent — a
// BaseEventMessage — must carry SelectionId, and EventBus is the low-level lib
// that RenderGraph depends on (never the reverse). Putting the type in EventBus
// lets the event carry it with NO RenderGraph→EventBus inversion.
//
// This header just pulls that definition in and re-exports it under the
// RenderGraph namespace so Selection code can write `SelectionId` naturally.
// ============================================================================

// Angle brackets (NOT quotes) are deliberate: this RenderGraph header is ALSO
// named SelectionId.h, so a quoted include would resolve to THIS file (MSVC/GCC
// search the includer's own directory first for quoted includes) and the
// EventBus definition would never be seen. Angle brackets skip the includer's
// dir and resolve via the include path to EventBus's SelectionId.h (its include
// dir is PUBLIC on the RenderGraph target).
#include <SelectionId.h>  // canonical definition, from the EventBus library
#include <cstdint>

namespace Vixen::RenderGraph {

// Re-export the canonical low-level selection types into the RenderGraph
// namespace. These are aliases — same type, usable interchangeably with the
// EventBus ones (e.g. when filling SelectionChangedEvent).
using ProviderKind = ::Vixen::ProviderKind;
using SelectionId  = ::Vixen::SelectionId;

inline constexpr SelectionId kInvalidSelectionId = ::Vixen::kInvalidSelectionId;

/**
 * @brief How a new hit combines with the existing SelectionSet.
 *
 * Driven by input modifiers (typically Shift/Ctrl/Alt). See SelectionSet::apply
 * for the exact semantics of each. Defined here (the lightweight identity
 * header) so SelectionSet and the SelectionCoordinatorNode can use it without
 * pulling in glm/CameraData.
 */
enum class SelectionModifier : uint8_t {
    Replace = 0,  ///< Clear the set, then select only this id (default click).
    Add     = 1,  ///< Add this id to the set (keep existing).
    Toggle  = 2,  ///< If present remove it, else add it.
    Range   = 3   ///< Range/span select (TODO: treated as Add for now).
};

} // namespace Vixen::RenderGraph
