#include "pch.h"
#include "MessageBus.h"
#include <iostream>
#include <algorithm>
#include <set>
#include <cstdint>
#include <unordered_set>

namespace Vixen::EventBus {

MessageBus::MessageBus() {
    UpdateWarningThreshold();
}

MessageBus::~MessageBus() = default;

EventSubscriptionID MessageBus::Subscribe(MessageType type, MessageHandler handler) {
    std::lock_guard<std::mutex> lock(subscriptionMutex);

    EventSubscriptionID id = nextSubscriptionID++;

    Subscription sub;
    sub.id = id;
    sub.type = type;
    sub.handler = std::move(handler);
    sub.mode = (type == 0) ? FilterMode::All : FilterMode::Type;

    subscriptions.push_back(std::move(sub));
    auto it = std::prev(subscriptions.end());

    // Register in type lookup
    typeSubscriptions[it->type].push_back(&*it);

    if (loggingEnabled) {
        std::cout << "[MessageBus] Subscription " << id << " created for type " << type << "\n";
    }

    return id;
}

EventSubscriptionID MessageBus::SubscribeAll(MessageHandler handler) {
    return Subscribe(0, std::move(handler)); // Type 0 = all messages
}

EventSubscriptionID MessageBus::SubscribeCategory(EventCategory category, MessageHandler handler) {
    std::lock_guard<std::mutex> lock(subscriptionMutex);

    EventSubscriptionID id = nextSubscriptionID++;
    Subscription sub;
    sub.id = id;
    sub.mode = FilterMode::Category;
    sub.categoryFilter = category;
    sub.type = 0;
    sub.handler = std::move(handler);

    subscriptions.push_back(std::move(sub));
    auto it = std::prev(subscriptions.end());

    // Register into categorySubscriptions per-bit
    uint64_t bits = static_cast<uint64_t>(category);
    while (bits) {
        uint64_t bit = bits & (~(bits - 1)); // lowest set bit
        categorySubscriptions[bit].push_back(&*it);
        bits &= (bits - 1);
    }

    if (loggingEnabled) {
        std::cout << "[MessageBus] Subscription " << id << " created for category " << static_cast<uint64_t>(category) << "\n";
    }

    return id;
}

EventSubscriptionID MessageBus::SubscribeCategories(EventCategory categories, MessageHandler handler) {
    return SubscribeCategory(categories, std::move(handler));
}

void MessageBus::Unsubscribe(EventSubscriptionID id) {
    std::lock_guard<std::mutex> lock(subscriptionMutex);

    // Find subscription in list
    for (auto it = subscriptions.begin(); it != subscriptions.end(); ++it) {
        if (it->id == id) {
            // Remove from typeSubscriptions
            if (it->mode == FilterMode::Type || it->mode == FilterMode::All) {
                auto &vec = typeSubscriptions[it->type];
                vec.erase(std::remove(vec.begin(), vec.end(), &*it), vec.end());
            }

            // Remove from categorySubscriptions
            if (it->mode == FilterMode::Category) {
                uint64_t bits = static_cast<uint64_t>(it->categoryFilter);
                while (bits) {
                    uint64_t bit = bits & (~(bits - 1));
                    auto &vec = categorySubscriptions[bit];
                    vec.erase(std::remove(vec.begin(), vec.end(), &*it), vec.end());
                    bits &= (bits - 1);
                }
            }

            subscriptions.erase(it);

            if (loggingEnabled) {
                std::cout << "[MessageBus] Subscription " << id << " removed\n";
            }
            return;
        }
    }
}

void MessageBus::UnsubscribeAll() {
    std::lock_guard<std::mutex> lock(subscriptionMutex);
    subscriptions.clear();

    if (loggingEnabled) {
        std::cout << "[MessageBus] All subscriptions cleared\n";
    }
}

void MessageBus::Publish(std::unique_ptr<BaseEventMessage> message) {
    size_t currentSize = 0;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        messageQueue.push(std::move(message));
        currentSize = messageQueue.size();
    }

    {
        std::lock_guard<std::mutex> lock(statsMutex);
        stats.totalPublished++;
        stats.currentQueueSize = currentSize;

        // Track high-water mark
        if (currentSize > stats.maxQueueSizeReached) {
            stats.maxQueueSizeReached = currentSize;
        }
    }

    // Check capacity warning (outside stats lock to avoid deadlock with logging)
    CheckCapacityWarning(currentSize);
}

void MessageBus::PublishImmediate(const BaseEventMessage& message) {
    DispatchMessage(message);

    {
        std::lock_guard<std::mutex> lock(statsMutex);
        stats.totalPublished++;
        stats.totalProcessed++;
        stats.publishedByType[message.type]++;
        stats.typeFilterHits++; // best-effort increment
    }
}

void MessageBus::ProcessMessages() {
    // Swap queue to minimize lock time
    std::queue<std::unique_ptr<BaseEventMessage>> localQueue;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        localQueue.swap(messageQueue);
    }

    size_t processed = 0;

    // Process all messages
    while (!localQueue.empty()) {
        auto& message = localQueue.front();

        if (loggingEnabled) {
            std::cout << "[MessageBus] Processing message type " << message->type
                     << " from sender " << message->sender
                     << " (age: " << message->GetAgeSeconds() << "s)\n";
        }

        DispatchMessage(*message);
        localQueue.pop();
        processed++;
    }

    // Update stats
    {
        std::lock_guard<std::mutex> lock(statsMutex);
        stats.totalProcessed += processed;
        stats.currentQueueSize = 0;
    }

    // Reset warning flag when queue is drained, allowing future warnings
    warningLoggedThisSession = false;
}

void MessageBus::DispatchMessage(const BaseEventMessage& message) {
    std::lock_guard<std::mutex> lock(subscriptionMutex);

    // Collect candidate subscriptions from type lookup (exact type and type==0)
    std::vector<Subscription*> candidates;
    auto itType = typeSubscriptions.find(message.type);
    if (itType != typeSubscriptions.end()) {
        candidates.insert(candidates.end(), itType->second.begin(), itType->second.end());
    }
    auto itAll = typeSubscriptions.find(0);
    if (itAll != typeSubscriptions.end()) {
        candidates.insert(candidates.end(), itAll->second.begin(), itAll->second.end());
    }

    // Add category-subscribers (per-bit) if present
    uint64_t bits = static_cast<uint64_t>(message.categoryFlags);
    while (bits) {
        uint64_t bit = bits & (~(bits - 1));
        auto itCat = categorySubscriptions.find(bit);
        if (itCat != categorySubscriptions.end()) {
            candidates.insert(candidates.end(), itCat->second.begin(), itCat->second.end());
        }
        bits &= (bits - 1);
    }

    // Deduplicate candidates by subscription id
    std::unordered_set<EventSubscriptionID> seen;
    for (auto *subPtr : candidates) {
        if (!subPtr) continue;
        if (seen.find(subPtr->id) != seen.end()) continue;
        seen.insert(subPtr->id);

        bool matches = false;
        if (subPtr->mode == FilterMode::All) {
            matches = true;
            stats.typeFilterHits++;
        } else if (subPtr->mode == FilterMode::Type) {
            matches = (subPtr->type == 0 || subPtr->type == message.type);
            if (matches) stats.typeFilterHits++;
        } else if (subPtr->mode == FilterMode::Category) {
            if ((static_cast<uint64_t>(message.categoryFlags) & static_cast<uint64_t>(subPtr->categoryFilter)) != 0ULL) {
                matches = true;
                stats.categoryFilterHits++;
            }
        }

        if (!matches) continue;

        try {
            bool handled = subPtr->handler(message);
            if (handled && loggingEnabled) {
                std::cout << "[MessageBus] Message handled by subscription " << subPtr->id << "\n";
            }
        } catch (const std::exception &e) {
            if (loggingEnabled) std::cerr << "[MessageBus] Handler threw: " << e.what() << "\n";
        }
    }

    // Update type statistics
    {
        std::lock_guard<std::mutex> lock(statsMutex);
        stats.publishedByType[message.type]++;
    }
}

void MessageBus::ClearQueue() {
    std::lock_guard<std::mutex> lock(queueMutex);
    
    size_t discarded = messageQueue.size();
    messageQueue = {};

    if (loggingEnabled && discarded > 0) {
        std::cout << "[MessageBus] Cleared " << discarded << " queued messages\n";
    }

    {
        std::lock_guard<std::mutex> lock(statsMutex);
        stats.currentQueueSize = 0;
    }
}

size_t MessageBus::GetQueuedCount() const {
    std::lock_guard<std::mutex> lock(queueMutex);
    return messageQueue.size();
}

MessageBus::Stats MessageBus::GetStats() const {
    std::lock_guard<std::mutex> lock(statsMutex);
    return stats;
}

void MessageBus::ResetStats() {
    std::lock_guard<std::mutex> lock(statsMutex);
    stats = {};
    warningLoggedThisSession = false;  // Allow warnings again after reset

    if (loggingEnabled) {
        std::cout << "[MessageBus] Statistics reset\n";
    }
}

void MessageBus::SetLoggingEnabled(bool enabled) {
    loggingEnabled = enabled;
    if (enabled) {
        std::cout << "[MessageBus] Logging enabled\n";
    }
}

void MessageBus::SetExpectedCapacity(size_t capacity) {
    expectedCapacity = capacity;
    UpdateWarningThreshold();
    warningLoggedThisSession = false;  // Reset warning state for new capacity

    if (loggingEnabled) {
        std::cout << "[MessageBus] Expected capacity set to " << capacity
                  << " (warning threshold: " << warningThreshold << ")\n";
    }
}

void MessageBus::UpdateWarningThreshold() {
    // 80% threshold for warnings
    warningThreshold = static_cast<size_t>(expectedCapacity * 0.8);
}

void MessageBus::CheckCapacityWarning(size_t currentSize) {
    // Only warn once per session to avoid log spam
    if (warningLoggedThisSession) {
        return;
    }

    if (currentSize >= warningThreshold) {
        warningLoggedThisSession = true;

        // Increment warning counter (needs lock)
        {
            std::lock_guard<std::mutex> lock(statsMutex);
            stats.capacityWarningCount++;
        }

        // Log warning - this appears regardless of loggingEnabled setting
        // because capacity warnings are important for pre-allocation tuning
        std::cerr << "[WARN] MessageBus queue approaching capacity ("
                  << currentSize << "/" << expectedCapacity
                  << " messages, " << stats.capacityWarningCount
                  << " warnings this session, max reached: "
                  << stats.maxQueueSizeReached << ")\n";
    }
}

} // namespace Vixen::EventBus
