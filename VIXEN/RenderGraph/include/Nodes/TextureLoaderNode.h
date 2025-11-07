#pragma once

#include "Core/NodeType.h"
#include "Core/TypedNodeInstance.h"
#include "Data/Nodes/TextureLoaderNodeConfig.h"

// Forward declarations
namespace CashSystem {
    class TextureCacher;
    struct TextureWrapper;
    struct SamplerWrapper;
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

    ~TextureLoaderNode() override = default;

protected:
    using TypedSetupContext = typename Base::TypedSetupContext;
    using TypedCompileContext = typename Base::TypedCompileContext;
    using TypedExecuteContext = typename Base::TypedExecuteContext;
    using TypedCleanupContext = typename Base::TypedCleanupContext;

    // Template method pattern - override *Impl() methods
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    // Loaded texture resources (output via typed slots)
    VkImage textureImage = VK_NULL_HANDLE;
    VkImageView textureView = VK_NULL_HANDLE;
    VkSampler textureSampler = VK_NULL_HANDLE;
    VkDeviceMemory textureMemory = VK_NULL_HANDLE;

    bool isLoaded = false;

    // CashSystem integration - cached wrappers
    std::shared_ptr<CashSystem::TextureWrapper> cachedTextureWrapper;
    std::shared_ptr<CashSystem::SamplerWrapper> cachedSamplerWrapper;
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
