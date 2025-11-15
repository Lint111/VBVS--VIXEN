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
#include "Core/NodeLogging.h"
#include <stdexcept>
#include <iostream>
#include <chrono>
#include <memory>

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
    NODE_LOG_DEBUG("[ComputePipelineNode] Constructor called for " + instanceName);
}

void ComputePipelineNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_INFO("[ComputePipelineNode] Graph-scope initialization...");

    // Create specialized performance logger (disabled by default)
    perfLogger_ = std::make_shared<ComputePerformanceLogger>(instanceName);
    perfLogger_->SetEnabled(false);  // Enable manually when needed for debugging

    // Register to node logger hierarchy for shared ownership
    if (nodeLogger) {
        nodeLogger->AddChild(perfLogger_);
    }

    NODE_LOG_INFO("[ComputePipelineNode] Setup complete");
}

void ComputePipelineNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("[ComputePipelineNode::CompileImpl] Compiling compute pipeline...");

    // Access device input
    VulkanDevice* devicePtr = ctx.In(ComputePipelineNodeConfig::VULKAN_DEVICE_IN);
    if (!devicePtr) {
        throw std::runtime_error("[ComputePipelineNode] VULKAN_DEVICE_IN is null");
    }
    SetDevice(devicePtr);

    // Get parameters
    uint32_t workgroupX = GetParameterValue<uint32_t>(ComputePipelineNodeConfig::WORKGROUP_SIZE_X, 8);
    uint32_t workgroupY = GetParameterValue<uint32_t>(ComputePipelineNodeConfig::WORKGROUP_SIZE_Y, 8);
    uint32_t workgroupZ = GetParameterValue<uint32_t>(ComputePipelineNodeConfig::WORKGROUP_SIZE_Z, 1);

    // Get inputs
    auto shaderBundle = ctx.In(ComputePipelineNodeConfig::SHADER_DATA_BUNDLE);
    VkDescriptorSetLayout descriptorSetLayout = ctx.In(ComputePipelineNodeConfig::DESCRIPTOR_SET_LAYOUT);

    if (!shaderBundle) {
        throw std::runtime_error("[ComputePipelineNode] SHADER_DATA_BUNDLE is null");
    }

    NODE_LOG_INFO("[ComputePipelineNode] Shader UUID: " + shaderBundle->uuid);
    NODE_LOG_INFO("[ComputePipelineNode] Workgroup: " + std::to_string(workgroupX) + "x" +
                  std::to_string(workgroupY) + "x" + std::to_string(workgroupZ));

    // Create shader module from SPIR-V
    const auto& spirv = shaderBundle->GetSpirv(ShaderManagement::ShaderStage::Compute);
    if (spirv.empty()) {
        throw std::runtime_error("[ComputePipelineNode] No compute shader SPIRV");
    }
    VkShaderModule shaderModule = CreateShaderModule(devicePtr, spirv);

    // Create pipeline layout
    auto layoutWrapper = CreatePipelineLayout(devicePtr, shaderBundle, descriptorSetLayout);
    std::string layoutKey = shaderBundle->uuid + "_pipeline_layout";

    // Create compute pipeline
    CreateComputePipeline(devicePtr, shaderModule, shaderBundle, layoutWrapper, layoutKey,
                          workgroupX, workgroupY, workgroupZ);

    // Set outputs
    ctx.Out(ComputePipelineNodeConfig::PIPELINE, pipeline_);
    ctx.Out(ComputePipelineNodeConfig::PIPELINE_LAYOUT, pipelineLayout_);
    ctx.Out(ComputePipelineNodeConfig::PIPELINE_CACHE, pipelineCache_);
    ctx.Out(ComputePipelineNodeConfig::VULKAN_DEVICE_OUT, devicePtr);

    NODE_LOG_INFO("[ComputePipelineNode] Pipeline created successfully");
}

void ComputePipelineNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // No-op: Pipeline is compile-time only resource
    // ComputeDispatchNode will use the pipeline during Execute phase
}

void ComputePipelineNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("[ComputePipelineNode] Cleaning up...");

    if (shaderModule_ != VK_NULL_HANDLE) {
        VulkanDevice* devicePtr = GetDevice();
        if (devicePtr) {
            vkDestroyShaderModule(devicePtr->device, shaderModule_, nullptr);
        }
        shaderModule_ = VK_NULL_HANDLE;
    }

    pipelineWrapper_.reset();
    pipeline_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    pipelineCache_ = VK_NULL_HANDLE;

    NODE_LOG_INFO("[ComputePipelineNode] Cleanup complete");
}

VkShaderModule ComputePipelineNode::CreateShaderModule(
    VulkanDevice* device,
    const std::vector<uint32_t>& spirv
) {
    VkShaderModuleCreateInfo moduleCreateInfo{};
    moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleCreateInfo.codeSize = spirv.size() * sizeof(uint32_t);
    moduleCreateInfo.pCode = spirv.data();

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    VkResult result = vkCreateShaderModule(device->device, &moduleCreateInfo, nullptr, &shaderModule);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("[ComputePipelineNode] Failed to create shader module: " + std::to_string(result));
    }

    NODE_LOG_INFO("[ComputePipelineNode] Created VkShaderModule: " +
                  std::to_string(reinterpret_cast<uint64_t>(shaderModule)));

    if (perfLogger_) {
        perfLogger_->LogShaderModule(spirv.size() * sizeof(uint32_t), 1);
    }

    return shaderModule;
}

std::shared_ptr<CashSystem::PipelineLayoutWrapper> ComputePipelineNode::CreatePipelineLayout(
    VulkanDevice* device,
    const std::shared_ptr<ShaderManagement::ShaderDataBundle>& shaderBundle,
    VkDescriptorSetLayout descriptorSetLayout
) {
    auto& mainCacher = GetOwningGraph()->GetMainCacher();

    // Register PipelineLayoutCacher if needed
    if (!mainCacher.IsRegistered(typeid(CashSystem::PipelineLayoutWrapper))) {
        NODE_LOG_INFO("[ComputePipelineNode] Registering PipelineLayoutCacher");
        mainCacher.RegisterCacher<
            CashSystem::PipelineLayoutCacher,
            CashSystem::PipelineLayoutWrapper,
            CashSystem::PipelineLayoutCreateParams
        >(typeid(CashSystem::PipelineLayoutWrapper), "PipelineLayout", true);
    }

    auto* layoutCacher = mainCacher.GetCacher<
        CashSystem::PipelineLayoutCacher,
        CashSystem::PipelineLayoutWrapper,
        CashSystem::PipelineLayoutCreateParams
    >(typeid(CashSystem::PipelineLayoutWrapper), device);

    if (!layoutCacher) {
        throw std::runtime_error("[ComputePipelineNode] Failed to get PipelineLayoutCacher");
    }

    if (!layoutCacher->IsInitialized()) {
        layoutCacher->Initialize(device);
    }

    // Build pipeline layout params
    CashSystem::PipelineLayoutCreateParams layoutParams;
    layoutParams.layoutKey = shaderBundle->uuid + "_pipeline_layout";

    if (descriptorSetLayout != VK_NULL_HANDLE) {
        layoutParams.descriptorSetLayout = descriptorSetLayout;
        NODE_LOG_INFO("[ComputePipelineNode] Using provided descriptor set layout");
    }

    // Extract push constants from shader reflection
    if (shaderBundle->reflectionData && !shaderBundle->reflectionData->pushConstants.empty()) {
        for (const auto& pc : shaderBundle->reflectionData->pushConstants) {
            VkPushConstantRange range{};
            range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            range.offset = pc.offset;
            range.size = pc.size;
            layoutParams.pushConstantRanges.push_back(range);
        }
        NODE_LOG_INFO("[ComputePipelineNode] Added " +
                      std::to_string(layoutParams.pushConstantRanges.size()) + " push constant ranges");
    }

    auto wrapper = layoutCacher->GetOrCreate(layoutParams);
    NODE_LOG_INFO("[ComputePipelineNode] Pipeline layout created: " + layoutParams.layoutKey);

    return wrapper;
}

void ComputePipelineNode::CreateComputePipeline(
    VulkanDevice* device,
    VkShaderModule shaderModule,
    const std::shared_ptr<ShaderManagement::ShaderDataBundle>& shaderBundle,
    std::shared_ptr<CashSystem::PipelineLayoutWrapper> layoutWrapper,
    const std::string& layoutKey,
    uint32_t workgroupX,
    uint32_t workgroupY,
    uint32_t workgroupZ
) {
    auto& mainCacher = GetOwningGraph()->GetMainCacher();

    // Register ComputePipelineCacher if needed
    if (!mainCacher.IsRegistered(typeid(CashSystem::ComputePipelineWrapper))) {
        NODE_LOG_INFO("[ComputePipelineNode] Registering ComputePipelineCacher");
        mainCacher.RegisterCacher<
            CashSystem::ComputePipelineCacher,
            CashSystem::ComputePipelineWrapper,
            CashSystem::ComputePipelineCreateParams
        >(typeid(CashSystem::ComputePipelineWrapper), "ComputePipeline", true);
    }

    auto* computeCacher = mainCacher.GetCacher<
        CashSystem::ComputePipelineCacher,
        CashSystem::ComputePipelineWrapper,
        CashSystem::ComputePipelineCreateParams
    >(typeid(CashSystem::ComputePipelineWrapper), device);

    if (!computeCacher) {
        throw std::runtime_error("[ComputePipelineNode] Failed to get ComputePipelineCacher");
    }

    // Build pipeline params
    CashSystem::ComputePipelineCreateParams pipelineParams;
    pipelineParams.shaderModule = shaderModule;
    entryPointName_ = shaderBundle->GetEntryPoint(ShaderManagement::ShaderStage::Compute);
    pipelineParams.entryPoint = entryPointName_.c_str();
    pipelineParams.pipelineLayoutWrapper = layoutWrapper;
    pipelineParams.shaderKey = shaderBundle->uuid;
    pipelineParams.layoutKey = layoutKey;
    pipelineParams.workgroupSizeX = workgroupX;
    pipelineParams.workgroupSizeY = workgroupY;
    pipelineParams.workgroupSizeZ = workgroupZ;

    // Create pipeline with timing
    auto pipelineCreateStart = std::chrono::high_resolution_clock::now();
    pipelineWrapper_ = computeCacher->GetOrCreate(pipelineParams);
    shaderModule_ = shaderModule;

    pipeline_ = pipelineWrapper_->pipeline;
    pipelineLayout_ = pipelineWrapper_->pipelineLayoutWrapper->layout;
    pipelineCache_ = pipelineWrapper_->cache;

    auto pipelineCreateEnd = std::chrono::high_resolution_clock::now();
    float timeMs = std::chrono::duration<float, std::milli>(pipelineCreateEnd - pipelineCreateStart).count();

    if (perfLogger_) {
        perfLogger_->LogPipelineCreation(reinterpret_cast<uint64_t>(pipeline_), shaderBundle->uuid, timeMs);
    }

    NODE_LOG_INFO("[ComputePipelineNode] Pipeline: " + std::to_string(reinterpret_cast<uint64_t>(pipeline_)));
    NODE_LOG_INFO("[ComputePipelineNode] Layout: " + std::to_string(reinterpret_cast<uint64_t>(pipelineLayout_)));
}

} // namespace Vixen::RenderGraph
