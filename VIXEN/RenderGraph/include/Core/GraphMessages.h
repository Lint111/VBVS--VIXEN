#pragma once

#include "EventBus/Message.h"
#include <string>
#include <vector>
#include <optional>

namespace Vixen::RenderGraph {

/**
 * @brief Message types for RenderGraph events
 */
enum class GraphMessageType : EventBus::MessageType {
    // Cleanup events (100-199)
    CleanupRequested = 100,
    CleanupCompleted = 101,
    
    // Recompilation events (200-299)
    RecompileRequested = 200,
    RecompileCompleted = 201,
    
    // Resource invalidation (300-399)
    WindowResized = 300,
    SwapChainInvalidated = 301,
    ShaderReloaded = 302,
    TextureReloaded = 303
};

/**
 * @brief Cleanup scope specification
 */
enum class CleanupScope {
    Specific,      // Clean specific node + orphaned dependencies
    ByTag,         // Clean all nodes with matching tag
    ByType,        // Clean all nodes of specific type
    Full           // Full graph cleanup
};

/**
 * @brief Request cleanup of graph nodes
 * 
 * Nodes subscribe to this event and determine if they should cleanup based on:
 * - Specific instance name match
 * - Tag match (e.g., "shadow-maps", "post-process")
 * - Type match (e.g., all "GeometryPass" nodes)
 * - Full cleanup flag
 * 
 * Example:
 * ```cpp
 * // Cleanup specific node
 * auto msg = std::make_unique<CleanupRequestedMessage>(0, "MainPass");
 * bus.Publish(std::move(msg));
 * 
 * // Cleanup all shadow map nodes
 * auto msg = std::make_unique<CleanupRequestedMessage>(0);
 * msg->scope = CleanupScope::ByTag;
 * msg->tag = "shadow-maps";
 * bus.Publish(std::move(msg));
 * ```
 */
struct CleanupRequestedMessage : public EventBus::BaseEventMessage {
    static constexpr EventBus::MessageType TYPE = 
        static_cast<EventBus::MessageType>(GraphMessageType::CleanupRequested);
    static constexpr EventBus::EventCategory FLAGS = 
        EventBus::EventCategory::GraphManagement | 
        EventBus::EventCategory::CleanupRequest;
    
    CleanupScope scope = CleanupScope::Specific;
    
    // For Specific scope
    std::optional<std::string> targetNodeName;
    
    // For ByTag scope
    std::optional<std::string> tag;
    
    // For ByType scope
    std::optional<std::string> typeName;
    
    // Reason for cleanup (debugging/logging)
    std::string reason;
    
    CleanupRequestedMessage(EventBus::SenderID sender)
        : BaseEventMessage(FLAGS, TYPE, sender) {}
    
    // Convenience constructors
    CleanupRequestedMessage(EventBus::SenderID sender, const std::string& nodeName)
        : BaseEventMessage(FLAGS, TYPE, sender)
        , scope(CleanupScope::Specific)
        , targetNodeName(nodeName) {}
    
    static std::unique_ptr<CleanupRequestedMessage> ByTag(
        EventBus::SenderID sender,
        const std::string& tagName,
        const std::string& cleanupReason = "")
    {
        auto msg = std::make_unique<CleanupRequestedMessage>(sender);
        msg->scope = CleanupScope::ByTag;
        msg->tag = tagName;
        msg->reason = cleanupReason;
        return msg;
    }
    
    static std::unique_ptr<CleanupRequestedMessage> ByType(
        EventBus::SenderID sender,
        const std::string& nodeTypeName,
        const std::string& cleanupReason = "")
    {
        auto msg = std::make_unique<CleanupRequestedMessage>(sender);
        msg->scope = CleanupScope::ByType;
        msg->typeName = nodeTypeName;
        msg->reason = cleanupReason;
        return msg;
    }
    
    static std::unique_ptr<CleanupRequestedMessage> Full(
        EventBus::SenderID sender,
        const std::string& cleanupReason = "")
    {
        auto msg = std::make_unique<CleanupRequestedMessage>(sender);
        msg->scope = CleanupScope::Full;
        msg->reason = cleanupReason;
        return msg;
    }
};

/**
 * @brief Notification that cleanup completed
 * 
 * Published after CleanupRequested processing finishes.
 * Contains list of nodes that were cleaned.
 */
struct CleanupCompletedMessage : public EventBus::BaseEventMessage {
    static constexpr EventBus::MessageType TYPE = 
        static_cast<EventBus::MessageType>(GraphMessageType::CleanupCompleted);
    static constexpr EventBus::EventCategory FLAGS = 
        EventBus::EventCategory::GraphManagement;
    
    std::vector<std::string> cleanedNodes;
    size_t cleanedCount = 0;
    
    CleanupCompletedMessage(EventBus::SenderID sender)
        : BaseEventMessage(FLAGS, TYPE, sender) {}
};

/**
 * @brief Request node recompilation
 * 
 * Triggers cleanup followed by recompilation. Useful for:
 * - Shader hot-reload
 * - Window resize (swapchain recreation)
 * - Dynamic parameter changes
 */
struct RecompileRequestedMessage : public EventBus::BaseEventMessage {
    static constexpr EventBus::MessageType TYPE = 
        static_cast<EventBus::MessageType>(GraphMessageType::RecompileRequested);
    static constexpr EventBus::EventCategory FLAGS = 
        EventBus::EventCategory::GraphManagement | 
        EventBus::EventCategory::GraphRecompile;
    
    std::vector<std::string> nodeNames;
    std::string reason;
    
    RecompileRequestedMessage(EventBus::SenderID sender)
        : BaseEventMessage(FLAGS, TYPE, sender) {}
};

/**
 * @brief Window resized - triggers swapchain + framebuffer recreation
 */
struct WindowResizedMessage : public EventBus::BaseEventMessage {
    static constexpr EventBus::MessageType TYPE = 
        static_cast<EventBus::MessageType>(GraphMessageType::WindowResized);
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
    static constexpr EventBus::MessageType TYPE = 
        static_cast<EventBus::MessageType>(GraphMessageType::ShaderReloaded);
    static constexpr EventBus::EventCategory FLAGS = 
        EventBus::EventCategory::ShaderEvents | 
        EventBus::EventCategory::ShaderHotReload | 
        EventBus::EventCategory::ResourceInvalidation | 
        EventBus::EventCategory::PipelineInvalid;
    
    std::string shaderPath;
    
    ShaderReloadedMessage(EventBus::SenderID sender, std::string path)
        : BaseEventMessage(FLAGS, TYPE, sender)
        , shaderPath(std::move(path)) {}
};

} // namespace Vixen::RenderGraph
