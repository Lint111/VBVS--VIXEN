#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

// Formats and frame semantics are engine vocabulary. Provider adapters translate them to the
// format and input names used by their own APIs.
enum class UpscalerImageFormat : uint8_t {
    Rgba8Unorm,
    Rgba16Float,
    Rg16Float,
    R32Float
};

enum class UpscalerFrameInput : uint8_t {
    Color,
    Depth,
    MotionVectors,
    Jitter,
    Exposure,
    ReactiveMask,
    TransparencyMask
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

enum class UpscalerHistoryResetCause : uint8_t {
    InputExtentChanged,
    OutputExtentChanged,
    CameraCut,
    DeviceChanged,
    Explicit
};

using UpscalerHistoryReset = void (*)(void* context,
                                      UpscalerHistoryResetCause cause) noexcept;

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
    std::span<const UpscalerImageFormat> inputFormats{};
    std::span<const UpscalerImageFormat> outputFormats{};
    std::span<const UpscalerFrameInput> requiredFrameInputs{};
    std::span<const UpscalerDepthConsumerConvention> depthConventions{};
    UpscalerContextLifetime contextLifetime = UpscalerContextLifetime::None;
    bool retainsHistory = false;
    UpscalerHistoryReset resetHistory = nullptr;
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
    UpscalerImageFormat inputFormat = UpscalerImageFormat::Rgba8Unorm;
    UpscalerImageFormat outputFormat = UpscalerImageFormat::Rgba8Unorm;
    std::span<const UpscalerFrameInput> availableFrameInputs{};
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

    return std::all_of(contract.requiredFrameInputs.begin(),
                       contract.requiredFrameInputs.end(),
                       [&frame](UpscalerFrameInput required) {
                           return std::find(frame.availableFrameInputs.begin(),
                                            frame.availableFrameInputs.end(), required) !=
                                  frame.availableFrameInputs.end();
                       });
}

inline void ResetUpscalerHistory(const UpscalerContract& contract,
                                 void* context,
                                 UpscalerHistoryResetCause cause) noexcept {
    if (contract.resetHistory) {
        contract.resetHistory(context, cause);
    }
}

enum class UpscalerSelectionFailureReason : uint8_t {
    None,
    NotCompiled,
    NotPermittedByPass,
    RuntimeUnsupported,
    InputIncompatible,
    NotInitialized,
    Unqualified,
    OverBudget
};

struct UpscalerEligibility {
    bool compiled = false;
    bool permittedByPass = false;
    bool runtimeSupported = false;
    bool inputCompatible = false;
    bool initialized = false;
    bool qualified = false;
    bool admittedByBudget = false;
};

constexpr UpscalerSelectionFailureReason EvaluateUpscalerEligibility(
    const UpscalerEligibility& eligibility) noexcept {
    if (!eligibility.compiled) return UpscalerSelectionFailureReason::NotCompiled;
    if (!eligibility.permittedByPass) return UpscalerSelectionFailureReason::NotPermittedByPass;
    if (!eligibility.runtimeSupported) return UpscalerSelectionFailureReason::RuntimeUnsupported;
    if (!eligibility.inputCompatible) return UpscalerSelectionFailureReason::InputIncompatible;
    if (!eligibility.initialized) return UpscalerSelectionFailureReason::NotInitialized;
    if (!eligibility.qualified) return UpscalerSelectionFailureReason::Unqualified;
    if (!eligibility.admittedByBudget) return UpscalerSelectionFailureReason::OverBudget;
    return UpscalerSelectionFailureReason::None;
}

constexpr std::string_view UpscalerSelectionFailureText(
    UpscalerSelectionFailureReason reason) noexcept {
    switch (reason) {
        case UpscalerSelectionFailureReason::None: return "eligible";
        case UpscalerSelectionFailureReason::NotCompiled: return "not compiled";
        case UpscalerSelectionFailureReason::NotPermittedByPass: return "not permitted by pass";
        case UpscalerSelectionFailureReason::RuntimeUnsupported: return "runtime unsupported";
        case UpscalerSelectionFailureReason::InputIncompatible: return "input incompatible";
        case UpscalerSelectionFailureReason::NotInitialized: return "not initialized";
        case UpscalerSelectionFailureReason::Unqualified: return "not qualified";
        case UpscalerSelectionFailureReason::OverBudget: return "not admitted by budget";
    }
    return "unknown";
}

struct UpscalerProviderCandidate {
    std::string_view id;
    UpscalerEligibility eligibility{};
    bool nativeFallback = false;
    // False before an optional module has been compiled; true when a prior graph compile
    // attempt proved that its provider context could not be initialized.
    bool initializationFailed = false;
};

struct UpscalerRejectedProvider {
    std::string_view id;
    UpscalerSelectionFailureReason reason = UpscalerSelectionFailureReason::None;
};

struct UpscalerSelectionResult {
    const UpscalerProviderCandidate* selected = nullptr;
    std::vector<UpscalerRejectedProvider> rejected;
};

inline UpscalerSelectionResult SelectUpscalerProvider(
    std::span<const UpscalerProviderCandidate> candidates) {
    UpscalerSelectionResult result;
    for (const auto& candidate : candidates) {
        const auto reason = EvaluateUpscalerEligibility(candidate.eligibility);
        if (reason == UpscalerSelectionFailureReason::None) {
            result.selected = &candidate;
            return result;
        }
        result.rejected.push_back({candidate.id, reason});
    }
    return result;
}

// Graph construction precedes provider context creation. This provisional selector applies
// every static gate and chooses the next module to compile while leaving eligibility.initialized
// false until that provider has created its real context. A failed compile attempt is retried with
// initializationFailed set on that provider; native remains the terminal candidate.
constexpr UpscalerSelectionFailureReason EvaluateUpscalerEligibilityBeforeInitialization(
    const UpscalerEligibility& eligibility) noexcept {
    if (!eligibility.compiled) return UpscalerSelectionFailureReason::NotCompiled;
    if (!eligibility.permittedByPass) return UpscalerSelectionFailureReason::NotPermittedByPass;
    if (!eligibility.runtimeSupported) return UpscalerSelectionFailureReason::RuntimeUnsupported;
    if (!eligibility.inputCompatible) return UpscalerSelectionFailureReason::InputIncompatible;
    if (!eligibility.qualified) return UpscalerSelectionFailureReason::Unqualified;
    if (!eligibility.admittedByBudget) return UpscalerSelectionFailureReason::OverBudget;
    return UpscalerSelectionFailureReason::None;
}

inline UpscalerSelectionResult SelectUpscalerProviderForInitialization(
    std::span<const UpscalerProviderCandidate> candidates) {
    UpscalerSelectionResult result;
    for (const auto& candidate : candidates) {
        const auto reason = candidate.initializationFailed
            ? UpscalerSelectionFailureReason::NotInitialized
            : EvaluateUpscalerEligibilityBeforeInitialization(candidate.eligibility);
        if (reason == UpscalerSelectionFailureReason::None) {
            result.selected = &candidate;
            return result;
        }
        result.rejected.push_back({candidate.id, reason});
    }
    return result;
}

class UpscalerProviderInitializationError final : public std::runtime_error {
public:
    UpscalerProviderInitializationError(std::string providerId, std::string message)
        : std::runtime_error(std::move(message)), providerId_(std::move(providerId)) {}

    [[nodiscard]] const std::string& ProviderId() const noexcept { return providerId_; }

private:
    std::string providerId_;
};

} // namespace Vixen::RenderGraph
