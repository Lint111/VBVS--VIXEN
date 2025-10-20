#include "EventBus/MessageBus.h"
#include <iostream>
#include <algorithm>

namespace EventBus {

MessageBus::MessageBus() = default;
MessageBus::~MessageBus() = default;

SubscriptionID MessageBus::Subscribe(MessageType type, MessageHandler handler) {
    std::lock_guard<std::mutex> lock(subscriptionMutex);

    SubscriptionID id = nextSubscriptionID++;
    subscriptions.push_back({id, type, std::move(handler)});

    if (loggingEnabled) {
        std::cout << "[MessageBus] Subscription " << id << " created for type " << type << "\n";
    }

    return id;
}

SubscriptionID MessageBus::SubscribeAll(MessageHandler handler) {
    return Subscribe(0, std::move(handler)); // Type 0 = all messages
}

void MessageBus::Unsubscribe(SubscriptionID id) {
    std::lock_guard<std::mutex> lock(subscriptionMutex);

    auto it = std::remove_if(subscriptions.begin(), subscriptions.end(),
        [id](const Subscription& sub) { return sub.id == id; });

    if (it != subscriptions.end()) {
        subscriptions.erase(it, subscriptions.end());

        if (loggingEnabled) {
            std::cout << "[MessageBus] Subscription " << id << " removed\n";
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

void MessageBus::Publish(std::unique_ptr<Message> message) {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        messageQueue.push(std::move(message));
    }

    {
        std::lock_guard<std::mutex> lock(statsMutex);
        stats.totalPublished++;
        stats.currentQueueSize = messageQueue.size();
    }
}

void MessageBus::PublishImmediate(const Message& message) {
    DispatchMessage(message);

    {
        std::lock_guard<std::mutex> lock(statsMutex);
        stats.totalPublished++;
        stats.totalProcessed++;
        stats.publishedByType[message.type]++;
    }
}

void MessageBus::ProcessMessages() {
    // Swap queue to minimize lock time
    std::queue<std::unique_ptr<Message>> localQueue;
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
}

void MessageBus::DispatchMessage(const Message& message) {
    std::lock_guard<std::mutex> lock(subscriptionMutex);

    for (const auto& sub : subscriptions) {
        // Check if subscription matches (type-specific or all)
        if (sub.type == 0 || sub.type == message.type) {
            bool handled = sub.handler(message);

            if (handled && loggingEnabled) {
                std::cout << "[MessageBus] Message handled by subscription " << sub.id << "\n";
            }

            // If handler returns true, it consumed the message (optional: stop propagation)
            // Currently, all matching subscribers receive the message
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

} // namespace EventBus
