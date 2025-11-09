#include <gtest/gtest.h>

#include "Core/NodeInstance.h"
#include "Core/NodeType.h"
#include "Core/ResourceDependencyTracker.h"
#include <vector>
#include <vulkan/vulkan.h>

using namespace Vixen::RenderGraph;

// Define globals required by DeviceNode
std::vector<const char*> deviceExtensionNames;
std::vector<const char*> layerNames;

// Reuse TestNodeInstance and DummyNodeType pattern from other tests
class TestNodeInstance : public NodeInstance {
public:
    using NodeInstance::NodeInstance;
    using NodeInstance::SetInput;
    using NodeInstance::SetOutput;
protected:
    void ExecuteImpl(ExecuteContext& ctx) override {}
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

TEST(RenderGraph_ArraySlot, DedupProducerList) {
    DummyNodeType type;
    auto producerA = type.CreateInstance("producerA");
    auto producerB = type.CreateInstance("producerB");
    auto consumer = type.CreateInstance("consumer");

    Resource* a0 = new Resource(Resource::Create<uint32_t>(HandleDescriptor("a0")));
    Resource* a1 = new Resource(Resource::Create<uint32_t>(HandleDescriptor("a1")));
    Resource* b0 = new Resource(Resource::Create<uint32_t>(HandleDescriptor("b0")));

    TestNodeInstance* pa = static_cast<TestNodeInstance*>(producerA.get());
    TestNodeInstance* pb = static_cast<TestNodeInstance*>(producerB.get());
    TestNodeInstance* cons = static_cast<TestNodeInstance*>(consumer.get());

    pa->SetOutput(0, 0, a0);
    pa->SetOutput(0, 1, a1);
    pb->SetOutput(0, 0, b0);

    // Consumer inputs: [a0, a1, b0]
    cons->SetInput(0, 0, a0);
    cons->SetInput(0, 1, a1);
    cons->SetInput(0, 2, b0);

    ResourceDependencyTracker tracker;
    tracker.RegisterResourceProducer(a0, producerA.get(), 0);
    tracker.RegisterResourceProducer(a1, producerA.get(), 1);
    tracker.RegisterResourceProducer(b0, producerB.get(), 0);

    // Mark all array indices as used in compile
    cons->MarkInputUsedInCompile(0, 0);
    cons->MarkInputUsedInCompile(0, 1);
    cons->MarkInputUsedInCompile(0, 2);

    auto deps = tracker.GetDependenciesForNode(consumer.get());

    // Should contain each producer only once
    EXPECT_EQ(deps.size(), 2u);
    bool hasA = (deps[0] == producerA.get() || deps[1] == producerA.get());
    bool hasB = (deps[0] == producerB.get() || deps[1] == producerB.get());
    EXPECT_TRUE(hasA);
    EXPECT_TRUE(hasB);

    delete a0;
    delete a1;
    delete b0;
}
