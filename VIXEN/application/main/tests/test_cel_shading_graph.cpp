#include "VulkanGraphApplication.h"
#include "Core/NodeInstance.h"
#include "Data/Nodes/LightingConfigNodeConfig.h"
#include <stb_image.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using Vixen::RenderGraph::LightingConfigNodeConfig;

std::vector<char> ReadBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

uint32_t CaptureBandCount() {
    const char* value = std::getenv("VIXEN_CELSHADE_BAND_COUNT");
    if (!value) return 3u;
    const unsigned long parsed = std::strtoul(value, nullptr, 10);
    return static_cast<uint32_t>(std::clamp(parsed, 2ul, 5ul));
}

float CelRampPosition(float NdotL, float shadowThreshold, float litThreshold) {
    return std::clamp((NdotL - shadowThreshold) / (litThreshold - shadowThreshold), 0.0f, 1.0f);
}

uint32_t CelBandAt(float NdotL, uint32_t bandCount, float shadowThreshold, float litThreshold) {
    const float ramp = CelRampPosition(NdotL, shadowThreshold, litThreshold);
    return static_cast<uint32_t>(std::floor(ramp * float(bandCount - 1u) + 0.5f));
}

enum class CaptureAction {
    BandCount,
    ModeSwitch,
    Bounds,
};

class CelShadingApplication final : public VulkanGraphApplication {
public:
    explicit CelShadingApplication(CaptureAction action) : action_(action) {
        SetPresentationTarget(PresentationTarget::Offscreen);
    }

    bool Render() override {
        if (!VulkanGraphApplication::Render()) return false;
        ++completedFrames_;

        auto* graph = GetRenderGraph();
        auto* lighting = graph ? graph->GetInstanceByName("lighting_config") : nullptr;
        if (!lighting) {
            captureError_ = "production graph has no lighting_config node";
            return false;
        }

        if (completedFrames_ == 1u) {
            if (action_ == CaptureAction::ModeSwitch) {
                lighting->SetParameter(LightingConfigNodeConfig::PARAM_SHADING_MODE,
                    LightingConfigNodeConfig::SHADING_MODE_LAMBERT_GGX);
            } else {
                lighting->SetParameter(LightingConfigNodeConfig::PARAM_SHADING_MODE,
                    LightingConfigNodeConfig::SHADING_MODE_CEL);
                lighting->SetParameter(LightingConfigNodeConfig::PARAM_CEL_BAND_COUNT,
                    action_ == CaptureAction::BandCount ? CaptureBandCount() : 5u);
                if (action_ == CaptureAction::Bounds) SetBoundaryParameters(*lighting);
            }
            return true;
        }

        if (action_ == CaptureAction::ModeSwitch) {
            if (completedFrames_ == 2u) {
                if (!Capture("lambert-ggx")) return false;
                lighting->SetParameter(LightingConfigNodeConfig::PARAM_SHADING_MODE,
                    LightingConfigNodeConfig::SHADING_MODE_CEL);
            } else if (completedFrames_ == 3u) {
                if (!Capture("cel")) return false;
                capturedModeSwitch_ = true;
            }
        } else if (action_ == CaptureAction::BandCount && completedFrames_ == 5u) {
            if (!Capture("bands-" + std::to_string(CaptureBandCount()))) return false;
            capturedBandCount_ = true;
        }
        return true;
    }

    bool CapturedBandCount() const { return capturedBandCount_; }
    bool CapturedModeSwitch() const { return capturedModeSwitch_; }
    const std::string& CaptureError() const { return captureError_; }

private:
    static void SetBoundaryParameters(Vixen::RenderGraph::NodeInstance& lighting) {
        lighting.SetParameter(LightingConfigNodeConfig::PARAM_CEL_SHADOW_THRESHOLD, 0.0f);
        lighting.SetParameter(LightingConfigNodeConfig::PARAM_CEL_LIT_THRESHOLD, 1.0f);
        lighting.SetParameter(LightingConfigNodeConfig::PARAM_CEL_RAMP_SOFTNESS, 0.5f);
        lighting.SetParameter(LightingConfigNodeConfig::PARAM_CEL_LIT_HUE_SHIFT_DEGREES, 180.0f);
        lighting.SetParameter(LightingConfigNodeConfig::PARAM_CEL_SHADOW_HUE_SHIFT_DEGREES, -180.0f);
        lighting.SetParameter(LightingConfigNodeConfig::PARAM_CEL_BAND_FALLOFF_START, 0.0f);
        lighting.SetParameter(LightingConfigNodeConfig::PARAM_CEL_BAND_FALLOFF_END, 1000000.0f);
        lighting.SetParameter(LightingConfigNodeConfig::PARAM_CEL_LIGHT_SPILL_SCALE, 0.25f);
    }

    bool Capture(const std::string& suffix) {
        const char* prefix = std::getenv("VIXEN_CELSHADE_CAPTURE_PREFIX");
        if (!prefix) {
            captureError_ = "CTest did not configure VIXEN_CELSHADE_CAPTURE_PREFIX";
            return false;
        }
        return CaptureOffscreenFrameToPng(std::string(prefix) + "-" + suffix + ".png",
                                          captureError_);
    }

    CaptureAction action_;
    uint32_t completedFrames_ = 0;
    bool capturedBandCount_ = false;
    bool capturedModeSwitch_ = false;
    std::string captureError_;
};

TEST(CelShading, BandQuantizationMapsNdotLToExpectedBand) {
    constexpr float kShadow = 0.16f;
    constexpr float kLit = 0.86f;
    EXPECT_EQ(CelBandAt(kShadow, 2u, kShadow, kLit), 0u);
    EXPECT_EQ(CelBandAt(0.51f, 2u, kShadow, kLit), 1u);
    EXPECT_EQ(CelBandAt(kShadow, 3u, kShadow, kLit), 0u);
    EXPECT_EQ(CelBandAt(0.51f, 3u, kShadow, kLit), 1u);
    EXPECT_EQ(CelBandAt(kLit, 3u, kShadow, kLit), 2u);
    EXPECT_EQ(CelBandAt(kShadow, 5u, kShadow, kLit), 0u);
    EXPECT_EQ(CelBandAt(0.335f, 5u, kShadow, kLit), 1u);
    EXPECT_EQ(CelBandAt(0.51f, 5u, kShadow, kLit), 2u);
    EXPECT_EQ(CelBandAt(0.685f, 5u, kShadow, kLit), 3u);
    EXPECT_EQ(CelBandAt(kLit, 5u, kShadow, kLit), 4u);
}

TEST(CelShading, CaptureBandCount) {
    const char* prefix = std::getenv("VIXEN_CELSHADE_CAPTURE_PREFIX");
    ASSERT_NE(prefix, nullptr) << "CTest must configure the capture prefix";
    const auto capture = std::string(prefix) + "-bands-" +
                         std::to_string(CaptureBandCount()) + ".png";
    std::error_code ec;
    std::filesystem::remove(capture, ec);

    CelShadingApplication app(CaptureAction::BandCount);
    ASSERT_EQ(app.Run(RunOptions{.exitAfterFrames = 5}), 0) << app.CaptureError();
    ASSERT_TRUE(app.CapturedBandCount()) << app.CaptureError();
    EXPECT_FALSE(ReadBytes(capture).empty()) << "cel band capture was empty";
}

TEST(CelShading, ModeSwitchTakesEffectOnTheLiveNode) {
    const char* prefix = std::getenv("VIXEN_CELSHADE_CAPTURE_PREFIX");
    ASSERT_NE(prefix, nullptr) << "CTest must configure the capture prefix";
    const std::filesystem::path legacy = std::string(prefix) + "-lambert-ggx.png";
    const std::filesystem::path cel = std::string(prefix) + "-cel.png";
    std::error_code ec;
    std::filesystem::remove(legacy, ec);
    std::filesystem::remove(cel, ec);

    CelShadingApplication app(CaptureAction::ModeSwitch);
    ASSERT_EQ(app.Run(RunOptions{.exitAfterFrames = 3}), 0) << app.CaptureError();
    ASSERT_TRUE(app.CapturedModeSwitch()) << app.CaptureError();
    const auto legacyBytes = ReadBytes(legacy);
    const auto celBytes = ReadBytes(cel);
    ASSERT_FALSE(legacyBytes.empty());
    ASSERT_FALSE(celBytes.empty());
    EXPECT_NE(legacyBytes, celBytes)
        << "the node parameter must switch the live renderer from Lambert+GGX to cel";
}

TEST(CelShading, ParameterBoundsRenderWithoutThrowing) {
    CelShadingApplication app(CaptureAction::Bounds);
    EXPECT_EQ(app.Run(RunOptions{.exitAfterFrames = 5}), 0) << app.CaptureError();
}

} // namespace
