// Offline (no GPU) unit test for the native HudView consumer (View Contract Inc-2, Task 4). Proves
// HudView::Register wires the generated BindHudModel to a real Rml::DataModelConstructor, and that
// SetHudView (the relocated UIRenderNode::SetHudView projection) drives its own storage correctly
// (lens-name mapping, juice-window recentChanged).
#include <gtest/gtest.h>
#include <RmlUi/Core.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/RenderInterface.h>
#include "graph/HudView.h"
#include "Ui/VixenRmlSystemInterface.h"

namespace {
class NullRI final : public Rml::RenderInterface {
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override { return 0; }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override {}
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override { return 0; }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return 0; }
    void ReleaseTexture(Rml::TextureHandle) override {}
};
}  // namespace

TEST(HudViewTest, RegistersAndProjectsFactionData) {
    Vixen::Ui::VixenRmlSystemInterface sys;
    NullRI ri;
    Rml::SetSystemInterface(&sys);
    Rml::SetRenderInterface(&ri);
    ASSERT_TRUE(Rml::Initialise());
    Rml::Context* ctx = Rml::CreateContext("hv", Rml::Vector2i(64, 64));
    ASSERT_NE(ctx, nullptr);

    Vixen::App::HudView view;
    Rml::DataModelConstructor c = ctx->CreateDataModel(view.ModelName());
    ASSERT_TRUE(static_cast<bool>(c));
    view.Register(c);
    auto handle = c.GetModelHandle();
    EXPECT_TRUE(static_cast<bool>(handle));
    EXPECT_STREQ(view.ModelName(), "hud");

    // projection: lens enum 2 -> "Logistics", juice window
    Vixen::App::HudFactionIn f{"acme", 3.5f, true, false, true, 0};
    view.SetHudView(42, 7, 2, 4, {&f, 1}, {});
    EXPECT_EQ(view.DebugTick(), 42);
    EXPECT_STREQ(view.DebugLensName(), "Logistics");
    EXPECT_TRUE(view.DebugFactionRecentChanged(0));  // recentEventAge 0 < kJuiceK

    Rml::RemoveContext("hv");
    Rml::Shutdown();
}
