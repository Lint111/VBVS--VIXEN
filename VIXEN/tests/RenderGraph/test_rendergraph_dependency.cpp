#include <gtest/gtest.h>

#include "Core/NodeInstance.h"
#include "Core/NodeType.h"
#include "Core/ResourceDependencyTracker.h"
#include "Data/VariantDescriptors.h"

using namespace Vixen::RenderGraph;

// Minimal NodeType implementation for test
// Small test helper: expose protected setters via a derived test instance
class TestNodeInstance : public NodeInstance {
public:
    using NodeInstance::NodeInstance;
    // make protected setters public for tests
    using NodeInstance::SetInput;
    using NodeInstance::SetOutput;
    using NodeInstance::GetInputs;
    using NodeInstance::GetOutputs;
    // Provide minimal Execute implementation to satisfy abstract base
    void Execute(VkCommandBuffer) override {}
};

class DummyNodeType : public NodeType {
public:
    DummyNodeType() : NodeType("Dummy") {
        // Create one input and one output slot
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

        allowInputArrays = false;
    }

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override {
        return std::make_unique<TestNodeInstance>(instanceName, const_cast<DummyNodeType*>(this));
    }
};

TEST(RenderGraph_Dependency, InputMarkedAsDependency_IsIncluded) {
    DummyNodeType type;

    auto producer = type.CreateInstance("producer");
    auto consumer = type.CreateInstance("consumer");

    // Create a resource and register producer
    Resource* res = new Resource(Resource::Create<uint32_t>(HandleDescriptor("h")));

    // Cast to TestNodeInstance to access test-only public setters
    TestNodeInstance* prodPtr = static_cast<TestNodeInstance*>(producer.get());
    TestNodeInstance* consPtr = static_cast<TestNodeInstance*>(consumer.get());
    prodPtr->SetOutput(0, 0, res);

    ResourceDependencyTracker tracker;
    tracker.RegisterResourceProducer(res, producer.get(), 0);

    // Connect resource to consumer input (via TestNodeInstance)
    consPtr->SetInput(0, 0, res);

    // Ensure no inputs are marked used in compile
    consumer->ResetInputsUsedInCompile();
    auto deps = tracker.GetDependenciesForNode(consumer.get());
    // Since the input wasn't marked used in compile, dependency list should be empty
    EXPECT_EQ(deps.size(), 0u);

    // Now mark input as used during compile and verify producer appears
    consumer->MarkInputUsedInCompile(0);
    auto deps2 = tracker.GetDependenciesForNode(consumer.get());
    ASSERT_EQ(deps2.size(), 1u);
    EXPECT_EQ(deps2[0], producer.get());

    delete res;
}

TEST(RenderGraph_Dependency, CleanupIncludesProducerRegardlessOfMark) {
    DummyNodeType type;
    auto producer = type.CreateInstance("producer");
    auto consumer = type.CreateInstance("consumer");

    Resource* res = new Resource(Resource::Create<uint32_t>(HandleDescriptor("h")));
    TestNodeInstance* prodPtr = static_cast<TestNodeInstance*>(producer.get());
    TestNodeInstance* consPtr = static_cast<TestNodeInstance*>(consumer.get());
    prodPtr->SetOutput(0, 0, res);

    // Give producer a concrete handle so cleanup returns an identifiable value
    Vixen::RenderGraph::NodeHandle handle; handle.index = 42;
    producer->SetHandle(handle);

    ResourceDependencyTracker tracker;
    tracker.RegisterResourceProducer(res, producer.get(), 0);

    consPtr->SetInput(0, 0, res);

    // Do NOT mark input used in compile; BuildCleanupDependencies should still include producer handle
    auto cleanupDeps = tracker.BuildCleanupDependencies(consumer.get());
    ASSERT_EQ(cleanupDeps.size(), 1u);
    EXPECT_EQ(cleanupDeps[0], handle);

    delete res;
}
