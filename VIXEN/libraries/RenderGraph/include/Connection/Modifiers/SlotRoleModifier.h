#pragma once

/**
 * @file SlotRoleModifier.h
 * @brief Generic modifier to override slot role
 *
 * Universal modifier that works with any connection rule.
 * Overrides the SlotRole used for dependency tracking.
 */

#include "Connection/ConnectionModifier.h"

namespace Vixen::RenderGraph {

/**
 * @brief Override slot role for a connection
 *
 * Generic modifier that changes the SlotRole used for dependency tracking.
 * Works with Direct, Variadic, and Accumulation connections.
 *
 * Use cases:
 * - Force Execute role for outputs that should rebuild every frame
 * - Force Dependency role for resources that should trigger recompilation
 *
 * Example:
 * @code
 * batch.Connect(nodeA, ConfigA::OUT, nodeB, ConfigB::IN,
 *               ConnectionMeta{}.With<SlotRoleModifier>(SlotRole::Execute));
 * @endcode
 */
class SlotRoleModifier : public ConnectionModifier {
public:
    SlotRole role;

    explicit SlotRoleModifier(SlotRole r) : role(r) {}

    ConnectionResult PreResolve(ConnectionContext& ctx) override {
        ctx.roleOverride = role;
        return ConnectionResult::Success();
    }

    [[nodiscard]] std::string_view Name() const override { return "SlotRole"; }
};

} // namespace Vixen::RenderGraph
