// BuildAutoSyncDemoGraph -- auto-sync FrameGraph P4 (AR#21) proving demo.
//
// Builds an env-gated, self-contained graph that proves BUFFER-hazard
// auto-synchronization through the generic PassGroupNode:
//
//     compute(autosync_fill)   -> writes SSBO            [ComputeStorageWrite]
//     compute(autosync_post)   -> in-place RMW SSBO      [ComputeStorageReadWrite]  (RAW vs fill)
//     render (fullscreen)      -> frag reads SSBO        [FragmentStorageRead]      (RAW vs post)
//     present
//
// All three passes are recorded into ONE command buffer + ONE submit by
// PassGroupNode, with the intra-pass buffer barriers AUTO-BAKED by
// BuildPassGroupSchedule (P2 scheduler core) and replayed between passes.
//
// Handle-timing: pipeline/render-pass/framebuffer/descriptor-set handles are
// produced by their NODES at COMPILE time, but this function runs at graph-BUILD
// time. We therefore resolve the concrete handles in a post-compile callback
// (RenderGraph::RegisterPostNodeCompileCallback) — VIXEN's idiomatic mechanism
// for acting on a just-compiled node's outputs — and feed them to PassGroupNode
// via its existing host-assembly API (AddComputePass / AddRenderPass). A single
// GENERIC compile-ordering input slot (PassGroupNodeConfig::COMPILE_AFTER) forces
// PassGroupNode to compile AFTER its handle sources, so the pass list is fully
// populated before PassGroupNode::CompileImpl bakes the schedule.
//
// Descriptor topology: three SEPARATE descriptor sets (one per shader bundle's
// reflected layout), all pointing at the SAME VkBuffer. This is legal Vulkan and
// proves the identical buffer hazard (syncval tracks the buffer's memory, not the
// descriptor-set identity) — see Auto-Sync-FrameGraph-Inc1-Plan-P4 notes. We do
// NOT need (and do not use) a single shared compute|fragment layout.
#include "VulkanGraphApplication.h"
#include <cstring>      // std::memcpy for push-constant packing
#include <filesystem>
#include <stdexcept>
#include "Connection/ConnectionModifier.h"
#include "Connection/Modifiers/SlotRoleModifier.h"  // ordering-edge role
#include "Core/NodeRegistration.h"
#include "ShaderStage.h"  // ShaderManagement::ShaderStage / PipelineTypeConstraint
// --- nodes this subgraph wires ---
#include "Data/Nodes/CommandPoolNodeConfig.h"
#include "Data/Nodes/ComputePipelineNodeConfig.h"
#include "Data/Nodes/DescriptorResourceGathererNodeConfig.h"
#include "Data/Nodes/DescriptorSetNodeConfig.h"
#include "Data/Nodes/DeviceNodeConfig.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "Data/Nodes/FramebufferNodeConfig.h"
#include "Data/Nodes/GraphicsPipelineNodeConfig.h"
#include "Data/Nodes/InstanceNodeConfig.h"
#include "Data/Nodes/PassGroupNodeConfig.h"
#include "Data/Nodes/PresentNodeConfig.h"
#include "Data/Nodes/RenderPassNodeConfig.h"
#include "Data/Nodes/ShaderLibraryNodeConfig.h"
#include "Data/Nodes/StorageBufferNodeConfig.h"
#include "Data/Nodes/SwapChainNodeConfig.h"
#include "Data/Nodes/WindowNodeConfig.h"
#include "Nodes/CommandPoolNode.h"
#include "Nodes/ComputePipelineNode.h"
#include "Nodes/DescriptorResourceGathererNode.h"
#include "Nodes/DescriptorSetNode.h"
#include "Nodes/DeviceNode.h"
#include "Nodes/FrameSyncNode.h"
#include "Nodes/FramebufferNode.h"
#include "Nodes/GraphicsPipelineNode.h"
#include "Nodes/InstanceNode.h"
#include "Nodes/PassGroupNode.h"
#include "Nodes/PresentNode.h"
#include "Nodes/RenderPassNode.h"
#include "Nodes/ShaderLibraryNode.h"
#include "Nodes/StorageBufferNode.h"
#include "Nodes/SwapChainNode.h"
#include "Nodes/WindowNode.h"
#include "Data/Core/CompileTimeResourceSystem.h"  // Resource (GetOutput/GetHandle)
#include "IRenderTarget.h"

namespace {

// Register a shader builder for a single-stage COMPUTE program (.comp).
// Mirrors the live BuildRenderGraph compute-shader builder (file:line ~393-437).
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

// Register a shader builder for a vert+frag GRAPHICS program.
void RegisterGraphicsShader(Vixen::RenderGraph::ShaderLibraryNode* node,
                            const char* vertFile, const char* fragFile, const char* programName) {
    node->RegisterShaderBuilder([vertFile, fragFile, programName](int vulkanVer, int spirvVer) {
        ShaderManagement::ShaderBundleBuilder builder;
        auto find = [](const char* file) -> std::filesystem::path {
            std::vector<std::filesystem::path> possiblePaths = {
#ifdef VIXEN_SHADER_SOURCE_DIR
                std::string(VIXEN_SHADER_SOURCE_DIR) + "/" + file,
#endif
                std::string("shaders/") + file,
                std::string("../shaders/") + file,
                file
            };
            for (const auto& p : possiblePaths) {
                if (std::filesystem::exists(p)) return p;
            }
            throw std::runtime_error(std::string(file) + " not found - check shader search paths");
        };
        builder.SetProgramName(programName)
               .SetPipelineType(ShaderManagement::PipelineTypeConstraint::Graphics)
               .SetTargetVulkanVersion(vulkanVer)
               .SetTargetSpirvVersion(spirvVer)
#ifdef VIXEN_SHADER_SOURCE_DIR
               .AddIncludePath(VIXEN_SHADER_SOURCE_DIR)
#endif
               .AddStageFromFile(ShaderManagement::ShaderStage::Vertex,   find(vertFile), "main")
               .AddStageFromFile(ShaderManagement::ShaderStage::Fragment, find(fragFile), "main");
        return builder;
    });
}

// Pack width/height into an 8-byte push-constant blob (matches the shaders'
// `layout(push_constant) { uint width; uint height; }`).
std::vector<uint8_t> PackExtentPC(uint32_t w, uint32_t h) {
    std::vector<uint8_t> bytes(2 * sizeof(uint32_t));
    std::memcpy(bytes.data(),                  &w, sizeof(uint32_t));
    std::memcpy(bytes.data() + sizeof(uint32_t), &h, sizeof(uint32_t));
    return bytes;
}

} // namespace

void VulkanGraphApplication::BuildAutoSyncDemoGraph() {
    using namespace Vixen::RenderGraph;
    mainLogger->Info("Building auto-sync FrameGraph demo graph (AR#21 P4): compute->compute->render->present");

    // ===================================================================
    // PHASE 1: Create nodes
    // ===================================================================
    // ----- Infrastructure (mirrors BuildInstancingDemoGraph) -----
    NodeHandle instanceNode    = renderGraph->AddNode<InstanceNodeType>("as_instance");
    NodeHandle deviceNode      = renderGraph->AddNode<DeviceNodeType>("as_device");
    NodeHandle windowNode      = renderGraph->AddNode<WindowNodeType>("main_window");
    NodeHandle swapChainNode   = renderGraph->AddNode<SwapChainNodeType>("as_swapchain");
    NodeHandle commandPoolNode = renderGraph->AddNode<CommandPoolNodeType>("as_cmd_pool");
    NodeHandle frameSyncNode   = renderGraph->AddNode<FrameSyncNodeType>("as_frame_sync");

    // ----- Shared SSBO (one vec4 per swapchain pixel) -----
    NodeHandle ssboNode = renderGraph->AddNode<StorageBufferNodeType>("as_ssbo");

    // ----- Compute path A: fill -----
    NodeHandle fillShaderLib   = renderGraph->AddNode<ShaderLibraryNodeType>("as_fill_shader");
    NodeHandle fillGatherer    = renderGraph->AddNode<DescriptorResourceGathererNodeType>("as_fill_gatherer");
    NodeHandle fillDescSet     = renderGraph->AddNode<DescriptorSetNodeType>("as_fill_descriptors");
    NodeHandle fillPipeline    = renderGraph->AddNode<ComputePipelineNodeType>("as_fill_pipeline");

    // ----- Compute path B: post (in-place RMW) -----
    NodeHandle postShaderLib   = renderGraph->AddNode<ShaderLibraryNodeType>("as_post_shader");
    NodeHandle postGatherer    = renderGraph->AddNode<DescriptorResourceGathererNodeType>("as_post_gatherer");
    NodeHandle postDescSet     = renderGraph->AddNode<DescriptorSetNodeType>("as_post_descriptors");
    NodeHandle postPipeline    = renderGraph->AddNode<ComputePipelineNodeType>("as_post_pipeline");

    // ----- Graphics path C: fullscreen present -----
    NodeHandle gfxShaderLib    = renderGraph->AddNode<ShaderLibraryNodeType>("as_gfx_shader");
    NodeHandle gfxGatherer     = renderGraph->AddNode<DescriptorResourceGathererNodeType>("as_gfx_gatherer");
    NodeHandle gfxDescSet      = renderGraph->AddNode<DescriptorSetNodeType>("as_gfx_descriptors");
    NodeHandle renderPassNode  = renderGraph->AddNode<RenderPassNodeType>("as_render_pass");
    NodeHandle framebufferNode = renderGraph->AddNode<FramebufferNodeType>("as_framebuffer");
    NodeHandle gfxPipeline     = renderGraph->AddNode<GraphicsPipelineNodeType>("as_gfx_pipeline");

    // ----- The generic pass group + present -----
    NodeHandle passGroupNode   = renderGraph->AddNode<PassGroupNodeType>("as_pass_group");
    NodeHandle presentNode     = renderGraph->AddNode<PresentNodeType>("as_present");

    // ===================================================================
    // Parameters
    // ===================================================================
    auto* window = static_cast<WindowNode*>(renderGraph->GetInstance(windowNode));
    window->SetParameter(WindowNodeConfig::PARAM_WIDTH, static_cast<uint32_t>(width));
    window->SetParameter(WindowNodeConfig::PARAM_HEIGHT, static_cast<uint32_t>(height));
    auto* device = static_cast<DeviceNode*>(renderGraph->GetInstance(deviceNode));
    device->SetParameter(DeviceNodeConfig::PARAM_GPU_INDEX, 0u);

    // SSBO sized to the swapchain extent: vec4 (16 bytes) per pixel. Connecting
    // SWAPCHAIN_INFO (below) drives the size from the live extent and re-sizes via
    // the swapchain resize/recompile cascade.
    auto* ssbo = static_cast<StorageBufferNode*>(renderGraph->GetInstance(ssboNode));
    ssbo->SetParameter(StorageBufferNodeConfig::PARAM_BYTES_PER_PIXEL,
                       static_cast<uint32_t>(4 * sizeof(float)));

    // Color-only render pass: Undefined -> PresentSrc, DONT_CARE load.
    // The fragment pass OVERWRITES every pixel of the color attachment from the SSBO
    // (fullscreen triangle, viewport = full extent), so prior attachment contents are
    // irrelevant — there is nothing to LOAD. Using initialLayout=Undefined lets the
    // render pass perform a valid layout transition from whatever the swapchain image
    // currently is (Undefined on first acquire, PresentSrc after) to PresentSrc, with no
    // false "expects General" expectation and no need for a separately baked image barrier.
    // (P4 scope: only SSBO BUFFER hazards are auto-baked; the image is handled by the
    // render pass's own attachment layout ops, the standard swapchain pattern.)
    auto* renderPass = static_cast<RenderPassNode*>(renderGraph->GetInstance(renderPassNode));
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_COLOR_LOAD_OP, AttachmentLoadOp::DontCare);
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_COLOR_STORE_OP, AttachmentStoreOp::Store);
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_INITIAL_LAYOUT, ImageLayout::Undefined);
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_FINAL_LAYOUT, ImageLayout::PresentSrc);
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_SAMPLES, 1u);

    auto* framebuffer = static_cast<FramebufferNode*>(renderGraph->GetInstance(framebufferNode));
    framebuffer->SetParameter(FramebufferNodeConfig::PARAM_LAYERS, 1u);

    // Fullscreen-triangle graphics pipeline: no vertex input, no depth, no cull.
    auto* gfxPipe = static_cast<GraphicsPipelineNode*>(renderGraph->GetInstance(gfxPipeline));
    gfxPipe->SetParameter(GraphicsPipelineNodeConfig::ENABLE_DEPTH_TEST, false);
    gfxPipe->SetParameter(GraphicsPipelineNodeConfig::ENABLE_DEPTH_WRITE, false);
    gfxPipe->SetParameter(GraphicsPipelineNodeConfig::ENABLE_VERTEX_INPUT, false);
    gfxPipe->SetParameter(GraphicsPipelineNodeConfig::CULL_MODE, std::string("None"));
    gfxPipe->SetParameter(GraphicsPipelineNodeConfig::POLYGON_MODE, std::string("Fill"));
    gfxPipe->SetParameter(GraphicsPipelineNodeConfig::TOPOLOGY, std::string("TriangleList"));

    auto* present = static_cast<PresentNode*>(renderGraph->GetInstance(presentNode));
    present->SetParameter(PresentNodeConfig::WAIT_FOR_IDLE, true);

    // Shader programs.
    RegisterComputeShader(static_cast<ShaderLibraryNode*>(renderGraph->GetInstance(fillShaderLib)),
                          "autosync_fill.comp", "AutoSyncFill");
    RegisterComputeShader(static_cast<ShaderLibraryNode*>(renderGraph->GetInstance(postShaderLib)),
                          "autosync_post.comp", "AutoSyncPost");
    RegisterGraphicsShader(static_cast<ShaderLibraryNode*>(renderGraph->GetInstance(gfxShaderLib)),
                           "autosync_fullscreen.vert", "autosync_present.frag", "AutoSyncPresent");

    // ===================================================================
    // Connections
    // ===================================================================
    ConnectionBatch batch(renderGraph);

    // --- Core infrastructure chain (mirrors BuildInstancingDemoGraph) ---
    batch.Connect(instanceNode, InstanceNodeConfig::INSTANCE, deviceNode, DeviceNodeConfig::INSTANCE_IN);
    batch.Connect(deviceNode, DeviceNodeConfig::INSTANCE_OUT, windowNode, WindowNodeConfig::INSTANCE);
    batch.Connect(windowNode, WindowNodeConfig::WINDOW, swapChainNode, SwapChainNodeConfig::WINDOW)
         .Connect(windowNode, WindowNodeConfig::WIDTH_OUT, swapChainNode, SwapChainNodeConfig::WIDTH)
         .Connect(windowNode, WindowNodeConfig::HEIGHT_OUT, swapChainNode, SwapChainNodeConfig::HEIGHT);
    batch.Connect(deviceNode, DeviceNodeConfig::INSTANCE_OUT, swapChainNode, SwapChainNodeConfig::INSTANCE)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, swapChainNode, SwapChainNodeConfig::VULKAN_DEVICE_IN);
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, frameSyncNode, FrameSyncNodeConfig::VULKAN_DEVICE);
    batch.Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX, swapChainNode, SwapChainNodeConfig::CURRENT_FRAME_INDEX)
         .Connect(frameSyncNode, FrameSyncNodeConfig::IN_FLIGHT_FENCE, swapChainNode, SwapChainNodeConfig::IN_FLIGHT_FENCE)  // per-image in-flight fence tracking
         .Connect(frameSyncNode, FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY, swapChainNode, SwapChainNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY);
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, commandPoolNode, CommandPoolNodeConfig::VULKAN_DEVICE_IN);

    // --- SSBO (extent-driven: device + swapchain) ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, ssboNode, StorageBufferNodeConfig::VULKAN_DEVICE_IN)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC, ssboNode, StorageBufferNodeConfig::SWAPCHAIN_INFO);

    // --- Helper to wire one descriptor path: shaderLib -> gatherer(binding 0 = SSBO) -> descSet ---
    auto wireDescriptorPath = [&](NodeHandle shaderLib, NodeHandle gatherer, NodeHandle descSet) {
        batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, shaderLib, ShaderLibraryNodeConfig::VULKAN_DEVICE_IN);
        // Gatherer: shader bundle (reflection) + the SSBO at binding 0.
        batch.Connect(shaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                      gatherer, DescriptorResourceGathererNodeConfig::SHADER_DATA_BUNDLE);
        batch.Connect(ssboNode, StorageBufferNodeConfig::STORAGE_BUFFER,
                      gatherer, 0,  // SSBO at binding 0
                      SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
        // Descriptor set: device + bundle + gathered resources + swapchain + image index.
        batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, descSet, DescriptorSetNodeConfig::VULKAN_DEVICE_IN)
             .Connect(shaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE, descSet, DescriptorSetNodeConfig::SHADER_DATA_BUNDLE)
             .Connect(gatherer, DescriptorResourceGathererNodeConfig::DESCRIPTOR_RESOURCES, descSet, DescriptorSetNodeConfig::DESCRIPTOR_RESOURCES)
             .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC, descSet, DescriptorSetNodeConfig::SWAPCHAIN_INFO)
             .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX, descSet, DescriptorSetNodeConfig::IMAGE_INDEX);
    };
    wireDescriptorPath(fillShaderLib, fillGatherer, fillDescSet);
    wireDescriptorPath(postShaderLib, postGatherer, postDescSet);
    wireDescriptorPath(gfxShaderLib,  gfxGatherer,  gfxDescSet);

    // --- Compute pipelines (fill, post): device + bundle + descriptor-set layout ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, fillPipeline, ComputePipelineNodeConfig::VULKAN_DEVICE_IN)
         .Connect(fillShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE, fillPipeline, ComputePipelineNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(fillDescSet, DescriptorSetNodeConfig::DESCRIPTOR_SET_LAYOUT, fillPipeline, ComputePipelineNodeConfig::DESCRIPTOR_SET_LAYOUT);
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, postPipeline, ComputePipelineNodeConfig::VULKAN_DEVICE_IN)
         .Connect(postShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE, postPipeline, ComputePipelineNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(postDescSet, DescriptorSetNodeConfig::DESCRIPTOR_SET_LAYOUT, postPipeline, ComputePipelineNodeConfig::DESCRIPTOR_SET_LAYOUT);

    // --- Render pass (color-only) + framebuffers over swapchain views ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, renderPassNode, RenderPassNodeConfig::VULKAN_DEVICE_IN)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC, renderPassNode, RenderPassNodeConfig::SWAPCHAIN_INFO);
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, framebufferNode, FramebufferNodeConfig::VULKAN_DEVICE_IN)
         .Connect(renderPassNode, RenderPassNodeConfig::RENDER_PASS, framebufferNode, FramebufferNodeConfig::RENDER_PASS)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC, framebufferNode, FramebufferNodeConfig::SWAPCHAIN_INFO);

    // --- Graphics pipeline (fullscreen): device + bundle + render pass + descriptor-set layout ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, gfxPipeline, GraphicsPipelineNodeConfig::VULKAN_DEVICE_IN)
         .Connect(gfxShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE, gfxPipeline, GraphicsPipelineNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(renderPassNode, RenderPassNodeConfig::RENDER_PASS, gfxPipeline, GraphicsPipelineNodeConfig::RENDER_PASS)
         .Connect(gfxDescSet, DescriptorSetNodeConfig::DESCRIPTOR_SET_LAYOUT, gfxPipeline, GraphicsPipelineNodeConfig::DESCRIPTOR_SET_LAYOUT);

    // --- PassGroupNode: 8 FrameSync/WSI inputs (copy of the composite wiring) ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, passGroupNode, PassGroupNodeConfig::VULKAN_DEVICE_IN)
         .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL, passGroupNode, PassGroupNodeConfig::COMMAND_POOL)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC, passGroupNode, PassGroupNodeConfig::SWAPCHAIN_INFO)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX, passGroupNode, PassGroupNodeConfig::IMAGE_INDEX)
         .Connect(frameSyncNode, FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY, passGroupNode, PassGroupNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY)
         .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX, passGroupNode, PassGroupNodeConfig::CURRENT_FRAME_INDEX)
         .Connect(frameSyncNode, FrameSyncNodeConfig::IN_FLIGHT_FENCE, passGroupNode, PassGroupNodeConfig::IN_FLIGHT_FENCE)
         .Connect(swapChainNode, SwapChainNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY, passGroupNode, PassGroupNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY);

    // --- PassGroupNode compile-ordering: one variadic edge per handle source ---
    // GUARANTEE (not "in practice"): the post-compile callback reads concrete handles from
    // all 9 handle-source nodes, so PassGroupNode MUST compile after every one of them. We
    // wire one Dependency-role VARIADIC edge (binding indices 0..8) from each source into
    // PassGroupNode. VariadicConnectionRule adds a real topology edge per connection, so
    // GraphTopology::TopologicalSort places PassGroupNode strictly after all 9 sources —
    // the callback has populated passes_ before CompileImpl runs. The edge VALUES are never
    // read (ordering only); we use each node's device pass-through (or the SSBO's BUFFER_SIZE,
    // which has no device-out). This is pass-count-agnostic and uses no artificial data deps.
    const SlotRoleModifier orderingDep(SlotRole::Dependency);
    batch.Connect(ssboNode,        StorageBufferNodeConfig::BUFFER_SIZE,         passGroupNode, 0u, orderingDep);
    batch.Connect(fillPipeline,    ComputePipelineNodeConfig::VULKAN_DEVICE_OUT, passGroupNode, 1u, orderingDep);
    batch.Connect(postPipeline,    ComputePipelineNodeConfig::VULKAN_DEVICE_OUT, passGroupNode, 2u, orderingDep);
    batch.Connect(gfxPipeline,     GraphicsPipelineNodeConfig::VULKAN_DEVICE_OUT, passGroupNode, 3u, orderingDep);
    batch.Connect(fillDescSet,     DescriptorSetNodeConfig::VULKAN_DEVICE_OUT,   passGroupNode, 4u, orderingDep);
    batch.Connect(postDescSet,     DescriptorSetNodeConfig::VULKAN_DEVICE_OUT,   passGroupNode, 5u, orderingDep);
    batch.Connect(gfxDescSet,      DescriptorSetNodeConfig::VULKAN_DEVICE_OUT,   passGroupNode, 6u, orderingDep);
    batch.Connect(renderPassNode,  RenderPassNodeConfig::VULKAN_DEVICE_OUT,      passGroupNode, 7u, orderingDep);
    batch.Connect(framebufferNode, FramebufferNodeConfig::VULKAN_DEVICE_OUT,     passGroupNode, 8u, orderingDep);

    // --- Present: waits on PassGroupNode's render-complete semaphore ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, presentNode, PresentNodeConfig::VULKAN_DEVICE_IN)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_HANDLE, presentNode, PresentNodeConfig::SWAPCHAIN)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX, presentNode, PresentNodeConfig::IMAGE_INDEX)
         .Connect(passGroupNode, PassGroupNodeConfig::RENDER_COMPLETE_SEMAPHORE, presentNode, PresentNodeConfig::RENDER_COMPLETE_SEMAPHORE)
         .Connect(swapChainNode, SwapChainNodeConfig::PRESENT_FENCES_ARRAY, presentNode, PresentNodeConfig::PRESENT_FENCE_ARRAY);

    size_t connectionCount = batch.GetConnectionCount();
    batch.RegisterAll();

    // ===================================================================
    // PHASE 2: Compile-time handle resolution (post-compile callback)
    // ===================================================================
    // PassGroupNode needs CONCRETE pipeline/render-pass/framebuffer/descriptor-set
    // handles, which only exist after those nodes compile. We fill its pass list in a
    // post-compile callback that fires once ALL handle-source nodes report Compiled
    // (callbacks fire after each node compiles, in topological order). The COMPILE_AFTER
    // edge guarantees PassGroupNode compiles after this fill completes.
    RenderGraph* graph = renderGraph;
    renderGraph->RegisterPostNodeCompileCallback(
        [this, graph, ssboNode, fillPipeline, postPipeline, gfxPipeline,
         fillDescSet, postDescSet, gfxDescSet, renderPassNode, framebufferNode,
         passGroupNode](NodeInstance* /*justCompiled*/) {

            auto* pg = static_cast<PassGroupNode*>(graph->GetInstance(passGroupNode));
            if (!pg || pg->PassCount() > 0) {
                return;  // already assembled, or node missing
            }

            // All handle-source nodes that must be compiled before we can read handles.
            // The callback fires after each node is marked Compiled; once the LAST of these
            // sources reports Compiled, every handle is available and we assemble. PassGroupNode
            // is topologically ordered after all 9 (one Dependency-role variadic edge each), so
            // this assembly always runs before PassGroupNode::CompileImpl.
            const NodeHandle sources[] = {
                ssboNode, fillPipeline, postPipeline, gfxPipeline,
                fillDescSet, postDescSet, gfxDescSet, renderPassNode, framebufferNode
            };
            for (NodeHandle h : sources) {
                NodeInstance* n = graph->GetInstance(h);
                if (!n || n->GetState() != NodeState::Compiled) {
                    return;  // not all sources ready yet — wait for a later callback
                }
            }

            // ---- Read concrete handles from compiled node outputs ----
            auto outHandle = [&](NodeHandle h, uint32_t slotIndex, auto tag) {
                using T = decltype(tag);
                Resource* res = graph->GetInstance(h)->GetOutput(slotIndex, 0);
                return res ? res->GetHandle<T>() : T{};
            };

            VkPipeline       fillPipe = outHandle(fillPipeline, ComputePipelineNodeConfig::PIPELINE_Slot::index,        VkPipeline{});
            VkPipelineLayout fillLo   = outHandle(fillPipeline, ComputePipelineNodeConfig::PIPELINE_LAYOUT_Slot::index, VkPipelineLayout{});
            VkPipeline       postPipe = outHandle(postPipeline, ComputePipelineNodeConfig::PIPELINE_Slot::index,        VkPipeline{});
            VkPipelineLayout postLo   = outHandle(postPipeline, ComputePipelineNodeConfig::PIPELINE_LAYOUT_Slot::index, VkPipelineLayout{});
            VkPipeline       gfxPipeH = outHandle(gfxPipeline,  GraphicsPipelineNodeConfig::PIPELINE_Slot::index,       VkPipeline{});
            VkPipelineLayout gfxLo    = outHandle(gfxPipeline,  GraphicsPipelineNodeConfig::PIPELINE_LAYOUT_Slot::index, VkPipelineLayout{});
            VkRenderPass     rpH      = outHandle(renderPassNode, RenderPassNodeConfig::RENDER_PASS_Slot::index,         VkRenderPass{});

            // Per-pipeline descriptor sets (each set is allocated per swapchain image).
            // DescriptorSetNode declares DESCRIPTOR_SETS as `const std::vector<VkDescriptorSet>&`
            // (a reference output: the Resource stores a pointer to the node's live member, no copy).
            // GetHandle MUST be queried with that exact declared type — TypeToTag maps `const T&` to
            // ConstRefTag<T> but `T` (value) to ValueTag<T>, so querying the value type finds nothing
            // and yields an EMPTY vector, which would skip vkCmdBindDescriptorSets and trip
            // VUID-vkCmd{Dispatch,Draw}-None-08600. Read the reference, then copy into the pass.
            using DescSetsRef = DescriptorSetNodeConfig::DESCRIPTOR_SETS_Slot::Type;  // const std::vector<VkDescriptorSet>&
            auto readDescSets = [&](NodeHandle h) -> std::vector<VkDescriptorSet> {
                Resource* res = graph->GetInstance(h)->GetOutput(DescriptorSetNodeConfig::DESCRIPTOR_SETS_Slot::index, 0);
                if (!res || !res->IsValid()) return {};
                return res->GetHandle<DescSetsRef>();  // ConstRefTag round-trip; copy on return
            };
            std::vector<VkDescriptorSet> fillSets = readDescSets(fillDescSet);
            std::vector<VkDescriptorSet> postSets = readDescSets(postDescSet);
            std::vector<VkDescriptorSet> gfxSets  = readDescSets(gfxDescSet);

            // Framebuffers (one per swapchain image).
            Resource* fbRes = graph->GetInstance(framebufferNode)->GetOutput(FramebufferNodeConfig::FRAMEBUFFERS_Slot::index, 0);
            std::vector<VkFramebuffer> framebuffers = fbRes ? fbRes->GetHandle<std::vector<VkFramebuffer>>() : std::vector<VkFramebuffer>{};

            // SSBO Resource* — the node-local hazard-correlation identity. The SAME pointer
            // is used in all three passes' accesses so BuildPassGroupSchedule correlates them.
            Resource* ssboRes = graph->GetInstance(ssboNode)->GetOutput(StorageBufferNodeConfig::STORAGE_BUFFER_Slot::index, 0);

            // Extent for workgroup counts + push constants. The swapchain is created at
            // the window size, so width/height are the authoritative extent here; the SSBO
            // (sized extent*16 via its swapchain input) and the framebuffers agree by
            // construction, and both re-derive on resize through the recompile cascade.
            uint32_t w = static_cast<uint32_t>(this->width);
            uint32_t h = static_cast<uint32_t>(this->height);

            const uint32_t localSize = 8u;
            const uint32_t groupsX = (w + localSize - 1u) / localSize;
            const uint32_t groupsY = (h + localSize - 1u) / localSize;

            // ---- Assemble the three passes (SAME ssboRes across all) ----
            ComputePassStep fill{};
            fill.pipeline       = fillPipe;
            fill.layout         = fillLo;
            fill.descriptorSets = fillSets;
            fill.firstSet       = 0;
            fill.pushConstants  = PushConstantData{ PackExtentPC(w, h), VK_SHADER_STAGE_COMPUTE_BIT, 0 };
            fill.workGroupCount = { groupsX, groupsY, 1u };
            fill.accesses       = { { ssboRes, AccessKind::ComputeStorageWrite, false } };
            fill.debugName      = "autosync_fill";

            ComputePassStep post{};
            post.pipeline       = postPipe;
            post.layout         = postLo;
            post.descriptorSets = postSets;
            post.firstSet       = 0;
            post.pushConstants  = PushConstantData{ PackExtentPC(w, h), VK_SHADER_STAGE_COMPUTE_BIT, 0 };
            post.workGroupCount = { groupsX, groupsY, 1u };
            post.accesses       = { { ssboRes, AccessKind::ComputeStorageReadWrite, false } };
            post.debugName      = "autosync_post";

            RenderPassStep draw{};
            draw.pipeline       = gfxPipeH;
            draw.layout         = gfxLo;
            draw.renderPass     = rpH;
            draw.framebuffers   = framebuffers;
            draw.renderArea     = { w, h };
            draw.clearValues    = {};  // LOAD op — no clear
            draw.descriptorSets = gfxSets;
            draw.firstSet       = 0;
            draw.pushConstants  = PushConstantData{ PackExtentPC(w, h), VK_SHADER_STAGE_FRAGMENT_BIT, 0 };
            draw.vertexCount    = 3;
            draw.instanceCount  = 1;
            draw.accesses       = { { ssboRes, AccessKind::FragmentStorageRead, false } };
            draw.debugName      = "autosync_present";

            pg->AddComputePass(std::move(fill));
            pg->AddComputePass(std::move(post));
            pg->AddRenderPass(std::move(draw));

            mainLogger->Info("[BuildAutoSyncDemoGraph] PassGroupNode assembled: 3 passes (fill->post->present), "
                             "extent " + std::to_string(w) + "x" + std::to_string(h));
        });

    mainLogger->Info("Auto-sync demo graph built (" + std::to_string(renderGraph->GetNodeCount()) +
                     " nodes, " + std::to_string(connectionCount) + " connections)");
}
