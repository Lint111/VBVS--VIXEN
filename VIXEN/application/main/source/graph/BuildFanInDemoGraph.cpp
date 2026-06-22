// BuildFanInDemoGraph -- auto-sync FrameGraph P5b M2 (AR#21) multi-submit fan-in proof.
//
// Builds an env-gated (VIXEN_FANIN_DEMO), self-contained graph that proves
// TIMELINE-ONLY ordering across SEPARATE compute submits (no binary handoff between
// the compute submits):
//
//     compute(fanin_a)  [OWN submit / OWN group] -> writes bufA      [ComputeStorageWrite]
//     compute(fanin_b)  [OWN submit / OWN group] -> writes bufB      [ComputeStorageWrite]
//     compute(consume)  [OWN submit / OWN group] -> reads bufA+bufB  [ComputeStorageRead x2]
//                                                 -> writes swapchain [ComputeStorageWrite]
//     present
//
// Each compute stage is a generic ComputeStageNode = its OWN SubmitGroup (the
// FrameSyncScheduler makes one group per node in the execution order). The scheduler
// bakes one SyncEdge per (writer-group → reader-group) hazard on a shared buffer
// Resource*: bufA is wired into BOTH producerA's BUFFER_WRITE (ComputeStorageWrite)
// and the consumer's BUFFER_READ_A (ComputeStorageRead), so both stages' bundles hold
// the SAME Resource* → the tracker records a write-then-read → one edge A→consumer.
// Likewise bufB → a second edge B→consumer. The consumer group thus has 2 waitEdges
// → 2 timeline waits: the genuine fan-in the 1-edge live composite can't isolate.
//
// There is NO binary semaphore between the producers and the consumer: the producers
// submit with NO fence and NO binary signal to the consumer (only a timeline SIGNAL);
// the consumer WAITS those two timeline values. WSI acquire/present stay binary (on
// the consumer/present). A missing/incorrect timeline wait → the consumer reads
// ungenerated buffer data → visibly wrong output (so the live gate can SEE it).
//
// Topological ordering producer→consumer is established by routing each producer's
// BUFFER_OUT passthrough into the consumer's descriptor gatherer (the buffer the
// consumer binds genuinely comes from the producer). The hazard edge itself is baked
// off the separate buffer sync-slot wiring (shared StorageBufferNode Resource*).
#include "VulkanGraphApplication.h"
#include <filesystem>
#include <stdexcept>
#include "Connection/ConnectionModifier.h"
#include "Connection/Modifiers/SlotRoleModifier.h"
#include "Core/NodeRegistration.h"
#include "ShaderStage.h"  // ShaderManagement::ShaderStage / PipelineTypeConstraint
// --- nodes this subgraph wires ---
#include "Data/Nodes/CommandPoolNodeConfig.h"
#include "Data/Nodes/ComputePipelineNodeConfig.h"
#include "Data/Nodes/ComputeStageNodeConfig.h"
#include "Data/Nodes/DescriptorResourceGathererNodeConfig.h"
#include "Data/Nodes/DescriptorSetNodeConfig.h"
#include "Data/Nodes/DeviceNodeConfig.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "Data/Nodes/InstanceNodeConfig.h"
#include "Data/Nodes/PresentNodeConfig.h"
#include "Data/Nodes/ShaderLibraryNodeConfig.h"
#include "Data/Nodes/StorageBufferNodeConfig.h"
#include "Data/Nodes/SwapChainNodeConfig.h"
#include "Data/Nodes/WindowNodeConfig.h"
#include "Nodes/CommandPoolNode.h"
#include "Nodes/ComputePipelineNode.h"
#include "Nodes/ComputeStageNode.h"
#include "Nodes/DescriptorResourceGathererNode.h"
#include "Nodes/DescriptorSetNode.h"
#include "Nodes/DeviceNode.h"
#include "Nodes/FrameSyncNode.h"
#include "Nodes/InstanceNode.h"
#include "Nodes/PresentNode.h"
#include "Nodes/ShaderLibraryNode.h"
#include "Nodes/StorageBufferNode.h"
#include "Nodes/SwapChainNode.h"
#include "Nodes/WindowNode.h"

namespace {

// Register a shader builder for a single-stage COMPUTE program (.comp).
// Mirrors BuildAutoSyncDemoGraph's RegisterComputeShader.
void RegisterComputeShader(Vixen::RenderGraph::ShaderLibraryNode* node,
                           const char* shaderFile, const char* programName) {
    node->RegisterShaderBuilder([shaderFile, programName](int vulkanVer, int spirvVer) {
        ShaderManagement::ShaderBundleBuilder builder;
        std::vector<std::filesystem::path> possiblePaths = {
#ifdef VIXEN_SHADER_SOURCE_DIR
            std::string(VIXEN_SHADER_SOURCE_DIR) + "/" + shaderFile,
#endif
            std::string("shaders/") + shaderFile,
            std::string("../shaders/") + shaderFile,
            shaderFile
        };
        std::filesystem::path compPath;
        for (const auto& p : possiblePaths) {
            if (std::filesystem::exists(p)) { compPath = p; break; }
        }
        if (compPath.empty()) {
            throw std::runtime_error(std::string(shaderFile) + " not found - check shader search paths");
        }
        builder.SetProgramName(programName)
               .SetPipelineType(ShaderManagement::PipelineTypeConstraint::Compute)
               .SetTargetVulkanVersion(vulkanVer)
               .SetTargetSpirvVersion(spirvVer)
#ifdef VIXEN_SHADER_SOURCE_DIR
               .AddIncludePath(VIXEN_SHADER_SOURCE_DIR)
#endif
               .AddStageFromFile(ShaderManagement::ShaderStage::Compute, compPath, "main");
        return builder;
    });
}

} // namespace

void VulkanGraphApplication::BuildFanInDemoGraph() {
    using namespace Vixen::RenderGraph;
    mainLogger->Info("Building fan-in FrameGraph demo graph (AR#21 P5b M2): "
                     "compute(A)+compute(B) -> compute(consume) -> present, timeline-only fan-in");

    // ===================================================================
    // PHASE 1: Create nodes
    // ===================================================================
    NodeHandle instanceNode    = renderGraph->AddNode<InstanceNodeType>("fi_instance");
    NodeHandle deviceNode      = renderGraph->AddNode<DeviceNodeType>("fi_device");
    NodeHandle windowNode      = renderGraph->AddNode<WindowNodeType>("main_window");
    NodeHandle swapChainNode   = renderGraph->AddNode<SwapChainNodeType>("fi_swapchain");
    NodeHandle commandPoolNode = renderGraph->AddNode<CommandPoolNodeType>("fi_cmd_pool");
    NodeHandle frameSyncNode   = renderGraph->AddNode<FrameSyncNodeType>("fi_frame_sync");

    // Two independent SSBOs (one vec4 per swapchain pixel each).
    NodeHandle bufANode = renderGraph->AddNode<StorageBufferNodeType>("fi_bufA");
    NodeHandle bufBNode = renderGraph->AddNode<StorageBufferNodeType>("fi_bufB");

    // Producer A path.
    NodeHandle aShaderLib = renderGraph->AddNode<ShaderLibraryNodeType>("fi_a_shader");
    NodeHandle aGatherer  = renderGraph->AddNode<DescriptorResourceGathererNodeType>("fi_a_gatherer");
    NodeHandle aDescSet   = renderGraph->AddNode<DescriptorSetNodeType>("fi_a_descriptors");
    NodeHandle aPipeline  = renderGraph->AddNode<ComputePipelineNodeType>("fi_a_pipeline");
    NodeHandle aStage     = renderGraph->AddNode<ComputeStageNodeType>("fi_a_stage");

    // Producer B path.
    NodeHandle bShaderLib = renderGraph->AddNode<ShaderLibraryNodeType>("fi_b_shader");
    NodeHandle bGatherer  = renderGraph->AddNode<DescriptorResourceGathererNodeType>("fi_b_gatherer");
    NodeHandle bDescSet   = renderGraph->AddNode<DescriptorSetNodeType>("fi_b_descriptors");
    NodeHandle bPipeline  = renderGraph->AddNode<ComputePipelineNodeType>("fi_b_pipeline");
    NodeHandle bStage     = renderGraph->AddNode<ComputeStageNodeType>("fi_b_stage");

    // Consumer path.
    NodeHandle cShaderLib = renderGraph->AddNode<ShaderLibraryNodeType>("fi_c_shader");
    NodeHandle cGatherer  = renderGraph->AddNode<DescriptorResourceGathererNodeType>("fi_c_gatherer");
    NodeHandle cDescSet   = renderGraph->AddNode<DescriptorSetNodeType>("fi_c_descriptors");
    NodeHandle cPipeline  = renderGraph->AddNode<ComputePipelineNodeType>("fi_c_pipeline");
    NodeHandle cStage     = renderGraph->AddNode<ComputeStageNodeType>("fi_c_stage");

    NodeHandle presentNode = renderGraph->AddNode<PresentNodeType>("fi_present");

    // ===================================================================
    // Parameters
    // ===================================================================
    auto* window = static_cast<WindowNode*>(renderGraph->GetInstance(windowNode));
    window->SetParameter(WindowNodeConfig::PARAM_WIDTH, width);
    window->SetParameter(WindowNodeConfig::PARAM_HEIGHT, height);
    auto* device = static_cast<DeviceNode*>(renderGraph->GetInstance(deviceNode));
    device->SetParameter(DeviceNodeConfig::PARAM_GPU_INDEX, 0u);

    // SSBOs sized to the swapchain extent: vec4 (16 bytes) per pixel.
    for (NodeHandle b : {bufANode, bufBNode}) {
        auto* ssbo = static_cast<StorageBufferNode*>(renderGraph->GetInstance(b));
        ssbo->SetParameter(StorageBufferNodeConfig::PARAM_BYTES_PER_PIXEL,
                           static_cast<uint32_t>(4 * sizeof(float)));
    }

    // Dispatch dims (8x8 tiling over the extent). The producers have no swapchain input,
    // so the host sets the dims explicitly; the consumer would otherwise derive them
    // from its swapchain input, but we set them uniformly for clarity.
    const uint32_t localSize = 8u;
    const uint32_t groupsX = (static_cast<uint32_t>(width)  + localSize - 1u) / localSize;
    const uint32_t groupsY = (static_cast<uint32_t>(height) + localSize - 1u) / localSize;

    auto* aStagePtr = static_cast<ComputeStageNode*>(renderGraph->GetInstance(aStage));
    auto* bStagePtr = static_cast<ComputeStageNode*>(renderGraph->GetInstance(bStage));
    auto* cStagePtr = static_cast<ComputeStageNode*>(renderGraph->GetInstance(cStage));
    for (ComputeStageNode* s : {aStagePtr, bStagePtr, cStagePtr}) {
        s->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_X, groupsX);
        s->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Y, groupsY);
        s->SetParameter(ComputeStageNodeConfig::PARAM_DISPATCH_Z, 1u);
        // {width,height} push constant for the shaders' idx = y*width+x addressing
        // (pushed through each shader's reflected PC range; no gatherer node needed).
        s->SetParameter(ComputeStageNodeConfig::PARAM_PC_WIDTH,  static_cast<uint32_t>(width));
        s->SetParameter(ComputeStageNodeConfig::PARAM_PC_HEIGHT, static_cast<uint32_t>(height));
    }
    // Only the consumer is swapchain-adjacent (binary acquire wait + renderComplete
    // signal + fence ownership + swapchain transition). The producers stay timeline-only.
    aStagePtr->SetParameter(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);
    bStagePtr->SetParameter(ComputeStageNodeConfig::PARAM_IS_CONSUMER, false);
    cStagePtr->SetParameter(ComputeStageNodeConfig::PARAM_IS_CONSUMER, true);

    auto* present = static_cast<PresentNode*>(renderGraph->GetInstance(presentNode));
    present->SetParameter(PresentNodeConfig::WAIT_FOR_IDLE, true);

    // Shader programs.
    RegisterComputeShader(static_cast<ShaderLibraryNode*>(renderGraph->GetInstance(aShaderLib)),
                          "fanin_a.comp", "FanInA");
    RegisterComputeShader(static_cast<ShaderLibraryNode*>(renderGraph->GetInstance(bShaderLib)),
                          "fanin_b.comp", "FanInB");
    RegisterComputeShader(static_cast<ShaderLibraryNode*>(renderGraph->GetInstance(cShaderLib)),
                          "fanin_consume.comp", "FanInConsume");

    // ===================================================================
    // Connections
    // ===================================================================
    ConnectionBatch batch(renderGraph);

    // --- Core infrastructure chain ---
    batch.Connect(instanceNode, InstanceNodeConfig::INSTANCE, deviceNode, DeviceNodeConfig::INSTANCE_IN);
    batch.Connect(deviceNode, DeviceNodeConfig::INSTANCE_OUT, windowNode, WindowNodeConfig::INSTANCE);
    batch.Connect(windowNode, WindowNodeConfig::WINDOW, swapChainNode, SwapChainNodeConfig::WINDOW)
         .Connect(windowNode, WindowNodeConfig::WIDTH_OUT, swapChainNode, SwapChainNodeConfig::WIDTH)
         .Connect(windowNode, WindowNodeConfig::HEIGHT_OUT, swapChainNode, SwapChainNodeConfig::HEIGHT);
    batch.Connect(deviceNode, DeviceNodeConfig::INSTANCE_OUT, swapChainNode, SwapChainNodeConfig::INSTANCE)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, swapChainNode, SwapChainNodeConfig::VULKAN_DEVICE_IN);
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, frameSyncNode, FrameSyncNodeConfig::VULKAN_DEVICE);
    batch.Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX, swapChainNode, SwapChainNodeConfig::CURRENT_FRAME_INDEX)
         .Connect(frameSyncNode, FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY, swapChainNode, SwapChainNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY);
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, commandPoolNode, CommandPoolNodeConfig::VULKAN_DEVICE_IN);

    // --- SSBOs (extent-driven: device + swapchain) ---
    for (NodeHandle b : {bufANode, bufBNode}) {
        batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, b, StorageBufferNodeConfig::VULKAN_DEVICE_IN)
             .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC, b, StorageBufferNodeConfig::SWAPCHAIN_INFO);
    }

    // --- Helper: wire a descriptor path (shaderLib -> gatherer -> descSet -> pipeline) ---
    // The gatherer's resource bindings are wired by the caller (different per role).
    auto wirePipeline = [&](NodeHandle shaderLib, NodeHandle gatherer, NodeHandle descSet, NodeHandle pipeline) {
        batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, shaderLib, ShaderLibraryNodeConfig::VULKAN_DEVICE_IN);
        batch.Connect(shaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                      gatherer, DescriptorResourceGathererNodeConfig::SHADER_DATA_BUNDLE);
        batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, descSet, DescriptorSetNodeConfig::VULKAN_DEVICE_IN)
             .Connect(shaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE, descSet, DescriptorSetNodeConfig::SHADER_DATA_BUNDLE)
             .Connect(gatherer, DescriptorResourceGathererNodeConfig::DESCRIPTOR_RESOURCES, descSet, DescriptorSetNodeConfig::DESCRIPTOR_RESOURCES)
             .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC, descSet, DescriptorSetNodeConfig::SWAPCHAIN_INFO)
             .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX, descSet, DescriptorSetNodeConfig::IMAGE_INDEX);
        batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, pipeline, ComputePipelineNodeConfig::VULKAN_DEVICE_IN)
             .Connect(shaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE, pipeline, ComputePipelineNodeConfig::SHADER_DATA_BUNDLE)
             .Connect(descSet, DescriptorSetNodeConfig::DESCRIPTOR_SET_LAYOUT, pipeline, ComputePipelineNodeConfig::DESCRIPTOR_SET_LAYOUT);
    };
    wirePipeline(aShaderLib, aGatherer, aDescSet, aPipeline);
    wirePipeline(bShaderLib, bGatherer, bDescSet, bPipeline);
    wirePipeline(cShaderLib, cGatherer, cDescSet, cPipeline);

    const SlotRoleModifier execDep(SlotRole::Dependency | SlotRole::Execute);
    const SlotRoleModifier execOnly(SlotRole::Execute);

    // --- Producer gatherers: bufA at binding 0 (A), bufB at binding 0 (B) ---
    batch.Connect(bufANode, StorageBufferNodeConfig::STORAGE_BUFFER, aGatherer, 0, execDep);
    batch.Connect(bufBNode, StorageBufferNodeConfig::STORAGE_BUFFER, bGatherer, 0, execDep);

    // --- Consumer gatherer: swapchain image (binding 0) + bufA (binding 1) + bufB (binding 2) ---
    // The buffer bindings are sourced from the PRODUCERS' BUFFER_OUT passthrough, which
    // (a) carries the same VkBuffer handle and (b) topologically orders producer→consumer
    // (so the consumer's group index is higher and the baked edge points producer→consumer).
    batch.Connect(swapChainNode, SwapChainNodeConfig::CURRENT_FRAME_IMAGE_VIEW, cGatherer, 0, execOnly);
    batch.Connect(aStage, ComputeStageNodeConfig::BUFFER_OUT, cGatherer, 1, execDep);
    batch.Connect(bStage, ComputeStageNodeConfig::BUFFER_OUT, cGatherer, 2, execDep);

    // --- Common ComputeStageNode inputs (device, cmd pool, pipeline/layout/descsets,
    // image index, frame sync, timeline) for all three stages ---
    auto wireStageCommon = [&](NodeHandle stage, NodeHandle pipeline, NodeHandle descSet, NodeHandle shaderLib) {
        batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, stage, ComputeStageNodeConfig::VULKAN_DEVICE_IN)
             .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL, stage, ComputeStageNodeConfig::COMMAND_POOL)
             .Connect(pipeline, ComputePipelineNodeConfig::PIPELINE, stage, ComputeStageNodeConfig::COMPUTE_PIPELINE)
             .Connect(pipeline, ComputePipelineNodeConfig::PIPELINE_LAYOUT, stage, ComputeStageNodeConfig::PIPELINE_LAYOUT)
             .Connect(descSet, DescriptorSetNodeConfig::DESCRIPTOR_SETS, stage, ComputeStageNodeConfig::DESCRIPTOR_SETS)
             .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX, stage, ComputeStageNodeConfig::IMAGE_INDEX)
             .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX, stage, ComputeStageNodeConfig::CURRENT_FRAME_INDEX)
             .Connect(frameSyncNode, FrameSyncNodeConfig::IN_FLIGHT_FENCE, stage, ComputeStageNodeConfig::IN_FLIGHT_FENCE)
             .Connect(frameSyncNode, FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY, stage, ComputeStageNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY)
             .Connect(swapChainNode, SwapChainNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY, stage, ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY)
             .Connect(shaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE, stage, ComputeStageNodeConfig::SHADER_DATA_BUNDLE);
        // Timeline primitives from FrameSyncNode → all three stages (producers SIGNAL, consumer WAITS).
        batch.Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_SEMAPHORE, stage, ComputeStageNodeConfig::TIMELINE_SEMAPHORE_IN)
             .Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_FRAME_BASE, stage, ComputeStageNodeConfig::TIMELINE_FRAME_BASE_IN);
    };
    wireStageCommon(aStage, aPipeline, aDescSet, aShaderLib);
    wireStageCommon(bStage, bPipeline, bDescSet, bShaderLib);
    wireStageCommon(cStage, cPipeline, cDescSet, cShaderLib);

    // --- Buffer SYNC slots: the hazard-correlation identity. The SAME StorageBufferNode
    // Resource* feeds the producer's WRITE slot AND the consumer's READ slot, so the
    // tracker bakes a SyncEdge per buffer (A→consumer, B→consumer) = the 2-wait fan-in. ---
    batch.Connect(bufANode, StorageBufferNodeConfig::STORAGE_BUFFER, aStage, ComputeStageNodeConfig::BUFFER_WRITE, execOnly);
    batch.Connect(bufBNode, StorageBufferNodeConfig::STORAGE_BUFFER, bStage, ComputeStageNodeConfig::BUFFER_WRITE, execOnly);
    batch.Connect(bufANode, StorageBufferNodeConfig::STORAGE_BUFFER, cStage, ComputeStageNodeConfig::BUFFER_READ_A, execOnly);
    batch.Connect(bufBNode, StorageBufferNodeConfig::STORAGE_BUFFER, cStage, ComputeStageNodeConfig::BUFFER_READ_B, execOnly);

    // --- Consumer also writes the swapchain image (SWAPCHAIN_INFO sync slot:
    // ComputeStorageWrite — drives the swapchain image transition + extent fallback). ---
    batch.Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC, cStage, ComputeStageNodeConfig::SWAPCHAIN_INFO, execOnly);

    // (Push constants {width,height} are supplied per-stage via PARAM_PC_WIDTH/HEIGHT,
    // pushed through each shader's reflected PC range — see the parameter block above.)

    // --- Present: waits on the consumer's renderComplete semaphore ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, presentNode, PresentNodeConfig::VULKAN_DEVICE_IN)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_HANDLE, presentNode, PresentNodeConfig::SWAPCHAIN)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX, presentNode, PresentNodeConfig::IMAGE_INDEX)
         .Connect(cStage, ComputeStageNodeConfig::RENDER_COMPLETE_SEMAPHORE, presentNode, PresentNodeConfig::RENDER_COMPLETE_SEMAPHORE)
         .Connect(swapChainNode, SwapChainNodeConfig::PRESENT_FENCES_ARRAY, presentNode, PresentNodeConfig::PRESENT_FENCE_ARRAY);

    size_t connectionCount = batch.GetConnectionCount();
    batch.RegisterAll();

    mainLogger->Info("Fan-in demo graph built (" + std::to_string(renderGraph->GetNodeCount()) +
                     " nodes, " + std::to_string(connectionCount) + " connections)");
}
