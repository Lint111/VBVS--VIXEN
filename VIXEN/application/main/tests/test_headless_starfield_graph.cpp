#include "VulkanGraphApplication.h"
#include "Nodes/SkySphereNode.h"
#include "Data/Nodes/SkySphereNodeConfig.h"
#include <stb_image.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::vector<char> ReadBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

struct RgbImage {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels;
};

RgbImage DecodeRgb(const std::vector<char>& png) {
    RgbImage image;
    if (png.empty()) return image;
    int channels = 0;
    auto* decoded = stbi_load_from_memory(
        reinterpret_cast<const unsigned char*>(png.data()), static_cast<int>(png.size()),
        &image.width, &image.height, &channels, 3);
    if (!decoded) return image;
    image.pixels.assign(decoded, decoded + (size_t(image.width) * image.height * 3));
    stbi_image_free(decoded);
    return image;
}

std::array<unsigned char, 3> PixelAt(const RgbImage& image, int x, int y) {
    const size_t offset = (size_t(y) * image.width + x) * 3;
    return {image.pixels[offset + 0], image.pixels[offset + 1], image.pixels[offset + 2]};
}

class HeadlessStarfieldApplication final : public VulkanGraphApplication {
public:
    HeadlessStarfieldApplication(std::string prefix, bool saveOffCapture)
        : prefix_(std::move(prefix)), saveOffCapture_(saveOffCapture) {
        SetPresentationTarget(PresentationTarget::Offscreen);
    }

    void BuildRenderGraph() override { VulkanGraphApplication::BuildRenderGraph(); }

    bool Render() override {
        if (!VulkanGraphApplication::Render()) return false;
        ++completedFrames_;

        auto* graph = GetRenderGraph();
        auto* targetNode = graph ? graph->GetInstanceByName("main_offscreen_target") : nullptr;
        auto* skyNode = graph ? dynamic_cast<Vixen::RenderGraph::SkySphereNode*>(
            graph->GetInstanceByName("sky_sphere")) : nullptr;
        if (!targetNode || !skyNode) {
            captureError_ = "production offscreen graph is missing its target or sky_sphere node";
            return false;
        }

        if (completedFrames_ == 5) {
            if (saveOffCapture_ && !Capture(prefix_ + "-off.png")) return false;
            // This changes the actual node parameter after a completed off frame; the cache
            // observes it on the next graph execution without rebuilding the render graph.
            skyNode->SetParameter(Vixen::RenderGraph::SkySphereNodeConfig::PARAM_ENABLED, uint32_t{1});
        } else if (completedFrames_ == 6) {
            if (!Capture(prefix_ + "-on.png")) return false;
            capturedOn_ = true;
        }
        return true;
    }

    bool CapturedOn() const { return capturedOn_; }
    const std::string& CaptureError() const { return captureError_; }

private:
    bool Capture(const std::string& path) {
        if (!CaptureOffscreenFrameToPng(path, captureError_)) return false;
        return true;
    }

    std::string prefix_;
    bool saveOffCapture_ = false;
    bool capturedOn_ = false;
    uint32_t completedFrames_ = 0;
    std::string captureError_;
};

TEST(HeadlessStarfieldGraph, CaptureSingleRun) {
    const char* prefixEnv = std::getenv("VIXEN_HEADLESS_STARFIELD_CAPTURE_PREFIX");
    ASSERT_NE(prefixEnv, nullptr) << "CMake must configure the starfield capture prefix";
    const std::string prefix(prefixEnv);
    const std::filesystem::path offPath = prefix + "-off.png";
    const std::filesystem::path onPath = prefix + "-on.png";
    std::error_code ec;
    std::filesystem::remove(offPath, ec);
    std::filesystem::remove(onPath, ec);

    HeadlessStarfieldApplication run(prefix, true);
    ASSERT_EQ(run.Run(RunOptions{.exitAfterFrames = 6}), 0) << run.CaptureError();
    ASSERT_TRUE(run.CapturedOn()) << run.CaptureError();
    EXPECT_FALSE(ReadBytes(offPath).empty()) << "disabled capture was empty";
    EXPECT_FALSE(ReadBytes(onPath).empty()) << "enabled capture was empty";
}

TEST(HeadlessStarfieldGraph, IndependentRunsHaveIdenticalPixelsAndToggleChangesOnlyBackground) {
    const char* prefixEnv = std::getenv("VIXEN_HEADLESS_STARFIELD_CAPTURE_PREFIX");
    ASSERT_NE(prefixEnv, nullptr) << "CMake must configure the starfield capture prefix";
    const std::string prefix(prefixEnv);
    const std::vector<char> offBytes = ReadBytes(prefix + "-off.png");
    const std::vector<char> repeatOffBytes = ReadBytes(prefix + "-repeat-off.png");
    const std::vector<char> firstOnBytes = ReadBytes(prefix + "-on.png");
    const std::vector<char> repeatOnBytes = ReadBytes(prefix + "-repeat-on.png");
    ASSERT_FALSE(offBytes.empty()) << "first disabled capture was empty";
    ASSERT_FALSE(repeatOffBytes.empty()) << "second disabled capture was empty";
    ASSERT_FALSE(firstOnBytes.empty()) << "first enabled capture was empty";
    ASSERT_FALSE(repeatOnBytes.empty()) << "second enabled capture was empty";

    const RgbImage off = DecodeRgb(offBytes);
    const RgbImage repeatOff = DecodeRgb(repeatOffBytes);
    const RgbImage on = DecodeRgb(firstOnBytes);
    const RgbImage repeatOn = DecodeRgb(repeatOnBytes);
    ASSERT_EQ(off.width, 500);
    ASSERT_EQ(off.height, 500);
    ASSERT_EQ(repeatOff.width, off.width);
    ASSERT_EQ(repeatOff.height, off.height);
    ASSERT_EQ(on.width, off.width);
    ASSERT_EQ(on.height, off.height);
    ASSERT_EQ(repeatOn.width, on.width);
    ASSERT_EQ(repeatOn.height, on.height);
    ASSERT_EQ(off.pixels.size(), size_t(off.width) * off.height * 3);
    ASSERT_EQ(repeatOff.pixels.size(), off.pixels.size());
    ASSERT_EQ(on.pixels.size(), off.pixels.size());
    ASSERT_EQ(repeatOn.pixels.size(), on.pixels.size());

    EXPECT_EQ(offBytes, repeatOffBytes)
        << "two independent default-off graph runs must preserve the existing background bytes";
    EXPECT_EQ(on.pixels, repeatOn.pixels)
        << "two independent runs with the same seed must produce identical RGB pixels";

    // These are stable interior pixels in the Cornell red and green walls, away from the HUD.
    const auto redWallOff = PixelAt(off, 151, 200);
    const auto redWallOn = PixelAt(on, 151, 200);
    const auto greenWallOff = PixelAt(off, 348, 200);
    const auto greenWallOn = PixelAt(on, 348, 200);
    EXPECT_GT(redWallOff[0], 2 * redWallOff[1]) << "expected the red wall test body at (151, 200)";
    EXPECT_GT(greenWallOff[1], 2 * greenWallOff[0]) << "expected the green wall test body at (348, 200)";
    EXPECT_EQ(redWallOn, redWallOff) << "the background must not alter the red wall hit pixel";
    EXPECT_EQ(greenWallOn, greenWallOff) << "the background must not alter the green wall hit pixel";

    size_t changedPixels = 0;
    for (size_t pixel = 0; pixel < off.pixels.size(); pixel += 3) {
        if (off.pixels[pixel + 0] != on.pixels[pixel + 0] ||
            off.pixels[pixel + 1] != on.pixels[pixel + 1] ||
            off.pixels[pixel + 2] != on.pixels[pixel + 2]) {
            ++changedPixels;
        }
    }
    EXPECT_GT(changedPixels, 0u) << "enabling the starfield must change visible background pixels";
    EXPECT_LT(changedPixels, size_t(off.width) * off.height / 3)
        << "the restrained background should not replace a large fraction of scene pixels";
}

} // namespace
