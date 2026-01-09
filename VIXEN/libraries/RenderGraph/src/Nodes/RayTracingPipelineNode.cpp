#include "Nodes/RayTracingPipelineNode.h"
#include "VulkanDevice.h"
#include "Core/NodeLogging.h"
#include "Core/TaskProfiles/SimpleTaskProfile.h"  // Sprint 6.5: Profile integration
#include "ShaderDataBundle.h"
#include "ShaderStage.h"
#include <cstring>
#include <array>
#include <fstream>
#include <filesystem>

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

    // Sprint 6.5: Register compile-time task profile for cost estimation
    std::string profileId = GetInstanceName() + "_compile";
    compileProfile_ = GetOrCreateProfile<SimpleTaskProfile>(profileId, profileId, "pipeline");
    if (compileProfile_) {
        RegisterPhaseProfile(VirtualTaskPhase::Compile, compileProfile_);
        NODE_LOG_INFO("[RayTracingPipelineNode] Registered compile profile: " + profileId);
    }

    NODE_LOG_DEBUG("[RayTracingPipelineNode::SetupImpl] COMPLETED");
}

void RayTracingPipelineNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_DEBUG("[RayTracingPipelineNode::CompileImpl] ENTERED");
    NODE_LOG_INFO("=== RayTracingPipelineNode::CompileImpl START ===");

    // Sprint 6.5: Start compile timing (RAII - records on scope exit)
    auto compileSample = compileProfile_ ? compileProfile_->Sample() : ITaskProfile::Sampler(nullptr);

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

    // Try to get shader bundle from ShaderLibraryNode input (preferred path)
    shaderBundle_ = ctx.In(RayTracingPipelineNodeConfig::SHADER_DATA_BUNDLE);
    if (shaderBundle_ && shaderBundle_->IsValid()) {
        NODE_LOG_INFO("Loading RT shaders from ShaderDataBundle (ShaderLibraryNode)");
        if (!LoadShadersFromBundle(*shaderBundle_)) {
            throw std::runtime_error("[RayTracingPipelineNode] Failed to load shaders from bundle");
        }
    } else {
        // Fallback: Load shaders from parameter paths (for standalone testing)
        NODE_LOG_INFO("Loading RT shaders from parameter paths (fallback)");
        shaderBundle_ = nullptr;  // No reflection data available
        if (!LoadShadersFromPaths()) {
            throw std::runtime_error("[RayTracingPipelineNode] Failed to load shaders from paths");
        }
    }

    if (raygenShader_ == VK_NULL_HANDLE) {
        throw std::runtime_error("[RayTracingPipelineNode] Ray generation shader is null");
    }
    if (missShader_ == VK_NULL_HANDLE) {
        throw std::runtime_error("[RayTracingPipelineNode] Miss shader is null");
    }
    if (intersectionShader_ == VK_NULL_HANDLE) {
        throw std::runtime_error("[RayTracingPipelineNode] Intersection shader is null");
    }
    if (closestHitShader_ == VK_NULL_HANDLE) {
        throw std::runtime_error("[RayTracingPipelineNode] Closest hit shader is null");
    }

    // Load RTX functions
    if (!LoadRTXFunctions()) {
        throw std::runtime_error("[RayTracingPipelineNode] Failed to load RTX functions");
    }

    // Get descriptor set layout from DescriptorSetNode (via SDI pattern)
    pipelineData_.descriptorSetLayout = ctx.In(RayTracingPipelineNodeConfig::DESCRIPTOR_SET_LAYOUT);
    if (pipelineData_.descriptorSetLayout == VK_NULL_HANDLE) {
        throw std::runtime_error("[RayTracingPipelineNode] DESCRIPTOR_SET_LAYOUT input is null");
    }
    NODE_LOG_INFO("Using descriptor set layout from DescriptorSetNode");

    // Create pipeline layout (uses the input descriptor set layout)
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
    DestroyShaderModules();
}

// ============================================================================
// RTX FUNCTION LOADING
// ============================================================================

// ============================================================================
// SHADER LOADING
// ============================================================================

bool RayTracingPipelineNode::LoadShadersFromBundle(const ShaderManagement::ShaderDataBundle& bundle) {
    // ShaderDataBundle contains CompiledProgram with SPIRV stages
    // Use GetSpirv() API to access each stage's SPIRV data
    VkDevice device = vulkanDevice_->device;

    using ShaderManagement::ShaderStage;

    // Helper to create shader module from stage
    auto createModule = [&](ShaderStage stage) -> VkShaderModule {
        const auto& spirv = bundle.GetSpirv(stage);
        if (spirv.empty()) {
            return VK_NULL_HANDLE;
        }

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = spirv.size() * sizeof(uint32_t);
        createInfo.pCode = spirv.data();

        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &module) != VK_SUCCESS) {
            NODE_LOG_ERROR("Failed to create shader module for stage: " +
                          std::string(ShaderManagement::ShaderStageName(stage)));
            return VK_NULL_HANDLE;
        }
        return module;
    };

    // Create modules for each RT stage
    raygenShader_ = createModule(ShaderStage::RayGen);
    missShader_ = createModule(ShaderStage::Miss);
    closestHitShader_ = createModule(ShaderStage::ClosestHit);
    intersectionShader_ = createModule(ShaderStage::Intersection);

    ownsShaderModules_ = true;
    return raygenShader_ != VK_NULL_HANDLE &&
           missShader_ != VK_NULL_HANDLE &&
           closestHitShader_ != VK_NULL_HANDLE &&
           intersectionShader_ != VK_NULL_HANDLE;
}

bool RayTracingPipelineNode::LoadShadersFromPaths() {
    std::string raygenPath = GetParameterValue<std::string>(
        RayTracingPipelineNodeConfig::PARAM_RAYGEN_SHADER_PATH, "shaders/VoxelRT.rgen.spv");
    std::string missPath = GetParameterValue<std::string>(
        RayTracingPipelineNodeConfig::PARAM_MISS_SHADER_PATH, "shaders/VoxelRT.rmiss.spv");
    std::string closestHitPath = GetParameterValue<std::string>(
        RayTracingPipelineNodeConfig::PARAM_CLOSEST_HIT_SHADER_PATH, "shaders/VoxelRT.rchit.spv");
    std::string intersectionPath = GetParameterValue<std::string>(
        RayTracingPipelineNodeConfig::PARAM_INTERSECTION_SHADER_PATH, "shaders/VoxelRT.rint.spv");

    NODE_LOG_INFO("Loading RT shaders:");
    NODE_LOG_INFO("  Raygen: " + raygenPath);
    NODE_LOG_INFO("  Miss: " + missPath);
    NODE_LOG_INFO("  ClosestHit: " + closestHitPath);
    NODE_LOG_INFO("  Intersection: " + intersectionPath);

    raygenShader_ = CreateShaderModule(raygenPath);
    missShader_ = CreateShaderModule(missPath);
    closestHitShader_ = CreateShaderModule(closestHitPath);
    intersectionShader_ = CreateShaderModule(intersectionPath);

    ownsShaderModules_ = true;
    return raygenShader_ != VK_NULL_HANDLE &&
           missShader_ != VK_NULL_HANDLE &&
           closestHitShader_ != VK_NULL_HANDLE &&
           intersectionShader_ != VK_NULL_HANDLE;
}

VkShaderModule RayTracingPipelineNode::CreateShaderModule(const std::string& path) {
    // Read SPIR-V file
    std::ifstream file(path, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        NODE_LOG_ERROR("Failed to open shader file: " + path);
        return VK_NULL_HANDLE;
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    // Create shader module
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = buffer.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(buffer.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(vulkanDevice_->device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        NODE_LOG_ERROR("Failed to create shader module from: " + path);
        return VK_NULL_HANDLE;
    }

    NODE_LOG_INFO("Created shader module from: " + path);
    return shaderModule;
}

void RayTracingPipelineNode::DestroyShaderModules() {
    if (!ownsShaderModules_ || !vulkanDevice_) {
        return;
    }

    VkDevice device = vulkanDevice_->device;

    if (raygenShader_ != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, raygenShader_, nullptr);
        raygenShader_ = VK_NULL_HANDLE;
    }
    if (missShader_ != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, missShader_, nullptr);
        missShader_ = VK_NULL_HANDLE;
    }
    if (closestHitShader_ != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, closestHitShader_, nullptr);
        closestHitShader_ = VK_NULL_HANDLE;
    }
    if (intersectionShader_ != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, intersectionShader_, nullptr);
        intersectionShader_ = VK_NULL_HANDLE;
    }

    ownsShaderModules_ = false;
    NODE_LOG_INFO("Destroyed shader modules");
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
// DESCRIPTOR SET LAYOUT - Now provided by DescriptorSetNode via SDI
// ============================================================================
// NOTE: CreateDescriptorSetLayout() removed - we now receive the descriptor
// set layout from DescriptorSetNode which creates it from shader reflection.
// The layout is set in CompileImpl via:
//   pipelineData_.descriptorSetLayout = ctx.In(DESCRIPTOR_SET_LAYOUT);

// ============================================================================
// PIPELINE LAYOUT
// ============================================================================

bool RayTracingPipelineNode::CreatePipelineLayout() {
    VkDevice device = vulkanDevice_->device;

    // Get push constant size from reflection data if available
    uint32_t pushConstantSize = 64;  // Default fallback
    if (shaderBundle_ && shaderBundle_->reflectionData) {
        const auto& pushConstants = shaderBundle_->GetPushConstants();
        if (!pushConstants.empty()) {
            // Calculate total size from all ranges (usually just one)
            uint32_t maxEnd = 0;
            for (const auto& range : pushConstants) {
                maxEnd = std::max(maxEnd, range.offset + range.size);
            }
            pushConstantSize = maxEnd;
            NODE_LOG_INFO("Push constant size from reflection: " + std::to_string(pushConstantSize));
        }
    }

    // Push constant for camera data (used by raygen and closest hit shaders)
    constexpr VkShaderStageFlags RT_PUSH_CONSTANT_STAGES =
        VK_SHADER_STAGE_RAYGEN_BIT_KHR |
        VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
        VK_SHADER_STAGE_MISS_BIT_KHR;

    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = RT_PUSH_CONSTANT_STAGES;
    pushConstant.offset = 0;
    pushConstant.size = pushConstantSize;

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

    // Store push constant config in pipeline data for consumers (e.g., TraceRaysNode)
    pipelineData_.pushConstantStages = RT_PUSH_CONSTANT_STAGES;
    pipelineData_.pushConstantOffset = 0;
    pipelineData_.pushConstantSize = pushConstantSize;

    NODE_LOG_INFO("Created RT pipeline layout with push constant size: " + std::to_string(pushConstantSize));
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

    // NOTE: We do NOT destroy descriptorSetLayout - it's owned by DescriptorSetNode
    // Just clear our reference
    pipelineData_.descriptorSetLayout = VK_NULL_HANDLE;

    NODE_LOG_INFO("Destroyed RT pipeline resources");
}

} // namespace Vixen::RenderGraph
