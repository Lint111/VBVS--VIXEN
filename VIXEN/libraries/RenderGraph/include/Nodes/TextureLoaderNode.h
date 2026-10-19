#pragma once

#include "Core/NodeType.h"
#include "Core/TypedNodeInstance.h"
#include "Data/Nodes/TextureLoaderNodeConfig.h"
#include "Textures/Residency.h"

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

    ~TextureLoaderNode() override;

protected:
    // Template method pattern - override *Impl() methods
    void TypedSetupImpl(TypedSetupContext& ctx) override;
    void TypedCompileImpl(TypedCompileContext& ctx) override;
    void TypedExecuteImpl(TypedExecuteContext& ctx) override;
    void TypedCleanupImpl(TypedCleanupContext& ctx) override;

private:
    // Loaded texture resources (output via typed slots)
    VkImage textureImage = VK_NULL_HANDLE;
    VkImageView textureView = VK_NULL_HANDLE;
    VkSampler textureSampler = VK_NULL_HANDLE;
    VkDeviceMemory textureMemory = VK_NULL_HANDLE;

    bool isLoaded = false;

    // Task profile for compile-time cost estimation (Sprint 6.5)
    ITaskProfile* compileProfile_ = nullptr;

    // Textures module ownership keeps the existing cache values alive while the
    // node binds their handles to graph outputs.
    std::shared_ptr<CashSystem::TextureWrapper> cachedTextureWrapper;
    std::shared_ptr<CashSystem::SamplerWrapper> cachedSamplerWrapper;
    std::unique_ptr<Vixen::Textures::TextureResidency> residency;

    // Helper methods
    void LoadTextureResources(const std::string& filePath);
};

/**
 * @brief Type definition for TextureLoaderNode
 */
class TextureLoaderNodeType : public TypedNodeType<TextureLoaderNodeConfig> {
public:
    TextureLoaderNodeType(const std::string& typeName = "TextureLoader")
        : TypedNodeType<TextureLoaderNodeConfig>(typeName) {}

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

} // namespace Vixen::RenderGraph
