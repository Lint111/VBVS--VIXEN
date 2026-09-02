#include <gtest/gtest.h>

#include "Core/GraphTaskLowering.h"
#include "Core/NodeType.h"
#include "Core/RenderGraph.h"
#include "KernelDispatch/TaskExecutor.h"

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
        setenv(name_, value, 1);
    }

    ~ScopedEnvironment() {
        if (previous_) {
            setenv(name_, previous_->c_str(), 1);
        } else {
            unsetenv(name_);
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

} // namespace
