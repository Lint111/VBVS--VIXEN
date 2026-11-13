// R874: the UI kit, headless. The pure number rule, the override contract, and the real kit fragments instanced into
// an Rml context (structure from the fragment, behaviour from the element: nudge/reset/range/scrub/refusal).
#include <gtest/gtest.h>

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>

#include "Text/RmlFontEngine.h"
#include "Ui/UiHitMask.h"
#include "Ui/UiKit.h"

#include <string>
#include <vector>

using namespace Vixen::RenderGraph;

TEST(UiKitRule, NudgeStepsAndClampsInOnePlace) {
    EXPECT_FLOAT_EQ(UiKit::DefaultStep(true, 0.0f, 4.0f), 0.5f) << "an eighth of the range";
    EXPECT_FLOAT_EQ(UiKit::DefaultStep(false, 0.0f, 0.0f), 0.25f);
    EXPECT_FLOAT_EQ(UiKit::Nudge(1.0f, +1, 0.25f, 1.0f, false, 0, 0), 1.25f);
    EXPECT_FLOAT_EQ(UiKit::Nudge(3.9f, +1, 0.5f, 1.0f, true, 0.0f, 4.0f), 4.0f) << "clamped to the range";
    EXPECT_FLOAT_EQ(UiKit::Nudge(0.1f, -1, 0.5f, 1.0f, true, 0.0f, 4.0f), 0.0f);
    EXPECT_FLOAT_EQ(UiKit::Nudge(1.0f, +1, 0.25f, 10.0f, false, 0, 0), 3.5f) << "the multiplier scales the step";
}

TEST(UiKitRule, ParseFormatRangeScrub) {
    float v = 0;
    EXPECT_TRUE(UiKit::ParseNumber("-1.5", v)); EXPECT_FLOAT_EQ(v, -1.5f);
    EXPECT_FALSE(UiKit::ParseNumber("abc", v));
    EXPECT_FALSE(UiKit::ParseNumber("", v));
    EXPECT_FALSE(UiKit::ParseNumber("inf", v));
    EXPECT_EQ(UiKit::FormatNumber(0.25f), "0.25");
    EXPECT_EQ(UiKit::FormatNumber(3.0f), "3");
    EXPECT_EQ(UiKit::FormatRange(true, 0.0f, 1.0f), "range 0\xE2\x80\x93" "1");
    EXPECT_EQ(UiKit::FormatRange(false, 0.0f, 1.0f), "");
    EXPECT_FLOAT_EQ(UiKit::ScrubValue(1.0f, 100.0f, 0.5f, true, 0.0f, 4.0f), 3.0f) << "half a range per 100 px";
    EXPECT_FLOAT_EQ(UiKit::ScrubValue(1.0f, 1000.0f, 0.5f, true, 0.0f, 4.0f), 4.0f);
}

TEST(UiKitContract, InstancesMayOnlyCarryTheOverrideContract) {
    EXPECT_TRUE(UiKit::AttributeAllowed("ut-number-field", "value-id"));
    EXPECT_TRUE(UiKit::AttributeAllowed("ut-number-field", "data-attr-value"));
    EXPECT_TRUE(UiKit::AttributeAllowed("ut-number-field", "data-if"));
    EXPECT_TRUE(UiKit::AttributeAllowed("ut-number-field", "id"));
    EXPECT_FALSE(UiKit::AttributeAllowed("ut-number-field", "hit-mask")) << "an instance cannot skip the hit rules";
    EXPECT_FALSE(UiKit::AttributeAllowed("ut-number-field", "data-attr-onclick"));
    EXPECT_FALSE(UiKit::AttributeAllowed("ut-button", "value"));
    EXPECT_TRUE(UiKit::AttributeAllowed("div", "anything")) << "non-kit tags are not the kit's business";
    EXPECT_EQ(UiKit::RefusedAttributes("ut-button", {"label", "bogus", "id", "hit-mask"}),
              (std::vector<std::string>{"bogus", "hit-mask"}));
    EXPECT_EQ(UiKit::Tags().size(), 10u);
}

namespace {

class NullRender final : public Rml::RenderInterface {
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

class RecordingSystem final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override { return 0.0; }
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        if (type == Rml::Log::LT_ERROR) errors.push_back(message);
        return true;
    }
    std::vector<std::string> errors;
};

struct CommitRecorder final : Rml::EventListener {
    struct Commit { std::string id, text; };
    std::vector<Commit> commits;
    void ProcessEvent(Rml::Event& e) override {
        commits.push_back({e.GetParameter<Rml::String>("id", ""), e.GetParameter<Rml::String>("text", "")});
    }
};

constexpr const char* kDoc = R"(<rml><head><style>
body { font-family: LatoLatin; font-size: 15px; position: absolute; left: 0; top: 0; width: 600px; height: 400px; }
ut-number-field { display: block; }
</style></head><body>
<ut-number-field id="nf" label="radius" unit="m" value="1" value-id="p-set-{value}" reset-id="p-reset" default="2" min="0" max="4"/>
<ut-button id="b" label="Add" variant="round"/>
<ut-toggle id="t" checked="true"/>
<ut-status-line id="s" doc="boulder.vxd" state="Editing" undo="1" redo="0" message=""/>
</body></rml>)";

class UiKitTest : public ::testing::Test {
protected:
    void SetUp() override {
        Rml::SetSystemInterface(&sys_);
        Rml::SetRenderInterface(&render_);
        Rml::SetFontEngineInterface(&fonts_);
        ASSERT_TRUE(Rml::Initialise());
        Rml::LoadFontFace("assets/ui/LatoLatin-Regular.ttf");
        kit_ = std::make_unique<UiKit::Registry>([](const std::string& a) { return a; });
        kit_->Register();
        ctx_ = Rml::CreateContext("ui_kit", Rml::Vector2i(800, 600));
        ASSERT_NE(ctx_, nullptr);
        ctx_->AddEventListener("ut-commit", &recorder_, true);
    }
    void TearDown() override {
        ctx_->RemoveEventListener("ut-commit", &recorder_, true);
        Rml::RemoveContext("ui_kit");
        kit_.reset();
        Rml::Shutdown();
    }
    Rml::ElementDocument* Load(const char* rml) {
        doc_ = ctx_->LoadDocumentFromMemory(rml);
        if (doc_) doc_->Show();
        ctx_->Update();
        return doc_;
    }
    Rml::Element* Part(Rml::Element* root, const std::string& part) {
        for (int i = 0; i < root->GetNumChildren(true); ++i) {
            Rml::Element* c = root->GetChild(i);
            if (c->GetAttribute<Rml::String>("part", "") == part) return c;
            if (auto* d = Part(c, part)) return d;
        }
        return nullptr;
    }
    void Click(Rml::Element* e) { e->DispatchEvent("click", {}); }

    RecordingSystem sys_;
    NullRender render_;
    Vixen::Text::RmlFontEngine fonts_;
    Rml::Context* ctx_ = nullptr;
    Rml::ElementDocument* doc_ = nullptr;
    std::unique_ptr<UiKit::Registry> kit_;
    CommitRecorder recorder_;
};

}  // namespace

TEST_F(UiKitTest, FragmentsBuildTheStructureAndAttributesFillIt) {
    ASSERT_NE(Load(kDoc), nullptr);
    EXPECT_TRUE(sys_.errors.empty()) << (sys_.errors.empty() ? "" : sys_.errors[0]);
    auto* nf = doc_->GetElementById("nf");
    ASSERT_NE(nf, nullptr);
    for (const char* part : {"label", "input", "down", "up", "reset", "range"}) EXPECT_NE(Part(nf, part), nullptr) << part;
    EXPECT_EQ(Part(nf, "input")->GetId(), "p-set-{value}") << "the typed field's id is the value-slot selector";
    EXPECT_EQ(Part(nf, "input")->GetAttribute<Rml::String>("value", ""), "1");
    EXPECT_NE(Part(nf, "label")->GetInnerRML().find("radius (m)"), std::string::npos);
    EXPECT_NE(Part(nf, "range")->GetInnerRML().find("range 0"), std::string::npos) << "the one range format";
    EXPECT_EQ(Part(nf, "reset")->GetId(), "p-reset");
    EXPECT_NE(Part(nf, "reset")->GetInnerRML().find("reset 2"), std::string::npos);
    EXPECT_TRUE(doc_->GetElementById("t")->IsClassSet("checked"));
    auto* status = doc_->GetElementById("s");
    EXPECT_NE(Part(status, "history")->GetInnerRML().find("undo 1 / redo 0"), std::string::npos);
    EXPECT_EQ(Part(status, "message")->GetProperty("display")->ToString(), "none") << "no message: no separator, no text";
}

TEST_F(UiKitTest, ButtonVariantRoundForcesTheEllipseHitMask) {
    Load(kDoc);
    auto* b = doc_->GetElementById("b");
    EXPECT_TRUE(b->IsClassSet("round"));
    EXPECT_EQ(b->GetAttribute<Rml::String>("hit-mask", ""), "ellipse");
    EXPECT_EQ(ParseHitMask(b->GetAttribute<Rml::String>("hit-mask", "")).shape, HitMaskShape::Ellipse);
    EXPECT_EQ(doc_->GetElementById("t")->GetAttribute<Rml::String>("hit-mask", ""), "none");
}

TEST_F(UiKitTest, NudgeCommitsThroughTheValueSlotWithTheOneRule) {
    Load(kDoc);
    auto* nf = doc_->GetElementById("nf");
    Click(Part(nf, "up"));
    ASSERT_EQ(recorder_.commits.size(), 1u);
    EXPECT_EQ(recorder_.commits[0].id, "p-set-{value}");
    EXPECT_EQ(recorder_.commits[0].text, "1.5") << "value 1 + an eighth of the 0..4 range";
    Click(Part(nf, "down"));
    EXPECT_EQ(recorder_.commits[1].text, "0.5");
    nf->SetAttribute("value", "4");
    ctx_->Update();
    Click(Part(nf, "up"));
    EXPECT_EQ(recorder_.commits.size(), 2u) << "clamped at the maximum: nothing to commit";
}

TEST_F(UiKitTest, ResetIsHiddenAtTheDefaultAndShownOtherwise) {
    Load(kDoc);
    auto* nf = doc_->GetElementById("nf");
    auto* reset = Part(nf, "reset");
    EXPECT_NE(reset->GetProperty("display")->ToString(), "none");
    nf->SetAttribute("value", "2");
    ctx_->Update();
    EXPECT_EQ(reset->GetProperty("display")->ToString(), "none");
}

TEST_F(UiKitTest, TypedTextIsMirroredSoARefusedValueCanSnapBack) {
    Load(kDoc);
    auto* nf = doc_->GetElementById("nf");
    Part(nf, "input")->SetAttribute("value", "-5");
    Part(nf, "input")->DispatchEvent("change", {});
    EXPECT_EQ(nf->GetAttribute<Rml::String>("value", ""), "-5");
    nf->SetAttribute("value", "1");  // what the data binding does after the model refused the value
    ctx_->Update();
    EXPECT_EQ(Part(nf, "input")->GetAttribute<Rml::String>("value", ""), "1");
}

TEST_F(UiKitTest, AnAttributeOutsideTheContractIsRefusedWithALocatedDiagnostic) {
    ASSERT_NE(Load(R"(<rml><body>
<ut-button label="ok"/>
<ut-button label="x" bogus="1"/>
</body></rml>)"), nullptr);
    ASSERT_FALSE(sys_.errors.empty());
    EXPECT_NE(sys_.errors[0].find("attribute 'bogus' is not part of the kit override contract"), std::string::npos) << sys_.errors[0];
}
