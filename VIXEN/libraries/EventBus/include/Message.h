#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <chrono>
#include <algorithm>
#include <vector>

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
// Auto-Incrementing Message Type Counter (Compile-Time)
// ============================================================================

/**
 * @brief Compile-time counter for automatic message type ID assignment
 *
 * Usage in message structs:
 * ```cpp
 * struct MyMessage : public BaseEventMessage {
 *     static constexpr MessageType TYPE = NextMessageType<__COUNTER__>::value;
 *     // ... rest of message definition
 * };
 * ```
 *
 * This prevents manual ID collisions by auto-incrementing from a base offset.
 */
namespace detail {
    // Base offset for auto-generated IDs (start at 1000 to avoid manual IDs)
    constexpr MessageType MESSAGE_TYPE_BASE = 1000;

    template<int N>
    struct NextMessageType {
        static constexpr MessageType value = MESSAGE_TYPE_BASE + N;
    };
}

// Helper macro for declaring message types with auto-increment
#define AUTO_MESSAGE_TYPE() (::Vixen::EventBus::detail::MESSAGE_TYPE_BASE + __COUNTER__)

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
    ShaderHotReload     = 1ULL << 33,

    // Frame Lifecycle (40-47)
    FrameLifecycle      = 1ULL << 40,
    FrameStart          = 1ULL << 41,
    FrameEnd            = 1ULL << 42,

    // Budget Management (48-55) - Sprint 6.3
    BudgetManagement    = 1ULL << 48,
    BudgetOverrun       = 1ULL << 49,
    BudgetAvailable     = 1ULL << 50
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
    static constexpr MessageType TYPE = AUTO_MESSAGE_TYPE();
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
    static constexpr MessageType TYPE = AUTO_MESSAGE_TYPE();
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
    static constexpr MessageType TYPE = AUTO_MESSAGE_TYPE();
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
    static constexpr MessageType TYPE = AUTO_MESSAGE_TYPE();  // 103 taken by RenderPauseEvent
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
    static constexpr MessageType TYPE = AUTO_MESSAGE_TYPE();
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

// ============================================================================
// Device Management Events
// ============================================================================

/**
 * @brief Device invalidation event
 *
 * Published when VulkanDevice state changes requiring cache invalidation:
 * - GPU hot-swap (disconnect/reconnect during runtime)
 * - Driver reset (TDR recovery)
 * - Device recompilation/recreation
 *
 * Subscribers (e.g., MainCacher) clear device-dependent caches automatically
 */
struct DeviceInvalidationEvent : public BaseEventMessage {
    static constexpr MessageType TYPE = AUTO_MESSAGE_TYPE();
    static constexpr EventCategory CATEGORY = EventCategory::ResourceInvalidation;

    enum class Reason {
        DeviceDisconnected,   // GPU physically removed/disconnected
        DriverReset,          // TDR or driver crash recovery
        DeviceRecompilation,  // DeviceNode recompiled (rare edge case)
        ManualInvalidation    // Explicit cache clear request
    };

    void* deviceHandle;  // VulkanDevice* (opaque to avoid header dependency)
    Reason reason;
    std::string deviceDescription;  // Human-readable device info

    DeviceInvalidationEvent(SenderID sender, void* device, Reason invalidationReason, const std::string& desc = "")
        : BaseEventMessage(CATEGORY, TYPE, sender)
        , deviceHandle(device)
        , reason(invalidationReason)
        , deviceDescription(desc) {}
};

/**
 * @brief Individual device metadata
 *
 * Describes capabilities of a single physical device
 */
struct DeviceInfo {
    // Vulkan API and SPIR-V versions
    uint32_t vulkanApiVersion;      // e.g., VK_API_VERSION_1_3
    uint32_t maxSpirvVersion;       // Maximum supported SPIR-V version (encoded: (major << 16) | (minor << 8))

    // Memory information
    uint64_t dedicatedMemoryMB;     // Device-local memory (MB)
    uint64_t sharedMemoryMB;        // Host-visible memory (MB)

    // Device identification
    std::string deviceName;         // GPU name (e.g., "NVIDIA GeForce RTX 3060")
    uint32_t vendorID;              // Vendor ID (0x10DE = NVIDIA, 0x1002 = AMD, 0x8086 = Intel)
    uint32_t deviceID;              // Device ID
    bool isDiscreteGPU;             // true if discrete GPU, false if integrated

    // Device index in system
    uint32_t deviceIndex;           // Index in availableGPUs array

    // Helper to convert to ShaderManagement shorthand versions
    int GetVulkanVersionShorthand() const {
        // Extract major.minor from VK_VERSION_MAJOR/MINOR
        uint32_t major = (vulkanApiVersion >> 22) & 0x3FF;
        uint32_t minor = (vulkanApiVersion >> 12) & 0x3FF;
        return static_cast<int>(major * 100 + minor * 10);  // e.g., 1.3 -> 130
    }

    int GetSpirvVersionShorthand() const {
        // maxSpirvVersion is already encoded as (major << 16) | (minor << 8)
        uint32_t major = (maxSpirvVersion >> 16) & 0xFF;
        uint32_t minor = (maxSpirvVersion >> 8) & 0xFF;
        return static_cast<int>(major * 100 + minor * 10);  // e.g., 1.6 -> 160
    }
};

/**
 * @brief Device metadata event
 *
 * Published after device enumeration with ALL available device capabilities.
 * Contains metadata for every detected GPU plus which one was selected.
 *
 * Subscribers use this to configure their systems appropriately:
 * - ShaderLibraryNode: Validates/recompiles shaders for selected device capabilities
 * - Memory allocators: Configure based on memory limits
 * - Feature systems: Enable/disable features based on device support
 * - Multi-GPU managers: Know all available GPUs for load balancing
 */
struct DeviceMetadataEvent : public BaseEventMessage {
    static constexpr MessageType TYPE = AUTO_MESSAGE_TYPE();
    static constexpr EventCategory CATEGORY = EventCategory::System;

    // All detected devices
    std::vector<DeviceInfo> availableDevices;

    // Index of selected device in availableDevices array
    uint32_t selectedDeviceIndex;

    // Device handle for the SELECTED device (opaque pointer to VulkanDevice)
    void* selectedDeviceHandle;     // VulkanDevice* (for systems that need direct access)

    DeviceMetadataEvent(
        SenderID sender,
        std::vector<DeviceInfo> devices,
        uint32_t selectedIndex,
        void* devHandle
    )
        : BaseEventMessage(CATEGORY, TYPE, sender)
        , availableDevices(std::move(devices))
        , selectedDeviceIndex(selectedIndex)
        , selectedDeviceHandle(devHandle) {}

    // Helper to get selected device info
    const DeviceInfo& GetSelectedDevice() const {
        return availableDevices[selectedDeviceIndex];
    }

    // Helper to count devices by type
    size_t GetDiscreteGPUCount() const {
        return std::count_if(availableDevices.begin(), availableDevices.end(),
            [](const DeviceInfo& d) { return d.isDiscreteGPU; });
    }

    size_t GetIntegratedGPUCount() const {
        return std::count_if(availableDevices.begin(), availableDevices.end(),
            [](const DeviceInfo& d) { return !d.isDiscreteGPU; });
    }
};

// ============================================================================
// Frame Lifecycle Events
// ============================================================================

/**
 * @brief Frame start event
 *
 * Published at the beginning of each frame by RenderGraph.
 * Systems subscribe to capture allocation snapshots, reset per-frame counters, etc.
 *
 * Usage:
 * - DeviceBudgetManager: Captures allocation snapshot for delta tracking
 * - StagingBufferPool: Resets per-frame statistics
 * - Profiler: Starts frame timing
 */
struct FrameStartEvent : public BaseEventMessage {
    static constexpr MessageType TYPE = AUTO_MESSAGE_TYPE();
    static constexpr EventCategory CATEGORY = EventCategory::FrameLifecycle | EventCategory::FrameStart;

    uint64_t frameNumber;

    FrameStartEvent(SenderID sender, uint64_t frame)
        : BaseEventMessage(CATEGORY, TYPE, sender)
        , frameNumber(frame) {}
};

/**
 * @brief Frame end event
 *
 * Published at the end of each frame by RenderGraph.
 * Systems subscribe to calculate deltas, log statistics, etc.
 *
 * Usage:
 * - DeviceBudgetManager: Calculates frame allocation delta
 * - StagingBufferPool: Reports chunk usage statistics
 * - Profiler: Ends frame timing
 */
struct FrameEndEvent : public BaseEventMessage {
    static constexpr MessageType TYPE = AUTO_MESSAGE_TYPE();
    static constexpr EventCategory CATEGORY = EventCategory::FrameLifecycle | EventCategory::FrameEnd;

    uint64_t frameNumber;

    FrameEndEvent(SenderID sender, uint64_t frame)
        : BaseEventMessage(CATEGORY, TYPE, sender)
        , frameNumber(frame) {}
};

// ============================================================================
// Budget Management Events (Sprint 6.3)
// ============================================================================

/**
 * @brief Budget overrun event
 *
 * Published by TimelineCapacityTracker when frame utilization exceeds budget.
 * TaskProfileRegistry subscribes to this event to reduce workload.
 *
 * This decouples capacity tracking from pressure valve adjustment:
 * - TimelineCapacityTracker measures and publishes
 * - TaskProfileRegistry reacts autonomously
 * - RenderGraph no longer mediates between them
 */
struct BudgetOverrunEvent : public BaseEventMessage {
    static constexpr MessageType TYPE = AUTO_MESSAGE_TYPE();
    static constexpr EventCategory CATEGORY = EventCategory::BudgetManagement | EventCategory::BudgetOverrun;

    uint64_t frameNumber;
    float utilization;       // 0.0-1.0+ (>1.0 means over budget)
    uint64_t budgetNs;       // Frame budget in nanoseconds
    uint64_t actualNs;       // Actual frame time in nanoseconds

    BudgetOverrunEvent(SenderID sender, uint64_t frame, float util, uint64_t budget, uint64_t actual)
        : BaseEventMessage(CATEGORY, TYPE, sender)
        , frameNumber(frame)
        , utilization(util)
        , budgetNs(budget)
        , actualNs(actual) {}
};

/**
 * @brief Budget available event
 *
 * Published by TimelineCapacityTracker when frame utilization is below threshold.
 * TaskProfileRegistry subscribes to this event to increase workload.
 *
 * Threshold is typically 80% to leave headroom for variance.
 */
struct BudgetAvailableEvent : public BaseEventMessage {
    static constexpr MessageType TYPE = AUTO_MESSAGE_TYPE();
    static constexpr EventCategory CATEGORY = EventCategory::BudgetManagement | EventCategory::BudgetAvailable;

    uint64_t frameNumber;
    float utilization;       // 0.0-1.0 (current utilization)
    float threshold;         // Threshold below which this event fires
    uint64_t remainingNs;    // Remaining budget in nanoseconds

    BudgetAvailableEvent(SenderID sender, uint64_t frame, float util, float thresh, uint64_t remaining)
        : BaseEventMessage(CATEGORY, TYPE, sender)
        , frameNumber(frame)
        , utilization(util)
        , threshold(thresh)
        , remainingNs(remaining) {}
};

} // namespace Vixen::EventBus
