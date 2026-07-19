#pragma once
#include <cstdint>
#include <unordered_map>
#include "AppFlowResults.h"
#include "ActionStack.h"
#include "BindingStore.h"
#include "FlowStateMachine.h"
#include "InputProfile.h"
#include "IViewDataProvider.h"   // ViewNounId (Vixen::AppFlow, NOT ::Generated) + ViewNounKey
#include "generated/AppFlow.g.h"

namespace Vixen::AppFlow {

using Generated::AppFlowContainerView;

// (action id -> the [View] noun a Data verb reads/writes). ViewNounId lives in Vixen::AppFlow
// (generated ViewNounId.g.h), NOT in ::Generated. Seam M2c: the loader fills this from
// view.dataTargets(); the runtime dispatches a Data action through its IViewDataProvider using
// this mapping. Keyed by the underlying uint16_t (no default hash for a plain enum class).
using DataTargetTable = std::unordered_map<uint16_t, ViewNounId>;

// Ingests the generated mirror (AppFlowContainerView) into the Inc-1 runtime primitives
// (design §5 "loader"), plus (Inc-4) the element-trigger/key-default tables into the
// BindingStore/InputProfile, plus (M2c) the dataTargets into a DataTargetTable. Validates
// before loading anything: an empty action table is rejected outright, and every transition's
// from/to must be valid FlowStateId members. Never throws (§6 error model).
struct AppFlowLoader {
    static LoadResult Load(const AppFlowContainerView& view, FlowStateMachine& fsm,
                            ActionStack& stack, BindingStore& bindings, InputProfile& input,
                            DataTargetTable* dataTargets = nullptr);
};

} // namespace Vixen::AppFlow
