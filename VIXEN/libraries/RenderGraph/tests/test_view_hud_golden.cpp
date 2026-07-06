/**
 * @file test_view_hud_golden.cpp
 * @brief View Contract Inc-2 golden-mirror gate. Proves the generated Hud.g.h RmlUi data-model
 * registration block (a) compiles + binds against a real Rml::DataModelConstructor, and (b)
 * matches, as a normalized ordered call sequence, the CANONICAL Hud [View] schema field order
 * (tick, bodyCount, activeLensName, activeLensCount, factions[HudFaction], events[HudEvent]).
 * The human truth used to be the hand-written block in UIRenderNode.cpp (Inc-1); that block is
 * deleted in Inc-2 (the node is now a generic IView host — see Ui/IView.h), so the truth moves
 * to the schema itself (codegen/view-schemas/Hud.cs) and its native consumer, HudView::Register
 * (application/main/include/graph/HudView.h). Do NOT parse either at runtime — the expected
 * sequence below is a maintained literal; the pinning comment tells a future editor to update
 * both sides together.
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

#include "Generated/Hud.g.h"   // the artifact under test
#include "Ui/VixenRmlSystemInterface.h"

namespace {

// Minimal null render interface (no GPU needed) — mirrors test_ui_hud_smoke.cpp's fixture;
// RmlUi requires both a SystemInterface and a RenderInterface set before Initialise()/
// CreateContext() will succeed, even headlessly.
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

// Canonical Hud schema field order (codegen/view-schemas/Hud.cs), mirrored by the native
// consumer's HudView::Register (application/main/include/graph/HudView.h) — NOT the deleted
// UIRenderNode.cpp hand-written block (Inc-1's anchor; removed in Inc-2).
const std::vector<std::string> kExpected = {
    "RegisterStruct<HudFaction>",
    "RegisterMember(name,&HudFaction::name)",
    "RegisterMember(grievance,&HudFaction::grievance)",
    "RegisterMember(focused,&HudFaction::focused)",
    "RegisterMember(known,&HudFaction::known)",
    "RegisterMember(inLens,&HudFaction::inLens)",
    "RegisterMember(recentChanged,&HudFaction::recentChanged)",
    "RegisterStruct<HudEvent>",
    "RegisterMember(kind,&HudEvent::kind)",
    "RegisterMember(tick,&HudEvent::tick)",
    "RegisterArray<std::vector<HudFaction>",   // regex [^>]+ stops at the first '>', so the
    "RegisterArray<std::vector<HudEvent>",     // captured token intentionally omits the closing '>>'
    "Bind(tick)",
    "Bind(bodyCount)",
    "Bind(activeLensName)",
    "Bind(activeLensCount)",
    "Bind(factions)",
    "Bind(events)",
};

std::string ReadGeneratedHeader() {
    std::ifstream f(VIEW_HUD_G_H);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

std::vector<std::string> ExtractSequence(const std::string& src) {
    std::vector<std::string> seq;
    // Custom delimiter "re(...)re": the pattern contains the literal sequence )" (e.g. in
    // (\w+)",) which would otherwise close a default-delimited R"(...)" raw string early.
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

TEST(ViewHudGolden, GeneratedSequenceMatchesCanonicalSchema) {
    const std::string src = ReadGeneratedHeader();
    ASSERT_FALSE(src.empty()) << "could not read " << VIEW_HUD_G_H;
    const std::vector<std::string> got = ExtractSequence(src);
    EXPECT_EQ(got, kExpected)
        << "generated Hud.g.h registration sequence diverged from the canonical Hud [View] "
           "schema (codegen/view-schemas/Hud.cs) / HudView::Register truth — regenerate the "
           "header or re-transcribe kExpected.";
}

TEST(ViewHudGolden, GeneratedBindFunctionCompilesAndBinds) {
    Vixen::Ui::VixenRmlSystemInterface sysIface;
    NullRenderInterface renderIface;
    Rml::SetSystemInterface(&sysIface);
    Rml::SetRenderInterface(&renderIface);
    ASSERT_TRUE(Rml::Initialise());
    Rml::Context* ctx = Rml::CreateContext("view-golden", Rml::Vector2i(64, 64));
    ASSERT_NE(ctx, nullptr);

    int tick = 7, bodyCount = 3, activeLensCount = 2;
    Rml::String activeLensName = "lens";
    std::vector<Vixen::Views::HudFaction> factions;
    std::vector<Vixen::Views::HudEvent> events;
    Vixen::Views::HudBind bind{ &tick, &bodyCount, &activeLensName, &activeLensCount, &factions, &events };

    Rml::DataModelConstructor c = ctx->CreateDataModel("hud");
    ASSERT_TRUE(static_cast<bool>(c));
    Vixen::Views::BindHudModel(c, bind);
    EXPECT_TRUE(static_cast<bool>(c.GetModelHandle()));

    Rml::RemoveContext("view-golden");
    Rml::Shutdown();
}
