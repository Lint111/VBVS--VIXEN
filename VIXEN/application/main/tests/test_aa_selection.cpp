#include "graph/AntiAliasingModule.h"

#include <gtest/gtest.h>
#include <string_view>

namespace Vixen::RenderGraph {
namespace {

constexpr RenderModuleFrameInput kTemporalFrame[] = {
    RenderModuleFrameInput::Color, RenderModuleFrameInput::Depth,
    RenderModuleFrameInput::MotionVectors, RenderModuleFrameInput::Jitter,
    RenderModuleFrameInput::Exposure};

AntiAliasingFrameDescription PresentationFrame() {
    return {.presentationSampling = true,
            .temporalUpscalerSelected = false,
            .colorSpace = AntiAliasingColorSpace::LinearHdr,
            .colorFormat = RenderModuleImageFormat::Rgba16Float,
            .availableFrameInputs = kTemporalFrame};
}

const RenderModuleProviderCandidate* Find(const std::vector<RenderModuleProviderCandidate>& candidates,
                                          std::string_view id) {
    for (const auto& candidate : candidates)
        if (candidate.id == id) return &candidate;
    return nullptr;
}

TEST(AntiAliasingSelectionTest, BackendTableNamesRulingProvidersWithPassThroughLast) {
    const auto backends = GetAntiAliasingBackendDescriptors();
    ASSERT_FALSE(backends.empty());
    EXPECT_EQ(backends.back().id, "none");
    EXPECT_TRUE(backends.back().passThrough);
    EXPECT_TRUE(backends.back().compiled);
    const auto* taa = FindAntiAliasingBackend("taa");
    ASSERT_NE(taa, nullptr);
    ASSERT_NE(taa->contract, nullptr);
    EXPECT_EQ(taa->contract->colorSpace, AntiAliasingColorSpace::LinearHdr);
    EXPECT_TRUE(taa->contract->retainsHistory);
    EXPECT_EQ(FindAntiAliasingBackend("unknown"), nullptr);
}

TEST(AntiAliasingSelectionTest, RequestedProviderPrecedesTerminalPassThrough) {
    const auto candidates = BuildAntiAliasingCandidates("taa", PresentationFrame());
    ASSERT_EQ(candidates.size(), 2u);
    EXPECT_EQ(candidates[0].id, "taa");
    EXPECT_FALSE(candidates[0].nativeFallback);
    EXPECT_EQ(candidates[1].id, "none");
    EXPECT_TRUE(candidates[1].nativeFallback);
    EXPECT_EQ(EvaluateRenderModuleEligibility(candidates[1].eligibility),
              RenderModuleSelectionFailureReason::None);
}

TEST(AntiAliasingSelectionTest, PassThroughOrUnknownRequestYieldsOnlyPassThrough) {
    for (std::string_view request : {"none", "", "smaa9x"}) {
        const auto candidates = BuildAntiAliasingCandidates(request, PresentationFrame());
        ASSERT_EQ(candidates.size(), 1u) << request;
        EXPECT_EQ(candidates[0].id, "none");
        const auto result = SelectRenderModuleProvider(candidates);
        ASSERT_NE(result.selected, nullptr);
        EXPECT_EQ(result.selected->id, "none");
        EXPECT_TRUE(result.rejected.empty());
    }
}

TEST(AntiAliasingSelectionTest, CanonicalSamplingRefusesEveryProviderButPassThrough) {
    auto frame = PresentationFrame();
    frame.presentationSampling = false;
    const auto candidates = BuildAntiAliasingCandidates("taa", frame);
    const auto* taa = Find(candidates, "taa");
    ASSERT_NE(taa, nullptr);
    EXPECT_FALSE(taa->eligibility.permittedByPass);
    EXPECT_EQ(AntiAliasingPassRefusal(*FindAntiAliasingBackend("taa"), frame),
              "canonical sampling stays one raw sample");
    const auto result = SelectRenderModuleProviderForInitialization(candidates);
    ASSERT_NE(result.selected, nullptr);
    EXPECT_EQ(result.selected->id, "none");
    ASSERT_EQ(result.rejected.size(), 1u);
    EXPECT_EQ(result.rejected[0].id, "taa");
    // The shared predicate reports the first failing gate in its fixed order.
    EXPECT_EQ(result.rejected[0].reason, FindAntiAliasingBackend("taa")->compiled
                                             ? RenderModuleSelectionFailureReason::NotPermittedByPass
                                             : RenderModuleSelectionFailureReason::NotCompiled);
}

TEST(AntiAliasingSelectionTest, TemporalUpscalerOwnsReconstructionHistory) {
    auto frame = PresentationFrame();
    frame.temporalUpscalerSelected = true;
    const auto* taa = FindAntiAliasingBackend("taa");
    ASSERT_NE(taa, nullptr);
    EXPECT_EQ(AntiAliasingPassRefusal(*taa, frame), "temporal upscaler owns reconstruction");
    const auto candidates = BuildAntiAliasingCandidates("taa", frame);
    EXPECT_FALSE(Find(candidates, "taa")->eligibility.permittedByPass);
    EXPECT_TRUE(Find(candidates, "none")->eligibility.permittedByPass);

    // A history-free provider is not refused by M3.
    constexpr AntiAliasingContract postContract{
        .colorSpace = AntiAliasingColorSpace::DisplayReferred,
        .colorFormats = AntiAliasingDetail::kAnyColor,
        .requiredFrameInputs = AntiAliasingDetail::kColorOnly,
        .retainsHistory = false};
    const AntiAliasingBackendDescriptor post{"post", &postContract, true, false};
    EXPECT_TRUE(AntiAliasingPassRefusal(post, frame).empty());
}

TEST(AntiAliasingSelectionTest, PresentationFramePermitsTaaAndChecksItsInputs) {
    const auto candidates = BuildAntiAliasingCandidates("taa", PresentationFrame());
    const auto& eligibility = Find(candidates, "taa")->eligibility;
    EXPECT_TRUE(eligibility.permittedByPass);
    EXPECT_TRUE(eligibility.inputCompatible);
    EXPECT_EQ(eligibility.compiled, FindAntiAliasingBackend("taa")->compiled);

    auto display = PresentationFrame();
    display.colorSpace = AntiAliasingColorSpace::DisplayReferred;
    display.colorFormat = RenderModuleImageFormat::Rgba8Unorm;
    EXPECT_FALSE(Find(BuildAntiAliasingCandidates("taa", display), "taa")->eligibility.inputCompatible);

    constexpr RenderModuleFrameInput colorOnly[] = {RenderModuleFrameInput::Color};
    auto unjittered = PresentationFrame();
    unjittered.availableFrameInputs = colorOnly;
    EXPECT_FALSE(Find(BuildAntiAliasingCandidates("taa", unjittered), "taa")->eligibility.inputCompatible);
    // The pass-through accepts any frame.
    EXPECT_TRUE(Find(BuildAntiAliasingCandidates("taa", unjittered), "none")->eligibility.inputCompatible);
}

TEST(AntiAliasingSelectionTest, EligibleTaaIsSelectedThroughTheSharedPredicate) {
    // Drive the predicate with the candidate the host builds once taa is compiled.
    auto candidates = BuildAntiAliasingCandidates("taa", PresentationFrame());
    candidates[0].eligibility.compiled = true;
    const auto result = SelectRenderModuleProvider(candidates);
    ASSERT_NE(result.selected, nullptr);
    EXPECT_EQ(result.selected->id, "taa");
    EXPECT_TRUE(result.rejected.empty());

    // A provider whose context failed falls back to the pass-through, as an upscaler does.
    candidates[0].initializationFailed = true;
    const auto fallback = SelectRenderModuleProviderForInitialization(candidates);
    ASSERT_NE(fallback.selected, nullptr);
    EXPECT_EQ(fallback.selected->id, "none");
    ASSERT_EQ(fallback.rejected.size(), 1u);
    EXPECT_EQ(fallback.rejected[0].reason, RenderModuleSelectionFailureReason::NotInitialized);
}

TEST(AntiAliasingSelectionTest, ProviderSystemsRunOnlyWhenTheirProviderIsSelected) {
    const auto* taa = FindAntiAliasingBackend("taa");
    ASSERT_NE(taa, nullptr);
    ASSERT_FALSE(taa->generatedSystem.empty());
    EXPECT_TRUE(AntiAliasingSystemEnabled(taa->generatedSystem, "taa"));
    EXPECT_FALSE(AntiAliasingSystemEnabled(taa->generatedSystem, "none"));
    // A system no AA provider claims (the tone map) is never gated by the module.
    EXPECT_TRUE(AntiAliasingSystemEnabled("ExposureTonemapSystem", "none"));
    EXPECT_TRUE(FindAntiAliasingBackend("none")->generatedSystem.empty());
}

} // namespace
} // namespace Vixen::RenderGraph
