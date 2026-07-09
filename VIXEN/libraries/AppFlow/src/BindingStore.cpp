#include "BindingStore.h"
#include <algorithm>

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

    bindings_.emplace(spec.selector, BoundAction{spec.action, spec.on, spec.params});
    return true;
}

bool BindingStore::TryGetForSelector(const std::string& selector, BoundAction& out) const {
    auto it = bindings_.find(selector);
    if (it == bindings_.end()) {
        return false;
    }
    out = it->second;
    return true;
}

} // namespace Vixen::AppFlow
