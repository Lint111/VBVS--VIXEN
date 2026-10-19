/**
 * @file test_view_editor_layers_golden.cpp
 * @brief Inc-A2 (View-Model-Binding-Inc-A2-Plan-2026-07.md) gate. Mirrors test_view_hud_golden.cpp
 * for the editor layer view: proves the generated Generated/EditorLayers.g.h RmlUi data-model
 * registration block compiles and binds against a real Rml::DataModelConstructor with no RmlUi
 * error. That the header matches the EditorLayers [View] schema (codegen/view-schemas/
 * EditorLayers.cs) is NOT asserted here: the view_editor_layers_check codegen target regenerates
 * the header from the schema and fails the build on any difference.
 */
#include <gtest/gtest.h>
#include <RmlUi/Core.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>

#include <bit>
#include <string>
#include <vector>

#include "Generated/EditorLayers.g.h"   // the artifact under test
#include "Text/RmlFontEngine.h"

// Inc-Ovr Override proof: this TU only sees the forward-declared
// Bind{V}Model_{Field}Override hook (Generated/EditorLayers.g.h emits no body for it, by design
// §5b) -- it must supply its OWN definition to link, independent of EditorLayersView.h's real
// implementation. A trivial popcount stand-in is enough; this test isn't proving the real
// editor's behavior, only that the generated call site resolves against SOME definition.
namespace Vixen::Views {
int BindEditorLayersModel_activeLayerCountOverride(int* maskAsInt) {
    return std::popcount(static_cast<unsigned>(*maskAsInt));
}
}  // namespace Vixen::Views

namespace {

// Minimal null render interface (no GPU needed) — mirrors test_view_hud_golden.cpp's fixture.
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

// Records RmlUi errors/warnings (RmlUi logs a failed Bind or an unregistered type through the
// SystemInterface and keeps going, so the log is where a broken registration shows up).
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

}  // namespace

TEST(ViewEditorLayersGolden, GeneratedBindFunctionCompilesAndBinds) {
    RecordingSystemInterface sysIface;
    NullRenderInterface renderIface;
    Vixen::Text::RmlFontEngine fontEngine;
    Rml::SetSystemInterface(&sysIface);
    Rml::SetRenderInterface(&renderIface);
    Rml::SetFontEngineInterface(&fontEngine);
    ASSERT_TRUE(Rml::Initialise());
    Rml::Context* ctx = Rml::CreateContext("view-editor-layers-golden", Rml::Vector2i(64, 64));
    ASSERT_NE(ctx, nullptr);

    std::vector<Vixen::Views::EditorLayerRow> layers;
    std::vector<Vixen::Views::EditorParameterRow> parameters;
    int activeLayerCount = 0;
    Vixen::Views::EditorLayersBind bind{ &layers, &parameters, &activeLayerCount };

    Rml::DataModelConstructor c = ctx->CreateDataModel("editor_layers");
    ASSERT_TRUE(static_cast<bool>(c));
    Vixen::Views::BindEditorLayersModel(c, bind);
    EXPECT_TRUE(static_cast<bool>(c.GetModelHandle()));
    EXPECT_TRUE(sysIface.problems.empty())
        << "RmlUi reported a problem while binding the generated EditorLayers model: "
        << (sysIface.problems.empty() ? "" : sysIface.problems.front());

    Rml::RemoveContext("view-editor-layers-golden");
    Rml::Shutdown();
}
