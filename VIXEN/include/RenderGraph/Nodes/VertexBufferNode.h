#pragma once
#include "RenderGraph/NodeInstance.h"
#include "RenderGraph/NodeType.h"
#include "MeshData.h"
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
    VertexBufferNodeType();
    virtual ~VertexBufferNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(
        const std::string& instanceName,
        Vixen::Vulkan::Resources::VulkanDevice* device
    ) const override;
};

/**
 * @brief Node instance for vertex buffer creation and management
 * 
 * Parameters:
 * - vertexData (string): Path to vertex data or inline data specification
 * - vertexCount (uint32_t): Number of vertices
 * - vertexStride (uint32_t): Size of each vertex in bytes
 * - useTexture (bool): Whether vertices use texture coordinates (affects attribute layout)
 * - indexData (string, optional): Path to index data for indexed rendering
 * - indexCount (uint32_t, optional): Number of indices
 * 
 * Outputs:
 * - vertexBuffer: GPU buffer containing vertex data
 * - indexBuffer (optional): GPU buffer containing index data
 * - vertexInputDescription: Vertex input binding and attribute descriptions
 */
class VertexBufferNode : public NodeInstance {
public:
    VertexBufferNode(
        const std::string& instanceName,
        NodeType* nodeType,
        Vixen::Vulkan::Resources::VulkanDevice* device
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
