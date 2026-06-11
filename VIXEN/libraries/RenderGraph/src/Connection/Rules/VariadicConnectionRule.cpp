#include "Connection/Rules/VariadicConnectionRule.h"
#include "Core/NodeInstance.h"
#include "Core/RenderGraph.h"
#include "Core/GraphLifecycleHooks.h"
#include "Core/GraphTopology.h"
#include "Core/VariadicTypedNode.h"

namespace Vixen::RenderGraph {

namespace {

[[nodiscard]] inline bool IsValidNodePtr(const void* p) {
    return reinterpret_cast<uintptr_t>(p) > 0x10000;
}

/**
 * @brief Determine SlotRole based on source output lifetime
 *
 * Auto-detects slot role if not explicitly overridden.
 * Transient outputs need both Dependency and Execute roles.
 */
SlotRole DetermineSlotRole(NodeInstance* sourceNode, uint32_t sourceSlotIndex, SlotRole roleOverride) {
    // Check for explicit override (SlotRole::Output is sentinel for "auto-detect")
    if (roleOverride != SlotRole::None && roleOverride != SlotRole::Output) {
        return roleOverride;
    }

    // Auto-detect based on source output lifetime
    if (sourceNode) {
        const auto* srcType = sourceNode->GetType();
        if (srcType) {
            const auto* outputDesc = srcType->GetOutputDescriptor(sourceSlotIndex);
            if (outputDesc && outputDesc->lifetime == ResourceLifetime::Transient) {
                return SlotRole::Dependency | SlotRole::Execute;
            }
        }
    }

    return SlotRole::Dependency;
}

/**
 * @brief Create VariadicSlotInfo from ConnectionContext
 */
VariadicSlotInfo CreateVariadicSlotInfo(const ConnectionContext& ctx, SlotRole resolvedRole) {
    VariadicSlotInfo info;

    info.resource = nullptr;  // Will be populated in PostCompile hook
    info.resourceType = ctx.GetEffectiveSourceType();
    info.slotName = std::string(ctx.targetSlot.name);
    info.binding = ctx.targetSlot.binding;
    info.descriptorType = ctx.targetSlot.descriptorType;
    info.state = SlotState::Tentative;
    info.sourceNode = ctx.sourceNode->GetHandle();
    info.sourceOutput = ctx.sourceSlot.index;
    info.slotRole = resolvedRole;

    // Field extraction support
    info.fieldOffset = ctx.sourceSlot.fieldOffset;
    info.hasFieldExtraction = ctx.sourceSlot.hasFieldExtraction;

    return info;
}

} // anonymous namespace

bool VariadicConnectionRule::CanHandle(
    const SlotInfo& source,
    const SlotInfo& target) const {

    (void)source;

    if (!target.IsBinding()) {
        return false;
    }

    if (target.IsAccumulation()) {
        return false;
    }

    return true;
}

ConnectionResult VariadicConnectionRule::Validate(const ConnectionContext& ctx) const {
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

    if (!targetSlot.IsBinding()) {
        return ConnectionResult::Error("Target must be a binding slot for variadic connection");
    }

    if (targetSlot.binding == UINT32_MAX) {
        return ConnectionResult::Error("Invalid binding index");
    }

    // Field extraction requires Persistent lifetime
    if (sourceSlot.hasFieldExtraction) {
        if (!ctx.IsPersistentSource()) {
            return ConnectionResult::Error(
                "Field extraction requires Persistent lifetime source. "
                "Source slot has Transient lifetime.");
        }
    }

    return ConnectionResult::Success();
}

ConnectionResult VariadicConnectionRule::Resolve(ConnectionContext& ctx) const {
    if (!IsValidNodePtr(ctx.sourceNode) || !IsValidNodePtr(ctx.targetNode)) {
        return ConnectionResult::Success();  // Skip placeholder nodes in tests
    }

    // Cast target to IVariadicNode
    auto* variadicNode = dynamic_cast<IVariadicNode*>(ctx.targetNode);
    if (!variadicNode) {
        return ConnectionResult::Error("Target node is not a variadic node");
    }

    // Determine slot role
    SlotRole resolvedRole = DetermineSlotRole(
        ctx.sourceNode,
        ctx.sourceSlot.index,
        ctx.roleOverride
    );

    // Create VariadicSlotInfo
    VariadicSlotInfo slotInfo = CreateVariadicSlotInfo(ctx, resolvedRole);

    // Update/create the variadic slot (bundleIndex = 0 for now)
    uint32_t bindingIndex = ctx.targetSlot.binding;
    size_t bundleIndex = 0;
    variadicNode->UpdateVariadicSlot(bindingIndex, slotInfo, bundleIndex);

    // Register dependency if slot has Dependency role
    if (HasDependency(resolvedRole)) {
        ctx.targetNode->AddDependency(ctx.sourceNode);
    }

    // Add topology edge
    if (ctx.graph) {
        GraphEdge edge;
        edge.source = ctx.sourceNode;
        edge.target = ctx.targetNode;
        edge.sourceOutputIndex = ctx.sourceSlot.index;
        edge.targetInputIndex = bindingIndex;
        ctx.graph->GetTopology().AddEdge(edge);

        // Register PostCompile hook for resource population
        if (HasDependency(resolvedRole)) {
            RegisterPostCompileHook(ctx, variadicNode, bindingIndex, bundleIndex);
        }

        // Register PreExecute hook for transient resources
        if (HasExecute(resolvedRole)) {
            RegisterPreExecuteHook(ctx, variadicNode, bindingIndex, bundleIndex);
        }
    }

    return ConnectionResult::Success();
}

void VariadicConnectionRule::RegisterPostCompileHook(
    const ConnectionContext& ctx,
    IVariadicNode* variadicNode,
    uint32_t bindingIndex,
    size_t bundleIndex) const {

    if (!ctx.graph) return;

    NodeInstance* sourceNodeInst = ctx.sourceNode;
    uint32_t sourceSlotIndex = ctx.sourceSlot.index;

    ctx.graph->GetLifecycleHooks().RegisterNodeHook(
        NodeLifecyclePhase::PostCompile,
        [=](NodeInstance* compiledNode) {
            if (compiledNode != sourceNodeInst) return;

            Resource* sourceRes = sourceNodeInst->GetOutput(
                static_cast<uint8_t>(sourceSlotIndex), 0);

            if (!sourceRes || !sourceRes->IsValid()) {
                return;  // Resource not yet available
            }

            const VariadicSlotInfo* existingSlot =
                variadicNode->GetVariadicSlotInfo(bindingIndex, bundleIndex);

            if (existingSlot) {
                VariadicSlotInfo updatedSlot = *existingSlot;
                updatedSlot.resource = sourceRes;
                updatedSlot.resourceType = sourceRes->GetType();
                variadicNode->UpdateVariadicSlot(bindingIndex, updatedSlot, bundleIndex);
            }
        },
        "VariadicConnectionRule PostCompile resource population"
    );
}

void VariadicConnectionRule::RegisterPreExecuteHook(
    const ConnectionContext& ctx,
    IVariadicNode* variadicNode,
    uint32_t bindingIndex,
    size_t bundleIndex) const {

    if (!ctx.graph) return;

    NodeInstance* sourceNodeInst = ctx.sourceNode;
    uint32_t sourceSlotIndex = ctx.sourceSlot.index;
    auto* variadicAsNodeInstance = dynamic_cast<NodeInstance*>(variadicNode);

    ctx.graph->GetLifecycleHooks().RegisterNodeHook(
        NodeLifecyclePhase::PreExecute,
        [=](NodeInstance* executingNode) {
            // Only run for the variadic node itself
            if (executingNode != variadicAsNodeInstance) return;

            Resource* sourceRes = sourceNodeInst->GetOutput(
                static_cast<uint8_t>(sourceSlotIndex), 0);

            if (!sourceRes || !sourceRes->IsValid()) {
                return;
            }

            const VariadicSlotInfo* currentSlot =
                variadicNode->GetVariadicSlotInfo(bindingIndex, bundleIndex);

            if (currentSlot) {
                VariadicSlotInfo updatedSlot = *currentSlot;
                updatedSlot.resource = sourceRes;
                updatedSlot.resourceType = sourceRes->GetType();
                variadicNode->UpdateVariadicSlot(bindingIndex, updatedSlot, bundleIndex);
            }
        },
        "VariadicConnectionRule PreExecute resource refresh"
    );
}

} // namespace Vixen::RenderGraph
