#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "generated/AppFlow.g.h"

namespace Vixen::AppFlow {

using Generated::FlowActionId;
using Generated::FlowParamSchema;
using Generated::AppFlowActionDecl;

// A resolved binding (mirrors undertow's BoundUiAction): action + event + ordered
// {name, source} params.
struct BoundAction {
    FlowActionId action;
    std::string on;
    std::vector<std::pair<std::string, std::string>> params;
};

// Engine-owned generalization of undertow's UiActionRegistry + UiBindingTable (design
// §7c). Registers actions with a typed param signature, resolves selector→action
// bindings with param-name validation, first-win insert, warn-skip-inert. Never throws
// across the boundary (§6 error model).
class BindingStore {
public:
    // An authored binding to resolve — the consumer-neutral form of a ui_binding.
    struct BindingSpec {
        std::string selector;
        FlowActionId action;
        std::string on;
        std::vector<std::pair<std::string, std::string>> params;
    };

    // Populates the action registry from the generated decl table (name via FlowActionId,
    // param signature via each decl's params/paramCount).
    void RegisterActions(std::span<const AppFlowActionDecl> decls);

    // undertow's LoadUiBindingsInto algorithm, verbatim: verify the action is registered
    // (else warn="unknown action ... inert", return false); validate every param name
    // against the action's signature (else warn="unknown param ... inert", return false);
    // first-win insert under selector (already-present selector, or empty selector, →
    // false, no overwrite). Never throws.
    bool AddBinding(const BindingSpec& spec, std::string& warn);

    // Mirrors undertow UiBindingTable::TryGetForSelector. Exact bindings win; falls back to
    // a parametric pattern match (see AddElementTrigger) extracting the {placeholder} value.
    bool TryGetForSelector(const std::string& selector, BoundAction& out) const;

    size_t BindingCount() const { return bindings_.size(); }

    // Registers a generated element-trigger. A pattern with a {placeholder} ("layer-{index}-toggle")
    // splits into a literal prefix/suffix; a selector matches if it starts with prefix, ends
    // with suffix, and the middle is non-empty — the middle becomes the extracted, typed param
    // value. A pattern with no placeholder ("back-button") is a literal — it goes into the
    // exact-match table with no params (design §4.3: a button reaches its action identically
    // to a key binding).
    void AddElementTrigger(const Generated::AppFlowElementTrigger& trig);

private:
    bool ValidateParams(const std::vector<std::pair<std::string, std::string>>& params,
                         const std::vector<FlowParamSchema>& schema, std::string& warn) const;

    struct PatternBinding {
        std::string prefix, suffix, paramName;
        FlowActionId action;
        std::string on;
    };

    std::unordered_map<uint16_t, std::vector<FlowParamSchema>> registry_;
    std::unordered_map<std::string, BoundAction> bindings_;
    std::vector<PatternBinding> patterns_;
};

} // namespace Vixen::AppFlow
