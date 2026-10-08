#include "Nodes/MiningBeamBufferNode.h"
#include "Core/NodeLogging.h"
#include "Core/NodeRegistration.h"
#include "Core/RenderGraph.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "Generated/MiningBeamBuffer.g.h"
#include "VulkanDevice.h"
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;

const uint32_t MiningBeamBufferNode::kRingSize = FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT;

std::unique_ptr<NodeInstance> MiningBeamBufferNodeType::CreateInstance(const std::string& name) const {
    return std::make_unique<MiningBeamBufferNode>(name, const_cast<MiningBeamBufferNodeType*>(this));
}

MiningBeamBufferNode::MiningBeamBufferNode(const std::string& name, NodeType* type)
    : TypedNode<MiningBeamBufferNodeConfig>(name, type) {}

void MiningBeamBufferNode::SetBeams(std::vector<MiningBeamInput> beams) {
    constexpr size_t kCapacity = sizeof(Vixen::Gpu::MiningBeamBuffer::beams) /
                                 sizeof(Vixen::Gpu::MiningBeamGpu);
    if (beams.size() > kCapacity) {
        throw std::length_error("MiningBeamBufferNode accepts at most " +
                                std::to_string(kCapacity) + " beams");
    }
    for (const MiningBeamInput& beam : beams) {
        const bool finiteOffsets = std::isfinite(beam.sourceLocalOffset.x) &&
            std::isfinite(beam.sourceLocalOffset.y) && std::isfinite(beam.sourceLocalOffset.z) &&
            std::isfinite(beam.targetLocalOffset.x) && std::isfinite(beam.targetLocalOffset.y) &&
            std::isfinite(beam.targetLocalOffset.z);
        const bool finiteAppearance = std::isfinite(beam.radius) && std::isfinite(beam.luminosity) &&
            std::isfinite(beam.purposeScale) && std::isfinite(beam.color.x) &&
            std::isfinite(beam.color.y) && std::isfinite(beam.color.z);
        if (!finiteOffsets || !finiteAppearance || beam.radius <= 0.0f ||
            beam.luminosity < 0.0f || beam.purposeScale < 0.0f ||
            beam.color.x < 0.0f || beam.color.y < 0.0f || beam.color.z < 0.0f) {
            throw std::invalid_argument("MiningBeamBufferNode beam values must be finite, with "
                                        "positive radius and non-negative emission values");
        }
    }
    beams_ = std::move(beams);
}

void MiningBeamBufferNode::TypedSetupImpl(TypedSetupContext&) {
    NODE_LOG_DEBUG("[MiningBeamBufferNode] Setup (graph-scope initialization)");
}

void MiningBeamBufferNode::TypedCompileImpl(TypedCompileContext& ctx) {
    SetDevice(ctx.In(MiningBeamBufferNodeConfig::VULKAN_DEVICE_IN));
    if (!GetDevice()) {
        throw std::runtime_error("[MiningBeamBufferNode] Vulkan device input is null");
    }

    constexpr VkDeviceSize kBufferSize = sizeof(Vixen::Gpu::MiningBeamBuffer);
    if (!perFrame_.IsInitialized()) {
        perFrame_.Initialize(GetDevice(), kRingSize);
        for (uint32_t i = 0; i < kRingSize; ++i) {
            perFrame_.CreateStorageBuffer(i, kBufferSize);
        }
    }
    ctx.Out(MiningBeamBufferNodeConfig::MINING_BEAM_BUFFER, perFrame_.GetUniformBuffer(0));
}

void MiningBeamBufferNode::TypedExecuteImpl(TypedExecuteContext& ctx) {
    const uint32_t frameIndex = ctx.In(MiningBeamBufferNodeConfig::CURRENT_FRAME_INDEX) % kRingSize;
    Vixen::Gpu::MiningBeamBuffer gpuBuffer{};
    const bool hasWork = enabled_ && !beams_.empty();
    gpuBuffer.beamCount = hasWork ? static_cast<uint32_t>(beams_.size()) : 0u;
    gpuBuffer.enabled = hasWork ? 1u : 0u;

    for (size_t i = 0; i < beams_.size(); ++i) {
        const MiningBeamInput& source = beams_[i];
        Vixen::Gpu::MiningBeamGpu& target = gpuBuffer.beams[i];
        target.sourceInstanceIndex = source.sourceInstanceIndex;
        target.targetInstanceIndex = source.targetInstanceIndex;
        target.radius = source.radius;
        target.luminosity = source.luminosity;
        target.sourceLocalOffsetX = source.sourceLocalOffset.x;
        target.sourceLocalOffsetY = source.sourceLocalOffset.y;
        target.sourceLocalOffsetZ = source.sourceLocalOffset.z;
        target.purposeScale = source.purposeScale;
        target.targetLocalOffsetX = source.targetLocalOffset.x;
        target.targetLocalOffsetY = source.targetLocalOffset.y;
        target.targetLocalOffsetZ = source.targetLocalOffset.z;
        target.colorX = source.color.x;
        target.colorY = source.color.y;
        target.colorZ = source.color.z;
    }

    if (void* mapped = perFrame_.GetUniformBufferMapped(frameIndex)) {
        std::memcpy(mapped, &gpuBuffer, sizeof(gpuBuffer));
    }
    ctx.Out(MiningBeamBufferNodeConfig::MINING_BEAM_BUFFER,
            perFrame_.GetUniformBuffer(frameIndex));
}

void MiningBeamBufferNode::TypedCleanupImpl(TypedCleanupContext& ctx) {
    if (ctx.reason == CleanupReason::Recompile) return;
    perFrame_.Cleanup();
}

} // namespace Vixen::RenderGraph

VIXEN_REGISTER_NODE(Vixen::RenderGraph::MiningBeamBufferNodeType);
