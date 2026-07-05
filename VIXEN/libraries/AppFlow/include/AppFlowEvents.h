#pragma once
#include "Message.h"                 // EventBus BaseEventMessage
#include "generated/AppFlow.g.h"     // FlowStateId, FlowActionId
namespace Vixen::AppFlow {
using ::Vixen::AppFlow::Generated::FlowStateId;
using ::Vixen::AppFlow::Generated::FlowActionId;

struct AppFlowChangedEvent : public Vixen::EventBus::BaseEventMessage {
    static constexpr Vixen::EventBus::MessageType TYPE = AUTO_MESSAGE_TYPE();
    static constexpr Vixen::EventBus::EventCategory CATEGORY =
        Vixen::EventBus::EventCategory::ApplicationState;

    enum class Kind { StateChanged, ActionApplied, ActionUndone, ActionRedone };
    Kind kind;
    FlowStateId  state;
    FlowActionId action;
    uint32_t     group;

    AppFlowChangedEvent(Vixen::EventBus::SenderID sender, Kind k,
                        FlowStateId s, FlowActionId a, uint32_t g)
        : BaseEventMessage(CATEGORY, TYPE, sender), kind(k), state(s), action(a), group(g) {}
};
} // namespace Vixen::AppFlow
