#include "Nodes/SelectionCoordinatorNode.h"
#include "Core/NodeLogging.h"
#include "Selection/VoxelSelectionProvider.h"
#include "Selection/SelectContext.h"
#include "InputEvents.h"
#include "SelectionEvents.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <memory>
#include <optional>
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

void SelectionCoordinatorNode::RegisterProvider(std::unique_ptr<ISelectionProvider> provider) {
    if (!provider) {
        return;
    }
    providers_.push_back(std::move(provider));
    // Keep providers sorted by priority DESCENDING (highest first → queried first, UI occludes world).
    std::stable_sort(providers_.begin(), providers_.end(),
                     [](const std::unique_ptr<ISelectionProvider>& a,
                        const std::unique_ptr<ISelectionProvider>& b) {
                         return a->priority() > b->priority();
                     });
}

void SelectionCoordinatorNode::CompileImpl(TypedCompileContext& ctx) {
    // Cache the compile-stable Dependency handles (the ID-image ring, device and command pool are
    // valid for the cached scene's lifetime). These drive the voxel provider's one-shot readback.
    device_      = ctx.In(SelectionCoordinatorNodeConfig::VULKAN_DEVICE);
    commandPool_ = ctx.In(SelectionCoordinatorNodeConfig::COMMAND_POOL);
    idImage_     = ctx.In(SelectionCoordinatorNodeConfig::ID_IMAGE);

    // Construct + configure the voxel provider and register it. Rebuilding the provider list on each
    // compile keeps the cached handles and the provider's bound resources consistent with the scene.
    providers_.clear();
    auto voxel = std::make_unique<VoxelSelectionProvider>();
    voxel->configure(device_, commandPool_, idImage_);
    RegisterProvider(std::move(voxel));

    const bool ready = device_ && commandPool_ != VK_NULL_HANDLE && idImage_ != VK_NULL_HANDLE;
    NODE_LOG_INFO(std::string("[SelectionCoordinator] compile: ") +
                  std::to_string(providers_.size()) + " provider(s) registered; voxel provider " +
                  (ready ? "configured (device+pool+ID image acquired)"
                         : "INERT (missing device/pool/ID image)"));
}

void SelectionCoordinatorNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Pull per-frame inputs (mirror CameraNode's pull pattern). Guard nulls.
    InputStatePtr input  = ctx.In(SelectionCoordinatorNodeConfig::INPUT_STATE);
    const uint32_t width  = ctx.In(SelectionCoordinatorNodeConfig::VIEWPORT_WIDTH);
    const uint32_t height = ctx.In(SelectionCoordinatorNodeConfig::VIEWPORT_HEIGHT);

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
        return;  // cheap: only run providers on a click edge
    }

    if (width == 0 || height == 0) {
        NODE_LOG_INFO("[SelectionCoordinator] click ignored — viewport not ready");
        return;
    }

    // --- Build the SelectContext for this click ---------------------------------------------------
    // CROSSHAIR pick: the app runs with the cursor locked to screen center (GLFW cursor-disabled,
    // FPS-style look), so input->mousePosition is virtual/accumulating, NOT a usable screen pixel —
    // query the CENTER of the viewport. A future RTS-style mode would release the cursor and use
    // mousePosition; the SelectContext field is unchanged.
    const glm::vec2 screen(static_cast<float>(width) * 0.5f, static_cast<float>(height) * 0.5f);

    // Map input modifier keys to a SelectionModifier: Shift→Add, Ctrl→Toggle, else Replace.
    SelectionModifier modifier = SelectionModifier::Replace;
    if (input->IsKeyDown(EventBus::KeyCode::Shift)) {
        modifier = SelectionModifier::Add;
    } else if (input->IsKeyDown(EventBus::KeyCode::Ctrl)) {
        modifier = SelectionModifier::Toggle;
    }

    // Snapshot the camera into a local so the SelectContext can carry a stable pointer through every
    // provider's resolve() in this scope (CAMERA_DATA is Required — CameraNode emits it each Execute).
    const CameraData cameraSnapshot = ctx.In(SelectionCoordinatorNodeConfig::CAMERA_DATA);

    SelectContext selCtx{};
    selCtx.screenPoint    = screen;
    selCtx.viewportWidth  = width;
    selCtx.viewportHeight = height;
    selCtx.camera         = &cameraSnapshot;
    selCtx.modifier       = modifier;
    selCtx.button         = static_cast<int>(EventBus::MouseButton::Left);
    // selCtx.hit stays nullopt; it is the coordinator's output below.

    // --- Run providers in priority order; take the first/topmost hit (UI occludes world) ----------
    std::optional<Hit> winning;
    for (const auto& provider : providers_) {
        if (!provider) {
            continue;
        }
        if (auto hit = provider->resolve(selCtx)) {
            winning = hit;
            break;  // first hit in priority order wins
        }
    }

    auto* bus = GetMessageBus();

    if (!winning) {
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
            NODE_LOG_INFO("[SelectionCoordinator] miss (no provider hit) at center=(" +
                          std::to_string(screen.x) + ", " + std::to_string(screen.y) +
                          "); selection unchanged (size=" + std::to_string(set_.size()) + ")");
        }
        return;
    }

    // --- Hit: apply the modifier to the owned SelectionSet ----------------------------------------
    const Hit& hit = *winning;
    set_.apply(modifier, hit.id);
    selectionCount_ = static_cast<uint32_t>(set_.size());
    ctx.Out(SelectionCoordinatorNodeConfig::SELECTION_COUNT, selectionCount_);

    // Decode the voxel pickID for the log (payload = (brick << 10) | voxel for the voxel domain).
    const uint32_t pickID = static_cast<uint32_t>(hit.id.payload);
    const uint32_t brickIndex     = pickID >> 10;
    const uint32_t voxelLinearIdx = pickID & 0x3FFu;

    NODE_LOG_INFO("[SelectionCoordinator] HIT kind=" +
                  std::to_string(static_cast<int>(hit.id.kind)) +
                  " pickID=" + std::to_string(pickID) +
                  " brick=" + std::to_string(brickIndex) +
                  " voxel=" + std::to_string(voxelLinearIdx) +
                  " modifier=" + std::to_string(static_cast<int>(modifier)) +
                  " selectionSize=" + std::to_string(selectionCount_));

    // Broadcast the durable selection change (snapshot of the whole set, primary = the click target).
    if (bus) {
        bus->Publish(std::make_unique<EventBus::SelectionChangedEvent>(
            instanceId,
            std::vector<SelectionId>(set_.ids().begin(), set_.ids().end()),
            hit.id));
    }
}

void SelectionCoordinatorNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("[SelectionCoordinator] cleanup");
    // Drop providers (their dtors release any Vulkan resources they own — e.g. the voxel provider's
    // staging buffer, RAII). Reset edge/state; drop cached Dependency handles (re-acquired next
    // CompileImpl). The SelectionSet is intentionally NOT cleared here — selection is durable state,
    // and Cleanup runs on recompile (e.g. swapchain resize); a resize should not wipe the selection.
    providers_.clear();
    device_      = nullptr;
    commandPool_ = VK_NULL_HANDLE;
    idImage_     = VK_NULL_HANDLE;
    lastLeftDown_ = false;
}

} // namespace Vixen::RenderGraph
