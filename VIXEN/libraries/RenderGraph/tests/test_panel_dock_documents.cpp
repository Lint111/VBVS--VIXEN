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
#include "Recipe/generated/SdfOpCodes.g.h"

#include "Text/RmlFontEngine.h"
#include "Ui/PanelDock.h"
#include "Ui/PanelLayoutView.h"

#include <algorithm>
#include <cstdint>
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
        declare("picker", "assets/ui/editor_picker.rml", DockRegion::Floating, 0, 300.0f, true, true);
        ASSERT_EQ(dock_.Close("picker"), DockResult::Ok);   // the flyout starts closed

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
    // R765 typed parameters: each row's value label (the ScrubParameter selector) and reset button resolve to
    // elements, with the row index spliced into the id by the data expression.
    EXPECT_TRUE(panelDocs_.at("inspector")->GetElementById("parameter-0-value") != nullptr);
    EXPECT_TRUE(panelDocs_.at("inspector")->GetElementById("parameter-0-reset") != nullptr);
    EXPECT_FALSE(panelDocs_.at("inspector")->GetElementById("parameter-0-value")->GetAttribute("hit-mask", Rml::String("x")) == Rml::String("x"));
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

// R872.1: the layers panel keeps its content width when the primitive picker is populated and the rows outgrow
// the panel. RmlUi gives an unstyled vertical scrollbar the whole width, which collapsed the first Dozen run's
// panel to 20 px once a fourth layer was added (editor.rcss now sizes the scrollbar).
TEST_F(PanelDockDocumentsTest, LayersPanelKeepsItsWidthWithThePickerAndManyRows) {
    std::vector<Vixen::App::EditorPrimitiveData> primitives;
    for (const char* name : {"Box", "BoxRounded", "CappedTorus", "Capsule", "Cone", "Cylinder", "Sphere", "Torus"}) {
        primitives.push_back({name, name, "Primitive"});
    }
    layers_.PopulatePrimitives(primitives);
    Rml::ElementDocument* doc = panelDocs_.at("layers");
    for (const int count : {2, 3, 4, 6, 12}) {
        std::vector<Vixen::App::EditorLayerData> rows;
        for (int i = 0; i < count; ++i) rows.push_back({"layer" + std::to_string(i), "Union", true, 0.0f});
        layers_.PopulateFromDocument((1u << count) - 1u, rows, {});
        Layout();
        EXPECT_TRUE(sys_.problems.empty()) << sys_.problems.front();
        EXPECT_GT(doc->GetClientWidth(), 300.0f) << count << " rows: the layers document's content width collapsed";
        for (const char* id : {"panel-picker-open", "undo-button"}) {
            Rml::Element* element = doc->GetElementById(id);
            ASSERT_NE(element, nullptr) << id;
            EXPECT_GT(element->GetBox().GetSize(Rml::BoxArea::Border).x, 40.0f) << count << " rows: " << id;
        }
    }
}

// sn-editorpicker1: the Layers toolbar (Add, Undo, Redo, Save, Reopen saved) stays inside the panel and pinned above the
// rows however many layers there are, at the default size and at the panel's minimum width; the rows scroll within the panel.
TEST_F(PanelDockDocumentsTest, LayersToolbarStaysPinnedInsideThePanelWithEightLayers) {
    std::vector<Vixen::App::EditorLayerData> rows;
    for (int i = 0; i < 8; ++i) rows.push_back({"layer" + std::to_string(i), "Union", true, 0.5f});
    layers_.PopulateFromDocument(0xFFu, rows, {});
    Rml::ElementDocument* doc = panelDocs_.at("layers");
    for (const float width : {340.0f, 220.0f}) {
        ASSERT_EQ(dock_.Resize(DockRegion::Left, width), DockResult::Ok);
        const DockFrame frame = Layout();
        EXPECT_TRUE(sys_.problems.empty()) << sys_.problems.front();
        PanelRect content{};
        for (const auto& panel : frame.panels) if (panel.panel == "layers") content = panel.content;
        ASSERT_GT(content.width, 0.0f);
        const Rml::Vector2f docOffset = doc->GetAbsoluteOffset(Rml::BoxArea::Border);
        EXPECT_FLOAT_EQ(docOffset.x, content.x);
        EXPECT_FLOAT_EQ(docOffset.y, content.y);

        // Every toolbar button lies inside the panel's box, in the first rows of it.
        float lowest = 0.0f;
        for (const char* id : {"panel-picker-open", "undo-button", "redo-button", "save-button", "reopen-button"}) {
            Rml::Element* button = doc->GetElementById(id);
            ASSERT_NE(button, nullptr) << id;
            const Rml::Vector2f at = button->GetAbsoluteOffset(Rml::BoxArea::Border);
            const Rml::Vector2f size = button->GetBox().GetSize(Rml::BoxArea::Border);
            EXPECT_GE(at.x, content.x) << id << " at width " << width;
            EXPECT_LE(at.x + size.x, content.x + content.width + 0.5f) << id << " at width " << width;
            EXPECT_GE(at.y, content.y) << id << " at width " << width;
            EXPECT_LE(at.y + size.y, content.y + content.height) << id << " at width " << width;
            lowest = std::max(lowest, at.y + size.y);
        }
        // The rows are below the toolbar, inside the panel, and scroll (8 two-line rows outgrow a ~346 px panel).
        Rml::Element* rowsBox = doc->QuerySelector(".layer-rows");
        ASSERT_NE(rowsBox, nullptr);
        const Rml::Vector2f rowsAt = rowsBox->GetAbsoluteOffset(Rml::BoxArea::Border);
        EXPECT_GE(rowsAt.y, lowest) << "the rows start below the toolbar at width " << width;
        EXPECT_LE(rowsAt.y + rowsBox->GetClientHeight(), content.y + content.height + 0.5f) << "rows end inside the panel at width " << width;
        EXPECT_GT(rowsBox->GetScrollHeight(), rowsBox->GetClientHeight()) << "8 layers overflow the rows box, which scrolls";
        EXPECT_GT(rowsBox->GetClientWidth(), 100.0f) << "the scrollbar leaves the rows their width";
        // The document itself does not scroll: nothing hides the toolbar by moving it.
        EXPECT_FLOAT_EQ(doc->GetScrollTop(), 0.0f);
        EXPECT_LE(doc->GetScrollHeight(), doc->GetClientHeight() + 1.0f);
    }
}

// sn-editorpicker1: the flyout is a declared Floating panel, closed until OpenPanel; it lists one row per published
// primitive (the display text; the id keeps the exact opcode name) above a filter field that takes focus.
TEST_F(PanelDockDocumentsTest, FlyoutPanelOpensFloatingAndListsOneRowPerPublishedPrimitive) {
    std::vector<Vixen::App::EditorPrimitiveData> all;
    for (int i = 0; i < 22; ++i) {
        all.push_back({"Prim" + std::to_string(i) + "Thing", "Prim " + std::to_string(i) + " Thing", "Primitive"});
    }
    layers_.PopulatePrimitives(all);
    DockFrame frame = Layout();
    EXPECT_FALSE(panelDocs_.at("picker")->IsVisible()) << "closed by default";
    ASSERT_EQ(dock_.Open("picker"), DockResult::Ok);
    frame = Layout();
    EXPECT_TRUE(sys_.problems.empty()) << sys_.problems.front();
    Rml::ElementDocument* doc = panelDocs_.at("picker");
    EXPECT_TRUE(doc->IsVisible());
    PanelRect content{};
    for (const auto& panel : frame.panels) if (panel.panel == "picker") content = panel.content;
    EXPECT_GT(content.width, 0.0f);
    EXPECT_FLOAT_EQ(doc->GetAbsoluteOffset(Rml::BoxArea::Border).x, content.x);

    Rml::ElementList rowsFound;
    doc->GetElementsByClassName(rowsFound, "picker-row");
    EXPECT_EQ(rowsFound.size(), all.size());
    EXPECT_EQ(rowsFound.front()->GetId(), "primitive-Prim0Thing-add") << "the id keeps the exact opcode name";
    EXPECT_NE(rowsFound.front()->GetInnerRML().find("Prim 0 Thing"), std::string::npos) << "the text is the display name";
    Rml::Element* filter = doc->GetElementById("picker-filter-{filter}");
    ASSERT_NE(filter, nullptr);
    EXPECT_EQ(ctx_->GetFocusElement(), filter) << "opening the flyout focuses the filter field";

    // A narrower list is just fewer rows; an empty list says so.
    layers_.PopulatePrimitives({all[3], all[4]});
    ctx_->Update();
    rowsFound.clear();
    doc->GetElementsByClassName(rowsFound, "picker-row");
    EXPECT_EQ(rowsFound.size(), 2u);
    layers_.DebugSetPickerFilter("zzz");
    layers_.PopulatePrimitives({});
    ctx_->Update();
    rowsFound.clear();
    doc->GetElementsByClassName(rowsFound, "picker-row");
    EXPECT_TRUE(rowsFound.empty());
    EXPECT_NE(doc->GetInnerRML().find("No primitive matches"), std::string::npos);
}

// sn-editorpicker1: the inspector's primitive fields show the catalogue label (the label text the table carries,
// plus the component for a vector), the declared range inline, and a reset button only while the value differs
// from the declared default.
TEST_F(PanelDockDocumentsTest, InspectorShapeFieldsShowCatalogueLabelsRangesAndAResetButton) {
    const Vixen::SVO::Recipe::SdfOpArgument* halfExtents = nullptr;
    for (const auto& argument : Vixen::SVO::Recipe::SdfOpArgumentTable) {
        if (argument.opcode == static_cast<std::uint8_t>(Vixen::SVO::Recipe::SdfOpCode::Box) &&
            std::string(argument.name) == "b") halfExtents = &argument;
    }
    ASSERT_NE(halfExtents, nullptr);
    Vixen::App::EditorShapeData shape;
    shape.primitive = "Box";
    shape.opChoices = {{"Union", true}};
    shape.fields = {{"b.x", std::string(halfExtents->label) + " x", 2.0f, true, 0.0f, 100.0f, halfExtents->defaultValue[0], false},
                    {"b.y", std::string(halfExtents->label) + " y", 0.5f, true, 0.0f, 100.0f, halfExtents->defaultValue[1], true}};
    layers_.PopulateFromDocument(0b01u, {{"body", "Union", true, 0.0f}}, {}, {}, shape);
    Layout();
    EXPECT_TRUE(sys_.problems.empty()) << sys_.problems.front();
    Rml::ElementDocument* doc = panelDocs_.at("inspector");
    const std::string text = doc->GetInnerRML();
    EXPECT_NE(text.find("half extents x"), std::string::npos) << "the catalogue's label for Box.b, not the argument name";
    EXPECT_NE(text.find("half extents y"), std::string::npos);
    EXPECT_EQ(text.find(">b x<"), std::string::npos) << "no raw argument name as a label";
    EXPECT_NE(text.find("0..100"), std::string::npos) << "the declared range, inline";
    EXPECT_NE(doc->GetElementById("layer-0-field-b-x-0.5"), nullptr) << "reset: the typed-value selector with the default filled in";
    EXPECT_EQ(doc->GetElementById("layer-0-field-b-y-0.5"), nullptr) << "no reset while the value is the default";
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
