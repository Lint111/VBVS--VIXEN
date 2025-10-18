#include "VulkanDrawable.h"
#include "VulkanApplication.h"
#include "VulkanRenderer.h"
#include "VulkanSwapChain.h"
#include "VulkanResources/VulkanDevice.h"

using namespace Vixen::Vulkan::Resources;

VulkanDrawable::VulkanDrawable(VulkanRenderer *parent)
    : rendererObj(parent)
{
    // Initialize vertex input structures to zero
    memset(&viIpBind, 0, sizeof(viIpBind));
    memset(&viIpAttr, 0, sizeof(viIpAttr));
    memset(&VertexBuffer, 0, sizeof(VertexBuffer));
    memset(&IndexBuffer, 0, sizeof(IndexBuffer));
}

VulkanStatus VulkanDrawable::Initialize()
{
    deviceObj = VulkanApplication::GetInstance()->deviceObj.get();

    // Prepare the semaphore create info data structure
    VkSemaphoreCreateInfo presentCompleteSemaphoreCreateInfo = {};
    presentCompleteSemaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    presentCompleteSemaphoreCreateInfo.pNext = nullptr;
    presentCompleteSemaphoreCreateInfo.flags = 0;

    VkSemaphoreCreateInfo drawingCompleteSemaphoreCreateInfo = {};
    drawingCompleteSemaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    drawingCompleteSemaphoreCreateInfo.pNext = nullptr;
    drawingCompleteSemaphoreCreateInfo.flags = 0;

    // Create the semaphores
    VK_CHECK(vkCreateSemaphore(deviceObj->device, &presentCompleteSemaphoreCreateInfo, nullptr, &presentCompleteSemaphore),
             "Failed to create present complete semaphore");

    VK_CHECK(vkCreateSemaphore(deviceObj->device, &drawingCompleteSemaphoreCreateInfo, nullptr, &drawingCompleteSemaphore),
             "Failed to create drawing complete semaphore");

    return {};
}

VulkanDrawable::~VulkanDrawable()
{
    // Ensure all GPU resources owned by this drawable are freed.
    // DestroyUniformBuffer must be called here so the uniform VkBuffer and
    // VkDeviceMemory are released before the base-class destructor runs
    // (which destroys descriptor sets/pools and pipeline layout).
    (void)DestroyUniformBuffer();

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

VulkanStatus VulkanDrawable::Update(float deltaTime)
{
    VulkanDevice* deviceObj = rendererObj->GetDevice();

    glm::mat4 Projection = glm::perspective(
        glm::radians(45.0f),
        (float)rendererObj->width / (float)rendererObj->height,
        0.1f,
        256.0f
    );
    glm::mat4 View = glm::lookAt(
        glm::vec3(0, 0, 5), // Camera is at (0,0,5), in World Space
        glm::vec3(0, 0, 0), // and looks at the origin
        glm::vec3(0, 1, 0)  // Head is up (set to 0,-1,0 to look upside-down)
    );
    glm::mat4 Model = glm::mat4(1.0f);

    // Frame-rate independent rotation: rotate at 45 degrees per second
    static float rot = 0.0f;
    rot += glm::radians(45.0f) * deltaTime;

    Model = glm::rotate(Model, rot, glm::vec3(0.0f, 1.0f, 0.0f)) *
            glm::rotate(Model, rot, glm::vec3(1.0f, 1.0f, 1.0f));

    glm::mat4 MVP = Projection * View * Model;

    VK_CHECK(vkInvalidateMappedMemoryRanges(deviceObj->device, static_cast<uint32_t>(UniformData.mappedRange.size()), UniformData.mappedRange.data()),
             "Failed to invalidate mapped memory range");

    // Memory is already persistently mapped - just copy directly
    memcpy(UniformData.pData, &MVP, sizeof(MVP));

    // Flush to make changes visible to GPU
    VK_CHECK(vkFlushMappedMemoryRanges(deviceObj->device, static_cast<uint32_t>(UniformData.mappedRange.size()), UniformData.mappedRange.data()),
             "Failed to flush mapped memory range");

    return {};
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
    submitInfo.commandBufferCount = (uint32_t)sizeof(vecCmdDraw[currentColorImage]) / sizeof(VkCommandBuffer);
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

VulkanStatus VulkanDrawable::DestroyUniformBuffer()
{
    if(UniformData.mem == VK_NULL_HANDLE)
        return {};


    vkUnmapMemory(deviceObj->device, UniformData.mem);
    vkDestroyBuffer(deviceObj->device, UniformData.buf, nullptr);
    vkFreeMemory(deviceObj->device, UniformData.mem, nullptr);
    UniformData.mem = VK_NULL_HANDLE;
    UniformData.buf = VK_NULL_HANDLE;
    UniformData.pData = nullptr;
    UniformData.mappedRange.clear();

    return {};
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

    vkCmdBindDescriptorSets(
        *cmdDraw,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout,
        0,
        1,
        descriptorSet.data(),
        0,
        nullptr 
    );

    // Push constants for fragment shader
    enum ColorFlag {
        RED = 0,
        GREEN = 1,
        BLUE = 2,
        YELLOW = 3,
        MIXED_COLOR = 4
    };

    float mixerValue = 0.3f;
    unsigned int constColorRGPFlag = YELLOW;

    unsigned pushConstants[2] = {};
    pushConstants[0] = constColorRGPFlag;
    memcpy(&pushConstants[1], &mixerValue, sizeof(float));

    vkCmdPushConstants(
        *cmdDraw,
        pipelineLayout,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(pushConstants),
        pushConstants
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

    vkCmdDraw(*cmdDraw, 3 * 2 * 6, 1, 0, 0);

    vkCmdEndRenderPass(*cmdDraw);
}



VulkanStatus VulkanDrawable::CreateVertexBuffer(const void *vertexData, uint32_t dataSize, uint32_t dataStride, bool useTexture)
{
    VulkanApplication* appObj = VulkanApplication::GetInstance();
    VulkanDevice* deviceObj = appObj->deviceObj.get();

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
    VK_CHECK(vkCreateBuffer(deviceObj->device, &bufInfo, nullptr, &VertexBuffer.buf),
             "Failed to create vertex buffer");


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
    auto memTypeResult = deviceObj->MemoryTypeFromProperties(
        memRqrmntVertexBuffer.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    VK_PROPAGATE_ERROR(memTypeResult);
    alloc_info_vertex_buffer.memoryTypeIndex = memTypeResult.value();



    // Allocate the physical backing for buffer resource
    VK_CHECK(vkAllocateMemory(deviceObj->device, &alloc_info_vertex_buffer, nullptr, &VertexBuffer.mem),
             "Failed to allocate vertex buffer memory");


    VertexBuffer.bufferInfo.range = memRqrmntVertexBuffer.size;
    VertexBuffer.bufferInfo.offset = 0;

    // Map the physical device memory region to the host
    uint8_t* pVertexData = nullptr;
    VK_CHECK(vkMapMemory(deviceObj->device, VertexBuffer.mem, 0, memRqrmntVertexBuffer.size, 0, (void **)&pVertexData),
             "Failed to map vertex buffer memory");




    // Copy the data in the mapped memory
    memcpy(pVertexData, vertexData, dataSize);

    // Unmap the device memory
    vkUnmapMemory(deviceObj->device, VertexBuffer.mem);

    // Bind the allocated buffer resource to the device memory
    VK_CHECK(vkBindBufferMemory(deviceObj->device, VertexBuffer.buf, VertexBuffer.mem, 0),
             "Failed to bind vertex buffer memory");

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

    return {};
}

VulkanStatus VulkanDrawable::CreateVertexIndex(const void* indexData, uint32_t dataSize, uint32_t dataStride)
{
    VulkanApplication* appObj = VulkanApplication::GetInstance();
    VulkanDevice* deviceObj = appObj->deviceObj.get();

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
    VK_CHECK(vkCreateBuffer(deviceObj->device, &indexBufInfo, nullptr, &IndexBuffer.buf),
             "Failed to create index buffer");

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

    auto memTypeResult = deviceObj->MemoryTypeFromProperties(
        memRqrmntIndexBuffer.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    VK_PROPAGATE_ERROR(memTypeResult);
    alloc_info_index_buffer.memoryTypeIndex = memTypeResult.value();


    VK_CHECK(vkAllocateMemory(deviceObj->device, &alloc_info_index_buffer, nullptr, &IndexBuffer.mem),
             "Failed to allocate index buffer memory");

    IndexBuffer.bufferInfo.range = memRqrmntIndexBuffer.size;
    IndexBuffer.bufferInfo.offset = 0;


    uint8_t* pIndexData = nullptr;
    VK_CHECK(vkMapMemory(deviceObj->device, IndexBuffer.mem, 0, memRqrmntIndexBuffer.size, 0, (void**)&pIndexData),
             "Failed to map index buffer memory");

    memcpy(pIndexData, indexData , dataSize);

    vkUnmapMemory(deviceObj->device, IndexBuffer.mem);
    
    VK_CHECK(vkBindBufferMemory(deviceObj->device, IndexBuffer.buf, IndexBuffer.mem, 0),
             "Failed to bind index buffer memory");

    return {};
}

// CreatePipelineLayout is a virtual funciton from
// VulkanDescriptor and defined in the VulkanDrawable class.
// Virtual VulkanStatus VulkanDescriptor::vkCreatePipelineLayout() = 0;
// Create the pipeline layout to inject into the pipeline.
VulkanStatus VulkanDrawable::CreatePipelineLayout()
{

    // Setup the push constant range
    const unsigned int pushConstantRangeCount = 1;
    VkPushConstantRange pushConstantRange[pushConstantRangeCount] = {};
    pushConstantRange[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange[0].offset = 0;
    pushConstantRange[0].size = sizeof(int) + sizeof(float); // int + float = 8 bytes

    // Create the pipeline layout using descriptor layout.
    VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo = {};
    pPipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pPipelineLayoutCreateInfo.pNext = nullptr;
    pPipelineLayoutCreateInfo.pushConstantRangeCount = pushConstantRangeCount;
    pPipelineLayoutCreateInfo.pPushConstantRanges = pushConstantRange;
    pPipelineLayoutCreateInfo.setLayoutCount = static_cast<uint32_t>(descLayout.size());
    pPipelineLayoutCreateInfo.pSetLayouts = descLayout.data();

    auto result = vkCreatePipelineLayout(
        deviceObj->device,
        &pPipelineLayoutCreateInfo,
        nullptr,
        &pipelineLayout
    );

    if (result != VK_SUCCESS) {
        return std::unexpected(VulkanError{ result, "Failed to create pipeline layout!" });
    }

    return {};
}

// Descriptor class for managing descriptor sets and layouts
VulkanStatus VulkanDrawable::CreateDescriptorSetLayout(bool useTexture)
{
    // Define the layout binding for the 
    // descriptor set (before creating it), specify binding point,
    // shader type (vertex, fragment etc.), count etc.
    const int NUMBER_OF_BINDINGS = 2;

    VkDescriptorSetLayoutBinding layoutBinding[NUMBER_OF_BINDINGS];
    layoutBinding[0].binding = 0; // DESCRIPTOR_SET_BINDING_INDEX
    layoutBinding[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layoutBinding[0].descriptorCount = 1;
    layoutBinding[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    layoutBinding[0].pImmutableSamplers = nullptr;

    // If texture is used then there exists a 
    // second binding in the fragment shader
    if (useTexture) {
        layoutBinding[1].binding = 1;
        layoutBinding[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        layoutBinding[1].descriptorCount = 1;
        layoutBinding[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        layoutBinding[1].pImmutableSamplers = nullptr;
    }

    // Specify the layout bind into VkDescriptorSetLayoutCreateInfo
    // and use it to create the descriptor set layout
    VkDescriptorSetLayoutCreateInfo descriptorLayout = {};
    descriptorLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorLayout.pNext = nullptr;
    descriptorLayout.bindingCount = useTexture ? 2 : 1;
    descriptorLayout.pBindings = layoutBinding;

    VkResult result;

    // Allocate required number of descriptor set layout objects and
    // create them using vkCreateDescriptorSetLayout
    descLayout.resize(1);
    result = vkCreateDescriptorSetLayout(
        deviceObj->device,
        &descriptorLayout,
        nullptr,
        descLayout.data()
    );

    if (result != VK_SUCCESS) {
        return std::unexpected(VulkanError{ result, "Failed to create descriptor set layout!" });
    }
    return {};
}

// Creates the descriptor pool, this function depends on -
// CreateDescriptorSetLayout() to be called first.
VulkanStatus VulkanDrawable::CreateDescriptorPool(bool useTexture)
{
    VkResult result;

    // Define the size of the descriptor pool based on the 
    // type of descriptor set being used.
    const int NUMBER_OF_POOL_SIZES = 2;
    VkDescriptorPoolSize descriptorTypePool[NUMBER_OF_POOL_SIZES];

    // The first descriptor pool object is of type Uniform buffer
    descriptorTypePool[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorTypePool[0].descriptorCount = 1;

    // If texture is used then define the second object with
    // descriptor type to be imgage sampler
    if (useTexture) {
        descriptorTypePool[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorTypePool[1].descriptorCount = 1;
    }

    // Populate the descriptor pool state information
    // in the create info structure.
    VkDescriptorPoolCreateInfo descriptorPoolInfo = {};
    descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolInfo.pNext = nullptr;
    descriptorPoolInfo.maxSets = 1;
    descriptorPoolInfo.poolSizeCount = useTexture ? 2 : 1;
    descriptorPoolInfo.pPoolSizes = descriptorTypePool;
    descriptorPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    // Create the descriptor pool using the descriptor
    // pool create info structure
    result = vkCreateDescriptorPool(
        deviceObj->device,
        &descriptorPoolInfo,
        nullptr,
        &descriptorPool
    );
    if (result != VK_SUCCESS) {
        return std::unexpected(VulkanError{ result, "Failed to create descriptor pool!" });
    }
    return {};
}

VulkanStatus VulkanDrawable::CreateDescriptorResources()
{
    VK_PROPAGATE_ERROR(CreateUniformBuffer());
    return {};
}

VulkanStatus VulkanDrawable::CreateUniformBuffer()
{
    VkResult result;
    Projection = glm::perspective(
        glm::radians(45.f),
        (float)rendererObj->width / (float)rendererObj->height,
        0.1f,
        100.0f
    );
    View = glm::lookAt(
        glm::vec3(10, 3, 10),
        glm::vec3(0, 0, 0),
        glm::vec3(0, -1, 0)
    );
    Model = glm::mat4(1.0f);
    MVP = Projection * View * Model;

    // Create the buffer resource metadata
    VkBufferCreateInfo bufInfo = {};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.pNext = nullptr;
    bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufInfo.size = sizeof(MVP);
    bufInfo.queueFamilyIndexCount = 0;
    bufInfo.pQueueFamilyIndices = nullptr;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufInfo.flags = 0;

    // Create the buffer resource
    result = vkCreateBuffer(
        deviceObj->device,
        &bufInfo,
        nullptr,
        &UniformData.buf
    );

    if( result != VK_SUCCESS) {
        return std::unexpected(VulkanError{ result, "Failed to create uniform buffer!" });
    }

    // Get the memory requirements for this buffer resource
    VkMemoryRequirements memRqrmnt;
    vkGetBufferMemoryRequirements(
        deviceObj->device,
        UniformData.buf,
        &memRqrmnt
    );

    // Create memory allocation information
    VkMemoryAllocateInfo memAllocInfo = {};
    memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memAllocInfo.pNext = nullptr;
    memAllocInfo.memoryTypeIndex = 0;
    memAllocInfo.allocationSize = memRqrmnt.size;

    // Get the memory type index that has the properties we require
    auto memTypeIndex = deviceObj->MemoryTypeFromProperties(
        memRqrmnt.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT        
    );
    if (!memTypeIndex) {
        return std::unexpected(VulkanError{ VK_ERROR_INITIALIZATION_FAILED, "Failed to get memory type for uniform buffer!" });
    }
    memAllocInfo.memoryTypeIndex = memTypeIndex.value();

    // Allocate the physical backing for buffer resource
    result = vkAllocateMemory(
        deviceObj->device,
        &memAllocInfo,
        nullptr,
        &UniformData.mem
    );

    if (result != VK_SUCCESS) {
        return std::unexpected(VulkanError{ result, "Failed to allocate memory for uniform buffer!" });
    }

    // Map the physical device memory region to the host
    result = vkMapMemory(
        deviceObj->device,
        UniformData.mem,
        0,
        memRqrmnt.size,
        0,
        (void**)&UniformData.pData
    );
    if (result != VK_SUCCESS) {
        return std::unexpected(VulkanError{ result, "Failed to map uniform buffer memory!" });
    }

    // Copy MVP data to mapped memory
    memcpy(UniformData.pData, &MVP, sizeof(MVP));

    // We have only one uniform buffer, so only one range
    UniformData.mappedRange.resize(1);

    // Populate the VkMappedMemoryRange structure
    UniformData.mappedRange[0].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    UniformData.mappedRange[0].memory = UniformData.mem;
    UniformData.mappedRange[0].offset = 0;
    UniformData.mappedRange[0].size = sizeof(MVP);

    // Invalidate the mapped memory range to make it visible to the device
    result = vkInvalidateMappedMemoryRanges(
        deviceObj->device,
        1,
        &UniformData.mappedRange[0]
    );
    if (result != VK_SUCCESS) {
        return std::unexpected(VulkanError{ result, "Failed to invalidate mapped memory range for uniform buffer!" });
    }

    // Bind the allocated buffer resource to the device memory
    result = vkBindBufferMemory(
        deviceObj->device,
        UniformData.buf,
        UniformData.mem,
        0
    );
    if (result != VK_SUCCESS) {
        return std::unexpected(VulkanError{ result, "Failed to bind buffer memory for uniform buffer!" });
    }

    // Update the local data structure with uniform buffer for house keeping
    UniformData.bufInfo.buffer = UniformData.buf;
    UniformData.bufInfo.offset = 0;
    UniformData.bufInfo.range = sizeof(MVP);
    UniformData.memRqrmnt = memRqrmnt;

    return {};
}

// Create descriptor set - stub implementation
VulkanStatus VulkanDrawable::CreateDescriptorSet(bool useTexture)
{
    VulkanPipeline* pipelineObj = rendererObj->GetPipeline();

    // Create the descriptor allocation structure and specify 
    // the descriptor pool and layout to be used for allocation
    VkDescriptorSetAllocateInfo descAllocInfo[1];
    descAllocInfo[0].sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descAllocInfo[0].pNext = nullptr;
    descAllocInfo[0].descriptorPool = descriptorPool;
    descAllocInfo[0].descriptorSetCount = 1;
    descAllocInfo[0].pSetLayouts = descLayout.data();

    // Allocate the number of descriptor set needs to be created
    descriptorSet.resize(1);

    // Allocate the descriptor set(s)
    VK_CHECK(vkAllocateDescriptorSets(
        deviceObj->device, 
        descAllocInfo, 
        descriptorSet.data()
    ), "Failed to allocate descriptor sets");

    // Allocate two write descriptors for - 1. MVP and 2. Texture
    const int NUMBER_OF_WRITE_DESCRIPTORS = 2;
    VkWriteDescriptorSet writes[NUMBER_OF_WRITE_DESCRIPTORS]; 
    memset(&writes, 0, sizeof(writes));

    // Specify the uniform buffer related information
    // into first write descriptor
    writes[0] = {};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].pNext = nullptr;
    writes[0].dstSet = descriptorSet[0];
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &UniformData.bufInfo;
    writes[0].dstArrayElement = 0;
    writes[0].dstBinding = 0;

    // If texture is used then populate the second write descriptor
    if (useTexture) {
        writes[1] = {};
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].pNext = nullptr;
        writes[1].dstSet = descriptorSet[0];
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &textures->descsImageInfo;
        writes[1].dstArrayElement = 0;
    }

    // Update the descriptor set with new data
    vkUpdateDescriptorSets(
        deviceObj->device,
        useTexture ? 2 : 1,
        writes,
        0,
        nullptr
    );

    return {};
}
