#include "pch.h"
#include "MessageBus.h"
#include <iostream>
#include <algorithm>
#include <cstdint>

namespace Vixen::EventBus {

namespace {

// busNext is a plain pointer on the message (so messages stay copyable); the queue reads and writes it
// atomically through atomic_ref.
inline BaseEventMessage* LoadNext(BaseEventMessage* node, std::memory_order order) {
    return std::atomic_ref<BaseEventMessage*>(node->busNext).load(order);
}
inline void StoreNext(BaseEventMessage* node, BaseEventMessage* next, std::memory_order order) {
    std::atomic_ref<BaseEventMessage*>(node->busNext).store(next, order);
}

}  // namespace

MessageBus::MessageBus()
    : queueStub_(EventCategory::None, 0, 0)
    , queueHead_(&queueStub_)
    , queueTail_(&queueStub_) {
    UpdateWarningThreshold();
    queueCapacity_.store(expectedCapacity.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

MessageBus::~MessageBus() {
    DrainAndDelete();
}

// ============================================================================
// Subscriptions: immutable table, copy-on-write publish
// ============================================================================

EventSubscriptionID MessageBus::AddSubscription(std::shared_ptr<const Subscription> sub) {
    const EventSubscriptionID id = sub->id;
    subscriptions_.Update([&](const SubscriptionTable& current) {
        auto next = std::make_unique<SubscriptionTable>(current);
        const Subscription* raw = sub.get();
        next->all.push_back(sub);
        if (raw->mode == FilterMode::Category) {
            // Register into byCategory per-bit
            uint64_t bits = static_cast<uint64_t>(raw->categoryFilter);
            while (bits) {
                uint64_t bit = bits & (~(bits - 1)); // lowest set bit
                next->byCategory[bit].push_back(raw);
                bits &= (bits - 1);
            }
        } else {
            next->byType[raw->type].push_back(raw);
        }
        return next;
    });
    return id;
}

EventSubscriptionID MessageBus::Subscribe(MessageType type, MessageHandler handler) {
    auto sub = std::make_shared<Subscription>();
    sub->id = nextSubscriptionID_.fetch_add(1, std::memory_order_relaxed);
    sub->type = type;
    sub->handler = std::move(handler);
    sub->mode = (type == 0) ? FilterMode::All : FilterMode::Type;
    const EventSubscriptionID id = AddSubscription(std::move(sub));

    if (loggingEnabled.load(std::memory_order_relaxed)) {
        std::cout << "[MessageBus] Subscription " << id << " created for type " << type << "\n";
    }

    return id;
}

EventSubscriptionID MessageBus::SubscribeAll(MessageHandler handler) {
    return Subscribe(0, std::move(handler)); // Type 0 = all messages
}

EventSubscriptionID MessageBus::SubscribeCategory(EventCategory category, MessageHandler handler) {
    auto sub = std::make_shared<Subscription>();
    sub->id = nextSubscriptionID_.fetch_add(1, std::memory_order_relaxed);
    sub->mode = FilterMode::Category;
    sub->categoryFilter = category;
    sub->type = 0;
    sub->handler = std::move(handler);
    const EventSubscriptionID id = AddSubscription(std::move(sub));

    if (loggingEnabled.load(std::memory_order_relaxed)) {
        std::cout << "[MessageBus] Subscription " << id << " created for category " << static_cast<uint64_t>(category) << "\n";
    }

    return id;
}

EventSubscriptionID MessageBus::SubscribeCategories(EventCategory categories, MessageHandler handler) {
    return SubscribeCategory(categories, std::move(handler));
}

void MessageBus::Unsubscribe(EventSubscriptionID id) {
    const bool removed = subscriptions_.Update([&](const SubscriptionTable& current) -> std::unique_ptr<SubscriptionTable> {
        auto found = std::find_if(current.all.begin(), current.all.end(),
                                  [id](const auto& s) { return s->id == id; });
        if (found == current.all.end()) return nullptr;  // unknown id: nothing to publish

        auto next = std::make_unique<SubscriptionTable>(current);
        const Subscription* raw = found->get();
        if (raw->mode == FilterMode::Type || raw->mode == FilterMode::All) {
            auto& vec = next->byType[raw->type];
            vec.erase(std::remove(vec.begin(), vec.end(), raw), vec.end());
        }
        if (raw->mode == FilterMode::Category) {
            uint64_t bits = static_cast<uint64_t>(raw->categoryFilter);
            while (bits) {
                uint64_t bit = bits & (~(bits - 1));
                auto& vec = next->byCategory[bit];
                vec.erase(std::remove(vec.begin(), vec.end(), raw), vec.end());
                bits &= (bits - 1);
            }
        }
        next->all.erase(next->all.begin() + (found - current.all.begin()));
        return next;
    });

    if (removed && loggingEnabled.load(std::memory_order_relaxed)) {
        std::cout << "[MessageBus] Subscription " << id << " removed\n";
    }
}

void MessageBus::UnsubscribeAll() {
    subscriptions_.Publish(SubscriptionTable{});

    if (loggingEnabled.load(std::memory_order_relaxed)) {
        std::cout << "[MessageBus] All subscriptions cleared\n";
    }
}

// ============================================================================
// Publish queue: Vyukov intrusive MPSC
// ============================================================================

void MessageBus::PushQueued(BaseEventMessage* node) {
    StoreNext(node, nullptr, std::memory_order_relaxed);
    BaseEventMessage* prev = queueHead_.exchange(node, std::memory_order_acq_rel);
    StoreNext(prev, node, std::memory_order_release);
}

BaseEventMessage* MessageBus::PopQueued() {
    BaseEventMessage* tail = queueTail_;
    BaseEventMessage* next = LoadNext(tail, std::memory_order_acquire);
    if (tail == &queueStub_) {
        if (!next) return nullptr;
        queueTail_ = next;
        tail = next;
        next = LoadNext(tail, std::memory_order_acquire);
    }
    if (next) {
        queueTail_ = next;
        return tail;
    }
    BaseEventMessage* head = queueHead_.load(std::memory_order_acquire);
    if (tail != head) return nullptr;  // a producer is between its exchange and its link: next call

    PushQueued(&queueStub_);
    next = LoadNext(tail, std::memory_order_acquire);
    if (next) {
        queueTail_ = next;
        return tail;
    }
    return nullptr;
}

void MessageBus::DrainAndDelete() {
    while (BaseEventMessage* node = PopQueued()) {
        delete node;
        queuedCount_.fetch_sub(1, std::memory_order_relaxed);
    }
}

void MessageBus::Publish(std::unique_ptr<BaseEventMessage> message) {
    // Count first so a consumer's batch budget never undercounts a message that is about to be linked.
    const int64_t depth = queuedCount_.fetch_add(1, std::memory_order_relaxed) + 1;
    PushQueued(message.release());
    const size_t currentSize = depth > 0 ? static_cast<size_t>(depth) : 0;

    totalPublished_.fetch_add(1, std::memory_order_relaxed);

    // Track high-water mark (monotonic max: retries only while another publisher raises it)
    size_t seen = maxQueueSizeReached_.load(std::memory_order_relaxed);
    while (currentSize > seen &&
           !maxQueueSizeReached_.compare_exchange_weak(seen, currentSize, std::memory_order_relaxed)) {
    }

    // The declared queue size doubles when exceeded (one CAS, no retry): the old "queue grew" signal.
    size_t capacity = queueCapacity_.load(std::memory_order_relaxed);
    if (capacity != 0 && currentSize > capacity &&
        queueCapacity_.compare_exchange_strong(capacity, capacity * 2, std::memory_order_relaxed)) {
        queueGrowthCount_.fetch_add(1, std::memory_order_relaxed);
    }

    CheckCapacityWarning(currentSize);
}

void MessageBus::PublishImmediate(const BaseEventMessage& message) {
    DispatchMessage(message);

    totalPublished_.fetch_add(1, std::memory_order_relaxed);
    totalProcessed_.fetch_add(1, std::memory_order_relaxed);
    typeFilterHits_.fetch_add(1, std::memory_order_relaxed); // best-effort increment
}

void MessageBus::ProcessMessages() {
    // Owner phase: exactly one consumer at a time. A concurrent caller does not wait; the owner
    // delivers what is queued.
    if (consumerBusy_.test_and_set(std::memory_order_acquire)) return;

    // Batch semantics: deliver what was queued when we started; messages published by handlers during
    // this call ride the next one.
    int64_t budget = queuedCount_.load(std::memory_order_acquire);
    size_t processed = 0;

    while (budget > 0) {
        std::unique_ptr<BaseEventMessage> message(PopQueued());
        if (!message) break;  // empty, or a producer has not finished linking: next call
        queuedCount_.fetch_sub(1, std::memory_order_relaxed);
        --budget;

        if (loggingEnabled.load(std::memory_order_relaxed)) {
            std::cout << "[MessageBus] Processing message type " << message->type
                     << " from sender " << message->sender
                     << " (age: " << message->GetAgeSeconds() << "s)\n";
        }

        DispatchMessage(*message);
        processed++;
    }

    totalProcessed_.fetch_add(processed, std::memory_order_relaxed);

    // Reset warning flag when queue is drained, allowing future warnings
    if (queuedCount_.load(std::memory_order_relaxed) <= 0) {
        warningLoggedThisSession.store(false, std::memory_order_relaxed);
    }

    consumerBusy_.clear(std::memory_order_release);
}

void MessageBus::DispatchMessage(const BaseEventMessage& message) {
    auto table = subscriptions_.Read();  // pins the table this dispatch started with

    // Collect candidate subscriptions from type lookup (exact type and type==0)
    std::vector<const Subscription*> candidates;
    candidates.reserve(16);
    auto addUnique = [&](const std::vector<const Subscription*>& list) {
        for (const Subscription* sub : list) {
            if (!sub) continue;
            if (std::find(candidates.begin(), candidates.end(), sub) != candidates.end()) continue;
            candidates.push_back(sub);
        }
    };

    auto itType = table->byType.find(message.type);
    if (itType != table->byType.end()) addUnique(itType->second);
    auto itAll = table->byType.find(0);
    if (itAll != table->byType.end()) addUnique(itAll->second);

    // Add category-subscribers (per-bit) if present
    uint64_t bits = static_cast<uint64_t>(message.categoryFlags);
    while (bits) {
        uint64_t bit = bits & (~(bits - 1));
        auto itCat = table->byCategory.find(bit);
        if (itCat != table->byCategory.end()) addUnique(itCat->second);
        bits &= (bits - 1);
    }

    for (const Subscription* subPtr : candidates) {
        bool matches = false;
        if (subPtr->mode == FilterMode::All) {
            matches = true;
            typeFilterHits_.fetch_add(1, std::memory_order_relaxed);
        } else if (subPtr->mode == FilterMode::Type) {
            matches = (subPtr->type == 0 || subPtr->type == message.type);
            if (matches) typeFilterHits_.fetch_add(1, std::memory_order_relaxed);
        } else if (subPtr->mode == FilterMode::Category) {
            if ((static_cast<uint64_t>(message.categoryFlags) & static_cast<uint64_t>(subPtr->categoryFilter)) != 0ULL) {
                matches = true;
                categoryFilterHits_.fetch_add(1, std::memory_order_relaxed);
            }
        }

        if (!matches) continue;

        try {
            bool handled = subPtr->handler(message);
            if (handled && loggingEnabled.load(std::memory_order_relaxed)) {
                std::cout << "[MessageBus] Message handled by subscription " << subPtr->id << "\n";
            }
        } catch (const std::exception &e) {
            if (loggingEnabled.load(std::memory_order_relaxed)) std::cerr << "[MessageBus] Handler threw: " << e.what() << "\n";
        }
    }

    CountPublishedType(message.type);
}

void MessageBus::CountPublishedType(MessageType type) {
    const uint64_t key = static_cast<uint64_t>(type) + 1;
    size_t index = (static_cast<size_t>(type) * 2654435761u) & (kTypeSlots - 1);
    for (size_t probe = 0; probe < kTypeSlots; ++probe, index = (index + 1) & (kTypeSlots - 1)) {
        TypeSlot& slot = typeSlots_[index];
        uint64_t current = slot.keyPlusOne.load(std::memory_order_acquire);
        if (current == 0 && slot.keyPlusOne.compare_exchange_strong(current, key, std::memory_order_acq_rel)) {
            current = key;  // claimed an empty slot for this type
        }
        if (current == key) {
            slot.count.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
    // Slot table exhausted (more than kTypeSlots distinct message types): per-type count is capped.
}

void MessageBus::ClearQueue() {
    // Owner phase, like ProcessMessages. Contended: the owner is delivering; nothing is discarded.
    if (consumerBusy_.test_and_set(std::memory_order_acquire)) return;

    size_t discarded = 0;
    while (BaseEventMessage* node = PopQueued()) {
        delete node;
        queuedCount_.fetch_sub(1, std::memory_order_relaxed);
        ++discarded;
    }

    if (loggingEnabled.load(std::memory_order_relaxed) && discarded > 0) {
        std::cout << "[MessageBus] Cleared " << discarded << " queued messages\n";
    }

    consumerBusy_.clear(std::memory_order_release);
}

size_t MessageBus::GetQueuedCount() const {
    const int64_t depth = queuedCount_.load(std::memory_order_relaxed);
    return depth > 0 ? static_cast<size_t>(depth) : 0;
}

MessageBus::Stats MessageBus::GetStats() const {
    Stats stats;
    stats.totalPublished = totalPublished_.load(std::memory_order_relaxed);
    stats.totalProcessed = totalProcessed_.load(std::memory_order_relaxed);
    stats.currentQueueSize = GetQueuedCount();
    stats.categoryFilterHits = categoryFilterHits_.load(std::memory_order_relaxed);
    stats.typeFilterHits = typeFilterHits_.load(std::memory_order_relaxed);
    stats.maxQueueSizeReached = maxQueueSizeReached_.load(std::memory_order_relaxed);
    stats.capacityWarningCount = capacityWarningCount_.load(std::memory_order_relaxed);
    for (const TypeSlot& slot : typeSlots_) {
        const uint64_t key = slot.keyPlusOne.load(std::memory_order_acquire);
        if (key == 0) continue;
        const uint64_t count = slot.count.load(std::memory_order_relaxed);
        if (count != 0) stats.publishedByType[static_cast<MessageType>(key - 1)] = count;
    }
    return stats;
}

void MessageBus::ResetStats() {
    totalPublished_.store(0, std::memory_order_relaxed);
    totalProcessed_.store(0, std::memory_order_relaxed);
    categoryFilterHits_.store(0, std::memory_order_relaxed);
    typeFilterHits_.store(0, std::memory_order_relaxed);
    maxQueueSizeReached_.store(0, std::memory_order_relaxed);
    capacityWarningCount_.store(0, std::memory_order_relaxed);
    for (TypeSlot& slot : typeSlots_) slot.count.store(0, std::memory_order_relaxed);
    warningLoggedThisSession.store(false, std::memory_order_relaxed);  // Allow warnings again after reset

    if (loggingEnabled.load(std::memory_order_relaxed)) {
        std::cout << "[MessageBus] Statistics reset\n";
    }
}

void MessageBus::SetLoggingEnabled(bool enabled) {
    loggingEnabled.store(enabled, std::memory_order_relaxed);
    if (enabled) {
        std::cout << "[MessageBus] Logging enabled\n";
    }
}

void MessageBus::SetExpectedCapacity(size_t capacity) {
    expectedCapacity.store(capacity, std::memory_order_relaxed);
    UpdateWarningThreshold();
    warningLoggedThisSession.store(false, std::memory_order_relaxed);  // Reset warning state for new capacity

    if (loggingEnabled.load(std::memory_order_relaxed)) {
        std::cout << "[MessageBus] Expected capacity set to " << capacity
                  << " (warning threshold: " << warningThreshold.load(std::memory_order_relaxed) << ")\n";
    }
}

void MessageBus::UpdateWarningThreshold() {
    // 80% threshold for warnings
    warningThreshold.store(static_cast<size_t>(expectedCapacity.load(std::memory_order_relaxed) * 0.8),
                           std::memory_order_relaxed);
}

void MessageBus::CheckCapacityWarning(size_t currentSize) {
    // Only warn once per session to avoid log spam
    if (warningLoggedThisSession.load(std::memory_order_relaxed)) {
        return;
    }

    if (currentSize >= warningThreshold.load(std::memory_order_relaxed)) {
        if (warningLoggedThisSession.exchange(true, std::memory_order_relaxed)) {
            return;  // another publisher logged it first
        }
        const uint32_t warnings = capacityWarningCount_.fetch_add(1, std::memory_order_relaxed) + 1;

        // Log warning - this appears regardless of loggingEnabled setting
        // because capacity warnings are important for pre-allocation tuning
        std::cerr << "[WARN] MessageBus queue approaching capacity ("
                  << currentSize << "/" << expectedCapacity.load(std::memory_order_relaxed)
                  << " messages, " << warnings
                  << " warnings this session, max reached: "
                  << maxQueueSizeReached_.load(std::memory_order_relaxed) << ")\n";
    }
}

// ============================================================================
// Pre-Allocation (Sprint 5.5)
// ============================================================================

void MessageBus::Reserve(size_t capacity) {
    // Messages carry their own queue link, so there is no storage to pre-allocate; this declares the
    // expected depth for the growth diagnostic and the warning threshold.
    size_t declared = queueCapacity_.load(std::memory_order_relaxed);
    while (capacity > declared &&
           !queueCapacity_.compare_exchange_weak(declared, capacity, std::memory_order_relaxed)) {
    }

    // Also update expected capacity for warning thresholds
    size_t expected = expectedCapacity.load(std::memory_order_relaxed);
    if (capacity > expected) {
        expectedCapacity.store(capacity, std::memory_order_relaxed);
        UpdateWarningThreshold();
    }

    if (loggingEnabled.load(std::memory_order_relaxed)) {
        std::cout << "[MessageBus] Queue depth declared for " << capacity
                  << " messages (declared capacity: " << queueCapacity_.load(std::memory_order_relaxed) << ")\n";
    }
}

size_t MessageBus::GetQueueCapacity() const {
    return queueCapacity_.load(std::memory_order_relaxed);
}

size_t MessageBus::GetQueueGrowthCount() const {
    return queueGrowthCount_.load(std::memory_order_relaxed);
}

} // namespace Vixen::EventBus
