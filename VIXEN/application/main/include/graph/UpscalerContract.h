#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "graph/RenderModuleSelection.h"

namespace Vixen::RenderGraph {

enum class ReconstructionDepthConvention : uint32_t;

struct UpscalerExtent {
    uint32_t width = 0;
    uint32_t height = 0;
};

struct UpscalerExtentSupport {
    UpscalerExtent minimum{1, 1};
    UpscalerExtent maximum{
        std::numeric_limits<uint32_t>::max(),
        std::numeric_limits<uint32_t>::max()};
    uint32_t widthMultiple = 1;
    uint32_t heightMultiple = 1;

    constexpr bool Supports(UpscalerExtent extent) const noexcept {
        return extent.width >= minimum.width && extent.width <= maximum.width &&
               extent.height >= minimum.height && extent.height <= maximum.height &&
               widthMultiple != 0 && heightMultiple != 0 &&
               extent.width % widthMultiple == 0 && extent.height % heightMultiple == 0;
    }
};

// Depth values are shared engine data, but each backend consumes a declared convention. Keep
// these rows on the provider contract so an SDK adapter cannot silently reinterpret the common
// normalized-depth image.
struct UpscalerDepthConsumerConvention {
    std::string_view consumer;
    ReconstructionDepthConvention convention{};
};

enum class UpscalerContextLifetime : uint8_t {
    None,
    Frame,
    RenderGraph,
    Device
};

enum class UpscalerExecutionRecording : uint8_t {
    RenderGraph,
    ExternalCommandBuffer
};

enum class UpscalerExternalResource : uint8_t {
    SourceColor,
    PresentationTarget,
    Device,
    ExecutionOrder
};

struct UpscalerExternalResourceDependency {
    UpscalerExternalResource resource;
    bool required;
};

struct UpscalerContract {
    UpscalerExtentSupport inputExtents{};
    UpscalerExtentSupport outputExtents{};
    std::span<const RenderModuleImageFormat> inputFormats{};
    std::span<const RenderModuleImageFormat> outputFormats{};
    std::span<const RenderModuleFrameInput> requiredFrameInputs{};
    std::span<const UpscalerDepthConsumerConvention> depthConventions{};
    UpscalerContextLifetime contextLifetime = UpscalerContextLifetime::None;
    bool retainsHistory = false;
    RenderModuleHistoryReset resetHistory = nullptr;
    UpscalerExecutionRecording executionRecording = UpscalerExecutionRecording::RenderGraph;
    std::span<const UpscalerExternalResourceDependency> externalResourceDependencies{};
};

inline std::optional<ReconstructionDepthConvention> FindUpscalerDepthConvention(
    const UpscalerContract& contract, std::string_view consumer) noexcept {
    const auto found = std::find_if(contract.depthConventions.begin(),
                                    contract.depthConventions.end(),
                                    [consumer](const auto& row) {
                                        return row.consumer == consumer;
                                    });
    if (found == contract.depthConventions.end()) return std::nullopt;
    return found->convention;
}

struct UpscalerFrameDescription {
    UpscalerExtent inputExtent{};
    UpscalerExtent outputExtent{};
    RenderModuleImageFormat inputFormat = RenderModuleImageFormat::Rgba8Unorm;
    RenderModuleImageFormat outputFormat = RenderModuleImageFormat::Rgba8Unorm;
    std::span<const RenderModuleFrameInput> availableFrameInputs{};
};

inline bool SupportsUpscalerFrame(const UpscalerContract& contract,
                                 const UpscalerFrameDescription& frame) noexcept {
    if (!contract.inputExtents.Supports(frame.inputExtent) ||
        !contract.outputExtents.Supports(frame.outputExtent) ||
        std::find(contract.inputFormats.begin(), contract.inputFormats.end(),
                  frame.inputFormat) == contract.inputFormats.end() ||
        std::find(contract.outputFormats.begin(), contract.outputFormats.end(),
                  frame.outputFormat) == contract.outputFormats.end()) {
        return false;
    }

    return ProvidesRenderModuleFrameInputs(contract.requiredFrameInputs, frame.availableFrameInputs);
}

inline void ResetUpscalerHistory(const UpscalerContract& contract,
                                 void* context,
                                 RenderModuleHistoryResetCause cause) noexcept {
    if (contract.resetHistory) {
        contract.resetHistory(context, cause);
    }
}

} // namespace Vixen::RenderGraph
