#pragma once

#include "Selection/SelectionId.h"
#include <glm/glm.hpp>
#include <cstdint>

namespace Vixen::RenderGraph {

/**
 * @brief A provider node's per-click selection result — the graph-passable hit.
 *
 * This is the value a *provider NODE* emits on its CANDIDATE output slot, and the
 * element type the SelectionCoordinatorNode gathers via its MultiConnect
 * PROVIDER_CANDIDATES accumulation slot (one candidate per wired provider per
 * click). Each provider node owns a domain (voxel = GPU ID-buffer readback, UI =
 * rect hit-test, mesh = ray test) and reports what is under the query as a
 * SelectionCandidate; the coordinator priority-resolves across all of them.
 *
 * It supersedes the old C++ `Hit` (returned by the deleted ISelectionProvider
 * abstraction): same payload, but now it crosses node boundaries, so it also
 * carries `hit` (a provider emits one every frame — `hit=false` off the click
 * edge / on a miss) and `priority` (the provider's layer, so the coordinator
 * resolves occlusion without holding a sorted provider list).
 *
 * Trivial/POD-ish (an id, two scalars, a vec3): cheap to copy through a slot.
 * Registered as a compile-time resource type (REGISTER_COMPILE_TIME_TYPE in
 * CompileTimeResourceSystem.h) so it — and std::vector<SelectionCandidate> for
 * the accumulation slot — are valid slot value types.
 */
struct SelectionCandidate {
    bool        hit;       ///< True iff this provider found something under the query this click.
    SelectionId id;        ///< Domain-tagged identity of the hit (meaningful only when hit==true).
    float       depth;     ///< View/ray depth of the hit — smaller = nearer (tie-break across providers).
    int         priority;  ///< Provider layer — HIGHER wins (UI occludes world). The coordinator's primary key.
    glm::vec3   worldPos;  ///< World-space position of the hit point (0 when not yet computed).
};

} // namespace Vixen::RenderGraph
