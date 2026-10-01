#include "VulkanGraphApplication.h"
#include <stb_image.h>

#include <gtest/gtest.h>

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

char CornellColorClass(const RgbImage& image, int x, int y) {
    const size_t offset = (size_t(y) * image.width + x) * 3;
    const unsigned char r = image.pixels[offset + 0];
    const unsigned char g = image.pixels[offset + 1];
    const unsigned char b = image.pixels[offset + 2];
    if (r > 2 * g && r > 2 * b) return 'R';
    if (g > 2 * r && g > 2 * b) return 'G';
    if (std::abs(int(r) - int(g)) <= 20 && std::abs(int(g) - int(b)) <= 20) return 'N';
    return 'X';
}

struct SeamLineCheck {
    int colorChanges = 0;
    int unexpectedPixels = 0;
};

SeamLineCheck CheckVerticalSeam(const RgbImage& image, int x, int firstY, int lastY,
                                char expectedColor) {
    SeamLineCheck check;
    char previous = CornellColorClass(image, x, firstY);
    check.unexpectedPixels += previous != expectedColor;
    for (int y = firstY + 1; y <= lastY; ++y) {
        const char current = CornellColorClass(image, x, y);
        check.colorChanges += current != previous;
        check.unexpectedPixels += current != expectedColor;
        previous = current;
    }
    return check;
}

class HeadlessCornellApplication final : public VulkanGraphApplication {
public:
    HeadlessCornellApplication() {
        SetPresentationTarget(PresentationTarget::Offscreen);
    }

    void BuildRenderGraph() override { VulkanGraphApplication::BuildRenderGraph(); }

    bool Render() override {
        if (!VulkanGraphApplication::Render()) return false;
        if (++completedFrames_ < 5) return true;

        auto* graph = GetRenderGraph();
        if (!graph || graph->GetInstanceByName("main_window") ||
            graph->GetInstanceByName("main_swapchain") || graph->GetInstanceByName("present") ||
            !graph->GetInstanceByName("main_offscreen_target")) {
            captureError_ = "production graph did not select its offscreen terminal target";
            return false;
        }

        const char* prefix = std::getenv("VIXEN_HEADLESS_CORNELL_CAPTURE_PREFIX");
        if (!prefix) {
            captureError_ = "CMake did not configure the Cornell capture prefix";
            return false;
        }
        const std::string first = std::string(prefix) + "-a.png";
        const std::string second = std::string(prefix) + "-b.png";
        if (!CaptureOffscreenFrameToPng(first, captureError_) ||
            !CaptureOffscreenFrameToPng(second, captureError_)) return false;
        captured_ = true;
        return true;
    }

    bool Captured() const { return captured_; }
    const std::string& CaptureError() const { return captureError_; }

private:
    bool captured_ = false;
    uint32_t completedFrames_ = 0;
    std::string captureError_;
};

TEST(HeadlessCornellGraph, ProductionGraphRendersDeterministicCornellWithStableSharedWallSeams) {
    const char* prefix = std::getenv("VIXEN_HEADLESS_CORNELL_CAPTURE_PREFIX");
    ASSERT_NE(prefix, nullptr) << "CMake must set VIXEN_HEADLESS_CORNELL_CAPTURE_PREFIX";
    const std::filesystem::path first = std::string(prefix) + "-a.png";
    const std::filesystem::path second = std::string(prefix) + "-b.png";
    std::error_code ec;
    std::filesystem::remove(first, ec);
    std::filesystem::remove(second, ec);

    HeadlessCornellApplication app;
    const int result = app.Run(RunOptions{.exitAfterFrames = 5});
    ASSERT_EQ(result, 0) << app.CaptureError();
    ASSERT_TRUE(app.Captured()) << app.CaptureError();

    const auto firstBytes = ReadBytes(first);
    const auto secondBytes = ReadBytes(second);
    ASSERT_FALSE(firstBytes.empty()) << "first engine PNG readback was empty";
    ASSERT_FALSE(secondBytes.empty()) << "second engine PNG readback was empty";
    EXPECT_EQ(firstBytes, secondBytes)
        << "capturing the same completed Cornell frame twice must produce identical PNG bytes";

    const RgbImage image = DecodeRgb(firstBytes);
    ASSERT_EQ(image.width, 500) << "Cornell terminal capture must be a nonempty 500x500 RGB PNG";
    ASSERT_EQ(image.height, 500) << "Cornell terminal capture must be a nonempty 500x500 RGB PNG";
    ASSERT_EQ(image.pixels.size(), size_t(image.width) * image.height * 3);

    // On the 500x500 Cornell camera, columns 151 and 348 are the shared left/back and
    // right/back wall seams. Rows 170..220 avoid the HUD panels. P584's exact-distance/lower-
    // body-slot tie-break must give one stable owner down each seam, with no alternating pixels.
    constexpr int kFirstSeamRow = 170;
    constexpr int kLastSeamRow = 220;
    const SeamLineCheck leftSeam = CheckVerticalSeam(image, 151, kFirstSeamRow, kLastSeamRow, 'R');
    const SeamLineCheck rightSeam = CheckVerticalSeam(image, 348, kFirstSeamRow, kLastSeamRow, 'G');
    EXPECT_EQ(leftSeam.unexpectedPixels, 0)
        << "left/back shared-wall seam must consistently select the red left wall";
    EXPECT_EQ(leftSeam.colorChanges, 0)
        << "left/back shared-wall seam has row-to-row pixel ownership alternation";
    EXPECT_EQ(rightSeam.unexpectedPixels, 0)
        << "right/back shared-wall seam must consistently select the green right wall";
    EXPECT_EQ(rightSeam.colorChanges, 0)
        << "right/back shared-wall seam has row-to-row pixel ownership alternation";
}

} // namespace
