#include "Connection/Rules/DirectConnectionRule.h"
#include "Core/NodeInstance.h"
#include "Core/RenderGraph.h"

namespace Vixen::RenderGraph {

namespace {

[[nodiscard]] inline bool IsValidNodePtr(const void* p) {
    return reinterpret_cast<uintptr_t>(p) > 0x10000;
}

} // anonymous namespace

bool DirectConnectionRule::CanHandle(
    const SlotInfo& source,
    const SlotInfo& target) const {

    (void)source;

    // We don't handle accumulation slots - those need AccumulationConnectionRule
    if (target.IsAccumulation()) {
        return false;
    }

    // We handle all other connections (slot-to-slot, slot-to-binding)
    return true;
}

ConnectionResult DirectConnectionRule::Validate(const ConnectionContext& ctx) const {
    if (!ctx.sourceNode) {
        return ConnectionResult::Error("Source node is null");
    }

    if (!ctx.targetNode) {
        return ConnectionResult::Error("Target node is null");
    }

    const auto& sourceSlot = ctx.sourceSlot;
    const auto& targetSlot = ctx.targetSlot;

    if (!sourceSlot.IsOutput()) {
        return ConnectionResult::Error("Source slot must be an output slot");
    }

    if (targetSlot.IsStatic()) {
        if (!targetSlot.IsInput()) {
            return ConnectionResult::Error("Target slot must be an input slot");
        }

        const ResourceType sourceType = sourceSlot.resourceType;
        const ResourceType targetType = targetSlot.resourceType;

        // Pass-through storage slots are compatible with any type.
        if (sourceType != ResourceType::PassThroughStorage &&
            targetType != ResourceType::PassThroughStorage) {
            ResourceType effectiveSourceType = ctx.GetEffectiveSourceType();
            if (effectiveSourceType != targetType) {
                return ConnectionResult::Error(
                    "Resource type mismatch: source type does not match target type");
            }
        }
    }

    if (targetSlot.IsBinding()) {
        if (targetSlot.binding == UINT32_MAX) {
            return ConnectionResult::Error("Invalid binding index");
        }
    }

    return ConnectionResult::Success();
}

ConnectionResult DirectConnectionRule::Resolve(ConnectionContext& ctx) const {
    if (ctx.targetSlot.IsStatic() && ctx.graph) {
        if (IsValidNodePtr(ctx.sourceNode) && IsValidNodePtr(ctx.targetNode)) {
            NodeHandle sourceHandle = ctx.sourceNode->GetHandle();
            NodeHandle targetHandle = ctx.targetNode->GetHandle();

            if (sourceHandle.index != UINT32_MAX && targetHandle.index != UINT32_MAX) {
                ctx.graph->ConnectNodes(
                    sourceHandle,
                    ctx.sourceSlot.index,
                    targetHandle,
                    ctx.targetSlot.index
                );
            }
        }
    }

    ConnectionResult result;
    result.success = true;
    return result;
}

} // namespace Vixen::RenderGraph
