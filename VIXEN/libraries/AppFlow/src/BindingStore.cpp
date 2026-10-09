#include "BindingStore.h"
#include <algorithm>
#include <charconv>

namespace Vixen::AppFlow {

void BindingStore::RegisterActions(std::span<const AppFlowActionDecl> decls) {
    for (const auto& decl : decls) {
        std::vector<FlowParamSchema> schema(decl.params, decl.params + decl.paramCount);
        registry_[static_cast<uint16_t>(decl.id)] = std::move(schema);
    }
}

bool BindingStore::ValidateParams(const std::vector<std::pair<std::string, std::string>>& params,
                                   const std::vector<FlowParamSchema>& schema,
                                   std::string& warn) const {
    for (const auto& [name, source] : params) {
        (void)source;
        bool known = std::any_of(schema.begin(), schema.end(),
                                  [&](const FlowParamSchema& s) { return name == s.name; });
        if (!known) {
            warn = "unknown param '" + name + "' — binding inert";
            return false;
        }
    }
    return true;
}

bool BindingStore::AddBinding(const BindingSpec& spec, std::string& warn) {
    auto regIt = registry_.find(static_cast<uint16_t>(spec.action));
    if (regIt == registry_.end()) {
        warn = "unknown action — binding inert";
        return false;
    }

    if (!ValidateParams(spec.params, regIt->second, warn)) {
        return false;
    }

    if (spec.selector.empty() || bindings_.contains(spec.selector)) {
        // Empty selector, or one already bound — first-win, no overwrite.
        return false;
    }

    bindings_.emplace(spec.selector, BoundAction{spec.action, spec.on, spec.params, {}});
    return true;
}

bool BindingStore::TryGetForSelector(const std::string& selector, BoundAction& out) const {
    auto it = bindings_.find(selector);
    if (it != bindings_.end()) {
        out = it->second;   // exact wins
        return true;
    }
    for (const auto& p : patterns_) {
        if (selector.size() <= p.prefix.size() + p.suffix.size()) continue;
        if (selector.compare(0, p.prefix.size(), p.prefix) != 0) continue;
        if (selector.compare(selector.size() - p.suffix.size(), p.suffix.size(), p.suffix) != 0) continue;
        std::string mid = selector.substr(p.prefix.size(), selector.size() - p.prefix.size() - p.suffix.size());
        if (mid.empty()) continue;
        const auto action = registry_.find(static_cast<uint16_t>(p.action));
        if (action == registry_.end()) continue;
        const auto param = std::find_if(action->second.begin(), action->second.end(),
            [&](const FlowParamSchema& schema) { return p.paramName == schema.name; });
        if (param == action->second.end()) continue;
        if (param->type == Generated::FlowParamType::Int) {
            int32_t value;
            const auto parsed = std::from_chars(mid.data(), mid.data() + mid.size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr != mid.data() + mid.size()) continue;
        }
        out = BoundAction{p.action, p.on, {{p.paramName, mid}},
                          {FlowTriggerKind::ElementTrigger, p.triggerId}};
        return true;
    }
    return false;
}

void BindingStore::AddElementTrigger(const Generated::AppFlowElementTrigger& trig) {
    const std::string pat = trig.elementPattern;
    const auto lb = pat.find('{');
    const auto rb = pat.find('}');
    if (lb == std::string::npos || rb == std::string::npos) {
        // No {placeholder} -> the pattern is a literal selector ("back-button"): exact-match,
        // no extracted param. First-win, same as AddBinding (never overwrite).
        if (!pat.empty() && !bindings_.contains(pat)) {
            bindings_.emplace(pat, BoundAction{trig.action, trig.on, {},
                                                {FlowTriggerKind::ElementTrigger,
                                                 trig.id ? trig.id : ""}});
        }
        return;
    }
    if (rb < lb) {
        return;   // malformed pattern -> inert (never a wrong dispatch)
    }
    // Split "layer-{index}-toggle" into prefix="layer-", suffix="-toggle".
    patterns_.push_back({pat.substr(0, lb), pat.substr(rb + 1), trig.paramName,
                          trig.id ? trig.id : "", trig.action, trig.on});
}

} // namespace Vixen::AppFlow
