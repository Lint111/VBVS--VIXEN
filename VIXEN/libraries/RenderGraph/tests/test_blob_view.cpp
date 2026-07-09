/**
 * @file test_blob_view.cpp
 * @brief View Contract Inc-2b BlobView gate. Proves the generic IView host builds a live RmlUi
 * data model directly from a ViewBlob (scalars + array-of-struct) with no per-schema C++ code,
 * and that a version mismatch between the blob and the ViewStore's consumer version hard-skips
 * registration rather than binding a partial/garbage model. Mirrors test_view_hud_golden's RmlUi
 * fixture (NullRenderInterface + VixenRmlSystemInterface + Initialise + CreateContext).
 */
#include <gtest/gtest.h>
#include <RmlUi/Core.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/RenderInterface.h>

#include "Ui/BlobView.h"
#include "Ui/VixenRmlSystemInterface.h"

using namespace Vixen::RenderGraph;

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

constexpr ViewFieldDesc kElem[] = { {"grievance", ViewKind::Float, {}}, {"focused", ViewKind::Bool, {}} };
constexpr ViewFieldDesc kFields[] = {
    {"tick", ViewKind::Int, {}}, {"name", ViewKind::String, {}},
    {"factions", ViewKind::ArrayOfStruct, kElem} };
constexpr ViewBlob kBlob = {"blobtest", kFields, 0x1234u};

struct RmlFixture {
    Vixen::Ui::VixenRmlSystemInterface sys;
    NullRender render;
    Rml::Context* ctx = nullptr;
    RmlFixture() {
        Rml::SetSystemInterface(&sys);
        Rml::SetRenderInterface(&render);
        Rml::Initialise();
        ctx = Rml::CreateContext("t", Rml::Vector2i(64, 64));
    }
    ~RmlFixture() { Rml::Shutdown(); }
};

}  // namespace

TEST(BlobView, MatchingVersionRegistersAndBinds) {
    RmlFixture fx;
    BlobView view(kBlob, "assets/ui/hud.rml");
    view.Store().SetScalar("tick", ViewValue::I(7));
    view.Store().SetScalar("name", ViewValue::S("Reds"));
    auto rows = view.Store().ResizeArray("factions", 1);
    rows.Set(0, "grievance", ViewValue::F(0.5f));
    rows.Set(0, "focused", ViewValue::B(true));

    Rml::DataModelConstructor c = fx.ctx->CreateDataModel(view.ModelName());
    ASSERT_TRUE(static_cast<bool>(c));
    view.Register(c);
    EXPECT_TRUE(view.Registered());
    EXPECT_TRUE(static_cast<bool>(c.GetModelHandle()));
}

TEST(BlobView, VersionMismatchSkipsRegister) {
    RmlFixture fx;
    BlobView view(kBlob, "assets/ui/hud.rml");
    view.SetConsumerVersion(0xDEADu);  // deliberate mismatch vs kBlob.version

    Rml::DataModelConstructor c = fx.ctx->CreateDataModel(view.ModelName());
    ASSERT_TRUE(static_cast<bool>(c));
    view.Register(c);
    EXPECT_FALSE(view.Registered());
}
