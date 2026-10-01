#include "VulkanGraphApplication.h"
#include "Nodes/RenderTargetNode.h"

#include <gtest/gtest.h>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

class HeadlessUiApplication final : public VulkanGraphApplication {
public:
    HeadlessUiApplication() {
        SetPresentationTarget(PresentationTarget::Offscreen);
    }

    void BuildRenderGraph() override { BuildUIGraph(); }

    bool Render() override {
        if (!VulkanGraphApplication::Render()) return false;
        auto* graph = GetRenderGraph();
        if (!graph || graph->GetInstanceByName("main_window") ||
            graph->GetInstanceByName("ui_present") ||
            !graph->GetInstanceByName("ui_offscreen_target")) {
            captureError_ = "offscreen graph contains the wrong terminal nodes";
            return false;
        }
        auto* target = graph->GetInstanceByName("ui_offscreen_target")->GetOutput(0)->GetHandle<Vixen::Vulkan::Resources::IRenderTarget*>();
        EXPECT_EQ(target->GetCurrentLayout(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        EXPECT_EQ(target->GetImageUsageFlags(), VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        EXPECT_FALSE(target->UsesWsiSynchronization());
        const char* path = std::getenv("VIXEN_HEADLESS_UI_CAPTURE");
        if (!path || !CaptureOffscreenFrameToPng(path, captureError_)) return false;
        EXPECT_EQ(target->GetCurrentLayout(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        // Repeated readback preserves the layout; the next frame renders another ring slot.
        if (!CaptureOffscreenFrameToPng(path, captureError_)) return false;
        captured_ = true;
        return true;
    }

    bool Captured() const { return captured_; }
    const std::string& CaptureError() const { return captureError_; }

private:
    bool captured_ = false;
    std::string captureError_;
};

TEST(HeadlessUiGraph, RendersAndReadsBackAnOffscreenFrame) {
    const char* path = std::getenv("VIXEN_HEADLESS_UI_CAPTURE");
    ASSERT_NE(path, nullptr) << "CMake must set VIXEN_HEADLESS_UI_CAPTURE";
    std::error_code ec;
    std::filesystem::remove(path, ec);

    HeadlessUiApplication app;
    const int result = app.Run(RunOptions{.exitAfterFrames = 3});
    ASSERT_EQ(result, 0) << app.CaptureError();
    ASSERT_TRUE(app.Captured()) << app.CaptureError();

    int width = 0, height = 0, channels = 0;
    unsigned char* pixels = stbi_load(path, &width, &height, &channels, 3);
    ASSERT_NE(pixels, nullptr) << "engine PNG readback was not a decodable image";
    EXPECT_GT(width, 0);
    EXPECT_GT(height, 0);
    bool hasRenderedPixels = false;
    for (int i = 0; i < width * height * 3; ++i) {
        if (pixels[i] > 16) {
            hasRenderedPixels = true;
            break;
        }
    }
    stbi_image_free(pixels);
    EXPECT_TRUE(hasRenderedPixels) << "offscreen UI frame contains only the clear color";
}

} // namespace
