#include "Nodes/TextureLoaderNode.h"
#include "Core/RenderGraph.h"
#include "Core/NodeLogging.h"
#include "VulkanResources/VulkanDevice.h"
#include "CashSystem/MainCacher.h"
#include "CashSystem/TextureCacher.h"
#include "CashSystem/SamplerCacher.h"

namespace Vixen::RenderGraph {

// ====== TextureLoaderNodeType ======

std::unique_ptr<NodeInstance> TextureLoaderNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<TextureLoaderNode>(
        instanceName,
        const_cast<TextureLoaderNodeType*>(this)
    );
}

// ====== TextureLoaderNode ======

TextureLoaderNode::TextureLoaderNode(
    const std::string& instanceName,
    NodeType* nodeType
)
    : TypedNode<TextureLoaderNodeConfig>(instanceName, nodeType)
{
}

void TextureLoaderNode::SetupImpl(TypedSetupContext& ctx) {
    // Graph-scope initialization only (no input access)
    NODE_LOG_DEBUG("TextureLoaderNode: Setup (graph-scope initialization)");
}

void TextureLoaderNode::CompileImpl(TypedCompileContext& ctx) {
    // Access device input (compile-time dependency)
    VulkanDevicePtr devicePtr = ctx.In(TextureLoaderNodeConfig::VULKAN_DEVICE_IN);
    if (devicePtr == nullptr) {
        throw std::runtime_error("TextureLoaderNode: Invalid device handle");
    }

    // Set base class device member for cleanup tracking
    SetDevice(devicePtr);

    // Get parameters using config constants
    std::string filePath = GetParameterValue<std::string>(TextureLoaderNodeConfig::FILE_PATH, "");
    if (filePath.empty()) {
        throw std::runtime_error("TextureLoaderNode: filePath parameter is required");
    }

    bool generateMipmaps = GetParameterValue<bool>(TextureLoaderNodeConfig::GENERATE_MIPMAPS, false);

    // Get MainCacher from owning graph
    auto& mainCacher = GetOwningGraph()->GetMainCacher();

    // Register TextureCacher (idempotent - safe to call multiple times)
    if (!mainCacher.IsRegistered(typeid(CashSystem::TextureWrapper))) {
        mainCacher.RegisterCacher<
            CashSystem::TextureCacher,
            CashSystem::TextureWrapper,
            CashSystem::TextureCreateParams
        >(
            typeid(CashSystem::TextureWrapper),
            "Texture",
            true  // device-dependent
        );
        NODE_LOG_DEBUG("TextureLoaderNode: Registered TextureCacher");
    }

    // Register SamplerCacher (idempotent - safe to call multiple times)
    if (!mainCacher.IsRegistered(typeid(CashSystem::SamplerWrapper))) {
        mainCacher.RegisterCacher<
            CashSystem::SamplerCacher,
            CashSystem::SamplerWrapper,
            CashSystem::SamplerCreateParams
        >(
            typeid(CashSystem::SamplerWrapper),
            "Sampler",
            true  // device-dependent
        );
        NODE_LOG_DEBUG("TextureLoaderNode: Registered SamplerCacher");
    }

    // Get SamplerCacher
    auto* samplerCacher = mainCacher.GetCacher<
        CashSystem::SamplerCacher,
        CashSystem::SamplerWrapper,
        CashSystem::SamplerCreateParams
    >(typeid(CashSystem::SamplerWrapper), device);

    if (!samplerCacher) {
        throw std::runtime_error("TextureLoaderNode: Failed to get SamplerCacher from MainCacher");
    }

    // Get TextureCacher
    auto* textureCacher = mainCacher.GetCacher<
        CashSystem::TextureCacher,
        CashSystem::TextureWrapper,
        CashSystem::TextureCreateParams
    >(typeid(CashSystem::TextureWrapper), device);

    if (!textureCacher) {
        throw std::runtime_error("TextureLoaderNode: Failed to get TextureCacher from MainCacher");
    }

    // Step 1: Get or create sampler
    CashSystem::SamplerCreateParams samplerParams{};
    samplerParams.minFilter = VK_FILTER_LINEAR;
    samplerParams.magFilter = VK_FILTER_LINEAR;
    samplerParams.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerParams.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerParams.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerParams.maxAnisotropy = 16.0f;
    samplerParams.compareEnable = VK_FALSE;
    samplerParams.compareOp = VK_COMPARE_OP_NEVER;

    cachedSamplerWrapper = samplerCacher->GetOrCreate(samplerParams);
    if (!cachedSamplerWrapper || cachedSamplerWrapper->resource == VK_NULL_HANDLE) {
        throw std::runtime_error("TextureLoaderNode: Failed to get or create sampler");
    }

    // Step 2: Get or create texture (passing sampler from step 1)
    CashSystem::TextureCreateParams textureParams{};
    textureParams.filePath = filePath;
    textureParams.format = VK_FORMAT_R8G8B8A8_UNORM;
    textureParams.generateMipmaps = generateMipmaps;
    textureParams.samplerWrapper = cachedSamplerWrapper;

    cachedTextureWrapper = textureCacher->GetOrCreate(textureParams);
    if (!cachedTextureWrapper || cachedTextureWrapper->image == VK_NULL_HANDLE) {
        throw std::runtime_error("TextureLoaderNode: Failed to get or create texture from cache");
    }

    // Extract resources from cached wrappers
    textureImage = cachedTextureWrapper->image;
    textureView = cachedTextureWrapper->view;
    textureSampler = cachedTextureWrapper->samplerWrapper->resource;
    textureMemory = cachedTextureWrapper->memory;
    isLoaded = true;

    // Set typed outputs
    ctx.Out(TextureLoaderNodeConfig::TEXTURE_IMAGE, textureImage);
    ctx.Out(TextureLoaderNodeConfig::TEXTURE_VIEW, textureView);
    ctx.Out(TextureLoaderNodeConfig::TEXTURE_SAMPLER, textureSampler);
    ctx.Out(TextureLoaderNodeConfig::VULKAN_DEVICE_OUT, device);
}

void TextureLoaderNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Texture loading happens in Compile phase
    // Execute phase is a no-op for this node since the texture is already loaded
    
    // The texture is in SHADER_READ_ONLY_OPTIMAL layout after loading
    // If we need to transition to a different layout, we would do it here
}

void TextureLoaderNode::CleanupImpl(TypedCleanupContext& ctx) {
    // Release cached wrappers - cachers own all Vulkan resources and manage lifecycle
    if (cachedTextureWrapper) {
        NODE_LOG_DEBUG("TextureLoaderNode: Releasing cached texture wrapper (cacher owns resources)");
        cachedTextureWrapper.reset();
        textureImage = VK_NULL_HANDLE;
        textureView = VK_NULL_HANDLE;
        textureMemory = VK_NULL_HANDLE;
    }

    if (cachedSamplerWrapper) {
        NODE_LOG_DEBUG("TextureLoaderNode: Releasing cached sampler wrapper (cacher owns resource)");
        cachedSamplerWrapper.reset();
        textureSampler = VK_NULL_HANDLE;
    }

    isLoaded = false;
}

} // namespace Vixen::RenderGraph
