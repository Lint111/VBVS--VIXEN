#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Core/PerFrameResources.h"
#include "Data/Nodes/TileBinViewNodeConfig.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include <memory>

namespace Vixen::RenderGraph {

class TileBinViewNodeType : public TypedNodeType<TileBinViewNodeConfig> {
public:
    explicit TileBinViewNodeType(const std::string& typeName = "TileBinView")
        : TypedNodeType<TileBinViewNodeConfig>(typeName) {}
    std::unique_ptr<NodeInstance> CreateInstance(const std::string& name) const override;
};

class TileBinViewNode : public TypedNode<TileBinViewNodeConfig> {
public:
    using Base = TypedNode<TileBinViewNodeConfig>;
    TileBinViewNode(const std::string& name, NodeType* type);
    ~TileBinViewNode() override = default;

protected:
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    static const uint32_t kRingSize;
    PerFrameResources viewRing_;
    VulkanDevice* device_ = nullptr;
};

} // namespace Vixen::RenderGraph
