// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
// Sampled Lighting Inc4 M1: variadic IRenderTarget* gatherer, the image-typed analogue of
// BufferSyncGathererNode (Sampled Lighting Inc3 M5). See
// ImageSyncGathererNodeConfig.h / ImageSyncGathererNode.h for the full rationale.

#include "Nodes/ImageSyncGathererNode.h"
#include "Core/NodeRegistration.h"
#include "Core/NodeLogging.h"
#include "IRenderTarget.h"

namespace Vixen::RenderGraph {

// ============================================================================
// NODETYPE FACTORY
// ============================================================================

std::unique_ptr<NodeInstance> ImageSyncGathererNodeType::CreateInstance(
    const std::string& instanceName) const {
    return std::make_unique<ImageSyncGathererNode>(instanceName, const_cast<ImageSyncGathererNodeType*>(this));
}

// ============================================================================
// CONSTRUCTOR
// ============================================================================

ImageSyncGathererNode::ImageSyncGathererNode(const std::string& instanceName, NodeType* nodeType)
    : VariadicTypedNode<ImageSyncGathererNodeConfig>(instanceName, nodeType) {
    auto* imgNodeType = static_cast<ImageSyncGathererNodeType*>(nodeType);
    SetVariadicInputConstraints(
        imgNodeType->GetDefaultMinVariadicInputs(),
        imgNodeType->GetDefaultMaxVariadicInputs());
}

// ============================================================================
// PRE-REGISTRATION
// ============================================================================

void ImageSyncGathererNode::PreRegisterImageSlots(size_t count) {
    for (size_t i = 0; i < count; ++i) {
        VariadicSlotInfo slotInfo;
        slotInfo.resource = nullptr;
        slotInfo.resourceType = ResourceType::Image;
        slotInfo.slotName = "image_" + std::to_string(i);
        RegisterVariadicSlot(slotInfo, 0);
    }
    if (count > 0) {
        SetVariadicInputConstraints(count, count);
    }
}

// ============================================================================
// SETUP / COMPILE / EXECUTE / CLEANUP
// ============================================================================

void ImageSyncGathererNode::SetupImpl(VariadicSetupContext& /*ctx*/) {
    // No-op: slots are pre-registered via PreRegisterImageSlots (graph-construction time),
    // mirroring BufferSyncGathererNode's own "discovery happens before Setup" note.
}

void ImageSyncGathererNode::CompileImpl(VariadicCompileContext& ctx) {
    size_t variadicCount = ctx.InVariadicCount();
    std::vector<Vixen::Vulkan::Resources::IRenderTarget*> images;
    std::vector<Resource*> constituents;
    images.reserve(variadicCount);
    constituents.reserve(variadicCount);

    for (size_t i = 0; i < variadicCount; ++i) {
        Resource* resource = ctx.InVariadicResource(i);
        if (!resource) {
            NODE_LOG_WARNING("[ImageSyncGathererNode::Compile] Slot " + std::to_string(i) +
                              " is unconnected for " + GetInstanceName());
            images.push_back(nullptr);
            continue;
        }
        images.push_back(resource->GetHandle<Vixen::Vulkan::Resources::IRenderTarget*>());
        // Preserve the ORIGINAL per-entry Resource* — see BufferSyncGathererNode's own
        // identical comment: this is what lets ResourceAccessTracker::AddNode expand this
        // gathered array into N independent hazard records (Resource::hazardConstituents_)
        // instead of collapsing all N images into one indivisible hazard on the
        // array-wrapper Resource* this node publishes below.
        constituents.push_back(resource);
    }

    ctx.Out(ImageSyncGathererNodeConfig::IMAGE_ARRAY, images);
    if (Resource* arrayRes = GetOutput(ImageSyncGathererNodeConfig::IMAGE_ARRAY_Slot::index, ctx.taskIndex)) {
        arrayRes->SetHazardConstituents(std::move(constituents));
    }
}

void ImageSyncGathererNode::ExecuteImpl(VariadicExecuteContext& ctx) {
    // Re-read every frame — mirrors BufferSyncGathererNode::ExecuteImpl's own rationale:
    // hazardConstituents_ is set ONLY at Compile (the tracker/scheduler bake the schedule
    // from the Compile-time graph shape), re-setting it here every frame would be
    // redundant work with no observable effect.
    size_t variadicCount = ctx.InVariadicCount();
    std::vector<Vixen::Vulkan::Resources::IRenderTarget*> images;
    images.reserve(variadicCount);

    for (size_t i = 0; i < variadicCount; ++i) {
        Resource* resource = ctx.InVariadicResource(i);
        images.push_back(resource ? resource->GetHandle<Vixen::Vulkan::Resources::IRenderTarget*>() : nullptr);
    }

    ctx.Out(ImageSyncGathererNodeConfig::IMAGE_ARRAY, images);
}

void ImageSyncGathererNode::CleanupImpl(VariadicCleanupContext& /*ctx*/) {
    // No owned resources to release — this node only gathers handles owned elsewhere.
}

} // namespace Vixen::RenderGraph

// Self-registration: registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::ImageSyncGathererNodeType);
