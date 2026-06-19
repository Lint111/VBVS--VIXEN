#include "Nodes/SelectionCoordinatorNode.h"
#include "Core/NodeRegistration.h"
#include "Core/NodeLogging.h"
#include "Selection/SelectionCandidate.h"
#include "Selection/SelectionResolve.h"
#include "InputEvents.h"
#include "SelectionEvents.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Vixen::RenderGraph {

// ============================================================================
// NODE TYPE FACTORY
// ============================================================================

std::unique_ptr<NodeInstance> SelectionCoordinatorNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<SelectionCoordinatorNode>(
        instanceName, const_cast<SelectionCoordinatorNodeType*>(this));
}

// ============================================================================
// SELECTION COORDINATOR NODE IMPLEMENTATION
// ============================================================================

SelectionCoordinatorNode::SelectionCoordinatorNode(
    const std::string& instanceName,
    NodeType* nodeType
) : TypedNode<SelectionCoordinatorNodeConfig>(instanceName, nodeType)
{
    NODE_LOG_INFO("[SelectionCoordinator] constructor");
}

void SelectionCoordinatorNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_INFO("[SelectionCoordinator] setup");
    lastLeftDown_   = false;
    selectionCount_ = 0;
}

void SelectionCoordinatorNode::CompileImpl(TypedCompileContext& ctx) {
    // Nothing to cache: the coordinator no longer owns providers or any Vulkan resource. Provider
    // NODES do the GPU work and feed candidates via the MultiConnect PROVIDER_CANDIDATES slot.
    NODE_LOG_INFO("[SelectionCoordinator] compile: candidate-gathering coordinator (providers are nodes)");
}

void SelectionCoordinatorNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Pull per-frame inputs. Guard nulls.
    InputStatePtr input = ctx.In(SelectionCoordinatorNodeConfig::INPUT_STATE);

    // Always publish the current status output, even on early-out frames.
    ctx.Out(SelectionCoordinatorNodeConfig::SELECTION_COUNT, selectionCount_);

    if (!input) {
        return;  // no input state this frame
    }

    // Edge-detect the left-button press: fire only on the down-edge.
    const bool leftDown = input->mouseButtons[0];
    const bool pressedThisFrame = leftDown && !lastLeftDown_;
    lastLeftDown_ = leftDown;

    if (!pressedThisFrame) {
        return;  // cheap: only resolve on a click edge
    }

    // --- Gather the provider candidate(s) and resolve --------------------------------------------
    // A provider node emits one SelectionCandidate per frame on its CANDIDATE output (hit=false off
    // the click edge / on a miss). We resolve through a vector so this stays ready for N providers:
    // pickBestCandidate picks the best HIT by MAX priority, tie-break MIN depth (UI occludes world).
    // (Today a single provider is wired — see the FAN-IN NOTE in the config for the engine
    // accumulation-gather gap; this is the same pickBestCandidate the vector fan-in would use.)
    const SelectionCandidate candidate = ctx.In(SelectionCoordinatorNodeConfig::PROVIDER_CANDIDATE);
    const std::vector<SelectionCandidate> candidates{ candidate };

    // Resolve via the shared rule (max priority, tie-break min depth; non-hits ignored).
    const SelectionCandidate* best = pickBestCandidate(candidates);

    // Map input modifier keys to a SelectionModifier: Shift→Add, Ctrl→Toggle, else Replace.
    SelectionModifier modifier = SelectionModifier::Replace;
    if (input->IsKeyDown(EventBus::KeyCode::Shift)) {
        modifier = SelectionModifier::Add;
    } else if (input->IsKeyDown(EventBus::KeyCode::Ctrl)) {
        modifier = SelectionModifier::Toggle;
    }

    auto* bus = GetMessageBus();

    if (!best) {
        // Miss. Replace-on-miss clears the selection (standard behavior); Add/Toggle on empty space
        // leave the set unchanged (nothing to combine), matching typical editors.
        if (modifier == SelectionModifier::Replace && !set_.empty()) {
            set_.clear();
            selectionCount_ = static_cast<uint32_t>(set_.size());
            ctx.Out(SelectionCoordinatorNodeConfig::SELECTION_COUNT, selectionCount_);

            NODE_LOG_INFO("[SelectionCoordinator] miss — selection cleared (Replace on empty)");
            if (bus) {
                bus->Publish(std::make_unique<EventBus::SelectionChangedEvent>(
                    instanceId,
                    std::vector<SelectionId>(set_.ids().begin(), set_.ids().end()),
                    kInvalidSelectionId));
            }
        } else {
            NODE_LOG_INFO(std::string("[SelectionCoordinator] miss (no provider candidate hit)") +
                          "; selection unchanged (size=" + std::to_string(set_.size()) + ")");
        }
        return;
    }

    // --- Hit: apply the modifier to the owned SelectionSet ----------------------------------------
    set_.apply(modifier, best->id);
    selectionCount_ = static_cast<uint32_t>(set_.size());
    ctx.Out(SelectionCoordinatorNodeConfig::SELECTION_COUNT, selectionCount_);

    // Decode the voxel pickID for the log (payload = (brick << 10) | voxel for the voxel domain).
    const uint32_t pickID = static_cast<uint32_t>(best->id.payload);
    const uint32_t brickIndex     = pickID >> 10;
    const uint32_t voxelLinearIdx = pickID & 0x3FFu;

    NODE_LOG_INFO("[SelectionCoordinator] HIT kind=" +
                  std::to_string(static_cast<int>(best->id.kind)) +
                  " pickID=" + std::to_string(pickID) +
                  " brick=" + std::to_string(brickIndex) +
                  " voxel=" + std::to_string(voxelLinearIdx) +
                  " priority=" + std::to_string(best->priority) +
                  " modifier=" + std::to_string(static_cast<int>(modifier)) +
                  " selectionSize=" + std::to_string(selectionCount_));

    // Broadcast the durable selection change (snapshot of the whole set, primary = the click target).
    if (bus) {
        bus->Publish(std::make_unique<EventBus::SelectionChangedEvent>(
            instanceId,
            std::vector<SelectionId>(set_.ids().begin(), set_.ids().end()),
            best->id));
    }
}

void SelectionCoordinatorNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("[SelectionCoordinator] cleanup");
    // Reset the click edge. The SelectionSet is intentionally NOT cleared here — selection is durable
    // state, and Cleanup runs on recompile (e.g. swapchain resize); a resize should not wipe it.
    lastLeftDown_ = false;
}

} // namespace Vixen::RenderGraph

// Self-registration (M3): registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::SelectionCoordinatorNodeType);
