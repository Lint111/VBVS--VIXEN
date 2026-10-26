#include "BindingStore.h"
#include <algorithm>
#include <charconv>

namespace Vixen::AppFlow {

void BindingStore::RegisterActions(std::span<const AppFlowActionDecl> decls) {
    for (const auto& decl : decls) {
        std::vector<ParamSchema> schema;
        schema.reserve(decl.paramCount);
        for (uint32_t i = 0; i < decl.paramCount; ++i)
            schema.push_back({decl.params[i].name ? decl.params[i].name : "", decl.params[i].type});
        registry_[static_cast<uint16_t>(decl.id)] = std::move(schema);
    }
}

bool BindingStore::ValidateParams(const std::vector<std::pair<std::string, std::string>>& params,
                                   const std::vector<ParamSchema>& schema,
                                   std::string& warn) const {
    for (const auto& [name, source] : params) {
        (void)source;
        bool known = std::any_of(schema.begin(), schema.end(),
                                  [&](const ParamSchema& s) { return name == s.name; });
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
        const auto action = registry_.find(static_cast<uint16_t>(p.action));
        if (action == registry_.end()) continue;
        if (selector.compare(0, p.literals.front().size(), p.literals.front()) != 0) continue;
        std::vector<std::pair<std::string, std::string>> params;
        size_t cursor = p.literals.front().size();
        bool matched = true;
        for (size_t i = 0; i < p.paramNames.size() && matched; ++i) {
            const std::string& next = p.literals[i + 1];
            const bool last = i + 1 == p.paramNames.size();
            size_t end = std::string::npos;
            if (last) {
                // The final placeholder runs up to the trailing literal, which must end the selector.
                if (selector.size() >= cursor + next.size() &&
                    selector.compare(selector.size() - next.size(), next.size(), next) == 0) {
                    end = selector.size() - next.size();
                }
            } else {
                end = selector.find(next, cursor + 1);
            }
            if (end == std::string::npos || end <= cursor) { matched = false; break; }
            std::string value = selector.substr(cursor, end - cursor);
            const auto param = std::find_if(action->second.begin(), action->second.end(),
                [&](const ParamSchema& schema) { return p.paramNames[i] == schema.name; });
            if (param == action->second.end()) { matched = false; break; }
            if (param->type == Generated::FlowParamType::Int) {
                int32_t parsedValue;
                const auto parsed = std::from_chars(value.data(), value.data() + value.size(), parsedValue);
                if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) { matched = false; break; }
            }
            params.emplace_back(p.paramNames[i], std::move(value));
            cursor = end + next.size();
        }
        if (!matched || cursor != selector.size()) continue;
        out = BoundAction{p.action, p.on, std::move(params),
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
    // Split "panel-{panelId}-to-{region}" into literals {"panel-", "-to-", ""} and placeholders
    // {panelId, region}. The first placeholder binds the generated ParamName ("layer-{index}-toggle"
    // binds layerIndex); a further placeholder binds the action parameter it names.
    PatternBinding binding{{}, {}, trig.id ? trig.id : "", trig.action, trig.on};
    size_t cursor = 0;
    while (true) {
        const auto open = pat.find('{', cursor);
        if (open == std::string::npos) { binding.literals.push_back(pat.substr(cursor)); break; }
        const auto close = pat.find('}', open);
        if (close == std::string::npos || close == open + 1) return;   // malformed -> inert
        binding.literals.push_back(pat.substr(cursor, open - cursor));
        binding.paramNames.push_back(binding.paramNames.empty() && trig.paramName && *trig.paramName
                                         ? std::string(trig.paramName)
                                         : pat.substr(open + 1, close - open - 1));
        cursor = close + 1;
    }
    // Adjacent placeholders cannot be split deterministically.
    for (size_t i = 1; i + 1 < binding.literals.size(); ++i) if (binding.literals[i].empty()) return;
    patterns_.push_back(std::move(binding));
}

} // namespace Vixen::AppFlow
