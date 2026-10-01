#include <gtest/gtest.h>

#include "Core/NodeInstance.h"
#include "Core/NodeType.h"
#include "Nodes/InputNode.h"
#include "NodeHelpers/ValidationHelpers.h"

using namespace Vixen::RenderGraph;

// Use centralized Vulkan global names to avoid duplicate strong symbols
#include <VulkanGlobalNames.h>

// Small test helper: expose protected setters via a derived test instance
class TestNodeInstance : public NodeInstance {
public:
    using NodeInstance::NodeInstance;
    // make protected setters/public accessors available for tests
    using NodeInstance::SetInput;
    using NodeInstance::SetOutput;
    // Phase F: GetInputs/GetOutputs removed, use GetBundles() instead
    // using NodeInstance::GetInputs;   // REMOVED: Use bundles[taskIndex].inputs
    // using NodeInstance::GetOutputs;  // REMOVED: Use bundles[taskIndex].outputs

protected:
    // Provide minimal Execute implementation to satisfy abstract base
    void ExecuteImpl(ExecuteContext& ctx) override {}
};

class DummyNodeType : public NodeType {
public:
    DummyNodeType() : NodeType("Dummy") {
        // Phase H: Schema now defined via TypedNode config classes
        // No manual ResourceSlotDescriptor needed
        allowInputArrays = true;
    }

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override {
        return std::make_unique<TestNodeInstance>(instanceName, const_cast<DummyNodeType*>(this));
    }
};

TEST(TypedNode_ActiveBundleIndex, MarkInputUsedRespectsActiveIndex) {
    DummyNodeType type;
    auto producer = type.CreateInstance("producer");
    auto consumer = type.CreateInstance("consumer");

    // Create two resources to simulate array slots
    Resource* r0 = new Resource(Resource::Create<uint32_t>(HandleDescriptor("h0")));
    Resource* r1 = new Resource(Resource::Create<uint32_t>(HandleDescriptor("h1")));

    // Use test-derived types to set inputs/outputs
    TestNodeInstance* prodPtr = static_cast<TestNodeInstance*>(producer.get());
    TestNodeInstance* consPtr = static_cast<TestNodeInstance*>(consumer.get());

    prodPtr->SetOutput(0, 0, r0);
    prodPtr->SetOutput(0, 1, r1);

    // Attach both to consumer inputs
    consPtr->SetInput(0, 0, r0);
    consPtr->SetInput(0, 1, r1);

    // Ensure reset
    consumer->ResetInputsUsedInCompile();

    // Mark array index 1 as used
    consumer->MarkInputUsedInCompile(0, 1);

    // Only index 1 should be marked
    EXPECT_FALSE(consumer->IsInputUsedInCompile(0, 0));
    EXPECT_TRUE(consumer->IsInputUsedInCompile(0, 1));

    // Now mark index 0 as well
    consumer->MarkInputUsedInCompile(0, 0);

    EXPECT_TRUE(consumer->IsInputUsedInCompile(0, 0));
    EXPECT_TRUE(consumer->IsInputUsedInCompile(0, 1));

    delete r0;
    delete r1;
}

namespace {

class InputValidationProbe : public InputNode {
public:
    using InputNode::InputNode;
    using NodeInstance::SetInput;
};

// Use the same pointer type and slot index to isolate the declared nullability.
using RequiredWindowSlot = ResourceSlot<GLFWwindow*, 0, SlotNullability::Required, SlotRole::Execute>;

} // namespace

TEST(TypedInputValidation, OptionalWindowMayBeUnconnected) {
    InputNodeType type;
    InputNode node("headless_input", &type);
    InputNode::TypedCompileContext ctx(&node, 0);

    EXPECT_EQ(::RenderGraph::NodeHelpers::ValidateInput<GLFWwindow*>(
                  ctx, "Window", InputNodeConfig::WINDOW), nullptr);
}

TEST(TypedInputValidation, RequiredNullInputKeepsItsDiagnostic) {
    InputNodeType type;
    InputNode node("required_input", &type);
    InputNode::TypedCompileContext ctx(&node, 0);

    try {
        ::RenderGraph::NodeHelpers::ValidateInput<GLFWwindow*>(ctx, "Window", RequiredWindowSlot{});
        FAIL() << "An absent required input must fail validation";
    } catch (const std::runtime_error& error) {
        EXPECT_STREQ(error.what(), "Required input 'Window' is null");
    }
}

TEST(TypedInputValidation, ConnectedPointerIsPreservedForEitherNullability) {
    InputNodeType type;
    Resource windowResource;
    InputValidationProbe node("connected_input", &type);
    // This opaque handle is only stored and compared; no GLFW operation runs.
    auto* window = reinterpret_cast<GLFWwindow*>(uintptr_t{1});
    windowResource.SetHandle(window);
    node.SetInput(0, 0, &windowResource);
    InputNode::TypedCompileContext ctx(&node, 0);

    EXPECT_EQ(::RenderGraph::NodeHelpers::ValidateInput<GLFWwindow*>(
                  ctx, "Window", InputNodeConfig::WINDOW), window);
    EXPECT_EQ(::RenderGraph::NodeHelpers::ValidateInput<GLFWwindow*>(
                  ctx, "Window", RequiredWindowSlot{}), window);
}

TEST(TypedInputValidation, ProductionInputNodeCompilesWithoutAWindow) {
    InputNodeType type;
    InputNode node("headless_input", &type);

    ASSERT_NO_THROW(node.Setup());
    EXPECT_NO_THROW(node.Compile());
}
