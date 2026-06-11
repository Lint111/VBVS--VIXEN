#pragma once

/**
 * @file VariadicConnectionRule.h
 * @brief Rule for variadic (slot-to-binding) connections
 */

#include "Connection/ConnectionRule.h"

namespace Vixen::RenderGraph {

// Forward declarations
class IVariadicNode;

/**
 * @brief Rule for variadic (slot-to-binding) connections
 *
 * Handles connections where:
 * - Source is a static output slot
 * - Target is a shader binding (SlotKind::Binding)
 *
 * This is the connection rule equivalent of ConnectVariadic() in TypedConnection.h.
 * It performs the full variadic connection including:
 * - Creating VariadicSlotInfo
 * - Calling UpdateVariadicSlot on the variadic node
 * - Registering dependencies
 * - Setting up lifecycle hooks for resource population
 *
 * Matches when:
 * - Target is a binding slot (SlotKind::Binding)
 * - Target is NOT accumulation (those go to AccumulationConnectionRule)
 *
 * Validation:
 * - Source is an output slot
 * - Target has valid binding index
 * - If field extraction: source lifetime is Persistent
 *
 * Resolution:
 * - Casts target to IVariadicNode
 * - Creates VariadicSlotInfo from context
 * - Calls UpdateVariadicSlot()
 * - Registers PostCompile/PreExecute hooks for resource population
 */
class VariadicConnectionRule : public ConnectionRule {
public:
    [[nodiscard]] bool CanHandle(
        const SlotInfo& source,
        const SlotInfo& target) const override;

    [[nodiscard]] ConnectionResult Validate(const ConnectionContext& ctx) const override;

    ConnectionResult Resolve(ConnectionContext& ctx) const override;

    [[nodiscard]] uint32_t Priority() const override { return 25; }

    [[nodiscard]] std::string_view Name() const override { return "VariadicConnectionRule"; }

private:
    /**
     * @brief Register PostCompile hook for resource population
     *
     * Called when source node compiles to populate the variadic slot
     * with the actual resource.
     */
    void RegisterPostCompileHook(
        const ConnectionContext& ctx,
        IVariadicNode* variadicNode,
        uint32_t bindingIndex,
        size_t bundleIndex) const;

    /**
     * @brief Register PreExecute hook for transient resources
     *
     * Called before variadic node executes to refresh transient resources.
     */
    void RegisterPreExecuteHook(
        const ConnectionContext& ctx,
        IVariadicNode* variadicNode,
        uint32_t bindingIndex,
        size_t bundleIndex) const;
};

} // namespace Vixen::RenderGraph
