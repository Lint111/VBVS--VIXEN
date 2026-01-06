#pragma once

/**
 * @file ConnectionModifier.h
 * @brief Base class for stackable connection modifiers
 *
 * Part of Sprint 6.0.1 Unified Connection System.
 * Modifiers wrap ConnectionRule execution with 3-phase lifecycle.
 *
 * Two categories of modifiers:
 * 1. Generic modifiers - Work with any connection rule (SlotRoleModifier, FieldExtractionModifier)
 * 2. RuleConfig - Rule-specific, self-validating (AccumulationConfig, etc.)
 */

#include "Connection/ConnectionTypes.h"
#include <algorithm>
#include <memory>
#include <span>
#include <string_view>
#include <typeindex>
#include <vector>

namespace Vixen::RenderGraph {

// Forward declare ConnectionRule for RuleConfig validation
class ConnectionRule;

/**
 * @brief Stackable modifier for cross-cutting connection concerns
 *
 * Modifiers wrap ConnectionRule execution with a 3-phase lifecycle pipeline
 * that enables orthogonal features to work with ANY connection type
 * (Direct, Variadic, Accumulation) without creating N×M rule subclasses.
 *
 * ## 3-Phase Pipeline Lifecycle
 *
 * ### Phase 1: PreValidation
 * **When:** Before the ConnectionRule's Validate() method runs
 * **Purpose:** Guards, preconditions, context transformation for validation
 * **State Available:** Source/target SlotInfo, nodes, graph
 * **Can Modify:** SlotInfo (roles, types, effective types)
 * **Return:** Success (continue), Error (abort), Skip (ignore this modifier)
 *
 * **Use cases:**
 * - Field extraction: Set effectiveResourceType before validation
 * - Role override: Modify SlotRole before dependency checking
 * - Early rejection: Validate preconditions (e.g., Persistent lifetime required)
 *
 * **Example:**
 * @code
 * ConnectionResult PreValidation(ConnectionContext& ctx) override {
 *     // FieldExtractionModifier: Require Persistent lifetime
 *     if (ctx.sourceSlot.lifetime != SlotLifetime::Persistent) {
 *         return ConnectionResult::Error("Field extraction requires Persistent slot");
 *     }
 *
 *     // Transform type for validation
 *     ctx.effectiveResourceType = fieldType_;
 *     return ConnectionResult::Success();
 * }
 * @endcode
 *
 * ### Phase 2: PreResolve
 * **When:** After validation passes, before ConnectionRule's Resolve() method
 * **Purpose:** Final context transformation before rule execution
 * **State Available:** Validated connection context
 * **Can Modify:** Context fields (offsets, metadata)
 * **Return:** Success (continue), Error (abort)
 *
 * **Use cases:**
 * - Offset calculation: Compute field offset for variadic binding
 * - Type transformation: Finalize type conversions
 * - Metadata preparation: Set up execution-time data
 *
 * **Example:**
 * @code
 * ConnectionResult PreResolve(ConnectionContext& ctx) override {
 *     // Calculate field offset for GPU shader
 *     ctx.fieldOffset = CalculateFieldOffset(structType_, fieldPtr_);
 *     return ConnectionResult::Success();
 * }
 * @endcode
 *
 * ### Phase 3: PostResolve
 * **When:** After ConnectionRule's Resolve() completes successfully
 * **Purpose:** Post-processing, hook registration, metrics collection
 * **State Available:** Finalized connection (edge registered)
 * **Can Modify:** Debug state, metrics, hooks
 * **Return:** Success (finalize), Error (report failure but don't undo edge)
 *
 * **Use cases:**
 * - Debug registration: Add connection to visualization graph
 * - Metrics: Track connection statistics
 * - Callback setup: Register execution-time hooks
 *
 * **Example:**
 * @code
 * ConnectionResult PostResolve(ConnectionContext& ctx) override {
 *     // Register for debug visualization
 *     debugGraph->AddConnection(ctx.sourceNode, ctx.targetNode, debugTag_);
 *     return ConnectionResult::Success();
 * }
 * @endcode
 *
 * ## Modifier Ordering and Priority
 *
 * Modifiers execute in priority order (higher = first) within each phase.
 * Default priority is 50. Use Priority() to control execution order:
 *
 * @code
 * uint32_t Priority() const override { return 100; }  // Run before default
 * @endcode
 *
 * **Execution flow:**
 * 1. Sort modifiers by priority (descending)
 * 2. PreValidation phase: For each modifier, call PreValidation()
 * 3. ConnectionRule::Validate() (base rule validation)
 * 4. PreResolve phase: For each modifier, call PreResolve()
 * 5. ConnectionRule::Resolve() (register edge)
 * 6. PostResolve phase: For each modifier, call PostResolve()
 *
 * If any phase returns Error, the pipeline aborts immediately.
 *
 * ## Built-in Modifiers
 *
 * **Generic Modifiers** (work with any connection type):
 * - `FieldExtractionModifier`: Extract struct field for variadic binding
 * - `SlotRoleModifier`: Override dependency/execute role
 * - `DebugTagModifier`: Add debug metadata for visualization
 *
 * **RuleConfig Modifiers** (rule-specific, self-validating):
 * - `AccumulationSortConfig`: Set sort key for accumulation ordering
 *
 * ## Creating Custom Modifiers
 *
 * Derive from ConnectionModifier and override relevant phases:
 *
 * @code
 * class MetricsModifier : public ConnectionModifier {
 * public:
 *     explicit MetricsModifier(MetricsCollector* collector)
 *         : collector_(collector) {}
 *
 *     // Track connection in metrics
 *     ConnectionResult PostResolve(ConnectionContext& ctx) override {
 *         collector_->RecordConnection(ctx.sourceNode, ctx.targetNode);
 *         return ConnectionResult::Success();
 *     }
 *
 *     std::string_view Name() const override { return "MetricsModifier"; }
 *
 * private:
 *     MetricsCollector* collector_;
 * };
 * @endcode
 *
 * Usage:
 * @code
 * batch.Connect(src, SrcConfig::OUT, tgt, TgtConfig::IN,
 *               ConnectionMeta{}.With<MetricsModifier>(&metricsCollector));
 * @endcode
 *
 * ## See Also
 *
 * - ConnectionPipeline: Orchestrates modifier execution
 * - ConnectionRule: Base rule validation and resolution
 * - RuleConfig: Self-validating rule-specific modifiers
 */
class ConnectionModifier {
public:
    virtual ~ConnectionModifier() = default;

    /**
     * @brief Phase 1: Pre-validation (guards + context transformation)
     *
     * Use for: Guards, preconditions, early rejection, AND context transformation.
     * Called BEFORE the base rule's Validate() method.
     *
     * Modifiers that need to transform the context for validation (e.g.,
     * FieldExtractionModifier setting effectiveResourceType) should do so here.
     *
     * @param ctx Connection context (may be modified)
     * @return Success to continue, Error to reject connection
     */
    [[nodiscard]] virtual ConnectionResult PreValidation(ConnectionContext& ctx) {
        return ConnectionResult::Success();
    }

    /**
     * @brief Phase 2: Pre-resolve transformation (before rule resolves)
     *
     * Use for: Context mutation, type transformation, offset calculation.
     * Called AFTER validation passes, BEFORE Resolve().
     *
     * @param ctx Connection context (may be modified)
     * @return Success to continue, Error to abort
     */
    virtual ConnectionResult PreResolve(ConnectionContext& ctx) {
        return ConnectionResult::Success();
    }

    /**
     * @brief Phase 3: Post-resolve hooks (after rule resolves)
     *
     * Use for: Debug registration, metrics, callback setup.
     * Called AFTER the base rule's Resolve() completes successfully.
     *
     * @param ctx Connection context (final state)
     * @return Success to finalize, Error to report failure
     */
    virtual ConnectionResult PostResolve(ConnectionContext& ctx) {
        return ConnectionResult::Success();
    }

    /**
     * @brief Priority for modifier ordering (higher = runs first within phase)
     *
     * Modifiers with higher priority execute first in each phase.
     * Default is 50.
     */
    [[nodiscard]] virtual uint32_t Priority() const { return 50; }

    /**
     * @brief Human-readable name for debugging/logging
     */
    [[nodiscard]] virtual std::string_view Name() const = 0;
};

// ============================================================================
// RULE CONFIG - Self-validating rule-specific modifier
// ============================================================================

/**
 * @brief Base class for rule-specific configuration modifiers
 *
 * RuleConfig extends ConnectionModifier with rule-type validation.
 * If applied to an incompatible rule type, it logs a warning and skips
 * (graceful failure - connection continues without this config).
 *
 * Subclasses must implement:
 * - ValidRuleTypes() - Return span of compatible rule type_index values
 * - ApplyConfig() - Apply configuration to context (called after validation)
 * - Name() - Human-readable name for logging
 *
 * Example:
 * @code
 * class AccumulationSortConfig : public RuleConfig {
 * public:
 *     int32_t sortKey = 0;
 *
 *     explicit AccumulationSortConfig(int32_t key) : sortKey(key) {}
 *
 *     std::span<const std::type_index> ValidRuleTypes() const override {
 *         static const std::type_index types[] = {
 *             std::type_index(typeid(AccumulationConnectionRule))
 *         };
 *         return types;
 *     }
 *
 * protected:
 *     ConnectionResult ApplyConfig(ConnectionContext& ctx) override {
 *         ctx.sortKey = sortKey;
 *         return ConnectionResult::Success();
 *     }
 * };
 * @endcode
 */
class RuleConfig : public ConnectionModifier {
public:
    /**
     * @brief Get the rule types this config is compatible with
     *
     * @return Span of type_index values for compatible ConnectionRule subclasses
     */
    [[nodiscard]] virtual std::span<const std::type_index> ValidRuleTypes() const = 0;

    /**
     * @brief Set the matched rule for validation (called by pipeline)
     */
    void SetMatchedRule(const ConnectionRule* rule) { matchedRule_ = rule; }

    /**
     * @brief PreValidation checks rule type compatibility
     *
     * If the matched rule's type is not in ValidRuleTypes(), logs warning
     * and returns Skip() to gracefully ignore this config.
     */
    [[nodiscard]] ConnectionResult PreValidation(ConnectionContext& ctx) override final {
        if (!matchedRule_) {
            return ConnectionResult::Skip("RuleConfig: No matched rule set");
        }

        const auto validTypes = ValidRuleTypes();
        const auto ruleType = std::type_index(typeid(*matchedRule_));

        bool isValid = std::ranges::any_of(validTypes, [&](const std::type_index& t) {
            return t == ruleType;
        });

        if (!isValid) {
            // Skip gracefully - wrong config for this rule type
            return ConnectionResult::Skip("RuleConfig type mismatch");
        }

        return ApplyConfig(ctx);
    }

protected:
    /**
     * @brief Apply rule-specific configuration to context
     *
     * Called after rule type validation passes.
     *
     * @param ctx Connection context to modify
     * @return Success to continue, Error to fail connection
     */
    virtual ConnectionResult ApplyConfig(ConnectionContext& ctx) = 0;

private:
    const ConnectionRule* matchedRule_ = nullptr;
};

// ============================================================================
// CONNECTION META - Pure modifier container
// ============================================================================

/**
 * @brief Metadata for connection customization
 *
 * Pure modifier container - no rule-specific fields here.
 * Rule-specific configuration is done via RuleConfig subclasses
 * that are added as modifiers.
 *
 * Example usage:
 * @code
 * // Accumulation with ordering
 * batch.Connect(nodeA, ConfigA::OUT, nodeB, ConfigB::ACCUM,
 *               ConnectionMeta{}.With<AccumulationSortConfig>(5));
 *
 * // Variadic with field extraction and role override
 * batch.Connect(swapchain, SwapChainConfig::PUBLIC,
 *               gatherer, Shader::output,
 *               ConnectionMeta{}
 *                   .With(ExtractField(&SwapChainVars::colorBuffer))
 *                   .With<SlotRoleModifier>(SlotRole::Execute));
 * @endcode
 */
struct ConnectionMeta {
    std::vector<std::unique_ptr<ConnectionModifier>> modifiers;

    ConnectionMeta() = default;

    // Move-only (unique_ptr members)
    ConnectionMeta(const ConnectionMeta&) = delete;
    ConnectionMeta& operator=(const ConnectionMeta&) = delete;
    ConnectionMeta(ConnectionMeta&&) = default;
    ConnectionMeta& operator=(ConnectionMeta&&) = default;

    /**
     * @brief Add a pre-constructed modifier (rvalue chain)
     */
    ConnectionMeta&& With(std::unique_ptr<ConnectionModifier> mod) && {
        modifiers.push_back(std::move(mod));
        return std::move(*this);
    }

    /**
     * @brief Construct and add a modifier in-place (rvalue chain)
     */
    template<typename Mod, typename... Args>
    ConnectionMeta&& With(Args&&... args) && {
        modifiers.push_back(std::make_unique<Mod>(std::forward<Args>(args)...));
        return std::move(*this);
    }

    /**
     * @brief Check if any modifiers are present
     */
    [[nodiscard]] bool HasModifiers() const { return !modifiers.empty(); }

    /**
     * @brief Get modifier count
     */
    [[nodiscard]] size_t ModifierCount() const { return modifiers.size(); }
};

} // namespace Vixen::RenderGraph
