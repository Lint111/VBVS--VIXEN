/**
 * @file test_node_lifecycle_events.cpp
 * @brief A node's lifecycle reactions are its own typed events
 *
 * - Teardown: the graph's reason selects the node's events (CleanupImpl on every teardown,
 *   DeviceReleaseImpl on device loss + shutdown, ShutdownImpl on shutdown only), so a node never
 *   branches on why it is cleaned up (FR-7).
 * - Recompile: RecompileImpl receives the cause (SurfaceResized, InputsChanged, UpstreamRecompiled).
 * - Variadic: a variadic node reads its declared slot sources itself at Compile and before Execute;
 *   no hook is registered by the connection.
 * - Graph-wide hooks (GraphLifecycleHooks) still run per graph phase.
 *
 * CPU-only: opaque VkBuffer values, no Vulkan device.
 */

#include <gtest/gtest.h>

#include "Core/RenderGraph.h"
#include "Core/NodeTypeRegistry.h"
#include "Core/TypedConnection.h"
#include "Core/TypedNodeInstance.h"
#include "Core/VariadicTypedNode.h"
#include "Core/GraphLifecycleHooks.h"
#include "Data/Core/ResourceConfig.h"

#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <vector>

using namespace Vixen::RenderGraph;

namespace {

using EventLog = std::vector<std::string>;

CONSTEXPR_NODE_CONFIG(PersistentSourcePorts, 0, 1, SlotArrayMode::Single) {
    OUTPUT_SLOT(Out, const VkBuffer, 0, SlotNullability::Required, SlotMutability::WriteOnly);

    PersistentSourcePorts() {
        HandleDescriptor desc{"VkBuffer"};
        INIT_OUTPUT_DESC(Out, "out", ResourceLifetime::Persistent, desc);
    }
};

CONSTEXPR_NODE_CONFIG(TransientSourcePorts, 0, 1, SlotArrayMode::Single) {
    OUTPUT_SLOT(Out, const VkBuffer, 0, SlotNullability::Required, SlotMutability::WriteOnly);

    TransientSourcePorts() {
        HandleDescriptor desc{"VkBuffer"};
        INIT_OUTPUT_DESC(Out, "out", ResourceLifetime::Transient, desc);
    }
};

CONSTEXPR_NODE_CONFIG(ConsumerPorts, 1, 0, SlotArrayMode::Single) {
    INPUT_SLOT(In, const VkBuffer, 0, SlotNullability::Required,
               SlotRole::Dependency, SlotMutability::ReadOnly, SlotScope::NodeLevel);

    ConsumerPorts() {
        HandleDescriptor desc{"VkBuffer"};
        INIT_INPUT_DESC(In, "in", ResourceLifetime::Persistent, desc);
    }
};

CONSTEXPR_NODE_CONFIG(NoPorts, 0, 0, SlotArrayMode::Single) {};

VkBuffer FakeBuffer(uintptr_t value) { return reinterpret_cast<VkBuffer>(value); }

// Records every lifecycle event it receives. ShutdownImpl blocks on `deviceIdle` the way a natural
// teardown blocks on vkDeviceWaitIdle: if a recompile ever reached it while frames are "in flight",
// the wait would not return (FR-7's resize freeze); the test bounds it with a timeout instead.
template<typename Ports>
class ProbeNode : public TypedNode<Ports> {
public:
    ProbeNode(const std::string& name, NodeType* type) : TypedNode<Ports>(name, type) {}

    EventLog log;
    std::vector<RecompileCause> causes;
    std::shared_future<void> deviceIdle;
    bool shutdownTimedOut = false;
    uintptr_t nextHandle = 0x1000;

protected:
    using typename TypedNode<Ports>::TypedCompileContext;
    using typename TypedNode<Ports>::TypedExecuteContext;
    using typename TypedNode<Ports>::TypedCleanupContext;

    void TypedCompileImpl(TypedCompileContext& ctx) override {
        log.push_back("compile");
        if constexpr (Ports::OUTPUT_COUNT > 0) {
            ctx.Out(Ports::Out, FakeBuffer(nextHandle++));
        }
    }
    void TypedExecuteImpl(TypedExecuteContext& ctx) override {
        if constexpr (Ports::OUTPUT_COUNT > 0) {
            ctx.Out(Ports::Out, FakeBuffer(nextHandle++));
        }
    }
    void TypedCleanupImpl(TypedCleanupContext&) override { log.push_back("cleanup"); }
    void TypedDeviceReleaseImpl(TypedCleanupContext&) override { log.push_back("device-release"); }
    void TypedShutdownImpl(TypedCleanupContext&) override {
        log.push_back("shutdown");
        if (deviceIdle.valid() &&
            deviceIdle.wait_for(std::chrono::seconds(2)) != std::future_status::ready) {
            shutdownTimedOut = true;
        }
    }
    void RecompileImpl(RecompileCause cause) override { causes.push_back(cause); }
};

template<typename Ports>
class ProbeNodeType final : public TypedNodeType<Ports> {
public:
    explicit ProbeNodeType(const std::string& name) : TypedNodeType<Ports>(name) {}
    std::unique_ptr<NodeInstance> CreateInstance(const std::string& name) const override {
        return std::make_unique<ProbeNode<Ports>>(name, const_cast<ProbeNodeType*>(this));
    }
};

// Variadic consumer: records, per Execute, the resource its slot 0 points at.
class VariadicProbe final : public VariadicTypedNode<NoPorts> {
public:
    VariadicProbe(const std::string& name, NodeType* type) : VariadicTypedNode(name, type) {}

    std::vector<VkBuffer> compiledHandles;
    std::vector<VkBuffer> executedHandles;

protected:
    void VariadicCompileImpl(VariadicCompileContext&) override {
        Resource* res = GetVariadicInputResource(0);
        compiledHandles.push_back(res ? res->GetHandle<VkBuffer>() : VK_NULL_HANDLE);
    }
    void VariadicExecuteImpl(VariadicExecuteContext&) override {
        Resource* res = GetVariadicInputResource(0);
        executedHandles.push_back(res ? res->GetHandle<VkBuffer>() : VK_NULL_HANDLE);
    }
};

class VariadicProbeType final : public NodeType {
public:
    VariadicProbeType() : NodeType("VariadicProbe") {}
    std::unique_ptr<NodeInstance> CreateInstance(const std::string& name) const override {
        return std::make_unique<VariadicProbe>(name, const_cast<VariadicProbeType*>(this));
    }
};

class LifecycleGraph {
public:
    LifecycleGraph() : graph(&registry) {
        registry.RegisterNodeType(std::make_unique<ProbeNodeType<PersistentSourcePorts>>("PersistentSource"));
        registry.RegisterNodeType(std::make_unique<ProbeNodeType<TransientSourcePorts>>("TransientSource"));
        registry.RegisterNodeType(std::make_unique<ProbeNodeType<ConsumerPorts>>("Consumer"));
        registry.RegisterNodeType(std::make_unique<VariadicProbeType>());
    }

    template<typename T>
    T* Get(NodeHandle handle) { return dynamic_cast<T*>(graph.GetInstance(handle)); }

    NodeTypeRegistry registry;
    RenderGraph graph;
};

using PersistentProbe = ProbeNode<PersistentSourcePorts>;
using ConsumerProbe = ProbeNode<ConsumerPorts>;

} // namespace

// ----------------------------------------------------------------------------
// Teardown events follow the graph's reason; the node never sees the reason
// ----------------------------------------------------------------------------

TEST(NodeLifecycleEvents, RecompileTeardownRunsOnlyCleanup) {
    LifecycleGraph g;
    auto* node = g.Get<PersistentProbe>(g.graph.AddNode("PersistentSource", "src"));
    node->Cleanup(CleanupReason::Recompile);
    EXPECT_EQ(node->log, (EventLog{"cleanup"}));
}

TEST(NodeLifecycleEvents, DeviceLossReleasesDeviceScopeButNotShutdownScope) {
    LifecycleGraph g;
    auto* node = g.Get<PersistentProbe>(g.graph.AddNode("PersistentSource", "src"));
    node->Cleanup(CleanupReason::DeviceLost);
    EXPECT_EQ(node->log, (EventLog{"cleanup", "device-release"}));
}

TEST(NodeLifecycleEvents, ShutdownRunsAllScopesInnerToOuter) {
    LifecycleGraph g;
    auto* node = g.Get<PersistentProbe>(g.graph.AddNode("PersistentSource", "src"));
    node->Cleanup(CleanupReason::FinalTeardown);
    EXPECT_EQ(node->log, (EventLog{"cleanup", "device-release", "shutdown"}));
}

// FR-7: a resize recompile must not reach the node's shutdown teardown. ShutdownImpl here blocks
// on a "device idle" that is not reached while the resize is in progress (the natural
// vkDeviceWaitIdle teardown that froze resize when it lived in a cleanup shared with recompile).
TEST(NodeLifecycleEvents, ResizeRecompileNeverReachesBlockingShutdownTeardown) {
    LifecycleGraph g;
    const NodeHandle src = g.graph.AddNode("PersistentSource", "src");
    const NodeHandle dst = g.graph.AddNode("Consumer", "dst");
    g.graph.ConnectNodes(src, 0, dst, 0);
    ASSERT_NO_THROW(g.graph.Compile());

    std::promise<void> deviceIdle;
    auto* source = g.Get<PersistentProbe>(src);
    auto* consumer = g.Get<ConsumerProbe>(dst);
    source->deviceIdle = deviceIdle.get_future().share();
    consumer->deviceIdle = source->deviceIdle;
    source->log.clear();
    consumer->log.clear();

    source->MarkNeedsRecompile(RecompileCause::SurfaceResized);
    g.graph.RecompileDirtyNodes();

    EXPECT_EQ(source->causes, (std::vector<RecompileCause>{RecompileCause::SurfaceResized}));
    EXPECT_EQ(consumer->causes, (std::vector<RecompileCause>{RecompileCause::UpstreamRecompiled}));
    EXPECT_EQ(source->log, (EventLog{"cleanup", "compile"}));
    EXPECT_EQ(consumer->log, (EventLog{"cleanup", "compile"}));
    EXPECT_FALSE(source->shutdownTimedOut);
    EXPECT_FALSE(consumer->shutdownTimedOut);

    // Shutdown: frames drained, so the blocking teardown returns.
    deviceIdle.set_value();
    g.graph.ExecuteCleanup();
    EXPECT_EQ(source->log.back(), "shutdown");
    EXPECT_EQ(consumer->log.back(), "shutdown");
    EXPECT_FALSE(source->shutdownTimedOut);
    EXPECT_FALSE(consumer->shutdownTimedOut);
}

TEST(NodeLifecycleEvents, MarkedDirtyWithoutCauseRecompilesAsInputsChanged) {
    LifecycleGraph g;
    const NodeHandle src = g.graph.AddNode("PersistentSource", "src");
    ASSERT_NO_THROW(g.graph.Compile());
    auto* source = g.Get<PersistentProbe>(src);

    source->MarkNeedsRecompile();
    g.graph.RecompileDirtyNodes();
    EXPECT_EQ(source->causes, (std::vector<RecompileCause>{RecompileCause::InputsChanged}));
}

// ----------------------------------------------------------------------------
// Variadic lifecycle is intrinsic to the variadic node
// ----------------------------------------------------------------------------

TEST(NodeLifecycleEvents, VariadicDependencySlotReadsItsSourceAtCompile) {
    LifecycleGraph g;
    const NodeHandle src = g.graph.AddNode("PersistentSource", "src");
    const NodeHandle sink = g.graph.AddNode("VariadicProbe", "sink");
    ConnectionBatch batch(&g.graph);
    batch.Connect(src, PersistentSourcePorts::Out, sink, 0u);
    batch.RegisterAll();
    ASSERT_NO_THROW(g.graph.Compile());

    auto* probe = g.Get<VariadicProbe>(sink);
    auto* source = g.Get<PersistentProbe>(src);
    ASSERT_FALSE(probe->compiledHandles.empty());
    EXPECT_EQ(probe->compiledHandles.back(), FakeBuffer(0x1000));
    const VariadicSlotInfo* slot = probe->GetVariadicSlotInfo(0);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->resource, source->GetOutput(0, 0));
    EXPECT_FALSE(HasExecute(slot->slotRole));
}

TEST(NodeLifecycleEvents, VariadicTransientSlotFollowsItsSourceEveryExecute) {
    LifecycleGraph g;
    const NodeHandle src = g.graph.AddNode("TransientSource", "src");
    const NodeHandle sink = g.graph.AddNode("VariadicProbe", "sink");
    ConnectionBatch batch(&g.graph);
    batch.Connect(src, TransientSourcePorts::Out, sink, 0u);
    batch.RegisterAll();
    ASSERT_NO_THROW(g.graph.Compile());

    auto* probe = g.Get<VariadicProbe>(sink);
    auto* source = g.graph.GetInstance(src);
    const VariadicSlotInfo* slot = probe->GetVariadicSlotInfo(0);
    ASSERT_NE(slot, nullptr);
    ASSERT_TRUE(HasExecute(slot->slotRole));

    for (int frame = 0; frame < 3; ++frame) {
        source->Execute();
        probe->Execute();
    }
    // Compile produced 0x1000; frames 0..2 produced 0x1001..0x1003, each read by the same frame.
    EXPECT_EQ(probe->executedHandles,
              (std::vector<VkBuffer>{FakeBuffer(0x1001), FakeBuffer(0x1002), FakeBuffer(0x1003)}));
    EXPECT_EQ(probe->GetVariadicSlotInfo(0)->resource, source->GetOutput(0, 0));
}

// ----------------------------------------------------------------------------
// Graph-wide phases stay graph-level
// ----------------------------------------------------------------------------

TEST(NodeLifecycleEvents, GraphHooksRunOncePerGraphPhase) {
    LifecycleGraph g;
    g.graph.AddNode("PersistentSource", "a");
    g.graph.AddNode("PersistentSource", "b");
    int topologyBuilt = 0;
    int compiled = 0;
    g.graph.GetLifecycleHooks().RegisterGraphHook(
        GraphLifecyclePhase::PostTopologyBuild, [&](RenderGraph*) { ++topologyBuilt; }, "topology");
    g.graph.GetLifecycleHooks().RegisterGraphHook(
        GraphLifecyclePhase::PostCompilation, [&](RenderGraph*) { ++compiled; }, "compiled");
    ASSERT_NO_THROW(g.graph.Compile());
    EXPECT_EQ(topologyBuilt, 1);
    EXPECT_EQ(compiled, 1);
}
