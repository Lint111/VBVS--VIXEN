#pragma once

#include "Headers.h"

// Utility function to read file into memory
void* ReadFile(const char* filePath, size_t* outSize);

class CommandBufferMgr {
public:
    //Allocate memory for command buffers from the command pool
    static void AllocateCommandBuffer(const VkDevice* pDevice,
                                      const VkCommandPool commandPool,
                                      VkCommandBuffer* pCmdBuffer,
                                      const VkCommandBufferAllocateInfo* pCmdBufferInfo = NULL);

    //Start Command buffer recording
    static void BeginCommandBuffer(VkCommandBuffer cmdBuf,
                                   VkCommandBufferBeginInfo* pInCmdBufInfo = NULL);

    //End Command buffer recording
    static void EndCommandBuffer(VkCommandBuffer cmdBuf);

    //Add commands to the command buffer
    static void AddCommandBuffer(VkCommandBuffer cmdBuf);

    //submit the command buffer for execution
    static void SubmitCommandBuffer(const VkQueue& queue,
                                    const VkCommandBuffer* pCmdBufList,
                                    const VkSubmitInfo* pSubmitInfo = NULL,
                                    const VkFence& fence = VK_NULL_HANDLE);



};