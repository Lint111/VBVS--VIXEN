#pragma once

/**
 * @file ConnectionRuleRegistry.h
 * @brief Registry for connection rules
 *
 * Maintains a prioritized list of rules and finds the appropriate
 * handler for each connection.
 */

#include "Connection/ConnectionRule.h"
#include <memory>
#include <vector>

namespace Vixen::RenderGraph {

/**
 * @brief Registry for connection rules
 *
 * Maintains a prioritized list of rules and finds the appropriate
 * handler for each connection. Used by the unified Connect() API.
 *
 * Usage:
 * ```cpp
 * ConnectionRuleRegistry registry;
 * registry.RegisterRule(std::make_unique<DirectConnectionRule>());
 * registry.RegisterRule(std::make_unique<AccumulationConnectionRule>());
 *
 * const ConnectionRule* rule = registry.FindRule(sourceSlot, targetSlot);
 * if (rule) {
 *     auto result = rule->Validate(ctx);
 *     if (result.success) {
 *         result = rule->Resolve(ctx);
 *     }
 * }
 * ```
 */
class ConnectionRuleRegistry {
public:
    ConnectionRuleRegistry() = default;
    ~ConnectionRuleRegistry() = default;

    // Non-copyable, movable
    ConnectionRuleRegistry(const ConnectionRuleRegistry&) = delete;
    ConnectionRuleRegistry& operator=(const ConnectionRuleRegistry&) = delete;
    ConnectionRuleRegistry(ConnectionRuleRegistry&&) = default;
    ConnectionRuleRegistry& operator=(ConnectionRuleRegistry&&) = default;

    /**
     * @brief Register a connection rule
     *
     * Rules are sorted by priority (descending) on insertion.
     */
    void RegisterRule(std::unique_ptr<ConnectionRule> rule);

    /**
     * @brief Find rule that can handle the given connection
     *
     * Searches rules in priority order, returns first that CanHandle().
     *
     * @param source Source slot info
     * @param target Target slot info
     * @return Pointer to matching rule, or nullptr if none found
     */
    [[nodiscard]] const ConnectionRule* FindRule(
        const SlotInfo& source,
        const SlotInfo& target) const;

    /**
     * @brief Get all registered rules (for debugging/introspection)
     */
    [[nodiscard]] const std::vector<std::unique_ptr<ConnectionRule>>& GetRules() const {
        return rules_;
    }

    /**
     * @brief Get number of registered rules
     */
    [[nodiscard]] size_t RuleCount() const {
        return rules_.size();
    }

    /**
     * @brief Create registry with default rules
     *
     * Registers:
     * - DirectConnectionRule (priority 50)
     * - AccumulationConnectionRule (priority 100)
     * - VariadicConnectionRule (priority 25)
     */
    static ConnectionRuleRegistry CreateDefault();

private:
    std::vector<std::unique_ptr<ConnectionRule>> rules_;

    void SortByPriority();
};

} // namespace Vixen::RenderGraph
