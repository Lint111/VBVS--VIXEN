#include "VulkanGraphApplication.h"
#include "Nodes/MiningBeamBufferNode.h"
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

enum class BeamScenario {
    Default,
    DisabledWithList,
    EnabledEmpty,
    Active,
};

class HeadlessCornellApplication final : public VulkanGraphApplication {
public:
    HeadlessCornellApplication(BeamScenario scenario, std::string capturePrefix)
        : scenario_(scenario), capturePrefix_(std::move(capturePrefix)) {
        SetPresentationTarget(PresentationTarget::Offscreen);
    }

    void BuildRenderGraph() override { VulkanGraphApplication::BuildRenderGraph(); }

    bool Render() override {
        if (!configuredBeamScenario_) {
            auto* graph = GetRenderGraph();
            auto* node = graph
                ? static_cast<Vixen::RenderGraph::MiningBeamBufferNode*>(
                      graph->GetInstanceByName("mining_beam_buffer"))
                : nullptr;
            if (!node) {
                captureError_ = "production graph did not create mining_beam_buffer";
                return false;
            }

            Vixen::RenderGraph::MiningBeamInput beam;
            beam.sourceInstanceIndex = 6; // Cornell sphere object
            beam.targetInstanceIndex = 7; // Cornell box object
            // The virtual Cornell bodies are world-authored procedural recipes.
            // These points sit on their facing surfaces, leaving a visible gap.
            beam.sourceLocalOffset = glm::vec3(13.85f, 9.4f, 14.93f);
            beam.targetLocalOffset = glm::vec3(18.8f, 9.1f, 17.9f);
            beam.radius = 0.09f;
            beam.luminosity = 0.75f;
            beam.purposeScale = 0.12f;

            switch (scenario_) {
                case BeamScenario::Default:
                    break;
                case BeamScenario::DisabledWithList:
                    node->SetBeams({beam});
                    node->SetEnabled(false);
                    break;
                case BeamScenario::EnabledEmpty:
                    node->SetBeams({});
                    node->SetEnabled(true);
                    break;
                case BeamScenario::Active:
                    node->SetBeams({beam});
                    node->SetEnabled(true);
                    break;
            }
            configuredBeamScenario_ = true;
        }

        if (!VulkanGraphApplication::Render()) return false;
        if (++completedFrames_ < 5) return true;

        auto* graph = GetRenderGraph();
        if (!graph || graph->GetInstanceByName("main_window") ||
            graph->GetInstanceByName("main_swapchain") || graph->GetInstanceByName("present") ||
            !graph->GetInstanceByName("main_offscreen_target")) {
            captureError_ = "production graph did not select its offscreen terminal target";
            return false;
        }

        auto* target = graph->GetInstanceByName("main_offscreen_target")->GetOutput(0)->GetHandle<Vixen::Vulkan::Resources::IRenderTarget*>();
        EXPECT_EQ(target->GetCurrentLayout(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        EXPECT_TRUE(target->GetImageUsageFlags() & VK_IMAGE_USAGE_STORAGE_BIT);
        EXPECT_TRUE(target->GetImageUsageFlags() & VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        EXPECT_FALSE(target->UsesWsiSynchronization());
        const std::string first = capturePrefix_ + "-a.png";
        const std::string second = capturePrefix_ + "-b.png";
        if (!CaptureOffscreenFrameToPng(first, captureError_) ||
            !CaptureOffscreenFrameToPng(second, captureError_)) return false;
        captured_ = true;
        return true;
    }

    bool Captured() const { return captured_; }
    const std::string& CaptureError() const { return captureError_; }

private:
    BeamScenario scenario_;
    std::string capturePrefix_;
    bool captured_ = false;
    bool configuredBeamScenario_ = false;
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

    auto captureScenario = [](BeamScenario scenario, const std::string& capturePrefix) {
        HeadlessCornellApplication app(scenario, capturePrefix);
        const int result = app.Run(RunOptions{.exitAfterFrames = 5});
        EXPECT_EQ(result, 0) << app.CaptureError();
        EXPECT_TRUE(app.Captured()) << app.CaptureError();

        const auto firstBytes = ReadBytes(capturePrefix + "-a.png");
        const auto secondBytes = ReadBytes(capturePrefix + "-b.png");
        EXPECT_FALSE(firstBytes.empty()) << "first engine PNG readback was empty";
        EXPECT_FALSE(secondBytes.empty()) << "second engine PNG readback was empty";
        EXPECT_EQ(firstBytes, secondBytes)
            << "capturing the same completed frame twice must produce identical PNG bytes";
        return firstBytes;
    };

    const auto defaultBytes = captureScenario(BeamScenario::Default, prefix);
    const auto disabledBytes = captureScenario(BeamScenario::DisabledWithList,
                                               std::string(prefix) + "-beam-disabled");
    const auto emptyBytes = captureScenario(BeamScenario::EnabledEmpty,
                                            std::string(prefix) + "-beam-empty");
    const auto activeBytes = captureScenario(BeamScenario::Active,
                                             std::string(prefix) + "-beam-active");
    ASSERT_FALSE(defaultBytes.empty());
    ASSERT_FALSE(disabledBytes.empty());
    ASSERT_FALSE(emptyBytes.empty());
    ASSERT_FALSE(activeBytes.empty());
    EXPECT_EQ(defaultBytes, disabledBytes)
        << "a populated but disabled beam list must preserve current capture bytes";
    EXPECT_EQ(defaultBytes, emptyBytes)
        << "an enabled empty list must preserve current capture bytes";
    EXPECT_NE(defaultBytes, activeBytes)
        << "the active beam capture must differ from the no-beam production frame";

    const RgbImage image = DecodeRgb(defaultBytes);
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
