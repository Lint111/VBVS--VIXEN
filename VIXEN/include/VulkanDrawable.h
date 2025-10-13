#pragma once
#include "Headers.h"
#include "wrapper.h"
class VulkanRenderer;
class VulkanApplication;
class VulkanSwapChain;


class VulkanDrawable {
    struct VertAttBuffer {
        VkBuffer buf;
        VkDeviceMemory mem;
        VkDescriptorBufferInfo bufferInfo;
	};

    public:
    VulkanDrawable(VulkanRenderer* parent = 0);
    ~VulkanDrawable();

    void CreateVertexBuffer(const void* vertexData,
                            uint32_t dataSize,
                            uint32_t dataStride,
                            bool useTexture = false);

	void CreateVertexIndex(const void* indexData, 
                          uint32_t dataSize, 
                          uint32_t dataStride);
    
    void Prepare();
    VkResult Render();

    void InitViewports(VkCommandBuffer* cmd);
    void InitScissors(VkCommandBuffer* cmd);

    void SetPipeline(VkPipeline vulkanPipeline) { pipelineHandle = vulkanPipeline; }
    VkPipeline GetPipeline() { return pipelineHandle; }
    VulkanRenderer* GetRenderer() { return rendererObj; }
    
    void DestroyCommandBuffer();
    void DestroyVertexBuffer();
	void DestroyIndexBuffer();
    void DestroySynchronizationObjects();

    VertAttBuffer VertexBuffer;

	VertAttBuffer IndexBuffer;

    VkVertexInputBindingDescription viIpBind;
    VkVertexInputAttributeDescription viIpAttr[2];

    private:
    std::vector<VkCommandBuffer> vecCmdDraw;
    
    void RecordCommandBuffer(int currentImage, VkCommandBuffer* cmdDraw);

    VkViewport viewport;
    VkRect2D scissor;
    VkSemaphore presentCompleteSemaphore;
    VkSemaphore drawingCompleteSemaphore;
    
    VulkanRenderer* rendererObj;
    VkPipeline pipelineHandle;
};