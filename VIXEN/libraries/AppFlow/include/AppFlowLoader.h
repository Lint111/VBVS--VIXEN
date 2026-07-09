#pragma once
#include "AppFlowResults.h"
#include "ActionStack.h"
#include "BindingStore.h"
#include "FlowStateMachine.h"
#include "generated/AppFlow.g.h"

namespace Vixen::AppFlow {

using Generated::AppFlowContainerView;

// Ingests the generated mirror (AppFlowContainerView) into the three Inc-1 runtime
// primitives (design §5 "loader"). Validates before loading anything: an empty action
// table is rejected outright, and every transition's from/to must be valid FlowStateId
// members. Never throws (§6 error model).
struct AppFlowLoader {
    static LoadResult Load(const AppFlowContainerView& view, FlowStateMachine& fsm,
                            ActionStack& stack, BindingStore& bindings);
};

} // namespace Vixen::AppFlow
