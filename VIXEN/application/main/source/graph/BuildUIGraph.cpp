// BuildUIGraph -- extracted from VulkanGraphApplication.cpp (M4: per-subgraph construction TU).
// Editing a node config now recompiles only the subgraph TU(s) wiring it, not the
// app's lifecycle code. Node includes below are derived from this subgraph's wiring.
#include "VulkanGraphApplication.h"
#include "Connection/ConnectionModifier.h"
#include "Connection/Modifiers/FieldExtractionModifier.h"
#include "Core/NodeRegistration.h"
#include "MeshData.h"
// --- nodes this subgraph wires ---
#include "Data/Nodes/CommandPoolNodeConfig.h"
#include "Data/Nodes/DeviceNodeConfig.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "Data/Nodes/FramebufferNodeConfig.h"
#include "Data/Nodes/InstanceNodeConfig.h"
#include "Data/Nodes/PresentNodeConfig.h"
#include "Data/Nodes/RenderPassNodeConfig.h"
#include "Data/Nodes/SwapChainNodeConfig.h"
#include "Data/Nodes/UIRenderNodeConfig.h"
#include "Data/Nodes/WindowNodeConfig.h"
#include "Nodes/CommandPoolNode.h"
#include "Nodes/DeviceNode.h"
#include "Nodes/FrameSyncNode.h"
#include "Nodes/FramebufferNode.h"
#include "Nodes/InstanceNode.h"
#include "Nodes/PresentNode.h"
#include "Nodes/RenderPassNode.h"
#include "Nodes/SwapChainNode.h"
#include "Nodes/UIRenderNode.h"
#include "Nodes/WindowNode.h"

void VulkanGraphApplication::BuildUIGraph() {
    using namespace Vixen::RenderGraph;
    mainLogger->Info("Building UI-only RmlUi demo graph");

    NodeHandle instanceNode    = renderGraph->AddNode<InstanceNodeType>("ui_instance");
    NodeHandle deviceNode      = renderGraph->AddNode<DeviceNodeType>("ui_device");
    NodeHandle windowNode      = renderGraph->AddNode<WindowNodeType>("main_window");
    NodeHandle swapChainNode   = renderGraph->AddNode<SwapChainNodeType>("ui_swapchain");
    NodeHandle commandPoolNode = renderGraph->AddNode<CommandPoolNodeType>("ui_cmd_pool");
    NodeHandle frameSyncNode   = renderGraph->AddNode<FrameSyncNodeType>("ui_frame_sync");
    NodeHandle renderPassNode  = renderGraph->AddNode<RenderPassNodeType>("ui_render_pass");
    NodeHandle framebufferNode = renderGraph->AddNode<FramebufferNodeType>("ui_framebuffer");
    NodeHandle uiRenderNode    = renderGraph->AddNode<UIRenderNodeType>("ui_render");
    NodeHandle presentNode     = renderGraph->AddNode<PresentNodeType>("ui_present");

    auto* window = static_cast<WindowNode*>(renderGraph->GetInstance(windowNode));
    window->SetParameter(WindowNodeConfig::PARAM_WIDTH, static_cast<uint32_t>(width));
    window->SetParameter(WindowNodeConfig::PARAM_HEIGHT, static_cast<uint32_t>(height));
    auto* device = static_cast<DeviceNode*>(renderGraph->GetInstance(deviceNode));
    device->SetParameter(DeviceNodeConfig::PARAM_GPU_INDEX, DeviceNodeConfig::GPU_INDEX_AUTO);
    auto* present = static_cast<PresentNode*>(renderGraph->GetInstance(presentNode));
    present->SetParameter(PresentNodeConfig::WAIT_FOR_IDLE, true);

    // Color-only render pass (no depth): clear → store → present-src. FramebufferNode wraps each
    // swapchain image view; both recreate on resize via the swapchain recompile cascade, so the
    // swapchain-derived resource lifecycle lives here, not in UIRenderNode.
    auto* renderPass = static_cast<RenderPassNode*>(renderGraph->GetInstance(renderPassNode));
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_COLOR_LOAD_OP, AttachmentLoadOp::Clear);
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_COLOR_STORE_OP, AttachmentStoreOp::Store);
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_INITIAL_LAYOUT, ImageLayout::Undefined);
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_FINAL_LAYOUT, ImageLayout::PresentSrc);
    renderPass->SetParameter(RenderPassNodeConfig::PARAM_SAMPLES, 1u);
    auto* framebuffer = static_cast<FramebufferNode*>(renderGraph->GetInstance(framebufferNode));
    framebuffer->SetParameter(FramebufferNodeConfig::PARAM_LAYERS, 1u);

    ConnectionBatch batch(renderGraph);

    // --- Infrastructure (mirrors BuildRenderGraph's core chain) ---
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
    // FR-3: renderComplete + presentFences are now PRODUCED by swapChainNode (sized to the actual image count).
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, commandPoolNode, CommandPoolNodeConfig::VULKAN_DEVICE_IN);

    // --- Render pass + framebuffers (swapchain-derived; these nodes own the resize lifecycle) ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, renderPassNode, RenderPassNodeConfig::VULKAN_DEVICE_IN)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC, renderPassNode, RenderPassNodeConfig::SWAPCHAIN_INFO);
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, framebufferNode, FramebufferNodeConfig::VULKAN_DEVICE_IN)
         .Connect(renderPassNode, RenderPassNodeConfig::RENDER_PASS, framebufferNode, FramebufferNodeConfig::RENDER_PASS)
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC, framebufferNode, FramebufferNodeConfig::SWAPCHAIN_INFO);

    // --- UIRenderNode inputs (slots into the render-node position) ---
    batch.Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_PUBLIC, uiRenderNode, UIRenderNodeConfig::SWAPCHAIN_INFO)
         .Connect(commandPoolNode, CommandPoolNodeConfig::COMMAND_POOL, uiRenderNode, UIRenderNodeConfig::COMMAND_POOL)
         .Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, uiRenderNode, UIRenderNodeConfig::VULKAN_DEVICE)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX, uiRenderNode, UIRenderNodeConfig::IMAGE_INDEX)
         .Connect(frameSyncNode, FrameSyncNodeConfig::CURRENT_FRAME_INDEX, uiRenderNode, UIRenderNodeConfig::CURRENT_FRAME_INDEX)
         .Connect(frameSyncNode, FrameSyncNodeConfig::IN_FLIGHT_FENCE, uiRenderNode, UIRenderNodeConfig::IN_FLIGHT_FENCE)
         .Connect(frameSyncNode, FrameSyncNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY, uiRenderNode, UIRenderNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY)
         .Connect(swapChainNode, SwapChainNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY, uiRenderNode, UIRenderNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY)
         .Connect(renderPassNode, RenderPassNodeConfig::RENDER_PASS, uiRenderNode, UIRenderNodeConfig::RENDER_PASS)
         .Connect(framebufferNode, FramebufferNodeConfig::FRAMEBUFFERS, uiRenderNode, UIRenderNodeConfig::FRAMEBUFFERS);

    // --- Present ---
    batch.Connect(deviceNode, DeviceNodeConfig::VULKAN_DEVICE_OUT, presentNode, PresentNodeConfig::VULKAN_DEVICE_IN)
         // Use the raw VkSwapchainKHR output (SWAPCHAIN_HANDLE), NOT SWAPCHAIN_PUBLIC: the
         // SwapChainPublicVariables*->VkSwapchainKHR implicit conversion is not invoked across the typed
         // connection on GCC (struct has many conversion operators), so SWAPCHAIN_PUBLIC would hand
         // present the struct pointer instead of the handle and fault inside vkQueuePresentKHR.
         .Connect(swapChainNode, SwapChainNodeConfig::SWAPCHAIN_HANDLE, presentNode, PresentNodeConfig::SWAPCHAIN)
         .Connect(swapChainNode, SwapChainNodeConfig::IMAGE_INDEX, presentNode, PresentNodeConfig::IMAGE_INDEX)
         .Connect(uiRenderNode, UIRenderNodeConfig::RENDER_COMPLETE_SEMAPHORE, presentNode, PresentNodeConfig::RENDER_COMPLETE_SEMAPHORE)
         .Connect(swapChainNode, SwapChainNodeConfig::PRESENT_FENCES_ARRAY, presentNode, PresentNodeConfig::PRESENT_FENCE_ARRAY);

    batch.RegisterAll();
    mainLogger->Info("UI-only RmlUi demo graph built (10 nodes)");
}
