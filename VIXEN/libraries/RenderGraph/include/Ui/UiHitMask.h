// UiHitMask.h — per-element UI hit-mask math (pure, engine-agnostic).
//
// The HUD `<body>` spans the viewport, so a naive "any element under the cursor is a UI hit"
// occludes the voxel pick on every click. The fix is layered (see UISelectionProviderNode):
//   1. `pointer-events: none` on read-only HUD areas → RmlUi skips them in GetElementAtPoint.
//   2. a `hit-mask` attribute on interactive elements → the provider runs THIS mask test; a miss
//      means the ray passes through (the voxel pick wins).
//
// This unit is the CORE of (2): it has NO RmlUi / Vulkan / GPU dependency. The provider maps the
// cursor to element-local coordinates (0..w, 0..h) and asks HitMaskContains() whether that pixel is
// a real hit for the element's shape. Pure float math + a small path-keyed alpha-mask cache (in the
// .cpp), so it is unit-testable headless.
//
// Authoring forms (the value of the RML `hit-mask` attribute), parsed by ParseHitMask():
//   "none" / "" / "box"      → Box        (rectangular; the AABB already passed, always a hit)
//   "ellipse"                → Ellipse    (inscribed ellipse)
//   "rounded-rect"           → RoundedRect with a default corner radius
//   "rounded-rect <px>"      → RoundedRect with an explicit corner radius in pixels
//   "url(path/to/mask.png)"  → Image      (sample the mask's alpha; alpha >= 128 is a hit)

#pragma once

#include <string>

namespace Vixen::RenderGraph {

/// The shape a `hit-mask` value selects.
enum class HitMaskShape {
    Box,          ///< Rectangular — always a hit (the element AABB already passed). Default.
    Ellipse,      ///< Ellipse inscribed in the element box.
    RoundedRect,  ///< Box minus four rounded corners (quarter-circles of `radius`).
    Image,        ///< Alpha mask sampled from an image at `imagePath`.
};

/// A sensible default corner radius (px) for `rounded-rect` when none is authored.
inline constexpr float kDefaultRoundedRectRadius = 8.0f;

/// The parsed `hit-mask` spec. Cheap to copy; no owned resources (the image is cached in the .cpp).
struct HitMaskSpec {
    HitMaskShape shape = HitMaskShape::Box;
    float        radius = 0.0f;   ///< RoundedRect corner radius (px). Ignored for other shapes.
    std::string  imagePath;       ///< Image-mask path (for HitMaskShape::Image). Empty otherwise.
};

/// Parse a `hit-mask` attribute value into a HitMaskSpec. Unrecognised input falls back to Box
/// (fail-open: an authoring typo never makes an interactive widget un-clickable). Tolerant of
/// surrounding whitespace and case for the keyword forms.
HitMaskSpec ParseHitMask(const std::string& attr);

/// Is the element-local pixel (localX, localY) a HIT for this mask, given the element's pixel size?
///
/// Coordinates are element-local: (0,0) is the element's top-left (border box), (width,height) its
/// bottom-right. The caller has already confirmed the cursor is within the element's AABB, so Box
/// returns true unconditionally; the other shapes carve the box down.
///
///   Box         : true.
///   Ellipse     : ((x-w/2)/(w/2))^2 + ((y-h/2)/(h/2))^2 <= 1.
///   RoundedRect : inside the box minus the four corner quarter-circles (radius clamped to
///                 min(radius, w/2, h/2)); the corner cut only applies in the corner quadrants.
///   Image       : sample the cached alpha mask at (x/w*imgW, y/h*imgH); hit if alpha >= 128. A
///                 failed/again-failing load is treated as Box (fail-open; logged once per path).
///
/// Degenerate sizes (width or height <= 0) return false. Out-of-[0,w]×[0,h] inputs return false
/// (defensive — the AABB pre-check should make that unreachable).
bool HitMaskContains(const HitMaskSpec& spec,
                     float localX, float localY,
                     float width, float height);

} // namespace Vixen::RenderGraph
