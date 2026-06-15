#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

// ============================================================================
// SelectionId — domain-tagged stable selection identity
// ============================================================================
//
// WHY THIS LIVES IN EventBus (and not in RenderGraph/Selection):
//   SelectionChangedEvent (a BaseEventMessage in this same library) carries
//   SelectionId. EventBus is the LOW-LEVEL library: RenderGraph depends on
//   EventBus, never the reverse (see CMake target_link_libraries — RenderGraph
//   PUBLIC-links EventBus; EventBus links only Threads + glm). Defining
//   SelectionId here lets the event carry it WITHOUT EventBus pulling in
//   RenderGraph — i.e. no dependency inversion. RenderGraph's Selection
//   headers simply `#include "SelectionId.h"` (EventBus exposes its include
//   dir PUBLICly) and re-export the type via `Vixen::SelectionId`.
//
// It is a tiny POD (a tag + a 64-bit payload): cheap to copy, comparable, and
// hashable so it can key an unordered_set in SelectionSet.
// ============================================================================

namespace Vixen {

/**
 * @brief Which selection domain an id belongs to.
 *
 * The coordinator runs providers in priority order; each provider owns one
 * domain and stamps its hits with the matching kind. The payload meaning is
 * domain-specific (see SelectionId::payload).
 */
enum class ProviderKind : uint8_t {
    Voxel = 0,  ///< GPU ID-buffer voxel pick   → payload = pickID
    Ui    = 1,  ///< UI element hit-test         → payload = element handle
    Mesh  = 2,  ///< 3D mesh ray test            → payload = entity id
    Custom = 3  ///< Application-defined provider → payload = provider-defined
};

/**
 * @brief Domain-tagged, stable, hashable selection identity.
 *
 * Identity = (kind, payload). Two ids are equal iff both fields match, so the
 * same numeric payload in different domains (e.g. voxel pickID 5 vs UI handle 5)
 * are distinct selections. Trivial/POD: safe to copy and store by value.
 */
struct SelectionId {
    ProviderKind kind;     ///< Which domain produced this id.
    uint64_t     payload;  ///< Domain-specific stable handle (pickID / element / entity / ...).

    friend constexpr bool operator==(const SelectionId& a, const SelectionId& b) noexcept {
        return a.kind == b.kind && a.payload == b.payload;
    }
    friend constexpr bool operator!=(const SelectionId& a, const SelectionId& b) noexcept {
        return !(a == b);
    }
};

/**
 * @brief The canonical "nothing selected / miss" id.
 *
 * Custom domain + payload 0. Providers return std::nullopt for a miss (they do
 * not emit this); this constant is for callers that need a sentinel value.
 */
inline constexpr SelectionId kInvalidSelectionId{ ProviderKind::Custom, 0ULL };

} // namespace Vixen

// ----------------------------------------------------------------------------
// std::hash specialization — combine kind + payload (FNV-style mix).
// Lets SelectionId key std::unordered_set / std::unordered_map (SelectionSet).
// ----------------------------------------------------------------------------
namespace std {
template <>
struct hash<::Vixen::SelectionId> {
    size_t operator()(const ::Vixen::SelectionId& id) const noexcept {
        // Mix the 8-bit kind into the 64-bit payload hash so different kinds
        // with the same payload land in different buckets.
        const size_t h1 = std::hash<uint64_t>{}(id.payload);
        const size_t h2 = std::hash<uint8_t>{}(static_cast<uint8_t>(id.kind));
        // Boost-style combine: h1 ^ (h2 + golden-ratio + shifts).
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
};
} // namespace std
