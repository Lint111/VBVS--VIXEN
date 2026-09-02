// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
// C0/C1 photon-cell params ring.  Lifecycle mirrors HitAccumParamsConfigNode.

#include "Nodes/PhotonCellParamsConfigNode.h"

#include "Core/NodeLogging.h"
#include "Core/NodeRegistration.h"
#include "Core/RenderGraph.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "VulkanDevice.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>

namespace Vixen::RenderGraph {

using namespace Vixen::Vulkan::Resources;

const uint32_t PhotonCellParamsConfigNode::kRingSize =
    FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT;
static constexpr VkDeviceSize kPhotonCellParamsBufferSize = 48;

std::unique_ptr<NodeInstance> PhotonCellParamsConfigNodeType::CreateInstance(
    const std::string& name) const {
    return std::make_unique<PhotonCellParamsConfigNode>(
        name, const_cast<PhotonCellParamsConfigNodeType*>(this));
}

PhotonCellParamsConfigNode::PhotonCellParamsConfigNode(
    const std::string& name, NodeType* type)
    : TypedNode<PhotonCellParamsConfigNodeConfig>(name, type),
      configuration_(ReadConfiguration()) {}

bool PhotonCellParamsConfigNode::EnvironmentFlag(const char* name) {
    const char* raw = std::getenv(name);
    if (!raw) return false;
    while (*raw != '\0' && std::isspace(static_cast<unsigned char>(*raw))) ++raw;
    if (*raw == '\0') return false;
    std::string value(raw);
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value != "0" && value != "false" && value != "off" && value != "no";
}

bool PhotonCellParamsConfigNode::FeatureEnabled() {
    return EnvironmentFlag("VIXEN_PHOTON_CELLS");
}

PhotonCellConfiguration PhotonCellParamsConfigNode::ReadConfiguration() {
    PhotonCellConfiguration configuration;
    if (const char* raw = std::getenv("VIXEN_PHOTON_CELL_SIZE")) {
        const float value = std::strtof(raw, nullptr);
        if (std::isfinite(value) && value > 0.0f) configuration.cellSize0 = value;
    }
    if (const char* raw = std::getenv("VIXEN_PHOTON_CELL_ALPHA")) {
        const float value = std::strtof(raw, nullptr);
        if (std::isfinite(value)) configuration.temporalAlpha =
            std::clamp(value, 0.0f, 1.0f);
    }
    if (const char* raw = std::getenv("VIXEN_PHOTON_CELL_MAX_AGE")) {
        char* end = nullptr;
        const unsigned long value = std::strtoul(raw, &end, 10);
        if (end != raw && value > 0ul) {
            configuration.maxAge = static_cast<uint32_t>(std::min<unsigned long>(
                value, Vixen::SVO::PhotonCells::kHalfGenerationRange));
        }
    }
    if (const char* raw = std::getenv("VIXEN_PHOTON_RADIANCE_CLAMP")) {
        const float value = std::strtof(raw, nullptr);
        if (std::isfinite(value) && value > 0.0f) configuration.radianceClamp = value;
    }
    if (const char* raw = std::getenv("VIXEN_PHOTON_CELLS_DIAG_FRAME")) {
        char* end = nullptr;
        const unsigned long long value = std::strtoull(raw, &end, 10);
        if (end != raw) configuration.diagnosticFrame = value;
    }
    configuration.clearRequested = EnvironmentFlag("VIXEN_PHOTON_CELLS_CLEAR");
    configuration.probeLog = EnvironmentFlag("VIXEN_PHOTON_CELLS_PROBE_LOG");
    return configuration;
}

void PhotonCellParamsConfigNode::SetupImpl(TypedSetupContext&) {
    NODE_LOG_DEBUG("[PhotonCellParamsConfigNode] Setup");
}

void PhotonCellParamsConfigNode::CompileImpl(TypedCompileContext& ctx) {
    SetDevice(ctx.In(PhotonCellParamsConfigNodeConfig::VULKAN_DEVICE_IN));
    if (!GetDevice()) {
        throw std::runtime_error(
            "[PhotonCellParamsConfigNode] VULKAN_DEVICE_IN is null");
    }

    if (!perFrame_.IsInitialized()) {
        perFrame_.Initialize(GetDevice(), kRingSize);
        for (uint32_t i = 0; i < kRingSize; ++i) {
            perFrame_.CreateStorageBuffer(i, kPhotonCellParamsBufferSize);
        }
        NODE_LOG_INFO("[PhotonCellParamsConfigNode] Allocated params ring");
    }
    ctx.Out(PhotonCellParamsConfigNodeConfig::PHOTON_CELL_PARAMS_BUFFER,
            perFrame_.GetUniformBuffer(0));
}

void PhotonCellParamsConfigNode::ExecuteImpl(TypedExecuteContext& ctx) {
    const uint32_t frameIndex =
        ctx.In(PhotonCellParamsConfigNodeConfig::CURRENT_FRAME_INDEX) % kRingSize;
    ctx.Out(PhotonCellParamsConfigNodeConfig::PHOTON_CELL_PARAMS_BUFFER,
            perFrame_.GetUniformBuffer(frameIndex));
}

void* PhotonCellParamsConfigNode::MapCurrentForWrite(uint32_t frameIndex) const {
    if (!perFrame_.IsInitialized()) return nullptr;
    return perFrame_.GetUniformBufferMapped(frameIndex % kRingSize);
}

void PhotonCellParamsConfigNode::PrepareFrame(uint32_t frameIndex,
                                              float primaryCoef,
                                              uint64_t frameNumber) {
    void* mapped = MapCurrentForWrite(frameIndex);
    if (!mapped) return;

    generation_ = Vixen::SVO::PhotonCells::NextGeneration(generation_);
    primaryCoef_ = primaryCoef;
    primaryBias_ = 0.0f;
    auto* params = reinterpret_cast<Vixen::SVO::PhotonCells::PhotonCellParams*>(mapped);
    params->generation = generation_;
    params->primaryCoef = primaryCoef_;
    params->primaryBias = primaryBias_;
    params->cellSize0 = configuration_.cellSize0;
    params->misc0 = {configuration_.temporalAlpha, configuration_.radianceClamp,
                     static_cast<float>(configuration_.maxAge), 0.0f};
    params->misc1 = {0.0f, 0.0f, 0.0f, 0.0f};

    if (configuration_.diagnosticFrame != 0 &&
        frameNumber == configuration_.diagnosticFrame) {
        diagnosticGeneration_ = generation_;
    }
}

bool PhotonCellParamsConfigNode::DiagnosticDue(uint64_t frameNumber) const {
    return configuration_.diagnosticFrame != 0 && !diagnosticFired_ &&
           frameNumber >= configuration_.diagnosticFrame && diagnosticGeneration_ != 0;
}

void PhotonCellParamsConfigNode::CleanupImpl(TypedCleanupContext& ctx) {
    if (ctx.reason == CleanupReason::Recompile) return;
    perFrame_.Cleanup();
}

} // namespace Vixen::RenderGraph

VIXEN_REGISTER_NODE(Vixen::RenderGraph::PhotonCellParamsConfigNodeType);
