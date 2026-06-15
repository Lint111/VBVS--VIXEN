#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/NodeLogging.h"
#include "Data/Nodes/SelectionCoordinatorNodeConfig.h"
#include "Selection/ISelectionProvider.h"
#include "Selection/SelectionSet.h"
#include "VulkanDeviceFwd.h"
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

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
 * @brief Engine-wide selection coordinator (SEL-P2) — generalizes the shipped PickingNode.
 *
 * The single source of truth for selection. It owns a SelectionSet (engine-side durable
 * state) and a priority-ordered list of ISelectionProviders. On a left-mouse-button PRESS
 * edge it:
 *   1. builds a SelectContext — crosshair screen point ({w/2, h/2}; the cursor is locked
 *      to screen center), viewport, camera, and a SelectionModifier read from the input
 *      modifier keys (Shift→Add, Ctrl→Toggle, else Replace);
 *   2. runs the providers highest-priority-first and takes the FIRST hit (UI-occludes-world
 *      ordering — only the voxel provider is registered today);
 *   3. applies the modifier to the SelectionSet and broadcasts a SelectionChangedEvent
 *      (snapshot of the set) on the message bus.
 *
 * The voxel domain is handled by a VoxelSelectionProvider the node owns, constructed and
 * configure()'d in CompileImpl with the device / command pool / ID image cached from the
 * graph. That provider performs the GPU ID-buffer readback the PickingNode used to do
 * inline (the readback logic was MOVED, not rewritten — see Selection-System-Design).
 *
 * Per-frame cost off the click edge is a couple of pointer reads and an edge comparison.
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
    // Register a provider, keeping providers_ sorted by priority() DESCENDING (highest first,
    // so it is queried first — UI occludes world). Called from CompileImpl.
    void RegisterProvider(std::unique_ptr<ISelectionProvider> provider);

    // ----- Compile-cached Dependency handles (stable for the cached scene's lifetime) -----
    Vixen::Vulkan::Resources::VulkanDevice* device_      = nullptr;
    VkCommandPool                           commandPool_ = VK_NULL_HANDLE;
    VkImage                                 idImage_     = VK_NULL_HANDLE;

    // ----- Selection state + providers (the node is the single source of truth) -----
    // Providers sorted by priority() descending; queried in order, first hit wins.
    std::vector<std::unique_ptr<ISelectionProvider>> providers_;
    SelectionSet set_;  ///< The durable selection set this node owns.

    // Edge detection for the left mouse button (fire on the down-edge only).
    bool lastLeftDown_ = false;

    // Status mirrored to the SELECTION_COUNT output (size of the set after the last pick).
    uint32_t selectionCount_ = 0;
};

} // namespace Vixen::RenderGraph
