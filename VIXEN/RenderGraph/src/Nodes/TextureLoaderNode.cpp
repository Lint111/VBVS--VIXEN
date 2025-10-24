#include "Nodes/TextureLoaderNode.h"
#include "TextureHandling/Loading/STBTextureLoader.h"
#include "VulkanResources/VulkanDevice.h"

namespace Vixen::RenderGraph {

// ====== TextureLoaderNodeType ======

TextureLoaderNodeType::TextureLoaderNodeType(const std::string& typeName) : NodeType(typeName) {
    pipelineType = PipelineType::Transfer;
    requiredCapabilities = DeviceCapability::Transfer;
    supportsInstancing = true;
    maxInstances = 0; // Unlimited

    // No inputs - texture is loaded from file

    // Output: Texture image
    ImageDescription textureOutput{};
    textureOutput.width = 1024; // Default, will be overridden by actual texture
    textureOutput.height = 1024;
    textureOutput.depth = 1;
    textureOutput.mipLevels = 1; // Will be set based on loaded texture
    textureOutput.arrayLayers = 1;
    textureOutput.format = VK_FORMAT_R8G8B8A8_UNORM; // Common format
    textureOutput.samples = VK_SAMPLE_COUNT_1_BIT;
    textureOutput.usage = ResourceUsage::TransferDst | ResourceUsage::Sampled;
    textureOutput.tiling = VK_IMAGE_TILING_OPTIMAL;

    outputSchema.push_back(ResourceDescriptor(
        "textureImage",
        ResourceType::Image,
        ResourceLifetime::Persistent, // Textures persist across frames
        textureOutput
    ));

    // Workload metrics
    workloadMetrics.estimatedMemoryFootprint = 1024 * 1024 * 4; // ~4MB for 1K texture
    workloadMetrics.estimatedComputeCost = 0.5f; // Loading is mostly I/O
    workloadMetrics.estimatedBandwidthCost = 2.0f; // High bandwidth for upload
    workloadMetrics.canRunInParallel = true; // Can load multiple textures in parallel
}

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

TextureLoaderNode::~TextureLoaderNode() {
    Cleanup();
}

void TextureLoaderNode::Setup() {
    // Create temporary command pool for texture loading
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = device->graphicsQueueIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkResult result = vkCreateCommandPool(device->device, &poolInfo, nullptr, &commandPool);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool for texture loading");
    }

    // Create texture loader (using STB for common formats)
    textureLoader = std::make_unique<Vixen::TextureHandling::STBTextureLoader>(
        device,
        commandPool
    );
}

void TextureLoaderNode::Compile() {
    // Get parameters using config constants
    std::string filePath = GetParameterValue<std::string>(TextureLoaderNodeConfig::FILE_PATH, "");
    if (filePath.empty()) {
        throw std::runtime_error("TextureLoaderNode: filePath parameter is required");
    }

    std::string uploadModeStr = GetParameterValue<std::string>(TextureLoaderNodeConfig::UPLOAD_MODE, "Optimal");
    bool generateMipmaps = GetParameterValue<bool>(TextureLoaderNodeConfig::GENERATE_MIPMAPS, false);

    // Configure load settings
    Vixen::TextureHandling::TextureLoadConfig config;
    if (uploadModeStr == "Linear") {
        config.uploadMode = Vixen::TextureHandling::TextureLoadConfig::UploadMode::Linear;
    } else {
        config.uploadMode = Vixen::TextureHandling::TextureLoadConfig::UploadMode::Optimal;
    }

    // Load the texture
    try {
        Vixen::TextureHandling::TextureData textureData = textureLoader->Load(filePath.c_str(), config);
        
        // Store resources for output
        textureImage = textureData.image;
        textureView = textureData.view;
        textureSampler = textureData.sampler;
        textureMemory = textureData.mem;
        isLoaded = true;

        // Set typed outputs
        Out(TextureLoaderNodeConfig::TEXTURE_IMAGE, textureImage);
        Out(TextureLoaderNodeConfig::TEXTURE_VIEW, textureView);
        Out(TextureLoaderNodeConfig::TEXTURE_SAMPLER, textureSampler);

    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to load texture: " + std::string(e.what()));
    }
}

void TextureLoaderNode::Execute(VkCommandBuffer commandBuffer) {
    // Texture loading happens in Compile phase
    // Execute phase is a no-op for this node since the texture is already loaded
    
    // The texture is in SHADER_READ_ONLY_OPTIMAL layout after loading
    // If we need to transition to a different layout, we would do it here
}

void TextureLoaderNode::Cleanup() {
    // Destroy texture resources
    if (isLoaded) {
        VkDevice vkDevice = device->device;
        
        if (textureView != VK_NULL_HANDLE) {
            vkDestroyImageView(vkDevice, textureView, nullptr);
            textureView = VK_NULL_HANDLE;
        }
        
        if (textureSampler != VK_NULL_HANDLE) {
            vkDestroySampler(vkDevice, textureSampler, nullptr);
            textureSampler = VK_NULL_HANDLE;
        }
        
        if (textureImage != VK_NULL_HANDLE) {
            vkDestroyImage(vkDevice, textureImage, nullptr);
            textureImage = VK_NULL_HANDLE;
        }
        
        if (textureMemory != VK_NULL_HANDLE) {
            vkFreeMemory(vkDevice, textureMemory, nullptr);
            textureMemory = VK_NULL_HANDLE;
        }
        
        isLoaded = false;
    }

    // Destroy command pool
    if (commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device->device, commandPool, nullptr);
        commandPool = VK_NULL_HANDLE;
    }

    textureLoader.reset();
}

} // namespace Vixen::RenderGraph
