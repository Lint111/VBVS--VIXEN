#pragma once
#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "VertexBufferNodeConfig.h"
#include "../../include/MeshData.h"
#include <memory>

namespace Vixen::RenderGraph {

/**
 * @brief Node type for creating and uploading vertex buffers to GPU
 *
 * Creates vertex buffers with vertex input descriptions for use in graphics pipelines.
 * Supports both vertex-only and indexed rendering configurations.
 *
 * Type ID: 103
 */
class VertexBufferNodeType : public NodeType {
public:
    VertexBufferNodeType(const std::string& typeName = "VertexBuffer");
    virtual ~VertexBufferNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * @brief Typed node instance for vertex buffer creation and management
 *
 * Uses VertexBufferNodeConfig for compile-time type safety.
 *
 * Inputs: None (vertex data provided via parameters)
 *
 * Outputs:
 * - VERTEX_BUFFER (VkBuffer) - GPU buffer containing vertex data
 * - INDEX_BUFFER (VkBuffer, nullable) - GPU buffer containing index data
 *
 * Parameters:
 * - VERTEX_COUNT (uint32_t): Number of vertices
 * - VERTEX_STRIDE (uint32_t): Size of each vertex in bytes
 * - USE_TEXTURE (bool): Whether vertices use texture coordinates
 * - INDEX_COUNT (uint32_t): Number of indices (0 = no indices)
 */
class VertexBufferNode : public TypedNode<VertexBufferNodeConfig> {
public:
    VertexBufferNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    virtual ~VertexBufferNode();

    void Setup() override;
    void Compile() override;
    void Execute(VkCommandBuffer commandBuffer) override;
    void Cleanup() override;

    // Accessors for other nodes
    VkBuffer GetVertexBuffer() const { return vertexBuffer; }
    VkBuffer GetIndexBuffer() const { return indexBuffer; }
    const VkVertexInputBindingDescription& GetVertexBinding() const { return vertexBinding; }
    const std::array<VkVertexInputAttributeDescription, 2>& GetVertexAttributes() const { return vertexAttributes; }
    uint32_t GetVertexCount() const { return vertexCount; }
    uint32_t GetIndexCount() const { return indexCount; }
    bool HasIndices() const { return hasIndices; }

private:
    // Vertex buffer resources
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;

    // Index buffer resources (optional)
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;

    // Vertex input description
    VkVertexInputBindingDescription vertexBinding{};
    std::array<VkVertexInputAttributeDescription, 2> vertexAttributes{};

    // Metadata
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint32_t vertexStride = 0;
    bool hasIndices = false;
    bool useTexture = false;

    VulkanDevicePtr vulkanDevice = VK_NULL_HANDLE;

    // Helper functions
    void CreateBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkBuffer& buffer,
        VkDeviceMemory& memory
    );

    void UploadData(
        VkDeviceMemory memory,
        const void* data,
        VkDeviceSize size
    );

    void SetupVertexInputDescription();
};

} // namespace Vixen::RenderGraph
