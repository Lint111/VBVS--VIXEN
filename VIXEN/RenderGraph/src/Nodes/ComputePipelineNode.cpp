#include "Nodes/ComputePipelineNode.h"
#include "Nodes/DeviceNode.h"
#include "Core/RenderGraph.h"
#include "CashSystem/MainCacher.h"
#include "CashSystem/ComputePipelineCacher.h"
#include "CashSystem/PipelineLayoutCacher.h"
#include "CashSystem/DescriptorSetLayoutCacher.h"
#include "CashSystem/PipelineCacher.h"
#include "ShaderManagement/ShaderDataBundle.h"
#include "ShaderManagement/ShaderProgram.h"
#include "VulkanResources/VulkanDevice.h"
#include "Core/ComputePerformanceLogger.h"
#include <stdexcept>
#include <iostream>
#include <chrono>

namespace Vixen::RenderGraph {

// ===== NodeType Factory =====

std::unique_ptr<NodeInstance> ComputePipelineNodeType::CreateInstance(
    const std::string& instanceName
) const {
    return std::make_unique<ComputePipelineNode>(instanceName, const_cast<ComputePipelineNodeType*>(this));
}

// ===== ComputePipelineNode Implementation =====

ComputePipelineNode::ComputePipelineNode(
    const std::string& instanceName,
    NodeType* nodeType
) : TypedNode<ComputePipelineNodeConfig>(instanceName, nodeType)
{
    std::cout << "[ComputePipelineNode] Constructor called for " << instanceName << std::endl;
}

void ComputePipelineNode::SetupImpl(SetupContext& ctx) {
    std::cout << "[ComputePipelineNode::SetupImpl] Graph-scope initialization..." << std::endl;

#if VIXEN_DEBUG_BUILD
    // Create specialized performance logger and register to node logger
    perfLogger_ = std::make_unique<ComputePerformanceLogger>(instanceName);
    if (nodeLogger) {
        nodeLogger->AddChild(perfLogger_.get());
    }
#endif

    std::cout << "[ComputePipelineNode::SetupImpl] Setup complete" << std::endl;
}

void ComputePipelineNode::CompileImpl(CompileContext& ctx) {
    std::cout << "[ComputePipelineNode::CompileImpl] Compiling compute pipeline..." << std::endl;

    // Access device input (compile-time dependency)
    VulkanDevicePtr devicePtr = In(ComputePipelineNodeConfig::VULKAN_DEVICE_IN);
    if (!devicePtr) {
        throw std::runtime_error("[ComputePipelineNode::CompileImpl] VULKAN_DEVICE_IN is null");
    }

    // Register device for cleanup tracking
    SetDevice(devicePtr);

    // ===== 1. Get Parameters =====
    uint32_t workgroupX = GetParameterValue<uint32_t>(ComputePipelineNodeConfig::WORKGROUP_SIZE_X, 0);
    uint32_t workgroupY = GetParameterValue<uint32_t>(ComputePipelineNodeConfig::WORKGROUP_SIZE_Y, 0);
    uint32_t workgroupZ = GetParameterValue<uint32_t>(ComputePipelineNodeConfig::WORKGROUP_SIZE_Z, 0);

    std::cout << "[ComputePipelineNode::CompileImpl] Workgroup sizes (params): "
              << workgroupX << "x" << workgroupY << "x" << workgroupZ << std::endl;

    // ===== 2. Get Inputs =====
    // Note: devicePtr already retrieved and validated at start of CompileImpl
    ShaderDataBundlePtr shaderBundle = In(ComputePipelineNodeConfig::SHADER_DATA_BUNDLE);
    VkDescriptorSetLayout descriptorSetLayout = In(ComputePipelineNodeConfig::DESCRIPTOR_SET_LAYOUT);

    if (!shaderBundle) {
        throw std::runtime_error("[ComputePipelineNode::CompileImpl] SHADER_DATA_BUNDLE is null");
    }

    std::cout << "[ComputePipelineNode::CompileImpl] Shader UUID: " << shaderBundle->uuid << std::endl;

    // ===== 3. Create VkShaderModule from SPIRV =====
    ShaderManagement::ShaderStage computeStage = ShaderManagement::ShaderStage::Compute;
    const auto& spirv = shaderBundle->GetSpirv(computeStage);
    if (spirv.empty()) {
        throw std::runtime_error("[ComputePipelineNode::CompileImpl] No compute shader SPIRV in bundle");
    }

    VkShaderModuleCreateInfo moduleCreateInfo{};
    moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleCreateInfo.codeSize = spirv.size() * sizeof(uint32_t);
    moduleCreateInfo.pCode = spirv.data();

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    VkResult result = vkCreateShaderModule(devicePtr->device, &moduleCreateInfo, nullptr, &shaderModule);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[ComputePipelineNode::CompileImpl] Failed to create shader module: " + std::to_string(result));
    }

    std::cout << "[ComputePipelineNode::CompileImpl] Created VkShaderModule: "
              << reinterpret_cast<uint64_t>(shaderModule) << std::endl;

#if VIXEN_DEBUG_BUILD
    if (perfLogger_) {
        perfLogger_->LogShaderModule(spirv.size() * sizeof(uint32_t), 1);
    }
#endif

    // ===== 4. Extract Workgroup Size from Shader (if not specified) =====
    // Note: SPIRV reflection doesn't expose local_size_x/y/z, so we must use parameters or defaults
    if (workgroupX == 0 || workgroupY == 0 || workgroupZ == 0) {
        // Use default workgroup size
        workgroupX = 8;
        workgroupY = 8;
        workgroupZ = 1;
        std::cout << "[ComputePipelineNode::CompileImpl] No workgroup size in params, using default: "
                  << workgroupX << "x" << workgroupY << "x" << workgroupZ << std::endl;
    }

    // ===== 5. Get MainCacher =====
    auto* renderGraph = GetOwningGraph();
    if (!renderGraph) {
        vkDestroyShaderModule(devicePtr->device, shaderModule, nullptr);
        throw std::runtime_error("[ComputePipelineNode::CompileImpl] Owning graph not available");
    }
    auto& mainCacher = renderGraph->GetMainCacher();

    // ===== 6. Create Pipeline Layout =====
    std::shared_ptr<CashSystem::PipelineLayoutWrapper> pipelineLayoutWrapper;
    std::string layoutKey;

    // Register PipelineLayoutCacher if not already registered
    if (!mainCacher.IsRegistered(typeid(CashSystem::PipelineLayoutWrapper))) {
        std::cout << "[ComputePipelineNode::CompileImpl] Registering PipelineLayoutCacher..." << std::endl;
        mainCacher.RegisterCacher<
            CashSystem::PipelineLayoutCacher,
            CashSystem::PipelineLayoutWrapper,
            CashSystem::PipelineLayoutCreateParams
        >(
            typeid(CashSystem::PipelineLayoutWrapper),
            "PipelineLayout",
            true  // device-dependent
        );
    }

    // Get PipelineLayoutCacher - this will initialize it with the device
    auto* layoutCacher = mainCacher.GetCacher<
        CashSystem::PipelineLayoutCacher,
        CashSystem::PipelineLayoutWrapper,
        CashSystem::PipelineLayoutCreateParams
    >(typeid(CashSystem::PipelineLayoutWrapper), devicePtr);

    if (!layoutCacher) {
        vkDestroyShaderModule(devicePtr->device, shaderModule, nullptr);
        throw std::runtime_error("[ComputePipelineNode::CompileImpl] Failed to get PipelineLayoutCacher");
    }

    // Ensure cacher is initialized with device (workaround for initialization timing issue)
    if (!layoutCacher->IsInitialized()) {
        std::cout << "[ComputePipelineNode::CompileImpl] Initializing PipelineLayoutCacher with device..." << std::endl;
        layoutCacher->Initialize(devicePtr);
    }

    // Build pipeline layout params
    CashSystem::PipelineLayoutCreateParams layoutParams;
    layoutParams.layoutKey = shaderBundle->uuid + "_pipeline_layout";

    // Add descriptor set layout if provided
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        layoutParams.descriptorSetLayout = descriptorSetLayout;
        std::cout << "[ComputePipelineNode::CompileImpl] Using provided descriptor set layout" << std::endl;
    }

    // Convert push constants from reflection if available
    if (shaderBundle->reflectionData && !shaderBundle->reflectionData->pushConstants.empty()) {
        for (const auto& pc : shaderBundle->reflectionData->pushConstants) {
            VkPushConstantRange range{};
            range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            range.offset = pc.offset;
            range.size = pc.size;
            layoutParams.pushConstantRanges.push_back(range);
        }
        std::cout << "[ComputePipelineNode::CompileImpl] Found " << layoutParams.pushConstantRanges.size()
                  << " push constant range(s) in shader reflection" << std::endl;
    }

    pipelineLayoutWrapper = layoutCacher->GetOrCreate(layoutParams);
    layoutKey = layoutParams.layoutKey;

    std::cout << "[ComputePipelineNode::CompileImpl] Created pipeline layout: " << layoutKey << std::endl;

    // ===== 7. Create Compute Pipeline via ComputePipelineCacher =====

    // Register ComputePipelineCacher if not already registered
    if (!mainCacher.IsRegistered(typeid(CashSystem::ComputePipelineWrapper))) {
        std::cout << "[ComputePipelineNode::CompileImpl] Registering ComputePipelineCacher..." << std::endl;
        mainCacher.RegisterCacher<
            CashSystem::ComputePipelineCacher,
            CashSystem::ComputePipelineWrapper,
            CashSystem::ComputePipelineCreateParams
        >(
            typeid(CashSystem::ComputePipelineWrapper),
            "ComputePipeline",
            true  // device-dependent
        );
    }

    auto* computeCacher = mainCacher.GetCacher<
        CashSystem::ComputePipelineCacher,
        CashSystem::ComputePipelineWrapper,
        CashSystem::ComputePipelineCreateParams
    >(typeid(CashSystem::ComputePipelineWrapper), devicePtr);

    if (!computeCacher) {
        vkDestroyShaderModule(devicePtr->device, shaderModule, nullptr);
        throw std::runtime_error("[ComputePipelineNode::CompileImpl] Failed to get ComputePipelineCacher");
    }

    CashSystem::ComputePipelineCreateParams pipelineParams;
    pipelineParams.shaderModule = shaderModule;
    entryPointName_ = shaderBundle->GetEntryPoint(computeStage);
    pipelineParams.entryPoint = entryPointName_.c_str();
    pipelineParams.pipelineLayoutWrapper = pipelineLayoutWrapper;  // Use explicit wrapper
    pipelineParams.shaderKey = shaderBundle->uuid;
    pipelineParams.layoutKey = layoutKey;
    pipelineParams.workgroupSizeX = workgroupX;
    pipelineParams.workgroupSizeY = workgroupY;
    pipelineParams.workgroupSizeZ = workgroupZ;

#if VIXEN_DEBUG_BUILD
    auto pipelineCreateStart = std::chrono::high_resolution_clock::now();
#endif

    pipelineWrapper_ = computeCacher->GetOrCreate(pipelineParams);
    shaderModule_ = shaderModule;  // Store for cleanup

    pipeline_ = pipelineWrapper_->pipeline;
    pipelineLayout_ = pipelineWrapper_->pipelineLayoutWrapper->layout;
    pipelineCache_ = pipelineWrapper_->cache;

#if VIXEN_DEBUG_BUILD
    auto pipelineCreateEnd = std::chrono::high_resolution_clock::now();
    float pipelineCreateTimeMs = std::chrono::duration<float, std::milli>(pipelineCreateEnd - pipelineCreateStart).count();

    if (perfLogger_) {
        perfLogger_->LogPipelineCreation(
            reinterpret_cast<uint64_t>(pipeline_),
            shaderBundle->uuid,
            pipelineCreateTimeMs
        );
    }
#endif

    std::cout << "[ComputePipelineNode::CompileImpl] Compute pipeline created successfully" << std::endl;
    std::cout << "[ComputePipelineNode::CompileImpl]   Pipeline: "
              << reinterpret_cast<uint64_t>(pipeline_) << std::endl;
    std::cout << "[ComputePipelineNode::CompileImpl]   Layout: "
              << reinterpret_cast<uint64_t>(pipelineLayout_) << std::endl;

    // ===== 8. Set Outputs =====
    Out(ComputePipelineNodeConfig::PIPELINE, pipeline_);
    Out(ComputePipelineNodeConfig::PIPELINE_LAYOUT, pipelineLayout_);
    Out(ComputePipelineNodeConfig::PIPELINE_CACHE, pipelineCache_);
    Out(ComputePipelineNodeConfig::VULKAN_DEVICE_OUT, devicePtr);

    std::cout << "[ComputePipelineNode::CompileImpl] Outputs set" << std::endl;
}

void ComputePipelineNode::ExecuteImpl(ExecuteContext& ctx) {
    // No-op: Pipeline is compile-time only resource
    // ComputeDispatchNode will use the pipeline during Execute phase
}

void ComputePipelineNode::CleanupImpl(CleanupContext& ctx) {
    std::cout << "[ComputePipelineNode::CleanupImpl] Cleaning up..." << std::endl;

    // Destroy shader module (we own this, not the cacher)
    if (shaderModule_ != VK_NULL_HANDLE) {
        VulkanDevicePtr devicePtr = In(ComputePipelineNodeConfig::VULKAN_DEVICE_IN);
        if (devicePtr) {
            vkDestroyShaderModule(devicePtr->device, shaderModule_, nullptr);
            std::cout << "[ComputePipelineNode::CleanupImpl] Destroyed shader module" << std::endl;
        }
        shaderModule_ = VK_NULL_HANDLE;
    }

    // Release shared wrapper (cacher owns the actual pipeline)
    pipelineWrapper_.reset();

    // Clear cached handles
    pipeline_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    pipelineCache_ = VK_NULL_HANDLE;

    std::cout << "[ComputePipelineNode::CleanupImpl] Cleanup complete" << std::endl;
}

} // namespace Vixen::RenderGraph
