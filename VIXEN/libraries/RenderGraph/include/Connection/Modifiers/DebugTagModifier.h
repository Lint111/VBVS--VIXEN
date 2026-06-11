#pragma once

/**
 * @file DebugTagModifier.h
 * @brief Generic modifier to add debug metadata to connections
 *
 * Universal modifier that works with any connection rule.
 * Attaches debug information for visualization and logging.
 */

#include "Connection/ConnectionModifier.h"
#include <string>

namespace Vixen::RenderGraph {

/**
 * @brief Add debug tag to a connection
 *
 * Generic modifier that attaches a debug tag for visualization
 * and logging purposes. Works with all connection types.
 *
 * Example:
 * @code
 * batch.Connect(nodeA, ConfigA::OUT, nodeB, ConfigB::IN,
 *               ConnectionMeta{}.With<DebugTagModifier>("main-pass-input"));
 * @endcode
 */
class DebugTagModifier : public ConnectionModifier {
public:
    std::string tag;

    explicit DebugTagModifier(std::string t) : tag(std::move(t)) {}

    ConnectionResult PostResolve(ConnectionContext& ctx) override {
        ctx.debugTag = tag;
        return ConnectionResult::Success();
    }

    [[nodiscard]] std::string_view Name() const override { return "DebugTag"; }
};

} // namespace Vixen::RenderGraph
