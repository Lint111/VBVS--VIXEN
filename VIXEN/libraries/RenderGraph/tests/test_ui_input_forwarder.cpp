// sn-rmlinput1: keyboard, text and pointer forwarding into an Rml::Context (UiInputForwarder), headless. Covers
// the focus rule (typing is text, not chords; Escape cancels; Enter commits once; blur after an edit commits),
// pointer ownership (a UI press is a UI drag, a world press stays the world's), the committed selector, and the
// value round trip on the real inspector document (a refused value snaps back to the model's).
// Links RmlUi but not gaia (EditorLayersView.h is ODR-safe here, as in test_panel_dock_documents).
#include <gtest/gtest.h>

#define GLFW_INCLUDE_NONE   // keyboard constants only: no <GL/gl.h> on headless/WSL
#include <GLFW/glfw3.h>

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>

#include "EditorLayersView.h"
#include "Text/RmlFontEngine.h"
#include "Ui/UiInputForwarder.h"

#include "Ui/UiKit.h"

#include <functional>
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

class RecordingSystemInterface final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override { return 0.0; }
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        if (type == Rml::Log::LT_ERROR || type == Rml::Log::LT_WARNING || type == Rml::Log::LT_ASSERT)
            problems.push_back(message);
        return Rml::SystemInterface::LogMessage(type, message);
    }
    std::vector<std::string> problems;
};

using namespace Vixen::RenderGraph;

// One frame of synthetic input, the shape InputNode hands the UI.
struct Frame {
    InputState state;
    Frame& MoveTo(float x, float y) { state.mousePosition = {x, y}; return *this; }
    Frame& Press(float x, float y, int button = 0) {
        MoveTo(x, y);
        state.clicksThisFrame.push_back({button, true, x, y});
        state.mouseButtons[button] = true;
        return *this;
    }
    Frame& Release(float x, float y, int button = 0) {
        MoveTo(x, y);
        state.clicksThisFrame.push_back({button, false, x, y});
        state.mouseButtons[button] = false;
        return *this;
    }
    Frame& Hold(int button = 0) { state.mouseButtons[button] = true; return *this; }  // the button stays down
    Frame& Type(const std::u32string& text) { state.textThisFrame += text; return *this; }
    Frame& Key(int glfwKey, int action = GLFW_PRESS, int mods = 0) {
        state.keyEventsThisFrame.push_back({glfwKey, action, mods});
        return *this;
    }
};

constexpr const char* kDoc = R"(<rml><head><style>
body { font-family: LatoLatin; font-size: 15px; position: absolute; left: 0; top: 0; width: 400px; height: 300px; }
input { display: block; position: absolute; left: 10px; top: 10px; width: 120px; height: 22px; }
</style></head><body><input type="text" id="parameter-0-set-{value}" value="9.25"/></body></rml>)";

class UiInputForwarderTest : public ::testing::Test {
protected:
    void SetUp() override {
        Rml::SetSystemInterface(&sys_);
        Rml::SetRenderInterface(&render_);
        Rml::SetFontEngineInterface(&fonts_);
        ASSERT_TRUE(Rml::Initialise());
        kit_ = std::make_unique<Vixen::RenderGraph::UiKit::Registry>([](const std::string& a) { return a; });  // R874: the documents use kit elements
        kit_->Register();
        Rml::LoadFontFace("assets/ui/LatoLatin-Regular.ttf");
        ctx_ = Rml::CreateContext("ui_input_forwarder", Rml::Vector2i(1280, 720));
        ASSERT_NE(ctx_, nullptr);
        forwarder_.Attach(ctx_);
    }
    void TearDown() override {
        forwarder_.Detach();
        if (ctx_) Rml::RemoveContext("ui_input_forwarder");
        ctx_ = nullptr;
        kit_.reset();
        Rml::Shutdown();
    }

    Rml::ElementDocument* LoadInline() {
        doc_ = ctx_->LoadDocumentFromMemory(kDoc);
        EXPECT_NE(doc_, nullptr);
        if (doc_) doc_->Show();
        ctx_->Update();
        return doc_;
    }
    Rml::Element* Field() { return doc_->GetElementById("parameter-0-set-{value}"); }

    // One frame: forward, then the node's context update.
    void Run(const Frame& frame, const std::function<void()>& resync = {}) {
        forwarder_.Forward(frame.state, resync);
        ctx_->Update();
    }
    void FocusField() { Run(Frame{}.Press(20, 20)); Run(Frame{}.Release(20, 20)); }
    std::string Value() { return Field()->GetAttribute<Rml::String>("value", ""); }

    RecordingSystemInterface sys_;
    NullRenderInterface render_;
    Vixen::Text::RmlFontEngine fonts_;
    Rml::Context* ctx_ = nullptr;
    std::unique_ptr<Vixen::RenderGraph::UiKit::Registry> kit_;
    Rml::ElementDocument* doc_ = nullptr;
    UiInputForwarder forwarder_;
};

TEST(UiInputForwarderPure, SelectorFillsTheLastValueSlot) {
    EXPECT_EQ(SelectorFromCommittedId("parameter-0-set-{value}", "1.2"), "parameter-0-set-1.2");
    EXPECT_EQ(SelectorFromCommittedId("layer-2-name-{name}", "rock face"), "layer-2-name-rock face");
    EXPECT_EQ(SelectorFromCommittedId("layer-2-field-b-x-{x}", ""), "layer-2-field-b-x-");
    EXPECT_EQ(SelectorFromCommittedId("no-slot-here", "1"), "no-slot-here");
}

TEST(UiInputForwarderPure, AnIdWithAValueSlotIsAPatternNotAClickableSelector) {
    EXPECT_TRUE(HasValueSlot("parameter-0-set-{value}"));
    EXPECT_TRUE(HasValueSlot("layer-2-name-{name}"));
    EXPECT_FALSE(HasValueSlot("layer-2-toggle"));
    EXPECT_FALSE(HasValueSlot("save-button"));
    EXPECT_FALSE(HasValueSlot(""));
}

TEST(UiInputForwarderPure, GlfwKeysAndModifiersMapToRml) {
    EXPECT_EQ(RmlKeyFromGlfw(GLFW_KEY_A), Rml::Input::KI_A);
    EXPECT_EQ(RmlKeyFromGlfw(GLFW_KEY_Z), Rml::Input::KI_Z);
    EXPECT_EQ(RmlKeyFromGlfw(GLFW_KEY_5), Rml::Input::KI_5);
    EXPECT_EQ(RmlKeyFromGlfw(GLFW_KEY_ENTER), Rml::Input::KI_RETURN);
    EXPECT_EQ(RmlKeyFromGlfw(GLFW_KEY_BACKSPACE), Rml::Input::KI_BACK);
    EXPECT_EQ(RmlKeyFromGlfw(GLFW_KEY_ESCAPE), Rml::Input::KI_ESCAPE);
    EXPECT_EQ(RmlKeyFromGlfw(GLFW_KEY_LEFT), Rml::Input::KI_LEFT);
    EXPECT_EQ(RmlKeyFromGlfw(GLFW_KEY_DELETE), Rml::Input::KI_DELETE);
    EXPECT_EQ(RmlKeyFromGlfw(GLFW_KEY_F12), Rml::Input::KI_F12);
    EXPECT_EQ(RmlKeyFromGlfw(GLFW_KEY_WORLD_1), Rml::Input::KI_UNKNOWN);
    EXPECT_EQ(RmlModifiersFromGlfw(GLFW_MOD_CONTROL | GLFW_MOD_SHIFT),
              Rml::Input::KM_CTRL | Rml::Input::KM_SHIFT);
    EXPECT_EQ(RmlModifiersFromGlfw(0), 0);
}

TEST_F(UiInputForwarderTest, ClickingAFieldTakesTheKeyboardAndTextTypesIntoIt) {
    LoadInline();
    EXPECT_FALSE(forwarder_.WantsKeyboard());
    FocusField();
    EXPECT_TRUE(forwarder_.WantsKeyboard());
    // Select-all then type replaces the value; "s" is text, never a chord (the host drops chords while
    // WantsKeyboard() holds: see KeyChord.TypingSuppressesChords...).
    Run(Frame{}.Key(GLFW_KEY_A, GLFW_PRESS, GLFW_MOD_CONTROL));
    Run(Frame{}.Type(U"1.5s"));
    EXPECT_EQ(Value(), "1.5s");
    Run(Frame{}.Key(GLFW_KEY_BACKSPACE));
    EXPECT_EQ(Value(), "1.5");
    EXPECT_TRUE(forwarder_.WantsKeyboard());
    EXPECT_TRUE(forwarder_.DrainCommits().empty()) << "typing alone commits nothing";
}

TEST_F(UiInputForwarderTest, EnterCommitsOnceThenReleasesTheKeyboard) {
    LoadInline();
    FocusField();
    Run(Frame{}.Key(GLFW_KEY_A, GLFW_PRESS, GLFW_MOD_CONTROL));
    Run(Frame{}.Type(U"0.75"));
    Run(Frame{}.Key(GLFW_KEY_ENTER));
    Run(Frame{}.Key(GLFW_KEY_ENTER, GLFW_RELEASE));
    const auto commits = forwarder_.DrainCommits();
    ASSERT_EQ(commits.size(), 1u) << "Enter commits exactly once, and the blur that follows adds nothing";
    EXPECT_EQ(commits[0].elementId, "parameter-0-set-{value}");
    EXPECT_EQ(commits[0].selector, "parameter-0-set-0.75");
    EXPECT_EQ(commits[0].text, "0.75");
    EXPECT_FALSE(forwarder_.WantsKeyboard()) << "after Enter the chords are live again";
    Run(Frame{});
    EXPECT_TRUE(forwarder_.DrainCommits().empty());
}

TEST_F(UiInputForwarderTest, EscapeCancelsTheEditAndBlursWithoutACommit) {
    LoadInline();
    FocusField();
    Run(Frame{}.Key(GLFW_KEY_A, GLFW_PRESS, GLFW_MOD_CONTROL));
    Run(Frame{}.Type(U"123"));
    EXPECT_EQ(Value(), "123");
    Run(Frame{}.Key(GLFW_KEY_ESCAPE));
    EXPECT_EQ(Value(), "9.25") << "Escape restores the value the field had when it gained focus";
    EXPECT_FALSE(forwarder_.WantsKeyboard());
    EXPECT_TRUE(forwarder_.DrainCommits().empty());
}

TEST_F(UiInputForwarderTest, BlurAfterAnEditCommitsAndAnUneditedBlurDoesNot) {
    LoadInline();
    FocusField();
    Run(Frame{}.Press(300, 250));  // click empty space: the field loses focus unedited
    Run(Frame{}.Release(300, 250));
    EXPECT_TRUE(forwarder_.DrainCommits().empty());
    EXPECT_FALSE(forwarder_.WantsKeyboard());

    FocusField();
    Run(Frame{}.Key(GLFW_KEY_A, GLFW_PRESS, GLFW_MOD_CONTROL));
    Run(Frame{}.Type(U"4"));
    Run(Frame{}.Press(300, 250));
    Run(Frame{}.Release(300, 250));
    const auto commits = forwarder_.DrainCommits();
    ASSERT_EQ(commits.size(), 1u);
    EXPECT_EQ(commits[0].selector, "parameter-0-set-4");
}

TEST_F(UiInputForwarderTest, ThePointerBelongsToWhoeverPressedFirst) {
    LoadInline();
    Run(Frame{}.MoveTo(900, 600));
    EXPECT_FALSE(forwarder_.WantsPointer()) << "outside every document is the world's";
    Run(Frame{}.MoveTo(20, 20));
    EXPECT_TRUE(forwarder_.WantsPointer()) << "hovering a control is the UI's";

    // A press that starts on the control is a UI gesture even once the pointer leaves it.
    Run(Frame{}.Press(20, 20));
    Run(Frame{}.MoveTo(900, 600).Hold());
    EXPECT_TRUE(forwarder_.WantsPointer());
    Run(Frame{}.Release(900, 600));
    EXPECT_FALSE(forwarder_.WantsPointer());

    // A press that starts in the world stays the world's (a camera orbit) while crossing the control.
    Run(Frame{}.Press(900, 600, 1));
    Run(Frame{}.MoveTo(20, 20).Hold(1));
    EXPECT_FALSE(forwarder_.WantsPointer());
    Run(Frame{}.Release(20, 20, 1));
    EXPECT_TRUE(forwarder_.WantsPointer()) << "released over the control: hovering it again";
}

TEST_F(UiInputForwarderTest, WithNoFocusableElementTheInputChangesNothing) {
    // The game HUD case: a document with no text control. Keys and text are forwarded and fall on nothing.
    doc_ = ctx_->LoadDocumentFromMemory(R"(<rml><head><style>body { font-family: LatoLatin; }</style></head>
        <body><div id="hud">hud</div></body></rml>)");
    ASSERT_NE(doc_, nullptr);
    doc_->Show();
    ctx_->Update();
    Run(Frame{}.Press(5, 5).Key(GLFW_KEY_S).Type(U"s"));
    Run(Frame{}.Release(5, 5).Key(GLFW_KEY_S, GLFW_RELEASE).Key(GLFW_KEY_ESCAPE));
    EXPECT_FALSE(forwarder_.WantsKeyboard());
    EXPECT_TRUE(forwarder_.DrainCommits().empty());
}

// The real inspector document: the parameter row's field is the SetParameter selector with a value slot, and a
// value the action refuses snaps back to the model's after the resync.
TEST_F(UiInputForwarderTest, InspectorFieldCommitsASelectorAndARefusedValueSnapsBack) {
    Vixen::App::EditorLayersView view;
    Rml::DataModelConstructor constructor = ctx_->CreateDataModel(view.ModelName());
    ASSERT_TRUE(static_cast<bool>(constructor));
    view.Register(constructor);
    Rml::DataModelHandle model = constructor.GetModelHandle();
    view.PopulateFromDocument(0b01u, {{"body", "Union", true, 1.0f}}, {{"lump_radius", "m", 9.25f, 4.0f, 12.0f}});
    doc_ = ctx_->LoadDocument("assets/ui/editor_inspector.rml");
    ASSERT_NE(doc_, nullptr);
    doc_->Show();
    ctx_->Update();

    Rml::Element* field = doc_->GetElementById("parameter-0-set-{value}");
    ASSERT_NE(field, nullptr) << "the parameter row's value is a text field named by the selector pattern";
    EXPECT_EQ(field->GetAttribute<Rml::String>("value", ""), "9.25");
    const Rml::Vector2f at = field->GetAbsoluteOffset(Rml::BoxArea::Border) + Rml::Vector2f(8.0f, 6.0f);

    Run(Frame{}.Press(at.x, at.y));
    Run(Frame{}.Release(at.x, at.y));
    ASSERT_TRUE(forwarder_.WantsKeyboard());
    Run(Frame{}.Key(GLFW_KEY_A, GLFW_PRESS, GLFW_MOD_CONTROL));
    Run(Frame{}.Type(U"99"));
    Run(Frame{}.Key(GLFW_KEY_ENTER));
    const auto commits = forwarder_.DrainCommits();
    ASSERT_EQ(commits.size(), 1u);
    EXPECT_EQ(commits[0].selector, "parameter-0-set-99");
    EXPECT_EQ(field->GetAttribute<Rml::String>("value", ""), "99") << "the typed text stays until the model answers";

    // The action refused (99 is outside 4..12): the model is unchanged. The next frame's resync re-reads it.
    int resyncs = 0;
    Run(Frame{}, [&] { ++resyncs; model.DirtyAllVariables(); });
    EXPECT_EQ(resyncs, 1);
    EXPECT_EQ(field->GetAttribute<Rml::String>("value", ""), "9.25") << "refused value snapped back to the model's";
    Run(Frame{}, [&] { ++resyncs; });
    EXPECT_EQ(resyncs, 1) << "one resync per drain";

    // An accepted value: the model changes and the field shows it.
    view.PopulateFromDocument(0b01u, {{"body", "Union", true, 1.0f}}, {{"lump_radius", "m", 7.5f, 4.0f, 12.0f}});
    ctx_->Update();
    EXPECT_EQ(field->GetAttribute<Rml::String>("value", ""), "7.5");
    EXPECT_TRUE(sys_.problems.empty()) << sys_.problems.front();
}

// sn-editorpicker1: the Add flyout's document. Showing it focuses the filter field (autofocus), typing writes the View's
// filter live through the data binding (no commit needed), Enter commits the ChoosePrimitive selector with the typed text,
// and Escape restores the empty value and drops focus.
TEST_F(UiInputForwarderTest, FlyoutFilterFieldTakesFocusOnShowFiltersLiveAndEnterCommitsTheSelector) {
    Vixen::App::EditorLayersView view;
    Rml::DataModelConstructor constructor = ctx_->CreateDataModel(view.ModelName());
    ASSERT_TRUE(static_cast<bool>(constructor));
    view.Register(constructor);
    Rml::DataModelHandle model = constructor.GetModelHandle();
    view.PopulatePrimitives({{"Box", "Box", "Primitive"}, {"CappedTorus", "Capped Torus", "Primitive"}, {"Torus", "Torus", "Primitive"}});
    doc_ = ctx_->LoadDocument("assets/ui/editor_picker.rml");
    ASSERT_NE(doc_, nullptr);
    doc_->Hide();
    ctx_->Update();
    EXPECT_FALSE(forwarder_.WantsKeyboard()) << "a hidden flyout holds no focus";
    doc_->Show();   // what UIRenderNode::ApplyPlacements does when the dock opens the panel
    ctx_->Update();
    ASSERT_TRUE(forwarder_.WantsKeyboard()) << "opening the flyout focuses its filter field";

    Rml::Element* field = doc_->GetElementById("picker-filter-{filter}");
    ASSERT_NE(field, nullptr);
    Run(Frame{}.Type(U"tor"));
    EXPECT_EQ(view.PickerFilter(), "tor") << "typing filters live: the field writes the View's filter as it changes";
    Run(Frame{}.Key(GLFW_KEY_ENTER));
    const auto commits = forwarder_.DrainCommits();
    ASSERT_EQ(commits.size(), 1u);
    EXPECT_EQ(commits[0].selector, "picker-filter-tor");
    EXPECT_FALSE(forwarder_.WantsKeyboard()) << "Enter commits and releases the keyboard";

    // The editor clears the filter when the flyout closes; the field shows it.
    view.ClearPickerFilter();
    ctx_->Update();
    EXPECT_EQ(field->GetAttribute<Rml::String>("value", ""), "");

    // Escape: the typed text is put back (empty) and the field blurs.
    doc_->Hide();
    doc_->Show();
    ctx_->Update();
    ASSERT_TRUE(forwarder_.WantsKeyboard());
    Run(Frame{}.Type(U"to"));
    EXPECT_EQ(view.PickerFilter(), "to");
    Run(Frame{}.Key(GLFW_KEY_ESCAPE));
    EXPECT_FALSE(forwarder_.WantsKeyboard());
    EXPECT_EQ(field->GetAttribute<Rml::String>("value", ""), "") << "Escape puts the field's value back";
    // (RmlUi writes the binding on a user change, not on this programmatic restore; the editor closes the flyout on
    // Escape and clears the filter then, so no stale filter survives.)
    EXPECT_TRUE(forwarder_.DrainCommits().empty()) << "a cancelled edit commits nothing";
    EXPECT_TRUE(sys_.problems.empty()) << sys_.problems.front();
}

// sn-editordock2: the generic cancel. Escape reports once so a host can close a dismissible panel. A focused field
// with uncommitted edits takes the key first (revert + blur, not a cancel); an unchanged field has nothing to revert,
// so the same press is also the cancel.
TEST_F(UiInputForwarderTest, EscapeIsAGenericCancelUnlessAFieldHadEditsToRevert) {
    LoadInline();
    EXPECT_FALSE(forwarder_.DrainCancel());
    FocusField();
    Run(Frame{}.Key(GLFW_KEY_A, GLFW_PRESS, GLFW_MOD_CONTROL));
    Run(Frame{}.Type(U"123"));
    Run(Frame{}.Key(GLFW_KEY_ESCAPE));
    EXPECT_EQ(Value(), "9.25");
    EXPECT_FALSE(forwarder_.WantsKeyboard()) << "the first Escape reverted and blurred the edited field";
    EXPECT_FALSE(forwarder_.DrainCancel()) << "the field consumed it: that was its revert, not a cancel";
    Run(Frame{}.Key(GLFW_KEY_ESCAPE, GLFW_RELEASE));
    Run(Frame{}.Key(GLFW_KEY_ESCAPE));
    EXPECT_TRUE(forwarder_.DrainCancel()) << "the second Escape had no field to consume it";
    EXPECT_FALSE(forwarder_.DrainCancel()) << "draining clears it";
    Run(Frame{}.Key(GLFW_KEY_ESCAPE, GLFW_RELEASE));

    // A focused field nobody edited: one Escape both blurs it and cancels.
    FocusField();
    EXPECT_TRUE(forwarder_.WantsKeyboard());
    Run(Frame{}.Key(GLFW_KEY_ESCAPE));
    EXPECT_FALSE(forwarder_.WantsKeyboard());
    EXPECT_TRUE(forwarder_.DrainCancel()) << "unchanged field: the first Escape is the cancel";
    EXPECT_TRUE(forwarder_.DrainCommits().empty());
    Run(Frame{}.Key(GLFW_KEY_ESCAPE, GLFW_RELEASE));

    // Escape pressed and released inside one frame (a tap) still counts: the press edge is in the frame's events.
    Frame tap;
    tap.Key(GLFW_KEY_ESCAPE).Key(GLFW_KEY_ESCAPE, GLFW_RELEASE);
    Run(tap);
    EXPECT_TRUE(forwarder_.DrainCancel());
}

}  // namespace

// R874: a nudge click on the kit number field reaches the host as the same typed commit a typed value does: one
// selector, filled with the element's stepped and clamped value.
TEST_F(UiInputForwarderTest, KitNudgeAndScrubCommitTheSameSelectorPathAsTyping) {
    Vixen::App::EditorLayersView view;
    Rml::DataModelConstructor constructor = ctx_->CreateDataModel(view.ModelName());
    ASSERT_TRUE(static_cast<bool>(constructor));
    view.Register(constructor);
    view.PopulateFromDocument(0b01u, {{"body", "Union", true, 1.0f}}, {{"lump_radius", "m", 9.25f, 4.0f, 12.0f}});
    doc_ = ctx_->LoadDocument("assets/ui/editor_inspector.rml");
    ASSERT_NE(doc_, nullptr);
    doc_->Show();
    ctx_->Update();
    Rml::ElementList fields;
    doc_->GetElementsByTagName(fields, "ut-number-field");
    ASSERT_FALSE(fields.empty());
    const auto part = [&](const char* name) -> Rml::Element* {
        std::function<Rml::Element*(Rml::Element*)> find = [&](Rml::Element* e) -> Rml::Element* {
            for (int i = 0; i < e->GetNumChildren(true); ++i) {
                Rml::Element* c = e->GetChild(i);
                if (c->GetAttribute<Rml::String>("part", "") == name) return c;
                if (Rml::Element* d = find(c)) return d;
            }
            return nullptr;
        };
        return find(fields[0]);
    };
    Rml::Element* up = part("up");
    ASSERT_NE(up, nullptr);
    const Rml::Vector2f at = up->GetAbsoluteOffset(Rml::BoxArea::Border) + Rml::Vector2f(3.0f, 3.0f);
    Run(Frame{}.Press(at.x, at.y));
    Run(Frame{}.Release(at.x, at.y));
    auto commits = forwarder_.DrainCommits();
    ASSERT_EQ(commits.size(), 1u);
    EXPECT_EQ(commits[0].selector, "parameter-0-set-10.25") << "9.25 + an eighth of the 4..12 range";

    // Scrub: press the label, drag 100 px right (half a range), release: ONE commit with the scrubbed value.
    Rml::Element* label = part("label");
    ASSERT_NE(label, nullptr);
    const Rml::Vector2f from = label->GetAbsoluteOffset(Rml::BoxArea::Border) + Rml::Vector2f(3.0f, 3.0f);
    Run(Frame{}.Press(from.x, from.y));
    Run(Frame{}.MoveTo(from.x + 60.0f, from.y).Hold());
    Run(Frame{}.MoveTo(from.x + 100.0f, from.y).Hold());
    EXPECT_TRUE(forwarder_.DrainCommits().empty()) << "dragging previews; nothing commits until release";
    Run(Frame{}.Release(from.x + 100.0f, from.y));
    commits = forwarder_.DrainCommits();
    ASSERT_EQ(commits.size(), 1u);
    EXPECT_EQ(commits[0].selector, "parameter-0-set-12") << "9.25 + 100 px of a 200 px range, clamped to 12";
}

