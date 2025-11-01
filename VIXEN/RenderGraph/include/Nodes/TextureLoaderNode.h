#pragma once

#include "Core/NodeType.h"
#include "Core/TypedNodeInstance.h"
#include "Nodes/TextureLoaderNodeConfig.h"
#include "TextureHandling/Loading/TextureLoader.h"

// Forward declarations
namespace CashSystem {
    class TextureCacher;
}

namespace Vixen::RenderGraph {

/**
 * @brief Texture loading node
 * 
 * Now uses TypedNode<TextureLoaderNodeConfig> for compile-time type safety.
 * All inputs/outputs are accessed via the typed config slot API.
 * 
 * See TextureLoaderNodeConfig.h for slot definitions and parameters.
 */
class TextureLoaderNode : public TypedNode<TextureLoaderNodeConfig> {
public:
    TextureLoaderNode(
        const std::string& instanceName,
        NodeType* nodeType
    );

    virtual ~TextureLoaderNode();

protected:
    // Template method pattern - override *Impl() methods
    void SetupImpl() override;
    void CompileImpl() override;
    void ExecuteImpl() override;
    void CleanupImpl() override;

private:
    std::unique_ptr<Vixen::TextureHandling::TextureLoader> textureLoader;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VulkanDevicePtr vulkanDevice = VK_NULL_HANDLE;
    
    // Loaded texture resources (output via typed slots)
    VkImage textureImage = VK_NULL_HANDLE;
    VkImageView textureView = VK_NULL_HANDLE;
    VkSampler textureSampler = VK_NULL_HANDLE;
    VkDeviceMemory textureMemory = VK_NULL_HANDLE;

    bool isLoaded = false;

    // CashSystem integration - cached during Compile()
    CashSystem::TextureCacher* textureCacher = nullptr;
};

/**
 * @brief Type definition for TextureLoaderNode
 */
class TextureLoaderNodeType : public NodeType {
public:
    TextureLoaderNodeType( const std::string& typeName = "TextureLoader");

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

} // namespace Vixen::RenderGraph
