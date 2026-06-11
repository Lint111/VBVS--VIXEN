#pragma once

/**
 * @file AccumulationSortConfig.h
 * @brief Rule-specific config for accumulation connection ordering
 *
 * Self-validating modifier that only applies to AccumulationConnectionRule.
 * If applied to other rule types, logs warning and skips gracefully.
 */

#include "Connection/ConnectionModifier.h"
#include "Connection/Rules/AccumulationConnectionRule.h"

namespace Vixen::RenderGraph {

/**
 * @brief Configuration for accumulation connection ordering
 *
 * Rule-specific config that sets the sort key for ordering entries
 * in accumulation slots. Only valid for AccumulationConnectionRule.
 *
 * If applied to a non-accumulation connection, logs a warning and
 * skips gracefully (connection continues without this config).
 *
 * Example:
 * @code
 * // Connect with sort key 5
 * batch.Connect(passNode, PassConfig::OUTPUT,
 *               multiDispatch, MultiDispatchConfig::PASSES,
 *               ConnectionMeta{}.With<AccumulationSortConfig>(5));
 *
 * // Wrong usage - will log warning and skip
 * batch.Connect(deviceNode, DeviceConfig::DEVICE,  // Direct connection
 *               swapchain, SwapChainConfig::DEVICE,
 *               ConnectionMeta{}.With<AccumulationSortConfig>(5));  // Skipped!
 * @endcode
 */
class AccumulationSortConfig : public RuleConfig {
public:
    int32_t sortKey = 0;

    /**
     * @brief Construct with sort key
     * @param key Sort key for ordering in accumulation array
     */
    explicit AccumulationSortConfig(int32_t key) : sortKey(key) {}

    [[nodiscard]] std::span<const std::type_index> ValidRuleTypes() const override {
        static const std::type_index types[] = {
            std::type_index(typeid(AccumulationConnectionRule))
        };
        return types;
    }

    [[nodiscard]] std::string_view Name() const override { return "AccumulationSortConfig"; }

protected:
    ConnectionResult ApplyConfig(ConnectionContext& ctx) override {
        ctx.sortKey = sortKey;
        return ConnectionResult::Success();
    }
};

} // namespace Vixen::RenderGraph
