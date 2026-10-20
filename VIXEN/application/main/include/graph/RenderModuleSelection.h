#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Vixen::RenderGraph {

// One provider-selection vocabulary for every renderer module host (upscaler, anti-aliasing;
// R676.5): the frame-input and format names a provider contract declares, the ordered
// eligibility gates, and the fallback walk. A module host builds candidates from its own backend
// table; this predicate never names a module or a provider.

// Formats and frame semantics are engine vocabulary. Provider adapters translate them to the
// format and input names used by their own APIs.
enum class RenderModuleImageFormat : uint8_t {
    Rgba8Unorm,
    Rgba16Float,
    Rg16Float,
    R32Float
};

enum class RenderModuleFrameInput : uint8_t {
    Color,
    Depth,
    MotionVectors,
    Jitter,
    Exposure,
    ReactiveMask,
    TransparencyMask
};

enum class RenderModuleHistoryResetCause : uint8_t {
    InputExtentChanged,
    OutputExtentChanged,
    CameraCut,
    DeviceChanged,
    Explicit
};

using RenderModuleHistoryReset = void (*)(void* context,
                                          RenderModuleHistoryResetCause cause) noexcept;

inline bool ProvidesRenderModuleFrameInputs(std::span<const RenderModuleFrameInput> required,
                                            std::span<const RenderModuleFrameInput> available) noexcept {
    return std::all_of(required.begin(), required.end(), [available](RenderModuleFrameInput input) {
        return std::find(available.begin(), available.end(), input) != available.end();
    });
}

enum class RenderModuleSelectionFailureReason : uint8_t {
    None,
    NotCompiled,
    NotInRuntimeProfile,
    NotPermittedByPass,
    RuntimeUnsupported,
    InputIncompatible,
    NotInitialized,
    Unqualified,
    OverBudget
};

struct RenderModuleEligibility {
    bool compiled = false;
    bool permittedByRuntimeProfile = true;
    bool permittedByPass = false;
    bool runtimeSupported = false;
    bool inputCompatible = false;
    bool initialized = false;
    bool qualified = false;
    bool admittedByBudget = false;
};

constexpr RenderModuleSelectionFailureReason EvaluateRenderModuleEligibility(
    const RenderModuleEligibility& eligibility) noexcept {
    if (!eligibility.compiled) return RenderModuleSelectionFailureReason::NotCompiled;
    if (!eligibility.permittedByRuntimeProfile) return RenderModuleSelectionFailureReason::NotInRuntimeProfile;
    if (!eligibility.permittedByPass) return RenderModuleSelectionFailureReason::NotPermittedByPass;
    if (!eligibility.runtimeSupported) return RenderModuleSelectionFailureReason::RuntimeUnsupported;
    if (!eligibility.inputCompatible) return RenderModuleSelectionFailureReason::InputIncompatible;
    if (!eligibility.initialized) return RenderModuleSelectionFailureReason::NotInitialized;
    if (!eligibility.qualified) return RenderModuleSelectionFailureReason::Unqualified;
    if (!eligibility.admittedByBudget) return RenderModuleSelectionFailureReason::OverBudget;
    return RenderModuleSelectionFailureReason::None;
}

constexpr std::string_view RenderModuleSelectionFailureText(
    RenderModuleSelectionFailureReason reason) noexcept {
    switch (reason) {
        case RenderModuleSelectionFailureReason::None: return "eligible";
        case RenderModuleSelectionFailureReason::NotCompiled: return "not compiled";
        case RenderModuleSelectionFailureReason::NotInRuntimeProfile: return "not in runtime machine profile";
        case RenderModuleSelectionFailureReason::NotPermittedByPass: return "not permitted by pass";
        case RenderModuleSelectionFailureReason::RuntimeUnsupported: return "runtime unsupported";
        case RenderModuleSelectionFailureReason::InputIncompatible: return "input incompatible";
        case RenderModuleSelectionFailureReason::NotInitialized: return "not initialized";
        case RenderModuleSelectionFailureReason::Unqualified: return "not qualified";
        case RenderModuleSelectionFailureReason::OverBudget: return "not admitted by budget";
    }
    return "unknown";
}

struct RenderModuleProviderCandidate {
    std::string_view id;
    RenderModuleEligibility eligibility{};
    bool nativeFallback = false;
    // False before an optional module has been compiled; true when a prior graph compile
    // attempt proved that its provider context could not be initialized.
    bool initializationFailed = false;
};

struct RenderModuleRejectedProvider {
    std::string_view id;
    RenderModuleSelectionFailureReason reason = RenderModuleSelectionFailureReason::None;
};

struct RenderModuleSelectionResult {
    const RenderModuleProviderCandidate* selected = nullptr;
    std::vector<RenderModuleRejectedProvider> rejected;
};

inline RenderModuleSelectionResult SelectRenderModuleProvider(
    std::span<const RenderModuleProviderCandidate> candidates) {
    RenderModuleSelectionResult result;
    for (const auto& candidate : candidates) {
        const auto reason = EvaluateRenderModuleEligibility(candidate.eligibility);
        if (reason == RenderModuleSelectionFailureReason::None) {
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
// initializationFailed set on that provider; the native fallback remains the terminal candidate.
constexpr RenderModuleSelectionFailureReason EvaluateRenderModuleEligibilityBeforeInitialization(
    const RenderModuleEligibility& eligibility) noexcept {
    if (!eligibility.compiled) return RenderModuleSelectionFailureReason::NotCompiled;
    if (!eligibility.permittedByRuntimeProfile) return RenderModuleSelectionFailureReason::NotInRuntimeProfile;
    if (!eligibility.permittedByPass) return RenderModuleSelectionFailureReason::NotPermittedByPass;
    if (!eligibility.runtimeSupported) return RenderModuleSelectionFailureReason::RuntimeUnsupported;
    if (!eligibility.inputCompatible) return RenderModuleSelectionFailureReason::InputIncompatible;
    if (!eligibility.qualified) return RenderModuleSelectionFailureReason::Unqualified;
    if (!eligibility.admittedByBudget) return RenderModuleSelectionFailureReason::OverBudget;
    return RenderModuleSelectionFailureReason::None;
}

inline RenderModuleSelectionResult SelectRenderModuleProviderForInitialization(
    std::span<const RenderModuleProviderCandidate> candidates) {
    RenderModuleSelectionResult result;
    for (const auto& candidate : candidates) {
        const auto reason = candidate.initializationFailed
            ? RenderModuleSelectionFailureReason::NotInitialized
            : EvaluateRenderModuleEligibilityBeforeInitialization(candidate.eligibility);
        if (reason == RenderModuleSelectionFailureReason::None) {
            result.selected = &candidate;
            return result;
        }
        result.rejected.push_back({candidate.id, reason});
    }
    return result;
}

// A provider reports whether its real context was created. A failure is data for the host's
// fallback loop, not an exception that unwinds through graph compilation.
struct RenderModuleProviderInitializationResult {
    std::string providerId;
    bool initialized = false;
    std::string message;
};

inline bool RecordRenderModuleProviderInitializationFailure(
    std::vector<std::string>& failedProviderIds, std::string_view providerId) {
    if (providerId.empty() ||
        std::find(failedProviderIds.begin(), failedProviderIds.end(), providerId) !=
            failedProviderIds.end()) {
        return false;
    }
    failedProviderIds.emplace_back(providerId);
    return true;
}

} // namespace Vixen::RenderGraph
