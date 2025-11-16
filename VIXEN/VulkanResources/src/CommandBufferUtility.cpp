#include "VulkanResources/CommandBufferUtility.h"
#include <fstream>

void* ReadFile(const char* filePath, size_t* outSize) {
    std::ifstream file(filePath, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filePath << std::endl;
        *outSize = 0;
        return nullptr;
    }

    size_t fileSize = (size_t)file.tellg();
    char* buffer = new char[fileSize + 1];  // +1 for null terminator

    file.seekg(0);
    file.read(buffer, fileSize);
    buffer[fileSize] = '\0';  // Null terminate for text files
    file.close();

    *outSize = fileSize;
    return buffer;
}

void CommandBufferMgr::AllocateCommandBuffer(const VkDevice *pDevice, const VkCommandPool cmdPool, VkCommandBuffer *pCmdBuf, const VkCommandBufferAllocateInfo *pCmdBufferInfo)
{
    VkResult result;

    if(pCmdBufferInfo) {
        result = vkAllocateCommandBuffers(*pDevice,pCmdBufferInfo,pCmdBuf);
        assert(!result);
        return;
    }

    // default implementation, create the command buffer
    // allocation info and use the supplied parameters into it

    VkCommandBufferAllocateInfo cmdInfo = {};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdInfo.pNext = nullptr;
    cmdInfo.commandPool = cmdPool;
    cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdInfo.commandBufferCount = (uint32_t) sizeof(pCmdBuf) / sizeof(VkCommandBuffer);

    result = vkAllocateCommandBuffers(*pDevice,&cmdInfo,pCmdBuf);
    assert(!result);
}

void CommandBufferMgr::BeginCommandBuffer(VkCommandBuffer cmdBuf, VkCommandBufferBeginInfo *pInCmdBufInfo)
{
    VkResult result;

    // if the user has supplied a command buffer begin info structure
    if(pInCmdBufInfo) {
        result = vkBeginCommandBuffer(cmdBuf,pInCmdBufInfo);
        assert(!result);
        return;
    }

    // default implementation, create the command buffer
    VkCommandBufferInheritanceInfo cmdBufInheritInfo = {};
    cmdBufInheritInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
    cmdBufInheritInfo.pNext = nullptr;
    cmdBufInheritInfo.renderPass = VK_NULL_HANDLE;
    cmdBufInheritInfo.subpass = 0;
    cmdBufInheritInfo.framebuffer = VK_NULL_HANDLE;
    cmdBufInheritInfo.occlusionQueryEnable = VK_FALSE;
    cmdBufInheritInfo.queryFlags = 0;
    cmdBufInheritInfo.pipelineStatistics = 0;

    VkCommandBufferBeginInfo cmdBufInfo = {};
    cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBufInfo.pNext = nullptr;
    cmdBufInfo.flags = 0;
    cmdBufInfo.pInheritanceInfo = &cmdBufInheritInfo;

    result = vkBeginCommandBuffer(cmdBuf,&cmdBufInfo);
    assert(result == VK_SUCCESS);
}

void CommandBufferMgr::EndCommandBuffer(VkCommandBuffer cmdBuf)
{
    VkResult result;

    result = vkEndCommandBuffer(cmdBuf);
    assert(result == VK_SUCCESS);
}

void CommandBufferMgr::AddCommandBuffer(VkCommandBuffer cmdBuf)
{
    VkResult result;
    result = vkEndCommandBuffer(cmdBuf);
    assert(result == VK_SUCCESS);
}

void CommandBufferMgr::SubmitCommandBuffer(const VkQueue &queue, const VkCommandBuffer *pCmdBufList, const VkSubmitInfo *pInSubmitInfo, const VkFence &fence)
{
    VkResult result;

    // if the user has supplied a submit info structure
    if(pInSubmitInfo) {
        vkQueueSubmit(queue,1,pInSubmitInfo,fence);
        result = vkQueueWaitIdle(queue);
        return;
    }

    // default implementation, create the submit info structure
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pNext = nullptr;
    submitInfo.waitSemaphoreCount = 0;
    submitInfo.pWaitSemaphores = nullptr;
    submitInfo.pWaitDstStageMask = nullptr;
    submitInfo.commandBufferCount = (uint32_t) sizeof(pCmdBufList) / sizeof(VkCommandBuffer);
    submitInfo.pCommandBuffers = pCmdBufList;
    submitInfo.signalSemaphoreCount = 0;
    submitInfo.pSignalSemaphores = nullptr;

    result = vkQueueSubmit(queue,1,&submitInfo,fence);
    assert(!result);

    result = vkQueueWaitIdle(queue);
    assert(!result);
}
