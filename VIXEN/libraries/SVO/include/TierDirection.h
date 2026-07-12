#pragma once
// TierDirection.h — Tiered ESVO Observer Addressing, Inc1 M2 (Task 3).
//
// Direction (and, via TierMath.h's linear-scale table, distance) between two
// TierAddress-identified points, composed ONLY through their shared-prefix
// ancestor (Tiered-ESVO-Observer-Addressing-Design-2026-07.md §4: "shared-
// prefix = shared ancestor"; §7 step 1: "compute direction
// normalize(objectLocal - observerLocal) composed only through the
// shared-prefix ancestor -- never a flattened world coordinate").
//
// Input shape (the actual design decision this file makes):
//
//   TierAddress only stores WHICH child octant/hop was taken at each tier
//   (TierAddress.h) -- it has no per-hop local-frame position. The design
//   doc's own per-hop payload for exactly this purpose is TierRef (§3.2):
//   `childOriginLocal[3]` (child tree's [1,2)-space origin, in the PARENT
//   tree's local [1,2) frame) + `childScale` (child's linear scale, in
//   parent-local units) -- "a single scale+offset [per hop], never a
//   flattened world matrix" (§3.3). Building TierRef/TierRefTable itself is
//   out of scope for this increment (plan §0), but this file's input shape
//   is deliberately compatible with that eventual data source: a
//   TierHopFrame below is exactly one TierRef's (origin, scale) pair, and a
//   caller composing a real traversal-restart chain later would fill an
//   array of these directly from resident TierRefTable entries instead of
//   the synthetic fixtures this milestone's tests construct by hand.
//
//   Concretely: ComposeLocalDirection consumes, for EACH address (observer
//   and object), a per-hop path of TierHopFrame covering only the hops
//   STRICTLY BELOW the shared prefix (the "divergent tail", length
//   depth - SharedPrefixLength). Each TierHopFrame is that hop's local
//   position within its own tier's [1,2) unit cube, plus the linear scale
//   (in centimeters, from TierMath's per-tier spanCm) that converts that
//   local frame into a real-world distance at the point the tail is
//   composed back up to the shared-ancestor frame. This keeps every
//   composition step a bounded [1,2)-frame scale+offset (§3.3's discipline)
//   -- composing K divergent hops is K well-conditioned local steps, never
//   an accumulated world transform, regardless of how large the absolute
//   tier scales get (galaxy-tier divergence is just as well-conditioned as
//   a same-tier sibling divergence).
//
// Scope note (Tiered-ESVO-Inc1-Plan-2026-07.md §0): pure CPU math, no
// dependency on ChildDescriptor, farBit, TierRef/TierRefTable itself,
// SVORebuild, or LaineKarrasOctree's traversal code. TierHopFrame here is a
// standalone synthetic-fixture input type for THIS milestone's tests, not a
// wired consumer of any real TierRefTable (that table does not exist yet).

#include <glm/glm.hpp>

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

#include "TierAddress.h"

// This header uses std::min. When included after <windows.h> (pulled in transitively on the
// Windows build via Vulkan/GTest), the `min`/`max` function-like macros mangle unqualified
// calls into a syntax error. NOMINMAX only helps before windows.h is seen, which a header
// cannot guarantee, so drop the macros outright — no C++ code wants them. Same convention as
// GpuTraversalMirror.h.
#undef min
#undef max

namespace Vixen::SVO {

// One hop's local-frame contribution to a composed direction/distance,
// shaped to match TierRef's (origin, scale) pair (design doc §3.2) so a
// later increment's real TierRefTable-backed traversal can feed this
// function without changing its signature.
struct TierHopFrame {
    // This hop's local position within its own tier's [1,2) unit-cube
    // frame (the same [1,2) convention LaineKarrasOctree's own traversal
    // frame uses -- NOT a normalized [0,1) or a world coordinate).
    glm::vec3 localPos{1.5f, 1.5f, 1.5f};

    // Linear scale of ONE UNIT of this hop's local frame, in centimeters
    // (i.e. how many cm one unit of localPos's [1,2) cube spans at this
    // tier -- typically a TierScaleRange::spanCm/leafCm value from
    // TierMath.h). This is what lets the composition convert a chain of
    // dimensionless local positions into a single real-world-scale
    // distance without ever materializing a flattened world coordinate.
    double scaleCm = 1.0;
};

// The result of composing two addresses' divergent tails through their
// shared-prefix ancestor: a normalized direction (object seen from
// observer) plus the real-world composed distance (centimeters) the
// direction was derived from. Distance is returned alongside direction
// because Task 4's magnitude falloff needs it, and because recomputing it
// from the direction alone would lose the magnitude (normalize() discards
// length) -- returning both avoids a second composition pass.
struct ComposedDirection {
    glm::vec3 direction{0.0f, 0.0f, 0.0f};  // normalized, object - observer
    double distanceCm = 0.0;                // ||object - observer||, centimeters
    bool valid = false;                     // false if observer == object (zero-length)
};

namespace detail {

// Sum a tail of hop frames into a single displacement, expressed in the
// shared-ancestor's local units: each hop's localPos is first re-centered
// on the [1,2) frame's own origin (subtracting the frame's center, 1.5, so
// a hop contributes a signed offset rather than an absolute [1,2)
// position), then scaled by that hop's own scaleCm and accumulated. This
// mirrors TierRef's "child origin expressed in the parent's local frame,
// converted by a single scale+offset" composition (§3.2/§3.3): each hop is
// one scale+offset step, summed rather than multiplied through, so no
// hop's error is amplified by a later hop's scale -- the well-conditioned
// property the design doc's precision discipline requires.
inline glm::dvec3 SumTail(std::span<const TierHopFrame> tail) {
    glm::dvec3 accum{0.0, 0.0, 0.0};
    for (const TierHopFrame& hop : tail) {
        const glm::dvec3 centered{
            static_cast<double>(hop.localPos.x) - 1.5,
            static_cast<double>(hop.localPos.y) - 1.5,
            static_cast<double>(hop.localPos.z) - 1.5,
        };
        accum += centered * hop.scaleCm;
    }
    return accum;
}

}  // namespace detail

// Composes a normalized direction (and the real-world distance it came
// from) from `observer` to `object`, walking ONLY the two addresses'
// divergent tails below their shared-prefix ancestor (TierAddress::
// SharedPrefixLength -- reused directly, not reimplemented).
//
// `observerTail`/`objectTail` must each have exactly
// (address.Depth() - sharedPrefixLen) entries, ordered shallowest-to-
// deepest (same order as TierAddress's own hops), giving the local-frame
// position/scale at each divergent hop. This is the synthetic per-hop
// input this milestone's tests construct by hand (see file header) -- a
// later increment sources it from a real TierRefTable instead.
inline ComposedDirection ComposeLocalDirection(
    const TierAddress& observer, std::span<const TierHopFrame> observerTail,
    const TierAddress& object, std::span<const TierHopFrame> objectTail) {
    const std::size_t sharedPrefixLen = TierAddress::SharedPrefixLength(observer, object);
    const std::size_t observerTailLen = observer.Depth() - sharedPrefixLen;
    const std::size_t objectTailLen = object.Depth() - sharedPrefixLen;

    // Defensive: a caller handing in the wrong tail length is a fixture bug,
    // not a runtime condition this function should silently paper over --
    // but since this is CPU-side test/authoring code (not a hot path), clamp
    // rather than throw so a slightly-too-long synthetic tail (e.g. a test
    // building both addresses' full paths and slicing) still composes using
    // only the divergent portion.
    const std::size_t observerUse = std::min(observerTailLen, observerTail.size());
    const std::size_t objectUse = std::min(objectTailLen, objectTail.size());

    // Both tails are composed into the SAME shared-ancestor local frame:
    // the observer's divergent displacement and the object's divergent
    // displacement are each a sum of well-conditioned local hops (never a
    // flattened world coordinate, §3.3), and the direction is simply their
    // difference in that common frame.
    const glm::dvec3 observerLocal = detail::SumTail(observerTail.first(observerUse));
    const glm::dvec3 objectLocal = detail::SumTail(objectTail.first(objectUse));

    const glm::dvec3 delta = objectLocal - observerLocal;
    const double distance = glm::length(delta);

    ComposedDirection result;
    result.distanceCm = distance;
    if (distance <= 0.0) {
        result.valid = false;
        return result;
    }
    const glm::dvec3 dir = delta / distance;
    result.direction = glm::vec3(static_cast<float>(dir.x), static_cast<float>(dir.y), static_cast<float>(dir.z));
    result.valid = true;
    return result;
}

}  // namespace Vixen::SVO
