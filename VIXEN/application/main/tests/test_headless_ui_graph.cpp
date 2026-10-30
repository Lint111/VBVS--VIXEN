#include "VulkanGraphApplication.h"
#include "Data/Nodes/UIRenderNodeConfig.h"
#include "Nodes/RenderTargetNode.h"
#include "Nodes/UIRenderNode.h"
#include "Text/RmlTextElement.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>

#include <gtest/gtest.h>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

void SetUiDemoEnvironment(bool enabled) {
#if defined(_WIN32)
    _putenv_s("VIXEN_UI_DEMO", enabled ? "1" : "");
#else
    if (enabled)
        setenv("VIXEN_UI_DEMO", "1", 1);
    else
        unsetenv("VIXEN_UI_DEMO");
#endif
}

class HeadlessTextResolver final : public Vixen::Text::TextResolver {
public:
    HeadlessTextResolver() {
        constexpr char kText[] = "A bold blue pause big.";
        bytes.assign(kText, sizeof(kText) - 1u);
        offsets = {0u, static_cast<std::uint32_t>(bytes.size())};
        elements = {
            {Vixen::Text::TextElementKind::ConventionTag, 0u, 1u, 0u},
            {Vixen::Text::TextElementKind::ConventionTag, 0u, 2u, 0x0064c8ffu},
            {Vixen::Text::TextElementKind::ConventionTag, 0u, 3u, 0u},
            {Vixen::Text::TextElementKind::ConventionTag, 0u, 4u, 0u},
        };
        spans = {
            {0u, 2u, 6u},   // emphasis: bold
            {1u, 7u, 11u},  // color: blue
            {2u, 12u, 12u}, // pause marker
            {3u, 18u, 21u}, // size: big
        };
    }

    bool Resolve(Vixen::Text::TextRef ref, Vixen::Text::ResolvedText& out) const override {
        if (ref.key != 0u || ref.structure != 0u)
            return false;
        out.arena = {bytes.data(), offsets.data(), static_cast<std::uint32_t>(bytes.size()), 1u};
        out.elements = {static_cast<std::uint16_t>(elements.size()), 0u, elements.data()};
        out.placement = {static_cast<std::uint16_t>(spans.size()), 0u, spans.data()};
        return true;
    }

    bool DescribeTag(const Vixen::Text::TextElement& element,
                     Vixen::Text::TextTagDescription& out) const override {
        switch (element.typeId) {
        case 1u: out.builtIn = Vixen::Text::BuiltInTextTag::Emphasis; return true;
        case 2u: out.builtIn = Vixen::Text::BuiltInTextTag::Color; out.rgba = element.payload; return true;
        case 3u: out.builtIn = Vixen::Text::BuiltInTextTag::Pause; return true;
        case 4u: out.builtIn = Vixen::Text::BuiltInTextTag::Size; out.sizeScale = 1.25f; return true;
        default: return false;
        }
    }

private:
    std::string bytes;
    std::vector<std::uint32_t> offsets;
    std::vector<Vixen::Text::TextElement> elements;
    std::vector<Vixen::Text::TextSpan> spans;
};

class HeadlessUiApplication final : public VulkanGraphApplication {
public:
    HeadlessUiApplication() {
        SetPresentationTarget(PresentationTarget::Offscreen);
    }

    void BuildRenderGraph() override {
        const bool utTextCapture = std::getenv("VIXEN_HEADLESS_UT_TEXT_CAPTURE") != nullptr;
        if (!utTextCapture) {
            BuildUIGraph();
            return;
        }

        // The production UI demo path also selects the native upscaler candidate required by
        // PrepareWithUpscalerFallback. Reuse that existing headless graph entry point so this
        // capture exercises the same renderer setup as the supported UI-only smoke route.
        SetUiDemoEnvironment(true);
        VulkanGraphApplication::BuildRenderGraph();
        SetUiDemoEnvironment(false);

        auto* uiNode = dynamic_cast<Vixen::RenderGraph::UIRenderNode*>(
            GetRenderGraph()->GetInstanceByName("ui_render"));
        if (!uiNode) {
            captureError_ = "ut-text capture could not find ui_render";
            return;
        }
        uiNode->SetTextResolver(std::make_shared<HeadlessTextResolver>());
        uiNode->SetParameter(Vixen::RenderGraph::UIRenderNodeConfig::RML_DOCUMENT_PATH,
                             "assets/ui/ut_text_capture.rml");
    }

    bool Render() override {
        if (!VulkanGraphApplication::Render()) return false;
        auto* graph = GetRenderGraph();
        if (!graph || graph->GetInstanceByName("main_window") ||
            graph->GetInstanceByName("ui_present") ||
            !graph->GetInstanceByName("ui_offscreen_target")) {
            captureError_ = "offscreen graph contains the wrong terminal nodes";
            return false;
        }
        if (std::getenv("VIXEN_HEADLESS_UT_TEXT_CAPTURE") && !utTextBound_) {
            auto* uiNode = dynamic_cast<Vixen::RenderGraph::UIRenderNode*>(
                graph->GetInstanceByName("ui_render"));
            auto* context = uiNode ? uiNode->GetUiContext() : nullptr;
            auto* document = context ? context->GetDocument(0) : nullptr;
            auto* element = document ? document->GetElementById("ut-text-line") : nullptr;
            auto* text = dynamic_cast<Vixen::Text::RmlTextElement*>(element);
            if (!text) {
                captureError_ = "ut-text capture could not bind the rendered ut-text element";
                return false;
            }
            text->SetTextRef({0u, 0u});
            utTextBound_ = true;
            // The document was already updated for this frame. Let the next frame render the
            // newly materialized spans before taking the PNG, keeping the capture a real frame.
            return true;
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
    bool utTextBound_ = false;
    std::string captureError_;
};

TEST(HeadlessUiGraph, RendersAndReadsBackAnOffscreenFrame) {
    const char* path = std::getenv("VIXEN_HEADLESS_UI_CAPTURE");
    ASSERT_NE(path, nullptr) << "CMake must set VIXEN_HEADLESS_UI_CAPTURE";
    std::error_code ec;
    std::filesystem::remove(path, ec);

    HeadlessUiApplication app;
    const int result = app.Run(RunOptions{
        .exitAfterFrames = static_cast<std::uint64_t>(
            std::getenv("VIXEN_HEADLESS_UT_TEXT_CAPTURE") ? 8 : 3)});
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
