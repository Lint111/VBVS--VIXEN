#pragma once

#include "EventBus/Message.h"
#include "EventBus/MessageBus.h"

namespace Vixen::EventTypes {

/**
 * @brief Render pause/resume event for swapchain recreation or resource reallocation
 *
 * Published when rendering needs to be temporarily paused (e.g., during swapchain recreation)
 * and resumed when the operation completes.
 */
struct RenderPauseEvent : public EventBus::BaseEventMessage {
    static constexpr EventBus::MessageType TYPE = 103;
    static constexpr EventBus::EventCategory CATEGORY = EventBus::EventCategory::GraphManagement;

    enum class Reason {
        SwapChainRecreation,
        ResourceReallocation
    };

    enum class Action {
        PAUSE_START,
        PAUSE_END
    };

    Reason pauseReason;
    Action pauseAction;

    RenderPauseEvent(EventBus::SenderID sender, Reason reason, Action action)
        : BaseEventMessage(CATEGORY, TYPE, sender)
        , pauseReason(reason)
        , pauseAction(action) {}
};

/**
 * @brief Window resized event
 *
 * Published when the window dimensions change, triggering render graph recompilation.
 */
struct WindowResizedMessage : public EventBus::BaseEventMessage {
    static constexpr EventBus::MessageType TYPE = 104;
    static constexpr EventBus::EventCategory FLAGS =
        EventBus::EventCategory::ResourceInvalidation |
        EventBus::EventCategory::WindowResize;

    uint32_t newWidth;
    uint32_t newHeight;

    WindowResizedMessage(EventBus::SenderID sender, uint32_t w, uint32_t h)
        : BaseEventMessage(FLAGS, TYPE, sender)
        , newWidth(w)
        , newHeight(h) {}
};

/**
 * @brief Shader file changed - triggers pipeline recreation
 */
struct ShaderReloadedMessage : public EventBus::BaseEventMessage {
    static constexpr EventBus::MessageType TYPE = 105;
    static constexpr EventBus::EventCategory FLAGS =
        EventBus::EventCategory::ResourceInvalidation |
        EventBus::EventCategory::ShaderHotReload;

    std::string shaderPath;

    ShaderReloadedMessage(EventBus::SenderID sender, std::string path)
        : BaseEventMessage(FLAGS, TYPE, sender)
        , shaderPath(std::move(path)) {}
};

/**
 * @brief Cleanup requested event
 *
 * Published when a component requests cleanup of resources.
 */
struct CleanupRequestedMessage : public EventBus::BaseEventMessage {
    static constexpr EventBus::MessageType TYPE = 106;
    static constexpr EventBus::EventCategory CATEGORY = EventBus::EventCategory::CleanupRequest;

    uint32_t requestId;

    CleanupRequestedMessage(EventBus::SenderID sender, uint32_t id = 0)
        : BaseEventMessage(CATEGORY, TYPE, sender)
        , requestId(id) {}
};

/**
 * @brief Cleanup completed event
 *
 * Published when cleanup operation finishes.
 */
struct CleanupCompletedMessage : public EventBus::BaseEventMessage {
    static constexpr EventBus::MessageType TYPE = 107;
    static constexpr EventBus::EventCategory CATEGORY = EventBus::EventCategory::CleanupRequest;

    uint32_t cleanedCount;

    CleanupCompletedMessage(EventBus::SenderID sender, uint32_t count = 0)
        : BaseEventMessage(CATEGORY, TYPE, sender)
        , cleanedCount(count) {}
};

} // namespace Vixen::EventTypes