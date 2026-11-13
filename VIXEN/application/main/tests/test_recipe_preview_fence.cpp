#include "RecipePreviewFence.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

TEST(RecipePreviewFence, PublishesSerialsInOrderAndWaitsForTheRequestedSerial) {
    RecipePreviewFence fence;
    EXPECT_EQ(fence.PublishedSerial(), 0u);
    EXPECT_TRUE(fence.Wait(0u, std::chrono::milliseconds(0)));

    fence.Publish(2u, true);
    fence.Publish(1u, true);
    EXPECT_EQ(fence.PublishedSerial(), 2u);
    EXPECT_TRUE(fence.Wait(1u, std::chrono::milliseconds(0)));
    EXPECT_TRUE(fence.Wait(2u, std::chrono::milliseconds(0)));
    EXPECT_FALSE(fence.Wait(3u, std::chrono::milliseconds(0)));
}

TEST(RecipePreviewFence, WaitUnblocksWhenAWorkerPublishes) {
    RecipePreviewFence fence;
    bool completed = false;
    std::thread publisher([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        fence.Publish(4u, true);
    });
    completed = fence.Wait(4u, std::chrono::milliseconds(250));
    publisher.join();
    EXPECT_TRUE(completed);
    EXPECT_TRUE(fence.Succeeded(4u));
}

TEST(RecipePreviewFence, FailedCompileAdvancesTheSerialWithoutReportingSuccess) {
    RecipePreviewFence fence;
    fence.Publish(7u, false);

    EXPECT_EQ(fence.PublishedSerial(), 7u);
    EXPECT_TRUE(fence.Wait(7u, std::chrono::milliseconds(0)));
    EXPECT_FALSE(fence.Succeeded(7u));

    fence.Publish(8u, true);
    EXPECT_EQ(fence.PublishedSerial(), 8u);
    EXPECT_FALSE(fence.Succeeded(7u));
    EXPECT_TRUE(fence.Succeeded(8u));
}

TEST(RecipePreviewFence, HighestCompletedSerialCoversCoalescedEditRows) {
    RecipePreviewFence fence;

    // A program edit can publish serial 6 and then advance twice before its next frame is
    // stamped: serial 7 for the replacement and serial 8 for the body-instance row. The frame
    // fence publishes the highest completed serial, so waiting on either row must not strand.
    fence.Publish(6u, true);
    EXPECT_FALSE(fence.Wait(8u, std::chrono::milliseconds(0)));

    fence.Publish(8u, true);
    EXPECT_EQ(fence.PublishedSerial(), 8u);
    EXPECT_TRUE(fence.Wait(7u, std::chrono::milliseconds(0)));
    EXPECT_TRUE(fence.Wait(8u, std::chrono::milliseconds(0)));
    EXPECT_TRUE(fence.Succeeded(7u));
    EXPECT_TRUE(fence.Succeeded(8u));
}
