#pragma once

#include "Selection/SelectionId.h"
#include <string>

namespace Vixen::RenderGraph {

/**
 * @brief Lightweight result identity + metadata for a selectable hit.
 *
 * IMPORTANT — this is NOT a base class every selectable object inherits.
 * Selection is provider-owned (see Selection-System-Design Decision 1): each
 * ISelectionProvider hit-tests its own domain (voxel = GPU readback, UI = rect
 * test, mesh = ray test). We do NOT instantiate one of these per voxel; this is
 * just the small descriptor a provider/coordinator can hand to consumers to
 * describe WHAT was hit — its stable id plus an optional human-readable name.
 *
 * Kept deliberately minimal. (Optional bounds / richer metadata can be added
 * later if a consumer needs it; the design lists bounds as optional.)
 */
struct Selectable {
    SelectionId id;     ///< Stable, domain-tagged identity of the hit.
    std::string name;   ///< Optional display name ("" when not provided).
};

} // namespace Vixen::RenderGraph
