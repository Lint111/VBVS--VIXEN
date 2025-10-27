#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <chrono>

namespace Vixen::EventBus {

/**
 * @brief Unique identifier for message sender
 * 
 * Can represent nodes, systems, threads, etc.
 * Value 0 reserved for "system" (no specific sender).
 */
using SenderID = uint64_t;

/**
 * @brief Message type identifier for filtering
 * 
 * Users define their own message type enums and cast to MessageType.
 * Base types (0-99 reserved):
 * - 0: Generic message
 * - 1: Worker thread result
 */
using MessageType = uint32_t;

// ============================================================================
// Event Category Bit Flags (64-bit)
// ============================================================================
enum class EventCategory : uint64_t {
    None                = 0ULL,

    // System (0-7)
    System              = 1ULL << 0,
	Debug               = 1ULL << 1,

    // Resource Invalidation (8-15)
    ResourceInvalidation = 1ULL << 8,
    WindowResize        = 1ULL << 9,
    SwapChainInvalid    = 1ULL << 10,
    PipelineInvalid     = 1ULL << 11,
    DescriptorInvalid   = 1ULL << 12,
    FramebufferInvalid  = 1ULL << 13,
    TextureReload       = 1ULL << 14,

    // Application State (16-23)
    ApplicationState    = 1ULL << 16,
    CameraUpdate        = 1ULL << 17,
    LightingChange      = 1ULL << 18,
    SceneChange         = 1ULL << 19,
    MaterialChange      = 1ULL << 20,

    // Graph Management (24-31)
    GraphManagement     = 1ULL << 24,
    CleanupRequest      = 1ULL << 25,
    GraphRecompile      = 1ULL << 26,

    // Shader Events (32-39)
    ShaderEvents        = 1ULL << 32,
    ShaderHotReload     = 1ULL << 33
};

constexpr EventCategory operator|(EventCategory a, EventCategory b) {
    return static_cast<EventCategory>(static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
}

constexpr EventCategory operator&(EventCategory a, EventCategory b) {
    return static_cast<EventCategory>(static_cast<uint64_t>(a) & static_cast<uint64_t>(b));
}

constexpr EventCategory operator~(EventCategory a) {
    return static_cast<EventCategory>(~static_cast<uint64_t>(a));
}

constexpr bool HasCategory(EventCategory flags, EventCategory category) {
    return (static_cast<uint64_t>(flags) & static_cast<uint64_t>(category)) != 0ULL;
}

/**
 * @brief Base message class
 * 
 * All messages inherit from this. Provides:
 * - Sender identification
 * - Timestamp for ordering
 * - Type ID for filtering
 * 
 * Usage:
 * ```cpp
 * struct MyMessage : public Message {
 *     MyMessage(SenderID sender) : Message(sender, MY_MESSAGE_TYPE) {}
 *     std::string data;
 * };
 * ```
 */
// New base message used throughout the system. Contains category flags
// (for fast filtering) and the existing type/sender/timestamp fields.
struct BaseEventMessage {
    EventCategory categoryFlags;
    MessageType type;
    SenderID sender;
    std::chrono::steady_clock::time_point timestamp;

    BaseEventMessage(EventCategory flags, MessageType msgType, SenderID senderId)
        : categoryFlags(flags)
        , type(msgType)
        , sender(senderId)
        , timestamp(std::chrono::steady_clock::now()) {}

    virtual ~BaseEventMessage() = default;

    double GetAgeSeconds() const {
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(now - timestamp);
        return duration.count() / 1000000.0;
    }

    bool HasCategory(EventCategory cat) const {
        return EventBus::HasCategory(categoryFlags, cat);
    }
};

// Keep the legacy Message type for backward compatibility. It maps to
// BaseEventMessage with default category = System.
struct Message : public BaseEventMessage {
    Message(SenderID senderID, MessageType msgType)
        : BaseEventMessage(EventCategory::System, msgType, senderID) {}
};

// ============================================================================
// Common Message Types
// ============================================================================

/**
 * @brief Generic text message (debugging, logging)
 */
struct TextMessage : public Message {
    static constexpr MessageType TYPE = 0;

    std::string content;

    TextMessage(SenderID sender, std::string text)
        : Message(sender, TYPE)
        , content(std::move(text)) {}
};

/**
 * @brief Worker thread result message
 * 
 * Automatically emitted by WorkerThreadBridge when async work completes.
 */
struct WorkerResultMessage : public Message {
    static constexpr MessageType TYPE = 1;

    uint64_t workID;      // Correlate with original request
    bool success;
    std::string error;    // Empty if success

    WorkerResultMessage(SenderID sender, uint64_t id, bool succeeded, std::string err = "")
        : Message(sender, TYPE)
        , workID(id)
        , success(succeeded)
        , error(std::move(err)) {}
};

// ============================================================================
// Window and SwapChain Event Messages
// ============================================================================

/**
 * @brief Window resize event
 * 
 * Published when window dimensions change (resize, maximize, restore)
 */
struct WindowResizeEvent : public BaseEventMessage {
    static constexpr MessageType TYPE = 100;
    static constexpr EventCategory CATEGORY = EventCategory::WindowResize;

    uint32_t newWidth;
    uint32_t newHeight;
    bool isMinimized;

    WindowResizeEvent(SenderID sender, uint32_t width, uint32_t height, bool minimized = false)
        : BaseEventMessage(CATEGORY, TYPE, sender)
        , newWidth(width)
        , newHeight(height)
        , isMinimized(minimized) {}
};

/**
 * @brief Window state change event
 * 
 * Published when window state changes (minimize, maximize, restore, focus)
 */
struct WindowStateChangeEvent : public BaseEventMessage {
    static constexpr MessageType TYPE = 101;
    static constexpr EventCategory CATEGORY = EventCategory::ApplicationState;

    enum class State {
        Minimized,
        Maximized,
        Restored,
        Focused,
        Unfocused
    };

    State newState;

    WindowStateChangeEvent(SenderID sender, State state)
        : BaseEventMessage(CATEGORY, TYPE, sender)
        , newState(state) {}
};

/**
 * @brief Window close event
 *
 * Published when user requests to close the application (X button)
 * Systems should subscribe to this event to perform graceful shutdown
 */
struct WindowCloseEvent : public BaseEventMessage {
    static constexpr MessageType TYPE = 102;
    static constexpr EventCategory CATEGORY = EventCategory::ApplicationState;

    WindowCloseEvent(SenderID sender)
        : BaseEventMessage(CATEGORY, TYPE, sender) {}
};

/**
 * @brief Shutdown acknowledgment event
 *
 * Published by systems when they have completed their shutdown sequence
 * Application tracks these to know when it's safe to destroy the window
 */
struct ShutdownAckEvent : public BaseEventMessage {
    static constexpr MessageType TYPE = 104;  // 103 taken by RenderPauseEvent
    static constexpr EventCategory CATEGORY = EventCategory::ApplicationState;

    std::string systemName;  // Name of system that acknowledged shutdown

    ShutdownAckEvent(SenderID sender, const std::string& name)
        : BaseEventMessage(CATEGORY, TYPE, sender), systemName(name) {}
};

/**
 * @brief Render pause event
 * 
 * Published by SwapChainNode during compilation/recreation to prevent
 * accessing resources that may be temporarily unavailable
 */
struct RenderPauseEvent : public BaseEventMessage {
    static constexpr MessageType TYPE = 103;
    static constexpr EventCategory CATEGORY = EventCategory::GraphManagement;

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

    RenderPauseEvent(SenderID sender, Reason reason, Action action)
        : BaseEventMessage(CATEGORY, TYPE, sender)
        , pauseReason(reason)
        , pauseAction(action) {}
};

} // namespace EventBus
