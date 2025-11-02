#include "Nodes/ComputePipelineNode.h"
#include "Nodes/DeviceNode.h"
#include "Core/GraphBuilder.h"
#include "CashSystem/MainCacher.h"
#include "CashSystem/ComputePipelineCacher.h"
#include "CashSystem/DescriptorSetLayoutCacher.h"
#include "CashSystem/PipelineCacher.h"
#include "ShaderManagement/ShaderDataBundle.h"
#include <stdexcept>
#include <iostream>

namespace Vixen::RenderGraph {

// ===== NodeType Factory =====

std::unique_ptr<INode> ComputePipelineNodeType::CreateInstance() const {
    return std::make_unique<ComputePipelineNode>();
}

// ===== ComputePipelineNode Implementation =====

ComputePipelineNode::ComputePipelineNode()
    : TypedNode<ComputePipelineNodeConfig>()
{
    std::cout << "[ComputePipelineNode] Constructor called" << std::endl;
}

void ComputePipelineNode::SetupImpl(Context& ctx) {
    std::cout << "[ComputePipelineNode::SetupImpl] Setting up..." << std::endl;

    // Get device from input
    VulkanDevicePtr devicePtr = In(ComputePipelineNodeConfig::VULKAN_DEVICE_IN);
    if (!devicePtr) {
        throw std::runtime_error("[ComputePipelineNode::SetupImpl] VULKAN_DEVICE_IN is null");
    }

    // Register device for cleanup tracking
    SetDevice(devicePtr);

    std::cout << "[ComputePipelineNode::SetupImpl] Setup complete (device registered)" << std::endl;
}

void ComputePipelineNode::CompileImpl(Context& ctx) {
    std::cout << "[ComputePipelineNode::CompileImpl] Compiling compute pipeline..." << std::endl;

    // ===== 1. Get Parameters =====
    uint32_t workgroupX = GetParameter<uint32_t>(ComputePipelineNodeConfig::WORKGROUP_SIZE_X, 0);
    uint32_t workgroupY = GetParameter<uint32_t>(ComputePipelineNodeConfig::WORKGROUP_SIZE_Y, 0);
    uint32_t workgroupZ = GetParameter<uint32_t>(ComputePipelineNodeConfig::WORKGROUP_SIZE_Z, 0);

    std::cout << "[ComputePipelineNode::CompileImpl] Workgroup sizes (params): "
              << workgroupX << "x" << workgroupY << "x" << workgroupZ << std::endl;

    // ===== 2. Get Inputs =====
    VulkanDevicePtr devicePtr = In(ComputePipelineNodeConfig::VULKAN_DEVICE_IN);
    ShaderDataBundlePtr shaderBundle = In(ComputePipelineNodeConfig::SHADER_DATA_BUNDLE);
    VkDescriptorSetLayout descriptorSetLayout = In(ComputePipelineNodeConfig::DESCRIPTOR_SET_LAYOUT);

    if (!devicePtr) {
        throw std::runtime_error("[ComputePipelineNode::CompileImpl] VULKAN_DEVICE_IN is null");
    }
    if (!shaderBundle) {
        throw std::runtime_error("[ComputePipelineNode::CompileImpl] SHADER_DATA_BUNDLE is null");
    }
    if (!shaderBundle->shaderModule) {
        throw std::runtime_error("[ComputePipelineNode::CompileImpl] ShaderDataBundle has null shader module");
    }

    std::cout << "[ComputePipelineNode::CompileImpl] Shader key: " << shaderBundle->shaderKey << std::endl;

    // ===== 3. Extract Workgroup Size from Shader (if not specified) =====
    if (workgroupX == 0 || workgroupY == 0 || workgroupZ == 0) {
        // Use shader reflection data
        if (shaderBundle->reflectionData.localSizeX > 0) {
            workgroupX = shaderBundle->reflectionData.localSizeX;
            workgroupY = shaderBundle->reflectionData.localSizeY;
            workgroupZ = shaderBundle->reflectionData.localSizeZ;
            std::cout << "[ComputePipelineNode::CompileImpl] Extracted workgroup size from shader: "
                      << workgroupX << "x" << workgroupY << "x" << workgroupZ << std::endl;
        } else {
            // Fallback to default
            workgroupX = 8;
            workgroupY = 8;
            workgroupZ = 1;
            std::cout << "[ComputePipelineNode::CompileImpl] WARNING: No workgroup size in shader or params, using default: "
                      << workgroupX << "x" << workgroupY << "x" << workgroupZ << std::endl;
        }
    }

    // ===== 4. Auto-Generate Descriptor Set Layout (Phase 4) =====
    std::shared_ptr<CashSystem::DescriptorSetLayoutWrapper> layoutWrapper;
    std::string layoutKey;

    if (descriptorSetLayout == VK_NULL_HANDLE) {
        std::cout << "[ComputePipelineNode::CompileImpl] No descriptor layout provided, auto-generating from shader reflection..." << std::endl;

        // Get DescriptorSetLayoutCacher
        auto& layoutCacher = CashSystem::MainCacher::GetInstance()
            .GetOrRegisterCacher<CashSystem::DescriptorSetLayoutCacher>(devicePtr);

        // Build descriptor layout from reflection
        CashSystem::DescriptorSetLayoutCreateParams layoutParams;
        layoutParams.bindings = shaderBundle->reflectionData.descriptorBindings;
        layoutParams.layoutKey = shaderBundle->shaderKey + "_auto_layout";

        layoutWrapper = layoutCacher.GetOrCreate(layoutParams);
        descriptorSetLayout = layoutWrapper->layout;
        layoutKey = layoutParams.layoutKey;

        std::cout << "[ComputePipelineNode::CompileImpl] Auto-generated descriptor layout: "
                  << layoutKey << " (bindings: " << layoutParams.bindings.size() << ")" << std::endl;
    } else {
        layoutKey = shaderBundle->shaderKey + "_external_layout";
        std::cout << "[ComputePipelineNode::CompileImpl] Using provided descriptor layout" << std::endl;
    }

    // ===== 5. Extract Push Constants from Shader Reflection (Phase 5) =====
    std::vector<VkPushConstantRange> pushConstantRanges;
    if (!shaderBundle->reflectionData.pushConstantRanges.empty()) {
        pushConstantRanges = shaderBundle->reflectionData.pushConstantRanges;
        std::cout << "[ComputePipelineNode::CompileImpl] Found " << pushConstantRanges.size()
                  << " push constant range(s) in shader reflection" << std::endl;
    }

    // ===== 6. Get Shared VkPipelineCache =====
    auto& pipelineCacher = CashSystem::MainCacher::GetInstance()
        .GetOrRegisterCacher<CashSystem::PipelineCacher>(devicePtr);
    VkPipelineCache sharedCache = pipelineCacher.GetPipelineCache();

    std::cout << "[ComputePipelineNode::CompileImpl] Using shared VkPipelineCache: "
              << reinterpret_cast<uint64_t>(sharedCache) << std::endl;

    // ===== 7. Create Compute Pipeline via ComputePipelineCacher =====
    auto& computeCacher = CashSystem::MainCacher::GetInstance()
        .GetOrRegisterCacher<CashSystem::ComputePipelineCacher>(devicePtr, sharedCache);

    CashSystem::ComputePipelineCreateParams pipelineParams;
    pipelineParams.shaderModule = shaderBundle->shaderModule;
    pipelineParams.entryPoint = "main";
    pipelineParams.descriptorSetLayout = descriptorSetLayout;
    pipelineParams.pushConstantRanges = pushConstantRanges;
    pipelineParams.shaderKey = shaderBundle->shaderKey;
    pipelineParams.layoutKey = layoutKey;
    pipelineParams.workgroupSizeX = workgroupX;
    pipelineParams.workgroupSizeY = workgroupY;
    pipelineParams.workgroupSizeZ = workgroupZ;

    pipelineWrapper_ = computeCacher.GetOrCreate(pipelineParams);

    pipeline_ = pipelineWrapper_->pipeline;
    pipelineLayout_ = pipelineWrapper_->pipelineLayoutWrapper->pipelineLayout;
    pipelineCache_ = pipelineWrapper_->cache;

    std::cout << "[ComputePipelineNode::CompileImpl] Compute pipeline created successfully" << std::endl;
    std::cout << "[ComputePipelineNode::CompileImpl]   Pipeline: "
              << reinterpret_cast<uint64_t>(pipeline_) << std::endl;
    std::cout << "[ComputePipelineNode::CompileImpl]   Layout: "
              << reinterpret_cast<uint64_t>(pipelineLayout_) << std::endl;
    std::cout << "[ComputePipelineNode::CompileImpl]   Cache: "
              << reinterpret_cast<uint64_t>(pipelineCache_) << std::endl;

    // ===== 8. Set Outputs =====
    Out(ComputePipelineNodeConfig::PIPELINE, pipeline_);
    Out(ComputePipelineNodeConfig::PIPELINE_LAYOUT, pipelineLayout_);
    Out(ComputePipelineNodeConfig::PIPELINE_CACHE, pipelineCache_);
    Out(ComputePipelineNodeConfig::VULKAN_DEVICE_OUT, devicePtr);

    std::cout << "[ComputePipelineNode::CompileImpl] Outputs set" << std::endl;
}

void ComputePipelineNode::ExecuteImpl(TaskContext& ctx) {
    // No-op: Pipeline is compile-time only resource
    // ComputeDispatchNode will use the pipeline during Execute phase
}

void ComputePipelineNode::CleanupImpl() {
    std::cout << "[ComputePipelineNode::CleanupImpl] Cleaning up..." << std::endl;

    // Release shared wrapper (cacher owns the actual Vulkan resources)
    pipelineWrapper_.reset();

    // Clear cached handles
    pipeline_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    pipelineCache_ = VK_NULL_HANDLE;

    std::cout << "[ComputePipelineNode::CleanupImpl] Cleanup complete (resources owned by cachers)" << std::endl;
}

} // namespace Vixen::RenderGraph
