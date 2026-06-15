#pragma once

#include "Selection/SelectionId.h"
#include "Data/CameraData.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <optional>

namespace Vixen::RenderGraph {

/**
 * @brief A single resolved selection candidate from a provider.
 *
 * A provider's resolve() returns std::optional<Hit>: the nearest/topmost thing
 * it found under the query, or std::nullopt on a miss. The coordinator compares
 * Hits across providers (priority first, then depth) to pick the winner.
 */
struct Hit {
    SelectionId id;        ///< What was hit (domain-tagged identity).
    float       depth;     ///< View/ray depth of the hit — smaller = nearer (for cross-provider sorting).
    glm::vec3   worldPos;  ///< World-space position of the hit point.
};

// NOTE: SelectionModifier is defined in Selection/SelectionId.h (included above)
// so SelectionSet can use it without dragging in glm/CameraData via this header.

/**
 * @brief The single query object that flows through ALL selection logic.
 *
 * Built once per pick by the SelectionCoordinator and passed BY CONST REF to
 * each provider's resolve(). Providers read the screen point + viewport +
 * camera to build their world ray / unproject, hit-test their domain, and
 * return a Hit. The coordinator then fills `hit` with the winning candidate and
 * applies `modifier` to the SelectionSet.
 *
 * NOTE on screenPoint: today this is the crosshair / viewport CENTER (the
 * engine has no free cursor-release mode yet). When a cursor mode lands this
 * becomes the actual cursor position; the field/contract is unchanged.
 *
 * `camera` is a non-owning pointer (may be null only in degenerate/test paths);
 * providers that unproject must check it. CameraData supplies the inverse
 * view/projection used to build the ray.
 */
struct SelectContext {
    glm::vec2          screenPoint;     ///< Query point in pixels (crosshair/center today; see note).
    uint32_t           viewportWidth;   ///< Viewport width in pixels.
    uint32_t           viewportHeight;  ///< Viewport height in pixels.
    const CameraData*  camera;          ///< Non-owning; for unproject / ray build. May be null in tests.
    SelectionModifier  modifier;        ///< How the resulting hit combines with the set.
    int                button;          ///< Originating mouse button (MouseButton value).
    std::optional<Hit> hit;             ///< Winning candidate — filled BY THE COORDINATOR after polling providers.
};

} // namespace Vixen::RenderGraph
