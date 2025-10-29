#include <gtest/gtest.h>

#include "Core/NodeInstance.h"
#include "Core/NodeType.h"
#include "Core/ResourceDependencyTracker.h"

using namespace Vixen::RenderGraph;

class TestNodeInstance : public NodeInstance {
public:
    using NodeInstance::NodeInstance;
    using NodeInstance::SetInput;
    using NodeInstance::SetOutput;
    void Execute(VkCommandBuffer) override {}
};

class DummyNodeType : public NodeType {
public:
    DummyNodeType() : NodeType("Dummy") {
        ResourceSlotDescriptor inDesc;
        inDesc.name = "in";
        inDesc.type = ResourceType::Buffer;
        inDesc.lifetime = ResourceLifetime::Transient;
        inDesc.descriptor = HandleDescriptor("handle");
        inputSchema.push_back(inDesc);

        ResourceSlotDescriptor outDesc;
        outDesc.name = "out";
        outDesc.type = ResourceType::Buffer;
        outDesc.lifetime = ResourceLifetime::Transient;
        outDesc.descriptor = HandleDescriptor("handle");
        outputSchema.push_back(outDesc);

        allowInputArrays = true;
    }

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override {
        return std::make_unique<TestNodeInstance>(instanceName, const_cast<DummyNodeType*>(this));
    }
};

TEST(RenderGraph_MultiProducer, OrderAndUniqueness) {
    DummyNodeType type;
    auto prod1 = type.CreateInstance("p1");
    auto prod2 = type.CreateInstance("p2");
    auto prod3 = type.CreateInstance("p3");
    auto consumer = type.CreateInstance("consumer");

    Resource* r1 = new Resource(Resource::Create<uint32_t>(HandleDescriptor("r1")));
    Resource* r2 = new Resource(Resource::Create<uint32_t>(HandleDescriptor("r2")));
    Resource* r3 = new Resource(Resource::Create<uint32_t>(HandleDescriptor("r3")));

    TestNodeInstance* p1 = static_cast<TestNodeInstance*>(prod1.get());
    TestNodeInstance* p2 = static_cast<TestNodeInstance*>(prod2.get());
    TestNodeInstance* p3 = static_cast<TestNodeInstance*>(prod3.get());
    TestNodeInstance* cons = static_cast<TestNodeInstance*>(consumer.get());

    p1->SetOutput(0, 0, r1);
    p2->SetOutput(0, 0, r2);
    p3->SetOutput(0, 0, r3);

    // Attach to consumer inputs in mixed order: r2, r1, r3
    cons->SetInput(0, 0, r2);
    cons->SetInput(0, 1, r1);
    cons->SetInput(0, 2, r3);

    ResourceDependencyTracker tracker;
    tracker.RegisterResourceProducer(r1, prod1.get(), 0);
    tracker.RegisterResourceProducer(r2, prod2.get(), 0);
    tracker.RegisterResourceProducer(r3, prod3.get(), 0);

    // Mark all three indices
    for (uint32_t i = 0; i < 3; ++i) {
        consumer->SetActiveBundleIndex(i);
        consumer->MarkInputUsedInCompile(0);
    }

    auto deps = tracker.GetDependenciesForNode(consumer.get());

    // Expect three unique producers
    EXPECT_EQ(deps.size(), 3u);
    // Ensure all producers appear
    std::vector<NodeInstance*> expected = { prod1.get(), prod2.get(), prod3.get() };
    for (auto* p : expected) {
        bool found = false;
        for (auto* d : deps) if (d == p) { found = true; break; }
        EXPECT_TRUE(found);
    }

    delete r1; delete r2; delete r3;
}
