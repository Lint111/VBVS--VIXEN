#pragma once

/**
 * @file DescriptorResourceTracker.h
 * @brief Debug tracking system for descriptor resources flowing through the render graph
 *
 * This system provides comprehensive tracking of:
 * - Resource creation (where and when)
 * - Handle storage (binding assignment)
 * - Handle extraction (when and where handles are retrieved)
 * - Handle mutations (value changes over time)
 *
 * Enable via VIXEN_DEBUG_DESCRIPTOR_TRACKING define.
 * In Release builds, all tracking code compiles to no-ops.
 */

#include <cstdint>
#include <string>
#include <string_view>
#include <source_location>
#include <chrono>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <atomic>
#include <iostream>
#include <sstream>
#include <iomanip>

// Enable tracking in Debug builds by default
#ifndef NDEBUG
#define VIXEN_DEBUG_DESCRIPTOR_TRACKING 1
#endif

namespace Vixen::RenderGraph::Debug {

// ============================================================================
// TRACKING ID SYSTEM
// ============================================================================

/**
 * @brief Unique identifier for tracking resources through the graph
 *
 * Each DescriptorResourceEntry gets a unique trackingId when created,
 * allowing us to follow its journey through the system.
 */
using TrackingId = uint64_t;
constexpr TrackingId InvalidTrackingId = 0;

/**
 * @brief Thread-safe tracking ID generator
 */
inline TrackingId GenerateTrackingId() {
    static std::atomic<TrackingId> nextId{1};
    return nextId.fetch_add(1, std::memory_order_relaxed);
}

// ============================================================================
// LIFECYCLE EVENTS
// ============================================================================

/**
 * @brief Types of events that can occur during resource lifecycle
 */
enum class ResourceEventType : uint8_t {
    Created,            // Resource entry created
    HandleStored,       // VkBuffer/VkImageView/etc stored
    HandleExtracted,    // GetHandle() called
    HandleChanged,      // Handle value changed (mutation)
    BoundToDescriptor,  // Bound to VkDescriptorSet
    Destroyed,          // Resource entry destroyed/cleared
    ExtractorCreated,   // descriptorExtractor_ lambda created
    ExtractorCalled,    // descriptorExtractor_ invoked
};

inline const char* ResourceEventTypeName(ResourceEventType type) {
    switch (type) {
        case ResourceEventType::Created: return "Created";
        case ResourceEventType::HandleStored: return "HandleStored";
        case ResourceEventType::HandleExtracted: return "HandleExtracted";
        case ResourceEventType::HandleChanged: return "HandleChanged";
        case ResourceEventType::BoundToDescriptor: return "BoundToDescriptor";
        case ResourceEventType::Destroyed: return "Destroyed";
        case ResourceEventType::ExtractorCreated: return "ExtractorCreated";
        case ResourceEventType::ExtractorCalled: return "ExtractorCalled";
        default: return "Unknown";
    }
}

/**
 * @brief Single event in a resource's lifecycle
 */
struct ResourceEvent {
    ResourceEventType type;
    TrackingId trackingId;
    uint32_t binding;                              // Shader binding index
    uint64_t handleValue;                          // Raw handle value (for comparison)
    std::string handleTypeName;                    // "VkBuffer", "VkImageView", etc.
    std::string location;                          // Source location (file:line:function)
    std::string nodeName;                          // Node that generated event
    std::chrono::steady_clock::time_point timestamp;
    std::string additionalInfo;                    // Extra context

    std::string ToString() const {
        std::ostringstream oss;
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            timestamp - std::chrono::steady_clock::time_point{}).count();

        oss << "[" << std::setw(12) << elapsed << "us] "
            << "ID=" << std::setw(4) << trackingId << " "
            << std::setw(18) << ResourceEventTypeName(type) << " "
            << "binding=" << std::setw(2) << binding << " "
            << handleTypeName << "=0x" << std::hex << handleValue << std::dec;

        if (!nodeName.empty()) {
            oss << " @" << nodeName;
        }
        if (!additionalInfo.empty()) {
            oss << " (" << additionalInfo << ")";
        }
        oss << " [" << location << "]";

        return oss.str();
    }
};

// ============================================================================
// DEBUG METADATA FOR DESCRIPTOR RESOURCE ENTRY
// ============================================================================

/**
 * @brief Debug metadata attached to each DescriptorResourceEntry
 *
 * Zero-overhead in Release builds (empty struct).
 */
struct DescriptorResourceDebugMetadata {
#if VIXEN_DEBUG_DESCRIPTOR_TRACKING
    TrackingId trackingId = InvalidTrackingId;
    std::string sourceName;                        // Name of source slot/node
    std::string creationLocation;                  // Where the entry was created
    uint64_t originalHandleValue = 0;              // Handle at creation time
    uint64_t lastExtractedValue = 0;               // Last value from GetHandle()
    uint32_t extractionCount = 0;                  // How many times GetHandle() called
    bool wasModified = false;                      // True if handle changed after creation

    void Initialize(std::string_view source, const std::source_location& loc = std::source_location::current()) {
        trackingId = GenerateTrackingId();
        sourceName = source;
        creationLocation = std::string(loc.file_name()) + ":" +
                          std::to_string(loc.line()) + ":" +
                          loc.function_name();
    }

    void RecordOriginalHandle(uint64_t value) {
        originalHandleValue = value;
    }

    void RecordExtraction(uint64_t value) {
        extractionCount++;
        lastExtractedValue = value;
        if (originalHandleValue != 0 && value != originalHandleValue) {
            wasModified = true;
        }
    }
#else
    // Release: zero-size struct
    void Initialize(std::string_view, const std::source_location& = std::source_location::current()) {}
    void RecordOriginalHandle(uint64_t) {}
    void RecordExtraction(uint64_t) {}
#endif
};

// ============================================================================
// CENTRALIZED TRACKING REGISTRY
// ============================================================================

/**
 * @brief Central registry for all resource tracking events
 *
 * Thread-safe singleton that collects all events.
 * Use GetRegistry() to access.
 */
class DescriptorResourceRegistry {
public:
    static DescriptorResourceRegistry& GetRegistry() {
        static DescriptorResourceRegistry instance;
        return instance;
    }

#if VIXEN_DEBUG_DESCRIPTOR_TRACKING
    void RecordEvent(const ResourceEvent& event) {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.push_back(event);

        // Also print immediately for real-time debugging
        if (printEventsImmediately_) {
            std::cout << "[ResourceTracker] " << event.ToString() << std::endl;
        }
    }

    void RecordEvent(ResourceEventType type, TrackingId id, uint32_t binding,
                     uint64_t handleValue, std::string_view handleType,
                     std::string_view nodeName = "",
                     std::string_view additionalInfo = "",
                     const std::source_location& loc = std::source_location::current()) {
        ResourceEvent event;
        event.type = type;
        event.trackingId = id;
        event.binding = binding;
        event.handleValue = handleValue;
        event.handleTypeName = handleType;
        event.location = std::string(loc.file_name()) + ":" +
                        std::to_string(loc.line()) + ":" +
                        loc.function_name();
        event.nodeName = nodeName;
        event.timestamp = std::chrono::steady_clock::now();
        event.additionalInfo = additionalInfo;

        RecordEvent(event);
    }

    // Get all events for a specific tracking ID
    std::vector<ResourceEvent> GetEventsForId(TrackingId id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<ResourceEvent> result;
        for (const auto& event : events_) {
            if (event.trackingId == id) {
                result.push_back(event);
            }
        }
        return result;
    }

    // Get all events for a specific binding
    std::vector<ResourceEvent> GetEventsForBinding(uint32_t binding) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<ResourceEvent> result;
        for (const auto& event : events_) {
            if (event.binding == binding) {
                result.push_back(event);
            }
        }
        return result;
    }

    // Find handle value mismatches (stored vs extracted)
    std::vector<std::pair<ResourceEvent, ResourceEvent>> FindHandleMismatches() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::pair<ResourceEvent, ResourceEvent>> mismatches;

        // Group events by tracking ID
        std::unordered_map<TrackingId, std::vector<const ResourceEvent*>> byId;
        for (const auto& event : events_) {
            byId[event.trackingId].push_back(&event);
        }

        // Check each resource's history
        for (const auto& [id, resourceEvents] : byId) {
            const ResourceEvent* lastStored = nullptr;
            for (const auto* event : resourceEvents) {
                if (event->type == ResourceEventType::HandleStored) {
                    lastStored = event;
                } else if (event->type == ResourceEventType::HandleExtracted && lastStored) {
                    if (event->handleValue != lastStored->handleValue) {
                        mismatches.emplace_back(*lastStored, *event);
                    }
                }
            }
        }
        return mismatches;
    }

    // Dump all events to console
    void DumpAllEvents() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << "\n========== DESCRIPTOR RESOURCE TRACKING DUMP ==========\n";
        std::cout << "Total events: " << events_.size() << "\n\n";

        for (const auto& event : events_) {
            std::cout << event.ToString() << "\n";
        }

        std::cout << "========================================================\n" << std::endl;
    }

    // Dump events filtered by binding
    void DumpEventsForBinding(uint32_t binding) const {
        auto events = GetEventsForBinding(binding);
        std::cout << "\n===== Events for binding " << binding << " (" << events.size() << " events) =====\n";
        for (const auto& event : events) {
            std::cout << event.ToString() << "\n";
        }
        std::cout << "=====================================================\n" << std::endl;
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.clear();
    }

    void SetPrintEventsImmediately(bool print) { printEventsImmediately_ = print; }
    bool GetPrintEventsImmediately() const { return printEventsImmediately_; }

    size_t GetEventCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return events_.size();
    }

private:
    mutable std::mutex mutex_;
    std::vector<ResourceEvent> events_;
    bool printEventsImmediately_ = true;  // Default: print as they happen
#else
    // Release: all operations are no-ops
    void RecordEvent(const ResourceEvent&) {}
    void RecordEvent(ResourceEventType, TrackingId, uint32_t, uint64_t,
                     std::string_view, std::string_view = "", std::string_view = "",
                     const std::source_location& = std::source_location::current()) {}
    std::vector<ResourceEvent> GetEventsForId(TrackingId) const { return {}; }
    std::vector<ResourceEvent> GetEventsForBinding(uint32_t) const { return {}; }
    std::vector<std::pair<ResourceEvent, ResourceEvent>> FindHandleMismatches() const { return {}; }
    void DumpAllEvents() const {}
    void DumpEventsForBinding(uint32_t) const {}
    void Clear() {}
    void SetPrintEventsImmediately(bool) {}
    bool GetPrintEventsImmediately() const { return false; }
    size_t GetEventCount() const { return 0; }
#endif
};

// ============================================================================
// CONVENIENCE MACROS
// ============================================================================

#if VIXEN_DEBUG_DESCRIPTOR_TRACKING

#define TRACK_RESOURCE_CREATED(trackingId, binding, handleValue, handleType, nodeName) \
    ::Vixen::RenderGraph::Debug::DescriptorResourceRegistry::GetRegistry().RecordEvent( \
        ::Vixen::RenderGraph::Debug::ResourceEventType::Created, \
        trackingId, binding, handleValue, handleType, nodeName)

#define TRACK_HANDLE_STORED(trackingId, binding, handleValue, handleType, nodeName) \
    ::Vixen::RenderGraph::Debug::DescriptorResourceRegistry::GetRegistry().RecordEvent( \
        ::Vixen::RenderGraph::Debug::ResourceEventType::HandleStored, \
        trackingId, binding, handleValue, handleType, nodeName)

#define TRACK_HANDLE_EXTRACTED(trackingId, binding, handleValue, handleType, nodeName, info) \
    ::Vixen::RenderGraph::Debug::DescriptorResourceRegistry::GetRegistry().RecordEvent( \
        ::Vixen::RenderGraph::Debug::ResourceEventType::HandleExtracted, \
        trackingId, binding, handleValue, handleType, nodeName, info)

#define TRACK_HANDLE_BOUND(trackingId, binding, handleValue, handleType, nodeName) \
    ::Vixen::RenderGraph::Debug::DescriptorResourceRegistry::GetRegistry().RecordEvent( \
        ::Vixen::RenderGraph::Debug::ResourceEventType::BoundToDescriptor, \
        trackingId, binding, handleValue, handleType, nodeName)

#define TRACK_EXTRACTOR_CREATED(trackingId, binding, nodeName) \
    ::Vixen::RenderGraph::Debug::DescriptorResourceRegistry::GetRegistry().RecordEvent( \
        ::Vixen::RenderGraph::Debug::ResourceEventType::ExtractorCreated, \
        trackingId, binding, 0, "lambda", nodeName)

#define TRACK_EXTRACTOR_CALLED(trackingId, binding, handleValue, handleType, nodeName) \
    ::Vixen::RenderGraph::Debug::DescriptorResourceRegistry::GetRegistry().RecordEvent( \
        ::Vixen::RenderGraph::Debug::ResourceEventType::ExtractorCalled, \
        trackingId, binding, handleValue, handleType, nodeName)

#define DUMP_RESOURCE_TRACKING() \
    ::Vixen::RenderGraph::Debug::DescriptorResourceRegistry::GetRegistry().DumpAllEvents()

#define DUMP_BINDING_TRACKING(binding) \
    ::Vixen::RenderGraph::Debug::DescriptorResourceRegistry::GetRegistry().DumpEventsForBinding(binding)

#define CHECK_HANDLE_MISMATCHES() \
    ::Vixen::RenderGraph::Debug::DescriptorResourceRegistry::GetRegistry().FindHandleMismatches()

#else

#define TRACK_RESOURCE_CREATED(trackingId, binding, handleValue, handleType, nodeName) ((void)0)
#define TRACK_HANDLE_STORED(trackingId, binding, handleValue, handleType, nodeName) ((void)0)
#define TRACK_HANDLE_EXTRACTED(trackingId, binding, handleValue, handleType, nodeName, info) ((void)0)
#define TRACK_HANDLE_BOUND(trackingId, binding, handleValue, handleType, nodeName) ((void)0)
#define TRACK_EXTRACTOR_CREATED(trackingId, binding, nodeName) ((void)0)
#define TRACK_EXTRACTOR_CALLED(trackingId, binding, handleValue, handleType, nodeName) ((void)0)
#define DUMP_RESOURCE_TRACKING() ((void)0)
#define DUMP_BINDING_TRACKING(binding) ((void)0)
#define CHECK_HANDLE_MISMATCHES() std::vector<std::pair<ResourceEvent, ResourceEvent>>{}

#endif

// ============================================================================
// HELPER FOR EXTRACTING HANDLE VALUE AS UINT64
// ============================================================================

/**
 * @brief Extract raw uint64_t value from DescriptorHandleVariant for tracking
 */
template<typename VariantT>
inline uint64_t GetHandleValueForTracking(const VariantT& variant) {
    return std::visit([](auto&& arg) -> uint64_t {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            return 0;
        } else if constexpr (std::is_pointer_v<T>) {
            return reinterpret_cast<uint64_t>(arg);
        } else if constexpr (std::is_same_v<T, VkBuffer> ||
                           std::is_same_v<T, VkImageView> ||
                           std::is_same_v<T, VkSampler> ||
                           std::is_same_v<T, VkBufferView> ||
                           std::is_same_v<T, VkImage> ||
                           std::is_same_v<T, VkAccelerationStructureKHR>) {
            return reinterpret_cast<uint64_t>(arg);
        } else if constexpr (requires { arg.imageView; arg.sampler; }) {
            // ImageSamplerPair - return imageView
            return reinterpret_cast<uint64_t>(arg.imageView);
        } else if constexpr (requires { arg.size(); }) {
            // Vector types - return first element or 0
            return arg.empty() ? 0 : reinterpret_cast<uint64_t>(arg[0]);
        } else {
            return 0;
        }
    }, variant);
}

/**
 * @brief Get type name from DescriptorHandleVariant for tracking
 */
template<typename VariantT>
inline std::string_view GetHandleTypeNameForTracking(const VariantT& variant) {
    return std::visit([](auto&& arg) -> std::string_view {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::monostate>) return "monostate";
        else if constexpr (std::is_same_v<T, VkBuffer>) return "VkBuffer";
        else if constexpr (std::is_same_v<T, VkImageView>) return "VkImageView";
        else if constexpr (std::is_same_v<T, VkSampler>) return "VkSampler";
        else if constexpr (std::is_same_v<T, VkBufferView>) return "VkBufferView";
        else if constexpr (std::is_same_v<T, VkImage>) return "VkImage";
        else if constexpr (std::is_same_v<T, VkAccelerationStructureKHR>) return "VkAccelStruct";
        else if constexpr (requires { arg.imageView; arg.sampler; }) return "ImageSamplerPair";
        else if constexpr (std::is_pointer_v<T>) return "SwapChainPtr";
        else if constexpr (requires { arg.size(); }) return "Vector";
        else return "Unknown";
    }, variant);
}

} // namespace Vixen::RenderGraph::Debug
