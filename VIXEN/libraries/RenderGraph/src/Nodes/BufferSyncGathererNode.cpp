// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
// Sampled Lighting Inc3 M5: variadic VkBuffer gatherer, the generic replacement for
// ComputeStageNodeConfig's old fixed named buffer-sync slots. See
// BufferSyncGathererNodeConfig.h / BufferSyncGathererNode.h for the full rationale.

#include "Nodes/BufferSyncGathererNode.h"
#include "Core/NodeRegistration.h"
#include "Core/NodeLogging.h"

namespace Vixen::RenderGraph {

// ============================================================================
// NODETYPE FACTORY
// ============================================================================

std::unique_ptr<NodeInstance> BufferSyncGathererNodeType::CreateInstance(
    const std::string& instanceName) const {
    return std::make_unique<BufferSyncGathererNode>(instanceName, const_cast<BufferSyncGathererNodeType*>(this));
}

// ============================================================================
// CONSTRUCTOR
// ============================================================================

BufferSyncGathererNode::BufferSyncGathererNode(const std::string& instanceName, NodeType* nodeType)
    : VariadicTypedNode<BufferSyncGathererNodeConfig>(instanceName, nodeType) {
    auto* bufNodeType = static_cast<BufferSyncGathererNodeType*>(nodeType);
    SetVariadicInputConstraints(
        bufNodeType->GetDefaultMinVariadicInputs(),
        bufNodeType->GetDefaultMaxVariadicInputs());
}

// ============================================================================
// PRE-REGISTRATION
// ============================================================================

void BufferSyncGathererNode::PreRegisterBufferSlots(size_t count) {
    for (size_t i = 0; i < count; ++i) {
        VariadicSlotInfo slotInfo;
        slotInfo.resource = nullptr;
        slotInfo.resourceType = ResourceType::Buffer;
        slotInfo.slotName = "buffer_" + std::to_string(i);
        RegisterVariadicSlot(slotInfo, 0);
    }
    if (count > 0) {
        SetVariadicInputConstraints(count, count);
    }
}

// ============================================================================
// SETUP / COMPILE / EXECUTE / CLEANUP
// ============================================================================

void BufferSyncGathererNode::SetupImpl(VariadicSetupContext& /*ctx*/) {
    // No-op: slots are pre-registered via PreRegisterBufferSlots (graph-construction time),
    // mirroring DescriptorResourceGathererNode's own "discovery happens before Setup" note.
}

void BufferSyncGathererNode::CompileImpl(VariadicCompileContext& ctx) {
    size_t variadicCount = ctx.InVariadicCount();
    std::vector<VkBuffer> buffers;
    std::vector<Resource*> constituents;
    buffers.reserve(variadicCount);
    constituents.reserve(variadicCount);

    for (size_t i = 0; i < variadicCount; ++i) {
        Resource* resource = ctx.InVariadicResource(i);
        if (!resource) {
            NODE_LOG_WARNING("[BufferSyncGathererNode::Compile] Slot " + std::to_string(i) +
                              " is unconnected for " + GetInstanceName());
            buffers.push_back(VK_NULL_HANDLE);
            continue;
        }
        buffers.push_back(resource->GetHandle<VkBuffer>());
        // Preserve the ORIGINAL per-entry Resource* (e.g. a StorageBufferNode's own
        // STORAGE_BUFFER output), not just the flattened VkBuffer value — this is what
        // lets ResourceAccessTracker::AddNode expand this gathered array into N
        // independent hazard records (see Resource::hazardConstituents_'s own doc
        // comment) instead of collapsing all N buffers into one indivisible hazard on
        // the array-wrapper Resource* this node publishes below.
        constituents.push_back(resource);
    }

    ctx.Out(BufferSyncGathererNodeConfig::BUFFER_ARRAY, buffers);
    if (Resource* arrayRes = GetOutput(BufferSyncGathererNodeConfig::BUFFER_ARRAY_Slot::index, ctx.taskIndex)) {
        arrayRes->SetHazardConstituents(std::move(constituents));
    }
}

void BufferSyncGathererNode::ExecuteImpl(VariadicExecuteContext& ctx) {
    // Re-read every frame: mirrors DescriptorResourceGathererNode's own Compile-gathers-
    // static/Execute-refreshes-transient split. The reservoir ping-pong buffers connected
    // here are Persistent (StorageBufferNode) so their handles never actually change frame-
    // to-frame, but republishing each Execute keeps this node consistent with every other
    // gatherer's own per-frame output convention (e.g. DescriptorResourceGathererNode's own
    // ExecuteImpl re-publishes DESCRIPTOR_RESOURCES every frame even for static resources).
    // hazardConstituents_ is set ONLY at Compile (the tracker/scheduler bake the schedule
    // from the Compile-time graph shape, not re-derived every Execute) — re-setting it here
    // every frame would be redundant work with no observable effect.
    size_t variadicCount = ctx.InVariadicCount();
    std::vector<VkBuffer> buffers;
    buffers.reserve(variadicCount);

    for (size_t i = 0; i < variadicCount; ++i) {
        Resource* resource = ctx.InVariadicResource(i);
        buffers.push_back(resource ? resource->GetHandle<VkBuffer>() : VK_NULL_HANDLE);
    }

    ctx.Out(BufferSyncGathererNodeConfig::BUFFER_ARRAY, buffers);
}

void BufferSyncGathererNode::CleanupImpl(VariadicCleanupContext& /*ctx*/) {
    // No owned resources to release — this node only gathers handles owned elsewhere.
}

} // namespace Vixen::RenderGraph

// Self-registration: registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::BufferSyncGathererNodeType);
