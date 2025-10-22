#pragma once

#include "../NodeType.h"
#include "../NodeInstance.h"
#include "TextureHandling/Loading/TextureLoader.h"

namespace Vixen::RenderGraph {

/**
 * @brief Texture loading node
 * 
 * Responsibilities:
 * - Load texture from file (PNG, JPG, DDS, KTX, etc.)
 * - Create VkImage and VkImageView
 * - Upload data to GPU
 * - Transition to shader-readable layout
 * 
 * Inputs: None (file path is a parameter)
 * Outputs: 
 *   [0] Texture image (VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
 * 
 * Parameters:
 *   - filePath: std::string - Path to texture file
 *   - uploadMode: std::string - "Optimal" or "Linear"
 */
class TextureLoaderNode : public NodeInstance {
public:
    TextureLoaderNode(
        const std::string& instanceName,
        NodeType* nodeType,
        Vixen::Vulkan::Resources::VulkanDevice* device
    );

    virtual ~TextureLoaderNode();

    // NodeInstance interface
    void Setup() override;
    void Compile() override;
    void Execute(VkCommandBuffer commandBuffer) override;
    void Cleanup() override;

private:
    std::unique_ptr<Vixen::TextureHandling::TextureLoader> textureLoader;
    Vixen::TextureHandling::TextureData textureData;
    VkCommandPool commandPool = VK_NULL_HANDLE; // Temporary pool for loading
    bool isLoaded = false;
};

/**
 * @brief Type definition for TextureLoaderNode
 */
class TextureLoaderNodeType : public NodeType {
public:
    TextureLoaderNodeType();

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName,
        Vixen::Vulkan::Resources::VulkanDevice* device
    ) const override;
};

} // namespace Vixen::RenderGraph
