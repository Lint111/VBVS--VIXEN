#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>
#include "MessageBus.h"
#include "Message.h"

using namespace Vixen::EventBus;

// Test message types
struct TestMessage : public Message {
    static constexpr MessageType TYPE = 100;
    std::string data;

    TestMessage(SenderID sender, std::string content)
        : Message(sender, TYPE), data(std::move(content)) {}
};

struct AnotherTestMessage : public Message {
    static constexpr MessageType TYPE = 101;
    int value;

    AnotherTestMessage(SenderID sender, int val)
        : Message(sender, TYPE), value(val) {}
};

// Test fixture
class EventBusTest : public ::testing::Test {
protected:
    void SetUp() override {
        bus = std::make_unique<MessageBus>();
    }

    void TearDown() override {
        bus.reset();
    }

    std::unique_ptr<MessageBus> bus;
};

// ============================================================================
// Subscription Tests
// ============================================================================

TEST_F(EventBusTest, SubscribeAndReceiveMessage) {
    bool received = false;
    std::string receivedData;

    bus->Subscribe(TestMessage::TYPE, [&](const Vixen::EventBus::BaseEventMessage& msg) {
        auto& testMsg = static_cast<const TestMessage&>(msg);
        received = true;
        receivedData = testMsg.data;
        return true;
    });

    auto msg = std::make_unique<TestMessage>(1, "Hello EventBus");
    bus->Publish(std::move(msg));
    bus->ProcessMessages();

    EXPECT_TRUE(received);
    EXPECT_EQ(receivedData, "Hello EventBus");
}

TEST_F(EventBusTest, SubscribeAllReceivesAllMessages) {
    int receivedCount = 0;

    bus->SubscribeAll([&](const Vixen::EventBus::BaseEventMessage&) {
        receivedCount++;
        return true;
    });

    bus->Publish(std::make_unique<TestMessage>(1, "msg1"));
    bus->Publish(std::make_unique<AnotherTestMessage>(1, 42));
    bus->ProcessMessages();

    EXPECT_EQ(receivedCount, 2);
}

TEST_F(EventBusTest, MultipleSubscribers) {
    int subscriber1Called = 0;
    int subscriber2Called = 0;

    bus->Subscribe(TestMessage::TYPE, [&](const Vixen::EventBus::BaseEventMessage&) {
        subscriber1Called++;
        return false; // Continue to next subscriber
    });

    bus->Subscribe(TestMessage::TYPE, [&](const Vixen::EventBus::BaseEventMessage&) {
        subscriber2Called++;
        return true;
    });

    bus->Publish(std::make_unique<TestMessage>(1, "test"));
    bus->ProcessMessages();

    EXPECT_EQ(subscriber1Called, 1);
    EXPECT_EQ(subscriber2Called, 1);
}

TEST_F(EventBusTest, UnsubscribeStopsReceiving) {
    bool received = false;

    auto id = bus->Subscribe(TestMessage::TYPE, [&](const Vixen::EventBus::BaseEventMessage&) {
        received = true;
        return true;
    });

    bus->Unsubscribe(id);

    bus->Publish(std::make_unique<TestMessage>(1, "test"));
    bus->ProcessMessages();

    EXPECT_FALSE(received);
}

// ============================================================================
// Category Subscription Tests
// ============================================================================

TEST_F(EventBusTest, SubscribeCategoryFiltersCorrectly) {
    int resizeCount = 0;
    int otherCount = 0;

    bus->SubscribeCategory(EventCategory::WindowResize, [&](const Vixen::EventBus::BaseEventMessage&) {
        resizeCount++;
        return true;
    });

    bus->SubscribeAll([&](const Vixen::EventBus::BaseEventMessage& msg) {
        if (!msg.HasCategory(EventCategory::WindowResize)) {
            otherCount++;
        }
        return false; // Continue
    });

    // Send resize event
    bus->Publish(std::make_unique<WindowResizeEvent>(1, 1920, 1080));
    // Send non-resize event
    bus->Publish(std::make_unique<TestMessage>(1, "test"));

    bus->ProcessMessages();

    EXPECT_EQ(resizeCount, 1);
    EXPECT_EQ(otherCount, 1);
}

// ============================================================================
// Queue Management Tests
// ============================================================================

TEST_F(EventBusTest, QueueCountCorrect) {
    EXPECT_EQ(bus->GetQueuedCount(), 0);

    bus->Publish(std::make_unique<TestMessage>(1, "msg1"));
    EXPECT_EQ(bus->GetQueuedCount(), 1);

    bus->Publish(std::make_unique<TestMessage>(1, "msg2"));
    EXPECT_EQ(bus->GetQueuedCount(), 2);

    bus->ProcessMessages();
    EXPECT_EQ(bus->GetQueuedCount(), 0);
}

TEST_F(EventBusTest, ClearQueueRemovesMessages) {
    bool received = false;

    bus->Subscribe(TestMessage::TYPE, [&](const Vixen::EventBus::BaseEventMessage&) {
        received = true;
        return true;
    });

    bus->Publish(std::make_unique<TestMessage>(1, "test"));
    EXPECT_EQ(bus->GetQueuedCount(), 1);

    bus->ClearQueue();
    EXPECT_EQ(bus->GetQueuedCount(), 0);

    bus->ProcessMessages();
    EXPECT_FALSE(received);
}

TEST_F(EventBusTest, PublishImmediateSkipsQueue) {
    bool received = false;

    bus->Subscribe(TestMessage::TYPE, [&](const Vixen::EventBus::BaseEventMessage&) {
        received = true;
        return true;
    });

    TestMessage msg(1, "immediate");
    bus->PublishImmediate(msg);

    // Should be received immediately without ProcessMessages
    EXPECT_TRUE(received);
    EXPECT_EQ(bus->GetQueuedCount(), 0);
}

// ============================================================================
// Statistics Tests
// ============================================================================

TEST_F(EventBusTest, StatisticsTracking) {
    bus->ResetStats();

    bus->Subscribe(TestMessage::TYPE, [](const Vixen::EventBus::BaseEventMessage&) { return true; });

    bus->Publish(std::make_unique<TestMessage>(1, "msg1"));
    bus->Publish(std::make_unique<TestMessage>(1, "msg2"));
    bus->ProcessMessages();

    auto stats = bus->GetStats();
    EXPECT_EQ(stats.totalPublished, 2);
    EXPECT_EQ(stats.totalProcessed, 2);
    EXPECT_EQ(stats.currentQueueSize, 0);
}

TEST_F(EventBusTest, MessageTimestamp) {
    std::chrono::steady_clock::time_point msgTime;

    bus->Subscribe(TestMessage::TYPE, [&](const Vixen::EventBus::BaseEventMessage& msg) {
        msgTime = msg.timestamp;
        return true;
    });

    auto beforeTime = std::chrono::steady_clock::now();
    bus->Publish(std::make_unique<TestMessage>(1, "test"));
    bus->ProcessMessages();
    auto afterTime = std::chrono::steady_clock::now();

    EXPECT_GE(msgTime, beforeTime);
    EXPECT_LE(msgTime, afterTime);
}

// ============================================================================
// Message Content Tests
// ============================================================================

TEST_F(EventBusTest, WindowResizeEventContent) {
    uint32_t receivedWidth = 0;
    uint32_t receivedHeight = 0;
    bool receivedMinimized = false;

    bus->Subscribe(WindowResizeEvent::TYPE, [&](const Vixen::EventBus::BaseEventMessage& msg) {
        auto& resizeMsg = static_cast<const WindowResizeEvent&>(msg);
        receivedWidth = resizeMsg.newWidth;
        receivedHeight = resizeMsg.newHeight;
        receivedMinimized = resizeMsg.isMinimized;
        return true;
    });

    bus->Publish(std::make_unique<WindowResizeEvent>(1, 1920, 1080, false));
    bus->ProcessMessages();

    EXPECT_EQ(receivedWidth, 1920);
    EXPECT_EQ(receivedHeight, 1080);
    EXPECT_FALSE(receivedMinimized);
}

// ============================================================================
// Lock-free behavior tests (no-stall MessageBus)
// ============================================================================

TEST_F(EventBusTest, ConcurrentPublishersDeliverEveryMessageInPerSenderOrder) {
    constexpr int kThreads = 4;
    constexpr int kPer = 2000;
    std::vector<int> nextSeq(kThreads, 0);
    std::atomic<int> outOfOrder{0};
    std::atomic<int> received{0};

    bus->Subscribe(AnotherTestMessage::TYPE, [&](const Vixen::EventBus::BaseEventMessage& msg) {
        auto& m = static_cast<const AnotherTestMessage&>(msg);
        const int thread = static_cast<int>(m.sender);
        if (m.value != nextSeq[thread]++) outOfOrder.fetch_add(1);
        received.fetch_add(1);
        return true;
    });

    std::atomic<bool> producing{true};
    std::thread consumer([&] {
        while (producing.load() || bus->GetQueuedCount() > 0) bus->ProcessMessages();
    });
    std::vector<std::thread> producers;
    for (int t = 0; t < kThreads; ++t) {
        producers.emplace_back([&, t] {
            for (int i = 0; i < kPer; ++i) bus->Publish(std::make_unique<AnotherTestMessage>(t, i));
        });
    }
    for (auto& p : producers) p.join();
    producing.store(false);
    consumer.join();
    bus->ProcessMessages();

    EXPECT_EQ(received.load(), kThreads * kPer);
    EXPECT_EQ(outOfOrder.load(), 0);
    EXPECT_EQ(bus->GetQueuedCount(), 0u);
    EXPECT_EQ(bus->GetStats().totalPublished, static_cast<uint64_t>(kThreads * kPer));
    EXPECT_EQ(bus->GetStats().publishedByType[AnotherTestMessage::TYPE], static_cast<uint64_t>(kThreads * kPer));
}

TEST_F(EventBusTest, SubscribeAndUnsubscribeWhileDispatching) {
    std::atomic<int> received{0};
    bus->Subscribe(TestMessage::TYPE, [&](const Vixen::EventBus::BaseEventMessage&) {
        received.fetch_add(1);
        return true;
    });

    std::atomic<bool> stop{false};
    std::thread churn([&] {
        while (!stop.load()) {
            const auto id = bus->Subscribe(TestMessage::TYPE, [](const Vixen::EventBus::BaseEventMessage&) { return true; });
            bus->Unsubscribe(id);
        }
    });
    constexpr int kMessages = 20000;
    for (int i = 0; i < kMessages; ++i) {
        TestMessage msg(1, "x");
        bus->PublishImmediate(msg);
    }
    stop.store(true);
    churn.join();
    EXPECT_EQ(received.load(), kMessages);  // the stable subscriber saw every message
}

TEST_F(EventBusTest, HandlerMaySubscribePublishAndUnsubscribeReentrantly) {
    int nested = 0;
    EventSubscriptionID selfId = 0;
    selfId = bus->Subscribe(TestMessage::TYPE, [&](const Vixen::EventBus::BaseEventMessage&) {
        bus->Unsubscribe(selfId);  // remove itself mid-dispatch
        bus->Subscribe(AnotherTestMessage::TYPE, [&](const Vixen::EventBus::BaseEventMessage&) {
            ++nested;
            return true;
        });
        AnotherTestMessage inner(1, 1);
        bus->PublishImmediate(inner);  // re-enters dispatch
        bus->Publish(std::make_unique<AnotherTestMessage>(1, 2));  // rides the next ProcessMessages
        return true;
    });

    bus->Publish(std::make_unique<TestMessage>(1, "go"));
    bus->ProcessMessages();
    EXPECT_EQ(nested, 1);  // the PublishImmediate; the queued one is still waiting
    EXPECT_EQ(bus->GetQueuedCount(), 1u);
    bus->ProcessMessages();
    EXPECT_EQ(nested, 2);
}

TEST_F(EventBusTest, UnsubscribedHandlerIsNotCalledForLaterMessages) {
    int calls = 0;
    const auto id = bus->Subscribe(TestMessage::TYPE, [&](const Vixen::EventBus::BaseEventMessage&) {
        ++calls;
        return true;
    });
    bus->Publish(std::make_unique<TestMessage>(1, "a"));
    bus->ProcessMessages();
    bus->Unsubscribe(id);
    bus->Publish(std::make_unique<TestMessage>(1, "b"));
    bus->ProcessMessages();
    EXPECT_EQ(calls, 1);
}

TEST_F(EventBusTest, QueuedMessagesAreFreedOnDestruction) {
    for (int i = 0; i < 100; ++i) bus->Publish(std::make_unique<TestMessage>(1, "leak check"));
    bus.reset();  // destructor drains and deletes (run under ASan/LSan to see leaks)
    SUCCEED();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
