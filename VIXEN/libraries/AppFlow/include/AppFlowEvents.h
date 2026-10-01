#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include "Message.h"
#include "generated/AppFlow.g.h"

namespace Vixen::AppFlow {
using ::Vixen::AppFlow::Generated::FlowStateId;
using ::Vixen::AppFlow::Generated::FlowActionId;

// Cause identities reuse the declaration family that admitted the trigger. System and
// Effect identities are supplied by their existing registries when they change AppFlow.
enum class FlowTriggerKind : uint8_t {
    ElementTrigger,
    KeyDefault,
    ReturnEdge,
    FlowAction,
    System,
    Effect
};

struct FlowTriggerCause {
    FlowTriggerKind kind = FlowTriggerKind::FlowAction;
    std::string identity;
};

// Published once for every accepted flow transition. UI observes this event; systems that
// request the transition do not need to know that a UI exists.
struct AppFlowEdgeEvent : public Vixen::EventBus::BaseEventMessage {
    static constexpr Vixen::EventBus::MessageType TYPE =
        Vixen::EventBus::detail::StableMessageTypeId("Vixen.AppFlow.AppFlowEdgeEvent", 0);
    static constexpr Vixen::EventBus::EventCategory CATEGORY =
        Vixen::EventBus::EventCategory::ApplicationState;

    std::string edgeId;
    FlowStateId from;
    FlowStateId to;
    FlowTriggerCause cause;

    AppFlowEdgeEvent(Vixen::EventBus::SenderID sender, std::string edge,
                     FlowStateId source, FlowStateId target, FlowTriggerCause trigger)
        : BaseEventMessage(CATEGORY, TYPE, sender), edgeId(std::move(edge)), from(source),
          to(target), cause(std::move(trigger)) {}
};

struct AppFlowDiagnosticEvent : public Vixen::EventBus::BaseEventMessage {
    static constexpr Vixen::EventBus::MessageType TYPE =
        Vixen::EventBus::detail::StableMessageTypeId("Vixen.AppFlow.AppFlowDiagnosticEvent", 0);
    static constexpr Vixen::EventBus::EventCategory CATEGORY =
        Vixen::EventBus::EventCategory::ApplicationState;

    std::string message;
    AppFlowDiagnosticEvent(Vixen::EventBus::SenderID sender, std::string diagnostic)
        : BaseEventMessage(CATEGORY, TYPE, sender), message(std::move(diagnostic)) {}
};

// Retained for action/undo notifications and compatibility. StateChanged accompanies the
// richer AppFlowEdgeEvent on successful transitions.
struct AppFlowChangedEvent : public Vixen::EventBus::BaseEventMessage {
    static constexpr Vixen::EventBus::MessageType TYPE =
        Vixen::EventBus::detail::StableMessageTypeId("Vixen.AppFlow.AppFlowChangedEvent", 0);
    static constexpr Vixen::EventBus::EventCategory CATEGORY =
        Vixen::EventBus::EventCategory::ApplicationState;

    enum class Kind { StateChanged, ActionApplied, ActionUndone, ActionRedone };
    Kind kind;
    FlowStateId state;
    FlowActionId action;
    uint32_t group;

    AppFlowChangedEvent(Vixen::EventBus::SenderID sender, Kind k,
                        FlowStateId s, FlowActionId a, uint32_t g)
        : BaseEventMessage(CATEGORY, TYPE, sender), kind(k), state(s), action(a), group(g) {}
};
} // namespace Vixen::AppFlow
