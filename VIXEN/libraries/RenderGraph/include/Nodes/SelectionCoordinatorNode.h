#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/NodeLogging.h"
#include "Data/Nodes/SelectionCoordinatorNodeConfig.h"
#include "Selection/SelectionSet.h"
#include <cstdint>
#include <memory>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for the engine-wide selection coordinator (SEL-P2).
 */
class SelectionCoordinatorNodeType : public TypedNodeType<SelectionCoordinatorNodeConfig> {
public:
    SelectionCoordinatorNodeType(const std::string& typeName = "SelectionCoordinator")
        : TypedNodeType<SelectionCoordinatorNodeConfig>(typeName) {}
    ~SelectionCoordinatorNodeType() override = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName
    ) const override;
};

/**
 * @brief Engine-wide selection coordinator (SEL-P2) — providers are NODES feeding it.
 *
 * The single source of truth for selection. It owns a SelectionSet (engine-side durable
 * state) and gathers candidates from provider NODES via a MultiConnect accumulation slot
 * (PROVIDER_CANDIDATES). It no longer owns providers or does any GPU work — each provider
 * node (VoxelSelectionProviderNode, a future UI provider node, …) resolves its own domain
 * on a click and emits a SelectionCandidate into that one slot.
 *
 * On a left-mouse-button PRESS edge (edge-detected from INPUT_STATE) it:
 *   1. gathers the accumulated candidates and keeps the ones that hit;
 *   2. picks the best — MAX priority, tie-break MIN depth (UI occludes the world);
 *   3. reads a SelectionModifier from the input modifier keys (Shift→Add, Ctrl→Toggle,
 *      else Replace) and applies it to the SelectionSet (Replace-on-miss clears);
 *   4. broadcasts a SelectionChangedEvent (snapshot of the set) on the message bus.
 *
 * Per-frame cost off the click edge is a couple of reads and an edge comparison.
 */
class SelectionCoordinatorNode : public TypedNode<SelectionCoordinatorNodeConfig> {
public:
    SelectionCoordinatorNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    ~SelectionCoordinatorNode() override = default;

protected:
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    // ----- Selection state (the node is the single source of truth) -----
    SelectionSet set_;  ///< The durable selection set this node owns.

    // Status mirrored to the SELECTION_COUNT output (size of the set after the last pick).
    uint32_t selectionCount_ = 0;
};

} // namespace Vixen::RenderGraph
