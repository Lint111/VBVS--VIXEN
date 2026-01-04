#pragma once

/**
 * @file DirectConnectionRule.h
 * @brief Rule for standard 1:1 connections
 */

#include "Connection/ConnectionRule.h"

namespace Vixen::RenderGraph {

/**
 * @brief Rule for standard 1:1 connections
 *
 * Handles any direct connection where one source connects to one target.
 * Works for both slot-to-slot AND slot-to-binding connections.
 *
 * Matches when:
 * - Target is NOT an accumulation slot (those need AccumulationConnectionRule)
 * - Single source → single target (1:1 relationship)
 *
 * Does NOT match when:
 * - Target has Accumulation flag (multi-connect)
 *
 * Supports:
 * - Slot → Slot connections
 * - Slot → Binding connections (variadic targets)
 * - Field extraction via member pointers
 *
 * Validation:
 * - Type compatibility (source type assignable to target, considering extraction)
 * - Nullability (required inputs must have connection)
 * - No existing connection for slot targets (inputs can only have one driver)
 */
class DirectConnectionRule : public ConnectionRule {
public:
    [[nodiscard]] bool CanHandle(
        const SlotInfo& source,
        const SlotInfo& target) const override;

    [[nodiscard]] ConnectionResult Validate(const ConnectionContext& ctx) const override;

    ConnectionResult Resolve(ConnectionContext& ctx) const override;

    [[nodiscard]] uint32_t Priority() const override { return 50; }

    [[nodiscard]] std::string_view Name() const override { return "DirectConnectionRule"; }
};

} // namespace Vixen::RenderGraph
