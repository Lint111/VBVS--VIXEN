#include "graph/UpscalerContract.h"

#include <array>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace Vixen::RenderGraph {
namespace {

RenderModuleEligibility Eligible() {
    return {
        .compiled = true,
        .permittedByPass = true,
        .runtimeSupported = true,
        .inputCompatible = true,
        .initialized = true,
        .qualified = true,
        .admittedByBudget = true
    };
}

TEST(UpscalerSelectionTest, ReportsEveryEligibilityFailureSeparately) {
    std::array<RenderModuleEligibility, 8> failures{};
    failures.fill(Eligible());
    failures[0].compiled = false;
    failures[1].permittedByRuntimeProfile = false;
    failures[2].permittedByPass = false;
    failures[3].runtimeSupported = false;
    failures[4].inputCompatible = false;
    failures[5].initialized = false;
    failures[6].qualified = false;
    failures[7].admittedByBudget = false;

    constexpr std::array expected{
        RenderModuleSelectionFailureReason::NotCompiled,
        RenderModuleSelectionFailureReason::NotInRuntimeProfile,
        RenderModuleSelectionFailureReason::NotPermittedByPass,
        RenderModuleSelectionFailureReason::RuntimeUnsupported,
        RenderModuleSelectionFailureReason::InputIncompatible,
        RenderModuleSelectionFailureReason::NotInitialized,
        RenderModuleSelectionFailureReason::Unqualified,
        RenderModuleSelectionFailureReason::OverBudget
    };

    for (size_t index = 0; index < expected.size(); ++index) {
        EXPECT_EQ(EvaluateRenderModuleEligibility(failures[index]), expected[index])
            << "failure index " << index;
    }
    EXPECT_NE(RenderModuleSelectionFailureText(expected[0]),
              RenderModuleSelectionFailureText(expected[1]));
    EXPECT_NE(RenderModuleSelectionFailureText(expected[1]),
              RenderModuleSelectionFailureText(expected[2]));
    EXPECT_NE(RenderModuleSelectionFailureText(expected[2]),
              RenderModuleSelectionFailureText(expected[3]));
    EXPECT_NE(RenderModuleSelectionFailureText(expected[3]),
              RenderModuleSelectionFailureText(expected[4]));
    EXPECT_NE(RenderModuleSelectionFailureText(expected[4]),
              RenderModuleSelectionFailureText(expected[5]));
    EXPECT_NE(RenderModuleSelectionFailureText(expected[5]),
              RenderModuleSelectionFailureText(expected[6]));
    EXPECT_NE(RenderModuleSelectionFailureText(expected[6]),
              RenderModuleSelectionFailureText(expected[7]));
}

TEST(UpscalerSelectionTest, SkipsRejectedBackendAndSelectsNextPermittedBackend) {
    auto fsr1 = Eligible();
    fsr1.runtimeSupported = false;
    const std::array candidates{
        RenderModuleProviderCandidate{"fsr1", fsr1, false},
        RenderModuleProviderCandidate{"linear", Eligible(), false},
        RenderModuleProviderCandidate{"native", Eligible(), true}
    };

    const auto result = SelectRenderModuleProvider(candidates);
    ASSERT_NE(result.selected, nullptr);
    EXPECT_EQ(result.selected->id, "linear");
    ASSERT_EQ(result.rejected.size(), 1u);
    EXPECT_EQ(result.rejected[0].id, "fsr1");
    EXPECT_EQ(result.rejected[0].reason,
              RenderModuleSelectionFailureReason::RuntimeUnsupported);
}

TEST(UpscalerSelectionTest, EndsAtNativeWhenAllRequestedBackendsFail) {
    auto fsr1 = Eligible();
    fsr1.runtimeSupported = false;
    auto linear = Eligible();
    linear.admittedByBudget = false;
    const std::array candidates{
        RenderModuleProviderCandidate{"fsr1", fsr1, false},
        RenderModuleProviderCandidate{"linear", linear, false},
        RenderModuleProviderCandidate{"native", Eligible(), true}
    };

    const auto result = SelectRenderModuleProvider(candidates);
    ASSERT_NE(result.selected, nullptr);
    EXPECT_EQ(result.selected->id, "native");
    EXPECT_TRUE(result.selected->nativeFallback);
    ASSERT_EQ(result.rejected.size(), 2u);
    EXPECT_EQ(result.rejected[0].reason,
              RenderModuleSelectionFailureReason::RuntimeUnsupported);
    EXPECT_EQ(result.rejected[1].reason,
              RenderModuleSelectionFailureReason::OverBudget);
}

TEST(UpscalerSelectionTest, OptionalProviderFallsBackToFsr1ThenNativeWhenUnqualifiedOrUnsupported) {
    auto fsr3 = Eligible();
    fsr3.qualified = false;
    const std::array withFsr1{
        RenderModuleProviderCandidate{"fsr3", fsr3, false},
        RenderModuleProviderCandidate{"fsr1", Eligible(), false},
        RenderModuleProviderCandidate{"native", Eligible(), true}
    };
    const auto fsr1Result = SelectRenderModuleProvider(withFsr1);
    ASSERT_NE(fsr1Result.selected, nullptr);
    EXPECT_EQ(fsr1Result.selected->id, "fsr1");
    ASSERT_EQ(fsr1Result.rejected.size(), 1u);
    EXPECT_EQ(fsr1Result.rejected.front().reason,
              RenderModuleSelectionFailureReason::Unqualified);

    auto fsr1 = Eligible();
    fsr1.runtimeSupported = false;
    const std::array withNative{
        RenderModuleProviderCandidate{"fsr3", fsr3, false},
        RenderModuleProviderCandidate{"fsr1", fsr1, false},
        RenderModuleProviderCandidate{"native", Eligible(), true}
    };
    const auto nativeResult = SelectRenderModuleProvider(withNative);
    ASSERT_NE(nativeResult.selected, nullptr);
    EXPECT_EQ(nativeResult.selected->id, "native");
    ASSERT_EQ(nativeResult.rejected.size(), 2u);
}

TEST(UpscalerSelectionTest, OptionalProviderInitializationFailureAdvancesThroughFsr1ToNative) {
    auto fsr3 = Eligible();
    fsr3.initialized = false;
    auto fsr1 = Eligible();
    fsr1.initialized = false;
    std::array candidates{
        RenderModuleProviderCandidate{"fsr3", fsr3, false},
        RenderModuleProviderCandidate{"fsr1", fsr1, false},
        RenderModuleProviderCandidate{"native", Eligible(), true}
    };
    std::vector<std::string> failedProviders;

    // First graph compile attempt selects FSR3 provisionally. Inject the same failure result a
    // provider reports when FFX context creation fails, and record it through Prepare's helper.
    auto result = SelectRenderModuleProviderForInitialization(candidates);
    ASSERT_NE(result.selected, nullptr);
    EXPECT_EQ(result.selected->id, "fsr3");
    EXPECT_FALSE(result.selected->eligibility.initialized);
    const RenderModuleProviderInitializationResult initialization{
        "fsr3", false, "forced FFX context creation failure"};
    EXPECT_FALSE(initialization.initialized);
    EXPECT_EQ(initialization.providerId, "fsr3");
    ASSERT_TRUE(RecordRenderModuleProviderInitializationFailure(
        failedProviders, initialization.providerId));
    candidates[0].initializationFailed =
        std::find(failedProviders.begin(), failedProviders.end(), candidates[0].id) !=
        failedProviders.end();

    result = SelectRenderModuleProviderForInitialization(candidates);
    ASSERT_NE(result.selected, nullptr);
    EXPECT_EQ(result.selected->id, "fsr1");
    ASSERT_EQ(result.rejected.size(), 1u);
    EXPECT_EQ(result.rejected.front().reason,
              RenderModuleSelectionFailureReason::NotInitialized);

    // If FSR1 is unavailable on this device, the required native candidate is next.
    candidates[1].eligibility.runtimeSupported = false;
    result = SelectRenderModuleProviderForInitialization(candidates);
    ASSERT_NE(result.selected, nullptr);
    EXPECT_EQ(result.selected->id, "native");
    EXPECT_TRUE(result.selected->nativeFallback);
    ASSERT_EQ(result.rejected.size(), 2u);
    EXPECT_EQ(result.rejected[0].reason,
              RenderModuleSelectionFailureReason::NotInitialized);
    EXPECT_EQ(result.rejected[1].reason,
              RenderModuleSelectionFailureReason::RuntimeUnsupported);
}

TEST(UpscalerSelectionTest, RuntimeProfileCompileSetGatesOptionalProviders) {
    auto fsr3 = Eligible();
    fsr3.permittedByRuntimeProfile = false;
    const std::array candidates{
        RenderModuleProviderCandidate{"fsr3", fsr3, false},
        RenderModuleProviderCandidate{"fsr1", Eligible(), false},
        RenderModuleProviderCandidate{"native", Eligible(), true}
    };

    const auto result = SelectRenderModuleProvider(candidates);
    ASSERT_NE(result.selected, nullptr);
    EXPECT_EQ(result.selected->id, "fsr1");
    ASSERT_EQ(result.rejected.size(), 1u);
    EXPECT_EQ(result.rejected.front().reason,
              RenderModuleSelectionFailureReason::NotInRuntimeProfile);
}

TEST(UpscalerContractTest, RequiresDeclaredInputsAndSupportedExtentsAndFormats) {
    constexpr std::array formats{RenderModuleImageFormat::Rgba8Unorm};
    constexpr std::array required{RenderModuleFrameInput::Color,
                                  RenderModuleFrameInput::MotionVectors};
    const UpscalerContract contract{
        .inputExtents = {{1, 1}, {1920, 1080}, 1, 1},
        .outputExtents = {{1, 1}, {3840, 2160}, 1, 1},
        .inputFormats = formats,
        .outputFormats = formats,
        .requiredFrameInputs = required
    };
    constexpr std::array colorOnly{RenderModuleFrameInput::Color};
    constexpr std::array completeInputs{RenderModuleFrameInput::Color,
                                        RenderModuleFrameInput::MotionVectors};

    EXPECT_FALSE(SupportsUpscalerFrame(
        contract, {{1280, 720}, {1920, 1080}, RenderModuleImageFormat::Rgba8Unorm,
                   RenderModuleImageFormat::Rgba8Unorm, colorOnly}));
    EXPECT_TRUE(SupportsUpscalerFrame(
        contract, {{1280, 720}, {1920, 1080}, RenderModuleImageFormat::Rgba8Unorm,
                   RenderModuleImageFormat::Rgba8Unorm, completeInputs}));
    EXPECT_FALSE(SupportsUpscalerFrame(
        contract, {{2560, 1440}, {1920, 1080}, RenderModuleImageFormat::Rgba8Unorm,
                   RenderModuleImageFormat::Rgba8Unorm, completeInputs}));
}

} // namespace
} // namespace Vixen::RenderGraph
