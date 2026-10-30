#include <gtest/gtest.h>

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
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

class FixtureResolver final : public Vixen::Text::TextResolver {
public:
    FixtureResolver() {
        constexpr char kText[] = "A bold blue pause big.";
        bytes.assign(kText, sizeof(kText) - 1u);
        offsets = {0u, static_cast<std::uint32_t>(bytes.size())};
        elements = {
            {Vixen::Text::TextElementKind::ConventionTag, 0u, 1u, 0u}, // emphasis
            {Vixen::Text::TextElementKind::ConventionTag, 0u, 2u, 0x0064c8ffu}, // color
            {Vixen::Text::TextElementKind::ConventionTag, 0u, 3u, 0u}, // pause
            {Vixen::Text::TextElementKind::ConventionTag, 0u, 4u, 0u}, // size
            {Vixen::Text::TextElementKind::AuthoredTag, 0u, 77u, 0u}, // profile/override hook
        };
        spans = {
            {0u, 2u, 6u},   // bold
            {1u, 7u, 11u},  // blue
            {2u, 12u, 12u}, // pause marker
            {3u, 13u, 17u}, // big
            {4u, 18u, 18u}, // authored hook marker
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

class RmlTextElementTest : public ::testing::Test {
protected:
    void SetUp() override {
        Rml::SetSystemInterface(&system_);
        Rml::SetRenderInterface(&render_);
        Rml::SetFontEngineInterface(&font_);
        ASSERT_TRUE(Rml::Initialise());
        ASSERT_TRUE(Rml::LoadFontFace(TEXT_LATO_FONT_PATH));
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
    EXPECT_NE(first.find("color: #ffc864"), Rml::String::npos);
    EXPECT_NE(first.find("data-ut-text-pause=\"true\""), Rml::String::npos);
    EXPECT_NE(first.find("font-size: 125%"), Rml::String::npos);
    EXPECT_NE(first.find("data-ut-authored-tag=\"77\""), Rml::String::npos);
    EXPECT_EQ(element->GetAttribute<Rml::String>("data-ut-text-motion", ""), "reduced");

    Rml::ElementList emphasis;
    Rml::ElementList color;
    Rml::ElementList size;
    element->GetElementsByClassName(emphasis, "ut-text-emphasis");
    element->GetElementsByClassName(color, "ut-text-color");
    element->GetElementsByClassName(size, "ut-text-size");
    EXPECT_EQ(emphasis.size(), 1u);
    EXPECT_EQ(color.size(), 1u);
    EXPECT_EQ(size.size(), 1u);

    // Reflow/replay is a fresh materialization, not an append. The plain row appears exactly once
    // after a second explicit rebuild and another RmlUi update.
    const std::uint32_t rebuilds = element->RebuildCount();
    ASSERT_TRUE(element->Rebuild());
    context_->Update();
    const Rml::String second = element->GetInnerRML();
    EXPECT_EQ(second.find("bold"), second.rfind("bold"));
    EXPECT_EQ(element->RebuildCount(), rebuilds + 1u);
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
