#include "Nodes/TileBinViewNode.h"
#include "Core/NodeRegistration.h"
#include "Core/NodeLogging.h"
#include "Nodes/TileBinViewProjection.h"

#include <cstdlib>
#include <sstream>

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

    const int32_t instanceCount = ctx.In(TileBinViewNodeConfig::INSTANCE_COUNT);
    *view = MakeTileBinView(camera, width, height, instanceCount);
    if (const char* trace = std::getenv("VIXEN_EDITOR_TILEBIN_READBACK");
        trace && std::string(trace) == "1") {
        std::ostringstream line;
        line << "[TileBinView] frame=" << frame << " extent=" << width << 'x' << height
             << " instances=" << instanceCount
             << " cameraPos=(" << camera.cameraPos.x << ',' << camera.cameraPos.y << ',' << camera.cameraPos.z << ')'
             << " dir=(" << camera.cameraDir.x << ',' << camera.cameraDir.y << ',' << camera.cameraDir.z << ')'
             << " up=(" << camera.cameraUp.x << ',' << camera.cameraUp.y << ',' << camera.cameraUp.z << ')'
             << " right=(" << camera.cameraRight.x << ',' << camera.cameraRight.y << ',' << camera.cameraRight.z << ')'
             << " fov=" << camera.fov << " aspect=" << camera.aspect
             << " near=" << view->nearPlane << " focal=(" << view->focalX << ',' << view->focalY << ')'
             << " center=(" << view->centerX << ',' << view->centerY << ')'
             << " tileExtent=" << view->tilesX << 'x' << view->tilesY;
        NODE_LOG_INFO(line.str());
    }
    ctx.Out(TileBinViewNodeConfig::VIEW_BUFFER, viewRing_.GetUniformBuffer(frame));
}

void TileBinViewNode::TypedCleanupImpl(TypedCleanupContext&) {
    viewRing_.Cleanup();
}

} // namespace Vixen::RenderGraph

VIXEN_REGISTER_NODE(Vixen::RenderGraph::TileBinViewNodeType);
