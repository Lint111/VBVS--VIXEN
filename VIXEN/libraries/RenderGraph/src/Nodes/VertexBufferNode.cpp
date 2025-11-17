#include "Nodes/VertexBufferNode.h"
#include "Core/RenderGraph.h"
#include "VulkanDevice.h"
#include "Core/NodeLogging.h"
#include "error/VulkanError.h"
#include "MeshCacher.h"
#include "MainCacher.h"
#include <cstring>
#include <typeindex>

namespace Vixen::RenderGraph {

// ====== VertexBufferNodeType ======

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

void VertexBufferNode::SetupImpl(TypedSetupContext& ctx) {
    // Graph-scope initialization only (no input access)
    NODE_LOG_DEBUG("VertexBufferNode: Setup (graph-scope initialization)");
}

void VertexBufferNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("Compile: Creating vertex and index buffers via MeshCacher");

    // Access device input
    VulkanDevice* devicePtr = ctx.In(VertexBufferNodeConfig::VULKAN_DEVICE_IN);
    if (devicePtr == nullptr) {
        throw std::runtime_error("VertexBufferNode: Invalid device handle");
    }
    SetDevice(devicePtr);

    // Get parameters
    vertexCount = GetParameterValue<uint32_t>(VertexBufferNodeConfig::PARAM_VERTEX_COUNT, 0);
    if (vertexCount == 0) {
        throw std::runtime_error("VertexBufferNode: vertexCount parameter is required");
    }

    vertexStride = GetParameterValue<uint32_t>(VertexBufferNodeConfig::PARAM_VERTEX_STRIDE, sizeof(VertexWithUV));
    useTexture = GetParameterValue<bool>(VertexBufferNodeConfig::PARAM_USE_TEXTURE, true);
    indexCount = GetParameterValue<uint32_t>(VertexBufferNodeConfig::PARAM_INDEX_COUNT, 0);
    hasIndices = (indexCount > 0);

    NODE_LOG_DEBUG("VertexBufferNode: count=" + std::to_string(vertexCount) +
                   ", stride=" + std::to_string(vertexStride) +
                   ", indices=" + std::to_string(indexCount));

    RegisterMeshCacher();
    CreateMeshBuffers();
    SetupVertexInputDescription();

    // Set outputs
    ctx.Out(VertexBufferNodeConfig::VERTEX_BUFFER, vertexBuffer);
    if (hasIndices) {
        ctx.Out(VertexBufferNodeConfig::INDEX_BUFFER, indexBuffer);
    }
    ctx.Out(VertexBufferNodeConfig::VULKAN_DEVICE_OUT, device);

    NODE_LOG_INFO("Compile complete: Vertex buffer ready");
}

void VertexBufferNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Vertex buffer creation happens in Compile phase
    // Execute is a no-op for this node
}

void VertexBufferNode::CleanupImpl(TypedCleanupContext& ctx) {
    if (cachedMeshWrapper) {
        NODE_LOG_DEBUG("Cleanup: Releasing cached mesh wrapper");
        cachedMeshWrapper.reset();
        vertexBuffer = VK_NULL_HANDLE;
        indexBuffer = VK_NULL_HANDLE;
    }
}

void VertexBufferNode::RegisterMeshCacher() {
    auto& mainCacher = GetOwningGraph()->GetMainCacher();
    if (mainCacher.IsRegistered(std::type_index(typeid(CashSystem::MeshWrapper)))) {
        return;
    }

    NODE_LOG_INFO("VertexBufferNode: Registering MeshCacher");
    mainCacher.RegisterCacher<
        CashSystem::MeshCacher,
        CashSystem::MeshWrapper,
        CashSystem::MeshCreateParams
    >(std::type_index(typeid(CashSystem::MeshWrapper)), "Mesh", true);
}

void VertexBufferNode::CreateMeshBuffers() {
    auto& mainCacher = GetOwningGraph()->GetMainCacher();
    auto* cacher = mainCacher.GetCacher<
        CashSystem::MeshCacher,
        CashSystem::MeshWrapper,
        CashSystem::MeshCreateParams
    >(std::type_index(typeid(CashSystem::MeshWrapper)), device);

    if (!cacher) {
        throw std::runtime_error("VertexBufferNode: Failed to get MeshCacher");
    }

    // Build cache parameters for procedural geometry
    CashSystem::MeshCreateParams cacheParams{};
    cacheParams.filePath = "";  // Empty = procedural data
    cacheParams.vertexDataPtr = geometryData;
    cacheParams.vertexDataSize = vertexCount * vertexStride;
    cacheParams.vertexStride = vertexStride;
    cacheParams.vertexCount = vertexCount;
    cacheParams.indexCount = indexCount;
    cacheParams.indexDataPtr = nullptr;
    cacheParams.indexDataSize = 0;
    cacheParams.vertexMemoryFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    cacheParams.indexMemoryFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    cachedMeshWrapper = cacher->GetOrCreate(cacheParams);
    if (!cachedMeshWrapper || cachedMeshWrapper->vertexBuffer == VK_NULL_HANDLE) {
        throw std::runtime_error("VertexBufferNode: Failed to create mesh");
    }

    vertexBuffer = cachedMeshWrapper->vertexBuffer;
    indexBuffer = cachedMeshWrapper->indexBuffer;

    NODE_LOG_DEBUG("VertexBufferNode: Mesh created (vertex=" +
                   std::to_string(reinterpret_cast<uint64_t>(vertexBuffer)) + ")");
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

    VkResult result = vkCreateBuffer(device->device, &bufferInfo, nullptr, &buffer);
    if (result != VK_SUCCESS) {
        VulkanError error{result, "Failed to create buffer"};
        NODE_LOG_ERROR(error.toString());
        throw std::runtime_error(error.toString());
    }

    // Get memory requirements
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device->device, buffer, &memRequirements);
    
    // Allocate memory
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = nullptr;
    allocInfo.allocationSize = memRequirements.size;

    // Find suitable memory type
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(*device->gpu, &memProperties);

    VkMemoryPropertyFlags requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    auto memoryTypeIndex = device->MemoryTypeFromProperties(
        memRequirements.memoryTypeBits,
        requiredFlags
	);

    if (!memoryTypeIndex.has_value())
    {
        vkDestroyBuffer(device->device, buffer, nullptr);
        VulkanError error{VK_ERROR_INITIALIZATION_FAILED, "Failed to find suitable memory type for buffer"};
        NODE_LOG_ERROR(error.toString());
		throw std::runtime_error(error.toString());
    }

    allocInfo.memoryTypeIndex = memoryTypeIndex.value();

    result = vkAllocateMemory(device->device, &allocInfo, nullptr, &memory);
    if (result != VK_SUCCESS) {
        vkDestroyBuffer(device->device, buffer, nullptr);
        VulkanError error{result, "Failed to allocate buffer memory"};
        NODE_LOG_ERROR(error.toString());
        throw std::runtime_error(error.toString());
    }

    // Bind buffer to memory
    result = vkBindBufferMemory(device->device, buffer, memory, 0);
    if (result != VK_SUCCESS) {
        vkFreeMemory(device->device, memory, nullptr);
        vkDestroyBuffer(device->device, buffer, nullptr);
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
    VkResult result = vkMapMemory(device->device, memory, 0, size, 0, &mappedData);
    if (result != VK_SUCCESS) {
        VulkanError error{result, "Failed to map buffer memory"};
        NODE_LOG_ERROR(error.toString());
        throw std::runtime_error(error.toString());
    }

    // Copy data
    std::memcpy(mappedData, data, static_cast<size_t>(size));

    // Unmap memory
    vkUnmapMemory(device->device, memory);
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
