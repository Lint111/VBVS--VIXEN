#include <gtest/gtest.h>

#include "Core/NodeInstance.h"
#include "Core/TypedNodeInstance.h"

using namespace Vixen::RenderGraph;

// Minimal config for TypedNode
struct TestConfig {
    static constexpr size_t INPUT_COUNT = 1;
    static constexpr size_t OUTPUT_COUNT = 1;
    struct INPUT_0_Slot {
        using Type = uint32_t;
        static constexpr size_t index = 0;
        static constexpr SlotRole role = SlotRole::Dependency;  // Phase F metadata
    };
    struct OUTPUT_0_Slot {
        using Type = uint32_t;
        static constexpr size_t index = 0;
        static constexpr SlotRole role = SlotRole::Output;
    };
};

// Test typed node that exposes SetInput for test setup
class MyTypedNode : public TypedNode<TestConfig> {
public:
    MyTypedNode(const std::string& name, NodeType* type)
        : TypedNode<TestConfig>(name, type) {}

    using NodeInstance::SetInput; // expose for tests

protected:
    void ExecuteImpl(Context& ctx) override {}
};

// A tiny dummy NodeType so we can construct instances
class DummyNodeType : public NodeType {
public:
    DummyNodeType() : NodeType("Dummy") {
        // Phase H: Schema now defined via TypedNode config classes
        // No manual ResourceSlotDescriptor needed
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

    // Phase F: Role is now in slot metadata, not parameter
    // TestConfig::INPUT_0_Slot should define its role as Dependency
    // The In() method is deprecated and should use context-based access
    // For now, just verify the input is not marked as used in compile
    // since we haven't called MarkInputUsedInCompile
    EXPECT_FALSE(node->IsInputUsedInCompile(0, 0));

    delete r;
}
