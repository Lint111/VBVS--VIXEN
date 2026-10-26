#include "VulkanGraphApplication.h"
#include "Core/NodeInstance.h"
#include "Data/Nodes/LightingConfigNodeConfig.h"
#include "Nodes/LightingConfigNode.h"
#include <stb_image.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unordered_set>
#include <string>
#include <vector>

namespace {

using Vixen::RenderGraph::LightingConfigNodeConfig;

std::vector<char> ReadBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

struct CelCaptureStats {
    int width = 0;
    int height = 0;
    size_t nonBackgroundPixels = 0;
    size_t quantizedColors = 0;
    uint8_t minLuma = 255;
    uint8_t maxLuma = 0;

    [[nodiscard]] bool HasRenderedScene(uint32_t requestedBands) const {
        const size_t pixels = size_t(width) * size_t(height);
        return width > 0 && height > 0 && nonBackgroundPixels > pixels / 100u &&
            maxLuma > minLuma + 16u && quantizedColors >= std::min<size_t>(requestedBands, 3u);
    }
};

CelCaptureStats InspectCapture(const std::filesystem::path& path) {
    CelCaptureStats stats;
    int channels = 0;
    stbi_uc* pixels = stbi_load(path.string().c_str(), &stats.width, &stats.height, &channels, 4);
    if (!pixels) return stats;
    const size_t count = size_t(stats.width) * size_t(stats.height);
    const uint32_t background = uint32_t(pixels[0]) << 16u | uint32_t(pixels[1]) << 8u | pixels[2];
    std::unordered_set<uint32_t> colors;
    colors.reserve(std::min<size_t>(count, 256u));
    for (size_t i = 0; i < count; ++i) {
        const uint8_t r = pixels[i * 4u];
        const uint8_t g = pixels[i * 4u + 1u];
        const uint8_t b = pixels[i * 4u + 2u];
        const uint32_t rgb = uint32_t(r) << 16u | uint32_t(g) << 8u | b;
        if (rgb != background) ++stats.nonBackgroundPixels;
        const uint8_t luma = static_cast<uint8_t>((54u * r + 183u * g + 19u * b) / 256u);
        stats.minLuma = std::min(stats.minLuma, luma);
        stats.maxLuma = std::max(stats.maxLuma, luma);
        colors.insert(uint32_t(r >> 4u) << 8u | uint32_t(g >> 4u) << 4u | (b >> 4u));
    }
    stats.quantizedColors = colors.size();
    stbi_image_free(pixels);
    return stats;
}

uint32_t CaptureBandCount() {
    const char* value = std::getenv("VIXEN_CELSHADE_BAND_COUNT");
    if (!value) return 3u;
    const unsigned long parsed = std::strtoul(value, nullptr, 10);
    return static_cast<uint32_t>(std::clamp(parsed, 2ul, 5ul));
}

bool CaptureRampSoftness(float& softness) {
    const char* value = std::getenv("VIXEN_CELSHADE_RAMP_SOFTNESS");
    if (!value) return false;
    char* end = nullptr;
    const float parsed = std::strtof(value, &end);
    if (end == value || *end != '\0' || !std::isfinite(parsed)) return false;
    softness = std::clamp(parsed, 0.0f, 0.5f);
    return true;
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
                float rampSoftness = 0.0f;
                if (action_ == CaptureAction::BandCount && CaptureRampSoftness(rampSoftness)) {
                    Vixen::RenderGraph::PartialLook look;
                    look.celRampSoftness = rampSoftness;
                    SetFixtureLook(*lighting, look);
                }
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
    static void SetFixtureLook(Vixen::RenderGraph::NodeInstance& lighting, const Vixen::RenderGraph::PartialLook& look) {
        Vixen::RenderGraph::LookContribution contribution;
        contribution.look = look;
        dynamic_cast<Vixen::RenderGraph::LightingConfigNode&>(lighting).Look().SetSlot(
            Vixen::RenderGraph::LookLayer::DevOverride, 1, std::move(contribution));
    }

    static void SetBoundaryParameters(Vixen::RenderGraph::NodeInstance& lighting) {
        lighting.SetParameter(LightingConfigNodeConfig::PARAM_CEL_LIT_HUE_SHIFT_DEGREES, 180.0f);
        lighting.SetParameter(LightingConfigNodeConfig::PARAM_CEL_SHADOW_HUE_SHIFT_DEGREES, -180.0f);
        lighting.SetParameter(LightingConfigNodeConfig::PARAM_CEL_BAND_FALLOFF_START, 0.0f);
        lighting.SetParameter(LightingConfigNodeConfig::PARAM_CEL_BAND_FALLOFF_END, 1000000.0f);
        lighting.SetParameter(LightingConfigNodeConfig::PARAM_CEL_LIGHT_SPILL_SCALE, 0.25f);
        Vixen::RenderGraph::PartialLook look;
        look.celShadowThreshold = 0.0f;
        look.celLitThreshold = 1.0f;
        look.celRampSoftness = 0.5f;
        look.exposureCompensationEV = 100.0f;
        SetFixtureLook(lighting, look);
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
    const auto stats = InspectCapture(capture);
    ASSERT_TRUE(stats.HasRenderedScene(CaptureBandCount()))
        << "cel capture must contain a spatially varying scene: "
        << stats.width << 'x' << stats.height
        << ", non_background=" << stats.nonBackgroundPixels
        << ", quantized_colors=" << stats.quantizedColors
        << ", luma_range=" << int(stats.minLuma) << "-" << int(stats.maxLuma);
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
    ASSERT_TRUE(InspectCapture(legacy).HasRenderedScene(2u)) << "Lambert capture is not a rendered scene";
    ASSERT_TRUE(InspectCapture(cel).HasRenderedScene(3u)) << "cel capture is not a rendered scene";
    EXPECT_NE(legacyBytes, celBytes)
        << "the node parameter must switch the live renderer from Lambert+GGX to cel";
}

TEST(CelShading, ParameterBoundsRenderWithoutThrowing) {
    CelShadingApplication app(CaptureAction::Bounds);
    EXPECT_EQ(app.Run(RunOptions{.exitAfterFrames = 5}), 0) << app.CaptureError();
}

} // namespace
