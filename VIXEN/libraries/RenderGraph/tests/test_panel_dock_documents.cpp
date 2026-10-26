// R766: the dock's RmlUi side, headless. Loads the real dock chrome (dock.rml, bound to the generated
// PanelLayout View) and the editor's four panel documents (all bound to ONE EditorLayersView model),
// applies the dock's computed placements the way UIRenderNode::ApplyPlacements does, and checks that
// the chrome's tab buttons and the panels' controls land where the layout says, with no RmlUi error.
// Links RmlUi but not gaia (EditorLayersView.h is ODR-safe here, as in test_view_editor_layers_golden).
#include <gtest/gtest.h>

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>

#include "EditorLayersView.h"
#include "Text/RmlFontEngine.h"
#include "Ui/PanelDock.h"
#include "Ui/PanelLayoutView.h"

#include <map>
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

class PanelDockDocumentsTest : public ::testing::Test {
protected:
    void SetUp() override {
        Rml::SetSystemInterface(&sys_);
        Rml::SetRenderInterface(&render_);
        Rml::SetFontEngineInterface(&fonts_);
        ASSERT_TRUE(Rml::Initialise());
        Rml::LoadFontFace("assets/ui/LatoLatin-Regular.ttf");
        ctx_ = Rml::CreateContext("panel_dock_documents", Rml::Vector2i(1280, 720));
        ASSERT_NE(ctx_, nullptr);

        auto& view = static_cast<IView&>(layers_);
        const auto declare = [&](const char* id, const char* doc, DockRegion region, uint32_t stack, float size,
                                 bool closable, bool header) {
            PanelDecl decl;
            decl.id = id;
            decl.title = id;
            decl.view = &view;
            decl.document = doc;
            decl.region = region;
            decl.stack = stack;
            decl.size = size;
            decl.minSize = region == DockRegion::Bottom ? 24.0f : 200.0f;
            decl.closable = closable;
            decl.header = header;
            dock_.Declare(decl);
        };
        declare("outliner", "assets/ui/editor_outliner.rml", DockRegion::Left, 0, 340.0f, true, true);
        declare("layers", "assets/ui/editor_layers.rml", DockRegion::Left, 1, 340.0f, true, true);
        declare("inspector", "assets/ui/editor_inspector.rml", DockRegion::Right, 0, 320.0f, true, true);
        declare("status", "assets/ui/editor_status.rml", DockRegion::Bottom, 0, 28.0f, false, false);

        Rml::DataModelConstructor chrome = ctx_->CreateDataModel(chromeView_.ModelName());
        ASSERT_TRUE(static_cast<bool>(chrome));
        chromeView_.Register(chrome);
        Rml::DataModelConstructor editor = ctx_->CreateDataModel(layers_.ModelName());
        ASSERT_TRUE(static_cast<bool>(editor));
        layers_.Register(editor);
        layers_.PopulateFromDocument(0b01u,
            {{"body", "Union", true, 1.0f}, {"chip", "Subtract", false, 0.5f}},
            {{"lump_radius", "m", 9.25f, 4.0f, 12.0f}});
        layers_.PopulateStatus({"witness:boulder", "layout: open", "Editing", 2, 0});

        chromeDoc_ = Load(chromeView_.DocumentPath());
        for (const auto& decl : dock_.Declarations()) panelDocs_[decl.id] = Load(decl.document);
    }

    void TearDown() override {
        if (ctx_) Rml::RemoveContext("panel_dock_documents");
        ctx_ = nullptr;
        Rml::Shutdown();
    }

    Rml::ElementDocument* Load(const std::string& path) {
        Rml::ElementDocument* doc = ctx_->LoadDocument(path);
        EXPECT_NE(doc, nullptr) << path;
        if (doc) doc->Show();
        return doc;
    }

    // UIRenderNode::ApplyPlacements' body-box placement.
    static void Place(Rml::ElementDocument* doc, const PanelRect& rect, bool visible) {
        const auto px = [](float value) { return std::to_string(value) + "px"; };
        doc->SetProperty("left", px(rect.x));
        doc->SetProperty("top", px(rect.y));
        doc->SetProperty("width", px(rect.width));
        doc->SetProperty("height", px(rect.height));
        doc->SetProperty("right", "auto");
        doc->SetProperty("bottom", "auto");
        if (visible) doc->Show(); else doc->Hide();
    }

    DockFrame Layout() {
        const DockFrame frame = dock_.Compute(1280.0f, 720.0f);
        chromeView_.Populate(dock_, frame);
        for (const auto& panel : frame.panels) Place(panelDocs_.at(panel.panel), panel.content, panel.visible);
        ctx_->Update();
        return frame;
    }

    // data-if hides an element (display: none) rather than removing it.
    static bool Shown(Rml::ElementDocument* doc, const char* id) {
        Rml::Element* element = doc->GetElementById(id);
        return element && element->IsVisible();
    }

    Rml::Vector2f Offset(Rml::ElementDocument* doc, const char* id) {
        Rml::Element* element = doc->GetElementById(id);
        EXPECT_NE(element, nullptr) << id;
        return element ? element->GetAbsoluteOffset(Rml::BoxArea::Border) : Rml::Vector2f(-1.0f, -1.0f);
    }

    RecordingSystemInterface sys_;
    NullRenderInterface render_;
    Vixen::Text::RmlFontEngine fonts_;
    Rml::Context* ctx_ = nullptr;
    PanelDock dock_;
    PanelLayoutView chromeView_;
    Vixen::App::EditorLayersView layers_;
    Rml::ElementDocument* chromeDoc_ = nullptr;
    std::map<std::string, Rml::ElementDocument*> panelDocs_;
};

}  // namespace

TEST_F(PanelDockDocumentsTest, EditorPanelsShareOneModelAndLandInTheirRegions) {
    const DockFrame frame = Layout();
    EXPECT_TRUE(sys_.problems.empty()) << sys_.problems.front();

    // Panel documents sit where the dock placed them; their controls are inside those boxes.
    const auto layerToggle = Offset(panelDocs_.at("layers"), "layer-0-toggle");
    EXPECT_LT(layerToggle.x, frame.viewport.x);
    EXPECT_GE(layerToggle.y, (720.0f - 28.0f) / 2.0f);  // second split of the left region
    const auto parameterUp = Offset(panelDocs_.at("inspector"), "parameter-0-up");
    EXPECT_GE(parameterUp.x, frame.viewport.x + frame.viewport.width);
    EXPECT_NE(panelDocs_.at("status")->GetInnerRML().find("witness:boulder"), std::string::npos);

    // Chrome: a tab per headed panel at the computed position, closable panels get a close button,
    // the dock-to buttons skip the region the panel is already in. The status line has no tab.
    const auto inspectorTab = Offset(chromeDoc_, "panel-inspector-tab");
    EXPECT_FLOAT_EQ(inspectorTab.x, 1280.0f - 320.0f);
    EXPECT_FLOAT_EQ(inspectorTab.y, 0.0f);
    EXPECT_TRUE(Shown(chromeDoc_, "panel-inspector-close"));
    EXPECT_TRUE(Shown(chromeDoc_, "panel-inspector-to-left"));
    EXPECT_FALSE(Shown(chromeDoc_, "panel-inspector-to-right"));
    EXPECT_FALSE(Shown(chromeDoc_, "panel-status-tab"));
    EXPECT_FALSE(Shown(chromeDoc_, "panel-status-close"));
}

TEST_F(PanelDockDocumentsTest, RearrangedLayoutMovesTheDocumentsAndTheChrome) {
    Layout();
    ASSERT_EQ(dock_.Move("inspector", DockRegion::Left), DockResult::Ok);
    ASSERT_EQ(dock_.Float("outliner", {600.0f, 120.0f, 300.0f, 240.0f}), DockResult::Ok);
    const DockFrame frame = Layout();
    EXPECT_TRUE(sys_.problems.empty()) << sys_.problems.front();

    // inspector is now the active tab of the left stack it joined; layers is hidden behind it.
    const auto parameterUp = Offset(panelDocs_.at("inspector"), "parameter-0-up");
    EXPECT_LT(parameterUp.x, frame.viewport.x);
    EXPECT_FALSE(panelDocs_.at("layers")->IsVisible());
    // The floating outliner's tab is the floating frame's header.
    const auto outlinerTab = Offset(chromeDoc_, "panel-outliner-tab");
    EXPECT_FLOAT_EQ(outlinerTab.x, 600.0f);
    EXPECT_FLOAT_EQ(outlinerTab.y, 120.0f);
    EXPECT_FLOAT_EQ(frame.viewport.x + frame.viewport.width, 1280.0f);  // right region emptied
}
