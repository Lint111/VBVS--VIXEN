#pragma once

/**
 * @file ConnectionPipeline.h
 * @brief Orchestrates modifier execution around base rule
 */

#include "Connection/ConnectionRule.h"
#include "Connection/ConnectionModifier.h"
#include <memory>
#include <vector>

namespace Vixen::RenderGraph {

/**
 * @brief Orchestrates modifier execution around base rule
 *
 * Executes the 3-phase modifier lifecycle:
 * 1. All modifiers' PreValidation (in priority order)
 * 2. Base rule Validate()
 * 3. All modifiers' PreResolve (in priority order)
 * 4. Base rule Resolve()
 * 5. All modifiers' PostResolve (in priority order)
 *
 * If any step fails, execution stops and error is returned.
 */
class ConnectionPipeline {
public:
    ConnectionPipeline() = default;
    ~ConnectionPipeline() = default;

    // Non-copyable, movable
    ConnectionPipeline(const ConnectionPipeline&) = delete;
    ConnectionPipeline& operator=(const ConnectionPipeline&) = delete;
    ConnectionPipeline(ConnectionPipeline&&) = default;
    ConnectionPipeline& operator=(ConnectionPipeline&&) = default;

    /**
     * @brief Add a modifier to the pipeline
     *
     * Modifiers are sorted by priority (descending) within each phase.
     */
    void AddModifier(std::unique_ptr<ConnectionModifier> modifier);

    /**
     * @brief Execute the full pipeline with the given rule
     *
     * @param ctx Connection context (will be modified)
     * @param rule The base connection rule to execute
     * @return Result from final stage, or first error encountered
     */
    [[nodiscard]] ConnectionResult Execute(ConnectionContext& ctx, const ConnectionRule& rule);

    /**
     * @brief Get number of modifiers in pipeline
     */
    [[nodiscard]] size_t ModifierCount() const { return modifiers_.size(); }

    /**
     * @brief Check if pipeline has any modifiers
     */
    [[nodiscard]] bool HasModifiers() const { return !modifiers_.empty(); }

    /**
     * @brief Clear all modifiers
     */
    void Clear() { modifiers_.clear(); }

private:
    std::vector<std::unique_ptr<ConnectionModifier>> modifiers_;

    void SortByPriority();
};

} // namespace Vixen::RenderGraph
