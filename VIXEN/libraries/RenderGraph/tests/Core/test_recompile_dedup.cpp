/**
 * @file test_recompile_dedup.cpp
 * @brief Regression test for RenderGraph::RecompileDirtyNodes wave-cascade duplication
 *        (Widescreen-Perf-Sweep-Findings-2026-07.md rank 8).
 *
 * Diamond graph: A -> B, A -> C, B -> D, C -> D. Marking A dirty must recompile every
 * node exactly once (in topological order), not once per incoming-edge path to D.
 */

#include <gtest/gtest.h>

#include "Core/RenderGraph.h"
#include "Core/NodeInstance.h"
#include "Core/NodeType.h"
#include "Core/NodeTypeRegistry.h"
#include "Data/Core/CompileTimeResourceSystem.h"
#include "Data/VariantDescriptors.h"

using namespace Vixen::RenderGraph;

namespace {

// Counts CompileImpl() invocations so the test can assert "compiled exactly once per wave".
class CountingNodeInstance : public NodeInstance {
public:
    using NodeInstance::NodeInstance;

    int compileCount = 0;

protected:
    void CompileImpl() override {
        ++compileCount;
    }
    void ExecuteImpl(ExecuteContext& ctx) override {}
};

Schema MakeBufferSchema(size_t slotCount, const std::string& baseName) {
    BufferDescriptor bufDesc;
    bufDesc.size = 4;  // arbitrary -- not dereferenced by the graph machinery under test
    Schema schema;
    for (size_t i = 0; i < slotCount; ++i) {
        schema.push_back(ResourceDescriptor(
            baseName + std::to_string(i), ResourceType::Buffer, ResourceLifetime::Transient, bufDesc));
    }
    return schema;
}

// Root: no inputs, one output (feeds both B and C).
class RootNodeType : public NodeType {
public:
    RootNodeType() : NodeType("DiamondRoot") {
        SetOutputSchema(MakeBufferSchema(1, "out"));
    }
    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override {
        return std::make_unique<CountingNodeInstance>(instanceName, const_cast<RootNodeType*>(this));
    }
};

// Middle (B, C): one input (from A), one output (to D).
class MiddleNodeType : public NodeType {
public:
    MiddleNodeType() : NodeType("DiamondMiddle") {
        SetInputSchema(MakeBufferSchema(1, "in"));
        SetOutputSchema(MakeBufferSchema(1, "out"));
    }
    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override {
        return std::make_unique<CountingNodeInstance>(instanceName, const_cast<MiddleNodeType*>(this));
    }
};

// Sink (D): two inputs (from B and from C).
class SinkNodeType : public NodeType {
public:
    SinkNodeType() : NodeType("DiamondSink") {
        SetInputSchema(MakeBufferSchema(2, "in"));
    }
    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override {
        return std::make_unique<CountingNodeInstance>(instanceName, const_cast<SinkNodeType*>(this));
    }
};

} // namespace

TEST(RenderGraph_RecompileDedup, DiamondDependents_EachNodeCompilesExactlyOncePerWave) {
    NodeTypeRegistry registry;
    registry.Register<RootNodeType>();
    registry.Register<MiddleNodeType>();
    registry.Register<SinkNodeType>();

    // No message bus, no logger, no cacher -- RenderGraph::Compile() skips the persistent-cache
    // path entirely when mainCacher is null.
    RenderGraph graph(&registry, nullptr, nullptr, nullptr);

    NodeHandle a = graph.AddNode<RootNodeType>("A");
    NodeHandle b = graph.AddNode<MiddleNodeType>("B");
    NodeHandle c = graph.AddNode<MiddleNodeType>("C");
    NodeHandle d = graph.AddNode<SinkNodeType>("D");

    // A -> B, A -> C, B -> D (input 0), C -> D (input 1)
    graph.ConnectNodes(a, 0, b, 0);
    graph.ConnectNodes(a, 0, c, 0);
    graph.ConnectNodes(b, 0, d, 0);
    graph.ConnectNodes(c, 0, d, 1);

    graph.Compile();

    auto* nodeA = static_cast<CountingNodeInstance*>(graph.GetInstance(a));
    auto* nodeB = static_cast<CountingNodeInstance*>(graph.GetInstance(b));
    auto* nodeC = static_cast<CountingNodeInstance*>(graph.GetInstance(c));
    auto* nodeD = static_cast<CountingNodeInstance*>(graph.GetInstance(d));
    ASSERT_TRUE(nodeA && nodeB && nodeC && nodeD);

    EXPECT_EQ(nodeA->compileCount, 1);
    EXPECT_EQ(nodeB->compileCount, 1);
    EXPECT_EQ(nodeC->compileCount, 1);
    EXPECT_EQ(nodeD->compileCount, 1);  // initial compile is always exactly once

    // Now dirty just the root and let the wave cascade. D is reachable from A via two paths
    // (A->B->D and A->C->D); the bug recompiles D twice (once per path) instead of once.
    graph.MarkNodeNeedsRecompile(a);
    graph.RecompileDirtyNodes();

    EXPECT_EQ(nodeA->compileCount, 2) << "A should recompile exactly once for this wave";
    EXPECT_EQ(nodeB->compileCount, 2) << "B should recompile exactly once for this wave";
    EXPECT_EQ(nodeC->compileCount, 2) << "C should recompile exactly once for this wave";
    EXPECT_EQ(nodeD->compileCount, 2) << "D should recompile exactly once for this wave, "
                                          "not once per incoming path from the dirtied root";
}
