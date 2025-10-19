#include "RenderGraph/Nodes/VertexBufferNode.h"
#include "VulkanResources/VulkanDevice.h"
#include <cstring>

namespace Vixen::RenderGraph {

// ====== VertexBufferNodeType ======

VertexBufferNodeType::VertexBufferNodeType() {
    typeId = 103; // Unique ID
    typeName = "VertexBuffer";
    pipelineType = PipelineType::Transfer;
    requiredCapabilities = DeviceCapability::Transfer;
    supportsInstancing = true;
    maxInstances = 0; // Unlimited

    // No inputs - vertex data provided via parameters

    // Outputs: Vertex buffer (opaque, accessed via GetVertexBuffer())
    BufferDescription vertexBufferOutput{};
    vertexBufferOutput.size = 1024 * 1024; // Default 1MB, will be updated
    vertexBufferOutput.usage = ResourceUsage::VertexBuffer;
    vertexBufferOutput.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | 
                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    outputSchema.push_back(ResourceDescriptor(
        "vertexBuffer",
        ResourceType::Buffer,
        ResourceLifetime::Persistent,
        vertexBufferOutput
    ));

    // Optional index buffer output
    BufferDescription indexBufferOutput{};
    indexBufferOutput.size = 256 * 1024; // Default 256KB
    indexBufferOutput.usage = ResourceUsage::IndexBuffer;
    indexBufferOutput.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    outputSchema.push_back(ResourceDescriptor(
        "indexBuffer",
        ResourceType::Buffer,
        ResourceLifetime::Persistent,
        indexBufferOutput
    ));

    // Workload metrics
    workloadMetrics.estimatedMemoryFootprint = 1024 * 1024; // ~1MB
    workloadMetrics.estimatedComputeCost = 0.3f;
    workloadMetrics.estimatedBandwidthCost = 1.5f; // Upload cost
    workloadMetrics.canRunInParallel = true;
}

std::unique_ptr<NodeInstance> VertexBufferNodeType::CreateInstance(
    const std::string& instanceName,
    Vixen::Vulkan::Resources::VulkanDevice* device
) const {
    return std::make_unique<VertexBufferNode>(
        instanceName,
        const_cast<VertexBufferNodeType*>(this),
        device
    );
}

// ====== VertexBufferNode ======

VertexBufferNode::VertexBufferNode(
    const std::string& instanceName,
    NodeType* nodeType,
    Vixen::Vulkan::Resources::VulkanDevice* device
)
    : NodeInstance(instanceName, nodeType, device)
{
}

VertexBufferNode::~VertexBufferNode() {
    Cleanup();
}

void VertexBufferNode::Setup() {
    // No setup needed
}

void VertexBufferNode::Compile() {
    // Get parameters
    vertexCount = GetParameterValue<uint32_t>("vertexCount", 0);
    if (vertexCount == 0) {
        throw std::runtime_error("VertexBufferNode: vertexCount parameter is required");
    }

    vertexStride = GetParameterValue<uint32_t>("vertexStride", sizeof(VertexWithUV));
    useTexture = GetParameterValue<bool>("useTexture", true);

    // For now, we'll use the built-in geometryData from MeshData.h
    // In the future, this could load from a file or accept custom data
    VkDeviceSize vertexBufferSize = vertexCount * vertexStride;

    // Create and upload vertex buffer
    CreateBuffer(
        vertexBufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        vertexBuffer,
        vertexMemory
    );

    // Upload the geometry data
    UploadData(vertexMemory, geometryData, vertexBufferSize);

    // Check if we have index data
    indexCount = GetParameterValue<uint32_t>("indexCount", 0);
    if (indexCount > 0) {
        hasIndices = true;
        VkDeviceSize indexBufferSize = indexCount * sizeof(uint32_t);

        CreateBuffer(
            indexBufferSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            indexBuffer,
            indexMemory
        );

        // Note: Index data upload would happen here if we had index data
        // For now, we're primarily supporting non-indexed rendering
    }

    // Setup vertex input description
    SetupVertexInputDescription();
}

void VertexBufferNode::Execute(VkCommandBuffer commandBuffer) {
    // Vertex buffer creation happens in Compile phase
    // Execute is a no-op for this node
}

void VertexBufferNode::Cleanup() {
    VkDevice vkDevice = device->device;

    if (vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vkDevice, vertexBuffer, nullptr);
        vertexBuffer = VK_NULL_HANDLE;
    }

    if (vertexMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vkDevice, vertexMemory, nullptr);
        vertexMemory = VK_NULL_HANDLE;
    }

    if (indexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vkDevice, indexBuffer, nullptr);
        indexBuffer = VK_NULL_HANDLE;
    }

    if (indexMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vkDevice, indexMemory, nullptr);
        indexMemory = VK_NULL_HANDLE;
    }
}

void VertexBufferNode::CreateBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkBuffer& buffer,
    VkDeviceMemory& memory
) {
    VkDevice vkDevice = device->device;

    // Create buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.pNext = nullptr;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferInfo.queueFamilyIndexCount = 0;
    bufferInfo.pQueueFamilyIndices = nullptr;
    bufferInfo.flags = 0;

    VkResult result = vkCreateBuffer(vkDevice, &bufferInfo, nullptr, &buffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer");
    }

    // Get memory requirements
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(vkDevice, buffer, &memRequirements);

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
        vkDestroyBuffer(vkDevice, buffer, nullptr);
        throw std::runtime_error("Failed to find suitable memory type for buffer");
    }

    allocInfo.memoryTypeIndex = memTypeResult.value();

    result = vkAllocateMemory(vkDevice, &allocInfo, nullptr, &memory);
    if (result != VK_SUCCESS) {
        vkDestroyBuffer(vkDevice, buffer, nullptr);
        throw std::runtime_error("Failed to allocate buffer memory");
    }

    // Bind buffer to memory
    result = vkBindBufferMemory(vkDevice, buffer, memory, 0);
    if (result != VK_SUCCESS) {
        vkFreeMemory(vkDevice, memory, nullptr);
        vkDestroyBuffer(vkDevice, buffer, nullptr);
        throw std::runtime_error("Failed to bind buffer memory");
    }
}

void VertexBufferNode::UploadData(
    VkDeviceMemory memory,
    const void* data,
    VkDeviceSize size
) {
    VkDevice vkDevice = device->device;

    // Map memory
    void* mappedData = nullptr;
    VkResult result = vkMapMemory(vkDevice, memory, 0, size, 0, &mappedData);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to map buffer memory");
    }

    // Copy data
    std::memcpy(mappedData, data, static_cast<size_t>(size));

    // Unmap memory
    vkUnmapMemory(vkDevice, memory);
}

void VertexBufferNode::SetupVertexInputDescription() {
    // Vertex binding description
    vertexBinding.binding = 0;
    vertexBinding.stride = vertexStride;
    vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    // Vertex attribute descriptions
    // Attribute 0: Position (vec4)
    vertexAttributes[0].binding = 0;
    vertexAttributes[0].location = 0;
    vertexAttributes[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    vertexAttributes[0].offset = 0;

    // Attribute 1: UV (vec2) or Color (vec4)
    vertexAttributes[1].binding = 0;
    vertexAttributes[1].location = 1;
    vertexAttributes[1].format = useTexture ? VK_FORMAT_R32G32_SFLOAT : VK_FORMAT_R32G32B32A32_SFLOAT;
    vertexAttributes[1].offset = 16; // After 4 floats (position)
}

} // namespace Vixen::RenderGraph
