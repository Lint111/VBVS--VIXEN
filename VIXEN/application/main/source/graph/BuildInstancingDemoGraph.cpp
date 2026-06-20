// BuildInstancingDemoGraph -- extracted from VulkanGraphApplication.cpp (M4: per-subgraph construction TU).
// Editing a node config now recompiles only the subgraph TU(s) wiring it, not the
// app's lifecycle code. Node includes below are derived from this subgraph's wiring.
#include "VulkanGraphApplication.h"
#include "Connection/ConnectionModifier.h"
#include "Connection/Modifiers/FieldExtractionModifier.h"
#include "Core/NodeRegistration.h"
#include "MeshData.h"
// --- nodes this subgraph wires ---
#include "Data/Nodes/CommandPoolNodeConfig.h"
#include "Data/Nodes/DepthBufferNodeConfig.h"
#include "Data/Nodes/DescriptorResourceGathererNodeConfig.h"
#include "Data/Nodes/DescriptorSetNodeConfig.h"
#include "Data/Nodes/DeviceNodeConfig.h"
#include "Data/Nodes/DynamicInstanceBufferNodeConfig.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "Data/Nodes/FramebufferNodeConfig.h"
#include "Data/Nodes/GeometryRenderNodeConfig.h"
#include "Data/Nodes/GraphicsPipelineNodeConfig.h"
#include "Data/Nodes/InstanceNodeConfig.h"
#include "Data/Nodes/MvpUniformNodeConfig.h"
#include "Data/Nodes/PresentNodeConfig.h"
#include "Data/Nodes/RenderPassNodeConfig.h"
#include "Data/Nodes/ShaderLibraryNodeConfig.h"
#include "Data/Nodes/SwapChainNodeConfig.h"
#include "Data/Nodes/TextureLoaderNodeConfig.h"
#include "Data/Nodes/VertexBufferNodeConfig.h"
#include "Data/Nodes/WindowNodeConfig.h"
#include "Nodes/CommandPoolNode.h"
#include "Nodes/DepthBufferNode.h"
#include "Nodes/DescriptorResourceGathererNode.h"
#include "Nodes/DescriptorSetNode.h"
#include "Nodes/DeviceNode.h"
#include "Nodes/DynamicInstanceBufferNode.h"
#include "Nodes/FrameSyncNode.h"
#include "Nodes/FramebufferNode.h"
#include "Nodes/GeometryRenderNode.h"
#include "Nodes/GraphicsPipelineNode.h"
#include "Nodes/InstanceNode.h"
#include "Nodes/MvpUniformNode.h"
#include "Nodes/PresentNode.h"
#include "Nodes/RenderPassNode.h"
#include "Nodes/ShaderLibraryNode.h"
#include "Nodes/SwapChainNode.h"
#include "Nodes/TextureLoaderNode.h"
#include "Nodes/VertexBufferNode.h"
#include "Nodes/WindowNode.h"

void VulkanGraphApplication::BuildInstancingDemoGraph() {
    using namespace Vixen::RenderGraph;
    mainLogger->Info("Building instanced-cube raster demo graph (AR#31)");

    // gridDim^2 cubes from a single mesh, one per-instance model matrix in an SSBO indexed
    // by gl_InstanceIndex. Isolated env-gated graph; mirrors BuildUIGraph's self-contained
    // infrastructure (own device/window/swapchain/etc.) so the live voxel path is untouched.
    constexpr uint32_t kGridDim = 8u;                 // 8x8 = 64 instances
    const uint32_t     kInstanceCount = kGridDim * kGridDim;

    // ----- Infrastructure -----
    NodeHandle instanceNode    = renderGraph->AddNode<InstanceNodeType>("inst_instance");
    NodeHandle deviceNode      = renderGraph->AddNode<DeviceNodeType>("inst_device");
    NodeHandle windowNode      = renderGraph->AddNode<WindowNodeType>("main_window");
    NodeHandle swapChainNode   = renderGraph->AddNode<SwapChainNodeType>("inst_swapchain");
    NodeHandle commandPoolNode = renderGraph->AddNode<CommandPoolNodeType>("inst_cmd_pool");
    NodeHandle frameSyncNode   = renderGraph->AddNode<FrameSyncNodeType>("inst_frame_sync");

    // ----- Raster resources -----
    NodeHandle depthBufferNode = renderGraph->AddNode<DepthBufferNodeType>("inst_depth");
    NodeHandle renderPassNode  = renderGraph->AddNode<RenderPassNodeType>("inst_render_pass");
    NodeHandle framebufferNode = renderGraph->AddNode<FramebufferNodeType>("inst_framebuffer");
    NodeHandle vertexBufferNode = renderGraph->AddNode<VertexBufferNodeType>("inst_cube_vb");
    NodeHandle instanceBufferNode = renderGraph->AddNode<DynamicInstanceBufferNodeType>("inst_buffer");
    NodeHandle mvpUniformNode  = renderGraph->AddNode<MvpUniformNodeType>("inst_mvp");
    NodeHandle textureNode     = renderGraph->AddNode<TextureLoaderNodeType>("inst_texture");
    NodeHandle shaderLibNode   = renderGraph->AddNode<ShaderLibraryNodeType>("inst_shader_lib");
    NodeHandle descGathererNode = renderGraph->AddNode<DescriptorResourceGathererNodeType>("inst_desc_gatherer");
    NodeHandle descriptorSetNode = renderGraph->AddNode<DescriptorSetNodeType>("inst_descriptors");
    NodeHandle pipelineNode    = renderGraph->AddNode<GraphicsPipelineNodeType>("inst_pipeline");
    NodeHandle geometryRenderNode = renderGraph->AddNode<GeometryRenderNodeType>("inst_render");
    NodeHandle presentNode     = renderGraph->AddNode<PresentNodeType>("inst_present");

    // ===================================================================
    // Parameters
    // ===================================================================
    auto* window = static_cast<WindowNode*>(renderGraph->GetInstance(windowNode));
    window->SetParameter(WindowNodeConfig::PARAM_WIDTH, width);
    window->SetParameter(WindowNodeConfig::PARAM_HEIGHT, height);
    auto* device = static_cast<DeviceNode*>(renderGraph->GetInstance(deviceNode));
    device->SetParameter(DeviceNodeConfig::PARAM_GPU_INDEX, 0u);

    // Cube vertex buffer (36 verts, pos vec4 + UV vec2; same layout as the disabled Draw path).
    auto* vertexBuffer = static_cast<VertexBufferNode*>(renderGraph->GetInstance(vertexBufferNode));
    vertexBuffer->SetParameter(VertexBufferNodeConfig::PARAM_VERTEX_COUNT, 36u);
    vertexBuffer->SetParameter(VertexBufferNodeConfig::PARAM_VERTEX_STRIDE, sizeof(VertexWithUV));
    vertexBuffer->SetParameter(VertexBufferNodeConfig::PARAM_USE_TEXTURE, true);  // location 1 = vec2 UV
    vertexBuffer->SetParameter(VertexBufferNodeConfig::PARAM_INDEX_COUNT, 0u);    // non-indexed draw

    // Per-instance model-matrix SSBO: gridDim^2 matrices on a planar grid, ANIMATED per-frame
    // (AR#33) — DynamicInstanceBufferNode rewrites a ring-buffered SSBO each frame so the cubes spin.
    auto* instanceBuffer = static_cast<DynamicInstanceBufferNode*>(renderGraph->GetInstance(instanceBufferNode));
    instanceBuffer->SetParameter(DynamicInstanceBufferNodeConfig::PARAM_GRID_DIM, kGridDim);
    instanceBuffer->SetParameter(DynamicInstanceBufferNodeConfig::PARAM_SPACING, 2.5f);
    instanceBuffer->SetParameter(DynamicInstanceBufferNodeConfig::PARAM_ROTATION_SPEED, 0.02f);
    if (auto* ibLogger = instanceBuffer->GetLogger()) {
        ibLogger->SetEnabled(true);
        ibLogger->SetTerminalOutput(true);
    }

    // MVP uniform buffer (binding 0): proj*view for the general Draw.vert. Draw.vert applies the
    // per-instance model matrix and the Vulkan Y-flip/Z-remap itself, so this is plain proj*view.
    auto* mvpUniform = static_cast<MvpUniformNode*>(renderGraph->GetInstance(mvpUniformNode));
    mvpUniform->SetParameter(MvpUniformNodeConfig::PARAM_FOV_DEGREES, 45.0f);
    mvpUniform->SetParameter(MvpUniformNodeConfig::PARAM_ASPECT,
                             static_cast<float>(width) / static_cast<float>(height));
    mvpUniform->SetParameter(MvpUniformNodeConfig::PARAM_NEAR, 0.1f);
    mvpUniform->SetParameter(MvpUniformNodeConfig::PARAM_FAR, 100.0f);
    // Pull the camera back so the whole gridDim^2 grid (spacing 2.5) is in frame.
    mvpUniform->SetParameter(MvpUniformNodeConfig::PARAM_CAMERA_DISTANCE, 30.0f);

    // Albedo texture (binding 1): empty FILE_PATH => TextureLoaderNode generates a default
    // checkerboard, so the general Draw.frag `sampler2D tex` path runs with zero asset deps.
    auto* texture = static_cast<TextureLoaderNode*>(renderGraph->GetInstance(textureNode));
    texture->SetParameter(TextureLoaderNodeConfig::SAMPLER_FILTER, std::string("Linear"));
    texture->SetParameter(TextureLoaderNodeConfig::SAMPLER_ADDRESS_MODE, std::string("Repeat"));
    if (auto* texLogger = texture->GetLogger()) {
        texLogger->SetEnabled(true);
        texLogger->SetTerminalOutput(true);
    }

    // Depth attachment.
    auto* depthBuffer = static_cast<DepthBufferNode*>(renderGraph->GetInstance(depthBufferNode));
    depthBuffer->SetParameter(DepthBufferNodeConfig::PARAM_FORMAT,
                              static_cast<uint32_t>(VK_FORMAT_D32_SFLOAT));

    // Color+depth render pass that ends in present-src.
    auto* renderPass = static_cast<RenderPassNode*>(renderGraph->GetInstance(renderPassNode));
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_COLOR_LOAD_OP, AttachmentLoadOp::Clear);
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_COLOR_STORE_OP, AttachmentStoreOp::Store);
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_DEPTH_LOAD_OP, AttachmentLoadOp::Clear);
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_DEPTH_STORE_OP, AttachmentStoreOp::DontCare);
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_INITIAL_LAYOUT, ImageLayout::Undefined);
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_FINAL_LAYOUT, ImageLayout::PresentSrc);
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_SAMPLES, 1u);

    auto* framebuffer = static_cast<FramebufferNode*>(renderGraph->GetInstance(framebufferNode));
    framebuffer->SetParameter(FramebufferNodeConfig::PARAM_LAYERS, 1u);

    // Graphics pipeline: depth-tested, back-face-culled, filled triangles, CCW front.
    auto* pipeline = static_cast<GraphicsPipelineNode*>(renderGraph->GetInstance(pipelineNode));
    pipeline->SetParameter(GraphicsPipelineNodeConfig::ENABLE_DEPTH_TEST, true);
    pipeline->SetParameter(GraphicsPipelineNodeConfig::ENABLE_DEPTH_WRITE, true);
    pipeline->SetParameter(GraphicsPipelineNodeConfig::ENABLE_VERTEX_INPUT, true);
    pipeline->SetParameter(GraphicsPipelineNodeConfig::CULL_MODE, std::string("Back"));
    pipeline->SetParameter(GraphicsPipelineNodeConfig::POLYGON_MODE, std::string("Fill"));
    pipeline->SetParameter(GraphicsPipelineNodeConfig::TOPOLOGY, std::string("TriangleList"));
    pipeline->SetParameter(GraphicsPipelineNodeConfig::FRONT_FACE, std::string("CounterClockwise"));

    // Geometry render: 36 verts, kInstanceCount instances (matches PARAM_GRID_DIM^2).
    auto* geometryRender = static_cast<GeometryRenderNode*>(renderGraph->GetInstance(geometryRenderNode));
    geometryRender->SetParameter(GeometryRenderNodeConfig::VERTEX_COUNT, 36u);
    geometryRender->SetParameter(GeometryRenderNodeConfig::INSTANCE_COUNT, kInstanceCount);
    geometryRender->SetParameter(GeometryRenderNodeConfig::FIRST_VERTEX, 0u);
    geometryRender->SetParameter(GeometryRenderNodeConfig::FIRST_INSTANCE, 0u);
    geometryRender->SetParameter(GeometryRenderNodeConfig::USE_INDEX_BUFFER, false);
    geometryRender->SetParameter(GeometryRenderNodeConfig::CLEAR_COLOR_R, 0.02f);
    geometryRender->SetParameter(GeometryRenderNodeConfig::CLEAR_COLOR_G, 0.02f);
    geometryRender->SetParameter(GeometryRenderNodeConfig::CLEAR_COLOR_B, 0.08f);
    geometryRender->SetParameter(GeometryRenderNodeConfig::CLEAR_COLOR_A, 1.0f);
    geometryRender->SetParameter(GeometryRenderNodeConfig::CLEAR_DEPTH, 1.0f);
    geometryRender->SetParameter(GeometryRenderNodeConfig::CLEAR_STENCIL, 0u);
    if (auto* grLogger = geometryRender->GetLogger()) {
        grLogger->SetEnabled(true);
        grLogger->SetTerminalOutput(true);
    }

    auto* present = static_cast<PresentNode*>(renderGraph->GetInstance(presentNode));
    present->SetParameter(PresentNodeConfig::WAIT_FOR_IDLE, true);

    // General shader program: the reusable Draw.vert + Draw.frag (AR#31). Reflects THREE bindings:
    //   0 = uniform buffer  (mvp, from MvpUniformNode)
    //   1 = combined sampler (sampler2D tex, from TextureLoaderNode's ImageSamplerPair)
    //   2 = storage buffer  (instance model[] SSBO, from InstanceBufferNode, indexed by gl_InstanceIndex)
    // This makes the general triangle path the real, instancing-capable one rather than a throwaway
    // demo pair. (Copied from the BuildRenderGraph Draw.vert/Draw.frag builder lambda.)
    auto* shaderLib = static_cast<ShaderLibraryNode*>(renderGraph->GetInstance(shaderLibNode));
    shaderLib->RegisterShaderBuilder([](int vulkanVer, int spirvVer) {
        ShaderManagement::ShaderBundleBuilder builder;

        std::vector<std::filesystem::path> possiblePaths = {
#ifdef VIXEN_SHADER_SOURCE_DIR
            VIXEN_SHADER_SOURCE_DIR "/Draw.vert",
#endif
            "shaders/Draw.vert",
            "Draw.vert",
            "../shaders/Draw.vert"
        };
        std::filesystem::path vertPath, fragPath;
        for (const auto& path : possiblePaths) {
            if (std::filesystem::exists(path)) {
                vertPath = path;
                fragPath = path.parent_path() / "Draw.frag";
                break;
            }
        }
        if (vertPath.empty()) {
            throw std::runtime_error("Draw.vert not found - check shader search paths");
        }

        ShaderManagement::SdiGeneratorConfig sdiConfig;
        sdiConfig.outputDirectory = std::filesystem::current_path() / "generated" / "sdi";
        sdiConfig.namespacePrefix = "ShaderInterface";
        sdiConfig.generateComments = true;

        builder.SetProgramName("Draw_Shader")
               .SetSdiConfig(sdiConfig)
               .EnableSdiGeneration(true)
               .SetTargetVulkanVersion(vulkanVer)
               .SetTargetSpirvVersion(spirvVer)
               .AddStageFromFile(ShaderManagement::ShaderStage::Vertex, vertPath, "main")
               .AddStageFromFile(ShaderManagement::ShaderStage::Fragment, fragPath, "main");
        return builder;
    });

    // ===================================================================
    // Connections
    // ===================================================================
    ConnectionBatch batch(renderGraph);

    // --- Core infrastructure chain (mirrors BuildUIGraph) ---
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

    // --- Depth buffer ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, depthBufferNode, DepthBufferNodeConfig::VULKAN_DEVICE_IN)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC, depthBufferNode, DepthBufferNodeConfig::SWAPCHAIN_PUBLIC_VARS)
         .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL, depthBufferNode, DepthBufferNodeConfig::COMMAND_POOL);

    // --- Render pass (color + depth) ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, renderPassNode, RenderPassNodeConfig::VULKAN_DEVICE_IN)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC, renderPassNode, RenderPassNodeConfig::SWAPCHAIN_INFO)
         .Connect(depthBufferNode, DepthBufferNodeConfig::DEPTH_FORMAT, renderPassNode, RenderPassNodeConfig::DEPTH_FORMAT);

    // --- Framebuffer (wraps swapchain image views + depth attachment) ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, framebufferNode, FramebufferNodeConfig::VULKAN_DEVICE_IN)
         .Connect(renderPassNode, RenderPassNodeConfig::RENDER_PASS, framebufferNode, FramebufferNodeConfig::RENDER_PASS)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC, framebufferNode, FramebufferNodeConfig::SWAPCHAIN_INFO)
         .Connect(depthBufferNode, DepthBufferNodeConfig::DEPTH_IMAGE_VIEW, framebufferNode, FramebufferNodeConfig::DEPTH_ATTACHMENT);

    // --- Vertex + instance buffers + MVP UBO + texture ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, vertexBufferNode, VertexBufferNodeConfig::VULKAN_DEVICE_IN);
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, instanceBufferNode, DynamicInstanceBufferNodeConfig::VULKAN_DEVICE_IN);
    // AR#33: the dynamic instance buffer needs the per-frame ring index to pick which buffer to write/emit.
    batch.Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX, instanceBufferNode, DynamicInstanceBufferNodeConfig::CURRENT_FRAME_INDEX);
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, mvpUniformNode, MvpUniformNodeConfig::VULKAN_DEVICE_IN);
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, textureNode, TextureLoaderNodeConfig::VULKAN_DEVICE_IN);

    // --- Shader library ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, shaderLibNode, ShaderLibraryNodeConfig::VULKAN_DEVICE_IN);

    // --- Descriptor path: gatherer (reflection-driven) → descriptor set ---
    // Draw.vert/Draw.frag reflect three bindings; wire each resource to its shader binding index:
    //   0 = MVP uniform buffer (MvpUniformNode)
    //   1 = combined image sampler (TextureLoaderNode's ImageSamplerPair — one connection carries
    //       BOTH the VkImageView and VkSampler the sampler2D needs; two separate view/sampler
    //       connections to the same binding would collapse and drop one handle)
    //   2 = instance model[] SSBO (InstanceBufferNode), indexed by gl_InstanceIndex
    // All Dependency|Execute: bound on first execute and refreshed if a source recompiles (resize).
    batch.Connect(shaderLibNode, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  descGathererNode, DescriptorResourceGathererNodeConfig::SHADER_DATA_BUNDLE);
    batch.Connect(mvpUniformNode, MvpUniformNodeConfig::MVP_BUFFER,
                  descGathererNode, 0,  // MVP uniform buffer at binding 0
                  SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(textureNode, TextureLoaderNodeConfig::TEXTURE_SAMPLER_PAIR,
                  descGathererNode, 1,  // Combined image sampler at binding 1 (view + sampler)
                  SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));
    batch.Connect(instanceBufferNode, DynamicInstanceBufferNodeConfig::INSTANCE_BUFFER,
                  descGathererNode, 2,  // Instances SSBO at binding 2 (ring buffer, re-emitted per frame)
                  SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, descriptorSetNode, DescriptorSetNodeConfig::VULKAN_DEVICE_IN)
         .Connect(shaderLibNode, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE, descriptorSetNode, DescriptorSetNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(descGathererNode, DescriptorResourceGathererNodeConfig::DESCRIPTOR_RESOURCES, descriptorSetNode, DescriptorSetNodeConfig::DESCRIPTOR_RESOURCES)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC, descriptorSetNode, DescriptorSetNodeConfig::SWAPCHAIN_INFO)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX, descriptorSetNode, DescriptorSetNodeConfig::IMAGE_INDEX);

    // --- Graphics pipeline ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, pipelineNode, GraphicsPipelineNodeConfig::VULKAN_DEVICE_IN)
         .Connect(shaderLibNode, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE, pipelineNode, GraphicsPipelineNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(renderPassNode, RenderPassNodeConfig::RENDER_PASS, pipelineNode, GraphicsPipelineNodeConfig::RENDER_PASS)
         .Connect(descriptorSetNode, DescriptorSetNodeConfig::DESCRIPTOR_SET_LAYOUT, pipelineNode, GraphicsPipelineNodeConfig::DESCRIPTOR_SET_LAYOUT);

    // --- Geometry render (the instanced draw) ---
    batch.Connect(renderPassNode, RenderPassNodeConfig::RENDER_PASS, geometryRenderNode, GeometryRenderNodeConfig::RENDER_PASS)
         .Connect(framebufferNode, FramebufferNodeConfig::FRAMEBUFFERS, geometryRenderNode, GeometryRenderNodeConfig::FRAMEBUFFERS)
         .Connect(pipelineNode, GraphicsPipelineNodeConfig::PIPELINE, geometryRenderNode, GeometryRenderNodeConfig::PIPELINE)
         .Connect(pipelineNode, GraphicsPipelineNodeConfig::PIPELINE_LAYOUT, geometryRenderNode, GeometryRenderNodeConfig::PIPELINE_LAYOUT)
         .Connect(descriptorSetNode, DescriptorSetNodeConfig::DESCRIPTOR_SETS, geometryRenderNode, GeometryRenderNodeConfig::DESCRIPTOR_SETS)
         .Connect(vertexBufferNode, VertexBufferNodeConfig::VERTEX_BUFFER, geometryRenderNode, GeometryRenderNodeConfig::VERTEX_BUFFER)
         .Connect(vertexBufferNode, VertexBufferNodeConfig::INDEX_BUFFER, geometryRenderNode, GeometryRenderNodeConfig::INDEX_BUFFER)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC, geometryRenderNode, GeometryRenderNodeConfig::SWAPCHAIN_INFO)
         .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL, geometryRenderNode, GeometryRenderNodeConfig::COMMAND_POOL)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, geometryRenderNode, GeometryRenderNodeConfig::VULKAN_DEVICE)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX, geometryRenderNode, GeometryRenderNodeConfig::IMAGE_INDEX)
         .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX, geometryRenderNode, GeometryRenderNodeConfig::CURRENT_FRAME_INDEX)
         .Connect(frameSyncNode, FrameSyncNodeConfig::IN_FLIGHT_FENCE, geometryRenderNode, GeometryRenderNodeConfig::IN_FLIGHT_FENCE)
         .Connect(frameSyncNode, FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY, geometryRenderNode, GeometryRenderNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY)
         .Connect(swapChainNode, SwapChainNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY, geometryRenderNode, GeometryRenderNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY);

    // --- Present ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, presentNode, PresentNodeConfig::VULKAN_DEVICE_IN)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_HANDLE, presentNode, PresentNodeConfig::SWAPCHAIN)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX, presentNode, PresentNodeConfig::IMAGE_INDEX)
         .Connect(geometryRenderNode, GeometryRenderNodeConfig::RENDER_COMPLETE_SEMAPHORE, presentNode, PresentNodeConfig::RENDER_COMPLETE_SEMAPHORE)
         .Connect(swapChainNode, SwapChainNodeConfig::PRESENT_FENCES_ARRAY, presentNode, PresentNodeConfig::PRESENT_FENCE_ARRAY);

    size_t connectionCount = batch.GetConnectionCount();
    batch.RegisterAll();
    mainLogger->Info("Instanced-cube demo graph built (" + std::to_string(renderGraph->GetNodeCount()) +
                     " nodes, " + std::to_string(connectionCount) + " connections, " +
                     std::to_string(kInstanceCount) + " instances)");
}
