#include "Nodes/VertexBufferNode.h"
#include "Core/RenderGraph.h"
#include "VulkanResources/VulkanDevice.h"
#include "Core/NodeLogging.h"
#include "error/VulkanError.h"
#include <cstring>

namespace Vixen::RenderGraph {

// ====== VertexBufferNodeType ======

VertexBufferNodeType::VertexBufferNodeType(const std::string& typeName) : NodeType(typeName) {
    typeId = 103; // Unique ID
    pipelineType = PipelineType::Transfer;
    requiredCapabilities = DeviceCapability::Transfer;
    supportsInstancing = true;
    maxInstances = 0; // Unlimited

    // Initialize config and extract schema
    VertexBufferNodeConfig config;
    inputSchema = config.GetInputVector();
    outputSchema = config.GetOutputVector();

    // Workload metrics
    workloadMetrics.estimatedMemoryFootprint = 1024 * 1024; // ~1MB
    workloadMetrics.estimatedComputeCost = 0.3f;
    workloadMetrics.estimatedBandwidthCost = 1.5f; // Upload cost
    workloadMetrics.canRunInParallel = true;
}

std::unique_ptr<NodeInstance> VertexBufferNodeType::CreateInstance(const std::string& instanceName) const {
    return std::make_unique<VertexBufferNode>(instanceName, const_cast<VertexBufferNodeType*>(this));
}

// ====== VertexBufferNode ======

VertexBufferNode::VertexBufferNode(
    const std::string& instanceName,
    NodeType* nodeType
)
    : TypedNode<VertexBufferNodeConfig>(instanceName, nodeType)
{
}

VertexBufferNode::~VertexBufferNode() {
    Cleanup();
}

void VertexBufferNode::Setup() {
    // Read and validate device input
    vulkanDevice = In(VertexBufferNodeConfig::VULKAN_DEVICE_IN);
    if (vulkanDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("VertexBufferNode: Invalid device handle");
    }

    NODE_LOG_INFO("Setup: Vertex buffer node ready");
}

void VertexBufferNode::Compile() {
    NODE_LOG_INFO("Compile: Creating vertex and index buffers");

    // Get typed parameters
    vertexCount = GetParameterValue<uint32_t>(
        VertexBufferNodeConfig::PARAM_VERTEX_COUNT, 0);
    if (vertexCount == 0) {
        VulkanError error{VK_ERROR_INITIALIZATION_FAILED, "vertexCount parameter is required"};
        NODE_LOG_ERROR(error.toString());
        throw std::runtime_error(error.toString());
    }

    vertexStride = GetParameterValue<uint32_t>(
        VertexBufferNodeConfig::PARAM_VERTEX_STRIDE, sizeof(VertexWithUV));
    useTexture = GetParameterValue<bool>(
        VertexBufferNodeConfig::PARAM_USE_TEXTURE, true);

    NODE_LOG_DEBUG("Vertex count: " + std::to_string(vertexCount) +
                   ", stride: " + std::to_string(vertexStride) +
                   ", texture: " + std::string(useTexture ? "enabled" : "disabled"));

    VkDeviceSize vertexBufferSize = vertexCount * vertexStride;

    // Create and upload vertex buffer
    CreateBuffer(
        vertexBufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        vertexBuffer,
        vertexMemory
    );

    UploadData(vertexMemory, geometryData, vertexBufferSize);
    NODE_LOG_DEBUG("Uploaded " + std::to_string(vertexBufferSize) + " bytes of vertex data");

    // Check if we have index data
    indexCount = GetParameterValue<uint32_t>(
        VertexBufferNodeConfig::PARAM_INDEX_COUNT, 0);
    if (indexCount > 0) {
        hasIndices = true;
        VkDeviceSize indexBufferSize = indexCount * sizeof(uint32_t);

        CreateBuffer(
            indexBufferSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            indexBuffer,
            indexMemory
        );

        NODE_LOG_DEBUG("Created index buffer for " + std::to_string(indexCount) + " indices");
    } else {
        NODE_LOG_DEBUG("No index buffer (non-indexed rendering)");
    }

    // Setup vertex input description
    SetupVertexInputDescription();

    // Set outputs
    std::cout << "[VertexBufferNode::Compile] vertexBuffer BEFORE Out(): " << reinterpret_cast<uint64_t>(vertexBuffer) << std::endl;
    NODE_LOG_DEBUG("Setting VERTEX_BUFFER output: " + std::to_string(reinterpret_cast<uint64_t>(vertexBuffer)));
    Out(VertexBufferNodeConfig::VERTEX_BUFFER, vertexBuffer);
    std::cout << "[VertexBufferNode::Compile] vertexBuffer AFTER Out(): " << reinterpret_cast<uint64_t>(vertexBuffer) << std::endl;
    if (hasIndices) {
        NODE_LOG_DEBUG("Setting INDEX_BUFFER output: " + std::to_string(reinterpret_cast<uint64_t>(indexBuffer)));
        Out(VertexBufferNodeConfig::INDEX_BUFFER, indexBuffer);
    }
    Out(VertexBufferNodeConfig::VULKAN_DEVICE_OUT, vulkanDevice); // Fixed: was VULKAN_DEVICE_IN

    NODE_LOG_INFO("Compile complete: Vertex buffer ready");

    // === REGISTER CLEANUP ===
    NodeInstance::RegisterCleanup();
}

void VertexBufferNode::Execute(VkCommandBuffer commandBuffer) {
    // Vertex buffer creation happens in Compile phase
    // Execute is a no-op for this node
}

void VertexBufferNode::CleanupImpl() {
    // Validate device before cleanup
    if (vulkanDevice == VK_NULL_HANDLE) {
        NODE_LOG_WARNING("Cleanup: VulkanDevice is null - skipping buffer destruction");
        return;
    }
    
    if (vulkanDevice->device == VK_NULL_HANDLE) {
        NODE_LOG_WARNING("Cleanup: VkDevice is null - skipping buffer destruction");
        vulkanDevice = VK_NULL_HANDLE;
        return;
    }
    
    if ((vertexBuffer != VK_NULL_HANDLE || indexBuffer != VK_NULL_HANDLE)) {
        NODE_LOG_DEBUG("Cleanup: Destroying vertex and index buffers");

        if (vertexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(vulkanDevice->device, vertexBuffer, nullptr);
            vertexBuffer = VK_NULL_HANDLE;
        }

        if (vertexMemory != VK_NULL_HANDLE) {
            vkFreeMemory(vulkanDevice->device, vertexMemory, nullptr);
            vertexMemory = VK_NULL_HANDLE;
        }

        if (indexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(vulkanDevice->device, indexBuffer, nullptr);
            indexBuffer = VK_NULL_HANDLE;
        }

        if (indexMemory != VK_NULL_HANDLE) {
            vkFreeMemory(vulkanDevice->device, indexMemory, nullptr);
            indexMemory = VK_NULL_HANDLE;
        }
    }

    // Null out device pointer to prevent dangling reference
    vulkanDevice = VK_NULL_HANDLE;
}

void VertexBufferNode::CreateBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkBuffer& buffer,
    VkDeviceMemory& memory
) {
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

    VkResult result = vkCreateBuffer(vulkanDevice->device, &bufferInfo, nullptr, &buffer);
    if (result != VK_SUCCESS) {
        VulkanError error{result, "Failed to create buffer"};
        NODE_LOG_ERROR(error.toString());
        throw std::runtime_error(error.toString());
    }

    // Get memory requirements
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(vulkanDevice->device, buffer, &memRequirements);
    
    // Allocate memory
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = nullptr;
    allocInfo.allocationSize = memRequirements.size;

    // Find suitable memory type
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(*vulkanDevice->gpu, &memProperties);

    VkMemoryPropertyFlags requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    auto memoryTypeIndex = vulkanDevice->MemoryTypeFromProperties(
        memRequirements.memoryTypeBits,
        requiredFlags
	);

    if (!memoryTypeIndex.has_value())
    {
        vkDestroyBuffer(vulkanDevice->device, buffer, nullptr);
        VulkanError error{VK_ERROR_INITIALIZATION_FAILED, "Failed to find suitable memory type for buffer"};
        NODE_LOG_ERROR(error.toString());
		throw std::runtime_error(error.toString());
    }

    allocInfo.memoryTypeIndex = memoryTypeIndex.value();

    result = vkAllocateMemory(vulkanDevice->device, &allocInfo, nullptr, &memory);
    if (result != VK_SUCCESS) {
        vkDestroyBuffer(vulkanDevice->device, buffer, nullptr);
        VulkanError error{result, "Failed to allocate buffer memory"};
        NODE_LOG_ERROR(error.toString());
        throw std::runtime_error(error.toString());
    }

    // Bind buffer to memory
    result = vkBindBufferMemory(vulkanDevice->device, buffer, memory, 0);
    if (result != VK_SUCCESS) {
        vkFreeMemory(vulkanDevice->device, memory, nullptr);
        vkDestroyBuffer(vulkanDevice->device, buffer, nullptr);
        VulkanError error{result, "Failed to bind buffer memory"};
        NODE_LOG_ERROR(error.toString());
        throw std::runtime_error(error.toString());
    }
}

void VertexBufferNode::UploadData(
    VkDeviceMemory memory,
    const void* data,
    VkDeviceSize size
) {
    // Map memory
    void* mappedData;
    VkResult result = vkMapMemory(vulkanDevice->device, memory, 0, size, 0, &mappedData);
    if (result != VK_SUCCESS) {
        VulkanError error{result, "Failed to map buffer memory"};
        NODE_LOG_ERROR(error.toString());
        throw std::runtime_error(error.toString());
    }

    // Copy data
    std::memcpy(mappedData, data, static_cast<size_t>(size));

    // Unmap memory
    vkUnmapMemory(vulkanDevice->device, memory);
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
