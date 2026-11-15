#include "Nodes/TileBinViewNode.h"
#include "Core/NodeRegistration.h"
#include "Core/NodeLogging.h"
#include "Generated/TileBinView.g.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/glm.hpp>

namespace Vixen::RenderGraph {

using Vixen::Codegen::Gpu::TileBinView;
using Vixen::Vulkan::Resources::VulkanDevice;

const uint32_t TileBinViewNode::kRingSize = FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT;

std::unique_ptr<NodeInstance> TileBinViewNodeType::CreateInstance(const std::string& name) const {
    return std::make_unique<TileBinViewNode>(name, const_cast<TileBinViewNodeType*>(this));
}

TileBinViewNode::TileBinViewNode(const std::string& name, NodeType* type)
    : TypedNode<TileBinViewNodeConfig>(name, type) {}

void TileBinViewNode::TypedSetupImpl(TypedSetupContext&) {}

void TileBinViewNode::TypedCompileImpl(TypedCompileContext& ctx) {
    device_ = ctx.In(TileBinViewNodeConfig::VULKAN_DEVICE_IN);
    if (!device_) {
        NODE_LOG_ERROR("[TileBinViewNode] VULKAN_DEVICE_IN is null");
        return;
    }
    if (!viewRing_.IsInitialized()) viewRing_.Initialize(device_, kRingSize);
    for (uint32_t i = 0; i < kRingSize; ++i) {
        if (viewRing_.GetUniformBuffer(i) == VK_NULL_HANDLE)
            viewRing_.CreateStorageBuffer(i, sizeof(TileBinView));
    }
    ctx.Out(TileBinViewNodeConfig::VIEW_BUFFER, viewRing_.GetUniformBuffer(0));
}

void TileBinViewNode::TypedExecuteImpl(TypedExecuteContext& ctx) {
    const uint32_t frame = ctx.In(TileBinViewNodeConfig::CURRENT_FRAME_INDEX) % kRingSize;
    const uint32_t width = ctx.In(TileBinViewNodeConfig::WIDTH);
    const uint32_t height = ctx.In(TileBinViewNodeConfig::HEIGHT);
    const CameraData& camera = ctx.In(TileBinViewNodeConfig::CAMERA_DATA);
    auto* view = static_cast<TileBinView*>(viewRing_.GetUniformBufferMapped(frame));
    if (!view) return;

    *view = {};
    const glm::vec3 forward = glm::normalize(camera.cameraDir);
    const glm::vec3 right = glm::normalize(camera.cameraRight);
    const glm::vec3 up = glm::normalize(camera.cameraUp);
    view->viewXx = right.x; view->viewXy = right.y; view->viewXz = right.z;
    view->viewXw = -glm::dot(right, camera.cameraPos);
    view->viewYx = up.x; view->viewYy = up.y; view->viewYz = up.z;
    view->viewYw = -glm::dot(up, camera.cameraPos);
    view->viewZx = forward.x; view->viewZy = forward.y; view->viewZz = forward.z;
    view->viewZw = -glm::dot(forward, camera.cameraPos);
    view->focalX = 0.5f * static_cast<float>(height) / std::tan(0.5f * camera.fov);
    view->focalY = view->focalX;
    view->centerX = 0.5f * static_cast<float>(width);
    view->centerY = 0.5f * static_cast<float>(height);
    view->nearPlane = 0.001f;
    view->tileSize = 8.0f;
    view->tilesX = static_cast<int32_t>((width + 7u) / 8u);
    view->tilesY = static_cast<int32_t>((height + 7u) / 8u);
    view->listCapacityLog2 = 9; // 512 candidates per tile; no 192-instance cap.
    view->instanceCount = std::max(ctx.In(TileBinViewNodeConfig::INSTANCE_COUNT), 0);
    ctx.Out(TileBinViewNodeConfig::VIEW_BUFFER, viewRing_.GetUniformBuffer(frame));
}

void TileBinViewNode::TypedCleanupImpl(TypedCleanupContext&) {
    viewRing_.Cleanup();
}

} // namespace Vixen::RenderGraph

VIXEN_REGISTER_NODE(Vixen::RenderGraph::TileBinViewNodeType);
