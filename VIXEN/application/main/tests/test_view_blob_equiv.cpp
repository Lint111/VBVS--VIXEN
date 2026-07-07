/**
 * @file test_view_blob_equiv.cpp
 * @brief View Contract Inc-2b Task 9 -- the PROOF GATE. Populates the SAME canonical fixture
 * through four independent paths and proves the resolved storage is byte-identical across the
 * native consumer and both blob-delivery forms:
 *   1. native  -- Vixen::App::HudView (Register + SetHudView) -> H_native
 *   2. header  -- BlobView(Vixen::Views::kHudBlob, ...) (constexpr-delivered blob)      -> H_header
 *   3. datafile-- BlobView(ViewBlobFile::Load("assets/ui/hud.viewblob")->Blob(), ...)   -> H_datafile
 *   4. mismatch-- BlobView with a deliberately wrong consumer version -> Register() hard-skips
 *
 * H_native == H_header == H_datafile is the actual claim of the View Contract program: the
 * generic reflection-blob host (BlobView/ViewStore), fed either delivery form, drives the SAME
 * data a hand-written native IView does -- so a renderer/consumer that only understands ViewBlob
 * (no per-schema C++) reproduces the native HUD's data exactly. test_hud_render_capture (the real
 * GPU anchor, unmodified) is the render-truth half of that claim: the native path, so hashed,
 * paints real pixels. This test is the CPU half: everything upstream of paint agrees byte-for-byte.
 *
 * Lives under application/main/tests/ (not libraries/RenderGraph/tests/) because it needs BOTH the
 * native Vixen::App::HudView (application/main) and the blob types (RenderGraph) -- linking HudView
 * into a RenderGraph-tests target would invert the dependency direction. Registered via
 * test_view_blob_equiv.cmake, mirroring test_hud_view.cmake (links VixenApp, which already pulls in
 * RenderGraph PUBLIC).
 */
#include <gtest/gtest.h>
#include <RmlUi/Core.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/RenderInterface.h>

#include "Ui/BlobView.h"
#include "Ui/ViewBlobFile.h"
#include "Ui/VixenRmlSystemInterface.h"
#include "Generated/Hud.blob.g.h"   // Vixen::Views::kHudBlob
#include "graph/HudView.h"
#include "VixenHash.h"

#include <cstdint>
#include <vector>

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

// One RmlUi global init/context per test -- mirrors test_blob_view.cpp's RmlFixture.
struct RmlFixture {
    Vixen::Ui::VixenRmlSystemInterface sys;
    NullRender render;
    Rml::Context* ctx = nullptr;
    RmlFixture() {
        Rml::SetSystemInterface(&sys);
        Rml::SetRenderInterface(&render);
        Rml::Initialise();
        ctx = Rml::CreateContext("equiv", Rml::Vector2i(64, 64));
    }
    ~RmlFixture() { Rml::Shutdown(); }
};

// --- the ONE canonical fixture, shared by all three populated paths ---
struct Fixture {
    int tick = 7;
    int bodyCount = 3;
    // "Logistics" is HudView::SetHudView's kLensNames[2] (native lens-name projection) -- the
    // blob-delivery paths just SetScalar the string directly, so this keeps all three paths
    // consistent with the SAME activeLens selector (2) without hardcoding the projection twice.
    const char* activeLensName = "Logistics";
    int activeLensCount = 2;
    // single faction row, declared field order: name, grievance, focused, known, inLens, recentChanged
    const char* factionName = "Reds";
    float factionGrievance = 0.7f;
    bool factionFocused = true;
    bool factionKnown = true;
    bool factionInLens = false;
    // native HudView derives recentChanged from recentEventAge < kJuiceK(20); the native call below
    // uses recentEventAge=0, so this must be true for the two paths to agree on the same fixture.
    bool factionRecentChanged = true;
    // single event row, declared field order: kind, tick
    const char* eventKind = "war";
    int eventTick = 81;
};

constexpr Fixture kFixture{};

void AppendBytes(std::vector<uint8_t>& buf, const void* p, size_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    buf.insert(buf.end(), b, b + n);
}
void AppendString(std::vector<uint8_t>& buf, std::string_view s) {
    AppendBytes(buf, s.data(), s.size());
}

// Hashes a ViewStore's resolved storage in the Hud blob's declared field order:
// tick, bodyCount, activeLensName, activeLensCount, factions[6 members], events[2 members].
uint64_t HashStore(ViewStore& s) {
    std::vector<uint8_t> buf;
    AppendBytes(buf, s.ScalarSlotPtr(0), sizeof(int));  // tick
    AppendBytes(buf, s.ScalarSlotPtr(1), sizeof(int));  // bodyCount
    {
        auto* str = static_cast<Rml::String*>(s.ScalarSlotPtr(2));  // activeLensName
        AppendString(buf, *str);
    }
    AppendBytes(buf, s.ScalarSlotPtr(3), sizeof(int));  // activeLensCount

    auto& factions = s.Array(4);
    for (auto& row : factions) {
        AppendString(buf, row.Cell(0).s);           // name
        AppendBytes(buf, &row.Cell(1).f, sizeof(float));  // grievance
        AppendBytes(buf, &row.Cell(2).b, sizeof(bool));   // focused
        AppendBytes(buf, &row.Cell(3).b, sizeof(bool));   // known
        AppendBytes(buf, &row.Cell(4).b, sizeof(bool));   // inLens
        AppendBytes(buf, &row.Cell(5).b, sizeof(bool));   // recentChanged
    }
    auto& events = s.Array(5);
    for (auto& row : events) {
        AppendString(buf, row.Cell(0).s);                 // kind
        AppendBytes(buf, &row.Cell(1).i, sizeof(int));    // tick
    }
    return Vixen::Hash::ComputeHash64(buf);
}

// Populates a ViewStore (either header- or datafile-delivered BlobView's store) with the fixture.
void PopulateBlobStore(ViewStore& s, const Fixture& fx) {
    s.SetScalar("tick", ViewValue::I(fx.tick));
    s.SetScalar("bodyCount", ViewValue::I(fx.bodyCount));
    s.SetScalar("activeLensName", ViewValue::S(fx.activeLensName));
    s.SetScalar("activeLensCount", ViewValue::I(fx.activeLensCount));

    auto factions = s.ResizeArray("factions", 1);
    factions.Set(0, "name", ViewValue::S(fx.factionName));
    factions.Set(0, "grievance", ViewValue::F(fx.factionGrievance));
    factions.Set(0, "focused", ViewValue::B(fx.factionFocused));
    factions.Set(0, "known", ViewValue::B(fx.factionKnown));
    factions.Set(0, "inLens", ViewValue::B(fx.factionInLens));
    factions.Set(0, "recentChanged", ViewValue::B(fx.factionRecentChanged));

    auto events = s.ResizeArray("events", 1);
    events.Set(0, "kind", ViewValue::S(fx.eventKind));
    events.Set(0, "tick", ViewValue::I(fx.eventTick));
}

// Hashes a native HudView's storage in the SAME declared field order as HashStore, via its debug
// accessors (existing ones + the minimal read-only additions below).
uint64_t HashNativeView(const Vixen::App::HudView& view) {
    std::vector<uint8_t> buf;
    int tick = view.DebugTick();
    AppendBytes(buf, &tick, sizeof(int));
    int bodyCount = view.DebugBodyCount();
    AppendBytes(buf, &bodyCount, sizeof(int));
    AppendString(buf, view.DebugLensName());
    int lensCount = view.DebugActiveLensCount();
    AppendBytes(buf, &lensCount, sizeof(int));

    for (size_t i = 0; i < view.DebugFactionCount(); ++i) {
        const auto& f = view.DebugFaction(i);
        AppendString(buf, f.name);
        AppendBytes(buf, &f.grievance, sizeof(float));
        AppendBytes(buf, &f.focused, sizeof(bool));
        AppendBytes(buf, &f.known, sizeof(bool));
        AppendBytes(buf, &f.inLens, sizeof(bool));
        AppendBytes(buf, &f.recentChanged, sizeof(bool));
    }
    for (size_t i = 0; i < view.DebugEventCount(); ++i) {
        const auto& e = view.DebugEvent(i);
        AppendString(buf, e.kind);
        AppendBytes(buf, &e.tick, sizeof(int));
    }
    return Vixen::Hash::ComputeHash64(buf);
}

}  // namespace

TEST(ViewBlobEquiv, NativeHeaderAndDatafileHashesAgree) {
    RmlFixture rmlFx;

    // --- 1. native HudView ---
    Vixen::App::HudView nativeView;
    Rml::DataModelConstructor nativeCtor = rmlFx.ctx->CreateDataModel(nativeView.ModelName());
    ASSERT_TRUE(static_cast<bool>(nativeCtor));
    nativeView.Register(nativeCtor);

    Vixen::App::HudFactionIn nativeFaction{
        kFixture.factionName, kFixture.factionGrievance, kFixture.factionFocused,
        kFixture.factionKnown, kFixture.factionInLens, /*recentEventAge=*/0 };
    Vixen::App::HudEventIn nativeEvent{ kFixture.eventKind, kFixture.eventTick };
    // activeLens=2 maps to "Logistics" per HudView's lens-name projection (kLensNames[2]; mirrors test_hud_view.cpp).
    nativeView.SetHudView(kFixture.tick, kFixture.bodyCount, /*activeLens=*/2, kFixture.activeLensCount,
                          {&nativeFaction, 1}, {&nativeEvent, 1});
    ASSERT_STREQ(nativeView.DebugLensName(), kFixture.activeLensName);
    const uint64_t hNative = HashNativeView(nativeView);

    // --- 2. header-delivered blob ---
    BlobView headerView(Vixen::Views::kHudBlob, "assets/ui/hud.rml");
    PopulateBlobStore(headerView.Store(), kFixture);
    const uint64_t hHeader = HashStore(headerView.Store());

    Rml::DataModelConstructor headerCtor = rmlFx.ctx->CreateDataModel("hud_header");
    ASSERT_TRUE(static_cast<bool>(headerCtor));
    headerView.Register(headerCtor);
    EXPECT_TRUE(headerView.Registered());

    // --- 3. datafile-delivered blob ---
    auto file = ViewBlobFile::Load("assets/ui/hud.viewblob");
    ASSERT_TRUE(file.has_value()) << "failed to load assets/ui/hud.viewblob (CWD-relative)";
    BlobView datafileView(file->Blob(), "assets/ui/hud.rml");
    PopulateBlobStore(datafileView.Store(), kFixture);
    const uint64_t hDatafile = HashStore(datafileView.Store());

    Rml::DataModelConstructor datafileCtor = rmlFx.ctx->CreateDataModel("hud_datafile");
    ASSERT_TRUE(static_cast<bool>(datafileCtor));
    datafileView.Register(datafileCtor);
    EXPECT_TRUE(datafileView.Registered());

    // --- the proof ---
    EXPECT_EQ(hNative, hHeader)
        << "native HudView storage disagrees with the header-delivered BlobView for the same fixture";
    EXPECT_EQ(hNative, hDatafile)
        << "native HudView storage disagrees with the datafile-delivered BlobView for the same fixture";
    EXPECT_EQ(hHeader, hDatafile)
        << "header- and datafile-delivered blobs disagree despite being the same schema";
}

TEST(ViewBlobEquiv, ParserRoundTripEqualsHeaderStructure) {
    auto file = ViewBlobFile::Load("assets/ui/hud.viewblob");
    ASSERT_TRUE(file.has_value());
    const ViewBlob& d = file->Blob();
    const ViewBlob& h = Vixen::Views::kHudBlob;

    EXPECT_EQ(d.model, h.model);
    EXPECT_EQ(d.version, h.version);
    ASSERT_EQ(d.fields.size(), h.fields.size());
    for (size_t i = 0; i < h.fields.size(); ++i) {
        EXPECT_EQ(d.fields[i].name, h.fields[i].name) << "field " << i;
        EXPECT_EQ(d.fields[i].kind, h.fields[i].kind) << "field " << i;
        ASSERT_EQ(d.fields[i].elem.size(), h.fields[i].elem.size()) << "field " << i;
        for (size_t j = 0; j < h.fields[i].elem.size(); ++j) {
            EXPECT_EQ(d.fields[i].elem[j].name, h.fields[i].elem[j].name) << "field " << i << " elem " << j;
            EXPECT_EQ(d.fields[i].elem[j].kind, h.fields[i].elem[j].kind) << "field " << i << " elem " << j;
        }
    }
}

TEST(ViewBlobEquiv, VersionMismatchYieldsEmptyModel) {
    RmlFixture rmlFx;
    BlobView view(Vixen::Views::kHudBlob, "assets/ui/hud.rml");
    view.SetConsumerVersion(Vixen::Views::kHudBlob.version ^ 0x1u);  // deliberate desync

    Rml::DataModelConstructor c = rmlFx.ctx->CreateDataModel(view.ModelName());
    ASSERT_TRUE(static_cast<bool>(c));
    view.Register(c);
    EXPECT_FALSE(view.Registered()) << "version mismatch must hard-skip registration, never bind a partial model";
}
