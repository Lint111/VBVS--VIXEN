#pragma once

/**
 * @file ConnectionRule.h
 * @brief Abstract base class for connection handlers
 *
 * Part of Sprint 6.0.1 Unified Connection System.
 * Each rule knows how to handle a specific type of connection.
 */

#include "Connection/ConnectionTypes.h"
#include <string_view>

namespace Vixen::RenderGraph {

/**
 * @brief Abstract base class for connection handlers
 *
 * Each rule knows how to handle a specific type of connection.
 * Rules are registered with ConnectionRuleRegistry and matched
 * based on source/target slot properties.
 *
 * Lifecycle:
 * 1. CanHandle() - Check if rule applies to this connection
 * 2. Validate() - Check if connection is valid
 * 3. Resolve() - Perform the actual connection wiring
 */
class ConnectionRule {
public:
    virtual ~ConnectionRule() = default;

    /**
     * @brief Check if this rule can handle the given connection
     *
     * Called during rule matching to find the appropriate handler.
     * Should be fast - just check slot flags and types.
     *
     * @param source Source slot info
     * @param target Target slot info (may be static slot or binding)
     * @return true if this rule can handle the connection
     */
    [[nodiscard]] virtual bool CanHandle(
        const SlotInfo& source,
        const SlotInfo& target) const = 0;

    /**
     * @brief Validate the connection
     *
     * Performs semantic validation: type compatibility, nullability,
     * ordering requirements, etc. Called before Resolve().
     *
     * @param ctx Full connection context
     * @return ConnectionResult with success/error status
     */
    [[nodiscard]] virtual ConnectionResult Validate(const ConnectionContext& ctx) const = 0;

    /**
     * @brief Resolve (execute) the connection
     *
     * Performs the actual wiring: creates resources, registers
     * dependencies, updates topology, etc.
     *
     * @param ctx Full connection context (may be modified)
     * @return ConnectionResult with success/error and created resource
     */
    virtual ConnectionResult Resolve(ConnectionContext& ctx) const = 0;

    /**
     * @brief Priority for rule matching (higher = checked first)
     *
     * When multiple rules could handle a connection, the highest
     * priority rule wins. Default is 0.
     *
     * Suggested priorities:
     * - 100: Specific rules (AccumulationConnectionRule)
     * - 50:  Standard rules (DirectConnectionRule)
     * - 25:  Fallback rules (VariadicConnectionRule)
     */
    [[nodiscard]] virtual uint32_t Priority() const { return 0; }

    /**
     * @brief Human-readable name for debugging
     */
    [[nodiscard]] virtual std::string_view Name() const = 0;
};

} // namespace Vixen::RenderGraph
