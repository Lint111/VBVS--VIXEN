#include "Connection/ConnectionPipeline.h"
#include <algorithm>

namespace Vixen::RenderGraph {

void ConnectionPipeline::AddModifier(std::unique_ptr<ConnectionModifier> modifier) {
    if (!modifier) return;
    modifiers_.push_back(std::move(modifier));
    SortByPriority();
}

void ConnectionPipeline::SortByPriority() {
    std::sort(modifiers_.begin(), modifiers_.end(),
        [](const std::unique_ptr<ConnectionModifier>& a,
           const std::unique_ptr<ConnectionModifier>& b) {
            return a->Priority() > b->Priority();
        });
}

ConnectionResult ConnectionPipeline::Execute(ConnectionContext& ctx, const ConnectionRule& rule) {
    // Pre-pass: Set matched rule on RuleConfig modifiers for type validation
    for (const auto& mod : modifiers_) {
        if (auto* ruleConfig = dynamic_cast<RuleConfig*>(mod.get())) {
            ruleConfig->SetMatchedRule(&rule);
        }
    }

    // Phase 1: All modifiers' PreValidation
    for (const auto& mod : modifiers_) {
        auto result = mod->PreValidation(ctx);
        if (!result.success) {
            result.errorMessage = std::string(mod->Name()) + " PreValidation: " + result.errorMessage;
            return result;
        }
        // Skip results are success - modifier just doesn't apply, continue
    }

    // Phase 2: Base rule validation
    auto validationResult = rule.Validate(ctx);
    if (!validationResult.success) {
        return validationResult;
    }

    // Phase 3: All modifiers' PreResolve
    for (const auto& mod : modifiers_) {
        auto result = mod->PreResolve(ctx);
        if (!result.success) {
            result.errorMessage = std::string(mod->Name()) + " PreResolve: " + result.errorMessage;
            return result;
        }
        // Skip results are success - continue
    }

    // Phase 4: Base rule resolution
    auto resolveResult = rule.Resolve(ctx);
    if (!resolveResult.success) {
        return resolveResult;
    }

    // Phase 5: All modifiers' PostResolve
    for (const auto& mod : modifiers_) {
        auto result = mod->PostResolve(ctx);
        if (!result.success) {
            result.errorMessage = std::string(mod->Name()) + " PostResolve: " + result.errorMessage;
            return result;
        }
        // Skip results are success - continue
    }

    return resolveResult;
}

} // namespace Vixen::RenderGraph
