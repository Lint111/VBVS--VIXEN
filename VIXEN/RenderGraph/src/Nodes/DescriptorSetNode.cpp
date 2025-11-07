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

void DescriptorSetNode::SetupImpl(TypedNode<DescriptorSetNodeConfig>::TypedSetupContext& ctx) {
    // Graph-scope initialization only (no input access)
    NODE_LOG_DEBUG("DescriptorSetNode: Setup (graph-scope initialization)");
}

void DescriptorSetNode::CompileImpl(TypedNode<DescriptorSetNodeConfig>::TypedCompileContext& ctx) {
    NODE_LOG_INFO("Compile: DescriptorSetNode (Phase 2: using reflection data from ShaderDataBundle)");

    // Access device input (compile-time dependency)
    VulkanDevicePtr devicePtr = ctx.In(DescriptorSetNodeConfig::VULKAN_DEVICE_IN);
    if (devicePtr == nullptr) {
        throw std::runtime_error("DescriptorSetNode: VulkanDevice input is null");
    }

    // Set base class device member for cleanup tracking
    SetDevice(devicePtr);

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
    // Phase H: SWAPCHAIN_IMAGE_COUNT directly provides the extracted imageCount value
    uint32_t imageCount = ctx.In(DescriptorSetNodeConfig::SWAPCHAIN_IMAGE_COUNT);

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

    // Phase H: Data-driven descriptor binding from DESCRIPTOR_RESOURCES array
    auto descriptorResources = ctx.In(DescriptorSetNodeConfig::DESCRIPTOR_RESOURCES);
    std::cout << "[DescriptorSetNode::Compile] Using DESCRIPTOR_RESOURCES array (" << descriptorResources.size() << " resources)" << std::endl;

    // Phase H: Initialize persistent descriptor info storage (node scope for lifetime)
    perFrameImageInfos.resize(imageCount);
    perFrameBufferInfos.resize(imageCount);

    for (uint32_t i = 0; i < imageCount; i++) {
        // Phase H: Data-driven descriptor binding from DESCRIPTOR_RESOURCES array
        perFrameImageInfos[i].reserve(descriptorBindings.size());
        perFrameBufferInfos[i].reserve(descriptorBindings.size());

        // Use helper method to build descriptor writes
        auto writes = BuildDescriptorWrites(i, descriptorResources, descriptorBindings,
                                           perFrameImageInfos[i], perFrameBufferInfos[i]);

        std::cout << "[DescriptorSetNode::Compile] Bound " << writes.size()
                  << " descriptors for frame " << i << " (data-driven)" << std::endl;

        if (!writes.empty()) {
            vkUpdateDescriptorSets(device->device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }

    std::cout << "[DescriptorSetNode::Compile] All descriptor sets updated (data-driven)" << std::endl;

    // Set outputs
    ctx.Out(DescriptorSetNodeConfig::DESCRIPTOR_SET_LAYOUT, descriptorSetLayout);
    ctx.Out(DescriptorSetNodeConfig::DESCRIPTOR_POOL, descriptorPool);
    ctx.Out(DescriptorSetNodeConfig::DESCRIPTOR_SETS, descriptorSets); 
    ctx.Out(DescriptorSetNodeConfig::VULKAN_DEVICE_OUT, device);

    std::cout << "[DescriptorSetNode::Compile] Outputs set successfully" << std::endl;
}

void DescriptorSetNode::ExecuteImpl(TypedNode<DescriptorSetNodeConfig>::TypedExecuteContext& ctx) {
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

    // Update descriptor sets for transient resources in Execute phase
    // Read fresh shader bundle and resource array
    auto shaderBundle = ctx.In(DescriptorSetNodeConfig::SHADER_DATA_BUNDLE);
    auto descriptorResources = ctx.In(DescriptorSetNodeConfig::DESCRIPTOR_RESOURCES);

    if (!shaderBundle || descriptorResources.empty()) {
        return;  // No transient updates needed
    }

    // Get descriptor bindings from shader reflection
    auto descriptorBindings = shaderBundle->GetDescriptorSet(0);
    if (descriptorBindings.empty()) {
        return;
    }

    // Reuse BuildDescriptorWrites helper for transient descriptor updates
    // Use temporary storage for info structures (they must outlive vkUpdateDescriptorSets call)
    std::vector<VkDescriptorImageInfo> transientImageInfos;
    std::vector<VkDescriptorBufferInfo> transientBufferInfos;

    auto writes = BuildDescriptorWrites(imageIndex, descriptorResources, descriptorBindings,
                                       transientImageInfos, transientBufferInfos);

    if (!writes.empty()) {
        vkUpdateDescriptorSets(GetDevice()->device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        std::cout << "[DescriptorSetNode::Execute] Updated " << writes.size()
                  << " transient descriptor(s) for frame " << imageIndex << std::endl;
    }
}

std::vector<VkWriteDescriptorSet> DescriptorSetNode::BuildDescriptorWrites(
    uint32_t imageIndex,
    const std::vector<ResourceVariant>& descriptorResources,
    const std::vector<ShaderManagement::SpirvDescriptorBinding>& descriptorBindings,
    std::vector<VkDescriptorImageInfo>& imageInfos,
    std::vector<VkDescriptorBufferInfo>& bufferInfos
) {
    std::vector<VkWriteDescriptorSet> writes;
    writes.reserve(descriptorBindings.size());

    // CRITICAL: Reserve space to prevent reallocation during iteration
    // When vectors reallocate, all pointers (write.pImageInfo, write.pBufferInfo) become invalid
    // Reserve enough space for worst-case: all bindings could be image descriptors
    imageInfos.reserve(imageInfos.size() + descriptorBindings.size());
    bufferInfos.reserve(bufferInfos.size() + descriptorBindings.size());

    // Helper to find sampler resource by searching all resources
    // This handles the case where ImageView and Sampler both connect to the same binding
    // but are stored in different slots in the resource array
    auto findSamplerResource = [&](uint32_t targetBinding) -> VkSampler {
        // First check the binding index itself
        if (targetBinding < descriptorResources.size()) {
            const auto& resource = descriptorResources[targetBinding];
            if (std::holds_alternative<VkSampler>(resource)) {
                return std::get<VkSampler>(resource);
            }
        }

        // Search all resources for a sampler (for combined image samplers)
        // The gatherer may have placed it elsewhere due to overwriting
        for (size_t i = 0; i < descriptorResources.size(); ++i) {
            const auto& resource = descriptorResources[i];
            if (std::holds_alternative<VkSampler>(resource)) {
                // Found a sampler - assume it's for this combined sampler binding
                // TODO: Better tracking of which sampler belongs to which binding
                return std::get<VkSampler>(resource);
            }
        }

        return VK_NULL_HANDLE;
    };

    for (size_t bindingIdx = 0; bindingIdx < descriptorBindings.size(); bindingIdx++) {
        const auto& binding = descriptorBindings[bindingIdx];

        // Use binding.binding (shader binding number) to index into resources, not loop index
        if (binding.binding >= descriptorResources.size()) {
            std::cout << "[DescriptorSetNode::BuildDescriptorWrites] WARNING: "
                      << "Binding " << binding.binding << " (" << binding.name << ") "
                      << "exceeds resource array size " << descriptorResources.size() << std::endl;
            continue;
        }

        const auto& resourceVariant = descriptorResources[binding.binding];

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptorSets[imageIndex];
        write.dstBinding = binding.binding;
        write.dstArrayElement = 0;
        write.descriptorType = binding.descriptorType;
        write.descriptorCount = 1;

        // Generic variant inspection based on descriptor type
        switch (binding.descriptorType) {
            case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: {
                // Storage images don't need samplers
                // Check for SwapChainPublicInfo (per-frame image views)
                if (auto* swapchainPtr = std::get_if<SwapChainPublicVariables*>(&resourceVariant)) {
                    SwapChainPublicVariables* sc = *swapchainPtr;
                    if (sc && imageIndex < sc->colorBuffers.size()) {
                        VkDescriptorImageInfo imageInfo{};
                        imageInfo.imageView = sc->colorBuffers[imageIndex].view;
                        imageInfo.sampler = VK_NULL_HANDLE;
                        imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                        imageInfos.push_back(imageInfo);
                        write.pImageInfo = &imageInfos.back();
                        writes.push_back(write);
                    }
                }
                // Direct VkImageView (single image, same for all frames)
                else if (std::holds_alternative<VkImageView>(resourceVariant)) {
                    VkImageView imageView = std::get<VkImageView>(resourceVariant);
                    VkDescriptorImageInfo imageInfo{};
                    imageInfo.imageView = imageView;
                    imageInfo.sampler = VK_NULL_HANDLE;
                    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                    imageInfos.push_back(imageInfo);
                    write.pImageInfo = &imageInfos.back();
                    writes.push_back(write);
                }
                break;
            }

            case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: {
                // Sampled images are used with separate samplers
                // Samplers should be in the same descriptor set (validated by ShaderDataBundle)
                VkImageView imageView = VK_NULL_HANDLE;

                // Check for SwapChainPublicInfo (per-frame image views)
                if (auto* swapchainPtr = std::get_if<SwapChainPublicVariables*>(&resourceVariant)) {
                    SwapChainPublicVariables* sc = *swapchainPtr;
                    if (sc && imageIndex < sc->colorBuffers.size()) {
                        imageView = sc->colorBuffers[imageIndex].view;
                    }
                }
                // Direct VkImageView (single image, same for all frames)
                else if (std::holds_alternative<VkImageView>(resourceVariant)) {
                    imageView = std::get<VkImageView>(resourceVariant);
                }

                if (imageView != VK_NULL_HANDLE) {
                    VkDescriptorImageInfo imageInfo{};
                    imageInfo.imageView = imageView;
                    imageInfo.sampler = VK_NULL_HANDLE;  // Sampled images don't include sampler
                    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    imageInfos.push_back(imageInfo);
                    write.pImageInfo = &imageInfos.back();
                    writes.push_back(write);

                    std::cout << "[DescriptorSetNode::BuildDescriptorWrites] Bound SAMPLED_IMAGE '"
                              << binding.name << "' at binding " << binding.binding
                              << " (sampler should be separate)" << std::endl;
                }
                break;
            }

            case VK_DESCRIPTOR_TYPE_SAMPLER: {
                // Standalone sampler descriptor (no image)
                if (std::holds_alternative<VkSampler>(resourceVariant)) {
                    VkSampler sampler = std::get<VkSampler>(resourceVariant);
                    VkDescriptorImageInfo imageInfo{};
                    imageInfo.sampler = sampler;
                    imageInfo.imageView = VK_NULL_HANDLE;
                    imageInfo.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    imageInfos.push_back(imageInfo);
                    write.pImageInfo = &imageInfos.back();
                    writes.push_back(write);
                }
                // Handle vector of samplers (for array bindings)
                else if (std::holds_alternative<std::vector<VkSampler>>(resourceVariant)) {
                    const auto& samplerArray = std::get<std::vector<VkSampler>>(resourceVariant);
                    if (!samplerArray.empty()) {
                        // For array bindings, create one write with multiple samplers
                        size_t startIdx = imageInfos.size();
                        for (const auto& sampler : samplerArray) {
                            VkDescriptorImageInfo imageInfo{};
                            imageInfo.sampler = sampler;
                            imageInfo.imageView = VK_NULL_HANDLE;
                            imageInfo.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                            imageInfos.push_back(imageInfo);
                        }
                        write.pImageInfo = &imageInfos[startIdx];
                        write.descriptorCount = static_cast<uint32_t>(samplerArray.size());
                        writes.push_back(write);
                    }
                }
                break;
            }

            case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: {
                // Handle combined image sampler (image + sampler)
                // Check for ImageSamplerPair first (new type-safe approach)
                std::cout << "[DescriptorSetNode::BuildDescriptorWrites] COMBINED_IMAGE_SAMPLER binding "
                          << binding.binding << " (" << binding.name << "), variant index=" << resourceVariant.index() << std::endl;

                if (std::holds_alternative<ImageSamplerPair>(resourceVariant)) {
                    const ImageSamplerPair& pair = std::get<ImageSamplerPair>(resourceVariant);

                    std::cout << "[DescriptorSetNode::BuildDescriptorWrites] Extracted ImageSamplerPair: "
                              << "imageView=" << pair.imageView << ", sampler=" << pair.sampler << std::endl;

                    VkDescriptorImageInfo imageInfo{};
                    imageInfo.imageView = pair.imageView;
                    imageInfo.sampler = pair.sampler;
                    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    imageInfos.push_back(imageInfo);
                    write.pImageInfo = &imageInfos.back();
                    writes.push_back(write);

                    std::cout << "[DescriptorSetNode::BuildDescriptorWrites] Bound COMBINED_IMAGE_SAMPLER '"
                              << binding.name << "' at binding " << binding.binding
                              << " (imageView=" << pair.imageView << ", sampler=" << pair.sampler << ")" << std::endl;
                }
                // Fallback: Legacy approach with separate ImageView and Sampler resources
                else if (std::holds_alternative<VkImageView>(resourceVariant)) {
                    VkImageView imageView = std::get<VkImageView>(resourceVariant);

                    // Find paired sampler resource using helper
                    VkSampler sampler = findSamplerResource(binding.binding);

                    VkDescriptorImageInfo imageInfo{};
                    imageInfo.imageView = imageView;
                    imageInfo.sampler = sampler;
                    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    imageInfos.push_back(imageInfo);
                    write.pImageInfo = &imageInfos.back();
                    writes.push_back(write);

                    std::cout << "[DescriptorSetNode::BuildDescriptorWrites] Bound COMBINED_IMAGE_SAMPLER '"
                              << binding.name << "' at binding " << binding.binding
                              << " (imageView=" << imageView << ", sampler=" << sampler << ") [legacy]" << std::endl;

                    if (sampler == VK_NULL_HANDLE) {
                        std::cout << "[DescriptorSetNode::BuildDescriptorWrites] WARNING: "
                                  << "Combined image sampler at binding " << binding.binding
                                  << " has no sampler (VK_NULL_HANDLE)" << std::endl;
                    }
                }
                // Handle array of combined image samplers
                // Expect vector<VkImageView> followed by vector<VkSampler>
                else if (std::holds_alternative<std::vector<VkImageView>>(resourceVariant)) {
                    const auto& imageViewArray = std::get<std::vector<VkImageView>>(resourceVariant);
                    std::vector<VkSampler> samplerArray;

                    // Try to get sampler array from next slot
                    if (bindingIdx + 1 < descriptorResources.size()) {
                        const auto& nextResource = descriptorResources[bindingIdx + 1];
                        if (std::holds_alternative<std::vector<VkSampler>>(nextResource)) {
                            samplerArray = std::get<std::vector<VkSampler>>(nextResource);
                        }
                    }

                    if (!imageViewArray.empty()) {
                        size_t startIdx = imageInfos.size();
                        for (size_t i = 0; i < imageViewArray.size(); i++) {
                            VkDescriptorImageInfo imageInfo{};
                            imageInfo.imageView = imageViewArray[i];
                            imageInfo.sampler = (i < samplerArray.size()) ? samplerArray[i] : VK_NULL_HANDLE;
                            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                            imageInfos.push_back(imageInfo);
                        }
                        write.pImageInfo = &imageInfos[startIdx];
                        write.descriptorCount = static_cast<uint32_t>(imageViewArray.size());
                        writes.push_back(write);
                    }
                }
                break;
            }

            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: {
                if (std::holds_alternative<VkBuffer>(resourceVariant)) {
                    VkBuffer buffer = std::get<VkBuffer>(resourceVariant);
                    VkDescriptorBufferInfo bufferInfo{};
                    bufferInfo.buffer = buffer;
                    bufferInfo.offset = 0;
                    bufferInfo.range = VK_WHOLE_SIZE;
                    bufferInfos.push_back(bufferInfo);
                    write.pBufferInfo = &bufferInfos.back();
                    writes.push_back(write);
                }
                break;
            }

            default:
                // Unsupported or constant descriptor type - skip
                break;
        }
    }

    return writes;
}

void DescriptorSetNode::CleanupImpl(TypedNode<DescriptorSetNodeConfig>::TypedCleanupContext& ctx) {
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
