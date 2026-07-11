/**
 * @file test_view_editor_layers_golden.cpp
 * @brief Inc-A2 (View-Model-Binding-Inc-A2-Plan-2026-07.md) golden-mirror gate. Mirrors
 * test_view_hud_golden.cpp exactly, for the editor layer view instead of the HUD: proves the
 * generated Generated/EditorLayers.g.h RmlUi data-model registration block (a) compiles + binds
 * against a real Rml::DataModelConstructor, and (b) matches, as a normalized ordered call
 * sequence, the CANONICAL EditorLayers [View] schema field order (codegen/view-schemas/
 * EditorLayers.cs: name, op, isChecked, elementId). The human truth is the schema itself plus its
 * native consumer, EditorLayersView::Register (application/editor/include/EditorLayersView.h).
 * Do NOT parse either at runtime — the expected sequence below is a maintained literal; the
 * pinning comment tells a future editor to update both sides together.
 */
#include <gtest/gtest.h>
#include <RmlUi/Core.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <regex>

#include "Generated/EditorLayers.g.h"   // the artifact under test
#include "Ui/VixenRmlSystemInterface.h"

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

// Canonical EditorLayers schema field order (codegen/view-schemas/EditorLayers.cs), mirrored by
// the native consumer's EditorLayersView::Register (application/editor/include/EditorLayersView.h).
const std::vector<std::string> kExpected = {
    "RegisterStruct<EditorLayerRow>",
    "RegisterMember(name,&EditorLayerRow::name)",
    "RegisterMember(op,&EditorLayerRow::op)",
    "RegisterMember(isChecked,&EditorLayerRow::isChecked)",
    "RegisterMember(elementId,&EditorLayerRow::elementId)",
    "RegisterArray<std::vector<EditorLayerRow>",   // regex [^>]+ stops at the first '>', so the
                                                     // captured token intentionally omits '>>'
    "Bind(layers)",
};

std::string ReadGeneratedHeader() {
    std::ifstream f(VIEW_EDITOR_LAYERS_G_H);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

std::vector<std::string> ExtractSequence(const std::string& src) {
    std::vector<std::string> seq;
    // Custom delimiter "re(...)re" — same rationale as test_view_hud_golden.cpp's identical regex.
    static const std::regex re(
        R"re(RegisterStruct<(\w+)>|RegisterMember\("(\w+)",\s*&(\w+::\w+)\)|RegisterArray<([^>]+)>|c\.Bind\("(\w+)",)re");
    for (std::sregex_iterator it(src.begin(), src.end(), re), end; it != end; ++it) {
        const std::smatch& m = *it;
        if (m[1].matched)      seq.push_back("RegisterStruct<" + m[1].str() + ">");
        else if (m[2].matched) seq.push_back("RegisterMember(" + m[2].str() + ",&" + m[3].str() + ")");
        else if (m[4].matched) seq.push_back("RegisterArray<" + m[4].str() + ">");
        else if (m[5].matched) seq.push_back("Bind(" + m[5].str() + ")");
    }
    return seq;
}

}  // namespace

TEST(ViewEditorLayersGolden, GeneratedSequenceMatchesCanonicalSchema) {
    const std::string src = ReadGeneratedHeader();
    ASSERT_FALSE(src.empty()) << "could not read " << VIEW_EDITOR_LAYERS_G_H;
    const std::vector<std::string> got = ExtractSequence(src);
    EXPECT_EQ(got, kExpected)
        << "generated EditorLayers.g.h registration sequence diverged from the canonical "
           "EditorLayers [View] schema (codegen/view-schemas/EditorLayers.cs) / "
           "EditorLayersView::Register truth — regenerate the header or re-transcribe kExpected.";
}

TEST(ViewEditorLayersGolden, GeneratedBindFunctionCompilesAndBinds) {
    Vixen::Ui::VixenRmlSystemInterface sysIface;
    NullRenderInterface renderIface;
    Rml::SetSystemInterface(&sysIface);
    Rml::SetRenderInterface(&renderIface);
    ASSERT_TRUE(Rml::Initialise());
    Rml::Context* ctx = Rml::CreateContext("view-editor-layers-golden", Rml::Vector2i(64, 64));
    ASSERT_NE(ctx, nullptr);

    std::vector<Vixen::Views::EditorLayerRow> layers;
    Vixen::Views::EditorLayersBind bind{ &layers };

    Rml::DataModelConstructor c = ctx->CreateDataModel("editor_layers");
    ASSERT_TRUE(static_cast<bool>(c));
    Vixen::Views::BindEditorLayersModel(c, bind);
    EXPECT_TRUE(static_cast<bool>(c.GetModelHandle()));

    Rml::RemoveContext("view-editor-layers-golden");
    Rml::Shutdown();
}
