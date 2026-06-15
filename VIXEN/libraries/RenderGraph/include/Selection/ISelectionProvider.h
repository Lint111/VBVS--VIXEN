#pragma once

#include "Selection/SelectionId.h"
#include "Selection/SelectContext.h"
#include <optional>

namespace Vixen::RenderGraph {

/**
 * @brief A domain owner that resolves a selection query to its candidate hit.
 *
 * Engine-native, pure-C++ interface (NO external dependencies). Each provider
 * owns ONE domain and knows how to hit-test it from a SelectContext:
 *   - VoxelSelectionProvider → GPU ID-buffer readback              → pickID
 *   - UiSelectionProvider    → rect/hit-test at screenPoint        → UI element
 *   - Mesh / custom          → ray test / app-defined              → entity / ...
 *
 * Providers are registered with the SelectionCoordinator (next phase). The
 * coordinator queries them in priority() order (highest first — UI occludes the
 * world), taking the first/nearest Hit. Providers are stateless w.r.t. the
 * SelectionSet; they only report what is under the query.
 */
class ISelectionProvider {
public:
    virtual ~ISelectionProvider() = default;

    /**
     * @brief Hit-test this provider's domain against the query.
     * @param ctx The selection query (screen point, viewport, camera, ...).
     *            `ctx.hit` is the coordinator's output and should be ignored here.
     * @return The nearest/topmost hit in this domain, or std::nullopt on a miss.
     */
    virtual std::optional<Hit> resolve(const SelectContext& ctx) = 0;

    /**
     * @brief Query priority — HIGHER is checked first and occludes lower layers.
     *
     * E.g. UI (high) is polled before voxels (low) so UI wins on overlap. When
     * two providers report comparable depth the coordinator may depth-sort, but
     * the simple contract is ordered-priority, first hit wins.
     */
    virtual int priority() const = 0;

    /**
     * @brief Which domain this provider owns (stamps the kind on its hits).
     */
    virtual ProviderKind kind() const = 0;
};

} // namespace Vixen::RenderGraph
