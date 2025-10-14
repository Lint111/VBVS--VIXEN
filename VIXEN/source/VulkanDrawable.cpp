#include "VulkanDrawable.h"
#include "VulkanApplication.h"
#include "VulkanRenderer.h"
#include "VulkanSwapChain.h"

VulkanDrawable::VulkanDrawable(VulkanRenderer *parent)
{
    memset(&VertexBuffer, 0, sizeof(VertexBuffer));
    rendererObj = parent;

    // Prepare the semaphore create info data structure
    VkSemaphoreCreateInfo presentCompleteSemaphoreCreateInfo = {};
    presentCompleteSemaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    presentCompleteSemaphoreCreateInfo.pNext = nullptr;
    presentCompleteSemaphoreCreateInfo.flags = 0;
    VkSemaphoreCreateInfo drawingCompleteSemaphoreCreateInfo = {};
    drawingCompleteSemaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    drawingCompleteSemaphoreCreateInfo.pNext = nullptr;
    drawingCompleteSemaphoreCreateInfo.flags = 0;

    VulkanDevice* deviceObj = VulkanApplication::GetInstance()->deviceObj.get();

    // Create the semaphores
    VkResult result = vkCreateSemaphore(
        deviceObj->device, 
        &presentCompleteSemaphoreCreateInfo, 
        nullptr, 
        &presentCompleteSemaphore
    );
    assert(result == VK_SUCCESS);

    result = vkCreateSemaphore(
        deviceObj->device, 
        &drawingCompleteSemaphoreCreateInfo, 
        nullptr, 
        &drawingCompleteSemaphore
    );
    assert(result == VK_SUCCESS);
}

VulkanDrawable::~VulkanDrawable()
{
    DestroyVertexBuffer();

    DestroyCommandBuffer();

    DestroySynchronizationObjects();
}

void VulkanDrawable::Prepare()
{
    VulkanDevice* deviceObj = rendererObj->GetDevice();
    vecCmdDraw.resize(rendererObj->GetSwapChain()->scPublicVars.colorBuffers.size());

    for (int i = 0; i < rendererObj->GetSwapChain()->scPublicVars.colorBuffers.size(); i++) {
        CommandBufferMgr::AllocateCommandBuffer(
            &deviceObj->device,
            rendererObj->GetCommandPool(),
            &vecCmdDraw[i]
        );

        CommandBufferMgr::BeginCommandBuffer(vecCmdDraw[i]);
        RecordCommandBuffer(i, &vecCmdDraw[i]);
        CommandBufferMgr::EndCommandBuffer(vecCmdDraw[i]);
    }

}

VkResult VulkanDrawable::Render()
{
    VulkanDevice* deviceObj = rendererObj->GetDevice();
    VulkanSwapChain* swapChainObj = rendererObj->GetSwapChain();

    uint32_t& currentColorImage = swapChainObj->scPublicVars.currentColorBuffer;
    VkSwapchainKHR& swapChain = swapChainObj->scPublicVars.swapChain;

    VkFence nullFence = VK_NULL_HANDLE;

    // Create semaphores for synchronization

    constexpr uint64_t ACQUIRE_IMAGE_TIMEOUT_NS = UINT64_MAX; // No timeout
    VkResult result = swapChainObj->fpAcquireNextImageKHR(
        deviceObj->device,
        swapChain,
        ACQUIRE_IMAGE_TIMEOUT_NS,
        presentCompleteSemaphore,
        nullFence,
        &currentColorImage
    );
    if(result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || result != VK_SUCCESS) {
        return result;
    }
    
    // Setup submit info to wait on image acquired and signal when render complete
    VkPipelineStageFlags submitPipelineStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pNext = nullptr;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &presentCompleteSemaphore;
    submitInfo.pWaitDstStageMask = &submitPipelineStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &vecCmdDraw[currentColorImage];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &drawingCompleteSemaphore;

    CommandBufferMgr::SubmitCommandBuffer(
        deviceObj->queue,
        &vecCmdDraw[currentColorImage],
        &submitInfo
    );

    // Present the image, waiting for render to complete
    VkPresentInfoKHR present = {};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.pNext = nullptr;
    present.waitSemaphoreCount = 1;
    present.swapchainCount = 1;
    present.pSwapchains = &swapChain;
    present.pImageIndices = &currentColorImage;
    present.pWaitSemaphores = &drawingCompleteSemaphore;
    present.waitSemaphoreCount = 1;
    present.pResults = nullptr;

    result = swapChainObj->fpQueuePresentKHR(deviceObj->queue, &present);

    return result;
}

void VulkanDrawable::InitViewports(VkCommandBuffer *cmd)
{    
    viewport.height = static_cast<float>(rendererObj->height);
    viewport.width = static_cast<float>(rendererObj->width);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    viewport.x = 0;
    viewport.y = 0;

    vkCmdSetViewport(*cmd, 0, NUMBER_OF_VIEWPORTS, &viewport);
}

void VulkanDrawable::InitScissors(VkCommandBuffer *cmd)
{
    scissor.extent.width = rendererObj->width;
    scissor.extent.height = rendererObj->height;
    scissor.offset.x = 0;
    scissor.offset.y = 0;

    vkCmdSetScissor(*cmd, 0, NUMBER_OF_SCISSORS, &scissor);
}

void VulkanDrawable::DestroyCommandBuffer()
{
    VulkanApplication* appObj = VulkanApplication::GetInstance();
    VulkanDevice* deviceObj = appObj->deviceObj.get();
    for (int i = 0; i < vecCmdDraw.size(); i++) {
        
        if (vecCmdDraw[i] == VK_NULL_HANDLE)
            continue;

        vkFreeCommandBuffers(
            deviceObj->device,
            rendererObj->GetCommandPool(),
            1,
            &vecCmdDraw[i]
         );
    }
    vecCmdDraw.clear();
}

void VulkanDrawable::DestroyVertexBuffer()
{
    VulkanDevice* deviceObj = rendererObj->GetDevice();
    if (VertexBuffer.buf != VK_NULL_HANDLE) {
        vkDestroyBuffer(deviceObj->device, VertexBuffer.buf, nullptr);
        vkFreeMemory(deviceObj->device, VertexBuffer.mem, nullptr);
        VertexBuffer.mem = VK_NULL_HANDLE;
        VertexBuffer.buf = VK_NULL_HANDLE;
    }
}

void VulkanDrawable::DestroyIndexBuffer()
{
    VulkanDevice* deviceObj = rendererObj->GetDevice();
    if (IndexBuffer.buf != VK_NULL_HANDLE) {
        vkDestroyBuffer(deviceObj->device, IndexBuffer.buf, nullptr);
        vkFreeMemory(deviceObj->device, IndexBuffer.mem, nullptr);
        IndexBuffer.mem = VK_NULL_HANDLE;
        IndexBuffer.buf = VK_NULL_HANDLE;
    }
}

void VulkanDrawable::DestroySynchronizationObjects()
{
    VulkanApplication* appObj = VulkanApplication::GetInstance();
    VulkanDevice* deviceObj = appObj->deviceObj.get();
    if (presentCompleteSemaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(deviceObj->device, presentCompleteSemaphore, nullptr);
        presentCompleteSemaphore = VK_NULL_HANDLE;
    }
    if (drawingCompleteSemaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(deviceObj->device, drawingCompleteSemaphore, nullptr);
        drawingCompleteSemaphore = VK_NULL_HANDLE;
    }
}

void VulkanDrawable::RecordCommandBuffer(int currentImage, VkCommandBuffer *cmdDraw)
{
    VulkanDevice* deviceObj = rendererObj->GetDevice();

    constexpr uint32_t MAX_CLEAR_VALUES = 2;
    VkClearValue clearValues[MAX_CLEAR_VALUES];
    clearValues[0].color.float32[0] = 0.0f;  // Clear to black
    clearValues[0].color.float32[1] = 0.0f;
    clearValues[0].color.float32[2] = 0.0f;
    clearValues[0].color.float32[3] = 1.0f;

    clearValues[1].depthStencil.depth = 1.0f;
    clearValues[1].depthStencil.stencil = 0;

    VkRenderPassBeginInfo rpBeginInfo = {};
    rpBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBeginInfo.pNext = nullptr;
    rpBeginInfo.renderPass = rendererObj->renderPass;
    rpBeginInfo.framebuffer = rendererObj->frameBuffers[currentImage];
    rpBeginInfo.renderArea.offset.x = 0;
    rpBeginInfo.renderArea.offset.y = 0;
    rpBeginInfo.renderArea.extent.width = rendererObj->width;
    rpBeginInfo.renderArea.extent.height = rendererObj->height;
    rpBeginInfo.clearValueCount = MAX_CLEAR_VALUES;
    rpBeginInfo.pClearValues = clearValues;

    vkCmdBeginRenderPass(
        *cmdDraw, 
        &rpBeginInfo, 
        VK_SUBPASS_CONTENTS_INLINE
    );

    vkCmdBindPipeline(
        *cmdDraw, 
        VK_PIPELINE_BIND_POINT_GRAPHICS, 
        pipelineHandle
    );

    const VkDeviceSize offsets[1] = {0};
    vkCmdBindVertexBuffers(
        *cmdDraw,
        0,
        1,
        &VertexBuffer.buf,
        offsets
    );

	vkCmdBindIndexBuffer(
		*cmdDraw,
		IndexBuffer.buf,
		0,
		VK_INDEX_TYPE_UINT16
	);

    InitViewports(cmdDraw);
    InitScissors(cmdDraw);

    vkCmdDrawIndexed(*cmdDraw, 6, 1, 0, 0, 0);

    vkCmdEndRenderPass(*cmdDraw);
}



void VulkanDrawable::CreateVertexBuffer(const void *vertexData, uint32_t dataSize, uint32_t dataStride, bool useTexture)
{
    VulkanApplication* appObj = VulkanApplication::GetInstance();
    VulkanDevice* deviceObj = appObj->deviceObj.get();

    VkResult result;
    bool pass;

    // Create the buffer resource metadata
    VkBufferCreateInfo bufInfo = {};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.pNext = nullptr;
    bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufInfo.size = dataSize;
    bufInfo.queueFamilyIndexCount = 0;
    bufInfo.pQueueFamilyIndices = nullptr;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufInfo.flags = 0;

    // Create the buffer resource
    result = vkCreateBuffer(
        deviceObj->device, 
        &bufInfo, 
        nullptr, 
        &VertexBuffer.buf
    );
    assert(result == VK_SUCCESS);


    // Get the memory requirements for this buffer resource
    VkMemoryRequirements memRqrmntVertexBuffer;
    vkGetBufferMemoryRequirements(
        deviceObj->device, 
        VertexBuffer.buf, 
        &memRqrmntVertexBuffer
    );


    // Create memory allocation information
    VkMemoryAllocateInfo alloc_info_vertex_buffer = {};
    alloc_info_vertex_buffer.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info_vertex_buffer.pNext = nullptr;
    alloc_info_vertex_buffer.memoryTypeIndex = 0;
    alloc_info_vertex_buffer.allocationSize = memRqrmntVertexBuffer.size;

    // Get the memory type index that has the properties we require
    pass = deviceObj->MemoryTypeFromProperties(
        memRqrmntVertexBuffer.memoryTypeBits, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | 
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
        &alloc_info_vertex_buffer.memoryTypeIndex
    );

    assert(pass);



    // Allocate the physical backing for buffer resource
    result = vkAllocateMemory(
        deviceObj->device, 
        &alloc_info_vertex_buffer, 
        nullptr, 
        &VertexBuffer.mem
    );

    assert(result == VK_SUCCESS);


    VertexBuffer.bufferInfo.range = memRqrmntVertexBuffer.size;
    VertexBuffer.bufferInfo.offset = 0;

    // Map the physical device memory region to the host
    uint8_t* pVertexData = nullptr;
    result = vkMapMemory(
        deviceObj->device, 
        VertexBuffer.mem, 
        0, 
        memRqrmntVertexBuffer.size, 
        0, 
        (void **)&pVertexData
    );

    assert(result == VK_SUCCESS);




    // Copy the data in the mapped memory
    memcpy(pVertexData, vertexData, dataSize);

    // Unmap the device memory
    vkUnmapMemory(deviceObj->device, VertexBuffer.mem);

    // Bind the allocated buffer resource to the device memory
    result = vkBindBufferMemory(
        deviceObj->device, 
        VertexBuffer.buf, 
        VertexBuffer.mem, 
        0
    );

    assert(result == VK_SUCCESS);

    // The VkVertexInputVinding viIpBind, stores the rate at 
    // which the information will be injected for vertex input.
    viIpBind.binding = 0;
    viIpBind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    viIpBind.stride = dataStride;

    // The VkVertexInputAttribute - Description structure,
    // store the information that helps in interpreting the data.
    viIpAttr[0].binding = 0;
    viIpAttr[0].location = 0;
    viIpAttr[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;  // vec4
    viIpAttr[0].offset = 0;
    viIpAttr[1].binding = 0;
    viIpAttr[1].location = 1;
    viIpAttr[1].format = useTexture ? VK_FORMAT_R32G32_SFLOAT : VK_FORMAT_R32G32B32A32_SFLOAT;  // vec4
    viIpAttr[1].offset = 16;  // 4 floats for position = 16 bytes





}

void VulkanDrawable::CreateVertexIndex(const void* indexData, uint32_t dataSize, uint32_t dataStride)
{
    VulkanApplication* appObj = VulkanApplication::GetInstance();
    VulkanDevice* deviceObj = appObj->deviceObj.get();

    VkResult result;
    bool pass;


    // Create the Index buffer resource metadata
    VkBufferCreateInfo indexBufInfo = {};
    indexBufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    indexBufInfo.pNext = nullptr;
    indexBufInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    indexBufInfo.size = dataSize;
    indexBufInfo.queueFamilyIndexCount = 0;
    indexBufInfo.pQueueFamilyIndices = nullptr;
    indexBufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    indexBufInfo.flags = 0;

    // Create the buffer resource
    result = vkCreateBuffer(
        deviceObj->device,
        &indexBufInfo,
        nullptr,
        &IndexBuffer.buf
    );
    assert(result == VK_SUCCESS);

    VkMemoryRequirements memRqrmntIndexBuffer;
    vkGetBufferMemoryRequirements(
        deviceObj->device,
        IndexBuffer.buf,
        &memRqrmntIndexBuffer
    );


    // Get the memory type index that has the properties we require
    VkMemoryAllocateInfo alloc_info_index_buffer = {};
	alloc_info_index_buffer.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info_index_buffer.pNext = nullptr;
	alloc_info_index_buffer.memoryTypeIndex = 0;
	alloc_info_index_buffer.allocationSize = memRqrmntIndexBuffer.size;

    pass = deviceObj->MemoryTypeFromProperties(
        memRqrmntIndexBuffer.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &alloc_info_index_buffer.memoryTypeIndex
    );
    assert(pass);


    result = vkAllocateMemory(
        deviceObj->device,
        &alloc_info_index_buffer,
        nullptr,
        &IndexBuffer.mem
    );
    assert(result == VK_SUCCESS);

    IndexBuffer.bufferInfo.range = memRqrmntIndexBuffer.size;
    IndexBuffer.bufferInfo.offset = 0;


    uint8_t* pIndexData = nullptr;
    result = vkMapMemory(
        deviceObj->device,
        IndexBuffer.mem,
        0,
        memRqrmntIndexBuffer.size,
        0,
        (void**)&pIndexData
    );
    assert(result == VK_SUCCESS);

    memcpy(pIndexData, indexData , dataSize);

    vkUnmapMemory(deviceObj->device, IndexBuffer.mem);
    
	result = vkBindBufferMemory(
		deviceObj->device,
		IndexBuffer.buf,
		IndexBuffer.mem,
		0
	);
    assert(result == VK_SUCCESS);


}
