#pragma once
#include "Headers.h"
#include "wrapper.h"
#include "VulkanDescriptor.h"

class VulkanRenderer;
class VulkanApplication;
class VulkanSwapChain;


class VulkanDrawable : public VulkanDescriptor {
    struct VertAttBuffer {
        VkBuffer buf;
        VkDeviceMemory mem;
        VkDescriptorBufferInfo bufferInfo;
	};

    public:
    VulkanDrawable(VulkanRenderer* parent = 0);
    ~VulkanDrawable();

    VulkanStatus Initialize();

    VulkanStatus CreateVertexBuffer(const void* vertexData,
                                     uint32_t dataSize,
                                     uint32_t dataStride,
                                     bool useTexture = false);

	VulkanStatus CreateVertexIndex(const void* indexData,
                                    uint32_t dataSize,
                                    uint32_t dataStride);
    
    void Prepare();
    VulkanStatus Update(float deltaTime);
    VkResult Render();

    void InitViewports(VkCommandBuffer* cmd);
    void InitScissors(VkCommandBuffer* cmd);

    void SetPipeline(VkPipeline vulkanPipeline) { pipelineHandle = vulkanPipeline; }
    VkPipeline GetPipeline() { return pipelineHandle; }
    VulkanRenderer* GetRenderer() { return rendererObj; }

    struct UniformData {
        VkBuffer buf;
        VkDeviceMemory mem;
        VkDescriptorBufferInfo bufInfo;
        VkMemoryRequirements memRqrmnt;

        std::vector<VkMappedMemoryRange> mappedRange;
        uint8_t* pData;
    } UniformData;

    VulkanStatus CreatePipelineLayout() override;
    VulkanStatus CreateDescriptorSetLayout(bool useTexture) override;
    VulkanStatus CreateDescriptorPool(bool useTexture) override;
    VulkanStatus CreateDescriptorSet(bool useTexture) override;
    VulkanStatus CreateDescriptorResources() override;

    void SetTexture(TextureData* tex) { textures = tex; }
    TextureData* GetTexture() const { return textures; }

    VulkanStatus CreateUniformBuffer();

    // Transformation matrices
    glm::mat4 Projection;
    glm::mat4 View;
    glm::mat4 Model;
    glm::mat4 MVP;

    void DestroyCommandBuffer();
    void DestroyVertexBuffer();
	void DestroyIndexBuffer();
    void DestroySynchronizationObjects();
    VulkanStatus DestroyUniformBuffer();

    VertAttBuffer VertexBuffer;

	VertAttBuffer IndexBuffer;

    VkVertexInputBindingDescription viIpBind;
    VkVertexInputAttributeDescription viIpAttr[2];

    TextureData* textures = nullptr; // Pointer to texture data, if any

    private:
    std::vector<VkCommandBuffer> vecCmdDraw;
    
    void RecordCommandBuffer(int currentImage, VkCommandBuffer* cmdDraw);

    VkViewport viewport;
    VkRect2D scissor;
    std::vector<VkSemaphore> presentCompleteSemaphores;
    std::vector<VkSemaphore> drawingCompleteSemaphores;

    VulkanRenderer* rendererObj;
    VkPipeline pipelineHandle;

};