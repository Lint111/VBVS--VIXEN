#include "KernelDispatch/FramePipeline.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace {

using namespace Vixen::KernelDispatch;

FramePipelineWork OneTask(uint64_t frame,
                          uint64_t owner,
                          LoopDomainMetadata domain,
                          std::function<void()> body,
                          std::vector<uint64_t>* commitOrder = nullptr,
                          std::mutex* commitMutex = nullptr) {
    const TaskId id{owner, 0};
    FramePipelineWork work;
    work.frameIndex = frame;
    work.domain = domain;
    work.workerCount = 2;
    work.tasks.push_back(VirtualTask{id, std::move(body), {}, VirtualTaskState::Pending, {}});
    work.waves = {{id}};
    if (commitOrder) {
        work.commit = [frame, commitOrder, commitMutex](const AsyncRunResult&) {
            std::lock_guard lock(*commitMutex);
            commitOrder->push_back(frame);
        };
    }
    return work;
}

}  // namespace

TEST(FramePipelineTest, DomainCadenceUsesStableFrameMetadata) {
    LoopDomainMetadata everyFrame{1, 1, 0, TaskLane::FrameCompute, true, true};
    LoopDomainMetadata everyOtherFrame{2, 2, 0, TaskLane::FrameCompute, true, true};

    EXPECT_TRUE(everyFrame.IsDue(0));
    EXPECT_TRUE(everyFrame.IsDue(17));
    EXPECT_FALSE(everyOtherFrame.IsDue(1));
    EXPECT_TRUE(everyOtherFrame.IsDue(2));
    EXPECT_TRUE(everyOtherFrame.IsDue(4));
}

TEST(FramePipelineTest, SlowerCadenceIsAdmittedThroughSharedExecutor) {
    TaskExecutor executor;
    FramePipeline pipeline(executor, 2);
    const LoopDomainMetadata simulation{
        0x53494D4C4F4F5031ULL, 2, 0, TaskLane::FrameCompute, true, true};
    std::atomic<int> executions{0};

    EXPECT_FALSE(pipeline.Submit(OneTask(
        1, 10, simulation, [&] { executions.fetch_add(1, std::memory_order_relaxed); })));
    EXPECT_TRUE(pipeline.Submit(OneTask(
        2, 10, simulation, [&] { executions.fetch_add(1, std::memory_order_relaxed); })));
    pipeline.Drain();

    EXPECT_EQ(executions.load(std::memory_order_relaxed), 1);
}

TEST(FramePipelineTest, AdjacentFramesOverlapButCommitInSubmissionOrder) {
    TaskExecutor executor;
    FramePipeline pipeline(executor, 2);
    const LoopDomainMetadata perFrame{
        0x52454E4445523031ULL, 1, 0, TaskLane::FrameCompute, true, true};
    std::atomic<int> active{0};
    std::atomic<int> maxActive{0};
    std::vector<uint64_t> commitOrder;
    std::mutex commitMutex;

    auto body = [&] {
        const int now = active.fetch_add(1, std::memory_order_relaxed) + 1;
        int observed = maxActive.load(std::memory_order_relaxed);
        while (observed < now && !maxActive.compare_exchange_weak(
                   observed, now, std::memory_order_relaxed)) {
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        active.fetch_sub(1, std::memory_order_relaxed);
    };

    ASSERT_TRUE(pipeline.Submit(OneTask(0, 20, perFrame, body, &commitOrder, &commitMutex)));
    ASSERT_TRUE(pipeline.Submit(OneTask(1, 20, perFrame, body, &commitOrder, &commitMutex)));
    pipeline.Drain();

    EXPECT_GE(maxActive.load(std::memory_order_relaxed), 2);
    ASSERT_EQ(commitOrder.size(), 2u);
    EXPECT_EQ(commitOrder[0], 0u);
    EXPECT_EQ(commitOrder[1], 1u);
}

TEST(FramePipelineTest, EpochChangeDiscardsUnpublishedResults) {
    TaskExecutor executor;
    FramePipeline pipeline(executor, 2);
    pipeline.BeginEpoch(7);
    const LoopDomainMetadata perFrame{
        0x52454E4445523031ULL, 1, 0, TaskLane::FrameCompute, true, true};
    std::atomic<int> commits{0};

    FramePipelineWork work = OneTask(0, 30, perFrame, [] {}, nullptr, nullptr);
    work.commit = [&](const AsyncRunResult&) { commits.fetch_add(1, std::memory_order_relaxed); };
    ASSERT_TRUE(pipeline.Submit(std::move(work)));

    pipeline.BeginEpoch(8);
    pipeline.Drain();

    EXPECT_EQ(commits.load(std::memory_order_relaxed), 0);
    EXPECT_EQ(pipeline.CurrentEpoch(), 8u);
}
