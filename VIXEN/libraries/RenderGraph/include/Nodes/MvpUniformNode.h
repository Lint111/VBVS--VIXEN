#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Data/Nodes/MvpUniformNodeConfig.h"
#include <memory>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for the binding-0 MVP uniform buffer (UBO)
 * Type ID: 123
 */
class MvpUniformNodeType : public TypedNodeType<MvpUniformNodeConfig> {
public:
    MvpUniformNodeType(const std::string& typeName = "MvpUniform")
        : TypedNodeType<MvpUniformNodeConfig>(typeName) {}
    virtual ~MvpUniformNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * @brief Allocates a host-visible uniform buffer (UBO) holding mvp = proj * view for
 * the general Draw.vert shader's `layout(std140, binding=0) uniform bufferVals { mat4 mvp; }`.
 *
 * Outputs the VkBuffer. The model matrix is applied per-instance in the shader, and
 * Draw.vert performs the Vulkan Y-flip / Z-remap itself, so this node bakes only the
 * unmodified proj*view.
 *
 * FR-7 lifecycle: the buffer persists across graph recompile; released only on
 * FinalTeardown.
 */
class MvpUniformNode : public TypedNode<MvpUniformNodeConfig> {
public:
    using Base = TypedNode<MvpUniformNodeConfig>;

    MvpUniformNode(const std::string& instanceName, NodeType* nodeType);
    ~MvpUniformNode() override = default;

protected:
    void SetupImpl(TypedSetupContext&    ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    void CreateBuffer(Vixen::Vulkan::Resources::VulkanDevice* device);
    void DestroyBuffer();

    Vixen::Vulkan::Resources::VulkanDevice* device_         = nullptr;  // cached for cleanup
    VkBuffer                                buffer_         = VK_NULL_HANDLE;
    VkDeviceMemory                          memory_         = VK_NULL_HANDLE;
    float                                   fovDegrees_     = 50.0f;
    float                                   aspect_         = 1.7777778f;
    float                                   nearZ_          = 0.1f;
    float                                   farZ_           = 200.0f;
    float                                   cameraDistance_ = 45.0f;
};

} // namespace Vixen::RenderGraph
