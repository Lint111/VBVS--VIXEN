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

// Event subscription
#include "EventTypes/RenderGraphEvents.h"

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

void DescriptorSetNode::SetupImpl(TypedSetupContext& ctx) {
    // Graph-scope initialization only (no input access)
    NODE_LOG_DEBUG("DescriptorSetNode: Setup (graph-scope initialization)");

    // Subscribe to RenderPauseEvent to track swapchain recreation
    // Use NodeInstance::SubscribeToMessage for automatic cleanup (avoids subscription leaks)
    if (GetMessageBus()) {
        SubscribeToMessage(EventTypes::RenderPauseEvent::TYPE,
            [this](const EventBus::BaseEventMessage& msg) {
                const auto& event = static_cast<const EventTypes::RenderPauseEvent&>(msg);
                if (event.pauseReason == EventTypes::RenderPauseEvent::Reason::SwapChainRecreation) {
                    if (event.pauseAction == EventTypes::RenderPauseEvent::Action::PAUSE_START) {
                        SetFlag(NodeFlags::Paused);
                        NODE_LOG_DEBUG("DescriptorSetNode: Rendering paused (swapchain recreation)");
                    } else {
                        ClearFlag(NodeFlags::Paused);
                        NODE_LOG_DEBUG("DescriptorSetNode: Rendering resumed");
                    }
                }
                return false;  // Don't consume message
            }
        );
    }
}

void DescriptorSetNode::SetupDeviceAndShaderBundle(TypedCompileContext& ctx, std::shared_ptr<ShaderManagement::ShaderDataBundle>& outShaderBundle) {
    // Access device input (compile-time dependency)
    VulkanDevicePtr devicePtr = ctx.In(DescriptorSetNodeConfig::VULKAN_DEVICE_IN);
    if (devicePtr == nullptr) {
        throw std::runtime_error("DescriptorSetNode: VulkanDevice input is null");
    }

    // Set base class device member for cleanup tracking
    SetDevice(devicePtr);

    // Phase 2: Read ShaderDataBundle from input
    outShaderBundle = ctx.In(DescriptorSetNodeConfig::SHADER_DATA_BUNDLE);
    if (!outShaderBundle) {
        throw std::runtime_error("DescriptorSetNode: ShaderDataBundle input is null");
    }

    NODE_LOG_INFO("[DescriptorSetNode::Compile] Received ShaderDataBundle: " + outShaderBundle->GetProgramName());
}

void DescriptorSetNode::RegisterDescriptorCacher() {
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

    if (descriptorCacher) {
        NODE_LOG_INFO("DescriptorSetNode: Descriptor cache ready");
        // TODO: Implement descriptor caching
    }
}

void DescriptorSetNode::CreateDescriptorSetLayout(const std::vector<ShaderManagement::SpirvDescriptorBinding>& descriptorBindings) {
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

        NODE_LOG_DEBUG("[DescriptorSetNode::CreateLayout] Binding " + std::to_string(vkBinding.binding) +
                      ": type=" + std::to_string(vkBinding.descriptorType) +
                      ", count=" + std::to_string(vkBinding.descriptorCount) +
                      ", stages=0x" + std::to_string(vkBinding.stageFlags) +
                      ", name=" + spirvBinding.name);
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

    NODE_LOG_INFO("[DescriptorSetNode::CreateLayout] Created descriptor set layout from reflection");
    NODE_LOG_DEBUG("[DescriptorSetNode::CreateLayout] Layout handle: " + std::to_string(reinterpret_cast<uint64_t>(descriptorSetLayout)));
}

void DescriptorSetNode::CreateDescriptorPool(const ShaderManagement::ShaderDataBundle& shaderBundle, uint32_t imageCount) {
    // Phase 5: Use helper function to calculate descriptor pool sizes from reflection
    // Phase 0.4: Multiply maxSets by imageCount for per-image descriptor sets
    std::vector<VkDescriptorPoolSize> poolSizes = CashSystem::CalculateDescriptorPoolSizes(
        shaderBundle,
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

    NODE_LOG_DEBUG("[DescriptorSetNode::CreatePool] Created descriptor pool: " + std::to_string(reinterpret_cast<uint64_t>(descriptorPool)));
}

void DescriptorSetNode::AllocateDescriptorSets(uint32_t imageCount) {
    // Phase 0.4: Allocate per-swapchain-image descriptor sets to prevent invalidation
    // This avoids the validation error: "vkUpdateDescriptorSets invalidates command buffers"
    descriptorSets.resize(imageCount);

    std::vector<VkDescriptorSetLayout> layouts(imageCount, descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = imageCount;
    allocInfo.pSetLayouts = layouts.data();

    VkResult result = vkAllocateDescriptorSets(
        device->device,
        &allocInfo,
        descriptorSets.data()
    );

    if (result != VK_SUCCESS) {
        throw std::runtime_error("DescriptorSetNode: Failed to allocate descriptor sets");
    }

    NODE_LOG_INFO("[DescriptorSetNode::AllocateSets] Allocated " + std::to_string(imageCount) + " descriptor sets (per-image)");
}

void DescriptorSetNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("Compile: DescriptorSetNode (Phase 2: using reflection data from ShaderDataBundle)");

    // Setup device and get shader bundle
    std::shared_ptr<ShaderManagement::ShaderDataBundle> shaderBundle;
    SetupDeviceAndShaderBundle(ctx, shaderBundle);

    // Register descriptor cacher
    RegisterDescriptorCacher();

    // Phase 2: Extract descriptor sets from reflection (we'll use set 0 for now)
    auto descriptorBindings = shaderBundle->GetDescriptorSet(0);

    if (descriptorBindings.empty()) {
        throw std::runtime_error("DescriptorSetNode: No descriptor bindings found in ShaderDataBundle set 0");
    }

    NODE_LOG_INFO("[DescriptorSetNode::Compile] Found " + std::to_string(descriptorBindings.size()) + " descriptor bindings in set 0");

    // Create descriptor set layout from shader reflection
    CreateDescriptorSetLayout(descriptorBindings);

    // Get swapchain image count for per-image resource allocation
    uint32_t imageCount = ctx.In(DescriptorSetNodeConfig::SWAPCHAIN_IMAGE_COUNT);

    if (imageCount == 0) {
        throw std::runtime_error("DescriptorSetNode: swapChainImageCount is 0");
    }

    NODE_LOG_INFO("[DescriptorSetNode::Compile] Creating per-frame resources for " + std::to_string(imageCount) + " swapchain images");

    // Create descriptor pool
    CreateDescriptorPool(*shaderBundle, imageCount);

    // Allocate descriptor sets
    AllocateDescriptorSets(imageCount);

    // Phase H: Bind Dependency (static) descriptors in Compile
    // Execute (transient) descriptors bound per-frame in Execute
    auto descriptorResources = ctx.In(DescriptorSetNodeConfig::DESCRIPTOR_RESOURCES);
    auto slotRoles = ctx.In(DescriptorSetNodeConfig::DESCRIPTOR_SLOT_ROLES);

    // Debug: Log received slot roles
    NODE_LOG_DEBUG("[DescriptorSetNode::Compile] Received " + std::to_string(slotRoles.size()) + " slot roles:");
    for (size_t i = 0; i < slotRoles.size(); ++i) {
        uint8_t roleVal = static_cast<uint8_t>(slotRoles[i]);
        NODE_LOG_DEBUG("  Binding " + std::to_string(i) + ": role=" + std::to_string(roleVal) +
                      " (Dependency=" + std::to_string(roleVal & static_cast<uint8_t>(SlotRole::Dependency)) +
                      ", Execute=" + std::to_string(roleVal & static_cast<uint8_t>(SlotRole::Execute)) + ")");
    }

    // Initialize persistent descriptor info storage (node scope for lifetime)
    perFrameImageInfos.resize(imageCount);
    perFrameBufferInfos.resize(imageCount);

    // Defer ALL descriptor binding to Execute phase
    // PostCompile hooks populate resources AFTER CompileImpl completes,
    // so resources aren't available yet during Compile phase
    // First Execute will bind both Dependency and Execute descriptors together
    NODE_LOG_DEBUG("[DescriptorSetNode::Compile] Deferring ALL descriptor binding to Execute phase (resources populated by PostCompile hooks)");

    // Mark that we need to bind Dependency descriptors on first Execute
    SetFlag(NodeFlags::NeedsInitialBind);

    NODE_LOG_INFO("[DescriptorSetNode::Compile] Descriptor sets created (binding deferred to Execute)");

    // Set outputs
    ctx.Out(DescriptorSetNodeConfig::DESCRIPTOR_SET_LAYOUT, descriptorSetLayout);
    ctx.Out(DescriptorSetNodeConfig::DESCRIPTOR_POOL, descriptorPool);
    ctx.Out(DescriptorSetNodeConfig::DESCRIPTOR_SETS, descriptorSets); 
    ctx.Out(DescriptorSetNodeConfig::VULKAN_DEVICE_OUT, device);

    NODE_LOG_DEBUG("[DescriptorSetNode::Compile] Outputs set successfully");
}

void DescriptorSetNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Skip execution during swapchain recreation to prevent descriptor access violations
    if (IsPaused()) {
        NODE_LOG_DEBUG("[DescriptorSetNode::Execute] Skipping frame (rendering paused)");
        return;
    }

    // Get current image index to select correct per-frame descriptor set
    uint32_t imageIndex = ctx.In(DescriptorSetNodeConfig::IMAGE_INDEX);

    // Get descriptor inputs
    auto shaderBundle = ctx.In(DescriptorSetNodeConfig::SHADER_DATA_BUNDLE);
    auto descriptorResources = ctx.In(DescriptorSetNodeConfig::DESCRIPTOR_RESOURCES);
    auto slotRoles = ctx.In(DescriptorSetNodeConfig::DESCRIPTOR_SLOT_ROLES);

    if (!shaderBundle || descriptorResources.empty()) {
        return;
    }

    auto descriptorBindings = shaderBundle->GetDescriptorSet(0);
    if (descriptorBindings.empty()) {
        return;
    }

    // On first execute after Compile, bind all Dependency descriptors for all frames
    if (HasFlag(NodeFlags::NeedsInitialBind)) {
        NODE_LOG_DEBUG("[DescriptorSetNode::Execute] First execute - binding Dependency descriptors for all frames");
        for (uint32_t i = 0; i < static_cast<uint32_t>(descriptorSets.size()); i++) {
            auto dependencyWrites = BuildDescriptorWrites(i, descriptorResources, descriptorBindings,
                                                         perFrameImageInfos[i], perFrameBufferInfos[i],
                                                         slotRoles, SlotRole::Dependency);
            if (!dependencyWrites.empty()) {
                vkUpdateDescriptorSets(GetDevice()->device, static_cast<uint32_t>(dependencyWrites.size()), dependencyWrites.data(), 0, nullptr);
                NODE_LOG_DEBUG("[DescriptorSetNode::Execute] Bound " + std::to_string(dependencyWrites.size()) +
                              " Dependency descriptor(s) for frame " + std::to_string(i));
            }
        }
        ClearFlag(NodeFlags::NeedsInitialBind);
    }

    // Build writes for Execute (transient) bindings for current frame
    auto writes = BuildDescriptorWrites(imageIndex, descriptorResources, descriptorBindings,
                                       perFrameImageInfos[imageIndex], perFrameBufferInfos[imageIndex],
                                       slotRoles, SlotRole::Execute);

    if (!writes.empty()) {
        vkUpdateDescriptorSets(GetDevice()->device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        NODE_LOG_DEBUG("[DescriptorSetNode::Execute] Updated " + std::to_string(writes.size()) +
                      " Execute descriptor(s) for frame " + std::to_string(imageIndex));
    }
}

std::vector<VkWriteDescriptorSet> DescriptorSetNode::BuildDescriptorWrites(
    uint32_t imageIndex,
    const std::vector<ResourceVariant>& descriptorResources,
    const std::vector<ShaderManagement::SpirvDescriptorBinding>& descriptorBindings,
    std::vector<VkDescriptorImageInfo>& imageInfos,
    std::vector<VkDescriptorBufferInfo>& bufferInfos,
    const std::vector<SlotRole>& slotRoles,
    SlotRole roleFilter
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
            NODE_LOG_DEBUG("[DescriptorSetNode::BuildDescriptorWrites] WARNING: Binding " +
                          std::to_string(binding.binding) + " (" + binding.name + ") exceeds resource array size " +
                          std::to_string(descriptorResources.size()));
            continue;
        }

        // Filter by slot role if provided
        // Support combined roles (e.g., Dependency | Execute)
        // A binding matches the filter if it has ANY of the filter's flags set
        if (!slotRoles.empty() && binding.binding < slotRoles.size()) {
            SlotRole bindingRole = slotRoles[binding.binding];
            uint8_t bindingFlags = static_cast<uint8_t>(bindingRole);
            uint8_t filterFlags = static_cast<uint8_t>(roleFilter);

            // Check if binding has any of the filter flags
            // For Dependency filter (1): matches roles 1 (Dependency) or 3 (Dependency|Execute)
            // For Execute filter (2): matches roles 2 (Execute) or 3 (Dependency|Execute)
            bool matchesFilter = (bindingFlags & filterFlags) != 0;

            NODE_LOG_DEBUG("[BuildDescriptorWrites] Binding " + std::to_string(binding.binding) +
                          " (" + binding.name + "): role=" + std::to_string(bindingFlags) +
                          ", filter=" + std::to_string(filterFlags) +
                          ", matches=" + (matchesFilter ? "YES" : "NO"));

            if (!matchesFilter) {
                continue;  // Skip this binding - doesn't match filter
            }
        }

        const auto& resourceVariant = descriptorResources[binding.binding];

        NODE_LOG_DEBUG("[BuildDescriptorWrites] Binding " + std::to_string(binding.binding) +
                      " (" + binding.name + ") resource type: " +
                      (std::holds_alternative<std::monostate>(resourceVariant) ? "monostate" :
                       std::holds_alternative<VkImageView>(resourceVariant) ? "VkImageView" :
                       std::holds_alternative<VkBuffer>(resourceVariant) ? "VkBuffer" :
                       std::holds_alternative<VkSampler>(resourceVariant) ? "VkSampler" : "unknown"));

        // Skip placeholder entries (std::monostate) - these are transient resources not yet populated
        if (std::holds_alternative<std::monostate>(resourceVariant)) {
            NODE_LOG_DEBUG("[BuildDescriptorWrites] Skipping binding " + std::to_string(binding.binding) +
                          " - placeholder not yet populated (transient resource)");
            continue;
        }

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

                    NODE_LOG_DEBUG("[DescriptorSetNode::BuildDescriptorWrites] Bound SAMPLED_IMAGE '" +
                                  binding.name + "' at binding " + std::to_string(binding.binding) +
                                  " (sampler should be separate)");
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
                NODE_LOG_DEBUG("[DescriptorSetNode::BuildDescriptorWrites] COMBINED_IMAGE_SAMPLER binding " +
                              std::to_string(binding.binding) + " (" + binding.name + "), variant index=" +
                              std::to_string(resourceVariant.index()));

                if (std::holds_alternative<ImageSamplerPair>(resourceVariant)) {
                    const ImageSamplerPair& pair = std::get<ImageSamplerPair>(resourceVariant);

                    NODE_LOG_DEBUG("[DescriptorSetNode::BuildDescriptorWrites] Extracted ImageSamplerPair: imageView=" +
                                  std::to_string(reinterpret_cast<uint64_t>(pair.imageView)) + ", sampler=" +
                                  std::to_string(reinterpret_cast<uint64_t>(pair.sampler)));

                    VkDescriptorImageInfo imageInfo{};
                    imageInfo.imageView = pair.imageView;
                    imageInfo.sampler = pair.sampler;
                    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    imageInfos.push_back(imageInfo);
                    write.pImageInfo = &imageInfos.back();
                    writes.push_back(write);

                    NODE_LOG_DEBUG("[DescriptorSetNode::BuildDescriptorWrites] Bound COMBINED_IMAGE_SAMPLER '" +
                                  binding.name + "' at binding " + std::to_string(binding.binding) +
                                  " (imageView=" + std::to_string(reinterpret_cast<uint64_t>(pair.imageView)) +
                                  ", sampler=" + std::to_string(reinterpret_cast<uint64_t>(pair.sampler)) + ")");
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

                    NODE_LOG_DEBUG("[DescriptorSetNode::BuildDescriptorWrites] Bound COMBINED_IMAGE_SAMPLER '" +
                                  binding.name + "' at binding " + std::to_string(binding.binding) +
                                  " (imageView=" + std::to_string(reinterpret_cast<uint64_t>(imageView)) +
                                  ", sampler=" + std::to_string(reinterpret_cast<uint64_t>(sampler)) + ") [legacy]");

                    if (sampler == VK_NULL_HANDLE) {
                        NODE_LOG_DEBUG("[DescriptorSetNode::BuildDescriptorWrites] WARNING: Combined image sampler at binding " +
                                      std::to_string(binding.binding) + " has no sampler (VK_NULL_HANDLE)");
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

void DescriptorSetNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_DEBUG("Cleanup: DescriptorSetNode");

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

} // namespace Vixen::RenderGraph
