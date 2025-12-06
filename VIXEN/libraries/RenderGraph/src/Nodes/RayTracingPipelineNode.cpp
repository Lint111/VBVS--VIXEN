#include "Nodes/RayTracingPipelineNode.h"
#include "VulkanDevice.h"
#include "Core/NodeLogging.h"
#include <cstring>
#include <array>

namespace Vixen::RenderGraph {

// ============================================================================
// NODE TYPE FACTORY
// ============================================================================

std::unique_ptr<NodeInstance> RayTracingPipelineNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::unique_ptr<NodeInstance>(
        new RayTracingPipelineNode(instanceName, const_cast<RayTracingPipelineNodeType*>(this))
    );
}

// ============================================================================
// RAY TRACING PIPELINE NODE IMPLEMENTATION
// ============================================================================

RayTracingPipelineNode::RayTracingPipelineNode(
    const std::string& instanceName,
    NodeType* nodeType
) : TypedNode<RayTracingPipelineNodeConfig>(instanceName, nodeType)
{
    NODE_LOG_INFO("RayTracingPipelineNode constructor (Phase K)");
}

void RayTracingPipelineNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_DEBUG("[RayTracingPipelineNode::SetupImpl] ENTERED");

    maxRayRecursion_ = GetParameterValue<uint32_t>(
        RayTracingPipelineNodeConfig::PARAM_MAX_RAY_RECURSION, 1u);
    outputWidth_ = GetParameterValue<uint32_t>(
        RayTracingPipelineNodeConfig::PARAM_OUTPUT_WIDTH, 1920u);
    outputHeight_ = GetParameterValue<uint32_t>(
        RayTracingPipelineNodeConfig::PARAM_OUTPUT_HEIGHT, 1080u);

    NODE_LOG_INFO("RayTracingPipeline setup: maxRecursion=" + std::to_string(maxRayRecursion_) +
                  ", output=" + std::to_string(outputWidth_) + "x" + std::to_string(outputHeight_));

    NODE_LOG_DEBUG("[RayTracingPipelineNode::SetupImpl] COMPLETED");
}

void RayTracingPipelineNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_DEBUG("[RayTracingPipelineNode::CompileImpl] ENTERED");
    NODE_LOG_INFO("=== RayTracingPipelineNode::CompileImpl START ===");

    // Get device
    VulkanDevice* devicePtr = ctx.In(RayTracingPipelineNodeConfig::VULKAN_DEVICE_IN);
    if (!devicePtr) {
        throw std::runtime_error("[RayTracingPipelineNode] VULKAN_DEVICE_IN is null");
    }
    SetDevice(devicePtr);
    vulkanDevice_ = devicePtr;

    // Verify RTX support
    if (!vulkanDevice_->IsRTXEnabled()) {
        throw std::runtime_error("[RayTracingPipelineNode] RTX is not enabled");
    }

    const auto& rtxCaps = vulkanDevice_->GetRTXCapabilities();
    if (!rtxCaps.rayTracingPipeline) {
        throw std::runtime_error("[RayTracingPipelineNode] VK_KHR_ray_tracing_pipeline not supported");
    }

    // Cache RTX properties
    shaderGroupHandleSize_ = rtxCaps.shaderGroupHandleSize;
    shaderGroupBaseAlignment_ = rtxCaps.shaderGroupBaseAlignment;
    shaderGroupHandleAlignment_ = rtxCaps.shaderGroupHandleAlignment;

    NODE_LOG_INFO("RTX properties: handleSize=" + std::to_string(shaderGroupHandleSize_) +
                  ", baseAlignment=" + std::to_string(shaderGroupBaseAlignment_) +
                  ", handleAlignment=" + std::to_string(shaderGroupHandleAlignment_));

    // Validate max recursion depth
    if (maxRayRecursion_ > rtxCaps.maxRayRecursionDepth) {
        NODE_LOG_WARNING("Requested recursion depth " + std::to_string(maxRayRecursion_) +
                         " exceeds max " + std::to_string(rtxCaps.maxRayRecursionDepth) +
                         ", clamping");
        maxRayRecursion_ = rtxCaps.maxRayRecursionDepth;
    }

    // Get acceleration structure (for validation, actual binding in descriptor set)
    AccelerationStructureData* accelData = ctx.In(
        RayTracingPipelineNodeConfig::ACCELERATION_STRUCTURE_DATA);
    if (!accelData || !accelData->IsValid()) {
        throw std::runtime_error("[RayTracingPipelineNode] ACCELERATION_STRUCTURE_DATA is null or invalid");
    }

    // Get shader modules
    raygenShader_ = ctx.In(RayTracingPipelineNodeConfig::RAYGEN_SHADER);
    missShader_ = ctx.In(RayTracingPipelineNodeConfig::MISS_SHADER);

    // HIT_GROUP_SHADERS contains intersection + closest-hit
    // For now, we assume a single combined module or we'll split later
    intersectionShader_ = ctx.In(RayTracingPipelineNodeConfig::HIT_GROUP_SHADERS);
    closestHitShader_ = intersectionShader_;  // Same module, different entry points

    if (raygenShader_ == VK_NULL_HANDLE) {
        throw std::runtime_error("[RayTracingPipelineNode] RAYGEN_SHADER is null");
    }
    if (missShader_ == VK_NULL_HANDLE) {
        throw std::runtime_error("[RayTracingPipelineNode] MISS_SHADER is null");
    }
    if (intersectionShader_ == VK_NULL_HANDLE) {
        throw std::runtime_error("[RayTracingPipelineNode] HIT_GROUP_SHADERS is null");
    }

    // Load RTX functions
    if (!LoadRTXFunctions()) {
        throw std::runtime_error("[RayTracingPipelineNode] Failed to load RTX functions");
    }

    // Create pipeline components
    if (!CreateDescriptorSetLayout()) {
        throw std::runtime_error("[RayTracingPipelineNode] Failed to create descriptor set layout");
    }

    if (!CreatePipelineLayout()) {
        throw std::runtime_error("[RayTracingPipelineNode] Failed to create pipeline layout");
    }

    if (!CreateRTPipeline()) {
        throw std::runtime_error("[RayTracingPipelineNode] Failed to create RT pipeline");
    }

    if (!BuildShaderBindingTable()) {
        throw std::runtime_error("[RayTracingPipelineNode] Failed to build SBT");
    }

    // Output pipeline data
    ctx.Out(RayTracingPipelineNodeConfig::RT_PIPELINE_DATA, &pipelineData_);

    NODE_LOG_INFO("=== RayTracingPipelineNode::CompileImpl COMPLETE ===");
    NODE_LOG_DEBUG("[RayTracingPipelineNode::CompileImpl] COMPLETED");
}

void RayTracingPipelineNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Pipeline is static, just pass through
    ctx.Out(RayTracingPipelineNodeConfig::RT_PIPELINE_DATA, &pipelineData_);
}

void RayTracingPipelineNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("RayTracingPipelineNode cleanup");
    DestroyPipeline();
}

// ============================================================================
// RTX FUNCTION LOADING
// ============================================================================

bool RayTracingPipelineNode::LoadRTXFunctions() {
    VkDevice device = vulkanDevice_->device;

    vkCreateRayTracingPipelinesKHR_ =
        (PFN_vkCreateRayTracingPipelinesKHR)vkGetDeviceProcAddr(
            device, "vkCreateRayTracingPipelinesKHR");

    vkGetRayTracingShaderGroupHandlesKHR_ =
        (PFN_vkGetRayTracingShaderGroupHandlesKHR)vkGetDeviceProcAddr(
            device, "vkGetRayTracingShaderGroupHandlesKHR");

    vkGetBufferDeviceAddressKHR_ =
        (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(
            device, "vkGetBufferDeviceAddressKHR");

    bool success =
        vkCreateRayTracingPipelinesKHR_ &&
        vkGetRayTracingShaderGroupHandlesKHR_ &&
        vkGetBufferDeviceAddressKHR_;

    if (!success) {
        NODE_LOG_ERROR("Failed to load RTX pipeline functions");
    }

    return success;
}

// ============================================================================
// DESCRIPTOR SET LAYOUT
// ============================================================================

bool RayTracingPipelineNode::CreateDescriptorSetLayout() {
    VkDevice device = vulkanDevice_->device;

    std::array<VkDescriptorSetLayoutBinding, 4> bindings{};

    // Binding 0: Acceleration Structure (TLAS)
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR |
                             VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    // Binding 1: Output Image (storage image)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    // Binding 2: Octree nodes buffer
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_INTERSECTION_BIT_KHR |
                             VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    // Binding 3: Materials buffer
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr,
                                    &pipelineData_.descriptorSetLayout) != VK_SUCCESS) {
        NODE_LOG_ERROR("Failed to create descriptor set layout");
        return false;
    }

    NODE_LOG_INFO("Created RT descriptor set layout with 4 bindings");
    return true;
}

// ============================================================================
// PIPELINE LAYOUT
// ============================================================================

bool RayTracingPipelineNode::CreatePipelineLayout() {
    VkDevice device = vulkanDevice_->device;

    // Push constant for camera data (same as compute shader)
    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    pushConstant.offset = 0;
    pushConstant.size = 64;  // cameraPos, time, cameraDir, fov, cameraUp, aspect, cameraRight, debugMode

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &pipelineData_.descriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstant;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr,
                               &pipelineData_.pipelineLayout) != VK_SUCCESS) {
        NODE_LOG_ERROR("Failed to create pipeline layout");
        return false;
    }

    NODE_LOG_INFO("Created RT pipeline layout");
    return true;
}

// ============================================================================
// RAY TRACING PIPELINE
// ============================================================================

bool RayTracingPipelineNode::CreateRTPipeline() {
    VkDevice device = vulkanDevice_->device;

    // Shader stages
    std::array<VkPipelineShaderStageCreateInfo, 4> stages{};

    // 0: Ray Generation
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    stages[0].module = raygenShader_;
    stages[0].pName = "main";

    // 1: Miss
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_MISS_BIT_KHR;
    stages[1].module = missShader_;
    stages[1].pName = "main";

    // 2: Intersection
    stages[2].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[2].stage = VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
    stages[2].module = intersectionShader_;
    stages[2].pName = "main";

    // 3: Closest Hit
    stages[3].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[3].stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    stages[3].module = closestHitShader_;
    stages[3].pName = "main";

    // Shader groups
    std::array<VkRayTracingShaderGroupCreateInfoKHR, 3> groups{};

    // Group 0: Ray Generation (general type)
    groups[0].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    groups[0].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    groups[0].generalShader = 0;  // Index into stages array
    groups[0].closestHitShader = VK_SHADER_UNUSED_KHR;
    groups[0].anyHitShader = VK_SHADER_UNUSED_KHR;
    groups[0].intersectionShader = VK_SHADER_UNUSED_KHR;

    // Group 1: Miss (general type)
    groups[1].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    groups[1].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    groups[1].generalShader = 1;  // Miss shader
    groups[1].closestHitShader = VK_SHADER_UNUSED_KHR;
    groups[1].anyHitShader = VK_SHADER_UNUSED_KHR;
    groups[1].intersectionShader = VK_SHADER_UNUSED_KHR;

    // Group 2: Hit (procedural hit type for AABBs)
    groups[2].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    groups[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
    groups[2].generalShader = VK_SHADER_UNUSED_KHR;
    groups[2].closestHitShader = 3;      // Closest hit shader
    groups[2].anyHitShader = VK_SHADER_UNUSED_KHR;
    groups[2].intersectionShader = 2;    // Intersection shader

    // Create pipeline
    VkRayTracingPipelineCreateInfoKHR pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.groupCount = static_cast<uint32_t>(groups.size());
    pipelineInfo.pGroups = groups.data();
    pipelineInfo.maxPipelineRayRecursionDepth = maxRayRecursion_;
    pipelineInfo.layout = pipelineData_.pipelineLayout;

    VkResult result = vkCreateRayTracingPipelinesKHR_(
        device,
        VK_NULL_HANDLE,  // deferredOperation
        VK_NULL_HANDLE,  // pipelineCache
        1,
        &pipelineInfo,
        nullptr,
        &pipelineData_.pipeline);

    if (result != VK_SUCCESS) {
        NODE_LOG_ERROR("vkCreateRayTracingPipelinesKHR failed: " + std::to_string(result));
        return false;
    }

    pipelineData_.raygenShaderCount = 1;
    pipelineData_.missShaderCount = 1;
    pipelineData_.hitShaderCount = 1;

    NODE_LOG_INFO("Created RT pipeline with 4 stages, 3 groups");
    return true;
}

// ============================================================================
// SHADER BINDING TABLE
// ============================================================================

bool RayTracingPipelineNode::BuildShaderBindingTable() {
    VkDevice device = vulkanDevice_->device;

    // Number of shader groups
    const uint32_t groupCount = 3;  // raygen, miss, hit

    // Calculate aligned handle size
    VkDeviceSize handleSize = shaderGroupHandleSize_;
    VkDeviceSize handleSizeAligned = AlignedSize(handleSize, shaderGroupHandleAlignment_);

    // Calculate region sizes (each region contains one handle)
    pipelineData_.sbt.raygenRegion.stride = AlignedSize(handleSizeAligned, shaderGroupBaseAlignment_);
    pipelineData_.sbt.raygenRegion.size = pipelineData_.sbt.raygenRegion.stride;

    pipelineData_.sbt.missRegion.stride = handleSizeAligned;
    pipelineData_.sbt.missRegion.size = AlignedSize(handleSizeAligned, shaderGroupBaseAlignment_);

    pipelineData_.sbt.hitRegion.stride = handleSizeAligned;
    pipelineData_.sbt.hitRegion.size = AlignedSize(handleSizeAligned, shaderGroupBaseAlignment_);

    pipelineData_.sbt.callableRegion.stride = 0;
    pipelineData_.sbt.callableRegion.size = 0;

    // Total SBT size
    VkDeviceSize sbtSize =
        pipelineData_.sbt.raygenRegion.size +
        pipelineData_.sbt.missRegion.size +
        pipelineData_.sbt.hitRegion.size;

    NODE_LOG_INFO("SBT sizes: raygen=" + std::to_string(pipelineData_.sbt.raygenRegion.size) +
                  ", miss=" + std::to_string(pipelineData_.sbt.missRegion.size) +
                  ", hit=" + std::to_string(pipelineData_.sbt.hitRegion.size) +
                  ", total=" + std::to_string(sbtSize));

    // Get shader group handles from pipeline
    std::vector<uint8_t> shaderHandleStorage(groupCount * handleSize);
    VkResult result = vkGetRayTracingShaderGroupHandlesKHR_(
        device,
        pipelineData_.pipeline,
        0,
        groupCount,
        shaderHandleStorage.size(),
        shaderHandleStorage.data());

    if (result != VK_SUCCESS) {
        NODE_LOG_ERROR("vkGetRayTracingShaderGroupHandlesKHR failed: " + std::to_string(result));
        return false;
    }

    // Create SBT buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = sbtSize;
    bufferInfo.usage = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                       VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &pipelineData_.sbt.buffer) != VK_SUCCESS) {
        NODE_LOG_ERROR("Failed to create SBT buffer");
        return false;
    }

    // Allocate memory
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, pipelineData_.sbt.buffer, &memRequirements);

    VkMemoryAllocateFlagsInfo allocFlagsInfo{};
    allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = &allocFlagsInfo;
    allocInfo.allocationSize = memRequirements.size;

    auto memTypeResult = vulkanDevice_->MemoryTypeFromProperties(
        memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    if (!memTypeResult.has_value()) {
        NODE_LOG_ERROR("Failed to find suitable memory type for SBT");
        return false;
    }
    allocInfo.memoryTypeIndex = memTypeResult.value();

    if (vkAllocateMemory(device, &allocInfo, nullptr, &pipelineData_.sbt.memory) != VK_SUCCESS) {
        NODE_LOG_ERROR("Failed to allocate SBT memory");
        return false;
    }

    vkBindBufferMemory(device, pipelineData_.sbt.buffer, pipelineData_.sbt.memory, 0);

    // Get SBT buffer device address
    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = pipelineData_.sbt.buffer;
    VkDeviceAddress sbtAddress = vkGetBufferDeviceAddressKHR_(device, &addressInfo);

    // Calculate region addresses
    VkDeviceSize offset = 0;
    pipelineData_.sbt.raygenRegion.deviceAddress = sbtAddress + offset;
    offset += pipelineData_.sbt.raygenRegion.size;

    pipelineData_.sbt.missRegion.deviceAddress = sbtAddress + offset;
    offset += pipelineData_.sbt.missRegion.size;

    pipelineData_.sbt.hitRegion.deviceAddress = sbtAddress + offset;

    // Map and copy shader handles to SBT
    void* mappedData;
    vkMapMemory(device, pipelineData_.sbt.memory, 0, sbtSize, 0, &mappedData);

    uint8_t* data = static_cast<uint8_t*>(mappedData);
    offset = 0;

    // Copy raygen handle (group 0)
    memcpy(data + offset, shaderHandleStorage.data() + 0 * handleSize, handleSize);
    offset += pipelineData_.sbt.raygenRegion.size;

    // Copy miss handle (group 1)
    memcpy(data + offset, shaderHandleStorage.data() + 1 * handleSize, handleSize);
    offset += pipelineData_.sbt.missRegion.size;

    // Copy hit handle (group 2)
    memcpy(data + offset, shaderHandleStorage.data() + 2 * handleSize, handleSize);

    vkUnmapMemory(device, pipelineData_.sbt.memory);

    pipelineData_.sbt.totalSize = sbtSize;

    NODE_LOG_INFO("Built SBT: " + std::to_string(sbtSize) + " bytes");
    NODE_LOG_INFO("  Raygen address: 0x" + std::to_string(pipelineData_.sbt.raygenRegion.deviceAddress));
    NODE_LOG_INFO("  Miss address: 0x" + std::to_string(pipelineData_.sbt.missRegion.deviceAddress));
    NODE_LOG_INFO("  Hit address: 0x" + std::to_string(pipelineData_.sbt.hitRegion.deviceAddress));

    return true;
}

VkDeviceSize RayTracingPipelineNode::AlignedSize(VkDeviceSize size, VkDeviceSize alignment) const {
    return (size + alignment - 1) & ~(alignment - 1);
}

// ============================================================================
// CLEANUP
// ============================================================================

void RayTracingPipelineNode::DestroyPipeline() {
    if (!vulkanDevice_) {
        return;
    }

    VkDevice device = vulkanDevice_->device;

    // Destroy SBT
    if (pipelineData_.sbt.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, pipelineData_.sbt.buffer, nullptr);
        pipelineData_.sbt.buffer = VK_NULL_HANDLE;
    }

    if (pipelineData_.sbt.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, pipelineData_.sbt.memory, nullptr);
        pipelineData_.sbt.memory = VK_NULL_HANDLE;
    }

    // Destroy pipeline
    if (pipelineData_.pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipelineData_.pipeline, nullptr);
        pipelineData_.pipeline = VK_NULL_HANDLE;
    }

    // Destroy pipeline layout
    if (pipelineData_.pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineData_.pipelineLayout, nullptr);
        pipelineData_.pipelineLayout = VK_NULL_HANDLE;
    }

    // Destroy descriptor set layout
    if (pipelineData_.descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, pipelineData_.descriptorSetLayout, nullptr);
        pipelineData_.descriptorSetLayout = VK_NULL_HANDLE;
    }

    NODE_LOG_INFO("Destroyed RT pipeline resources");
}

} // namespace Vixen::RenderGraph
