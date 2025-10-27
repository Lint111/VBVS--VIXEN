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

/**
 * @brief Request device synchronization
 *
 * Triggers vkDeviceWaitIdle on specified devices to ensure GPU has finished
 * using resources before they are destroyed/recreated.
 *
 * Typically published immediately before cleanup/recompilation to ensure safety.
 *
 * Example:
 * ```cpp
 * // Wait for all devices
 * auto msg = std::make_unique<DeviceSyncRequestedMessage>(0);
 * msg->scope = DeviceSyncRequestedMessage::Scope::AllDevices;
 * bus.PublishImmediate(*msg);  // Synchronous
 * ```
 */
struct DeviceSyncRequestedMessage : public EventBus::BaseEventMessage {
    static constexpr EventBus::MessageType TYPE = 108;
    static constexpr EventBus::EventCategory FLAGS =
        EventBus::EventCategory::GraphManagement |
        EventBus::EventCategory::Debug;

    enum class Scope {
        AllDevices,      // Wait for all devices in graph
        SpecificNodes,   // Wait for devices used by specific nodes
        SpecificDevices  // Wait for specific VkDevice handles
    };

    Scope scope = Scope::AllDevices;

    // For SpecificNodes scope
    std::vector<std::string> nodeNames;

    // For SpecificDevices scope
    std::vector<VkDevice> devices;

    // Reason for sync (debugging/logging)
    std::string reason;

    DeviceSyncRequestedMessage(EventBus::SenderID sender)
        : BaseEventMessage(FLAGS, TYPE, sender) {}

    static std::unique_ptr<DeviceSyncRequestedMessage> AllDevices(
        EventBus::SenderID sender,
        const std::string& syncReason = "")
    {
        auto msg = std::make_unique<DeviceSyncRequestedMessage>(sender);
        msg->scope = Scope::AllDevices;
        msg->reason = syncReason;
        return msg;
    }

    static std::unique_ptr<DeviceSyncRequestedMessage> ForNodes(
        EventBus::SenderID sender,
        const std::vector<std::string>& nodes,
        const std::string& syncReason = "")
    {
        auto msg = std::make_unique<DeviceSyncRequestedMessage>(sender);
        msg->scope = Scope::SpecificNodes;
        msg->nodeNames = nodes;
        msg->reason = syncReason;
        return msg;
    }
};

/**
 * @brief Notification that device synchronization completed
 *
 * Published after DeviceSyncRequested processing finishes.
 * Contains statistics about sync duration for performance monitoring.
 */
struct DeviceSyncCompletedMessage : public EventBus::BaseEventMessage {
    static constexpr EventBus::MessageType TYPE = 109;
    static constexpr EventBus::EventCategory CATEGORY = EventBus::EventCategory::GraphManagement;

    size_t deviceCount = 0;
    std::chrono::milliseconds waitTime{0};

    DeviceSyncCompletedMessage(EventBus::SenderID sender, size_t count, std::chrono::milliseconds time)
        : BaseEventMessage(CATEGORY, TYPE, sender)
        , deviceCount(count)
        , waitTime(time) {}
};

} // namespace Vixen::EventTypes