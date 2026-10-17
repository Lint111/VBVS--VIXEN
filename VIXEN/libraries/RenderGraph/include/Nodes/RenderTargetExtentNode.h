#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "IRenderTarget.h"

namespace Vixen::RenderGraph {

CONSTEXPR_NODE_CONFIG(RenderTargetExtentNodeConfig, 1, 2, SlotArrayMode::Single) {
    INPUT_SLOT(TARGET, Vixen::Vulkan::Resources::IRenderTarget*, 0, SlotNullability::Required,
        SlotRole::Dependency | SlotRole::Execute, SlotMutability::ReadOnly, SlotScope::NodeLevel);
    OUTPUT_SLOT(WIDTH, uint32_t, 0, SlotNullability::Required, SlotMutability::WriteOnly);
    OUTPUT_SLOT(HEIGHT, uint32_t, 1, SlotNullability::Required, SlotMutability::WriteOnly);
    RenderTargetExtentNodeConfig() {
        INIT_INPUT_DESC(TARGET, "target", ResourceLifetime::Persistent, HandleDescriptor{"IRenderTarget*"});
        INIT_OUTPUT_DESC(WIDTH, "width", ResourceLifetime::Transient, HandleDescriptor{"uint32_t"});
        INIT_OUTPUT_DESC(HEIGHT, "height", ResourceLifetime::Transient, HandleDescriptor{"uint32_t"});
    }
};

// Obtain dimensions from the actual presentation target, including WSI surface
// constraints, without allocating a dummy image or assuming a window size.
class RenderTargetExtentNode final : public TypedNode<RenderTargetExtentNodeConfig> {
public:
    RenderTargetExtentNode(const std::string& name, NodeType* type) : TypedNode(name, type) {}
protected:
    void TypedCompileImpl(TypedCompileContext& ctx) override { Publish(ctx); }
    void TypedExecuteImpl(TypedExecuteContext& ctx) override { Publish(ctx); }
private:
    template<class Context> void Publish(Context& ctx) {
        const auto extent = ctx.In(RenderTargetExtentNodeConfig::TARGET)->GetExtent();
        ctx.Out(RenderTargetExtentNodeConfig::WIDTH, extent.width);
        ctx.Out(RenderTargetExtentNodeConfig::HEIGHT, extent.height);
    }
};

class RenderTargetExtentNodeType final : public TypedNodeType<RenderTargetExtentNodeConfig> {
public:
    RenderTargetExtentNodeType() : TypedNodeType("RenderTargetExtent") { graphExecutionKind = GraphExecutionKind::Cpu; }
    std::unique_ptr<NodeInstance> CreateInstance(const std::string& name) const override {
        return std::make_unique<RenderTargetExtentNode>(name, const_cast<RenderTargetExtentNodeType*>(this));
    }
};
}
