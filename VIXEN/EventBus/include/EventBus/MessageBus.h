#pragma once

#include "Message.h"
#include <functional>
#include <vector>
#include <list>
#include <queue>
#include <mutex>
#include <unordered_map>
#include <memory>

class BaseEventMessage;

namespace Vixen::EventBus {

/**
 * @brief Message handler callback signature
 * 
 * Receives message by const reference.
 * Returns true if handled, false if should continue to other subscribers.
 */
using MessageHandler = std::function<bool(const BaseEventMessage&)>;

/**
 * @brief Subscription handle for unsubscribing
 */
using EventSubscriptionID = uint32_t;

/**
 * @brief Core message bus for publish-subscribe messaging
 * 
 * Features:
 * - Type-safe message inheritance
 * - Filtered subscriptions by message type
 * - Queue-based async processing (safe points)
 * - Immediate dispatch option (for time-critical messages)
 * - Thread-safe emission (mutex-protected queue)
 * 
 * Architecture:
 * ```
 * Sender → Publish() → Queue (thread-safe)
 *                         ↓
 *             ProcessMessages() (main thread)
 *                         ↓
 *           Subscribers receive messages
 * ```
 * 
 * Usage:
 * ```cpp
 * MessageBus bus;
 * 
 * // Subscribe
 * SubscriptionID id = bus.Subscribe(MY_MESSAGE_TYPE, [](const Message& msg) {
 *     auto& myMsg = static_cast<const MyMessage&>(msg);
 *     HandleMyMessage(myMsg);
 *     return true; // Handled
 * });
 * 
 * // Publish
 * auto msg = std::make_unique<MyMessage>(senderID);
 * msg->data = "Hello";
 * bus.Publish(std::move(msg));
 * 
 * // Process (once per frame)
 * bus.ProcessMessages();
 * 
 * // Unsubscribe
 * bus.Unsubscribe(id);
 * ```
 */
class MessageBus {
public:
    MessageBus();
    ~MessageBus();

    // ========================================================================
    // Subscription Management
    // ========================================================================

    /**
     * @brief Subscribe to specific message type
     * 
     * @param type Message type to filter (use MyMessage::TYPE)
     * @param handler Callback receiving messages
     * @return Subscription ID for unsubscribing
     */
    EventSubscriptionID Subscribe(MessageType type, MessageHandler handler);

    /**
     * @brief Subscribe to ALL message types
     * 
     * @param handler Callback receiving all messages
     * @return Subscription ID
     */
    EventSubscriptionID SubscribeAll(MessageHandler handler);

    /**
     * @brief Subscribe to messages by category flags (bit flags)
     *
     * Subscriber will receive messages whose categoryFlags match the provided
     * category (HasAny semantics). Returns a SubscriptionID.
     */
    EventSubscriptionID SubscribeCategory(EventCategory category, MessageHandler handler);

    /**
     * @brief Subscribe to multiple categories (bitmask)
     */
    EventSubscriptionID SubscribeCategories(EventCategory categories, MessageHandler handler);

    /**
     * @brief Unsubscribe from messages
     * 
     * @param id Subscription ID from Subscribe()
     */
    void Unsubscribe(EventSubscriptionID id);

    /**
     * @brief Unsubscribe all handlers
     */
    void UnsubscribeAll();

    // ========================================================================
    // Message Publishing
    // ========================================================================

    /**
     * @brief Publish message for async processing
     * 
     * Thread-safe. Message queued and dispatched during ProcessMessages().
     * 
     * @param message Unique pointer to message (ownership transferred)
     */
    void Publish(std::unique_ptr<BaseEventMessage> message);

    /**
     * @brief Publish message immediately (synchronous)
     * 
     * Dispatches to subscribers now, bypassing queue.
     * Use sparingly - prefer Publish() for normal flow.
     * 
     * @param message Message reference (not transferred)
     */
    void PublishImmediate(const BaseEventMessage& message);

    // ========================================================================
    // Message Processing
    // ========================================================================

    /**
     * @brief Process all queued messages (call once per frame)
     * 
     * Dispatches messages to subscribers in FIFO order.
     * Thread-safe - can be called while messages are being published.
     */
    void ProcessMessages();

    /**
     * @brief Clear all queued messages without processing
     */
    void ClearQueue();

    /**
     * @brief Get number of queued messages
     */
    size_t GetQueuedCount() const;

    // ========================================================================
    // Statistics
    // ========================================================================

    struct Stats {
        uint64_t totalPublished = 0;
        uint64_t totalProcessed = 0;
        uint64_t currentQueueSize = 0;
        std::unordered_map<MessageType, uint64_t> publishedByType;
        uint64_t categoryFilterHits = 0;
        uint64_t typeFilterHits = 0;
    };

    /**
     * @brief Get message bus statistics
     */
    Stats GetStats() const;

    /**
     * @brief Reset statistics counters
     */
    void ResetStats();

    /**
     * @brief Enable/disable debug logging
     */
    void SetLoggingEnabled(bool enabled);

private:
    enum class FilterMode : uint8_t { All = 0, Type = 1, Category = 2 };

    struct Subscription {
        EventSubscriptionID id;
        FilterMode mode = FilterMode::All;
        EventCategory categoryFilter = EventCategory::None; // used when mode==Category
        MessageType type = 0;     // 0 = subscribe to all or category-only
        MessageHandler handler;
    };

    void DispatchMessage(const BaseEventMessage& message);

    std::queue<std::unique_ptr<BaseEventMessage>> messageQueue;
    mutable std::mutex queueMutex;

    // Owning storage for subscriptions
    std::list<Subscription> subscriptions;
    std::mutex subscriptionMutex;

    // Fast lookup by MessageType (emT) -> list of subscribers
    std::unordered_map<MessageType, std::vector<Subscription*>> typeSubscriptions;

    // Optional: lookup by individual category bit -> subscribers
    // key = uint64_t bit mask with single bit set
    std::unordered_map<uint64_t, std::vector<Subscription*>> categorySubscriptions;

    EventSubscriptionID nextSubscriptionID = 1;

    // Statistics
    Stats stats;
    mutable std::mutex statsMutex;

    bool loggingEnabled = false;
};

} // namespace EventBus
