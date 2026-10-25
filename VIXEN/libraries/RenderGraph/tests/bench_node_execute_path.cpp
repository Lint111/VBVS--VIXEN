/**
 * @file bench_node_execute_path.cpp
 * @brief Native CPU cost of the per-frame node execute path (no GPU)
 *
 * Builds a 40-node graph (32 plain typed nodes, 8 transient sources feeding one variadic consumer)
 * and times node->Execute() over every node, the work the graph's per-frame loop does around each
 * node's ExecuteImpl. Uses only API that predates the per-node lifecycle refactor, so the same file
 * measures the base (generic hook registry walked per Execute) and the refactor.
 *
 * Opt-in: runs only with VIXEN_BENCH_EXECUTE_PATH=1; prints ns/frame per round.
 */

#include <gtest/gtest.h>

#include "Core/RenderGraph.h"
#include "Core/NodeTypeRegistry.h"
#include "Core/TypedConnection.h"
#include "Core/TypedNodeInstance.h"
#include "Core/VariadicTypedNode.h"
#include "Data/Core/ResourceConfig.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace Vixen::RenderGraph;

namespace {

CONSTEXPR_NODE_CONFIG(BenchSourcePorts, 0, 1, SlotArrayMode::Single) {
    OUTPUT_SLOT(Out, const VkBuffer, 0, SlotNullability::Required, SlotMutability::WriteOnly);

    BenchSourcePorts() {
        HandleDescriptor desc{"VkBuffer"};
        INIT_OUTPUT_DESC(Out, "out", ResourceLifetime::Transient, desc);
    }
};

CONSTEXPR_NODE_CONFIG(BenchEmptyPorts, 0, 0, SlotArrayMode::Single) {};

class BenchSource final : public TypedNode<BenchSourcePorts> {
public:
    BenchSource(const std::string& name, NodeType* type) : TypedNode(name, type) {}
    uintptr_t next = 0x1000;

protected:
    void TypedCompileImpl(TypedCompileContext& ctx) override {
        ctx.Out(BenchSourcePorts::Out, reinterpret_cast<VkBuffer>(next++));
    }
    void TypedExecuteImpl(TypedExecuteContext& ctx) override {
        ctx.Out(BenchSourcePorts::Out, reinterpret_cast<VkBuffer>(next++));
    }
};

class BenchSourceType final : public TypedNodeType<BenchSourcePorts> {
public:
    BenchSourceType() : TypedNodeType("BenchSource") {}
    std::unique_ptr<NodeInstance> CreateInstance(const std::string& name) const override {
        return std::make_unique<BenchSource>(name, const_cast<BenchSourceType*>(this));
    }
};

class BenchPlain final : public TypedNode<BenchEmptyPorts> {
public:
    BenchPlain(const std::string& name, NodeType* type) : TypedNode(name, type) {}
    uint64_t ticks = 0;

protected:
    void TypedExecuteImpl(TypedExecuteContext&) override { ++ticks; }
};

class BenchPlainType final : public TypedNodeType<BenchEmptyPorts> {
public:
    BenchPlainType() : TypedNodeType("BenchPlain") {}
    std::unique_ptr<NodeInstance> CreateInstance(const std::string& name) const override {
        return std::make_unique<BenchPlain>(name, const_cast<BenchPlainType*>(this));
    }
};

class BenchVariadic final : public VariadicTypedNode<BenchEmptyPorts> {
public:
    BenchVariadic(const std::string& name, NodeType* type) : VariadicTypedNode(name, type) {}
    uintptr_t sum = 0;

protected:
    void VariadicExecuteImpl(VariadicExecuteContext&) override {
        for (size_t i = 0; i < 8; ++i) {
            if (Resource* res = GetVariadicInputResource(i)) {
                sum += reinterpret_cast<uintptr_t>(res->GetHandle<VkBuffer>());
            }
        }
    }
};

class BenchVariadicType final : public NodeType {
public:
    BenchVariadicType() : NodeType("BenchVariadic") {}
    std::unique_ptr<NodeInstance> CreateInstance(const std::string& name) const override {
        return std::make_unique<BenchVariadic>(name, const_cast<BenchVariadicType*>(this));
    }
};

} // namespace

TEST(NodeExecutePathBench, NsPerFrame) {
    if (!std::getenv("VIXEN_BENCH_EXECUTE_PATH")) {
        GTEST_SKIP() << "set VIXEN_BENCH_EXECUTE_PATH=1 to run";
    }
    NodeTypeRegistry registry;
    registry.RegisterNodeType(std::make_unique<BenchSourceType>());
    registry.RegisterNodeType(std::make_unique<BenchPlainType>());
    registry.RegisterNodeType(std::make_unique<BenchVariadicType>());
    RenderGraph graph(&registry);

    std::vector<NodeHandle> order;
    for (int i = 0; i < 32; ++i) order.push_back(graph.AddNode("BenchPlain", "plain" + std::to_string(i)));
    std::vector<NodeHandle> sources;
    for (int i = 0; i < 8; ++i) sources.push_back(graph.AddNode("BenchSource", "src" + std::to_string(i)));
    const NodeHandle sink = graph.AddNode("BenchVariadic", "sink");
    ConnectionBatch batch(&graph);
    for (uint32_t i = 0; i < 8; ++i) batch.Connect(sources[i], BenchSourcePorts::Out, sink, i);
    batch.RegisterAll();
    ASSERT_NO_THROW(graph.Compile());
    order.insert(order.end(), sources.begin(), sources.end());
    order.push_back(sink);

    std::vector<NodeInstance*> nodes;
    for (NodeHandle h : order) nodes.push_back(graph.GetInstance(h));

    constexpr int kFrames = 200000;
    std::vector<double> rounds;
    for (int round = 0; round < 5; ++round) {
        const auto start = std::chrono::steady_clock::now();
        for (int frame = 0; frame < kFrames; ++frame) {
            for (NodeInstance* node : nodes) node->Execute();
        }
        const auto ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
        rounds.push_back(ns / kFrames);
        std::printf("[bench] round=%d nodes=%zu ns_per_frame=%.1f\n", round, nodes.size(), rounds.back());
    }
    std::sort(rounds.begin(), rounds.end());
    std::printf("[bench] median_ns_per_frame=%.1f\n", rounds[rounds.size() / 2]);
    EXPECT_GT(dynamic_cast<BenchVariadic*>(graph.GetInstance(sink))->sum, 0u);
}
