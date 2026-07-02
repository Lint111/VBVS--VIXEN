// BuildRenderGraph -- extracted from VulkanGraphApplication.cpp (M4: per-subgraph construction TU).
// Editing a node config now recompiles only the subgraph TU(s) wiring it, not the
// app's lifecycle code. Node includes below are derived from this subgraph's wiring.
//
// SP2 body-octree render route (merged onto the M4 TU split): this subgraph builds the
// BodyOctreeSceneNode + BodyInstanceRayMarch.comp route (bindings 1/2/3/5/10) and the
// composite-HUD/UI-selection passes, so it also pulls the UI nodes. The include ORDER below
// is load-bearing: BodyOctreeSceneNode.h MUST precede UIRenderNode.h (and any RmlUi/robin_hood
// header) because BodyOctreeSceneNode.h transitively pulls gaia.h (ShellOctree ->
// LaineKarrasOctree -> ISVOStructure), whose std::hash<> specialisations must be visible before
// RmlUi's bundled robin_hood.h wraps them.
#include "VulkanGraphApplication.h"
#include <cmath>  // std::tan for the LOD ray-cone (raySizeCoef) computation
#include "Connection/ConnectionModifier.h"
#include "Connection/Modifiers/FieldExtractionModifier.h"
#include "Connection/Modifiers/AccumulationSortConfig.h"  // SEL-P3: accumulation-connect sort key (provider fan-in)
#include "Core/NodeRegistration.h"
#include "MeshData.h"
#include "VoxelRayMarchNames.h"  // Generated SDI shader-binding constants (VoxelRayMarch::*)
// --- nodes this subgraph wires ---
#include "Data/Nodes/BodyOctreeSceneNodeConfig.h"  // M-wire: sparse shell octree + instance SSBO config
#include "Data/Nodes/CameraNodeConfig.h"
#include "Data/Nodes/CommandPoolNodeConfig.h"
#include "Data/Nodes/ComputeDispatchNodeConfig.h"
#include "Data/Nodes/ComputePipelineNodeConfig.h"
#include "Data/Nodes/ConstantNodeConfig.h"
#include "Data/Nodes/DebugBufferReaderNodeConfig.h"
#include "Data/Nodes/DepthBufferNodeConfig.h"
#include "Data/Nodes/DescriptorResourceGathererNodeConfig.h"
#include "Data/Nodes/DescriptorSetNodeConfig.h"
#include "Data/Nodes/DeviceNodeConfig.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "Data/Nodes/FramebufferNodeConfig.h"
#include "Data/Nodes/GeometryRenderNodeConfig.h"
#include "Data/Nodes/GraphicsPipelineNodeConfig.h"
#include "Data/Nodes/InputNodeConfig.h"
#include "Data/Nodes/InstanceNodeConfig.h"
#include "Data/Nodes/LoopBridgeNodeConfig.h"
#include "Data/Nodes/PickIdTargetNodeConfig.h"
#include "Data/Nodes/PresentNodeConfig.h"
#include "Data/Nodes/PushConstantGathererNodeConfig.h"
#include "Data/Nodes/RenderPassNodeConfig.h"
#include "Data/Nodes/SelectionCoordinatorNodeConfig.h"
#include "Data/Nodes/ShaderLibraryNodeConfig.h"
#include "Data/Nodes/SwapChainNodeConfig.h"
#include "Data/Nodes/TextureLoaderNodeConfig.h"
#include "Data/Nodes/UIRenderNodeConfig.h"  // S0: composite-HUD render node config
#include "Data/Nodes/UISelectionProviderNodeConfig.h"  // SEL-P3: UI-domain selection provider config
#include "Data/Nodes/VertexBufferNodeConfig.h"
#include "Data/Nodes/VoxelGridNodeConfig.h"
#include "Data/Nodes/VoxelSelectionProviderNodeConfig.h"
#include "Data/Nodes/WindowNodeConfig.h"
// M-wire: BodyOctreeSceneNode.h MUST precede UIRenderNode.h (gaia std::hash before robin_hood) — see file header above.
#include "Nodes/BodyOctreeSceneNode.h"  // M-wire: sparse shell octree + instance SSBO
#include "Nodes/CameraNode.h"
#include "Nodes/CommandPoolNode.h"
#include "Nodes/ComputeDispatchNode.h"
#include "Nodes/ComputePipelineNode.h"
#include "Nodes/ConstantNodeType.h"
#include "Nodes/DebugBufferReaderNode.h"
#include "Nodes/DepthBufferNode.h"
#include "Nodes/DescriptorResourceGathererNode.h"
#include "Nodes/DescriptorSetNode.h"
#include "Nodes/DeviceNode.h"
#include "Nodes/FrameSyncNode.h"
#include "Nodes/FramebufferNode.h"
#include "Nodes/GeometryRenderNode.h"
#include "Nodes/GraphicsPipelineNode.h"
#include "Nodes/InputNode.h"
#include "Nodes/InstanceNode.h"
#include "Nodes/LoopBridgeNode.h"
#include "Nodes/PickIdTargetNode.h"
#include "Nodes/PresentNode.h"
#include "Nodes/PushConstantGathererNode.h"
#include "Nodes/RenderPassNode.h"
#include "Nodes/SelectionCoordinatorNode.h"
#include "Nodes/ShaderLibraryNode.h"
#include "Nodes/SwapChainNode.h"
#include "Nodes/TextureLoaderNode.h"
#include "Nodes/UIRenderNode.h"  // S0: composite-HUD render node (RmlUi) — AFTER BodyOctreeSceneNode.h
#include "Nodes/UISelectionProviderNode.h"  // SEL-P3: UI-domain selection provider (RmlUi hit-test)
#include "Nodes/VertexBufferNode.h"
#include "Nodes/VoxelGridNode.h"
#include "Nodes/VoxelSelectionProviderNode.h"
#include "Nodes/WindowNode.h"

void VulkanGraphApplication::BuildRenderGraph() {
    if (!renderGraph) {
        mainLogger->Error("Cannot build render graph: RenderGraph not initialized");
        return;
    }

    // S0: opt into the UI-only RmlUi demo graph via env var, leaving the voxel path untouched.
    if (std::getenv("VIXEN_UI_DEMO")) {
        mainLogger->Info("VIXEN_UI_DEMO set - building UI-only RmlUi demo graph");
        BuildUIGraph();
        return;
    }

    // AR#31: opt into the isolated instanced-cube raster demo via env var, leaving the
    // live voxel-compute path untouched.
    if (std::getenv("VIXEN_INSTANCING_DEMO")) {
        mainLogger->Info("VIXEN_INSTANCING_DEMO set - building instanced-cube raster demo graph");
        BuildInstancingDemoGraph();
        return;
    }

    // AR#21 P4: opt into the isolated auto-sync FrameGraph demo via env var. Proves
    // buffer-hazard auto-synchronization (compute->compute->render->present in ONE
    // command buffer via PassGroupNode). Leaves the live voxel-compute path untouched.
    if (std::getenv("VIXEN_AUTOSYNC_DEMO")) {
        mainLogger->Info("VIXEN_AUTOSYNC_DEMO set - building auto-sync FrameGraph demo graph");
        BuildAutoSyncDemoGraph();
        return;
    }

    // AR#21 P5b M2: opt into the multi-submit fan-in demo via env var. Proves
    // TIMELINE-ONLY ordering across separate compute submits: 2 independent producer
    // compute submits write 2 buffers, 1 consumer compute submit waits BOTH via 2 baked
    // timeline edges (NO binary handoff between them) + writes the swapchain. Leaves the
    // live voxel-compute path untouched.
    if (std::getenv("VIXEN_FANIN_DEMO")) {
        mainLogger->Info("VIXEN_FANIN_DEMO set - building multi-submit fan-in timeline demo graph");
        BuildFanInDemoGraph();
        return;
    }

    mainLogger->Info("Building complete render pipeline with typed connections");

    // ===================================================================
    // PHASE 1: Create all nodes
    // ===================================================================

    // --- Infrastructure Nodes ---
    NodeHandle instanceNode = renderGraph->AddNode<InstanceNodeType>( "main_instance");  // Phase 1.1
    NodeHandle windowNode = renderGraph->AddNode<WindowNodeType>("main_window");
    windowNode_ = windowNode;                        // store for GetWindowHandle() live lookup
    NodeHandle deviceNode = renderGraph->AddNode<DeviceNodeType>("main_device");
    NodeHandle swapChainNode = renderGraph->AddNode<SwapChainNodeType>("main_swapchain");
    NodeHandle commandPoolNode = renderGraph->AddNode<CommandPoolNodeType>("main_cmd_pool");

    // --- Resource Nodes ---
    // DISABLED FOR COMPUTE TEST: Graphics pipeline nodes
    /*
    NodeHandle depthBufferNode = renderGraph->AddNode("DepthBuffer", "depth_buffer");
    NodeHandle vertexBufferNode = renderGraph->AddNode("VertexBuffer", "triangle_vb");
    NodeHandle textureNode = renderGraph->AddNode("TextureLoader", "main_texture");

    // --- Rendering Configuration Nodes ---
    NodeHandle renderPassNode = renderGraph->AddNode("RenderPass", "main_pass");
    NodeHandle framebufferNode = renderGraph->AddNode("Framebuffer", "main_fb");
    NodeHandle shaderLibNode = renderGraph->AddNode("ShaderLibrary", "shader_lib");
    NodeHandle descriptorSetNode = renderGraph->AddNode("DescriptorSet", "main_descriptors");
    NodeHandle pipelineNode = renderGraph->AddNode("GraphicsPipeline", "triangle_pipeline");

    // Phase 1: ShaderLibraryNode replaces manual shader loading
    // Removed ConstantNode - ShaderLibraryNode outputs VulkanShader directly

    // --- Execution Nodes ---
    NodeHandle geometryRenderNode = renderGraph->AddNode("GeometryRender", "triangle_render");
    */
    NodeHandle presentNode = renderGraph->AddNode<PresentNodeType>("present");

    // --- Phase G: Compute Pipeline Nodes ---
    NodeHandle computeShaderLib = renderGraph->AddNode<ShaderLibraryNodeType>("compute_shader_lib");
    NodeHandle descriptorGatherer = renderGraph->AddNode<DescriptorResourceGathererNodeType>("compute_desc_gatherer");  // Phase H
    NodeHandle pushConstantGatherer = renderGraph->AddNode<PushConstantGathererNodeType>("push_constant_gatherer");  // Phase H
    NodeHandle computeDescriptorSet = renderGraph->AddNode<DescriptorSetNodeType>("compute_descriptors");
    NodeHandle computePipeline = renderGraph->AddNode<ComputePipelineNodeType>("test_compute_pipeline");
    NodeHandle computeDispatch = renderGraph->AddNode<ComputeDispatchNodeType>("test_dispatch");
    NodeHandle frameSyncNode = renderGraph->AddNode<FrameSyncNodeType>("frame_sync");

    // --- Ray Marching Nodes ---
    NodeHandle cameraNode = renderGraph->AddNode<CameraNodeType>("raymarch_camera");
    NodeHandle voxelGridNode = renderGraph->AddNode<VoxelGridNodeType>("voxel_grid");
    voxelGridNode_ = voxelGridNode;                  // store for MarkVoxelSceneDirty() (debug buffers only; not the render source post M-wire)

    // M-wire Task 8: sparse shell octree node — the live render source for bodies.
    // Outputs OCTREE_NODES/BRICKS/MATERIALS/CONFIG_BUFFER (identical slot names to VoxelGridNode,
    // so the descriptor wiring for bindings 1/2/3/5 just points here instead) plus
    // INSTANCE_BUFFER (binding 10) and INSTANCE_COUNT.
    NodeHandle bodyOctreeSceneNode = renderGraph->AddNode<BodyOctreeSceneNodeType>("body_octree_scene");
    bodyOctreeSceneNode_ = bodyOctreeSceneNode;      // store so SetBodyInstances() can forward to it

    // --- Input Node ---
    NodeHandle inputNode = renderGraph->AddNode<InputNodeType>("input_handler");
    inputNode_ = inputNode;                          // store for Update()'s live ProcessPendingInput() lookup

    // --- Pick ID Target (AR#35 GPU picking P1: R32_UINT storage-image ring at binding 9) ---
    NodeHandle pickIdTargetNode = renderGraph->AddNode<PickIdTargetNodeType>("pick_id_target");

    // --- Voxel Selection Provider (SEL-P2: providers are nodes) — on a click edge it reads back the
    // center pick-ID texel from PickIdTargetNode's ID image, decodes brick/voxel, and emits a
    // SelectionCandidate into the coordinator's MultiConnect candidate slot. ---
    NodeHandle voxelSelectionProviderNode = renderGraph->AddNode<VoxelSelectionProviderNodeType>("voxel_selection_provider");
    // Provider HIT/miss is user-facing; enable its logger to the terminal (defaults DISABLED).
    if (auto* provInst = renderGraph->GetInstance(voxelSelectionProviderNode)) {
        if (auto* pl = provInst->GetLogger()) { pl->SetEnabled(true); pl->SetTerminalOutput(true); }
    }

    // --- UI Selection Provider (SEL-P3: providers are nodes) — on a click edge it hit-tests the HUD's
    // Rml::Context at the cursor and emits a SelectionCandidate (priority 10) into the coordinator's
    // gather slot, so a HUD click OCCLUDES the voxel pick (priority 0). It reads the live context from
    // the UI composite node via SetUiRenderNode (wired below, once uiCompositeNode exists). ---
    NodeHandle uiSelectionProviderNode = renderGraph->AddNode<UISelectionProviderNodeType>("ui_selection_provider");
    uiSelectionProviderNode_ = uiSelectionProviderNode;   // store for GetUiSelectionProviderNode() live lookup (host drains HUD clicks)
    // Provider HIT/miss is user-facing; enable its logger to the terminal (defaults DISABLED).
    if (auto* uiProvInst = renderGraph->GetInstance(uiSelectionProviderNode)) {
        if (auto* pl = uiProvInst->GetLogger()) { pl->SetEnabled(true); pl->SetTerminalOutput(true); }
    }

    // --- Selection Coordinator (SEL-P2: engine-wide selection; gathers provider candidates via a
    // MultiConnect slot, priority-resolves, and owns the durable SelectionSet) ---
    NodeHandle selectionCoordinatorNode = renderGraph->AddNode<SelectionCoordinatorNodeType>("selection_coordinator");
    // Node loggers default DISABLED (NodeInstance ctor); selection results are user-facing, so enable
    // this node's logger to the terminal (otherwise its HIT/miss + diagnostics are silently dropped).
    if (auto* selInst = renderGraph->GetInstance(selectionCoordinatorNode)) {
        if (auto* sl = selInst->GetLogger()) { sl->SetEnabled(true); sl->SetTerminalOutput(true); }
    }

    NodeHandle physicsLoopBridge = renderGraph->AddNode<LoopBridgeNodeType>("physics_loop");
    NodeHandle physicsLoopIDConstant = renderGraph->AddNode<ConstantNodeType>("physics_loop_id");

    // M-wire Task 8: push constants for BodyInstanceRayMarch.comp (fields 8 and 9).
    // raySizeCoef = 0.0 disables LOD (full-detail traversal); set non-zero for screen-space LOD.
    // raySizeBias = 0.0 (pinhole camera; no bias at origin).
    NodeHandle raySizeCoefConstant = renderGraph->AddNode<ConstantNodeType>("ray_size_coef");
    NodeHandle raySizeBiasConstant = renderGraph->AddNode<ConstantNodeType>("ray_size_bias");
    
    NodeHandle debugCaptureNode = renderGraph->AddNode<DebugBufferReaderNodeType>("debug_capture");

    // --- UI composite pass (HUD over the voxel render) ---
    // A color-only graphics pass layered over the compute output: RenderPassNode (LOAD, initial=General,
    // final=PresentSrc) + FramebufferNode (swapchain image views) + UIRenderNode (composite). Mirrors
    // BuildUIGraph's UIRenderNode shape, but LOADs the voxel image instead of clearing.
    NodeHandle uiRenderPassNode  = renderGraph->AddNode<RenderPassNodeType>("ui_composite_render_pass");
    NodeHandle uiFramebufferNode = renderGraph->AddNode<FramebufferNodeType>("ui_composite_framebuffer");
    NodeHandle uiCompositeNode   = renderGraph->AddNode<UIRenderNodeType>("ui_composite_render");
    uiRenderNode_ = uiCompositeNode;                 // store for GetUiRenderNode() live lookup (host SetHudData)

    // SEL-P3: give the UI selection provider the composite UIRenderNode, so it can hit-test that
    // node's Rml::Context on a click (the context is a raw RmlUi pointer created in UIRenderNode's
    // CompileImpl — not a graph slot, so it is passed by node reference, not connected). The context
    // is null until the UI node first compiles; the provider tolerates that (emits a miss).
    if (auto* uiNodeInst = static_cast<UIRenderNode*>(renderGraph->GetInstance(uiCompositeNode))) {
        if (auto* uiProvInst = static_cast<UISelectionProviderNode*>(renderGraph->GetInstance(uiSelectionProviderNode))) {
            uiProvInst->SetUiRenderNode(uiNodeInst);
        }
    }

    mainLogger->Info("Created node instances (including compute pipeline, camera, voxel grid, gatherers, selection provider, and UI composite pass)");

    // ===================================================================
    // PHASE 2: Configure node parameters
    // ===================================================================

    // Window parameters
    auto* window = static_cast<WindowNode*>(renderGraph->GetInstance(windowNode));
    window->SetParameter(WindowNodeConfig::PARAM_WIDTH, width);
    window->SetParameter(WindowNodeConfig::PARAM_HEIGHT, height);

    // Device parameters (default GPU = 0)
    auto* device = static_cast<DeviceNode*>(renderGraph->GetInstance(deviceNode));
    device->SetParameter(DeviceNodeConfig::PARAM_GPU_INDEX, 0u);

    // DISABLED FOR COMPUTE TEST: Graphics pipeline parameters
    /*
    // Vertex buffer parameters (simple triangle)
    auto* vertexBuffer = static_cast<VertexBufferNode*>(renderGraph->GetInstance(vertexBufferNode));
    vertexBuffer->SetParameter(VertexBufferNodeConfig::PARAM_VERTEX_COUNT, 36u);
    vertexBuffer->SetParameter(VertexBufferNodeConfig::PARAM_VERTEX_STRIDE, sizeof(VertexWithUV)); // pos(vec4) + UV(vec2)
    vertexBuffer->SetParameter(VertexBufferNodeConfig::PARAM_USE_TEXTURE, true); // Shader uses vec2 UV at location 1
    vertexBuffer->SetParameter(VertexBufferNodeConfig::PARAM_INDEX_COUNT, 0u); // No index buffer

    // Texture loader parameters
    auto* textureLoader = static_cast<TextureLoaderNode*>(renderGraph->GetInstance(textureNode));
    textureLoader->SetParameter(TextureLoaderNodeConfig::FILE_PATH, std::string("C:\\Users\\liory\\Downloads\\earthmap.jpg"));
    textureLoader->SetParameter(TextureLoaderNodeConfig::SAMPLER_FILTER, std::string("Linear"));
    textureLoader->SetParameter(TextureLoaderNodeConfig::SAMPLER_ADDRESS_MODE, std::string("Repeat"));

    // Render pass parameters
    auto* renderPass = static_cast<RenderPassNode*>(renderGraph->GetInstance(renderPassNode));
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_COLOR_LOAD_OP, AttachmentLoadOp::Clear);
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_COLOR_STORE_OP, AttachmentStoreOp::Store);
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_DEPTH_LOAD_OP, AttachmentLoadOp::Clear);
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_DEPTH_STORE_OP, AttachmentStoreOp::DontCare);
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_INITIAL_LAYOUT, ImageLayout::Undefined);
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_FINAL_LAYOUT, ImageLayout::PresentSrc);
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_SAMPLES, 1u);

    // Framebuffer parameters
    auto* framebuffer = static_cast<FramebufferNode*>(renderGraph->GetInstance(framebufferNode));
    framebuffer->SetParameter(FramebufferNodeConfig::PARAM_LAYERS, 1u);

    // Depth buffer parameters
    auto* depthBuffer = static_cast<DepthBufferNode*>(renderGraph->GetInstance(depthBufferNode));
    depthBuffer->SetParameter(DepthBufferNodeConfig::PARAM_FORMAT, static_cast<uint32_t>(VK_FORMAT_D32_SFLOAT));

    // Graphics pipeline parameters
    auto* pipeline = static_cast<GraphicsPipelineNode*>(renderGraph->GetInstance(pipelineNode));
    pipeline->SetParameter(GraphicsPipelineNodeConfig::ENABLE_DEPTH_TEST, true);
    pipeline->SetParameter(GraphicsPipelineNodeConfig::ENABLE_DEPTH_WRITE, true);
    pipeline->SetParameter(GraphicsPipelineNodeConfig::ENABLE_VERTEX_INPUT, true);
    pipeline->SetParameter(GraphicsPipelineNodeConfig::CULL_MODE, std::string("Back"));

    // MVP: Shader loading deferred to CompileRenderGraph (after device is created)
    mainLogger->Info("Shader loading will occur during compilation phase");
    
    pipeline->SetParameter(GraphicsPipelineNodeConfig::POLYGON_MODE, std::string("Fill"));
    pipeline->SetParameter(GraphicsPipelineNodeConfig::TOPOLOGY, std::string("TriangleList"));
    pipeline->SetParameter(GraphicsPipelineNodeConfig::FRONT_FACE, std::string("CounterClockwise"));

    // Geometry render parameters
    auto* geometryRender = static_cast<GeometryRenderNode*>(renderGraph->GetInstance(geometryRenderNode));
    geometryRender->SetParameter(GeometryRenderNodeConfig::VERTEX_COUNT, 36u);
    geometryRender->SetParameter(GeometryRenderNodeConfig::INSTANCE_COUNT, 1u);
    geometryRender->SetParameter(GeometryRenderNodeConfig::FIRST_VERTEX, 0u);
    geometryRender->SetParameter(GeometryRenderNodeConfig::FIRST_INSTANCE, 0u);
    geometryRender->SetParameter(GeometryRenderNodeConfig::USE_INDEX_BUFFER, false);
    geometryRender->SetParameter(GeometryRenderNodeConfig::CLEAR_COLOR_R, 0.0f);
    geometryRender->SetParameter(GeometryRenderNodeConfig::CLEAR_COLOR_G, 0.0f);
    geometryRender->SetParameter(GeometryRenderNodeConfig::CLEAR_COLOR_B, 0.2f);
    geometryRender->SetParameter(GeometryRenderNodeConfig::CLEAR_COLOR_A, 1.0f);
    geometryRender->SetParameter(GeometryRenderNodeConfig::CLEAR_DEPTH, 1.0f);
    geometryRender->SetParameter(GeometryRenderNodeConfig::CLEAR_STENCIL, 0u);

    // Phase G: Configure shader libraries with builder functions

    // Graphics shader library (Draw.vert + Draw.frag)
    auto* graphicsShaderLib = static_cast<ShaderLibraryNode*>(renderGraph->GetInstance(shaderLibNode));
    graphicsShaderLib->RegisterShaderBuilder([](int vulkanVer, int spirvVer) {
        ShaderManagement::ShaderBundleBuilder builder;

        // Find shader paths - try compile-time shader directory first
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

        // Configure SDI generation
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
    */

    // Present parameters (needed for both graphics and compute)
    auto* present = static_cast<PresentNode*>(renderGraph->GetInstance(presentNode));
    present->SetParameter(PresentNodeConfig::WAIT_FOR_IDLE, true);

    // Phase 0.4: Loop ID constant (connects to LoopBridgeNode) - needed for both graphics and compute
    auto* loopIDConst = static_cast<ConstantNode*>(renderGraph->GetInstance(physicsLoopIDConstant));
    loopIDConst->SetValue<uint32_t>(physicsLoopID);
    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] Loop ID set, moving to shader library...");
    }

    // Vertical FOV of the ray-march camera, in degrees. Single source of truth: used both to
    // configure CameraNode (PARAM_FOV, below) and to derive the LOD ray-cone spread (raySizeCoef).
    constexpr float kRaymarchCameraFovDegrees = 45.0f;

    // M-wire Task 8: set LOD push constant values (fields 8 and 9 of BodyInstanceRayMarch.comp).
    // raySizeCoef is the ray cone spread per unit distance — drives the screen-space-error LOD
    // stop in BodyInstanceRayMarch.comp (gated on raySizeCoef > 0.0). Match the reference
    // SVOLOD.h::LODParameters::fromCamera: 2*tan((fovY / screenHeight) / 2), with fovY in radians
    // and screenHeight the swapchain pixel height (the same `height` used for dispatch dims below).
    // kRaymarchCameraFovDegrees is the vertical FOV; it is fed to CameraNode (PARAM_FOV) below so
    // the two stay in lock-step. raySizeBias = 0.0 (pinhole camera; zero cone diameter at origin).
    const float fovYRadians   = kRaymarchCameraFovDegrees * (3.14159265358979323846f / 180.0f);
    const float screenHeightF = static_cast<float>(height);
    const float raySizeCoef   = 2.0f * std::tan((fovYRadians / screenHeightF) * 0.5f);
    auto* raySizeCoefConst = static_cast<ConstantNode*>(renderGraph->GetInstance(raySizeCoefConstant));
    raySizeCoefConst->SetValue<float>(raySizeCoef);   // 2*tan(fovY/h/2): LOD enabled (Task 7)
    auto* raySizeBiasConst = static_cast<ConstantNode*>(renderGraph->GetInstance(raySizeBiasConstant));
    raySizeBiasConst->SetValue<float>(0.0f);   // 0.0 = pinhole camera bias
    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] LOD raySizeCoef=" + std::to_string(raySizeCoef) +
                         " (fov=" + std::to_string(kRaymarchCameraFovDegrees) + " deg, screenHeight=" +
                         std::to_string(height) + ")");
    }

    auto* frameSync = static_cast<FrameSyncNode*>(renderGraph->GetInstance(frameSyncNode));

    // Voxel ray marching compute shader (VoxelRayMarch.comp)
    // Load from pre-compiled shaders in build directory
    auto* computeShaderLibNode = static_cast<ShaderLibraryNode*>(renderGraph->GetInstance(computeShaderLib));

    // M-wire Task 8: use the instanced shell-octree ray-march shader (BodyInstanceRayMarch.comp).
    // Replaces VoxelRayMarch_Compressed.comp. Bindings 1/2/3/5 come from BodyOctreeSceneNode;
    // binding 10 = per-body instance SSBO; bindings 4/8 = debug/counters from voxelGridNode.
    computeShaderLibNode->RegisterShaderBuilder([this](int vulkanVer, int spirvVer) {
        ShaderManagement::ShaderBundleBuilder builder;

        constexpr const char* shaderName = "BodyInstanceRayMarch.comp";
        constexpr const char* programName = "BodyInstanceRayMarch";

        // Find shader source — try compile-time dir first, then fallback runtime paths.
        std::vector<std::filesystem::path> possiblePaths = {
#ifdef VIXEN_SHADER_SOURCE_DIR
            std::string(VIXEN_SHADER_SOURCE_DIR) + "/" + shaderName,
#endif
            std::string("shaders/") + shaderName,
            std::string("../shaders/") + shaderName,
            shaderName
        };

        std::filesystem::path compPath;
        for (const auto& path : possiblePaths) {
            if (std::filesystem::exists(path)) {
                compPath = path;
                break;
            }
        }

        if (compPath.empty()) {
            if (mainLogger && mainLogger->IsEnabled()) {
                mainLogger->Error("[BuildRenderGraph] " + std::string(shaderName) + " not found in search paths");
                mainLogger->Error("[BuildRenderGraph] Current working directory: " + std::filesystem::current_path().string());
#ifdef VIXEN_SHADER_SOURCE_DIR
                mainLogger->Error("[BuildRenderGraph] VIXEN_SHADER_SOURCE_DIR: " VIXEN_SHADER_SOURCE_DIR);
#endif
            }
            throw std::runtime_error(std::string(shaderName) + " not found - check shader search paths");
        }

        builder.SetProgramName(programName)
               .SetPipelineType(ShaderManagement::PipelineTypeConstraint::Compute)
               .SetTargetVulkanVersion(vulkanVer)
               .SetTargetSpirvVersion(spirvVer)
               .AddIncludePath("shaders")
               .AddIncludePath("../shaders")
#ifdef VIXEN_SHADER_SOURCE_DIR
               .AddIncludePath(VIXEN_SHADER_SOURCE_DIR)
#endif
               .AddStageFromFile(ShaderManagement::ShaderStage::Compute, compPath, "main");

        if (mainLogger && mainLogger->IsEnabled()) {
            mainLogger->Info("[BuildRenderGraph] Using BodyInstanceRayMarch shader: " + compPath.string());
            mainLogger->Info("[BuildRenderGraph] Octree buffers at bindings 1/2/3/5 (BodyOctreeSceneNode); instances at binding 10");
        }

        return builder;
    });

    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] Registered VoxelRayMarch shader builder");
    }

    // Phase G: Compute dispatch parameters
    auto* dispatch = static_cast<ComputeDispatchNode*>(renderGraph->GetInstance(computeDispatch));
    uint32_t dispatchX = width / 8;
    uint32_t dispatchY = height / 8;
    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] Setting dispatch dims: " + std::to_string(dispatchX) + "x" + std::to_string(dispatchY) + "x1 (from window " + std::to_string(width) + "x" + std::to_string(height) + ")");
    }
    dispatch->SetParameter(ComputeDispatchNodeConfig::DISPATCH_X, dispatchX);  // Workgroup size 8x8
    dispatch->SetParameter(ComputeDispatchNodeConfig::DISPATCH_Y, dispatchY);
    dispatch->SetParameter(ComputeDispatchNodeConfig::DISPATCH_Z, 1u);

    // Ray marching: Camera parameters
    auto* camera = static_cast<CameraNode*>(renderGraph->GetInstance(cameraNode));

    // Enable camera logger to debug position
    if (auto* cameraLogger = camera->GetLogger()) {
        cameraLogger->SetEnabled(false);
        cameraLogger->SetTerminalOutput(false);
    }

    camera->SetParameter(CameraNodeConfig::PARAM_FOV, kRaymarchCameraFovDegrees);  // shared with raySizeCoef
    camera->SetParameter(CameraNodeConfig::PARAM_NEAR_PLANE, 0.1f);
    camera->SetParameter(CameraNodeConfig::PARAM_FAR_PLANE, 500.0f);
    // Camera presets for Cornell box (grid spans [0,128], center at 64,64,64)
    // Uncomment one preset below:

    // PRESET 1: Front view looking into box (camera outside grid)
    // Orbit mode: yaw=0 means camera at +Z looking toward orbitCenter
    // yaw=pi means camera at -Z looking toward +Z (into the grid)
    // For camera at +Z looking toward grid, use yaw=0
    camera->SetParameter(CameraNodeConfig::PARAM_CAMERA_X, 64.0f);   // Center X (ignored in orbit mode)
    camera->SetParameter(CameraNodeConfig::PARAM_CAMERA_Y, 64.0f);   // Center Y (ignored in orbit mode)
    camera->SetParameter(CameraNodeConfig::PARAM_CAMERA_Z, 300.0f);  // Outside grid (ignored in orbit mode)
    camera->SetParameter(CameraNodeConfig::PARAM_YAW, 0.0f);         // Camera at +Z, looking toward -Z
    camera->SetParameter(CameraNodeConfig::PARAM_PITCH, 0.0f);

    // PRESET 2: Offset to see both left (red) and right (green) walls
    //camera->SetParameter(CameraNodeConfig::PARAM_CAMERA_X, 1.5f);
    //camera->SetParameter(CameraNodeConfig::PARAM_CAMERA_Y, 0.5f);
    //camera->SetParameter(CameraNodeConfig::PARAM_CAMERA_Z, -0.5f);
    //camera->SetParameter(CameraNodeConfig::PARAM_YAW, 0.0f);
    //camera->SetParameter(CameraNodeConfig::PARAM_PITCH, 0.0f);

    // PRESET 3: Far view of entire box
    //camera->SetParameter(CameraNodeConfig::PARAM_CAMERA_X, 0.0f);
    //camera->SetParameter(CameraNodeConfig::PARAM_CAMERA_Y, 0.0f);
    //camera->SetParameter(CameraNodeConfig::PARAM_CAMERA_Z, 5.0f);
    //camera->SetParameter(CameraNodeConfig::PARAM_YAW, 0.0f);
    //camera->SetParameter(CameraNodeConfig::PARAM_PITCH, 0.0f);

    // PRESET 4: Side view
    //camera->SetParameter(CameraNodeConfig::PARAM_CAMERA_X, 5.0f);
    //camera->SetParameter(CameraNodeConfig::PARAM_CAMERA_Y, 0.0f);
    //camera->SetParameter(CameraNodeConfig::PARAM_CAMERA_Z, -8.0f);
    //camera->SetParameter(CameraNodeConfig::PARAM_YAW, -90.0f);  // Look left toward -X
    //camera->SetParameter(CameraNodeConfig::PARAM_PITCH, 0.0f);
    camera->SetParameter(CameraNodeConfig::PARAM_GRID_RESOLUTION, 128u);

    // Ray marching: Voxel grid parameters
    auto* voxelGrid = static_cast<VoxelGridNode*>(renderGraph->GetInstance(voxelGridNode));
    voxelGrid->SetParameter(VoxelGridNodeConfig::PARAM_RESOLUTION, 128u);
    // Scene type defaults to the Cornell-box test scene; a host can override it (e.g. the UNDERTOW
    // render host sets VIXEN_SCENE=starsystem after registering its bodies with the scene factory).
    const char* sceneEnv = std::getenv("VIXEN_SCENE");
    voxelGrid->SetParameter(VoxelGridNodeConfig::PARAM_SCENE_TYPE,
                            std::string(sceneEnv != nullptr ? sceneEnv : "cornell"));

    // --- Standalone default body scene (Option A) ---
    // The live render path dispatches BodyInstanceRayMarch.comp, which only draws per-body INSTANCES
    // (numInstances = clamp(pc.instanceCount, ...)). The UNDERTOW host feeds real bodies at runtime via
    // VulkanGraphApplication::SetBodyInstances() -> BodyOctreeSceneNode::SetInstances(), but standalone
    // VIXEN.exe has no body source, so with 0 instances every ray misses and the screen is just the
    // dark sky color (looks black). Seed a few default instances so the standalone app shows a scene.
    //
    // SetInstances REPLACES the list (instances_ = std::move(...)), so a host that calls SetBodyInstances
    // at runtime fully overwrites these defaults — they are a standalone fallback only, no host gating
    // needed. BodyOctreeSceneNode builds 3 shell-octree "kinds" (octreeIndex 0/1/2), each a [0,64]^3 shell
    // (base center (32,32,32)). The instance transform is instOrigin = (rayOrigin - worldPos)/renderScale,
    // so a shell centered at world C needs worldPos = C - (32,32,32)*renderScale. We center the 3 shells
    // around the 128^3 grid centre (64,64,64) and spread them along X so they don't overlap and all sit
    // in the default camera view (verified on screen: three distinct red/green/white spheres).
    // color[3] is a per-instance tint that MULTIPLIES the kind's material (1=red, 2=green, 3=white), kept
    // near-white per instance (slight warm/neutral/cool bias) so each stays bright and the three are
    // distinguishable by both base material and tint.
    {
        if (std::getenv("VIXEN_STORED_SDF_DEMO")) {
            // VIXEN_STORED_SDF_DEMO — Stored-SDF bodies (Increment 2, M5 Task 10).
            // EnsureOctreesBuilt has baked 3 SdfBodyOctrees (kinds 0/1/2) via ConcatenateSdf,
            // setting configs[k].formatId = STORED_SDF and populating the sdfBricks /
            // brickGridLookup buffers (bindings 11/12). Instances use providerKind=0 (STORED)
            // and octreeIndex=0/1/2 to select the per-kind OctreeConfig.
            //
            // Transform convention (binary-shell / marchStoredSdf AABB):
            //   renderScale = 0.75       — scales grid-voxel [0,64] into world units
            //   worldPos    = center - 32*0.75 = center - 24
            //     → de-instance transform: instOrigin = (rayOrigin - worldPos) / renderScale
            //       so a ray at world center maps to grid (32,32,32) = [0,64] AABB center.
            //
            // Body centers in world space (same spread as the Procedural seed so the
            // default camera (X=64, Z=300, looking -Z) frames all three):
            //   left   center = (14, 64, 64)  → worldPos = (14-24, 64-24, 64-24) = (-10, 40, 40)
            //   centre center = (64, 64, 64)  → worldPos = (64-24, 64-24, 64-24) = ( 40, 40, 40)
            //   right  center = (114,64, 64)  → worldPos = (114-24,64-24, 64-24) = ( 90, 40, 40)
            constexpr float kRenderScale = 0.75f;
            constexpr float kHalf        = 32.0f * kRenderScale;  // = 24.0f

            auto placeStored = [&](float cx, float cy, float cz,
                                   float r, float g, float b,
                                   uint32_t octreeIdx) {
                Vixen::SVO::BodyInstanceGpu inst{};
                inst.worldPos[0]  = cx - kHalf;  // worldPos = center - 24 per axis
                inst.worldPos[1]  = cy - kHalf;
                inst.worldPos[2]  = cz - kHalf;
                inst.renderScale  = kRenderScale;
                inst.color[0]     = r;
                inst.color[1]     = g;
                inst.color[2]     = b;
                inst.octreeIndex  = octreeIdx;    // selects configs[k] (incl. formatId)
                inst.providerKind = 0u;           // PROVIDER_STORED: octree/Stored path
                inst.recipeId     = 0u;           // unused by Stored path
                return inst;
            };
            std::vector<Vixen::SVO::BodyInstanceGpu> storedBodies = {
                placeStored( 14.0f, 64.0f, 64.0f, 1.00f, 0.95f, 0.85f, 0u),  // left   — smooth sphere   (kind 0, red)
                placeStored( 64.0f, 64.0f, 64.0f, 0.55f, 0.75f, 1.00f, 1u),  // centre — displaced sphere (kind 1, green)
                placeStored(114.0f, 64.0f, 64.0f, 0.85f, 0.90f, 1.00f, 2u),  // right  — smooth sphere   (kind 2, white)
            };
            if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                bodyScene->SetInstances(std::move(storedBodies));
                mainLogger->Info("[BuildRenderGraph] VIXEN_STORED_SDF_DEMO: seeded 3 Stored-SDF body instances");
            }
        } else {
            // Default — Procedural SDF bodies (Increment 1): true smooth spheres, no octree.
            // worldPos = world centre; recipeParams = (radius, displaceAmp, displaceFreq).
            // Radius 24 matches the prior Stored shells' on-screen size (kHalf=24), so the
            // default camera frames all three. providerKind=1 selects the Procedural path.
            constexpr float kRadius = 24.0f;
            auto placeProcedural = [&](float cx, float cy, float cz,
                                       float r, float g, float b,
                                       uint32_t recipeId, float amp, float freq) {
                Vixen::SVO::BodyInstanceGpu inst{};
                inst.worldPos[0] = cx;
                inst.worldPos[1] = cy;
                inst.worldPos[2] = cz;
                inst.renderScale = 1.0f;            // unused by Procedural
                inst.color[0]    = r;
                inst.color[1]    = g;
                inst.color[2]    = b;
                inst.octreeIndex = 0u;              // unused by Procedural
                inst.providerKind = 1u;             // PROVIDER_PROCEDURAL
                inst.recipeId     = recipeId;       // 0 = sphere, 1 = displaced sphere
                inst.recipeParams[0] = kRadius;
                inst.recipeParams[1] = amp;
                inst.recipeParams[2] = freq;
                return inst;
            };
            std::vector<Vixen::SVO::BodyInstanceGpu> defaultBodies = {
                placeProcedural( 14.0f, 64.0f, 64.0f, 1.00f, 0.95f, 0.85f, 0u, 0.0f, 0.0f),  // left   — smooth star/sphere
                placeProcedural( 64.0f, 64.0f, 64.0f, 0.55f, 0.75f, 1.00f, 1u, 2.0f, 0.5f),  // centre — displaced planet
                placeProcedural(114.0f, 64.0f, 64.0f, 0.85f, 0.90f, 1.00f, 0u, 0.0f, 0.0f),  // right  — smooth sphere
            };
            if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                bodyScene->SetInstances(std::move(defaultBodies));
                mainLogger->Info("[BuildRenderGraph] Seeded 3 Procedural SDF body instances (standalone fallback)");
            }
        }
    }

    // Enable logging for VoxelGridNode to see octree generation
    if (auto* voxelLogger = voxelGrid->GetLogger()) {
        voxelLogger->SetEnabled(true);  // Enable to debug voxel rendering
        voxelLogger->SetTerminalOutput(true);
    }

    // Enable logging for descriptor gatherer to debug bindings
    auto* descGatherer = static_cast<DescriptorResourceGathererNode*>(renderGraph->GetInstance(descriptorGatherer));
    if (auto* gathererLogger = descGatherer->GetLogger()) {
        gathererLogger->SetEnabled(false);  // Enable to debug descriptor bindings
        gathererLogger->SetTerminalOutput(false);
    }

    // Enable logging for compute dispatch to see execution
    if (auto* dispatchLogger = dispatch->GetLogger()) {
        dispatchLogger->SetEnabled(true);
        dispatchLogger->SetTerminalOutput(true);
    }

    // Enable logging for push constant gatherer to see packing
    auto* pcGatherer = static_cast<PushConstantGathererNode*>(renderGraph->GetInstance(pushConstantGatherer));
    if (auto* pcLogger = pcGatherer->GetLogger()) {
        pcLogger->SetEnabled(false);
        pcLogger->SetTerminalOutput(false);
    }

    auto* debugCapture = static_cast<DebugBufferReaderNode*>(renderGraph->GetInstance(debugCaptureNode));
    debugCapture->SetParameter(DebugBufferReaderNodeConfig::PARAM_MAX_SAMPLES, 1000u);
    debugCapture->SetParameter(DebugBufferReaderNodeConfig::PARAM_AUTO_EXPORT, true);
    debugCapture->SetParameter(DebugBufferReaderNodeConfig::PARAM_EXPORT_FORMAT, static_cast<int>(DebugExportFormat::JSON));
    debugCapture->SetParameter(DebugBufferReaderNodeConfig::PARAM_OUTPUT_PATH, std::string("binaries/compute_debug_output"));
    debugCapture->SetParameter(DebugBufferReaderNodeConfig::PARAM_FRAMES_PER_EXPORT, 10u);
    if (auto* debugLogger = debugCapture->GetLogger()) {
        debugLogger->SetEnabled(true);
        debugLogger->SetTerminalOutput(true);
    }

    

    // --- UI composite pass parameters ---
    // The compute leaves the swapchain image in GENERAL (it no longer transitions to PRESENT_SRC); the
    // UI render pass LOADs that image, draws the HUD over it, and owns the →PRESENT_SRC transition.
    dispatch->SetParameter(ComputeDispatchNodeConfig::PARAM_LEAVE_IMAGE_IN_GENERAL, true);

    auto* uiRenderPass = static_cast<RenderPassNode*>(renderGraph->GetInstance(uiRenderPassNode));
    uiRenderPass->SetParameter(RenderPassNodeConfig::PARAM_COLOR_LOAD_OP, AttachmentLoadOp::Load);   // preserve voxels
    uiRenderPass->SetParameter(RenderPassNodeConfig::PARAM_COLOR_STORE_OP, AttachmentStoreOp::Store);
    uiRenderPass->SetParameter(RenderPassNodeConfig::PARAM_INITIAL_LAYOUT, ImageLayout::General);    // compute leaves GENERAL
    uiRenderPass->SetParameter(RenderPassNodeConfig::PARAM_FINAL_LAYOUT, ImageLayout::PresentSrc);   // ready for present
    uiRenderPass->SetParameter(RenderPassNodeConfig::PARAM_SAMPLES, 1u);

    auto* uiFramebuffer = static_cast<FramebufferNode*>(renderGraph->GetInstance(uiFramebufferNode));
    uiFramebuffer->SetParameter(FramebufferNodeConfig::PARAM_LAYERS, 1u);

    auto* uiComposite = static_cast<UIRenderNode*>(renderGraph->GetInstance(uiCompositeNode));
    uiComposite->SetParameter(UIRenderNodeConfig::PARAM_COMPOSITE, true);
    uiComposite->SetParameter(UIRenderNodeConfig::RML_DOCUMENT_PATH, std::string("assets/ui/hud.rml"));

    mainLogger->Info("Configured all node parameters (including camera, voxel grid, and UI composite pass)");

    // ===================================================================
    // PHASE 3: Wire connections using TypedConnection API
    // ===================================================================

    using namespace Vixen::RenderGraph;

    mainLogger->Info("Wiring node connections using TypedConnection API");

    // Use ConnectionBatch for atomic registration
    ConnectionBatch batch(renderGraph);

    // --- Instance → Device connection (Phase 1.1: Dependency injection) ---
    batch.Connect(instanceNode, InstanceNodeConfig::INSTANCE,
                  deviceNode, DeviceNodeConfig::INSTANCE_IN);

    // --- Device → Window connection (VkInstance passthrough) ---
    batch.Connect(deviceNode, DeviceNodeConfig::INSTANCE_OUT,
                  windowNode, WindowNodeConfig::INSTANCE);

    // --- Window → SwapChain connections ---
    batch.Connect(windowNode, WindowNodeConfig::WINDOW,
                  swapChainNode, SwapChainNodeConfig::WINDOW)
         .Connect(windowNode, WindowNodeConfig::WIDTH_OUT,
                  swapChainNode, SwapChainNodeConfig::WIDTH)
         .Connect(windowNode, WindowNodeConfig::HEIGHT_OUT,
                  swapChainNode, SwapChainNodeConfig::HEIGHT);

    // --- Window → Input connection ---
    batch.Connect(windowNode, WindowNodeConfig::WINDOW,
                  inputNode, InputNodeConfig::WINDOW);

    // --- Device → SwapChain connections ---
    batch.Connect(deviceNode, DeviceNodeConfig::INSTANCE_OUT,
                  swapChainNode, SwapChainNodeConfig::INSTANCE)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  swapChainNode, SwapChainNodeConfig::VULKAN_DEVICE_IN);

    // --- Device → FrameSync connection (Phase 0.2) ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  frameSyncNode, FrameSyncNodeConfig::VULKAN_DEVICE);

    // --- FrameSync → SwapChain connections (Phase 0.4) ---
    // Phase 0.4: Per-flight semaphores and current frame index
    batch.Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  swapChainNode, SwapChainNodeConfig::CURRENT_FRAME_INDEX);
    batch.Connect(frameSyncNode, FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY,
                  swapChainNode, SwapChainNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY);
    // FR-3: renderComplete + presentFences are now PRODUCED by swapChainNode (sized to the actual image count).

    // --- Device → CommandPool connection ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  commandPoolNode, CommandPoolNodeConfig::VULKAN_DEVICE_IN);

    // DISABLED FOR COMPUTE TEST: Graphics pipeline connections
    /*
    // --- Device → DepthBuffer device connection (for Vulkan operations) ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  depthBufferNode, DepthBufferNodeConfig::VULKAN_DEVICE_IN);

    // --- SwapChain → DepthBuffer connection (for dimensions) ---
    batch.Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  depthBufferNode, DepthBufferNodeConfig::SWAPCHAIN_PUBLIC_VARS);

    // --- CommandPool → DepthBuffer connection ---
    batch.Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL,
                  depthBufferNode, DepthBufferNodeConfig::COMMAND_POOL);

    // --- Device → RenderPass device connection ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  renderPassNode, RenderPassNodeConfig::VULKAN_DEVICE_IN);

    // --- SwapChain → RenderPass connection (swapchain info bundle) ---
    batch.Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  renderPassNode, RenderPassNodeConfig::SWAPCHAIN_INFO);

    // --- DepthBuffer → RenderPass connection (depth format) ---
    batch.Connect(depthBufferNode, DepthBufferNodeConfig::DEPTH_FORMAT,
                  renderPassNode, RenderPassNodeConfig::DEPTH_FORMAT);

    // --- Device → Framebuffer device connection ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  framebufferNode, FramebufferNodeConfig::VULKAN_DEVICE_IN);

    // --- RenderPass + SwapChain + DepthBuffer → Framebuffer connections ---
    batch.Connect(renderPassNode, RenderPassNodeConfig::RENDER_PASS,
        framebufferNode, FramebufferNodeConfig::RENDER_PASS)
        .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
            framebufferNode, FramebufferNodeConfig::SWAPCHAIN_INFO)
        .Connect(depthBufferNode, DepthBufferNodeConfig::DEPTH_IMAGE_VIEW,
            framebufferNode, FramebufferNodeConfig::DEPTH_ATTACHMENT);


    // --- Device → ShaderLibrary device chain ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  shaderLibNode, ShaderLibraryNodeConfig::VULKAN_DEVICE_IN);

    // --- Device → GraphicsPipeline device connection ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  pipelineNode, GraphicsPipelineNodeConfig::VULKAN_DEVICE_IN);

    // --- RenderPass + DescriptorSet + SwapChain → Pipeline connections ---
    // Phase 2: Connect ShaderDataBundle to both DescriptorSetNode and GraphicsPipelineNode
    batch.Connect(shaderLibNode, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  descriptorSetNode, DescriptorSetNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(shaderLibNode, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  pipelineNode, GraphicsPipelineNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(renderPassNode, RenderPassNodeConfig::RENDER_PASS,
                  pipelineNode, GraphicsPipelineNodeConfig::RENDER_PASS)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  descriptorSetNode, DescriptorSetNodeConfig::VULKAN_DEVICE_IN)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  descriptorSetNode, DescriptorSetNodeConfig::SWAPCHAIN_PUBLIC)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX,
                  descriptorSetNode, DescriptorSetNodeConfig::IMAGE_INDEX)
         .Connect(descriptorSetNode, DescriptorSetNodeConfig::DESCRIPTOR_SET_LAYOUT,
                  pipelineNode, GraphicsPipelineNodeConfig::DESCRIPTOR_SET_LAYOUT);
         // Note: SWAPCHAIN_INFO removed from GraphicsPipelineNode (pipelines are swapchain-independent)

    // --- Device → TextureLoader device chain ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  textureNode, TextureLoaderNodeConfig::VULKAN_DEVICE_IN);

    // --- TextureLoader → DescriptorSet texture connections ---
    batch.Connect(textureNode, TextureLoaderNodeConfig::TEXTURE_IMAGE,
                  descriptorSetNode, DescriptorSetNodeConfig::TEXTURE_IMAGE)
         .Connect(textureNode, TextureLoaderNodeConfig::TEXTURE_VIEW,
                  descriptorSetNode, DescriptorSetNodeConfig::TEXTURE_VIEW)
         .Connect(textureNode, TextureLoaderNodeConfig::TEXTURE_SAMPLER,
                  descriptorSetNode, DescriptorSetNodeConfig::TEXTURE_SAMPLER);

    // --- Device → VertexBuffer device chain ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  vertexBufferNode, VertexBufferNodeConfig::VULKAN_DEVICE_IN);

    // --- All resources → GeometryRender connections ---
    batch.Connect(renderPassNode, RenderPassNodeConfig::RENDER_PASS,
                  geometryRenderNode, GeometryRenderNodeConfig::RENDER_PASS)
         .Connect(framebufferNode, FramebufferNodeConfig::FRAMEBUFFERS,
                  geometryRenderNode, GeometryRenderNodeConfig::FRAMEBUFFERS)
         .Connect(pipelineNode, GraphicsPipelineNodeConfig::PIPELINE,
                  geometryRenderNode, GeometryRenderNodeConfig::PIPELINE)
         .Connect(pipelineNode, GraphicsPipelineNodeConfig::PIPELINE_LAYOUT,
                  geometryRenderNode, GeometryRenderNodeConfig::PIPELINE_LAYOUT)
         .Connect(descriptorSetNode, DescriptorSetNodeConfig::DESCRIPTOR_SETS,
                  geometryRenderNode, GeometryRenderNodeConfig::DESCRIPTOR_SETS)
         .Connect(vertexBufferNode, VertexBufferNodeConfig::VERTEX_BUFFER,
                  geometryRenderNode, GeometryRenderNodeConfig::VERTEX_BUFFER)
         .Connect(vertexBufferNode, VertexBufferNodeConfig::INDEX_BUFFER,
                  geometryRenderNode, GeometryRenderNodeConfig::INDEX_BUFFER)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  geometryRenderNode, GeometryRenderNodeConfig::SWAPCHAIN_INFO)
         .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL,
                  geometryRenderNode, GeometryRenderNodeConfig::COMMAND_POOL)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  geometryRenderNode, GeometryRenderNodeConfig::VULKAN_DEVICE)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX,
                  geometryRenderNode, GeometryRenderNodeConfig::IMAGE_INDEX)
         .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  geometryRenderNode, GeometryRenderNodeConfig::CURRENT_FRAME_INDEX)  // Phase 0.5: Frame-in-flight index for semaphore indexing
         .Connect(frameSyncNode, FrameSyncNodeConfig::IN_FLIGHT_FENCE,
                  geometryRenderNode, GeometryRenderNodeConfig::IN_FLIGHT_FENCE)  // Phase 0.5: Per-flight fence (CPU-GPU sync)
         .Connect(frameSyncNode, FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY,
                  geometryRenderNode, GeometryRenderNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY)  // Phase 0.5: Array of per-flight semaphores (indexed by frameIndex)
         .Connect(frameSyncNode, FrameSyncNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY,
                  geometryRenderNode, GeometryRenderNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY);  // Phase 0.5: Array of per-image semaphores (indexed by imageIndex)
    */

    // --- Device → Present device connection ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  presentNode, PresentNodeConfig::VULKAN_DEVICE_IN);

    // --- SwapChain → Present connections (for compute-only rendering) ---
    batch.Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_HANDLE,
                  presentNode, PresentNodeConfig::SWAPCHAIN)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX,
                  presentNode, PresentNodeConfig::IMAGE_INDEX);

    // --- UI composite → Present semaphore connection ---
    // The UI pass is now the frame's last submit: present waits on the UI's render-complete semaphore
    // (not the compute's). The compute's render-complete is consumed by the UI as the compute→UI handoff.
    batch.Connect(uiCompositeNode, UIRenderNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                  presentNode, PresentNodeConfig::RENDER_COMPLETE_SEMAPHORE);


    // --- Gatherer/ComputeDispatch → DebugBufferReader connections ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  debugCaptureNode, DebugBufferReaderNodeConfig::VULKAN_DEVICE_IN);
    batch.Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL,
                  debugCaptureNode, DebugBufferReaderNodeConfig::COMMAND_POOL);
    // Debug capture flows: Gatherer extracts from entries → ComputeDispatch (passthrough) → DebugReader
    batch.Connect(descriptorGatherer, DescriptorResourceGathererNodeConfig::DEBUG_CAPTURE,
                  computeDispatch, ComputeDispatchNodeConfig::DEBUG_CAPTURE);
    batch.Connect(computeDispatch, ComputeDispatchNodeConfig::DEBUG_CAPTURE_OUT,
                  debugCaptureNode, DebugBufferReaderNodeConfig::DEBUG_CAPTURE);
    // Fence connection - wait for GPU to finish before reading debug buffer
    batch.Connect(frameSyncNode, FrameSyncNodeConfig::IN_FLIGHT_FENCE,
                  debugCaptureNode, DebugBufferReaderNodeConfig::IN_FLIGHT_FENCE);

    // --- SwapChain → Present present-fence array (FR-3: owned by swapChainNode) ---
    batch.Connect(swapChainNode, SwapChainNodeConfig::PRESENT_FENCES_ARRAY,
                  presentNode, PresentNodeConfig::PRESENT_FENCE_ARRAY);

    // MVP: Shader connection happens in CompileRenderGraph (after device creation)

    // --- Phase 0.4: Loop System Connections ---
    batch.Connect(physicsLoopIDConstant, ConstantNodeConfig::OUTPUT,
                  physicsLoopBridge, LoopBridgeNodeConfig::LOOP_ID);

    // --- Phase G: Compute Pipeline Connections ---
    // Pipeline setup
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  computeShaderLib, ShaderLibraryNodeConfig::VULKAN_DEVICE_IN)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  computeDescriptorSet, DescriptorSetNodeConfig::VULKAN_DEVICE_IN)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  computePipeline, ComputePipelineNodeConfig::VULKAN_DEVICE_IN)
         // Phase H: Shader bundle → Gatherer for descriptor discovery
         .Connect(computeShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  descriptorGatherer, DescriptorResourceGathererNodeConfig::SHADER_DATA_BUNDLE)
         // Phase H: Gatherer → DescriptorSet (data-driven resources with embedded slotRole + debugCapture)
         .Connect(descriptorGatherer, DescriptorResourceGathererNodeConfig::DESCRIPTOR_RESOURCES,
                  computeDescriptorSet, DescriptorSetNodeConfig::DESCRIPTOR_RESOURCES)
         // Phase H: Push constant gatherer connections
         .Connect(computeShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  pushConstantGatherer, PushConstantGathererNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(pushConstantGatherer, PushConstantGathererNodeConfig::PUSH_CONSTANT_DATA,
                  computeDispatch, ComputeDispatchNodeConfig::PUSH_CONSTANT_DATA)
         .Connect(pushConstantGatherer, PushConstantGathererNodeConfig::PUSH_CONSTANT_RANGES,
                  computeDispatch, ComputeDispatchNodeConfig::PUSH_CONSTANT_RANGES)
         // Pass shader bundle directly to descriptor set and pipeline (needed during Compile)
         .Connect(computeShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  computeDescriptorSet, DescriptorSetNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(computeShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  computePipeline, ComputePipelineNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(computeShaderLib, ShaderLibraryNodeConfig::SHADER_DATA_BUNDLE,
                  computeDispatch, ComputeDispatchNodeConfig::SHADER_DATA_BUNDLE)
         .Connect(computeDescriptorSet, DescriptorSetNodeConfig::DESCRIPTOR_SET_LAYOUT,
                  computePipeline, ComputePipelineNodeConfig::DESCRIPTOR_SET_LAYOUT)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  computeDispatch, ComputeDispatchNodeConfig::VULKAN_DEVICE_IN)
         .Connect(computePipeline, ComputePipelineNodeConfig::PIPELINE,
                  computeDispatch, ComputeDispatchNodeConfig::COMPUTE_PIPELINE)
         .Connect(computePipeline, ComputePipelineNodeConfig::PIPELINE_LAYOUT,
                  computeDispatch, ComputeDispatchNodeConfig::PIPELINE_LAYOUT)
         .Connect(computeDescriptorSet, DescriptorSetNodeConfig::DESCRIPTOR_SETS,
                  computeDispatch, ComputeDispatchNodeConfig::DESCRIPTOR_SETS)
         .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL,
                  computeDispatch, ComputeDispatchNodeConfig::COMMAND_POOL);

    // --- Ray Marching Resource Connections ---
    // Camera node connections
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  cameraNode, CameraNodeConfig::VULKAN_DEVICE_IN)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  cameraNode, CameraNodeConfig::SWAPCHAIN_PUBLIC)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX,
                  cameraNode, CameraNodeConfig::IMAGE_INDEX)
         .Connect(inputNode, InputNodeConfig::INPUT_STATE,
                  cameraNode, CameraNodeConfig::INPUT_STATE);

    // Selection (SEL-P2) — providers are NODES. The voxel provider node copies the crosshair texel of
    // PickIdTargetNode's ID image (binding-9 target) via a one-shot fenced copy on a left-click edge,
    // decodes brick/voxel, and emits a SelectionCandidate. Its inputs: per-frame InputState; the ID
    // VkImage; device + command pool for the one-shot copy; the frame-in-flight index; the swapchain
    // viewport size (for the center offset).
    batch.Connect(inputNode, InputNodeConfig::INPUT_STATE,
                  voxelSelectionProviderNode, VoxelSelectionProviderNodeConfig::INPUT_STATE)
         .Connect(pickIdTargetNode, PickIdTargetNodeConfig::ID_IMAGE,
                  voxelSelectionProviderNode, VoxelSelectionProviderNodeConfig::ID_IMAGE)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  voxelSelectionProviderNode, VoxelSelectionProviderNodeConfig::VULKAN_DEVICE)
         .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL,
                  voxelSelectionProviderNode, VoxelSelectionProviderNodeConfig::COMMAND_POOL)
         .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  voxelSelectionProviderNode, VoxelSelectionProviderNodeConfig::CURRENT_FRAME_INDEX)
         .Connect(windowNode, WindowNodeConfig::WIDTH_OUT,
                  voxelSelectionProviderNode, VoxelSelectionProviderNodeConfig::VIEWPORT_WIDTH)
         .Connect(windowNode, WindowNodeConfig::HEIGHT_OUT,
                  voxelSelectionProviderNode, VoxelSelectionProviderNodeConfig::VIEWPORT_HEIGHT);

    // SEL-P3 UI provider: only needs per-frame InputState (cursor position + left button). It reads
    // the HUD's Rml::Context via the UIRenderNode reference wired above (SetUiRenderNode), not a slot.
    batch.Connect(inputNode, InputNodeConfig::INPUT_STATE,
                  uiSelectionProviderNode, UISelectionProviderNodeConfig::INPUT_STATE);

    // The coordinator GATHERS every provider's CANDIDATE into its PROVIDER_CANDIDATES accumulation
    // slot, priority-resolves (pickBestCandidate over the gathered vector), applies the input modifier
    // to the durable SelectionSet, and broadcasts a SelectionChangedEvent. It also reads InputState
    // for the click edge. Both providers MultiConnect into the gather slot via the accumulation-connect
    // path (ConnectionMeta{}.With<AccumulationSortConfig>(key)); the sort key only orders the gathered
    // vector — the WINNER is decided by candidate priority (UI=10 occludes voxel world=0). Adding
    // another provider is one more MultiConnect here, with no coordinator change.
    batch.Connect(inputNode, InputNodeConfig::INPUT_STATE,
                  selectionCoordinatorNode, SelectionCoordinatorNodeConfig::INPUT_STATE)
         .Connect(voxelSelectionProviderNode, VoxelSelectionProviderNodeConfig::CANDIDATE,
                  selectionCoordinatorNode, SelectionCoordinatorNodeConfig::PROVIDER_CANDIDATES,
                  ConnectionMeta{}.With<AccumulationSortConfig>(0))
         .Connect(uiSelectionProviderNode, UISelectionProviderNodeConfig::CANDIDATE,
                  selectionCoordinatorNode, SelectionCoordinatorNodeConfig::PROVIDER_CANDIDATES,
                  ConnectionMeta{}.With<AccumulationSortConfig>(1));

    // Pick ID target (AR#35 GPU picking P1): allocate the R32_UINT storage-image ring sized to the
    // window, transition it to GENERAL once, and expose the current frame's view for binding 9.
    // Device + command pool drive allocation + the one-shot UNDEFINED->GENERAL transition; the frame
    // index advances the ring each Execute. The ID_IMAGE_VIEW -> descriptorGatherer binding-9 wiring
    // is below, beside the other compute descriptor connections.
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  pickIdTargetNode, PickIdTargetNodeConfig::VULKAN_DEVICE_IN)
         .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL,
                  pickIdTargetNode, PickIdTargetNodeConfig::COMMAND_POOL)
         .Connect(windowNode, WindowNodeConfig::WIDTH_OUT,
                  pickIdTargetNode, PickIdTargetNodeConfig::WIDTH)
         .Connect(windowNode, WindowNodeConfig::HEIGHT_OUT,
                  pickIdTargetNode, PickIdTargetNodeConfig::HEIGHT)
         .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  pickIdTargetNode, PickIdTargetNodeConfig::CURRENT_FRAME_INDEX);

    // Voxel grid node connections (debug-only; no longer the render source — kept for bindings 4 and 8)
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  voxelGridNode, VoxelGridNodeConfig::VULKAN_DEVICE_IN)
         .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL,
                  voxelGridNode, VoxelGridNodeConfig::COMMAND_POOL);

    // M-wire Task 8: body octree scene node connections (the live render source post M-wire).
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT,
                  bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::VULKAN_DEVICE_IN)
         .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL,
                  bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::COMMAND_POOL)
         // FR-7 ring fix: supply the per-frame index so ExecuteImpl picks which ring
         // slot to upload instances into (prevents CPU/GPU races on the instance SSBO).
         .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::CURRENT_FRAME_INDEX);

    // Connect push constant fields to push constant gatherer using member extraction
    // CameraNode now outputs a CameraData struct, so we can extract individual fields
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          pushConstantGatherer, VoxelRayMarch::cameraPos::BINDING,  // vec3 cameraPos
                          ExtractField(&CameraData::cameraPos, SlotRole::Execute));  // Mark as Execute-only

    // Note: time field (index 1) NOT connected - will be filled with zero by gatherer
    // This will trigger a warning log but shader will receive valid (zero) value
    // TODO: Connect actual time source when animation is needed

    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          pushConstantGatherer, VoxelRayMarch::cameraDir::BINDING,  // vec3 cameraDir
                          ExtractField(&CameraData::cameraDir, SlotRole::Execute));  // Mark as Execute-only
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          pushConstantGatherer, VoxelRayMarch::fov::BINDING,  // float fov
                          ExtractField(&CameraData::fov, SlotRole::Execute));  // Mark as Execute-only
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          pushConstantGatherer, VoxelRayMarch::cameraUp::BINDING,  // vec3 cameraUp
                          ExtractField(&CameraData::cameraUp, SlotRole::Execute));  // Mark as Execute-only
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          pushConstantGatherer, VoxelRayMarch::aspect::BINDING,  // float aspect
                          ExtractField(&CameraData::aspect, SlotRole::Execute));  // Mark as Execute-only
    batch.Connect(cameraNode, CameraNodeConfig::CAMERA_DATA,
                          pushConstantGatherer, VoxelRayMarch::cameraRight::BINDING,  // vec3 cameraRight
                          ExtractField(&CameraData::cameraRight, SlotRole::Execute));  // Mark as Execute-only

    // Connect debugMode from InputState to push constant gatherer for debug visualization
    // Press 0-9 keys to switch between visualization modes at runtime
    batch.Connect(inputNode, InputNodeConfig::INPUT_STATE,
                          pushConstantGatherer, VoxelRayMarch::debugMode::BINDING,  // int debugMode
                          ExtractField(&InputState::debugMode, SlotRole::Execute));  // Mark as Execute-only

    // M-wire Task 8: new push constant fields 8, 9, 10 for BodyInstanceRayMarch.comp.
    // Field indices match shader reflection order: cameraPos(0),time(1),cameraDir(2),fov(3),
    // cameraUp(4),aspect(5),cameraRight(6),debugMode(7), raySizeCoef(8),raySizeBias(9),instanceCount(10).
    // raySizeCoef (binding 8): LOD cone-spread constant; 0.0 disables LOD (full-detail traversal).
    batch.Connect(raySizeCoefConstant, ConstantNodeConfig::OUTPUT,
                          pushConstantGatherer, 8,  // push constant field 8: float raySizeCoef
                          SlotRoleModifier(SlotRole::Execute));
    // raySizeBias (binding 9): LOD origin cone size; 0.0 for pinhole camera.
    batch.Connect(raySizeBiasConstant, ConstantNodeConfig::OUTPUT,
                          pushConstantGatherer, 9,  // push constant field 9: float raySizeBias
                          SlotRoleModifier(SlotRole::Execute));
    // instanceCount (binding 10): number of valid entries in bodyInstances[]; from BodyOctreeSceneNode.
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::INSTANCE_COUNT,
                          pushConstantGatherer, 10,  // push constant field 10: int instanceCount
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    // Connect ray marching resources to descriptor gatherer using VoxelRayMarchNames.h bindings
    // Binding 0: outputImage - Transient (Execute-only), others are Persistent (Dependency|Execute)
    // Binding 0: outputImage (swapchain image view) - changes per frame
    // Note: outputImage is not in SDI (writeonly image) so we use literal binding index 0
    batch.Connect(swapChainNode, SwapChainNodeConfig::CURRENT_FRAME_IMAGE_VIEW,
                          descriptorGatherer, 0,  // outputImage at binding 0
                          SlotRoleModifier(SlotRole::Execute));

    // M-wire Task 8: bindings 1/2/3/5 now come from BodyOctreeSceneNode (sparse shell octrees).
    // Slot names are identical to VoxelGridNode's octree outputs (by design in BodyOctreeSceneNodeConfig).
    // The shader's esvoNodes/brickData/materials/OctreeConfigsUBO at these bindings are now the
    // concatenated per-kind shell octrees, NOT the dense 128^3 grid.

    // Binding 1: esvoNodes (SSBO) - concatenated shell octree node descriptors for <= 3 kinds
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::OCTREE_NODES_BUFFER,
                          descriptorGatherer, VoxelRayMarch::esvoNodes::BINDING,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    // Binding 2: brickData (SSBO) - concatenated brick voxel data
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::OCTREE_BRICKS_BUFFER,
                          descriptorGatherer, VoxelRayMarch::brickData::BINDING,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    // Binding 3: materials (SSBO) - material palette
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::OCTREE_MATERIALS_BUFFER,
                          descriptorGatherer, VoxelRayMarch::materials::BINDING,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    // Binding 4: RayTraceBuffer (debug capture) — still from voxelGridNode (it has the buffer;
    // BodyOctreeSceneNode has no debug capture). VoxelGridNode stays in graph for this purpose.
    batch.Connect(voxelGridNode, VoxelGridNodeConfig::DEBUG_CAPTURE_BUFFER,
                          descriptorGatherer, VoxelRayMarch::traceWriteIndex::BINDING,
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute | SlotRole::Debug));

    // Binding 5: OctreeConfigsUBO (3 x 256 B std140 array) - per-kind octree config
    // BodyOctreeSceneNode outputs OCTREE_CONFIG_BUFFER sized to 3 x kMaxOctrees entries.
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::OCTREE_CONFIG_BUFFER,
                          descriptorGatherer, 5,  // Binding 5 (hardcoded; no SDI regen yet)
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    // Binding 9: idOutputImage (R32_UINT storage image) - AR#35 GPU picking P1. The compute shader
    // writes the per-pixel pick ID here. Execute-only, exactly like the swapchain output at binding 0:
    // PickIdTargetNode re-emits the current ring image's view each frame and the gatherer refreshes it.
    // The shader reflects binding 9 as a STORAGE_IMAGE; DescriptorSetNode writes it with layout GENERAL.
    batch.Connect(pickIdTargetNode, PickIdTargetNodeConfig::ID_IMAGE_VIEW,
                          descriptorGatherer, 9,  // Binding 9: idOutputImage
                          SlotRoleModifier(SlotRole::Execute));

    // Binding 8: ShaderCounters — BodyInstanceRayMarch.comp uses ENABLE_SHADER_COUNTERS at binding 8
    // exactly like the compressed variant did. Still sourced from voxelGridNode (the only node with
    // this buffer; BodyOctreeSceneNode has no shader counters). Must be bound to avoid UB.
    batch.Connect(voxelGridNode, VoxelGridNodeConfig::SHADER_COUNTERS_BUFFER,
                          descriptorGatherer, 8,  // Binding 8: ShaderCountersBuffer
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] Connected debug/counters: binding 4 (voxelGridNode debug capture), binding 8 (voxelGridNode shader counters)");
    }

    // Binding 10: BodyInstanceBuffer (SSBO) — per-body BodyInstanceGpu records (64 B each).
    // M-wire Task 8: this is the NEW binding not present in the dense path.
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::INSTANCE_BUFFER,
                          descriptorGatherer, 10,  // Binding 10: BodyInstanceBuffer
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] Connected body instance SSBO at binding 10 (BodyOctreeSceneNode)");
    }

    // Inc2 M3: Binding 11: SoA-SDF brick SSBO (float[] per-voxel SDF values).
    // Placeholder (1-byte pad) for binary/Procedural bodies; populated by ConcatenateSdf for Stored-SDF.
    // Shader only reads this when OctreeConfig.formatId == FORMAT_STORED_SDF (1u) — dead code for current bodies.
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::OCTREE_SDF_BUFFER,
                          descriptorGatherer, 11,  // Binding 11: SdfBrickBuffer
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    // Inc2 M3: Binding 12: Brick-grid lookup SSBO (uint32[bpa^3] grid-coord→brickIndex table).
    batch.Connect(bodyOctreeSceneNode, BodyOctreeSceneNodeConfig::OCTREE_BRICKLOOKUP_BUFFER,
                          descriptorGatherer, 12,  // Binding 12: BrickLookupBuffer
                          SlotRoleModifier(SlotRole::Dependency | SlotRole::Execute));

    if (mainLogger && mainLogger->IsEnabled()) {
        mainLogger->Info("[BuildRenderGraph] Connected SoA-SDF buffer at binding 11, brick-grid lookup at binding 12 (Inc2 M3)");
    }

    // Swapchain connections to descriptor set and dispatch
    // Pass swapchain public vars; DescriptorSetNode reads swapChainImageCount during Compile.
    // DESCRIPTOR_RESOURCES provides the actual bindings.
    batch.Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  computeDescriptorSet, DescriptorSetNodeConfig::SWAPCHAIN_INFO)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX,
                  computeDescriptorSet, DescriptorSetNodeConfig::IMAGE_INDEX)
         // REMOVED DUPLICATE: descriptorGatherer -> computeDescriptorSet DESCRIPTOR_RESOURCES (already connected at line 919-920)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC,
                  computeDispatch, ComputeDispatchNodeConfig::SWAPCHAIN_INFO)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX,
                  computeDispatch, ComputeDispatchNodeConfig::IMAGE_INDEX);

    // Sync connections
    batch.Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX,
                  computeDispatch, ComputeDispatchNodeConfig::CURRENT_FRAME_INDEX)
         .Connect(frameSyncNode, FrameSyncNodeConfig::IN_FLIGHT_FENCE,
                  computeDispatch, ComputeDispatchNodeConfig::IN_FLIGHT_FENCE)
         .Connect(frameSyncNode, FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY,
                  computeDispatch, ComputeDispatchNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY)
         .Connect(swapChainNode, SwapChainNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY,
                  computeDispatch, ComputeDispatchNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY)
         // P5b M1: wire FrameSyncNode timeline primitives into ComputeDispatchNode
         .Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_SEMAPHORE,
                  computeDispatch, ComputeDispatchNodeConfig::TIMELINE_SEMAPHORE_IN)
         .Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_FRAME_BASE,
                  computeDispatch, ComputeDispatchNodeConfig::TIMELINE_FRAME_BASE_IN);

    // REMOVED DUPLICATE: computeDispatch -> present RENDER_COMPLETE_SEMAPHORE (already connected at line 894-895)

    // ===================================================================
    // UI composite pass: HUD render pass over the compute output, before present.
    // Mirrors BuildUIGraph's RenderPassNode → FramebufferNode → UIRenderNode shape, but the render pass
    // LOADs (initial=General, from the compute) instead of clearing, and the UI node runs in composite
    // mode (waits on the compute→UI handoff, signals its own present semaphore, owns the frame fence).
    // ===================================================================

    // UI render pass: device + swapchain format. (Color-only; no depth → LOAD/initial=General set above.)
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, uiRenderPassNode, RenderPassNodeConfig::VULKAN_DEVICE_IN)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC, uiRenderPassNode, RenderPassNodeConfig::SWAPCHAIN_INFO);

    // UI framebuffers: wrap each swapchain image view against the UI render pass.
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, uiFramebufferNode, FramebufferNodeConfig::VULKAN_DEVICE_IN)
         .Connect(uiRenderPassNode, RenderPassNodeConfig::RENDER_PASS, uiFramebufferNode, FramebufferNodeConfig::RENDER_PASS)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC, uiFramebufferNode, FramebufferNodeConfig::SWAPCHAIN_INFO);

    // UIRenderNode (composite) inputs — mirrors BuildUIGraph's UIRenderNode wiring.
    batch.Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC, uiCompositeNode, UIRenderNodeConfig::SWAPCHAIN_INFO)
         .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL, uiCompositeNode, UIRenderNodeConfig::COMMAND_POOL)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, uiCompositeNode, UIRenderNodeConfig::VULKAN_DEVICE)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX, uiCompositeNode, UIRenderNodeConfig::IMAGE_INDEX)
         .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX, uiCompositeNode, UIRenderNodeConfig::CURRENT_FRAME_INDEX)
         .Connect(frameSyncNode, FrameSyncNodeConfig::IN_FLIGHT_FENCE, uiCompositeNode, UIRenderNodeConfig::IN_FLIGHT_FENCE)
         .Connect(frameSyncNode, FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY, uiCompositeNode, UIRenderNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY)
         .Connect(swapChainNode, SwapChainNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY, uiCompositeNode, UIRenderNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY)
         .Connect(uiRenderPassNode, RenderPassNodeConfig::RENDER_PASS, uiCompositeNode, UIRenderNodeConfig::RENDER_PASS)
         .Connect(uiFramebufferNode, FramebufferNodeConfig::FRAMEBUFFERS, uiCompositeNode, UIRenderNodeConfig::FRAMEBUFFERS)
         // P5b M1: wire FrameSyncNode timeline primitives into UIRenderNode (consumer waits on edges)
         .Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_SEMAPHORE, uiCompositeNode, UIRenderNodeConfig::TIMELINE_SEMAPHORE_IN)
         .Connect(frameSyncNode, FrameSyncNodeConfig::TIMELINE_FRAME_BASE, uiCompositeNode, UIRenderNodeConfig::TIMELINE_FRAME_BASE_IN);

    // P5b M3: the compute→UI ordering is carried by the baked timeline edge for GPU SYNC (memory
    // visibility), but the graph still needs the compute→UI TOPOLOGY edge so the execution order
    // (and hence the timeline edge the scheduler bakes from it) is compute-before-UI. The
    // FrameSyncScheduler derives edge DIRECTION from groupId order (== execution order); without this
    // dependency the topological sort places UI before compute, so it bakes the edge BACKWARDS
    // (UI→compute), tags the COMPUTE group as the swapchain present-signal, and leaves the presented
    // image in GENERAL (compute runs last w/ leaveImageInGeneral) — VUID-...-01430 — while the UI draw
    // sees the image still UNDEFINED — VUID-vkCmdDraw-None-09600. So we keep this connection purely as
    // the ORDERING edge (its documented secondary purpose, UIRenderNodeConfig SWAPCHAIN/COMPOSITE_WAIT):
    // the binary semaphore it carries is INERT — compute no longer SIGNALS renderComplete in composite
    // (ComputeDispatchNode gates it to !leaveImageInGeneral) and UIRenderNode no longer WAITS
    // compositeWait (the M3 binary handoff was dropped from its submit). With the edge in the right
    // direction the scheduler bakes the single compute(GENERAL)→UI(GENERAL) timeline edge (UI gets the
    // waitEdge + waits the compute's timeline value, the timeline semaphore carries cross-submit memory
    // visibility, both layouts GENERAL ⇒ no transition), tags the UI group as present (its render pass
    // owns GENERAL→PRESENT_SRC), and the timeline alone — not a binary handoff — orders compute→UI.
    // WSI acquire (compute waits imageAvailable) and present (UI signals its uiComplete) stay binary.
    batch.Connect(computeDispatch, ComputeDispatchNodeConfig::RENDER_COMPLETE_SEMAPHORE,
                  uiCompositeNode, UIRenderNodeConfig::COMPOSITE_WAIT_SEMAPHORE);

    // Atomically register all connections
    size_t connectionCount = batch.GetConnectionCount();
    mainLogger->Info("Registering " + std::to_string(connectionCount) + " connections...");
    batch.RegisterAll();

    mainLogger->Info("Successfully wired " + std::to_string(connectionCount) + " connections");

    // --- Phase 0.4: Loop Propagation Connections ---
    // TODO: Re-enable loop propagation connections after implementing proper API
    // Note: AUTO_LOOP slots exist on all nodes, but direct Connect() is not exposed on RenderGraph
    // batch.Connect(
    //     physicsLoopBridge, NodeInstance::AUTO_LOOP_OUT_SLOT,
    //     geometryRenderNode, NodeInstance::AUTO_LOOP_IN_SLOT
    // );
    // mainLogger->Info("Connected physics loop propagation to GeometryRenderNode");

    mainLogger->Info("Complete render pipeline built with " + std::to_string(renderGraph->GetNodeCount()) + " nodes");
}
