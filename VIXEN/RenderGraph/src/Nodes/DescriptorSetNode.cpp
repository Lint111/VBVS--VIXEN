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
    // Phase G: SWAPCHAIN_PUBLIC is now optional (required for compute, optional for graphics)
    auto* swapchainPublic = ctx.In(DescriptorSetNodeConfig::SWAPCHAIN_PUBLIC);

    uint32_t imageCount = swapchainPublic ? swapchainPublic->swapChainImageCount : 0;
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

    // Phase 0.4: Update each descriptor set with its corresponding per-frame resources
    // This avoids the need to update descriptor sets in Execute(), preventing command buffer invalidation

    // Phase H: Check if DESCRIPTOR_RESOURCES input is provided (new data-driven approach)
    auto& descriptorResources = ctx.In(DescriptorSetNodeConfig::DESCRIPTOR_RESOURCES);
    bool useResourceArray = !descriptorResources.empty();

    if (useResourceArray) {
        std::cout << "[DescriptorSetNode::Compile] Using DESCRIPTOR_RESOURCES array (" << descriptorResources.size() << " resources)" << std::endl;
    } else {
        std::cout << "[DescriptorSetNode::Compile] Using legacy descriptor binding logic" << std::endl;
    }

    // Phase G: Determine what binding 0 is from shader reflection (data-driven approach)
    VkDescriptorType binding0Type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
    if (!descriptorBindings.empty()) {
        binding0Type = descriptorBindings[0].descriptorType;
    }

    // Get optional inputs (reuse swapchainPublic from line 156)
    VkImageView textureView = ctx.In(DescriptorSetNodeConfig::TEXTURE_VIEW);
    VkSampler textureSampler = ctx.In(DescriptorSetNodeConfig::TEXTURE_SAMPLER);

    // Phase H: Persistent storage for descriptor infos (must outlive vkUpdateDescriptorSets call)
    std::vector<std::vector<VkDescriptorImageInfo>> perFrameImageInfos(imageCount);
    std::vector<std::vector<VkDescriptorBufferInfo>> perFrameBufferInfos(imageCount);

    for (uint32_t i = 0; i < imageCount; i++) {
        std::vector<VkWriteDescriptorSet> writes;

        if (useResourceArray) {
            // Phase H: Data-driven descriptor binding from DESCRIPTOR_RESOURCES array
            perFrameImageInfos[i].reserve(descriptorBindings.size());
            perFrameBufferInfos[i].reserve(descriptorBindings.size());

            for (size_t bindingIdx = 0; bindingIdx < descriptorBindings.size() && bindingIdx < descriptorResources.size(); bindingIdx++) {
                const auto& binding = descriptorBindings[bindingIdx];
                const auto& resourceVariant = descriptorResources[bindingIdx];

                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = descriptorSets[i];
                write.dstBinding = binding.binding;
                write.dstArrayElement = 0;
                write.descriptorType = binding.descriptorType;
                write.descriptorCount = 1;

                // Generic variant inspection and descriptor creation
                switch (binding.descriptorType) {
                    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: {
                        if (auto* imageView = std::get_if<VkImageView>(&resourceVariant)) {
                            VkDescriptorImageInfo imageInfo{};
                            imageInfo.imageView = *imageView;
                            imageInfo.imageLayout = (binding.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                                ? VK_IMAGE_LAYOUT_GENERAL
                                : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                            perFrameImageInfos[i].push_back(imageInfo);
                            write.pImageInfo = &perFrameImageInfos[i].back();
                            writes.push_back(write);
                        }
                        break;
                    }

                    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: {
                        // For combined sampler, need VkImageView + VkSampler
                        // Assume gatherer provides VkImageView, use textureSampler from legacy slot
                        if (auto* imageView = std::get_if<VkImageView>(&resourceVariant)) {
                            VkDescriptorImageInfo imageInfo{};
                            imageInfo.imageView = *imageView;
                            imageInfo.sampler = textureSampler;  // From legacy TEXTURE_SAMPLER slot
                            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                            perFrameImageInfos[i].push_back(imageInfo);
                            write.pImageInfo = &perFrameImageInfos[i].back();
                            writes.push_back(write);
                        }
                        break;
                    }

                    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: {
                        if (auto* buffer = std::get_if<VkBuffer>(&resourceVariant)) {
                            VkDescriptorBufferInfo bufferInfo{};
                            bufferInfo.buffer = *buffer;
                            bufferInfo.offset = 0;
                            bufferInfo.range = VK_WHOLE_SIZE;
                            perFrameBufferInfos[i].push_back(bufferInfo);
                            write.pBufferInfo = &perFrameBufferInfos[i].back();
                            writes.push_back(write);
                        }
                        break;
                    }

                    default:
                        std::cout << "[DescriptorSetNode::Compile] WARNING: Unsupported descriptor type "
                                  << binding.descriptorType << " at binding " << binding.binding << std::endl;
                        break;
                }
            }

            std::cout << "[DescriptorSetNode::Compile] Bound " << writes.size()
                      << " descriptors for frame " << i << " (data-driven)" << std::endl;
        } else {
            // Legacy hardcoded logic
            // Update binding 0 based on what the shader reflection says it is
            if (binding0Type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
            // Compute shader: binding 0 = storage image (from swapchain)
            if (!swapchainPublic) {
                throw std::runtime_error("[DescriptorSetNode::Compile] Shader requires storage image at binding 0, but SWAPCHAIN_PUBLIC not provided");
            }

            VkDescriptorImageInfo storageImageInfo{};
            storageImageInfo.imageView = swapchainPublic->colorBuffers[i].view;
            storageImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkWriteDescriptorSet storageWrite{};
            storageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            storageWrite.dstSet = descriptorSets[i];
            storageWrite.dstBinding = 0;
            storageWrite.dstArrayElement = 0;
            storageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            storageWrite.descriptorCount = 1;
            storageWrite.pImageInfo = &storageImageInfo;
            writes.push_back(storageWrite);

            std::cout << "[DescriptorSetNode::Compile] Updated descriptor set " << i << " with storage image (from shader reflection)" << std::endl;
        } else if (binding0Type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            // Graphics shader: binding 0 = UBO
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
            writes.push_back(bufferWrite);

            // Texture descriptor (binding 1) - optional for graphics shaders
            if (textureView != VK_NULL_HANDLE && textureSampler != VK_NULL_HANDLE) {
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
                writes.push_back(textureWrite);
            }

            std::cout << "[DescriptorSetNode::Compile] Updated descriptor set " << i << " with UBO (from shader reflection)" << std::endl;
            }
        }  // End of legacy logic

        vkUpdateDescriptorSets(device->device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    if (binding0Type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
        std::cout << "[DescriptorSetNode::Compile] All descriptor sets updated with swapchain storage images (shader-driven)" << std::endl;
    } else if (binding0Type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
        std::cout << "[DescriptorSetNode::Compile] All descriptor sets updated with UBOs (shader-driven)" << std::endl;
        if (textureView != VK_NULL_HANDLE && textureSampler != VK_NULL_HANDLE) {
            std::cout << "[DescriptorSetNode::Compile] Texture also bound at binding 1" << std::endl;
        }
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
