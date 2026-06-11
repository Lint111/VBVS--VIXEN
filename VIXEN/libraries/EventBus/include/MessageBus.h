#pragma once

#include "Message.h"
#include "PreAllocatedQueue.h"
#include <functional>
#include <vector>
#include <list>
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

        // Capacity tracking (for pre-allocation diagnostics)
        size_t maxQueueSizeReached = 0;    // High-water mark
        uint32_t capacityWarningCount = 0; // Times queue exceeded warning threshold
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

    /**
     * @brief Set expected queue capacity for warning thresholds
     *
     * When queue size exceeds 80% of this value, a warning is logged.
     * Default is 1024 messages.
     *
     * @param capacity Expected maximum queue capacity
     */
    void SetExpectedCapacity(size_t capacity);

    /**
     * @brief Get the expected capacity setting
     */
    size_t GetExpectedCapacity() const { return expectedCapacity; }

    // ========================================================================
    // Pre-Allocation (Sprint 5.5)
    // ========================================================================

    /**
     * @brief Pre-allocate message queue storage
     *
     * Call during setup phase to prevent heap allocations during frame execution.
     * Should be called with expected maximum messages per frame.
     *
     * Heuristic: nodeCount * 3 events per node is a good starting point.
     *
     * @param capacity Number of message slots to pre-allocate
     */
    void Reserve(size_t capacity);

    /**
     * @brief Get current queue capacity (pre-allocated slots)
     */
    size_t GetQueueCapacity() const;

    /**
     * @brief Get number of times queue had to grow during runtime
     *
     * If this is > 0, Reserve() was called with too small a capacity.
     * Use this to tune pre-allocation size.
     */
    size_t GetQueueGrowthCount() const;

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

    PreAllocatedQueue<std::unique_ptr<BaseEventMessage>> messageQueue_;
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

    // Capacity tracking
    size_t expectedCapacity = 1024;  // Default expected capacity
    size_t warningThreshold = 0;     // 80% of expectedCapacity (cached)
    bool warningLoggedThisSession = false;  // Avoid spamming warnings

    void UpdateWarningThreshold();
    void CheckCapacityWarning(size_t currentSize);
};

// ============================================================================
// ScopedSubscriptions - RAII subscription manager (Sprint 6.3)
// ============================================================================

/**
 * @brief RAII helper for managing MessageBus subscriptions
 *
 * Automatically unsubscribes all subscriptions when destroyed.
 * Provides type-safe Subscribe<EventType>() that handles casting internally.
 *
 * Usage:
 * @code
 * class MySystem {
 *     ScopedSubscriptions subs_;
 *
 *     void Initialize(MessageBus* bus) {
 *         subs_.SetBus(bus);
 *         subs_.Subscribe<FrameStartEvent>([this](const FrameStartEvent& e) {
 *             OnFrameStart(e.frameNumber);
 *         });
 *         subs_.Subscribe<FrameEndEvent>([this](const FrameEndEvent& e) {
 *             OnFrameEnd(e.frameNumber);
 *         });
 *         // Destructor auto-unsubscribes when MySystem is destroyed
 *     }
 * };
 * @endcode
 *
 * Benefits:
 * - Single member instead of N subscription IDs
 * - RAII cleanup (no manual Unsubscribe calls)
 * - Type-safe handlers (no manual static_cast)
 * - Cleaner lambda signatures
 */
class ScopedSubscriptions {
public:
    ScopedSubscriptions() = default;
    explicit ScopedSubscriptions(MessageBus* bus) : bus_(bus) {}

    ~ScopedSubscriptions() {
        UnsubscribeAll();
    }

    // Non-copyable (subscriptions are owned)
    ScopedSubscriptions(const ScopedSubscriptions&) = delete;
    ScopedSubscriptions& operator=(const ScopedSubscriptions&) = delete;

    // Movable
    ScopedSubscriptions(ScopedSubscriptions&& other) noexcept
        : bus_(other.bus_)
        , ids_(std::move(other.ids_))
    {
        other.bus_ = nullptr;
    }

    ScopedSubscriptions& operator=(ScopedSubscriptions&& other) noexcept {
        if (this != &other) {
            UnsubscribeAll();
            bus_ = other.bus_;
            ids_ = std::move(other.ids_);
            other.bus_ = nullptr;
        }
        return *this;
    }

    /**
     * @brief Set the MessageBus to use for subscriptions
     *
     * Must be called before Subscribe() if default constructor was used.
     * Unsubscribes any existing subscriptions first.
     */
    void SetBus(MessageBus* bus) {
        UnsubscribeAll();
        bus_ = bus;
    }

    /**
     * @brief Get the current MessageBus
     */
    [[nodiscard]] MessageBus* GetBus() const { return bus_; }

    /**
     * @brief Type-safe subscribe to a specific event type
     *
     * Handler receives the correctly-typed event reference directly.
     * No manual static_cast needed in the handler.
     *
     * @tparam EventType The event class (must have static TYPE member)
     * @param handler Lambda/function receiving const EventType&
     */
    template<typename EventType>
    void Subscribe(std::function<void(const EventType&)> handler) {
        if (!bus_) return;

        auto id = bus_->Subscribe(
            EventType::TYPE,
            [h = std::move(handler)](const BaseEventMessage& e) -> bool {
                h(static_cast<const EventType&>(e));
                return true;
            }
        );
        ids_.push_back(id);
    }

    /**
     * @brief Subscribe with custom return value control
     *
     * Use when you need to control whether the event continues propagating.
     *
     * @tparam EventType The event class
     * @param handler Lambda returning bool (true = handled, false = continue)
     */
    template<typename EventType>
    void SubscribeWithResult(std::function<bool(const EventType&)> handler) {
        if (!bus_) return;

        auto id = bus_->Subscribe(
            EventType::TYPE,
            [h = std::move(handler)](const BaseEventMessage& e) -> bool {
                return h(static_cast<const EventType&>(e));
            }
        );
        ids_.push_back(id);
    }

    /**
     * @brief Subscribe to event category (receives all events in category)
     *
     * @param category Category flags to match
     * @param handler Handler receiving BaseEventMessage (must cast manually)
     */
    void SubscribeCategory(EventCategory category, MessageHandler handler) {
        if (!bus_) return;
        auto id = bus_->SubscribeCategory(category, std::move(handler));
        ids_.push_back(id);
    }

    /**
     * @brief Unsubscribe all managed subscriptions
     *
     * Called automatically by destructor. Safe to call multiple times.
     */
    void UnsubscribeAll() {
        if (bus_) {
            for (auto id : ids_) {
                bus_->Unsubscribe(id);
            }
        }
        ids_.clear();
    }

    /**
     * @brief Get number of active subscriptions
     */
    [[nodiscard]] size_t GetSubscriptionCount() const { return ids_.size(); }

    /**
     * @brief Check if any subscriptions are active
     */
    [[nodiscard]] bool HasSubscriptions() const { return !ids_.empty(); }

private:
    MessageBus* bus_ = nullptr;
    std::vector<EventSubscriptionID> ids_;
};

} // namespace Vixen::EventBus
