#include "RenderGraph/Nodes/DescriptorSetNode.h"
#include "VulkanResources/VulkanDevice.h"
#include <cstring>

namespace Vixen::RenderGraph {

// ====== DescriptorSetNodeType ======

DescriptorSetNodeType::DescriptorSetNodeType() {
    typeId = 107; // Unique ID
    typeName = "DescriptorSet";
    pipelineType = PipelineType::Graphics;
    requiredCapabilities = DeviceCapability::Graphics;
    supportsInstancing = true;
    maxInstances = 0; // Unlimited

    // Optional input: Texture image (if using textures)
    ImageDescription textureInput{};
    textureInput.width = 1024;
    textureInput.height = 1024;
    textureInput.depth = 1;
    textureInput.mipLevels = 1;
    textureInput.arrayLayers = 1;
    textureInput.format = VK_FORMAT_R8G8B8A8_UNORM;
    textureInput.samples = VK_SAMPLE_COUNT_1_BIT;
    textureInput.usage = ResourceUsage::Sampled;
    textureInput.tiling = VK_IMAGE_TILING_OPTIMAL;

    inputSchema.push_back(ResourceDescriptor(
        "textureImage",
        ResourceType::Image,
        ResourceLifetime::Persistent,
        textureInput
    ));

    // Outputs are opaque (accessed via Get methods)
    BufferDescription uniformBufferOutput{};
    uniformBufferOutput.size = 256; // Default MVP matrix size
    uniformBufferOutput.usage = ResourceUsage::UniformBuffer;
    uniformBufferOutput.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    outputSchema.push_back(ResourceDescriptor(
        "uniformBuffer",
        ResourceType::Buffer,
        ResourceLifetime::Persistent,
        uniformBufferOutput
    ));

    // Workload metrics
    workloadMetrics.estimatedMemoryFootprint = 4096; // Small metadata
    workloadMetrics.estimatedComputeCost = 0.1f;
    workloadMetrics.estimatedBandwidthCost = 0.1f;
    workloadMetrics.canRunInParallel = true;
}

std::unique_ptr<NodeInstance> DescriptorSetNodeType::CreateInstance(
    const std::string& instanceName,
    Vixen::Vulkan::Resources::VulkanDevice* device
) const {
    return std::make_unique<DescriptorSetNode>(
        instanceName,
        const_cast<DescriptorSetNodeType*>(this),
        device
    );
}

// ====== DescriptorSetNode ======

DescriptorSetNode::DescriptorSetNode(
    const std::string& instanceName,
    NodeType* nodeType,
    Vixen::Vulkan::Resources::VulkanDevice* device
)
    : NodeInstance(instanceName, nodeType, device)
{
}

DescriptorSetNode::~DescriptorSetNode() {
    Cleanup();
}

void DescriptorSetNode::Setup() {
    // No setup needed
}

void DescriptorSetNode::Compile() {
    // Get parameters
    uniformBufferSize = GetParameterValue<uint32_t>("uniformBufferSize", 256);
    useTexture = GetParameterValue<bool>("useTexture", false);
    maxSets = GetParameterValue<uint32_t>("maxSets", 1);
    uniformBufferBinding = GetParameterValue<uint32_t>("uniformBufferBinding", 0);
    samplerBinding = GetParameterValue<uint32_t>("samplerBinding", 1);

    // Create descriptor set layout
    CreateDescriptorSetLayout();

    // Create descriptor pool
    CreateDescriptorPool();

    // Create uniform buffer
    CreateUniformBuffer();

    // Allocate descriptor sets
    AllocateDescriptorSets();

    // Update descriptor sets with buffer/texture info
    UpdateDescriptorSets();
}

void DescriptorSetNode::Execute(VkCommandBuffer commandBuffer) {
    // Descriptor setup happens in Compile phase
    // Execute is a no-op for this node
}

void DescriptorSetNode::Cleanup() {
    VkDevice vkDevice = device->device;

    // Free descriptor sets
    if (!descriptorSets.empty() && descriptorPool != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(
            vkDevice,
            descriptorPool,
            static_cast<uint32_t>(descriptorSets.size()),
            descriptorSets.data()
        );
        descriptorSets.clear();
    }

    // Destroy descriptor pool
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vkDevice, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }

    // Destroy descriptor set layout
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vkDevice, descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
    }

    // Destroy uniform buffer
    if (uniformBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vkDevice, uniformBuffer, nullptr);
        uniformBuffer = VK_NULL_HANDLE;
    }

    if (uniformMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vkDevice, uniformMemory, nullptr);
        uniformMemory = VK_NULL_HANDLE;
    }
}

void DescriptorSetNode::CreateDescriptorSetLayout() {
    // Define layout bindings
    std::vector<VkDescriptorSetLayoutBinding> bindings;

    // Uniform buffer binding (always present)
    VkDescriptorSetLayoutBinding uniformBinding{};
    uniformBinding.binding = uniformBufferBinding;
    uniformBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uniformBinding.descriptorCount = 1;
    uniformBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    uniformBinding.pImmutableSamplers = nullptr;
    bindings.push_back(uniformBinding);

    // Texture sampler binding (optional)
    if (useTexture) {
        VkDescriptorSetLayoutBinding samplerBinding{};
        samplerBinding.binding = this->samplerBinding;
        samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerBinding.descriptorCount = 1;
        samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        samplerBinding.pImmutableSamplers = nullptr;
        bindings.push_back(samplerBinding);
    }

    // Create descriptor set layout
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.pNext = nullptr;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    VkResult result = vkCreateDescriptorSetLayout(
        device->device,
        &layoutInfo,
        nullptr,
        &descriptorSetLayout
    );

    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor set layout");
    }
}

void DescriptorSetNode::CreateDescriptorPool() {
    // Define pool sizes
    std::vector<VkDescriptorPoolSize> poolSizes;

    // Uniform buffer pool
    VkDescriptorPoolSize uniformPoolSize{};
    uniformPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uniformPoolSize.descriptorCount = maxSets;
    poolSizes.push_back(uniformPoolSize);

    // Texture sampler pool (optional)
    if (useTexture) {
        VkDescriptorPoolSize samplerPoolSize{};
        samplerPoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerPoolSize.descriptorCount = maxSets;
        poolSizes.push_back(samplerPoolSize);
    }

    // Create descriptor pool
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.pNext = nullptr;
    poolInfo.maxSets = maxSets;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    VkResult result = vkCreateDescriptorPool(
        device->device,
        &poolInfo,
        nullptr,
        &descriptorPool
    );

    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool");
    }
}

void DescriptorSetNode::CreateUniformBuffer() {
    VkDevice vkDevice = device->device;

    // Create buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.pNext = nullptr;
    bufferInfo.size = uniformBufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferInfo.queueFamilyIndexCount = 0;
    bufferInfo.pQueueFamilyIndices = nullptr;
    bufferInfo.flags = 0;

    VkResult result = vkCreateBuffer(vkDevice, &bufferInfo, nullptr, &uniformBuffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create uniform buffer");
    }

    // Get memory requirements
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(vkDevice, uniformBuffer, &memRequirements);

    // Allocate memory
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = nullptr;
    allocInfo.allocationSize = memRequirements.size;

    auto memTypeResult = device->MemoryTypeFromProperties(
        memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    if (!memTypeResult.has_value()) {
        vkDestroyBuffer(vkDevice, uniformBuffer, nullptr);
        throw std::runtime_error("Failed to find suitable memory type for uniform buffer");
    }

    allocInfo.memoryTypeIndex = memTypeResult.value();

    result = vkAllocateMemory(vkDevice, &allocInfo, nullptr, &uniformMemory);
    if (result != VK_SUCCESS) {
        vkDestroyBuffer(vkDevice, uniformBuffer, nullptr);
        throw std::runtime_error("Failed to allocate uniform buffer memory");
    }

    // Bind buffer to memory
    result = vkBindBufferMemory(vkDevice, uniformBuffer, uniformMemory, 0);
    if (result != VK_SUCCESS) {
        vkFreeMemory(vkDevice, uniformMemory, nullptr);
        vkDestroyBuffer(vkDevice, uniformBuffer, nullptr);
        throw std::runtime_error("Failed to bind uniform buffer memory");
    }
}

void DescriptorSetNode::AllocateDescriptorSets() {
    // Prepare allocation info
    std::vector<VkDescriptorSetLayout> layouts(maxSets, descriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.pNext = nullptr;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = maxSets;
    allocInfo.pSetLayouts = layouts.data();

    // Allocate descriptor sets
    descriptorSets.resize(maxSets);
    VkResult result = vkAllocateDescriptorSets(
        device->device,
        &allocInfo,
        descriptorSets.data()
    );

    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor sets");
    }
}

void DescriptorSetNode::UpdateDescriptorSets() {
    // Update each descriptor set
    for (size_t i = 0; i < descriptorSets.size(); i++) {
        std::vector<VkWriteDescriptorSet> writes;

        // Uniform buffer write
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = uniformBufferSize;

        VkWriteDescriptorSet uniformWrite{};
        uniformWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        uniformWrite.pNext = nullptr;
        uniformWrite.dstSet = descriptorSets[i];
        uniformWrite.dstBinding = uniformBufferBinding;
        uniformWrite.dstArrayElement = 0;
        uniformWrite.descriptorCount = 1;
        uniformWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uniformWrite.pBufferInfo = &bufferInfo;
        writes.push_back(uniformWrite);

        // Texture sampler write (if enabled)
        if (useTexture && textureView != VK_NULL_HANDLE && textureSampler != VK_NULL_HANDLE) {
            VkWriteDescriptorSet samplerWrite{};
            samplerWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            samplerWrite.pNext = nullptr;
            samplerWrite.dstSet = descriptorSets[i];
            samplerWrite.dstBinding = samplerBinding;
            samplerWrite.dstArrayElement = 0;
            samplerWrite.descriptorCount = 1;
            samplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            samplerWrite.pImageInfo = &textureImageInfo;
            writes.push_back(samplerWrite);
        }

        // Update descriptor sets
        vkUpdateDescriptorSets(
            device->device,
            static_cast<uint32_t>(writes.size()),
            writes.data(),
            0,
            nullptr
        );
    }
}

void DescriptorSetNode::UpdateUniformBuffer(const void* data, VkDeviceSize size) {
    if (size > uniformBufferSize) {
        throw std::runtime_error("Data size exceeds uniform buffer size");
    }

    void* mappedData = nullptr;
    VkResult result = vkMapMemory(device->device, uniformMemory, 0, size, 0, &mappedData);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to map uniform buffer memory");
    }

    std::memcpy(mappedData, data, static_cast<size_t>(size));
    vkUnmapMemory(device->device, uniformMemory);
}

} // namespace Vixen::RenderGraph
