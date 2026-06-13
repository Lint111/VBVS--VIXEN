#include "Nodes/UIRenderNode.h"

#include "VulkanDevice.h"
#include "VulkanSwapChain.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>

#include <cstdint>

namespace Vixen::RenderGraph {

std::unique_ptr<NodeInstance> UIRenderNodeType::CreateInstance(const std::string& instanceName) const {
    return std::make_unique<UIRenderNode>(instanceName, const_cast<UIRenderNodeType*>(this));
}

UIRenderNode::UIRenderNode(const std::string& instanceName, NodeType* nodeType)
    : TypedNode<UIRenderNodeConfig>(instanceName, nodeType) {}

void UIRenderNode::SetupImpl(TypedSetupContext& /*ctx*/) {}

void UIRenderNode::FreeCommandBuffers() {
    if (!commandBuffers_.empty() && commandPool_ && device_ != VK_NULL_HANDLE)
        vkFreeCommandBuffers(device_, commandPool_, static_cast<uint32_t>(commandBuffers_.size()), commandBuffers_.data());
    commandBuffers_.clear();
}

void UIRenderNode::CompileImpl(TypedCompileContext& ctx) {
    VulkanDevice* device = ctx.In(UIRenderNodeConfig::VULKAN_DEVICE);
    SwapChainPublicVariables* sc = ctx.In(UIRenderNodeConfig::SWAPCHAIN_INFO);
    commandPool_ = ctx.In(UIRenderNodeConfig::COMMAND_POOL);
    renderPass_ = ctx.In(UIRenderNodeConfig::RENDER_PASS);

    device_ = device->device;
    queue_ = device->queue;
    extent_ = sc->Extent;

    if (!initialized_) {
        // One-time: the RmlUi render interface/pipeline (built against the consumed render pass —
        // render passes of the same colour format are compatible, so the pipeline survives a resize),
        // RmlUi global init, the context, and the document.
        renderInterface_.Init(device->device, *device->gpu, device->queue, device->graphicsQueueIndex,
                              device->gpuMemoryProperties, commandPool_, renderPass_);
        Rml::SetSystemInterface(&systemInterface_);
        Rml::SetRenderInterface(&renderInterface_);
        Rml::Initialise();
        const std::string fontPath = GetParameterValue<std::string>(UIRenderNodeConfig::FONT_PATH, "assets/ui/LatoLatin-Regular.ttf");
        const std::string docPath = GetParameterValue<std::string>(UIRenderNodeConfig::RML_DOCUMENT_PATH, "assets/ui/demo.rml");
        Rml::LoadFontFace(fontPath);
        context_ = Rml::CreateContext("vixen_ui", Rml::Vector2i(static_cast<int>(extent_.width), static_cast<int>(extent_.height)));
        if (context_) {
            // S1: construct the "hud" data model so hud.rml (data-model="hud") can bind tick + bodyCount.
            if (Rml::DataModelConstructor c = context_->CreateDataModel("hud")) {
                c.Bind("tick", &hud_.tick);
                c.Bind("bodyCount", &hud_.bodyCount);
                hudModel_ = c.GetModelHandle();
            }
            document_ = context_->LoadDocument(docPath);
            if (document_) document_->Show();
        }
        initialized_ = true;
    } else if (context_) {
        // Recompile (window resize): RenderPassNode/FramebufferNode rebuilt the render pass +
        // framebuffers for the new extent; just re-fit the RmlUi document to the new size.
        context_->SetDimensions(Rml::Vector2i(static_cast<int>(extent_.width), static_cast<int>(extent_.height)));
    }

    // (Re)allocate one command buffer per swapchain image (idempotent: free any stale ones first).
    FreeCommandBuffers();
    commandBuffers_.resize(sc->swapChainImageCount);
    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = commandPool_;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());
    vkAllocateCommandBuffers(device_, &cbai, commandBuffers_.data());
}

void UIRenderNode::RecordFrame(VkCommandBuffer cmd, VkFramebuffer framebuffer) {
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cmd, &bi);

    VkClearValue clear{};
    clear.color = {{0.05f, 0.05f, 0.08f, 1.0f}};
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = renderPass_;
    rp.framebuffer = framebuffer;
    rp.renderArea.offset = {0, 0};
    rp.renderArea.extent = extent_;
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    renderInterface_.BeginFrame(cmd, extent_);
    if (context_) {
        context_->SetDimensions(Rml::Vector2i(static_cast<int>(extent_.width), static_cast<int>(extent_.height)));
        context_->Update();
        context_->Render();
    }

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);
}

void UIRenderNode::ExecuteImpl(TypedExecuteContext& ctx) {
    const uint32_t imageIndex = ctx.In(UIRenderNodeConfig::IMAGE_INDEX);
    const uint32_t currentFrameIndex = ctx.In(UIRenderNodeConfig::CURRENT_FRAME_INDEX);
    const std::vector<VkSemaphore>& imageAvailable = ctx.In(UIRenderNodeConfig::IMAGE_AVAILABLE_SEMAPHORES_ARRAY);
    const std::vector<VkSemaphore>& renderComplete = ctx.In(UIRenderNodeConfig::RENDER_COMPLETE_SEMAPHORES_ARRAY);
    VkFence inFlightFence = ctx.In(UIRenderNodeConfig::IN_FLIGHT_FENCE);
    const std::vector<VkFramebuffer>& framebuffers = ctx.In(UIRenderNodeConfig::FRAMEBUFFERS);

    if (imageIndex == UINT32_MAX || imageIndex >= commandBuffers_.size() || imageIndex >= framebuffers.size()) return;

    VkSemaphore waitSem = imageAvailable[currentFrameIndex];
    VkSemaphore signalSem = renderComplete[imageIndex];

    vkResetFences(device_, 1, &inFlightFence);

    VkCommandBuffer cmd = commandBuffers_[imageIndex];
    RecordFrame(cmd, framebuffers[imageIndex]);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &waitSem;
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &signalSem;
    vkQueueSubmit(queue_, 1, &si, inFlightFence);

    ctx.Out(UIRenderNodeConfig::COMMAND_BUFFERS, cmd);
    ctx.Out(UIRenderNodeConfig::RENDER_COMPLETE_SEMAPHORE, signalSem);
}

void UIRenderNode::SetHudData(int tick, int bodyCount) {
    hud_.tick = tick;
    hud_.bodyCount = bodyCount;
    if (hudModel_) {
        hudModel_.DirtyVariable("tick");
        hudModel_.DirtyVariable("bodyCount");
    }
}

void UIRenderNode::CleanupImpl(TypedCleanupContext& /*ctx*/) {
    if (device_ == VK_NULL_HANDLE) return;

    // Free only the per-image command buffers. No vkDeviceWaitIdle: during a recompile the graph
    // relies on FrameSyncNode for frame-in-flight sync (RenderGraph::RecompileDirtyNodes skips device
    // waits on purpose), and a wait on a submit still blocked on an un-signalled acquire semaphore
    // would deadlock. The render pass + framebuffers are owned by RenderPassNode/FramebufferNode.
    //
    // We deliberately do NOT tear RmlUi (context/document/pipeline) down here: a resize triggers a
    // recompile (Cleanup → Compile), and distinguishing that from a true shutdown is unreliable
    // (NeedsRecompile() doesn't hold under the cascading recompiles a resize produces). Tearing RmlUi
    // down per recompile re-initialized it every resize, which both churned and disrupted rendering.
    // The persistent state is kept so CompileImpl's resize path just re-fits + rebuilds the command
    // buffers. RmlUi is reclaimed at process exit (the demo is a short-lived process).
    FreeCommandBuffers();
}

} // namespace Vixen::RenderGraph
