// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include "Core/BarrierTypes.h"   // AccessKind
#include "Data/DispatchPass.h"   // PushConstantData

namespace Vixen::RenderGraph {

class Resource;

/// One resource touch by one pass — the node-local twin of P3's slot-level accessKind.
/// `resource` is a node-local identity used only for hazard correlation (pointer compare).
struct PassResourceAccess {
    const Resource* resource = nullptr;
    AccessKind      kind     = AccessKind::None;
    bool            isImage  = false;   // P4: false (buffer proof). P5 turns on image barriers.
};

/// A compute dispatch step.
struct ComputePassStep {
    VkPipeline                   pipeline = VK_NULL_HANDLE;
    VkPipelineLayout             layout   = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;
    uint32_t                     firstSet = 0;
    std::optional<PushConstantData> pushConstants;
    glm::uvec3                   workGroupCount = {1, 1, 1};
    std::vector<PassResourceAccess> accesses;
    std::string                  debugName;
};

/// A graphics (render) step. Consumes a VkRenderPass + framebuffers from existing nodes.
struct RenderPassStep {
    VkPipeline                   pipeline   = VK_NULL_HANDLE;   // graphics pipeline
    VkPipelineLayout             layout     = VK_NULL_HANDLE;
    VkRenderPass                 renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer>   framebuffers;                  // one per swapchain image
    VkExtent2D                   renderArea = {0, 0};
    std::vector<VkClearValue>    clearValues;
    std::vector<VkDescriptorSet> descriptorSets;
    uint32_t                     firstSet = 0;
    std::optional<PushConstantData> pushConstants;
    uint32_t                     vertexCount   = 3;             // fullscreen triangle default
    uint32_t                     instanceCount = 1;
    std::optional<VkBuffer>      vertexBuffer;                  // none for fullscreen draw
    std::vector<PassResourceAccess> accesses;
    std::string                  debugName;
};

using PassStep = std::variant<ComputePassStep, RenderPassStep>;

/// Accessor for the access list of any PassStep variant.
[[nodiscard]] inline const std::vector<PassResourceAccess>& StepAccesses(const PassStep& s) {
    return std::visit([](const auto& step) -> const std::vector<PassResourceAccess>& {
        return step.accesses;
    }, s);
}

} // namespace Vixen::RenderGraph
