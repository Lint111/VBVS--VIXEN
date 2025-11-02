#include "Nodes/DescriptorSetNode.h"
#include "Core/RenderGraph.h"
#include "VulkanResources/VulkanDevice.h"
#include "VulkanSwapChain.h"  // Phase 0.1: SwapChainPublicVariables definition
#include "Core/NodeLogging.h"
#include "CashSystem/MainCacher.h"
#include "CashSystem/DescriptorCacher.h"
#include "CashSystem/DescriptorSetLayoutCacher.h"  // Phase 5: Helper functions
#include <ShaderManagement/ShaderDataBundle.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstring> // for memcpy
#include <unordered_map>

// Phase 5: Type-safe UBO updates with generated SDI headers
#include "generated/sdi/Draw_ShaderNames.h"

namespace Vixen::RenderGraph {

// ===== NODE TYPE =====

DescriptorSetNodeType::DescriptorSetNodeType(const std::string& typeName)
    : TypedNodeType<DescriptorSetNodeConfig>(typeName)
{
    pipelineType = PipelineType::Graphics;
    requiredCapabilities = DeviceCapability::Graphics;
    supportsInstancing = true;
    maxInstances = 0;

    // Schema population now handled by TypedNodeType base class

    workloadMetrics.estimatedMemoryFootprint = 4096;
    workloadMetrics.estimatedComputeCost = 0.1f;
    workloadMetrics.estimatedBandwidthCost = 0.1f;
    workloadMetrics.canRunInParallel = true;
}

std::unique_ptr<NodeInstance> DescriptorSetNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<DescriptorSetNode>(
        instanceName,
        const_cast<DescriptorSetNodeType*>(this)
    );
}

// ===== NODE INSTANCE (MVP STUB) =====

DescriptorSetNode::DescriptorSetNode(
    const std::string& instanceName,
    NodeType* nodeType
)
    : TypedNode<DescriptorSetNodeConfig>(instanceName, nodeType)
{
    // MVP STUB: No descriptor set initialization
}

DescriptorSetNode::~DescriptorSetNode() {
    Cleanup();
}

void DescriptorSetNode::SetupImpl(Context& ctx) {
    NODE_LOG_DEBUG("Setup: DescriptorSetNode (MVP stub)");
    VulkanDevicePtr devicePtr = ctx.In(DescriptorSetNodeConfig::VULKAN_DEVICE_IN);
    if (devicePtr == nullptr) {
        throw std::runtime_error("DescriptorSetNode: VulkanDevice input is null");
    }

    // Set base class device member for cleanup tracking
    SetDevice(devicePtr);

    NODE_LOG_INFO("Setup: Descriptor set node ready (MVP stub - no descriptors)");
}

void DescriptorSetNode::CompileImpl(Context& ctx) {
    NODE_LOG_INFO("Compile: DescriptorSetNode (Phase 2: using reflection data from ShaderDataBundle)");

    // Phase 2: Read ShaderDataBundle from input
    auto shaderBundle = ctx.In(DescriptorSetNodeConfig::SHADER_DATA_BUNDLE);
    if (!shaderBundle) {
        throw std::runtime_error("DescriptorSetNode: ShaderDataBundle input is null");
    }

    std::cout << "[DescriptorSetNode::Compile] Received ShaderDataBundle: " << shaderBundle->GetProgramName() << std::endl;

    // Get MainCacher from owning graph
    auto& mainCacher = GetOwningGraph()->GetMainCacher();

    // Register DescriptorCacher (idempotent - safe to call multiple times)
    if (!mainCacher.IsRegistered(typeid(CashSystem::DescriptorWrapper))) {
        mainCacher.RegisterCacher<
            CashSystem::DescriptorCacher,
            CashSystem::DescriptorWrapper,
            CashSystem::DescriptorCreateParams
        >(
            typeid(CashSystem::DescriptorWrapper),
            "Descriptor",
            true  // device-dependent
        );
        NODE_LOG_DEBUG("DescriptorSetNode: Registered DescriptorCacher");
    }

    // Cache the cacher reference for use throughout node lifetime
    descriptorCacher = mainCacher.GetCacher<
        CashSystem::DescriptorCacher,
        CashSystem::DescriptorWrapper,
        CashSystem::DescriptorCreateParams
    >(typeid(CashSystem::DescriptorWrapper), device);

    bool useCache = false;
    if (descriptorCacher) {
        NODE_LOG_INFO("DescriptorSetNode: Descriptor cache ready");
        // TODO: Implement descriptor caching
        // auto cachedDescriptor = descriptorCacher->GetOrCreate(params);
        useCache = false;  // Not yet implemented
    }

    // Phase 2: Extract descriptor sets from reflection (we'll use set 0 for now)
    auto descriptorBindings = shaderBundle->GetDescriptorSet(0);

    if (descriptorBindings.empty()) {
        throw std::runtime_error("DescriptorSetNode: No descriptor bindings found in ShaderDataBundle set 0");
    }

    std::cout << "[DescriptorSetNode::Compile] Found " << descriptorBindings.size()
              << " descriptor bindings in set 0" << std::endl;

    if (!useCache) {
        // Phase 2: Generate descriptor set layout from shader reflection data
        // Convert SpirvDescriptorBinding to VkDescriptorSetLayoutBinding
        std::vector<VkDescriptorSetLayoutBinding> vkBindings;
        vkBindings.reserve(descriptorBindings.size());

        for (const auto& spirvBinding : descriptorBindings) {
            VkDescriptorSetLayoutBinding vkBinding{};
            vkBinding.binding = spirvBinding.binding;
            vkBinding.descriptorType = spirvBinding.descriptorType;
            vkBinding.descriptorCount = spirvBinding.descriptorCount;
            vkBinding.stageFlags = spirvBinding.stageFlags;
            vkBinding.pImmutableSamplers = nullptr;

            vkBindings.push_back(vkBinding);

            std::cout << "[DescriptorSetNode::Compile] Binding " << vkBinding.binding
                      << ": type=" << vkBinding.descriptorType
                      << ", count=" << vkBinding.descriptorCount
                      << ", stages=" << std::hex << vkBinding.stageFlags << std::dec
                      << ", name=" << spirvBinding.name << std::endl;
        }

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(vkBindings.size());
        layoutInfo.pBindings = vkBindings.data();

        VkResult result = vkCreateDescriptorSetLayout(
            device->device,
            &layoutInfo,
            nullptr,
            &descriptorSetLayout
        );

        if (result != VK_SUCCESS) {
            throw std::runtime_error("DescriptorSetNode: Failed to create descriptor set layout from reflection");
        }

        std::cout << "[DescriptorSetNode::Compile] Successfully created descriptor set layout from reflection" << std::endl;
    }

    // Continue with pool creation and descriptor allocation...

    NODE_LOG_INFO("Compile: Descriptor set layout created from reflection");
    std::cout << "[DescriptorSetNode::Compile] Created layout: " << descriptorSetLayout << std::endl;

    // Phase 0.4: Get swapchain image count for per-image resource allocation
    auto* swapchainPublic = ctx.In(DescriptorSetNodeConfig::SWAPCHAIN_PUBLIC);
    if (!swapchainPublic) {
        throw std::runtime_error("DescriptorSetNode: SWAPCHAIN_PUBLIC input is null");
    }

    uint32_t imageCount = swapchainPublic->swapChainImageCount;
    if (imageCount == 0) {
        throw std::runtime_error("DescriptorSetNode: swapChainImageCount is 0");
    }

    std::cout << "[DescriptorSetNode::Compile] Creating per-frame resources for " << imageCount << " swapchain images" << std::endl;

    // Phase 5: Use helper function to calculate descriptor pool sizes from reflection
    // Phase 0.4: Multiply maxSets by imageCount for per-image descriptor sets
    std::vector<VkDescriptorPoolSize> poolSizes = CashSystem::CalculateDescriptorPoolSizes(
        *shaderBundle,
        0,  // setIndex
        imageCount   // maxSets (per-image)
    );

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = imageCount; // Phase 0.4: Per-image descriptor sets

    VkResult result = vkCreateDescriptorPool(
        device->device,
        &poolInfo,
        nullptr,
        &descriptorPool
    );

    if (result != VK_SUCCESS) {
        throw std::runtime_error("DescriptorSetNode: Failed to create descriptor pool");
    }

    std::cout << "[DescriptorSetNode::Compile] Created descriptor pool: " << descriptorPool << std::endl;

    // Phase 0.4: Allocate per-swapchain-image descriptor sets to prevent invalidation
    // This avoids the validation error: "vkUpdateDescriptorSets invalidates command buffers"
    descriptorSets.resize(imageCount);

    std::vector<VkDescriptorSetLayout> layouts(imageCount, descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = imageCount;
    allocInfo.pSetLayouts = layouts.data();

    result = vkAllocateDescriptorSets(
        device->device,
        &allocInfo,
        descriptorSets.data()
    );

    if (result != VK_SUCCESS) {
        throw std::runtime_error("DescriptorSetNode: Failed to allocate descriptor sets");
    }

    std::cout << "[DescriptorSetNode::Compile] Allocated " << imageCount << " descriptor sets (per-image)" << std::endl;

    // Phase 0.4: Initialize per-frame resources (ring buffer pattern)
    perFrameResources.Initialize(device, imageCount);

    // Create UBO for each frame to prevent race conditions
    for (uint32_t i = 0; i < imageCount; i++) {
        perFrameResources.CreateUniformBuffer(i, sizeof(Draw_Shader::bufferVals));

        // Write initial MVP transformation using type-safe SDI struct
        glm::mat4 Projection = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
        glm::mat4 View = glm::lookAt(
            glm::vec3(10.0f, 3.0f, 10.0f),  // Camera in World Space
            glm::vec3(0.0f, 0.0f, 0.0f),     // Look at origin
            glm::vec3(0.0f, -1.0f, 0.0f)     // Up vector (Y-axis down)
        );
        glm::mat4 Model = glm::mat4(1.0f);

        Draw_Shader::bufferVals ubo;
        ubo.mvp = Projection * View * Model;

        void* mappedData = perFrameResources.GetUniformBufferMapped(i);
        memcpy(mappedData, &ubo, sizeof(Draw_Shader::bufferVals));
    }

    std::cout << "[DescriptorSetNode::Compile] Created " << imageCount << " per-frame UBOs" << std::endl;

    // Phase 0.4: Update each descriptor set with its corresponding per-frame UBO
    // This avoids the need to update descriptor sets in Execute(), preventing command buffer invalidation
    // Check if texture inputs are provided
    VkImageView textureView = ctx.In(DescriptorSetNodeConfig::TEXTURE_VIEW);
    VkSampler textureSampler = ctx.In(DescriptorSetNodeConfig::TEXTURE_SAMPLER);

    for (uint32_t i = 0; i < imageCount; i++) {
        // UBO descriptor (binding 0)
        VkDescriptorBufferInfo bufferDescInfo{};
        bufferDescInfo.buffer = perFrameResources.GetUniformBuffer(i);
        bufferDescInfo.offset = 0;
        bufferDescInfo.range = sizeof(Draw_Shader::bufferVals);

        VkWriteDescriptorSet bufferWrite{};
        bufferWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        bufferWrite.dstSet = descriptorSets[i];
        bufferWrite.dstBinding = 0;
        bufferWrite.dstArrayElement = 0;
        bufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bufferWrite.descriptorCount = 1;
        bufferWrite.pBufferInfo = &bufferDescInfo;

        if (textureView != VK_NULL_HANDLE && textureSampler != VK_NULL_HANDLE) {
            // Texture descriptor (binding 1)
            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = textureView;
            imageInfo.sampler = textureSampler;

            VkWriteDescriptorSet textureWrite{};
            textureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            textureWrite.dstSet = descriptorSets[i];
            textureWrite.dstBinding = 1;
            textureWrite.dstArrayElement = 0;
            textureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            textureWrite.descriptorCount = 1;
            textureWrite.pImageInfo = &imageInfo;

            VkWriteDescriptorSet writes[] = {bufferWrite, textureWrite};
            vkUpdateDescriptorSets(device->device, 2, writes, 0, nullptr);
        } else {
            // UBO only
            vkUpdateDescriptorSets(device->device, 1, &bufferWrite, 0, nullptr);
        }

        std::cout << "[DescriptorSetNode::Compile] Updated descriptor set " << i << " with UBO" << std::endl;
    }

    if (textureView != VK_NULL_HANDLE && textureSampler != VK_NULL_HANDLE) {
        std::cout << "[DescriptorSetNode::Compile] All descriptor sets updated with texture (view="
                  << textureView << ", sampler=" << textureSampler << ")" << std::endl;
    } else {
        std::cout << "[DescriptorSetNode::Compile] WARNING: No texture inputs provided!" << std::endl;
    }
    
    // Note: We're intentionally NOT storing dummyUBO/dummyUBOMemory for cleanup
    // This is a memory leak but acceptable for MVP testing
    // TODO: Properly manage dummy resources

    // Set outputs
    ctx.Out(DescriptorSetNodeConfig::DESCRIPTOR_SET_LAYOUT, descriptorSetLayout);
    ctx.Out(DescriptorSetNodeConfig::DESCRIPTOR_POOL, descriptorPool);
    ctx.Out(DescriptorSetNodeConfig::DESCRIPTOR_SETS, descriptorSets); 
    ctx.Out(DescriptorSetNodeConfig::VULKAN_DEVICE_OUT, device);

    std::cout << "[DescriptorSetNode::Compile] Outputs set successfully" << std::endl;
}

void DescriptorSetNode::ExecuteImpl(Context& ctx) {
    // Phase 0.4: Get current image index to select correct per-frame buffer
    uint32_t imageIndex = ctx.In(DescriptorSetNodeConfig::IMAGE_INDEX);

    if (!perFrameResources.IsInitialized()) {
        std::cout << "[DescriptorSetNode::Execute] WARNING: PerFrameResources not initialized!" << std::endl;
        return;
    }

    // Get delta time from graph's centralized time system
    RenderGraph* graph = GetOwningGraph();
    if (!graph) {
        std::cout << "[DescriptorSetNode::Execute] WARNING: No graph context!" << std::endl;
        return;
    }

    float deltaTime = graph->GetTime().GetDeltaTime();

    // Increment rotation angle (0.03 rad/sec for frame-rate independence)
    rotationAngle += 0.03f * deltaTime;

    // Recalculate MVP with rotation
    glm::mat4 Projection = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
    glm::mat4 View = glm::lookAt(
        glm::vec3(0.0f, 0.0f, 5.0f),     // Camera position
        glm::vec3(0.0f, 0.0f, 0.0f),     // Look at origin
        glm::vec3(0.0f, 1.0f, 0.0f)      // Up vector (Y-axis up)
    );
    glm::mat4 Model = glm::mat4(1.0f);
    Model = glm::rotate(Model, rotationAngle, glm::vec3(0.0f, 1.0f, 0.0f))      // Rotate around Y
          * glm::rotate(Model, rotationAngle, glm::vec3(1.0f, 1.0f, 1.0f));    // Rotate around diagonal

    // Phase 0.4: Update per-frame UBO (no descriptor set update needed)
    // Each image index has its own descriptor set pre-bound to its UBO
    Draw_Shader::bufferVals ubo;
    ubo.mvp = Projection * View * Model;

    void* mappedData = perFrameResources.GetUniformBufferMapped(imageIndex);
    if (!mappedData) {
        std::cout << "[DescriptorSetNode::Execute] WARNING: Frame " << imageIndex << " UBO not mapped!" << std::endl;
        return;
    }

    memcpy(mappedData, &ubo, sizeof(Draw_Shader::bufferVals));

    // Phase 0.4: NO DESCRIPTOR SET UPDATE - each image uses its own set
    // This avoids invalidating command buffers that reference the descriptor sets
}

void DescriptorSetNode::CleanupImpl() {
    NODE_LOG_DEBUG("Cleanup: DescriptorSetNode");

    // Phase 0.1: Cleanup per-frame resources (UBOs for all frames)
    if (perFrameResources.IsInitialized()) {
        perFrameResources.Cleanup();
        NODE_LOG_DEBUG("Cleanup: Per-frame resources cleaned up");
    }

    // Destroy descriptor pool (this also frees descriptor sets)
    if (descriptorPool != VK_NULL_HANDLE && device) {
        vkDestroyDescriptorPool(
            device->device,
            descriptorPool,
            nullptr
        );
        descriptorPool = VK_NULL_HANDLE;
        descriptorSets.clear();
        NODE_LOG_DEBUG("Cleanup: Descriptor pool destroyed");
    }

    // Destroy descriptor set layout
    if (descriptorSetLayout != VK_NULL_HANDLE && device) {
        vkDestroyDescriptorSetLayout(
            device->device,
            descriptorSetLayout,
            nullptr
        );
        descriptorSetLayout = VK_NULL_HANDLE;
        NODE_LOG_DEBUG("Cleanup: Descriptor set layout destroyed");
    }
}

// ===== API METHODS (MVP STUBS) =====

void DescriptorSetNode::UpdateDescriptorSet(
    uint32_t setIndex,
    const std::vector<DescriptorUpdate>& updates
) {
    // MVP STUB: Descriptor sets not implemented yet
    NODE_LOG_WARNING("UpdateDescriptorSet: MVP stub - not implemented");
}

void DescriptorSetNode::UpdateBinding(
    uint32_t setIndex,
    uint32_t binding,
    const VkDescriptorBufferInfo& bufferInfo
) {
    // MVP STUB: Descriptor sets not implemented yet
    NODE_LOG_WARNING("UpdateBinding (buffer): MVP stub - not implemented");
}

void DescriptorSetNode::UpdateBinding(
    uint32_t setIndex,
    uint32_t binding,
    const VkDescriptorImageInfo& imageInfo
) {
    // MVP STUB: Descriptor sets not implemented yet
    NODE_LOG_WARNING("UpdateBinding (image): MVP stub - not implemented");
}

// ===== PRIVATE HELPERS (MVP STUBS) =====

void DescriptorSetNode::ValidateLayoutSpec() {
    // MVP STUB: No validation in MVP
}

void DescriptorSetNode::CreateDescriptorSetLayout() {
    // MVP STUB: No descriptor layout in MVP
}

void DescriptorSetNode::CreateDescriptorPool() {
    // MVP STUB: No descriptor pool in MVP
}

void DescriptorSetNode::AllocateDescriptorSets() {
    // MVP STUB: No descriptor sets in MVP
}

void DescriptorSetNode::CreateDescriptorSetLayoutManually() {
    // This method is no longer used - inlined into Compile()
    // Keeping stub for API compatibility
}

} // namespace Vixen::RenderGraph
