#pragma once

#include "Core/FrameSyncSchedule.h"
#include "Core/NodeType.h"
#include "Core/TypedNodeInstance.h"
#include "Data/Nodes/ProxyRasterStageNodeConfig.h"
#include "State/StatefulContainer.h"

namespace Vixen::RenderGraph {

class ProxyRasterStageNodeType : public TypedNodeType<ProxyRasterStageNodeConfig> {
public:
    explicit ProxyRasterStageNodeType(const std::string& typeName = "ProxyRasterStage")
        : TypedNodeType<ProxyRasterStageNodeConfig>(typeName) {}
    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

class ProxyRasterStageNode : public TypedNode<ProxyRasterStageNodeConfig> {
public:
    ProxyRasterStageNode(const std::string& instanceName, NodeType* nodeType);

protected:
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    void RecordCommands(TypedExecuteContext& ctx, VkCommandBuffer commandBuffer);

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    StatefulContainer<VkCommandBuffer> commandBuffers_;
    bool useFragmentWriter_ = false;
};

} // namespace Vixen::RenderGraph
