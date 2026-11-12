#include "Nodes/ComputePipelineNode.h"
#include "Core/NodeRegistration.h"
#include "Nodes/DeviceNode.h"
#include "Core/RenderGraph.h"
#include "Core/TaskProfiles/SimpleTaskProfile.h"  // Sprint 6.5: Profile integration
#include "MainCacher.h"
#include "ComputePipelineCacher.h"
#include "PipelineLayoutCacher.h"
#include "DescriptorSetLayoutCacher.h"
#include "PipelineCacher.h"
#include "ShaderModuleCacher.h"
#include "NodeHelpers/CacherHelpers.h"
#include "ShaderDataBundle.h"
#include "ShaderProgram.h"
#include "Hash.h"  // ComputeSHA256HexFromUint32Vec — SPIR-V content hash for pipeline cache key
#include "VulkanDevice.h"
#include "Core/ComputePerformanceLogger.h"
#include "Core/NodeLogging.h"
#include <stdexcept>
#include <iostream>
#include <chrono>
#include <memory>

namespace Vixen::RenderGraph {

namespace {

// The ComputePipelineCacher identity of a bundle's pipeline. Shared by the node's Compile and the
// off-thread warmer (R864.4) so the swap Compile hits what the worker created.
//
// The shader key MUST identify the shader's CODE. The bundle's `uuid` is the descriptor-INTERFACE
// hash (deliberately code-agnostic so shaders with identical descriptor layouts share an SDI header
// — see ShaderDataBundle descriptorInterfaceHash). Two functionally different compute shaders with
// the same descriptor interface (e.g. the auto-sync demo's fill vs post — both expose one std430
// SSBO at set0/binding0 + a uvec2 push constant) carry the SAME uuid, so keying on it would bind
// the first pass's pipeline for the second. The key is program name + SPIR-V content hash; the
// interface uuid keeps its descriptor-sharing role in the layout key.
std::string PipelineCodeKey(const ShaderManagement::ShaderDataBundle& bundle) {
    return bundle.GetProgramName() + ":" +
           ShaderManagement::ComputeSHA256HexFromUint32Vec(bundle.GetSpirv(ShaderManagement::ShaderStage::Compute));
}

std::string PipelineLayoutKey(const ShaderManagement::ShaderDataBundle& bundle) {
    return bundle.uuid + "_pipeline_layout";
}

CashSystem::PipelineLayoutCreateParams PipelineLayoutParams(
    const ShaderManagement::ShaderDataBundle& bundle, VkDescriptorSetLayout descriptorSetLayout) {
    CashSystem::PipelineLayoutCreateParams layoutParams;
    layoutParams.layoutKey = PipelineLayoutKey(bundle);
    if (descriptorSetLayout != VK_NULL_HANDLE) layoutParams.descriptorSetLayout = descriptorSetLayout;
    // Push constants from shader reflection
    if (bundle.reflectionData) {
        for (const auto& pc : bundle.reflectionData->pushConstants) {
            VkPushConstantRange range{};
            range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            range.offset = pc.offset;
            range.size = pc.size;
            layoutParams.pushConstantRanges.push_back(range);
        }
    }
    return layoutParams;
}

} // namespace

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

void ComputePipelineNode::TypedSetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_INFO("[ComputePipelineNode] Graph-scope initialization...");

    // Create specialized performance logger (disabled by default)
    perfLogger_ = std::make_shared<ComputePerformanceLogger>(instanceName);
    perfLogger_->SetEnabled(false);  // Enable manually when needed for debugging

    // Register to node logger hierarchy for shared ownership
    if (nodeLogger) {
        nodeLogger->AddChild(perfLogger_);
    }

    // Sprint 6.5: Register compile-time task profile for cost estimation
    std::string profileId = GetInstanceName() + "_compile";
    compileProfile_ = GetOrCreateProfile<SimpleTaskProfile>(profileId, profileId, "pipeline");
    if (compileProfile_) {
        RegisterPhaseProfile(TaskProfilePhase::Compile, compileProfile_);
        NODE_LOG_INFO("[ComputePipelineNode] Registered compile profile: " + profileId);
    }

    NODE_LOG_INFO("[ComputePipelineNode] Setup complete");
}

void ComputePipelineNode::TypedCompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("[ComputePipelineNode::CompileImpl] Compiling compute pipeline...");

    // Sprint 6.5: Start compile timing (RAII - records on scope exit)
    auto compileSample = compileProfile_ ? compileProfile_->Sample() : ITaskProfile::Sampler(nullptr);

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
    VkShaderModule shaderModule = CreateShaderModule(
        devicePtr, spirv, shaderBundle->GetProgramName(),
        shaderBundle->GetEntryPoint(ShaderManagement::ShaderStage::Compute));

    // Create pipeline layout
    auto layoutWrapper = CreatePipelineLayout(devicePtr, shaderBundle, descriptorSetLayout);
    std::string layoutKey = PipelineLayoutKey(*shaderBundle);

    // Create compute pipeline
    CreateComputePipeline(devicePtr, shaderModule, shaderBundle, layoutWrapper, layoutKey,
                          workgroupX, workgroupY, workgroupZ);
    compiledInterfaceUuid_ = shaderBundle->uuid;
    compiledDescriptorSetLayout_ = descriptorSetLayout;
    compiledWorkgroup_[0] = workgroupX;
    compiledWorkgroup_[1] = workgroupY;
    compiledWorkgroup_[2] = workgroupZ;

    // Set outputs
    ctx.Out(ComputePipelineNodeConfig::PIPELINE, pipeline_);
    ctx.Out(ComputePipelineNodeConfig::PIPELINE_LAYOUT, pipelineLayout_);
    ctx.Out(ComputePipelineNodeConfig::PIPELINE_CACHE, pipelineCache_);
    ctx.Out(ComputePipelineNodeConfig::VULKAN_DEVICE_OUT, devicePtr);

    NODE_LOG_INFO("[ComputePipelineNode] Pipeline created successfully");
}

void ComputePipelineNode::TypedExecuteImpl(TypedExecuteContext& ctx) {
    // No-op: Pipeline is compile-time only resource
    // ComputeDispatchNode will use the pipeline during Execute phase
}

void ComputePipelineNode::TypedCleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("[ComputePipelineNode] Cleaning up...");

    // The module belongs to ShaderModuleCacher.
    shaderModuleWrapper_.reset();
    shaderModule_ = VK_NULL_HANDLE;

    pipelineWrapper_.reset();
    pipeline_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    pipelineCache_ = VK_NULL_HANDLE;

    NODE_LOG_INFO("[ComputePipelineNode] Cleanup complete");
}

VkShaderModule ComputePipelineNode::CreateShaderModule(
    VulkanDevice* device,
    const std::vector<uint32_t>& spirv,
    const std::string& programName,
    const std::string& entryPoint
) {
    auto* moduleCacher = ::RenderGraph::NodeHelpers::RegisterCacherIfNeeded<
        CashSystem::ShaderModuleCacher,
        CashSystem::ShaderModuleWrapper,
        CashSystem::ShaderModuleCreateParams
    >(GetOwningGraph(), device, "ShaderModule");

    // Content-keyed by the SPIR-V: every node compiling the same code shares one module.
    shaderModuleWrapper_ = moduleCacher->GetOrCreateFromSpirv(
        spirv, entryPoint, {}, VK_SHADER_STAGE_COMPUTE_BIT, programName);
    if (!shaderModuleWrapper_ || shaderModuleWrapper_->shaderModule == VK_NULL_HANDLE) {
        throw std::runtime_error("[ComputePipelineNode] Failed to create shader module for " + programName);
    }

    NODE_LOG_INFO("[ComputePipelineNode] VkShaderModule: " +
                  std::to_string(reinterpret_cast<uint64_t>(shaderModuleWrapper_->shaderModule)));

    if (perfLogger_) {
        perfLogger_->LogShaderModule(spirv.size() * sizeof(uint32_t), 1);
    }

    return shaderModuleWrapper_->shaderModule;
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

    const CashSystem::PipelineLayoutCreateParams layoutParams = PipelineLayoutParams(*shaderBundle, descriptorSetLayout);
    NODE_LOG_INFO("[ComputePipelineNode] Descriptor set layout " +
                  std::string(descriptorSetLayout != VK_NULL_HANDLE ? "provided" : "absent") + ", " +
                  std::to_string(layoutParams.pushConstantRanges.size()) + " push constant ranges");

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

    // Code identity (program + SPIR-V hash), not the interface uuid: see PipelineCodeKey.
    const std::string codeKey = PipelineCodeKey(*shaderBundle);

    CashSystem::ComputePipelineCreateParams pipelineParams;
    pipelineParams.shaderModule = shaderModule;
    entryPointName_ = shaderBundle->GetEntryPoint(ShaderManagement::ShaderStage::Compute);
    pipelineParams.entryPoint = entryPointName_.c_str();
    pipelineParams.pipelineLayoutWrapper = layoutWrapper;
    pipelineParams.shaderKey = codeKey;  // code identity, not interface uuid
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

std::function<void(const std::shared_ptr<ShaderManagement::ShaderDataBundle>&)>
ComputePipelineNode::MakePipelineWarmer() {
    VulkanDevice* device = GetDevice();
    if (device == nullptr || compiledInterfaceUuid_.empty() || GetOwningGraph() == nullptr) return {};
    auto& mainCacher = GetOwningGraph()->GetMainCacher();
    // Resolved here (frame thread): MainCacher's registry is not built for concurrent lookup. The
    // cachers themselves are lock-free per key (TypedCacher), so the worker may call them.
    auto* moduleCacher = mainCacher.GetCacher<CashSystem::ShaderModuleCacher, CashSystem::ShaderModuleWrapper,
                                              CashSystem::ShaderModuleCreateParams>(
        typeid(CashSystem::ShaderModuleWrapper), device);
    auto* layoutCacher = mainCacher.GetCacher<CashSystem::PipelineLayoutCacher, CashSystem::PipelineLayoutWrapper,
                                              CashSystem::PipelineLayoutCreateParams>(
        typeid(CashSystem::PipelineLayoutWrapper), device);
    auto* computeCacher = mainCacher.GetCacher<CashSystem::ComputePipelineCacher, CashSystem::ComputePipelineWrapper,
                                               CashSystem::ComputePipelineCreateParams>(
        typeid(CashSystem::ComputePipelineWrapper), device);
    if (moduleCacher == nullptr || layoutCacher == nullptr || computeCacher == nullptr) return {};

    return [moduleCacher, layoutCacher, computeCacher, interfaceUuid = compiledInterfaceUuid_,
            descriptorSetLayout = compiledDescriptorSetLayout_, workgroupX = compiledWorkgroup_[0],
            workgroupY = compiledWorkgroup_[1], workgroupZ = compiledWorkgroup_[2]](
               const std::shared_ptr<ShaderManagement::ShaderDataBundle>& bundle) {
        // A changed descriptor interface means a changed layout: leave it to the swap Compile.
        if (!bundle || bundle->uuid != interfaceUuid) return;
        const auto& spirv = bundle->GetSpirv(ShaderManagement::ShaderStage::Compute);
        if (spirv.empty()) return;
        const std::string entryPoint = bundle->GetEntryPoint(ShaderManagement::ShaderStage::Compute);
        auto module = moduleCacher->GetOrCreateFromSpirv(
            spirv, entryPoint, {}, VK_SHADER_STAGE_COMPUTE_BIT, bundle->GetProgramName());
        if (!module || module->shaderModule == VK_NULL_HANDLE) return;
        auto layout = layoutCacher->GetOrCreate(PipelineLayoutParams(*bundle, descriptorSetLayout));
        if (!layout) return;

        CashSystem::ComputePipelineCreateParams params;
        params.shaderModule = module->shaderModule;
        params.entryPoint = entryPoint.c_str();
        params.pipelineLayoutWrapper = std::move(layout);
        params.shaderKey = PipelineCodeKey(*bundle);
        params.layoutKey = PipelineLayoutKey(*bundle);
        params.workgroupSizeX = workgroupX;
        params.workgroupSizeY = workgroupY;
        params.workgroupSizeZ = workgroupZ;
        static_cast<void>(computeCacher->GetOrCreate(params));
    };
}

} // namespace Vixen::RenderGraph

// Self-registration (M3): registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::ComputePipelineNodeType);
