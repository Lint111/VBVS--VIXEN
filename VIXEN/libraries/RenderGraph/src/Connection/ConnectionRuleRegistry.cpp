#include "Connection/ConnectionRuleRegistry.h"
#include "Connection/Rules/DirectConnectionRule.h"
#include "Connection/Rules/AccumulationConnectionRule.h"
#include "Connection/Rules/VariadicConnectionRule.h"
#include <algorithm>

namespace Vixen::RenderGraph {

void ConnectionRuleRegistry::RegisterRule(std::unique_ptr<ConnectionRule> rule) {
    if (!rule) return;
    rules_.push_back(std::move(rule));
    SortByPriority();
}

const ConnectionRule* ConnectionRuleRegistry::FindRule(
    const SlotInfo& source,
    const SlotInfo& target) const {

    for (const auto& rule : rules_) {
        if (rule->CanHandle(source, target)) {
            return rule.get();
        }
    }
    return nullptr;
}

void ConnectionRuleRegistry::SortByPriority() {
    std::sort(rules_.begin(), rules_.end(),
        [](const std::unique_ptr<ConnectionRule>& a,
           const std::unique_ptr<ConnectionRule>& b) {
            return a->Priority() > b->Priority();
        });
}

ConnectionRuleRegistry ConnectionRuleRegistry::CreateDefault() {
    ConnectionRuleRegistry registry;
    registry.RegisterRule(std::make_unique<AccumulationConnectionRule>());
    registry.RegisterRule(std::make_unique<DirectConnectionRule>());
    registry.RegisterRule(std::make_unique<VariadicConnectionRule>());
    return registry;
}

} // namespace Vixen::RenderGraph
