#pragma once
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "AppFlowEvents.h"
#include "AppFlowResults.h"
#include "ActionStack.h"
#include "BindingStore.h"
#include "FlowStateMachine.h"
#include "InputProfile.h"
#include "LayerController.h"
#include "MessageBus.h"

namespace Vixen::AppFlow {

// The Inc-1 façade (design §5 "A user gesture, end to end"): owns the FSM, ActionStack, and
// BindingStore, and is the one thing consumers talk to. Holds a nullable, non-owning
// MessageBus*; every publish is if(bus_)-guarded so a runtime built with no bus (as in
// offline tests) is fully usable. Uses PublishImmediate (not the queued Publish) for Inc-1
// determinism — there is no drain step in this increment.
class AppFlowRuntime {
public:
    // Mirrors BoundAction::params (BindingStore.h) — an ordered {name, source/value} list
    // handed to a registered handler. No framework-known schema; each handler interprets its
    // own params.
    using Params = std::vector<std::pair<std::string, std::string>>;
    using Handler = std::function<void(const Params&)>;

    AppFlowRuntime(Vixen::EventBus::MessageBus* bus, Vixen::EventBus::SenderID sender);

    // Builds the generated AppFlowContainerView and loads it into the fsm/stack/bindings.
    LoadResult Load();

    // Pass-throughs to the fsm, exposed so a consumer (or test) can drive Inc-1's
    // externally-set guard stub without reaching into the owned FlowStateMachine.
    void SetGuardResult(Generated::FlowGuardId g, bool pass) { fsm_.SetGuardResult(g, pass); }
    void SetCurrent(Generated::FlowStateId s) { fsm_.SetCurrent(s); }
    Generated::FlowStateId Current() const { return fsm_.Current(); }

    // Delegates to the fsm; on Ok publishes AppFlowChangedEvent{StateChanged}. A navigation
    // service, not a dispatchable verb (design §4.3 — "no special framework case" for nav is
    // achieved by consumers wiring their OWN Return handler to call NavPop(); NavTo/NavPop
    // stay as plain services other services, like handlers, may call).
    DispatchResult NavTo(Generated::FlowStateId to);

    // Encapsulated pass-through to the fsm's entry-history pop (design §D6 — navigation
    // "Return", distinct from ActionStack::Undo's data revert). Mirrors NavTo: publish
    // StateChanged on Ok. Does NOT expose a raw FlowStateMachine& — the FSM stays private, as
    // in Inc-1.
    DispatchResult NavPop();

    // The registry (design §4.2): the entire router. No categories, no action-name literals,
    // no undo knowledge — a declared-but-unwired id is RejectedByState, caught rather than
    // silently no-op'd. Handlers are self-contained (design §4.3): each decides for itself
    // whether to go through Stack() (undoable), call a service, or run a bare side effect.
    void RegisterHandler(Generated::FlowActionId id, Handler fn);
    DispatchResult Dispatch(Generated::FlowActionId id, const Params& params);

    // Trigger-less dispatch for tests/programmatic callers: resolves nothing, just routes.
    DispatchResult DispatchById(Generated::FlowActionId id, const Params& params = {});

    // THE walking-skeleton spine: resolve selector -> bound action via the BindingStore,
    // then Dispatch() its id with the resolved params. A selector with no binding is
    // RejectedByState.
    DispatchResult DispatchBySelector(const std::string& selector);

    // The typed key path (Inc-4 §4.4): resolve the chord via the InputProfile under the
    // current flow-state, then Dispatch() with no params. An unbound chord is
    // RejectedByState — never a crash.
    DispatchResult DispatchByKey(Generated::KeyChord chord);

    // Pass-through so tests/consumers author bindings without reaching into the store.
    bool AddBinding(const BindingStore::BindingSpec& spec, std::string& warn) {
        return bindings_.AddBinding(spec, warn);
    }

    // The runtime's InputProfile, seeded from kKeyDefaults at Load() (mutable — the deferred
    // rebind/Steam seam).
    InputProfile& Input() { return inputProfile_; }

    // The undo/redo service (design §4.3): handlers call Stack().Dispatch/Undo/Redo
    // themselves — the framework no longer knows "Undo" as a verb.
    ActionStack& Stack() { return stack_; }

    // Inc-2: the layer enabled-mask source of truth (design §2.1/§2.3), owned by the runtime
    // so a consumer (editor) drives it through dispatch rather than mutating it directly.
    LayerController& Layers() { return layers_; }

private:
    void Publish(AppFlowChangedEvent::Kind kind, Generated::FlowStateId state,
                 Generated::FlowActionId action, uint32_t group);

    Vixen::EventBus::MessageBus* bus_;
    Vixen::EventBus::SenderID sender_;
    FlowStateMachine fsm_;
    ActionStack stack_;
    BindingStore bindings_;
    LayerController layers_;
    InputProfile inputProfile_;
    // Keyed by the underlying uint16_t (mirrors BindingStore's registry_ key type) rather
    // than FlowActionId itself, since std::unordered_map has no default hash for a plain
    // enum class without <cstdint>-adjacent boilerplate.
    std::unordered_map<uint16_t, Handler> handlers_;
};

} // namespace Vixen::AppFlow
