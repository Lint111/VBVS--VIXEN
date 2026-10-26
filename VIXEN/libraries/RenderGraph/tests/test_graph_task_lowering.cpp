#include "Nodes/Common/GpuChunkCost.h"
#include <gtest/gtest.h>

#include "Core/GraphTaskLowering.h"
#include "Core/NodeType.h"
#include "Core/RenderGraph.h"
#include "KernelDispatch/TaskExecutor.h"
#include "Core/QueueOwnerTestFixture.h"
#include "QueueSubmitRequest.h"
#include "VulkanDevice.h"

#include <array>
#include <atomic>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace Vixen::RenderGraph;

namespace {

class LoweringNode final : public NodeInstance {
public:
    LoweringNode(const std::string& name, NodeType* type)
        : NodeInstance(name, type) {}

    void SetWork(std::function<void()> work) { work_ = std::move(work); }

protected:
    void ExecuteImpl() override {
        if (work_) work_();
    }

private:
    std::function<void()> work_;
};

class LoweringNodeType final : public NodeType {
public:
    LoweringNodeType(const std::string& name, GraphExecutionKind executionKind)
        : NodeType(name) {
        graphExecutionKind = executionKind;
        inputSchema.resize(1);
        inputSchema[0].nullable = true;
        outputSchema.resize(1);
    }

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName) const override {
        return std::make_unique<LoweringNode>(
            instanceName, const_cast<LoweringNodeType*>(this));
    }
};

class ScopedEnvironment final {
public:
    ScopedEnvironment(const char* name, const char* value)
        : name_(name) {
        if (const char* previous = std::getenv(name_)) {
            previous_ = std::string(previous);
        }
#ifdef _WIN32
        _putenv_s(name_, value);
#else
        setenv(name_, value, 1);
#endif
    }

    ~ScopedEnvironment() {
        if (previous_) {
#ifdef _WIN32
            _putenv_s(name_, previous_->c_str());
#else
            setenv(name_, previous_->c_str(), 1);
#endif
        } else {
#ifdef _WIN32
            _putenv_s(name_, "");
#else
            unsetenv(name_);
#endif
        }
    }

private:
    const char* name_;
    std::optional<std::string> previous_;
};

class GraphTaskLoweringTest : public ::testing::Test {
protected:
    LoweringNode* AddNode(LoweringNodeType& type, const char* name) {
        auto node = type.CreateInstance(name);
        auto* result = static_cast<LoweringNode*>(node.get());
        nodes_.push_back(std::move(node));
        return result;
    }

    Resource* AddResource() {
        resources_.push_back(std::make_unique<Resource>());
        return resources_.back().get();
    }

    static void AddOutput(NodeInstance* node, Resource* resource) {
        auto& bundles = const_cast<std::vector<NodeInstance::Bundle>&>(node->GetBundles());
        if (bundles.empty()) bundles.emplace_back();
        if (bundles[0].outputs.empty()) bundles[0].outputs.resize(1, nullptr);
        bundles[0].outputs[0] = resource;
    }

    static void AddInput(NodeInstance* node, Resource* resource) {
        auto& bundles = const_cast<std::vector<NodeInstance::Bundle>&>(node->GetBundles());
        if (bundles.empty()) bundles.emplace_back();
        if (bundles[0].inputs.empty()) bundles[0].inputs.resize(1, nullptr);
        bundles[0].inputs[0] = resource;
    }

    static void ResetTasks(GraphTaskPlan& plan) {
        for (auto& task : plan.tasks) {
            task.state = Vixen::KernelDispatch::VirtualTaskState::Pending;
            task.errorMessage.clear();
        }
    }

    std::vector<std::unique_ptr<NodeInstance>> nodes_;
    std::vector<std::unique_ptr<Resource>> resources_;
};

TEST_F(GraphTaskLoweringTest, TopologyEdgesBecomeSharedDagEdgesAndWaves) {
    LoweringNodeType cpuType("Cpu", GraphExecutionKind::Cpu);
    auto* a = AddNode(cpuType, "A");
    auto* b = AddNode(cpuType, "B");
    auto* c = AddNode(cpuType, "C");

    GraphTopology topology;
    topology.AddNode(a);
    topology.AddNode(b);
    topology.AddNode(c);
    topology.AddEdge({a, 0, b, 0});
    topology.AddEdge({a, 0, c, 0});

    ResourceAccessTracker tracker;
    tracker.BuildFromTopology(topology);
    const std::vector<NodeInstance*> order{a, b, c};
    auto plan = GraphTaskLowering::Build(topology, order, tracker);

    ASSERT_EQ(plan.tasks.size(), 3u);
    EXPECT_EQ(plan.dependencyGraph.GetEdgeCount(), topology.GetEdgeCount());
    EXPECT_TRUE(plan.dependencyGraph.HasDependency(plan.FindTaskId(a), plan.FindTaskId(b)));
    EXPECT_TRUE(plan.dependencyGraph.HasDependency(plan.FindTaskId(a), plan.FindTaskId(c)));
    ASSERT_EQ(plan.waves.size(), 2u);
    EXPECT_EQ(plan.waves[0].size(), 1u);
    EXPECT_EQ(plan.waves[1].size(), 2u);
}

TEST_F(GraphTaskLoweringTest, AccessHazardAddsConservativeOrderWhenConnectionIsAbsent) {
    LoweringNodeType cpuType("Cpu", GraphExecutionKind::Cpu);
    auto* writer = AddNode(cpuType, "Writer");
    auto* reader = AddNode(cpuType, "Reader");
    Resource* resource = AddResource();
    AddOutput(writer, resource);
    AddInput(reader, resource);

    GraphTopology topology;
    topology.AddNode(writer);
    topology.AddNode(reader);
    ResourceAccessTracker tracker;
    tracker.BuildFromTopology(topology);

    auto plan = GraphTaskLowering::Build(
        topology, std::vector<NodeInstance*>{writer, reader}, tracker);

    EXPECT_EQ(plan.dependencyGraph.GetEdgeCount(), 1u);
    EXPECT_TRUE(plan.dependencyGraph.HasDependency(
        plan.FindTaskId(writer), plan.FindTaskId(reader)));
    ASSERT_EQ(plan.waves.size(), 2u);
}

TEST_F(GraphTaskLoweringTest, NonCpuNodesRemainOnOneOrderedLane) {
    LoweringNodeType cpuType("Cpu", GraphExecutionKind::Cpu);
    LoweringNodeType serialType("Serial", GraphExecutionKind::Serial);
    auto* firstCpu = AddNode(cpuType, "FirstCpu");
    auto* firstSerial = AddNode(serialType, "FirstSerial");
    auto* secondCpu = AddNode(cpuType, "SecondCpu");
    auto* secondSerial = AddNode(serialType, "SecondSerial");

    GraphTopology topology;
    topology.AddNode(firstCpu);
    topology.AddNode(firstSerial);
    topology.AddNode(secondCpu);
    topology.AddNode(secondSerial);
    ResourceAccessTracker tracker;
    tracker.BuildFromTopology(topology);
    const std::vector<NodeInstance*> order{
        firstCpu, firstSerial, secondCpu, secondSerial};
    auto plan = GraphTaskLowering::Build(topology, order, tracker);

    EXPECT_TRUE(plan.dependencyGraph.HasDependency(
        plan.FindTaskId(firstSerial), plan.FindTaskId(secondSerial)));
    ASSERT_EQ(plan.waves.size(), 2u);
    EXPECT_EQ(plan.waves[0].size(), 3u);
    EXPECT_EQ(plan.waves[1].size(), 1u);
}

TEST_F(GraphTaskLoweringTest, OutputIsInvariantAcrossWorkerCountsAndQueuedRuns) {
    LoweringNodeType cpuType("Cpu", GraphExecutionKind::Cpu);
    std::array<uint32_t, 3> output{};
    std::array<LoweringNode*, 3> nodePtrs{
        AddNode(cpuType, "A"), AddNode(cpuType, "B"), AddNode(cpuType, "C")};
    for (size_t index = 0; index < nodePtrs.size(); ++index) {
        nodePtrs[index]->SetWork([&output, index] {
            output[index] = static_cast<uint32_t>((index + 1) * 17);
        });
    }

    GraphTopology topology;
    for (LoweringNode* node : nodePtrs) topology.AddNode(node);
    ResourceAccessTracker tracker;
    tracker.BuildFromTopology(topology);
    auto plan = GraphTaskLowering::Build(
        topology,
        std::vector<NodeInstance*>{nodePtrs[0], nodePtrs[1], nodePtrs[2]},
        tracker);
    ASSERT_EQ(plan.waves.size(), 1u);

    const int nativeWorkerCount = static_cast<int>(
        std::max(3u, std::thread::hardware_concurrency()));
    std::array<uint32_t, 3> expected{};
    for (const int workerCount : {1, 2, nativeWorkerCount}) {
        for (int run = 0; run < 3; ++run) {
            output.fill(0);
            ResetTasks(plan);
            Vixen::KernelDispatch::TaskExecutor executor;
            ASSERT_TRUE(executor.Run(plan.tasks, plan.waves, workerCount));
            if (workerCount == 1 && run == 0) expected = output;
            EXPECT_EQ(output, expected);
        }
    }
}

TEST_F(GraphTaskLoweringTest, ErrorSelectionIsStableAcrossWorkerCounts) {
    LoweringNodeType cpuType("Cpu", GraphExecutionKind::Cpu);
    auto* first = AddNode(cpuType, "FirstFailure");
    auto* second = AddNode(cpuType, "SecondFailure");
    first->SetWork([] { throw std::runtime_error("first failure"); });
    second->SetWork([] { throw std::runtime_error("second failure"); });

    GraphTopology topology;
    topology.AddNode(first);
    topology.AddNode(second);
    ResourceAccessTracker tracker;
    tracker.BuildFromTopology(topology);
    auto plan = GraphTaskLowering::Build(
        topology, std::vector<NodeInstance*>{first, second}, tracker);
    ASSERT_EQ(plan.waves.size(), 1u);

    const int nativeWorkerCount = static_cast<int>(
        std::max(3u, std::thread::hardware_concurrency()));
    for (const int workerCount : {1, 2, nativeWorkerCount}) {
        ResetTasks(plan);
        Vixen::KernelDispatch::TaskExecutor executor;
        EXPECT_FALSE(executor.Run(plan.tasks, plan.waves, workerCount));
        ASSERT_FALSE(executor.GetErrors().empty());
        EXPECT_EQ(executor.GetErrors().front().task, plan.FindTaskId(first));
        EXPECT_EQ(executor.GetErrors().front().message, "first failure");
    }
}

TEST_F(GraphTaskLoweringTest, WaveCompletionRunsBetweenDependentWaves) {
    LoweringNodeType cpuType("Cpu", GraphExecutionKind::Cpu);
    auto* first = AddNode(cpuType, "First");
    auto* second = AddNode(cpuType, "Second");

    GraphTopology topology;
    topology.AddNode(first);
    topology.AddNode(second);
    topology.AddEdge({first, 0, second, 0});
    ResourceAccessTracker tracker;
    tracker.BuildFromTopology(topology);
    auto plan = GraphTaskLowering::Build(
        topology, std::vector<NodeInstance*>{first, second}, tracker);

    std::vector<Vixen::KernelDispatch::TaskId> committed;
    Vixen::KernelDispatch::TaskExecutor executor;
    ASSERT_TRUE(executor.Run(
        plan.tasks,
        plan.waves,
        2,
        {},
        [&committed](const std::vector<Vixen::KernelDispatch::TaskId>& wave) {
            committed.insert(committed.end(), wave.begin(), wave.end());
        }));

    ASSERT_EQ(committed.size(), 2u);
    EXPECT_EQ(committed[0], plan.FindTaskId(first));
    EXPECT_EQ(committed[1], plan.FindTaskId(second));
}

TEST_F(GraphTaskLoweringTest, EpochStopPreventsTheNextGraphWave) {
    LoweringNodeType cpuType("Cpu", GraphExecutionKind::Cpu);
    auto* first = AddNode(cpuType, "First");
    auto* second = AddNode(cpuType, "Second");
    NodeTypeRegistry registry;
    RenderGraph graph(&registry, nullptr, nullptr, nullptr);
    const uint64_t epoch = graph.GetExecutionEpoch();
    std::atomic<int> secondCalls{0};
    first->SetWork([&graph] { graph.AbortCurrentFrame(); });
    second->SetWork([&secondCalls] { ++secondCalls; });

    GraphTopology topology;
    topology.AddNode(first);
    topology.AddNode(second);
    topology.AddEdge({first, 0, second, 0});
    ResourceAccessTracker tracker;
    tracker.BuildFromTopology(topology);
    auto plan = GraphTaskLowering::Build(
        topology, std::vector<NodeInstance*>{first, second}, tracker);

    Vixen::KernelDispatch::TaskExecutor executor;
    EXPECT_FALSE(executor.Run(plan.tasks, plan.waves, 2, graph.GetExecutionStopToken()));
    EXPECT_GT(graph.GetExecutionEpoch(), epoch);
    EXPECT_EQ(secondCalls.load(), 0);
    EXPECT_EQ(plan.FindTask(plan.FindTaskId(first))->state,
              Vixen::KernelDispatch::VirtualTaskState::Completed);
    EXPECT_EQ(plan.FindTask(plan.FindTaskId(second))->state,
              Vixen::KernelDispatch::VirtualTaskState::Pending);
}

TEST_F(GraphTaskLoweringTest, RenderFrameUsesLoweredPlanAndHonorsEpochAbort) {
    NodeTypeRegistry registry;
    ASSERT_TRUE(registry.RegisterNodeType(
        std::make_unique<LoweringNodeType>("Cpu", GraphExecutionKind::Cpu)));
    RenderGraph graph(&registry);
    const auto firstHandle = graph.AddNode("Cpu", "GraphFirst");
    const auto secondHandle = graph.AddNode("Cpu", "GraphSecond");
    auto* first = dynamic_cast<LoweringNode*>(graph.GetInstance(firstHandle));
    auto* second = dynamic_cast<LoweringNode*>(graph.GetInstance(secondHandle));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    std::atomic<int> secondCalls{0};
    first->SetWork([&graph] { graph.AbortCurrentFrame(); });
    second->SetWork([&secondCalls] { ++secondCalls; });
    graph.GetTopology().AddEdge({first, 0, second, 0});

    ASSERT_NO_THROW(graph.Compile());
    ASSERT_EQ(graph.GetExecutionTaskPlan().waves.size(), 2u);
    {
        ScopedEnvironment lowered("VIXEN_GRAPH_LOWERED", "1");
        EXPECT_EQ(graph.RenderFrame(), VK_SUCCESS);
    }

    EXPECT_EQ(secondCalls.load(), 0);
    EXPECT_EQ(first->GetState(), NodeState::Complete);
    EXPECT_EQ(second->GetState(), NodeState::Compiled);
}

TEST_F(GraphTaskLoweringTest, CompletedFramesHonorConfiguredBudgetAndKeepTheirCount) {
    for (const char* loweredValue : {"0", "1"}) {
        ScopedEnvironment lowered("VIXEN_GRAPH_LOWERED", loweredValue);
        NodeTypeRegistry registry;
        ASSERT_TRUE(registry.RegisterNodeType(
            std::make_unique<LoweringNodeType>("Cpu", GraphExecutionKind::Cpu)));
        RenderGraph graph(&registry);
        const auto handle = graph.AddNode("Cpu", "PacedWork");
        auto* node = dynamic_cast<LoweringNode*>(graph.GetInstance(handle));
        ASSERT_NE(node, nullptr);
        unsigned calls = 0;
        node->SetWork([&calls] { ++calls; });
        TimelineCapacityTracker::Config config;
        config.gpuTimeBudgetNs = 8'000'000;
        graph.ConfigureCapacityTracking(config);
        ASSERT_NO_THROW(graph.Compile());
        const auto start = std::chrono::steady_clock::now();
        for (unsigned i = 0; i < 3; ++i) EXPECT_EQ(graph.RenderFrame(), VK_SUCCESS);
        const auto elapsed = std::chrono::steady_clock::now() - start;
        EXPECT_GE(elapsed, std::chrono::nanoseconds(config.gpuTimeBudgetNs * 3));
        EXPECT_EQ(calls, 3u);

        // An over-budget frame must not consume the following frame's pacing deadline.
        node->SetWork([&calls, &config] {
            ++calls;
            std::this_thread::sleep_for(std::chrono::nanoseconds(config.gpuTimeBudgetNs * 2));
        });
        EXPECT_EQ(graph.RenderFrame(), VK_SUCCESS);
        node->SetWork([&calls] { ++calls; });
        const auto nextStart = std::chrono::steady_clock::now();
        EXPECT_EQ(graph.RenderFrame(), VK_SUCCESS);
        EXPECT_GE(std::chrono::steady_clock::now() - nextStart,
                  std::chrono::nanoseconds(config.gpuTimeBudgetNs));
        EXPECT_EQ(calls, 5u);
    }
}

struct NativeSubmitCall {
    uint32_t waits, commands, signals;
    VkFence fence;
    VkCommandBuffer command;
};
std::vector<NativeSubmitCall> nativeSubmitCalls;
int failNativeSubmit = -1;
// A fake GPU: the timeline counter only advances when the host waits on it (or a test sets it).
uint64_t fakeCounter = 0;
int fakeWaitCalls = 0;
int failFakeWait = -1;
std::vector<uint64_t> fakeWaitedValues;
VKAPI_ATTR VkResult VKAPI_CALL CaptureNativeSubmit(
    VkQueue, uint32_t count, const VkSubmitInfo2* infos, VkFence fence) {
    EXPECT_EQ(count, 1u);
    const auto& si = infos[0];
    nativeSubmitCalls.push_back({si.waitSemaphoreInfoCount, si.commandBufferInfoCount,
        si.signalSemaphoreInfoCount, fence,
        si.commandBufferInfoCount ? si.pCommandBufferInfos[0].commandBuffer : VK_NULL_HANDLE});
    if (static_cast<int>(nativeSubmitCalls.size()) == failNativeSubmit)
        return VK_ERROR_DEVICE_LOST;
    return VK_SUCCESS;
}
const SubmitRecord::IssueFn CaptureIssue = [](const VkSubmitInfo2& info, VkFence fence) {
    return CaptureNativeSubmit(VK_NULL_HANDLE, 1, &info, fence);
};
VkResult FakeCounter(VkDevice, VkSemaphore, uint64_t* value) {
    *value = fakeCounter;
    return VK_SUCCESS;
}
VkResult FakeWait(VkDevice, VkSemaphore, uint64_t value, uint64_t) {
    if (++fakeWaitCalls == failFakeWait) return VK_ERROR_DEVICE_LOST;
    fakeWaitedValues.push_back(value);
    fakeCounter = std::max(fakeCounter, value);
    return VK_SUCCESS;
}
QueueTimeline FakeTimeline() {
    nativeSubmitCalls.clear();
    failNativeSubmit = -1;
    fakeCounter = 0;
    fakeWaitCalls = 0;
    failFakeWait = -1;
    fakeWaitedValues.clear();
    QueueTimeline timeline;
    timeline.semaphore = reinterpret_cast<VkSemaphore>(uintptr_t(42));
    timeline.counterFn = FakeCounter;
    timeline.waitFn = FakeWait;
    return timeline;
}

TEST(SubmitRecord, TilesConsumeWaitsOnceAndCompleteOnlyAfterFinalSubmit) {
    SubmitRecord record;
    record.waits.resize(2);
    record.signals.resize(3);
    record.fence = reinterpret_cast<VkFence>(uintptr_t(7));
    for (uintptr_t i = 1; i <= 3; ++i) {
        VkCommandBufferSubmitInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        info.commandBuffer = reinterpret_cast<VkCommandBuffer>(i);
        record.commandBuffers.push_back(info);
    }
    QueueTimeline timeline = FakeTimeline();
    EXPECT_EQ(record.Submit(CaptureIssue, timeline), VK_SUCCESS);
    ASSERT_EQ(nativeSubmitCalls.size(), 3u);
    // R503 on the timeline: the third batch waits for the OLDEST batch (value 1) only.
    ASSERT_EQ(fakeWaitedValues, std::vector<uint64_t>{1});
    EXPECT_LE(timeline.outstanding.size(), static_cast<std::size_t>(SubmitRecord::kOutstandingBatchCap));
    EXPECT_EQ(timeline.lastSignaled, 3u);

    for (size_t i = 0; i < nativeSubmitCalls.size(); ++i) {
        EXPECT_EQ(nativeSubmitCalls[i].commands, 1u);
        EXPECT_EQ(nativeSubmitCalls[i].command, record.commandBuffers[i].commandBuffer);
        EXPECT_EQ(nativeSubmitCalls[i].waits, i == 0 ? 2u : 0u);
        // Every batch signals the queue timeline; the final one also carries the record's signals.
        EXPECT_EQ(nativeSubmitCalls[i].signals, i == 2 ? 4u : 1u);
        EXPECT_EQ(nativeSubmitCalls[i].fence, i == 2 ? record.fence : VK_NULL_HANDLE);
    }
    timeline = FakeTimeline();
    failNativeSubmit = 2;
    EXPECT_EQ(record.Submit(CaptureIssue, timeline), VK_ERROR_DEVICE_LOST);
    EXPECT_EQ(nativeSubmitCalls.size(), 2u);
    EXPECT_EQ(timeline.lastSignaled, 1u);  // the failed batch's value is never recorded as issued
    failNativeSubmit = -1;
}

TEST(SubmitRecord, RetirementFailureStopsBeforeTheNextChunkAndFinalSignal) {
    SubmitRecord record;
    record.signals.resize(1);
    for (uintptr_t i = 1; i <= 3; ++i) {
        VkCommandBufferSubmitInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        command.commandBuffer = reinterpret_cast<VkCommandBuffer>(i);
        record.commandBuffers.push_back(command);
    }
    QueueTimeline timeline = FakeTimeline();
    failFakeWait = 1;
    EXPECT_EQ(record.Submit(CaptureIssue, timeline), VK_ERROR_DEVICE_LOST);
    // R503: two chunks were queued before the failing retirement; the final chunk never ran.
    ASSERT_EQ(nativeSubmitCalls.size(), 2u);
    for (const auto& call : nativeSubmitCalls) {
        EXPECT_EQ(call.signals, 1u);  // the queue timeline only, never the record's own signal
        EXPECT_EQ(call.fence, VK_NULL_HANDLE);
    }
}

TEST(SubmitRecord, TimelineKeepsTwoBatchesOutstandingAcrossRecords) {
    // R503 + R715: the queue-wide timeline lets the second record's first batch follow the first
    // record's batch without a host round trip; the third batch waits for the oldest value only.
    SubmitRecord first, second;
    for (uintptr_t i = 1; i <= 2; ++i) {
        VkCommandBufferSubmitInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        command.commandBuffer = reinterpret_cast<VkCommandBuffer>(i);
        (i == 1 ? first : second).commandBuffers.push_back(command);
    }
    VkCommandBufferSubmitInfo third{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    third.commandBuffer = reinterpret_cast<VkCommandBuffer>(uintptr_t(3));
    second.commandBuffers.push_back(third);
    second.commandCostUpperBoundsNs = {2'000'000, 2'000'000};  // two batches
    QueueTimeline timeline = FakeTimeline();
    ASSERT_EQ(first.Submit(CaptureIssue, timeline), VK_SUCCESS);
    EXPECT_EQ(fakeWaitCalls, 0);
    EXPECT_EQ(timeline.outstanding.size(), 1u);
    ASSERT_EQ(second.Submit(CaptureIssue, timeline), VK_SUCCESS);
    ASSERT_EQ(nativeSubmitCalls.size(), 3u);
    ASSERT_EQ(fakeWaitedValues, std::vector<uint64_t>{1});  // only before the third batch
    EXPECT_EQ(timeline.outstanding.size(), 2u);              // the queue is never drained to zero
}

TEST(SubmitRecord, ReachedOldestBatchIsPolledNotWaited) {
    // R715 pattern 5: when the GPU keeps up, reaching the cap costs one counter read and no wait.
    SubmitRecord record;
    for (uintptr_t i = 1; i <= 3; ++i) {
        VkCommandBufferSubmitInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        command.commandBuffer = reinterpret_cast<VkCommandBuffer>(i);
        record.commandBuffers.push_back(command);
    }
    QueueTimeline timeline = FakeTimeline();
    fakeCounter = 1;  // the first batch has already completed when the third is issued
    ASSERT_EQ(record.Submit(CaptureIssue, timeline), VK_SUCCESS);
    EXPECT_EQ(nativeSubmitCalls.size(), 3u);
    EXPECT_EQ(fakeWaitCalls, 0);
    EXPECT_EQ(timeline.blockingWaits, 0u);
    EXPECT_EQ(timeline.outstanding.size(), 2u);
}

TEST(SubmitRecord, ReleaseRunsOnlyOnceItsValueIsReached) {
    QueueTimeline timeline = FakeTimeline();
    int released = 0;
    timeline.lastSignaled = 2;
    timeline.Issued(1);
    timeline.Issued(2);
    timeline.Defer(2, [&released] { ++released; });
    fakeCounter = 1;
    ASSERT_EQ(timeline.Poll(), VK_SUCCESS);
    EXPECT_TRUE(timeline.TakeCompleted().empty());
    fakeCounter = 2;
    ASSERT_EQ(timeline.Poll(), VK_SUCCESS);
    for (auto& release : timeline.TakeCompleted()) release();
    EXPECT_EQ(released, 1);
    EXPECT_TRUE(timeline.outstanding.empty());
    timeline.Defer(1, [&released] { ++released; });  // already reached: runs at once
    EXPECT_EQ(released, 2);
    timeline.Defer(5, [&released] { ++released; });
    for (auto& release : timeline.TakeAll()) release();  // teardown releases everything
    EXPECT_EQ(released, 3);
}

TEST(SubmitRecord, CalibratedChunksBatchWithinBudgetAndKeepFinalOwnership) {
    SubmitRecord record;
    record.waits.resize(2);
    record.signals.resize(3);
    record.fence = reinterpret_cast<VkFence>(uintptr_t(9));
    for (uintptr_t i = 1; i <= 4; ++i) {
        VkCommandBufferSubmitInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        command.commandBuffer = reinterpret_cast<VkCommandBuffer>(i);
        record.commandBuffers.push_back(command);
    }
    record.commandCostUpperBoundsNs = {600'000, 500'000, 800'000, 100'001};
    const auto batches = record.SubmissionBatches();
    ASSERT_EQ(batches.size(), 2u);
    EXPECT_EQ(batches[0].first, 0u);
    EXPECT_EQ(batches[0].count, 3u);
    EXPECT_EQ(batches[1].first, 3u);
    EXPECT_EQ(batches[1].count, 1u);
    QueueTimeline timeline = FakeTimeline();
    ASSERT_EQ(record.Submit(CaptureIssue, timeline), VK_SUCCESS);
    ASSERT_EQ(nativeSubmitCalls.size(), 2u);
    EXPECT_EQ(nativeSubmitCalls[0].commands, 3u);
    EXPECT_EQ(nativeSubmitCalls[0].waits, 2u);
    EXPECT_EQ(nativeSubmitCalls[0].signals, 1u);
    EXPECT_EQ(nativeSubmitCalls[1].waits, 0u);
    EXPECT_EQ(nativeSubmitCalls[1].signals, 4u);
    EXPECT_EQ(nativeSubmitCalls[1].fence, record.fence);
    EXPECT_EQ(timeline.outstanding.size(), 2u);  // R503: both batches queued with no host wait
    EXPECT_EQ(fakeWaitCalls, 0);
}

TEST(SubmitRecord, UnknownOrMissingCostReservesAWholeSubmission) {
    SubmitRecord record;
    record.commandBuffers.resize(4);
    record.commandCostUpperBoundsNs = {500'000, 0, 500'000};
    const auto batches = record.SubmissionBatches();
    ASSERT_EQ(batches.size(), 4u);
    for (const auto& batch : batches) EXPECT_EQ(batch.count, 1u);
    record.commandCostUpperBoundsNs = {1'000'000, 1'000'000, 0, 3'000'000};
    const auto exact = record.SubmissionBatches();
    ASSERT_EQ(exact.size(), 3u);
    EXPECT_EQ(exact[0].count, 2u);
    EXPECT_EQ(exact[1].count, 1u);
    EXPECT_EQ(exact[2].count, 1u);
}

TEST(SubmitRecord, SemaphoreOnlyCompletionStillSubmitsItsFence) {
    VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    VkSubmitInfo2 info{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    info.signalSemaphoreInfoCount = 1;
    info.pSignalSemaphoreInfos = &signal;
    const auto fence = reinterpret_cast<VkFence>(uintptr_t(7));
    const auto record = SubmitRecord::FromSubmitInfo2(info, fence);
    QueueTimeline timeline = FakeTimeline();
    EXPECT_EQ(record.Submit(CaptureIssue, timeline), VK_SUCCESS);
    ASSERT_EQ(nativeSubmitCalls.size(), 1u);
    EXPECT_EQ(nativeSubmitCalls[0].commands, 0u);
    EXPECT_EQ(nativeSubmitCalls[0].waits, 0u);
    EXPECT_EQ(nativeSubmitCalls[0].signals, 2u);
    EXPECT_EQ(nativeSubmitCalls[0].fence, fence);
}

TEST(SubmitRecord, LegacySubmitInfoKeepsStagesAndTimelineValues) {
    VkSemaphore wait = reinterpret_cast<VkSemaphore>(uintptr_t(3));
    VkSemaphore signal = reinterpret_cast<VkSemaphore>(uintptr_t(4));
    VkCommandBuffer command = reinterpret_cast<VkCommandBuffer>(uintptr_t(5));
    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    const uint64_t waitValue = 11, signalValue = 12;
    VkTimelineSemaphoreSubmitInfo values{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    values.waitSemaphoreValueCount = 1;
    values.pWaitSemaphoreValues = &waitValue;
    values.signalSemaphoreValueCount = 1;
    values.pSignalSemaphoreValues = &signalValue;
    VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    info.pNext = &values;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &wait;
    info.pWaitDstStageMask = &stage;
    info.commandBufferCount = 1;
    info.pCommandBuffers = &command;
    info.signalSemaphoreCount = 1;
    info.pSignalSemaphores = &signal;
    const auto record = SubmitRecord::FromSubmitInfo(info, VK_NULL_HANDLE);
    ASSERT_EQ(record.waits.size(), 1u);
    EXPECT_EQ(record.waits[0].semaphore, wait);
    EXPECT_EQ(record.waits[0].value, waitValue);
    EXPECT_EQ(record.waits[0].stageMask, static_cast<VkPipelineStageFlags2>(stage));
    ASSERT_EQ(record.commandBuffers.size(), 1u);
    EXPECT_EQ(record.commandBuffers[0].commandBuffer, command);
    ASSERT_EQ(record.signals.size(), 1u);
    EXPECT_EQ(record.signals[0].semaphore, signal);
    EXPECT_EQ(record.signals[0].value, signalValue);
}

} // namespace

TEST(GpuChunkCost, ClippedLatencyDoesNotAmplifyThroughputCost) {
    GpuChunkCost cost;
    cost.Observe(49, 49, 1'200'000);
    cost.Observe(1, 49, 800'000);
    EXPECT_GE(cost.Edge(2'000'000, 63, 7), 5u);
    EXPECT_GE(cost.Estimate(1), 880'000u);
    EXPECT_GE(cost.Estimate(49), 1'320'000u);
}
TEST(GpuChunkCost, LargerLaunchRetainsEveryEarlierSampleBound) {
    GpuChunkCost cost;
    cost.Observe(49, 49, 20'000);
    cost.Observe(4, 49, 8'000);
    cost.Observe(1024, 1024, 100'000);
    EXPECT_GE(cost.Estimate(49), 22'000u);
    EXPECT_GE(cost.Estimate(4), 8'800u);
    EXPECT_GE(cost.Estimate(1024), 110'000u);
}
TEST(GpuChunkCost, FixedLatencyFloorRetainsLaunchScaleAndUnknownReservesBudget) {
    GpuChunkCost cost;
    EXPECT_EQ(cost.Estimate(49), 0u);
    cost.Observe(49, 49, 1'000'000);
    cost.Observe(1, 49, 3'000'000);
    EXPECT_EQ(cost.Edge(2'000'000, 63, 7), 7u);
    EXPECT_GT(cost.Estimate(1), 2'000'000u);
}

TEST(GpuChunkCost, NearBudgetFixedLatencyDoesNotCollapseIntoThousandsOfCommands) {
    GpuChunkCost cost;
    cost.Observe(49, 49, 1'200'000);
    cost.Observe(1, 49, 1'700'000);
    EXPECT_GE(cost.Edge(2'000'000, 63, 7), 7u);
}

TEST(GpuChunkCost, FixedLatencyDoesNotHideAnOverBudgetFullTile) {
    GpuChunkCost cost;
    cost.Observe(49, 49, 1'000'000);
    cost.Observe(1, 49, 3'000'000);
    cost.Observe(49, 49, 4'000'000);
    EXPECT_LT(cost.Edge(2'000'000, 63, 3), 7u);
    EXPECT_GE(cost.Edge(2'000'000, 63, 3), 4u);
    EXPECT_GE(cost.Estimate(49), 4'400'000u);
}

TEST(GpuChunkCost, SmallerFullTilesUpdateThroughputAfterLargerCalibration) {
    // Native 1080p: the anchor was set by a 134-edge tile; later 121-edge full
    // tiles ran 2.6 ms and were filed as latency, so the launch never shrank.
    GpuChunkCost cost;
    cost.Observe(64, 64, 1'500'000);
    cost.Observe(49, 49, 4'000'000);
    EXPECT_GE(cost.Estimate(49), 4'400'000u);
    EXPECT_LE(cost.Edge(2'000'000, 63, 3), 4u);
    cost.Observe(16, 16, 1'000'000);
    EXPECT_LE(cost.Edge(2'000'000, 63, 3), 4u);
}

TEST(GpuChunkCost, ImportedEnvelopeStartsWarmAndRejectsEmptyState) {
    // R503: a persisted envelope reproduces the learned launch; an empty or invalid
    // record leaves the model cold.
    GpuChunkCost learned;
    learned.Observe(14400, 14400, 1'680'000);
    learned.Observe(1800, 14400, 900'000);
    GpuChunkCost warm;
    warm.Import(learned.AnchorUnits(), learned.UnitNs(), learned.FixedNs());
    EXPECT_TRUE(warm.Calibrated());
    EXPECT_EQ(warm.Edge(2'000'000, 240, 7), learned.Edge(2'000'000, 240, 7));
    EXPECT_EQ(warm.Estimate(14400), learned.Estimate(14400));
    GpuChunkCost cold;
    cold.Import(0, 0.0, 0.0);
    EXPECT_FALSE(cold.Calibrated());
    cold.Import(49, -1.0, 0.0);
    EXPECT_FALSE(cold.Calibrated());
    EXPECT_EQ(cold.Edge(2'000'000, 63, 7), 7u);
}

TEST(GpuChunkCost, EdgeNeverSubdividesBelowTheColdLaunch) {
    GpuChunkCost cost;
    EXPECT_EQ(cost.Edge(2'000'000, 63, 7), 7u);
    cost.Observe(49, 49, 1'000'000);
    cost.Observe(49, 49, 50'000'000);
    EXPECT_EQ(cost.Edge(2'000'000, 63, 7), 7u);
    EXPECT_EQ(cost.Edge(2'000'000, 5, 7), 5u);
    EXPECT_GE(cost.Estimate(49), 55'000'000u);
}

TEST(QueueOwnerFixture, DrainsPublishedRequestIntoOwnerRecord) {
    Vixen::Vulkan::Resources::VulkanDevice device(nullptr);
    Vixen::RenderGraph::Test::QueueOwnerTestFixture owner;
    owner.Attach(device);

    auto request = Vixen::Vulkan::Resources::QueueSubmitRequest::FromSubmitInfo(
        &device, VK_NULL_HANDLE, VkSubmitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO});
    const auto completion = device.PublishSubmitRequest(std::move(request));
    ASSERT_NE(completion, nullptr);
    EXPECT_EQ(completion->Result(), VK_NOT_READY);

    owner.Drain();
    ASSERT_EQ(owner.Records().size(), 1u);
    EXPECT_EQ(owner.Records().front().device, &device);
    EXPECT_EQ(owner.Records().front().completion, completion);
    EXPECT_EQ(owner.Records().front().commandBuffers.size(), 0u);
}
