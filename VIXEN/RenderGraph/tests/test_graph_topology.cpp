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
#include "../include/Nodes/ConstantNode.h"
#include "../include/Data/Core/GraphMessages.h"

using namespace Vixen::RenderGraph;

// ============================================================================
// Mock Node for Testing
// ============================================================================

class MockNode : public NodeInstance {
public:
    explicit MockNode(const std::string& name) : NodeInstance(name) {}

    void SetupImpl(Context& ctx) override {}
    void CompileImpl(Context& ctx) override {}
    void ExecuteImpl(Context& ctx) override {}
};

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
    auto node1 = std::make_shared<MockNode>("Node1");
    auto node2 = std::make_shared<MockNode>("Node2");

    topology->AddNode(node1);
    topology->AddNode(node2);

    EXPECT_EQ(topology->GetNodeCount(), 2);
}

TEST_F(GraphTopologyTest, AddEdge) {
    auto node1 = std::make_shared<MockNode>("Node1");
    auto node2 = std::make_shared<MockNode>("Node2");

    topology->AddNode(node1);
    topology->AddNode(node2);

    GraphEdge edge{node1.get(), node2.get(), 0, 0};
    topology->AddEdge(edge);

    EXPECT_TRUE(topology->HasEdge(node1.get(), node2.get()));
}

TEST_F(GraphTopologyTest, CircularDependencyDetection_Direct) {
    // Create A -> B -> A (direct cycle)
    auto nodeA = std::make_shared<MockNode>("A");
    auto nodeB = std::make_shared<MockNode>("B");

    topology->AddNode(nodeA);
    topology->AddNode(nodeB);

    topology->AddEdge({nodeA.get(), nodeB.get(), 0, 0});
    topology->AddEdge({nodeB.get(), nodeA.get(), 0, 0});

    // Should detect circular dependency
    EXPECT_FALSE(topology->IsAcyclic());
}

TEST_F(GraphTopologyTest, CircularDependencyDetection_Indirect) {
    // Create A -> B -> C -> A (indirect cycle)
    auto nodeA = std::make_shared<MockNode>("A");
    auto nodeB = std::make_shared<MockNode>("B");
    auto nodeC = std::make_shared<MockNode>("C");

    topology->AddNode(nodeA);
    topology->AddNode(nodeB);
    topology->AddNode(nodeC);

    topology->AddEdge({nodeA.get(), nodeB.get(), 0, 0});
    topology->AddEdge({nodeB.get(), nodeC.get(), 0, 0});
    topology->AddEdge({nodeC.get(), nodeA.get(), 0, 0});

    EXPECT_FALSE(topology->IsAcyclic());
}

TEST_F(GraphTopologyTest, AcyclicGraph) {
    // Create A -> B -> C (no cycle)
    auto nodeA = std::make_shared<MockNode>("A");
    auto nodeB = std::make_shared<MockNode>("B");
    auto nodeC = std::make_shared<MockNode>("C");

    topology->AddNode(nodeA);
    topology->AddNode(nodeB);
    topology->AddNode(nodeC);

    topology->AddEdge({nodeA.get(), nodeB.get(), 0, 0});
    topology->AddEdge({nodeB.get(), nodeC.get(), 0, 0});

    EXPECT_TRUE(topology->IsAcyclic());
}

TEST_F(GraphTopologyTest, TopologicalSort_Linear) {
    // Create A -> B -> C
    auto nodeA = std::make_shared<MockNode>("A");
    auto nodeB = std::make_shared<MockNode>("B");
    auto nodeC = std::make_shared<MockNode>("C");

    topology->AddNode(nodeA);
    topology->AddNode(nodeB);
    topology->AddNode(nodeC);

    topology->AddEdge({nodeA.get(), nodeB.get(), 0, 0});
    topology->AddEdge({nodeB.get(), nodeC.get(), 0, 0});

    auto sorted = topology->TopologicalSort();
    ASSERT_EQ(sorted.size(), 3);

    // Should be in order: A, B, C
    EXPECT_EQ(sorted[0], nodeA.get());
    EXPECT_EQ(sorted[1], nodeB.get());
    EXPECT_EQ(sorted[2], nodeC.get());
}

TEST_F(GraphTopologyTest, TopologicalSort_Diamond) {
    // Create diamond: A -> B, A -> C, B -> D, C -> D
    auto nodeA = std::make_shared<MockNode>("A");
    auto nodeB = std::make_shared<MockNode>("B");
    auto nodeC = std::make_shared<MockNode>("C");
    auto nodeD = std::make_shared<MockNode>("D");

    topology->AddNode(nodeA);
    topology->AddNode(nodeB);
    topology->AddNode(nodeC);
    topology->AddNode(nodeD);

    topology->AddEdge({nodeA.get(), nodeB.get(), 0, 0});
    topology->AddEdge({nodeA.get(), nodeC.get(), 0, 1});
    topology->AddEdge({nodeB.get(), nodeD.get(), 0, 0});
    topology->AddEdge({nodeC.get(), nodeD.get(), 0, 0});

    auto sorted = topology->TopologicalSort();
    ASSERT_EQ(sorted.size(), 4);

    // A must come first, D must come last
    EXPECT_EQ(sorted[0], nodeA.get());
    EXPECT_EQ(sorted[3], nodeD.get());

    // B and C can be in either order (both depend on A, both feed into D)
    bool validOrder = (sorted[1] == nodeB.get() && sorted[2] == nodeC.get()) ||
                      (sorted[1] == nodeC.get() && sorted[2] == nodeB.get());
    EXPECT_TRUE(validOrder);
}

TEST_F(GraphTopologyTest, GetDependencies) {
    auto nodeA = std::make_shared<MockNode>("A");
    auto nodeB = std::make_shared<MockNode>("B");
    auto nodeC = std::make_shared<MockNode>("C");

    topology->AddNode(nodeA);
    topology->AddNode(nodeB);
    topology->AddNode(nodeC);

    topology->AddEdge({nodeA.get(), nodeC.get(), 0, 0});
    topology->AddEdge({nodeB.get(), nodeC.get(), 0, 1});

    auto dependencies = topology->GetDependencies(nodeC.get());
    EXPECT_EQ(dependencies.size(), 2);
    EXPECT_TRUE(std::find(dependencies.begin(), dependencies.end(), nodeA.get()) != dependencies.end());
    EXPECT_TRUE(std::find(dependencies.begin(), dependencies.end(), nodeB.get()) != dependencies.end());
}

TEST_F(GraphTopologyTest, GetDependents) {
    auto nodeA = std::make_shared<MockNode>("A");
    auto nodeB = std::make_shared<MockNode>("B");
    auto nodeC = std::make_shared<MockNode>("C");

    topology->AddNode(nodeA);
    topology->AddNode(nodeB);
    topology->AddNode(nodeC);

    topology->AddEdge({nodeA.get(), nodeB.get(), 0, 0});
    topology->AddEdge({nodeA.get(), nodeC.get(), 0, 1});

    auto dependents = topology->GetDependents(nodeA.get());
    EXPECT_EQ(dependents.size(), 2);
    EXPECT_TRUE(std::find(dependents.begin(), dependents.end(), nodeB.get()) != dependents.end());
    EXPECT_TRUE(std::find(dependents.begin(), dependents.end(), nodeC.get()) != dependents.end());
}

TEST_F(GraphTopologyTest, ComplexGraph_MultipleProducers) {
    // Test graph with multiple producers feeding into one node
    auto producer1 = std::make_shared<MockNode>("Producer1");
    auto producer2 = std::make_shared<MockNode>("Producer2");
    auto producer3 = std::make_shared<MockNode>("Producer3");
    auto consumer = std::make_shared<MockNode>("Consumer");

    topology->AddNode(producer1);
    topology->AddNode(producer2);
    topology->AddNode(producer3);
    topology->AddNode(consumer);

    topology->AddEdge({producer1.get(), consumer.get(), 0, 0});
    topology->AddEdge({producer2.get(), consumer.get(), 0, 1});
    topology->AddEdge({producer3.get(), consumer.get(), 0, 2});

    EXPECT_TRUE(topology->IsAcyclic());

    auto deps = topology->GetDependencies(consumer.get());
    EXPECT_EQ(deps.size(), 3);
}

TEST_F(GraphTopologyTest, ComplexGraph_MultipleConsumers) {
    // Test graph with one producer feeding multiple consumers
    auto producer = std::make_shared<MockNode>("Producer");
    auto consumer1 = std::make_shared<MockNode>("Consumer1");
    auto consumer2 = std::make_shared<MockNode>("Consumer2");
    auto consumer3 = std::make_shared<MockNode>("Consumer3");

    topology->AddNode(producer);
    topology->AddNode(consumer1);
    topology->AddNode(consumer2);
    topology->AddNode(consumer3);

    topology->AddEdge({producer.get(), consumer1.get(), 0, 0});
    topology->AddEdge({producer.get(), consumer2.get(), 0, 0});
    topology->AddEdge({producer.get(), consumer3.get(), 0, 0});

    EXPECT_TRUE(topology->IsAcyclic());

    auto deps = topology->GetDependents(producer.get());
    EXPECT_EQ(deps.size(), 3);
}

TEST_F(GraphTopologyTest, SelfLoop_Detection) {
    // Node connecting to itself should be detected as cyclic
    auto node = std::make_shared<MockNode>("SelfLoop");

    topology->AddNode(node);
    topology->AddEdge({node.get(), node.get(), 0, 0});

    EXPECT_FALSE(topology->IsAcyclic());
}

TEST_F(GraphTopologyTest, DisconnectedGraph) {
    // Graph with disconnected components
    auto nodeA = std::make_shared<MockNode>("A");
    auto nodeB = std::make_shared<MockNode>("B");
    auto nodeC = std::make_shared<MockNode>("C");
    auto nodeD = std::make_shared<MockNode>("D");

    topology->AddNode(nodeA);
    topology->AddNode(nodeB);
    topology->AddNode(nodeC);
    topology->AddNode(nodeD);

    // Connect A -> B and C -> D (two separate components)
    topology->AddEdge({nodeA.get(), nodeB.get(), 0, 0});
    topology->AddEdge({nodeC.get(), nodeD.get(), 0, 0});

    EXPECT_TRUE(topology->IsAcyclic());
    EXPECT_EQ(topology->GetNodeCount(), 4);
}

TEST_F(GraphTopologyTest, RemoveNode) {
    auto nodeA = std::make_shared<MockNode>("A");
    auto nodeB = std::make_shared<MockNode>("B");

    topology->AddNode(nodeA);
    topology->AddNode(nodeB);
    topology->AddEdge({nodeA.get(), nodeB.get(), 0, 0});

    topology->RemoveNode(nodeA.get());

    EXPECT_EQ(topology->GetNodeCount(), 1);
    EXPECT_FALSE(topology->HasEdge(nodeA.get(), nodeB.get()));
}

TEST_F(GraphTopologyTest, RemoveEdge) {
    auto nodeA = std::make_shared<MockNode>("A");
    auto nodeB = std::make_shared<MockNode>("B");

    topology->AddNode(nodeA);
    topology->AddNode(nodeB);

    GraphEdge edge{nodeA.get(), nodeB.get(), 0, 0};
    topology->AddEdge(edge);

    EXPECT_TRUE(topology->HasEdge(nodeA.get(), nodeB.get()));

    topology->RemoveEdge(edge);

    EXPECT_FALSE(topology->HasEdge(nodeA.get(), nodeB.get()));
}

// ============================================================================
// Integration Test: Complex Rendering Pipeline
// ============================================================================

TEST(GraphTopologyIntegration, RenderingPipelineTopology) {
    GraphTopology topology;

    // Simulate a rendering pipeline
    auto device = std::make_shared<MockNode>("Device");
    auto swapchain = std::make_shared<MockNode>("Swapchain");
    auto renderPass = std::make_shared<MockNode>("RenderPass");
    auto pipeline = std::make_shared<MockNode>("Pipeline");
    auto commandBuffer = std::make_shared<MockNode>("CommandBuffer");
    auto present = std::make_shared<MockNode>("Present");

    topology.AddNode(device);
    topology.AddNode(swapchain);
    topology.AddNode(renderPass);
    topology.AddNode(pipeline);
    topology.AddNode(commandBuffer);
    topology.AddNode(present);

    // Build dependency chain
    topology.AddEdge({device.get(), swapchain.get(), 0, 0});
    topology.AddEdge({swapchain.get(), renderPass.get(), 0, 0});
    topology.AddEdge({renderPass.get(), pipeline.get(), 0, 0});
    topology.AddEdge({pipeline.get(), commandBuffer.get(), 0, 0});
    topology.AddEdge({commandBuffer.get(), present.get(), 0, 0});

    // Verify acyclic
    EXPECT_TRUE(topology.IsAcyclic());

    // Verify topological order
    auto sorted = topology.TopologicalSort();
    ASSERT_EQ(sorted.size(), 6);

    // Device should be first, Present should be last
    EXPECT_EQ(sorted[0], device.get());
    EXPECT_EQ(sorted[5], present.get());
}

TEST(GraphTopologyIntegration, DetectInvalidPipeline) {
    GraphTopology topology;

    // Create invalid pipeline with feedback loop
    auto renderPass = std::make_shared<MockNode>("RenderPass");
    auto pipeline = std::make_shared<MockNode>("Pipeline");
    auto framebuffer = std::make_shared<MockNode>("Framebuffer");

    topology.AddNode(renderPass);
    topology.AddNode(pipeline);
    topology.AddNode(framebuffer);

    // Create cycle: RenderPass -> Pipeline -> Framebuffer -> RenderPass
    topology.AddEdge({renderPass.get(), pipeline.get(), 0, 0});
    topology.AddEdge({pipeline.get(), framebuffer.get(), 0, 0});
    topology.AddEdge({framebuffer.get(), renderPass.get(), 0, 0}); // Invalid!

    // Should detect cycle
    EXPECT_FALSE(topology.IsAcyclic());
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
        std::cout << "  ✅ ALL GRAPH TOPOLOGY TESTS PASSED!\n";
        std::cout << "\n  Coverage:\n";
        std::cout << "  ✅ Circular dependency detection (direct & indirect)\n";
        std::cout << "  ✅ Topological sorting (linear & diamond)\n";
        std::cout << "  ✅ Dependency tracking (dependencies & dependents)\n";
        std::cout << "  ✅ Complex graphs (multiple producers/consumers)\n";
        std::cout << "  ✅ Edge cases (self-loops, disconnected graphs)\n";
        std::cout << "  ✅ Integration tests (rendering pipelines)\n";
        std::cout << "═══════════════════════════════════════════════════════\n";
    }

    return result;
}
