#include "graph/UpscalerContract.h"

#include <array>
#include <gtest/gtest.h>

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
    std::array<UpscalerEligibility, 7> failures{};
    failures.fill(Eligible());
    failures[0].compiled = false;
    failures[1].permittedByPass = false;
    failures[2].runtimeSupported = false;
    failures[3].inputCompatible = false;
    failures[4].initialized = false;
    failures[5].qualified = false;
    failures[6].admittedByBudget = false;

    constexpr std::array expected{
        UpscalerSelectionFailureReason::NotCompiled,
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
