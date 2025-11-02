#include <gtest/gtest.h>

#include "Core/NodeInstance.h"
#include "Core/TypedNodeInstance.h"

using namespace Vixen::RenderGraph;

// Minimal config for TypedNode
struct TestConfig {
    static constexpr size_t INPUT_COUNT = 1;
    static constexpr size_t OUTPUT_COUNT = 1;
    struct INPUT_0_Slot { using Type = uint32_t; static constexpr size_t index = 0; };
    struct OUTPUT_0_Slot { using Type = uint32_t; static constexpr size_t index = 0; };
};

// Test typed node that exposes SetInput for test setup
class MyTypedNode : public TypedNode<TestConfig> {
public:
    MyTypedNode(const std::string& name, NodeType* type)
        : TypedNode<TestConfig>(name, type) {}

    using NodeInstance::SetInput; // expose for tests

protected:
    void ExecuteImpl(uint32_t taskIndex) override {}
};

// A tiny dummy NodeType so we can construct instances
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
    }

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override {
        return std::make_unique<MyTypedNode>(instanceName, const_cast<DummyNodeType*>(this));
    }
};

TEST(TypedNode_ExecuteOnly, ExecuteOnlyDoesNotMarkCompileUsage) {
    DummyNodeType t;
    auto nodePtr = t.CreateInstance("typed");
    MyTypedNode* node = static_cast<MyTypedNode*>(nodePtr.get());

    // Create a resource and attach it to input 0
    Resource* r = new Resource(Resource::Create<uint32_t>(HandleDescriptor("h")));
    node->SetInput(0, 0, r);

    // Reset markers
    node->ResetInputsUsedInCompile();

    // Call typed In with ExecuteOnly role - should NOT mark Dependency
    auto val = node->In(TestConfig::INPUT_0_Slot{}, NodeInstance::SlotRole::ExecuteOnly);
    EXPECT_FALSE(node->IsInputUsedInCompile(0, 0));

    // Now call with Dependency role - should mark
    auto val2 = node->In(TestConfig::INPUT_0_Slot{}, NodeInstance::SlotRole::Dependency);
    EXPECT_TRUE(node->IsInputUsedInCompile(0, 0));

    // silence unused variable warnings
    (void)val;
    (void)val2;

    delete r;
}
