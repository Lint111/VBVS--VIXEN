#pragma once

#include "Message.h"
#include <functional>
#include <vector>
#include <queue>
#include <mutex>
#include <unordered_map>
#include <memory>

namespace EventBus {

/**
 * @brief Message handler callback signature
 * 
 * Receives message by const reference.
 * Returns true if handled, false if should continue to other subscribers.
 */
using MessageHandler = std::function<bool(const Message&)>;

/**
 * @brief Subscription handle for unsubscribing
 */
using SubscriptionID = uint32_t;

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
    SubscriptionID Subscribe(MessageType type, MessageHandler handler);

    /**
     * @brief Subscribe to ALL message types
     * 
     * @param handler Callback receiving all messages
     * @return Subscription ID
     */
    SubscriptionID SubscribeAll(MessageHandler handler);

    /**
     * @brief Unsubscribe from messages
     * 
     * @param id Subscription ID from Subscribe()
     */
    void Unsubscribe(SubscriptionID id);

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
    void Publish(std::unique_ptr<Message> message);

    /**
     * @brief Publish message immediately (synchronous)
     * 
     * Dispatches to subscribers now, bypassing queue.
     * Use sparingly - prefer Publish() for normal flow.
     * 
     * @param message Message reference (not transferred)
     */
    void PublishImmediate(const Message& message);

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
    struct Subscription {
        SubscriptionID id;
        MessageType type;     // 0 = subscribe to all
        MessageHandler handler;
    };

    void DispatchMessage(const Message& message);

    std::queue<std::unique_ptr<Message>> messageQueue;
    mutable std::mutex queueMutex;

    std::vector<Subscription> subscriptions;
    std::mutex subscriptionMutex;

    SubscriptionID nextSubscriptionID = 1;

    // Statistics
    Stats stats;
    mutable std::mutex statsMutex;

    bool loggingEnabled = false;
};

} // namespace EventBus
