#include "graph/UpscalerContract.h"

#include <array>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace Vixen::RenderGraph {
namespace {

UpscalerEligibility Eligible() {
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
    std::array<UpscalerEligibility, 8> failures{};
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
        UpscalerSelectionFailureReason::NotCompiled,
        UpscalerSelectionFailureReason::NotInRuntimeProfile,
        UpscalerSelectionFailureReason::NotPermittedByPass,
        UpscalerSelectionFailureReason::RuntimeUnsupported,
        UpscalerSelectionFailureReason::InputIncompatible,
        UpscalerSelectionFailureReason::NotInitialized,
        UpscalerSelectionFailureReason::Unqualified,
        UpscalerSelectionFailureReason::OverBudget
    };

    for (size_t index = 0; index < expected.size(); ++index) {
        EXPECT_EQ(EvaluateUpscalerEligibility(failures[index]), expected[index])
            << "failure index " << index;
    }
    EXPECT_NE(UpscalerSelectionFailureText(expected[0]),
              UpscalerSelectionFailureText(expected[1]));
    EXPECT_NE(UpscalerSelectionFailureText(expected[1]),
              UpscalerSelectionFailureText(expected[2]));
    EXPECT_NE(UpscalerSelectionFailureText(expected[2]),
              UpscalerSelectionFailureText(expected[3]));
    EXPECT_NE(UpscalerSelectionFailureText(expected[3]),
              UpscalerSelectionFailureText(expected[4]));
    EXPECT_NE(UpscalerSelectionFailureText(expected[4]),
              UpscalerSelectionFailureText(expected[5]));
    EXPECT_NE(UpscalerSelectionFailureText(expected[5]),
              UpscalerSelectionFailureText(expected[6]));
    EXPECT_NE(UpscalerSelectionFailureText(expected[6]),
              UpscalerSelectionFailureText(expected[7]));
}

TEST(UpscalerSelectionTest, SkipsRejectedBackendAndSelectsNextPermittedBackend) {
    auto fsr1 = Eligible();
    fsr1.runtimeSupported = false;
    const std::array candidates{
        UpscalerProviderCandidate{"fsr1", fsr1, false},
        UpscalerProviderCandidate{"linear", Eligible(), false},
        UpscalerProviderCandidate{"native", Eligible(), true}
    };

    const auto result = SelectUpscalerProvider(candidates);
    ASSERT_NE(result.selected, nullptr);
    EXPECT_EQ(result.selected->id, "linear");
    ASSERT_EQ(result.rejected.size(), 1u);
    EXPECT_EQ(result.rejected[0].id, "fsr1");
    EXPECT_EQ(result.rejected[0].reason,
              UpscalerSelectionFailureReason::RuntimeUnsupported);
}

TEST(UpscalerSelectionTest, EndsAtNativeWhenAllRequestedBackendsFail) {
    auto fsr1 = Eligible();
    fsr1.runtimeSupported = false;
    auto linear = Eligible();
    linear.admittedByBudget = false;
    const std::array candidates{
        UpscalerProviderCandidate{"fsr1", fsr1, false},
        UpscalerProviderCandidate{"linear", linear, false},
        UpscalerProviderCandidate{"native", Eligible(), true}
    };

    const auto result = SelectUpscalerProvider(candidates);
    ASSERT_NE(result.selected, nullptr);
    EXPECT_EQ(result.selected->id, "native");
    EXPECT_TRUE(result.selected->nativeFallback);
    ASSERT_EQ(result.rejected.size(), 2u);
    EXPECT_EQ(result.rejected[0].reason,
              UpscalerSelectionFailureReason::RuntimeUnsupported);
    EXPECT_EQ(result.rejected[1].reason,
              UpscalerSelectionFailureReason::OverBudget);
}

TEST(UpscalerSelectionTest, OptionalProviderFallsBackToFsr1ThenNativeWhenUnqualifiedOrUnsupported) {
    auto fsr3 = Eligible();
    fsr3.qualified = false;
    const std::array withFsr1{
        UpscalerProviderCandidate{"fsr3", fsr3, false},
        UpscalerProviderCandidate{"fsr1", Eligible(), false},
        UpscalerProviderCandidate{"native", Eligible(), true}
    };
    const auto fsr1Result = SelectUpscalerProvider(withFsr1);
    ASSERT_NE(fsr1Result.selected, nullptr);
    EXPECT_EQ(fsr1Result.selected->id, "fsr1");
    ASSERT_EQ(fsr1Result.rejected.size(), 1u);
    EXPECT_EQ(fsr1Result.rejected.front().reason,
              UpscalerSelectionFailureReason::Unqualified);

    auto fsr1 = Eligible();
    fsr1.runtimeSupported = false;
    const std::array withNative{
        UpscalerProviderCandidate{"fsr3", fsr3, false},
        UpscalerProviderCandidate{"fsr1", fsr1, false},
        UpscalerProviderCandidate{"native", Eligible(), true}
    };
    const auto nativeResult = SelectUpscalerProvider(withNative);
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
        UpscalerProviderCandidate{"fsr3", fsr3, false},
        UpscalerProviderCandidate{"fsr1", fsr1, false},
        UpscalerProviderCandidate{"native", Eligible(), true}
    };
    std::vector<std::string> failedProviders;

    // First graph compile attempt selects FSR3 provisionally. Inject the same failure result a
    // provider reports when FFX context creation fails, and record it through Prepare's helper.
    auto result = SelectUpscalerProviderForInitialization(candidates);
    ASSERT_NE(result.selected, nullptr);
    EXPECT_EQ(result.selected->id, "fsr3");
    EXPECT_FALSE(result.selected->eligibility.initialized);
    const UpscalerProviderInitializationResult initialization{
        "fsr3", false, "forced FFX context creation failure"};
    EXPECT_FALSE(initialization.initialized);
    EXPECT_EQ(initialization.providerId, "fsr3");
    ASSERT_TRUE(RecordUpscalerProviderInitializationFailure(
        failedProviders, initialization.providerId));
    candidates[0].initializationFailed =
        std::find(failedProviders.begin(), failedProviders.end(), candidates[0].id) !=
        failedProviders.end();

    result = SelectUpscalerProviderForInitialization(candidates);
    ASSERT_NE(result.selected, nullptr);
    EXPECT_EQ(result.selected->id, "fsr1");
    ASSERT_EQ(result.rejected.size(), 1u);
    EXPECT_EQ(result.rejected.front().reason,
              UpscalerSelectionFailureReason::NotInitialized);

    // If FSR1 is unavailable on this device, the required native candidate is next.
    candidates[1].eligibility.runtimeSupported = false;
    result = SelectUpscalerProviderForInitialization(candidates);
    ASSERT_NE(result.selected, nullptr);
    EXPECT_EQ(result.selected->id, "native");
    EXPECT_TRUE(result.selected->nativeFallback);
    ASSERT_EQ(result.rejected.size(), 2u);
    EXPECT_EQ(result.rejected[0].reason,
              UpscalerSelectionFailureReason::NotInitialized);
    EXPECT_EQ(result.rejected[1].reason,
              UpscalerSelectionFailureReason::RuntimeUnsupported);
}

TEST(UpscalerSelectionTest, RuntimeProfileCompileSetGatesOptionalProviders) {
    auto fsr3 = Eligible();
    fsr3.permittedByRuntimeProfile = false;
    const std::array candidates{
        UpscalerProviderCandidate{"fsr3", fsr3, false},
        UpscalerProviderCandidate{"fsr1", Eligible(), false},
        UpscalerProviderCandidate{"native", Eligible(), true}
    };

    const auto result = SelectUpscalerProvider(candidates);
    ASSERT_NE(result.selected, nullptr);
    EXPECT_EQ(result.selected->id, "fsr1");
    ASSERT_EQ(result.rejected.size(), 1u);
    EXPECT_EQ(result.rejected.front().reason,
              UpscalerSelectionFailureReason::NotInRuntimeProfile);
}

TEST(UpscalerContractTest, RequiresDeclaredInputsAndSupportedExtentsAndFormats) {
    constexpr std::array formats{UpscalerImageFormat::Rgba8Unorm};
    constexpr std::array required{UpscalerFrameInput::Color,
                                  UpscalerFrameInput::MotionVectors};
    const UpscalerContract contract{
        .inputExtents = {{1, 1}, {1920, 1080}, 1, 1},
        .outputExtents = {{1, 1}, {3840, 2160}, 1, 1},
        .inputFormats = formats,
        .outputFormats = formats,
        .requiredFrameInputs = required
    };
    constexpr std::array colorOnly{UpscalerFrameInput::Color};
    constexpr std::array completeInputs{UpscalerFrameInput::Color,
                                        UpscalerFrameInput::MotionVectors};

    EXPECT_FALSE(SupportsUpscalerFrame(
        contract, {{1280, 720}, {1920, 1080}, UpscalerImageFormat::Rgba8Unorm,
                   UpscalerImageFormat::Rgba8Unorm, colorOnly}));
    EXPECT_TRUE(SupportsUpscalerFrame(
        contract, {{1280, 720}, {1920, 1080}, UpscalerImageFormat::Rgba8Unorm,
                   UpscalerImageFormat::Rgba8Unorm, completeInputs}));
    EXPECT_FALSE(SupportsUpscalerFrame(
        contract, {{2560, 1440}, {1920, 1080}, UpscalerImageFormat::Rgba8Unorm,
                   UpscalerImageFormat::Rgba8Unorm, completeInputs}));
}

} // namespace
} // namespace Vixen::RenderGraph
