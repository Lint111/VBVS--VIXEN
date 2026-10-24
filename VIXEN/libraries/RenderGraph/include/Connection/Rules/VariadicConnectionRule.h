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
 *
 * It only declares the connection: the slot records its upstream (sourceNode/sourceOutput), and the
 * variadic node re-reads that output itself at Compile and before Execute (VariadicTypedNode).
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
};

} // namespace Vixen::RenderGraph
