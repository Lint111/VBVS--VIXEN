#include "Nodes/VertexBufferNode.h"
#include "Core/RenderGraph.h"
#include "VulkanResources/VulkanDevice.h"
#include "Core/NodeLogging.h"
#include "error/VulkanError.h"
#include "CashSystem/MeshCacher.h"
#include "CashSystem/MainCacher.h"
#include <cstring>
#include <typeindex>

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

void VertexBufferNode::SetupImpl(Context& ctx) {
    // Read and validate device input
    VulkanDevicePtr devicePtr = ctx.In(VertexBufferNodeConfig::VULKAN_DEVICE_IN);
    if (devicePtr == nullptr) {
        throw std::runtime_error("VertexBufferNode: Invalid device handle");
    }

    // Set base class device member for cleanup tracking
    SetDevice(devicePtr);

    NODE_LOG_INFO("Setup: Vertex buffer node ready");
}

void VertexBufferNode::CompileImpl(Context& ctx) {
    NODE_LOG_INFO("Compile: Creating vertex and index buffers via MeshCacher");

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

    indexCount = GetParameterValue<uint32_t>(
        VertexBufferNodeConfig::PARAM_INDEX_COUNT, 0);
    hasIndices = (indexCount > 0);

    // Register MeshCacher if not already registered
    auto& mainCacher = GetOwningGraph()->GetMainCacher();
    if (!mainCacher.IsRegistered(std::type_index(typeid(CashSystem::MeshWrapper)))) {
        NODE_LOG_INFO("Registering MeshCacher with MainCacher");
        mainCacher.RegisterCacher<
            CashSystem::MeshCacher,
            CashSystem::MeshWrapper,
            CashSystem::MeshCreateParams
        >(
            std::type_index(typeid(CashSystem::MeshWrapper)),
            "Mesh",
            true  // device-dependent
        );
    }

    // Get or create cached mesh
    auto* cacher = mainCacher.GetCacher<
        CashSystem::MeshCacher,
        CashSystem::MeshWrapper,
        CashSystem::MeshCreateParams
    >(std::type_index(typeid(CashSystem::MeshWrapper)), device);

    if (!cacher) {
        throw std::runtime_error("VertexBufferNode: Failed to get MeshCacher from MainCacher");
    }

    // Build cache parameters
    CashSystem::MeshCreateParams cacheParams{};
    // For now, using procedural geometry data (no file path)
    cacheParams.filePath = "";  // Empty = procedural data
    cacheParams.vertexDataPtr = geometryData;
    cacheParams.vertexDataSize = vertexCount * vertexStride;
    cacheParams.vertexStride = vertexStride;
    cacheParams.vertexCount = vertexCount;
    cacheParams.indexCount = indexCount;
    cacheParams.indexDataPtr = nullptr;  // No index data for now
    cacheParams.indexDataSize = 0;
    cacheParams.vertexMemoryFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    cacheParams.indexMemoryFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    // Get or create cached mesh
    cachedMeshWrapper = cacher->GetOrCreate(cacheParams);

    if (!cachedMeshWrapper || cachedMeshWrapper->vertexBuffer == VK_NULL_HANDLE) {
        throw std::runtime_error("VertexBufferNode: Failed to get or create mesh from cache");
    }

    // Store direct references for convenience
    vertexBuffer = cachedMeshWrapper->vertexBuffer;
    indexBuffer = cachedMeshWrapper->indexBuffer;

    NODE_LOG_DEBUG("Using cached mesh: vertex buffer = " +
                   std::to_string(reinterpret_cast<uint64_t>(vertexBuffer)) +
                   ", index buffer = " +
                   std::to_string(reinterpret_cast<uint64_t>(indexBuffer)));

    // Setup vertex input description
    SetupVertexInputDescription();

    // Set outputs
    std::cout << "[VertexBufferNode::Compile] vertexBuffer BEFORE ctx.Out(): " << reinterpret_cast<uint64_t>(vertexBuffer) << std::endl;
    NODE_LOG_DEBUG("Setting VERTEX_BUFFER output: " + std::to_string(reinterpret_cast<uint64_t>(vertexBuffer)));
    ctx.Out(VertexBufferNodeConfig::VERTEX_BUFFER, vertexBuffer);
    std::cout << "[VertexBufferNode::Compile] vertexBuffer AFTER ctx.Out(): " << reinterpret_cast<uint64_t>(vertexBuffer) << std::endl;
    if (hasIndices) {
        NODE_LOG_DEBUG("Setting INDEX_BUFFER output: " + std::to_string(reinterpret_cast<uint64_t>(indexBuffer)));
        ctx.Out(VertexBufferNodeConfig::INDEX_BUFFER, indexBuffer);
    }
    ctx.Out(VertexBufferNodeConfig::VULKAN_DEVICE_OUT, device);

    NODE_LOG_INFO("Compile complete: Vertex buffer ready (via cache)");
}

void VertexBufferNode::ExecuteImpl(Context& ctx) {
    // Vertex buffer creation happens in Compile phase
    // Execute is a no-op for this node
}

void VertexBufferNode::CleanupImpl() {
    // Release cached wrapper - cacher owns VkBuffer and VkDeviceMemory and destroys when appropriate
    if (cachedMeshWrapper) {
        NODE_LOG_DEBUG("Cleanup: Releasing cached mesh wrapper (cacher owns resources)");
        cachedMeshWrapper.reset();
        vertexBuffer = VK_NULL_HANDLE;
        indexBuffer = VK_NULL_HANDLE;
    }
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
