#pragma once

#include "graph/RenderModuleSelection.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace Vixen::RenderGraph {

// The anti-aliasing module host (R676). It follows the upscaler module's contract shape and
// selects through the same RenderModuleSelection predicate (R676.5): a backend table, a
// provider-neutral contract, ordered gates with one reason each, and a terminal pass-through.

// Where a provider resolves: linear HDR before the exposure meter (TAA, R676.1) or
// display-referred after the tone map (post providers, R676.4).
enum class AntiAliasingColorSpace : uint8_t {
    LinearHdr,
    DisplayReferred
};

struct AntiAliasingContract {
    AntiAliasingColorSpace colorSpace = AntiAliasingColorSpace::LinearHdr;
    std::span<const RenderModuleImageFormat> colorFormats{};
    std::span<const RenderModuleFrameInput> requiredFrameInputs{};
    // A history-holding provider never runs beside a temporal upscaler (design M3).
    bool retainsHistory = false;
};

struct AntiAliasingBackendDescriptor {
    std::string_view id;
    const AntiAliasingContract* contract = nullptr;
    bool compiled = false;
    // The terminal fallback: adds no work and leaves the frame's colour untouched.
    bool passThrough = false;
    // The generated GPU system (manifest class name) that is this provider's work; empty for the
    // pass-through. The host instantiates it only when the provider is selected (R677).
    std::string_view generatedSystem{};
};

namespace AntiAliasingDetail {
inline constexpr RenderModuleImageFormat kHdrColor[] = {RenderModuleImageFormat::Rgba16Float};
inline constexpr RenderModuleImageFormat kAnyColor[] = {RenderModuleImageFormat::Rgba16Float,
                                                        RenderModuleImageFormat::Rgba8Unorm};
inline constexpr RenderModuleFrameInput kColorOnly[] = {RenderModuleFrameInput::Color};
inline constexpr RenderModuleFrameInput kTemporalInputs[] = {RenderModuleFrameInput::Color,
                                                             RenderModuleFrameInput::Jitter};
} // namespace AntiAliasingDetail

inline constexpr AntiAliasingContract kNoneAntiAliasingContract{
    .colorSpace = AntiAliasingColorSpace::LinearHdr,
    .colorFormats = AntiAliasingDetail::kAnyColor,
    .requiredFrameInputs = AntiAliasingDetail::kColorOnly,
    .retainsHistory = false,
};
inline constexpr AntiAliasingContract kTaaAntiAliasingContract{
    .colorSpace = AntiAliasingColorSpace::LinearHdr,
    .colorFormats = AntiAliasingDetail::kHdrColor,
    .requiredFrameInputs = AntiAliasingDetail::kTemporalInputs,
    .retainsHistory = true,
};

// Provider ids are R676.5's. Post providers (cmaa2, smaa1x, fxaa-lite) and supersample join this
// table with their slices; `none` stays last as the terminal fallback. `taa` is the composition's
// TemporalAntiAliasingSystem, run by the generated-system runner (history pair, sampled view).
inline constexpr std::array<AntiAliasingBackendDescriptor, 2> kAntiAliasingBackends{{
    {"taa", &kTaaAntiAliasingContract, true, false, "TemporalAntiAliasingSystem"},
    {"none", &kNoneAntiAliasingContract, true, true, {}},
}};

inline std::span<const AntiAliasingBackendDescriptor> GetAntiAliasingBackendDescriptors() noexcept {
    return kAntiAliasingBackends;
}

inline const AntiAliasingBackendDescriptor* FindAntiAliasingBackend(std::string_view id) noexcept {
    const auto found = std::find_if(kAntiAliasingBackends.begin(), kAntiAliasingBackends.end(),
                                    [id](const auto& backend) { return backend.id == id; });
    return found == kAntiAliasingBackends.end() ? nullptr : &*found;
}

// False for a generated system that belongs to an AA provider other than the selected one.
inline bool AntiAliasingSystemEnabled(std::string_view generatedSystem,
                                      std::string_view selectedProvider) noexcept {
    for (const auto& backend : kAntiAliasingBackends)
        if (!backend.generatedSystem.empty() && backend.generatedSystem == generatedSystem)
            return backend.id == selectedProvider;
    return true;
}

// What the frame offers the module.
struct AntiAliasingFrameDescription {
    // Canonical capture, parity and picking stay one raw unjittered sample (R676.2, design M1);
    // only a presentation frame on the interactive sampling path admits a provider.
    bool presentationSampling = false;
    // A temporal upscaler already owns reconstruction history (design M3).
    bool temporalUpscalerSelected = false;
    AntiAliasingColorSpace colorSpace = AntiAliasingColorSpace::LinearHdr;
    RenderModuleImageFormat colorFormat = RenderModuleImageFormat::Rgba16Float;
    std::span<const RenderModuleFrameInput> availableFrameInputs{};
};

// The pass gate's reason in words; empty when the pass permits the provider.
inline std::string_view AntiAliasingPassRefusal(const AntiAliasingBackendDescriptor& backend,
                                                const AntiAliasingFrameDescription& frame) noexcept {
    if (backend.passThrough) return {};
    if (!frame.presentationSampling) return "canonical sampling stays one raw sample";
    if (backend.contract && backend.contract->retainsHistory && frame.temporalUpscalerSelected)
        return "temporal upscaler owns reconstruction";
    return {};
}

inline bool SupportsAntiAliasingFrame(const AntiAliasingContract& contract,
                                      const AntiAliasingFrameDescription& frame) noexcept {
    return contract.colorSpace == frame.colorSpace &&
           std::find(contract.colorFormats.begin(), contract.colorFormats.end(), frame.colorFormat) !=
               contract.colorFormats.end() &&
           ProvidesRenderModuleFrameInputs(contract.requiredFrameInputs, frame.availableFrameInputs);
}

// The candidates in selection order: the requested provider, then the pass-through. An unknown
// or pass-through request yields the pass-through alone.
inline std::vector<RenderModuleProviderCandidate> BuildAntiAliasingCandidates(
    std::string_view requested, const AntiAliasingFrameDescription& frame) {
    std::vector<const AntiAliasingBackendDescriptor*> order;
    if (const auto* backend = FindAntiAliasingBackend(requested); backend && !backend->passThrough)
        order.push_back(backend);
    for (const auto& backend : kAntiAliasingBackends)
        if (backend.passThrough) order.push_back(&backend);

    std::vector<RenderModuleProviderCandidate> candidates;
    candidates.reserve(order.size());
    for (const auto* backend : order) {
        RenderModuleEligibility eligibility;
        eligibility.compiled = backend->compiled;
        eligibility.permittedByPass = AntiAliasingPassRefusal(*backend, frame).empty();
        // Every AA provider is a kernel system or no work: no runtime capability, external
        // context, qualification run or separate allocation budget gates it yet.
        eligibility.runtimeSupported = true;
        eligibility.inputCompatible = backend->passThrough ||
            (backend->contract && SupportsAntiAliasingFrame(*backend->contract, frame));
        eligibility.initialized = true;
        eligibility.qualified = true;
        eligibility.admittedByBudget = true;
        candidates.push_back({backend->id, eligibility, backend->passThrough, false});
    }
    return candidates;
}

} // namespace Vixen::RenderGraph
