/**
 * @file test_graph_topology.cpp
 * @brief Tests for RenderGraph topology validation and dependency tracking
 *
 * Tests:
 * - Circular dependency detection
 * - Complex graph validation
 * - Topological sorting
 * - Dependency chain analysis
 * - Edge case handling
 *
 * Compatible with VULKAN_TRIMMED_BUILD (headers only).
 */

#include <gtest/gtest.h>
#include "../include/Core/RenderGraph.h"
#include "../include/Core/GraphTopology.h"
#include "../include/Core/NodeType.h"
#include <algorithm>

// Use the centralized Vulkan globals (inline/selectany) to avoid
// duplicate strong-symbol definitions across test translation units.
#include <VulkanGlobalNames.h>

using namespace Vixen::RenderGraph;

// ============================================================================
// Mock Node Type and Instance for Testing
// ============================================================================

class MockNodeType : public NodeType {
public:
    explicit MockNodeType(const std::string& typeName = "MockNode")
        : NodeType(typeName) {}

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override {
        return nullptr;
    }
};

class MockNode : public NodeInstance {
public:
    explicit MockNode(const std::string& name, NodeType* nodeType = nullptr)
        : NodeInstance(name, nodeType ? nodeType : &mockType) {}

private:
    static MockNodeType mockType;
};

MockNodeType MockNode::mockType("MockNode");

// ============================================================================
// Helper: Create GraphEdge
// ============================================================================

static GraphEdge MakeEdge(NodeInstance* source, NodeInstance* target, uint32_t srcIdx = 0, uint32_t tgtIdx = 0) {
    GraphEdge edge;
    edge.source = source;
    edge.sourceOutputIndex = srcIdx;
    edge.target = target;
    edge.targetInputIndex = tgtIdx;
    return edge;
}

// ============================================================================
// GraphTopology Tests
// ============================================================================

class GraphTopologyTest : public ::testing::Test {
protected:
    void SetUp() override {
        topology = std::make_unique<GraphTopology>();
    }

    std::unique_ptr<GraphTopology> topology;
};

TEST_F(GraphTopologyTest, AddNodes) {
    MockNode node1("Node1");
    MockNode node2("Node2");

    topology->AddNode(&node1);
    topology->AddNode(&node2);

    EXPECT_EQ(topology->GetNodeCount(), 2);
}

TEST_F(GraphTopologyTest, AddEdge) {
    MockNode node1("Node1");
    MockNode node2("Node2");

    topology->AddNode(&node1);
    topology->AddNode(&node2);

    topology->AddEdge(MakeEdge(&node1, &node2, 0, 0));

    auto outgoing = topology->GetOutgoingEdges(&node1);
    EXPECT_EQ(outgoing.size(), 1);
}

TEST_F(GraphTopologyTest, CircularDependencyDetection_Direct) {
    MockNode nodeA("A");
    MockNode nodeB("B");

    topology->AddNode(&nodeA);
    topology->AddNode(&nodeB);

    topology->AddEdge(MakeEdge(&nodeA, &nodeB, 0, 0));
    topology->AddEdge(MakeEdge(&nodeB, &nodeA, 0, 0));

    EXPECT_TRUE(topology->HasCycles());
}

TEST_F(GraphTopologyTest, CircularDependencyDetection_Indirect) {
    MockNode nodeA("A");
    MockNode nodeB("B");
    MockNode nodeC("C");

    topology->AddNode(&nodeA);
    topology->AddNode(&nodeB);
    topology->AddNode(&nodeC);

    topology->AddEdge(MakeEdge(&nodeA, &nodeB, 0, 0));
    topology->AddEdge(MakeEdge(&nodeB, &nodeC, 0, 0));
    topology->AddEdge(MakeEdge(&nodeC, &nodeA, 0, 0));

    EXPECT_TRUE(topology->HasCycles());
}

TEST_F(GraphTopologyTest, AcyclicGraph) {
    MockNode nodeA("A");
    MockNode nodeB("B");
    MockNode nodeC("C");

    topology->AddNode(&nodeA);
    topology->AddNode(&nodeB);
    topology->AddNode(&nodeC);

    topology->AddEdge(MakeEdge(&nodeA, &nodeB, 0, 0));
    topology->AddEdge(MakeEdge(&nodeB, &nodeC, 0, 0));

    EXPECT_FALSE(topology->HasCycles());
}

TEST_F(GraphTopologyTest, TopologicalSort_Linear) {
    MockNode nodeA("A");
    MockNode nodeB("B");
    MockNode nodeC("C");

    topology->AddNode(&nodeA);
    topology->AddNode(&nodeB);
    topology->AddNode(&nodeC);

    topology->AddEdge(MakeEdge(&nodeA, &nodeB, 0, 0));
    topology->AddEdge(MakeEdge(&nodeB, &nodeC, 0, 0));

    auto sorted = topology->TopologicalSort();
    ASSERT_EQ(sorted.size(), 3);

    EXPECT_EQ(sorted[0], &nodeA);
    EXPECT_EQ(sorted[1], &nodeB);
    EXPECT_EQ(sorted[2], &nodeC);
}

TEST_F(GraphTopologyTest, TopologicalSort_Diamond) {
    MockNode nodeA("A");
    MockNode nodeB("B");
    MockNode nodeC("C");
    MockNode nodeD("D");

    topology->AddNode(&nodeA);
    topology->AddNode(&nodeB);
    topology->AddNode(&nodeC);
    topology->AddNode(&nodeD);

    topology->AddEdge(MakeEdge(&nodeA, &nodeB, 0, 0));
    topology->AddEdge(MakeEdge(&nodeA, &nodeC, 0, 1));
    topology->AddEdge(MakeEdge(&nodeB, &nodeD, 0, 0));
    topology->AddEdge(MakeEdge(&nodeC, &nodeD, 0, 0));

    auto sorted = topology->TopologicalSort();
    ASSERT_EQ(sorted.size(), 4);

    EXPECT_EQ(sorted[0], &nodeA);
    EXPECT_EQ(sorted[3], &nodeD);

    bool validOrder = (sorted[1] == &nodeB && sorted[2] == &nodeC) ||
                      (sorted[1] == &nodeC && sorted[2] == &nodeB);
    EXPECT_TRUE(validOrder);
}

TEST_F(GraphTopologyTest, GetDirectDependencies) {
    MockNode nodeA("A");
    MockNode nodeB("B");
    MockNode nodeC("C");

    topology->AddNode(&nodeA);
    topology->AddNode(&nodeB);
    topology->AddNode(&nodeC);

    topology->AddEdge(MakeEdge(&nodeA, &nodeC, 0, 0));
    topology->AddEdge(MakeEdge(&nodeB, &nodeC, 0, 1));

    auto dependencies = topology->GetDirectDependencies(&nodeC);
    EXPECT_EQ(dependencies.size(), 2);
    EXPECT_TRUE(std::find(dependencies.begin(), dependencies.end(), &nodeA) != dependencies.end());
    EXPECT_TRUE(std::find(dependencies.begin(), dependencies.end(), &nodeB) != dependencies.end());
}

TEST_F(GraphTopologyTest, GetDirectDependents) {
    MockNode nodeA("A");
    MockNode nodeB("B");
    MockNode nodeC("C");

    topology->AddNode(&nodeA);
    topology->AddNode(&nodeB);
    topology->AddNode(&nodeC);

    topology->AddEdge(MakeEdge(&nodeA, &nodeB, 0, 0));
    topology->AddEdge(MakeEdge(&nodeA, &nodeC, 0, 1));

    auto dependents = topology->GetDirectDependents(&nodeA);
    EXPECT_EQ(dependents.size(), 2);
    EXPECT_TRUE(std::find(dependents.begin(), dependents.end(), &nodeB) != dependents.end());
    EXPECT_TRUE(std::find(dependents.begin(), dependents.end(), &nodeC) != dependents.end());
}

TEST_F(GraphTopologyTest, ComplexGraph_MultipleProducers) {
    MockNode producer1("Producer1");
    MockNode producer2("Producer2");
    MockNode producer3("Producer3");
    MockNode consumer("Consumer");

    topology->AddNode(&producer1);
    topology->AddNode(&producer2);
    topology->AddNode(&producer3);
    topology->AddNode(&consumer);

    topology->AddEdge(MakeEdge(&producer1, &consumer, 0, 0));
    topology->AddEdge(MakeEdge(&producer2, &consumer, 0, 1));
    topology->AddEdge(MakeEdge(&producer3, &consumer, 0, 2));

    EXPECT_FALSE(topology->HasCycles());

    auto deps = topology->GetDirectDependencies(&consumer);
    EXPECT_EQ(deps.size(), 3);
}

TEST_F(GraphTopologyTest, ComplexGraph_MultipleConsumers) {
    MockNode producer("Producer");
    MockNode consumer1("Consumer1");
    MockNode consumer2("Consumer2");
    MockNode consumer3("Consumer3");

    topology->AddNode(&producer);
    topology->AddNode(&consumer1);
    topology->AddNode(&consumer2);
    topology->AddNode(&consumer3);

    topology->AddEdge(MakeEdge(&producer, &consumer1, 0, 0));
    topology->AddEdge(MakeEdge(&producer, &consumer2, 0, 0));
    topology->AddEdge(MakeEdge(&producer, &consumer3, 0, 0));

    EXPECT_FALSE(topology->HasCycles());

    auto deps = topology->GetDirectDependents(&producer);
    EXPECT_EQ(deps.size(), 3);
}

TEST_F(GraphTopologyTest, SelfLoop_Detection) {
    MockNode node("SelfLoop");

    topology->AddNode(&node);
    topology->AddEdge(MakeEdge(&node, &node, 0, 0));

    EXPECT_TRUE(topology->HasCycles());
}

TEST_F(GraphTopologyTest, DisconnectedGraph) {
    MockNode nodeA("A");
    MockNode nodeB("B");
    MockNode nodeC("C");
    MockNode nodeD("D");

    topology->AddNode(&nodeA);
    topology->AddNode(&nodeB);
    topology->AddNode(&nodeC);
    topology->AddNode(&nodeD);

    topology->AddEdge(MakeEdge(&nodeA, &nodeB, 0, 0));
    topology->AddEdge(MakeEdge(&nodeC, &nodeD, 0, 0));

    EXPECT_FALSE(topology->HasCycles());
    EXPECT_EQ(topology->GetNodeCount(), 4);
}

TEST_F(GraphTopologyTest, RemoveNode) {
    MockNode nodeA("A");
    MockNode nodeB("B");

    topology->AddNode(&nodeA);
    topology->AddNode(&nodeB);
    topology->AddEdge(MakeEdge(&nodeA, &nodeB, 0, 0));

    topology->RemoveNode(&nodeA);

    EXPECT_EQ(topology->GetNodeCount(), 1);
    auto outgoing = topology->GetOutgoingEdges(&nodeA);
    EXPECT_EQ(outgoing.size(), 0);
}

TEST_F(GraphTopologyTest, RemoveEdge) {
    MockNode nodeA("A");
    MockNode nodeB("B");

    topology->AddNode(&nodeA);
    topology->AddNode(&nodeB);

    GraphEdge edge = MakeEdge(&nodeA, &nodeB, 0, 0);
    topology->AddEdge(edge);

    auto outgoing = topology->GetOutgoingEdges(&nodeA);
    EXPECT_EQ(outgoing.size(), 1);

    topology->RemoveEdge(edge);

    outgoing = topology->GetOutgoingEdges(&nodeA);
    EXPECT_EQ(outgoing.size(), 0);
}

// ============================================================================
// Integration Test: Complex Rendering Pipeline
// ============================================================================

TEST(GraphTopologyIntegration, RenderingPipelineTopology) {
    GraphTopology topology;

    MockNode device("Device");
    MockNode swapchain("Swapchain");
    MockNode renderPass("RenderPass");
    MockNode pipeline("Pipeline");
    MockNode commandBuffer("CommandBuffer");
    MockNode present("Present");

    topology.AddNode(&device);
    topology.AddNode(&swapchain);
    topology.AddNode(&renderPass);
    topology.AddNode(&pipeline);
    topology.AddNode(&commandBuffer);
    topology.AddNode(&present);

    topology.AddEdge(MakeEdge(&device, &swapchain, 0, 0));
    topology.AddEdge(MakeEdge(&swapchain, &renderPass, 0, 0));
    topology.AddEdge(MakeEdge(&renderPass, &pipeline, 0, 0));
    topology.AddEdge(MakeEdge(&pipeline, &commandBuffer, 0, 0));
    topology.AddEdge(MakeEdge(&commandBuffer, &present, 0, 0));

    EXPECT_FALSE(topology.HasCycles());

    auto sorted = topology.TopologicalSort();
    ASSERT_EQ(sorted.size(), 6);

    EXPECT_EQ(sorted[0], &device);
    EXPECT_EQ(sorted[5], &present);
}

TEST(GraphTopologyIntegration, DetectInvalidPipeline) {
    GraphTopology topology;

    MockNode renderPass("RenderPass");
    MockNode pipeline("Pipeline");
    MockNode framebuffer("Framebuffer");

    topology.AddNode(&renderPass);
    topology.AddNode(&pipeline);
    topology.AddNode(&framebuffer);

    topology.AddEdge(MakeEdge(&renderPass, &pipeline, 0, 0));
    topology.AddEdge(MakeEdge(&pipeline, &framebuffer, 0, 0));
    topology.AddEdge(MakeEdge(&framebuffer, &renderPass, 0, 0));

    EXPECT_TRUE(topology.HasCycles());
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    std::cout << "═══════════════════════════════════════════════════════\n";
    std::cout << "  GRAPH TOPOLOGY TEST SUITE\n";
    std::cout << "  Trimmed Build Compatible (Headers Only)\n";
    std::cout << "═══════════════════════════════════════════════════════\n\n";

    int result = RUN_ALL_TESTS();

    if (result == 0) {
        std::cout << "\n═══════════════════════════════════════════════════════\n";
        std::cout << "  ✓ ALL GRAPH TOPOLOGY TESTS PASSED!\n";
        std::cout << "\n  Coverage:\n";
        std::cout << "  ✓ Circular dependency detection (direct & indirect)\n";
        std::cout << "  ✓ Topological sorting (linear & diamond)\n";
        std::cout << "  ✓ Dependency tracking (dependencies & dependents)\n";
        std::cout << "  ✓ Complex graphs (multiple producers/consumers)\n";
        std::cout << "  ✓ Edge cases (self-loops, disconnected graphs)\n";
        std::cout << "  ✓ Integration tests (rendering pipelines)\n";
        std::cout << "═══════════════════════════════════════════════════════\n";
    }

    return result;
}
