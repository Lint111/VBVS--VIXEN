#include <gtest/gtest.h>

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>

#include "Text/RmlFontEngine.h"
#include "Text/RmlTextElement.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

class NullRenderInterface final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override { return 0; }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override {}
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override { return 0; }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return 0; }
    void ReleaseTexture(Rml::TextureHandle) override {}
};

class TestSystemInterface final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    }

private:
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

std::string RenderedText(const Rml::Element* element) {
    std::string result;
    if (const auto* text = dynamic_cast<const Rml::ElementText*>(element)) {
        for (const auto& line : text->GetLines())
            result += line.text;
    }
    for (int index = 0; index < element->GetNumChildren(); ++index)
        result += RenderedText(element->GetChild(index));
    return result;
}

class FixtureResolver final : public Vixen::Text::TextResolver {
public:
    explicit FixtureResolver(std::uint32_t color = 0x0064c8ffu) : color_(color) {
        constexpr char kText[] = "A bold blue pause big.";
        bytes.assign(kText, sizeof(kText) - 1u);
        offsets = {0u, static_cast<std::uint32_t>(bytes.size())};
        elements = {
            {Vixen::Text::TextElementKind::ConventionTag, 0u, 1u, 0u}, // emphasis
            {Vixen::Text::TextElementKind::ConventionTag, 0u, 2u, color_}, // color
            {Vixen::Text::TextElementKind::ConventionTag, 0u, 3u, 0u}, // pause
            {Vixen::Text::TextElementKind::ConventionTag, 0u, 4u, 0u}, // size
            {Vixen::Text::TextElementKind::AuthoredTag, 0u, 77u, 0u}, // profile/override hook
        };
        spans = {
            {0u, 2u, 6u},   // bold
            {1u, 7u, 11u},  // blue
            {2u, 12u, 12u}, // pause marker
            {3u, 18u, 21u}, // big
            {4u, 21u, 21u}, // authored hook marker
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
    std::uint32_t color_;
    std::string bytes;
    std::vector<std::uint32_t> offsets;
    std::vector<Vixen::Text::TextElement> elements;
    std::vector<Vixen::Text::TextSpan> spans;
};

class RmlTextElementTest : public ::testing::Test {
protected:
    void SetUp() override {
        Rml::SetSystemInterface(&system_);
        Rml::SetRenderInterface(&render_);
        Rml::SetFontEngineInterface(&font_);
        ASSERT_TRUE(Rml::Initialise());
        ASSERT_TRUE(Rml::LoadFontFace(TEXT_LATO_FONT_PATH));
        ASSERT_TRUE(Rml::LoadFontFace(TEXT_LATO_BOLD_FONT_PATH, "LatoLatin", Rml::Style::FontStyle::Normal,
                                      Rml::Style::FontWeight::Bold));
        registry_ = std::make_unique<Vixen::Text::RmlTextElementRegistry>(resolver_);
        ASSERT_TRUE(registry_->Register());
        context_ = Rml::CreateContext("ut_text", Rml::Vector2i(640, 120));
        ASSERT_NE(context_, nullptr);
    }

    void TearDown() override {
        if (context_)
            Rml::RemoveContext("ut_text");
        context_ = nullptr;
        registry_.reset();
        Rml::Shutdown();
    }

    TestSystemInterface system_;
    NullRenderInterface render_;
    Vixen::Text::RmlFontEngine font_;
    std::shared_ptr<const FixtureResolver> resolver_ = std::make_shared<FixtureResolver>();
    std::unique_ptr<Vixen::Text::RmlTextElementRegistry> registry_;
    Rml::Context* context_ = nullptr;
};

TEST_F(RmlTextElementTest, RegisteredElementResolvesClosedTagsAndAuthoredHook) {
    const Rml::String rml = R"(<rml><head><title>text</title></head><body style="font-family: LatoLatin;"><ut-text id="line" /></body></rml>)";
    Rml::ElementDocument* document = context_->LoadDocumentFromMemory(rml);
    ASSERT_NE(document, nullptr);
    auto* element = static_cast<Vixen::Text::RmlTextElement*>(document->GetElementById("line"));
    ASSERT_NE(element, nullptr);

    element->SetTextRef({0u, 0u});
    context_->Update();
    const Rml::String first = element->GetInnerRML();
    std::printf("[RmlTextElementCapture] %s\n", first.c_str());

    EXPECT_NE(first.find("A "), Rml::String::npos);
    EXPECT_NE(first.find("bold"), Rml::String::npos);
    EXPECT_NE(first.find("blue"), Rml::String::npos);
    EXPECT_NE(first.find("big"), Rml::String::npos);
    EXPECT_NE(first.find("font-weight: 700"), Rml::String::npos);
    EXPECT_NE(first.find("color: #64c8ff"), Rml::String::npos);
    EXPECT_NE(first.find("data-ut-text-pause=\"true\""), Rml::String::npos);
    EXPECT_NE(first.find("font-size: 125%"), Rml::String::npos);
    EXPECT_NE(first.find("data-ut-authored-tag=\"77\""), Rml::String::npos);
    EXPECT_EQ(element->GetAttribute<Rml::String>("data-ut-text-motion", ""), "reduced");
    EXPECT_EQ(element->GetComputedValues().white_space(), Rml::Style::WhiteSpace::Prewrap);

    Rml::ElementList emphasis;
    Rml::ElementList color;
    Rml::ElementList size;
    element->GetElementsByClassName(emphasis, "ut-text-emphasis");
    element->GetElementsByClassName(color, "ut-text-color");
    element->GetElementsByClassName(size, "ut-text-size");
    EXPECT_EQ(emphasis.size(), 1u);
    EXPECT_EQ(color.size(), 1u);
    EXPECT_EQ(size.size(), 1u);

    // Layout, rather than serialized markup, is the contract: spaces survive the styled
    // inline boundaries and the emphasis weight resolves to the registered bold face.
    EXPECT_EQ(RenderedText(element), "A bold blue pause big.");
    ASSERT_EQ(emphasis.size(), 1u);
    EXPECT_EQ(emphasis.front()->GetComputedValues().font_weight(), Rml::Style::FontWeight::Bold);
    const auto normalFace = font_.GetFontFaceHandle(
        "LatoLatin", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, 24);
    const auto boldFace = font_.GetFontFaceHandle(
        "LatoLatin", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Bold, 24);
    const auto emphasisFace = font_.GetFontFaceHandle(
        "LatoLatin", emphasis.front()->GetComputedValues().font_style(),
        emphasis.front()->GetComputedValues().font_weight(), 24);
    EXPECT_NE(normalFace, boldFace);
    EXPECT_EQ(emphasisFace, boldFace);

    // Reflow/replay is a fresh materialization, not an append. The plain row appears exactly once
    // after a second explicit rebuild and another RmlUi update.
    const std::uint32_t rebuilds = element->RebuildCount();
    ASSERT_TRUE(element->Rebuild());
    context_->Update();
    const Rml::String second = element->GetInnerRML();
    EXPECT_EQ(second.find("bold"), second.rfind("bold"));
    EXPECT_EQ(element->RebuildCount(), rebuilds + 1u);
}

TEST_F(RmlTextElementTest, ColorTagPreservesDistinctRgbByteOrder) {
    const Rml::String rml = R"(<rml><head><title>text</title></head><body style="font-family: LatoLatin;"><ut-text id="line" /></body></rml>)";
    Rml::ElementDocument* document = context_->LoadDocumentFromMemory(rml);
    ASSERT_NE(document, nullptr);
    auto* element = static_cast<Vixen::Text::RmlTextElement*>(document->GetElementById("line"));
    ASSERT_NE(element, nullptr);

    // 0x12345678 has deliberately distinct R/G/B bytes (12/34/56) and a non-zero alpha byte.
    element->SetResolver(std::make_shared<FixtureResolver>(0x12345678u));
    element->SetTextRef({0u, 0u});
    context_->Update();

    EXPECT_NE(element->GetInnerRML().find("color: #345678"), Rml::String::npos);
}

TEST_F(RmlTextElementTest, MotionFlagsAreReadableByDefaultAndDoNotStartAnimation) {
    const Rml::String rml = R"(<rml><head><title>text</title></head><body style="font-family: LatoLatin;"><ut-text id="line" /></body></rml>)";
    Rml::ElementDocument* document = context_->LoadDocumentFromMemory(rml);
    ASSERT_NE(document, nullptr);
    auto* element = static_cast<Vixen::Text::RmlTextElement*>(document->GetElementById("line"));
    ASSERT_NE(element, nullptr);

    EXPECT_FALSE(element->MotionEnabled());
    EXPECT_TRUE(element->ReducedMotion());
    element->SetTextRef({0u, 0u});
    element->SetMotionEnabled(true);
    element->SetReducedMotion(false);
    context_->Update();
    EXPECT_EQ(element->GetAttribute<Rml::String>("data-ut-text-motion", ""), "enabled");
    EXPECT_GT(element->GetNumChildren(), 0);
}

} // namespace
