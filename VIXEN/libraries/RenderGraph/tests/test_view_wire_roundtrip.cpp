#include "Ui/ViewWireReader.h"
#include "Ui/ViewStore.h"
#include "Generated/Hud.blob.g.h"   // Vixen::Views::kHudBlob (version 0x55D27B8C)
#include <gtest/gtest.h>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

using namespace Vixen::RenderGraph;

namespace {

// Build the canonical UTVA wire "B" — the byte-for-byte twin of the C# ToBuffer() golden
// (Yeroket ViewWriterEmitterTests). Same known input: tick=42, bodyCount=9, activeLensName="Intel",
// activeLensCount=3, factions=[Reds,Blues], events=[war@40].
struct WB {
    std::vector<std::byte> b;
    void u8(uint8_t v)  { b.push_back(std::byte{v}); }
    void u32(uint32_t v){ u8(v&0xFF); u8((v>>8)&0xFF); u8((v>>16)&0xFF); u8((v>>24)&0xFF); }
    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
    void f32(float v)   { uint32_t u; std::memcpy(&u,&v,4); u32(u); }
    void str(const std::string& s){ u32(static_cast<uint32_t>(s.size())); for(char ch: s) u8(static_cast<uint8_t>(ch)); }
};

std::vector<std::byte> CanonicalWire(uint32_t version) {
    WB w;
    w.u8('U'); w.u8('T'); w.u8('V'); w.u8('A');
    w.u32(version);
    w.u32(6);                      // top-field count
    w.i32(42); w.i32(9); w.str("Intel"); w.i32(3);
    w.u32(2);                      // factions
    w.str("Reds");  w.f32(0.5f);  w.u8(1); w.u8(1); w.u8(0); w.u8(1);
    w.str("Blues"); w.f32(0.25f); w.u8(0); w.u8(0); w.u8(1); w.u8(0);
    w.u32(1);                      // events
    w.str("war"); w.i32(40);
    return w.b;
}

// field index by name in the Hud blob (declared order).
int Field(const ViewBlob& blob, std::string_view name) {
    for (size_t k = 0; k < blob.fields.size(); ++k) if (blob.fields[k].name == name) return (int)k;
    return -1;
}
int Elem(const ViewFieldDesc& f, std::string_view name) {
    for (size_t k = 0; k < f.elem.size(); ++k) if (f.elem[k].name == name) return (int)k;
    return -1;
}

}  // namespace

TEST(ViewWireRoundtrip, ReadsBackEveryField) {
    const auto& blob = Vixen::Views::kHudBlob;
    ViewStore store(blob, blob.version);
    auto wire = CanonicalWire(blob.version);

    ASSERT_TRUE(ViewWireReader::Apply(wire, store));

    // scalars
    EXPECT_EQ(*static_cast<int*>(store.ScalarSlotPtr(Field(blob,"tick"))), 42);
    EXPECT_EQ(*static_cast<int*>(store.ScalarSlotPtr(Field(blob,"bodyCount"))), 9);
    EXPECT_EQ(*static_cast<Rml::String*>(store.ScalarSlotPtr(Field(blob,"activeLensName"))), "Intel");
    EXPECT_EQ(*static_cast<int*>(store.ScalarSlotPtr(Field(blob,"activeLensCount"))), 3);

    // factions array
    int fi = Field(blob, "factions");
    const auto& fdesc = blob.fields[fi];
    auto& fac = store.Array(fi);
    ASSERT_EQ(fac.size(), 2u);
    EXPECT_EQ(fac[0].cells[Elem(fdesc,"name")].s, "Reds");
    EXPECT_FLOAT_EQ(fac[0].cells[Elem(fdesc,"grievance")].f, 0.5f);
    EXPECT_TRUE (fac[0].cells[Elem(fdesc,"focused")].b);
    EXPECT_TRUE (fac[0].cells[Elem(fdesc,"known")].b);
    EXPECT_FALSE(fac[0].cells[Elem(fdesc,"inLens")].b);
    EXPECT_TRUE (fac[0].cells[Elem(fdesc,"recentChanged")].b);
    EXPECT_EQ(fac[1].cells[Elem(fdesc,"name")].s, "Blues");
    EXPECT_FLOAT_EQ(fac[1].cells[Elem(fdesc,"grievance")].f, 0.25f);
    EXPECT_TRUE (fac[1].cells[Elem(fdesc,"inLens")].b);

    // events array
    int ei = Field(blob, "events");
    const auto& edesc = blob.fields[ei];
    auto& ev = store.Array(ei);
    ASSERT_EQ(ev.size(), 1u);
    EXPECT_EQ(ev[0].cells[Elem(edesc,"kind")].s, "war");
    EXPECT_EQ(ev[0].cells[Elem(edesc,"tick")].i, 40);
}

TEST(ViewWireRoundtrip, VersionMismatchIsHardError) {
    const auto& blob = Vixen::Views::kHudBlob;
    ViewStore store(blob, blob.version);
    auto wire = CanonicalWire(blob.version ^ 0x1u);   // perturbed version

    EXPECT_FALSE(ViewWireReader::Apply(wire, store));
    // store untouched — the version guard returns before any field write.
    EXPECT_EQ(*static_cast<int*>(store.ScalarSlotPtr(Field(blob,"tick"))), 0);
    EXPECT_EQ(store.Array(Field(blob,"factions")).size(), 0u);
}

TEST(ViewWireRoundtrip, MalformedIsRejected) {
    const auto& blob = Vixen::Views::kHudBlob;
    auto good = CanonicalWire(blob.version);

    // (a) wrong magic
    { ViewStore s(blob, blob.version); auto w = good; w[0] = std::byte{'X'};
      EXPECT_FALSE(ViewWireReader::Apply(w, s)); }
    // (b) truncated body
    { ViewStore s(blob, blob.version); std::vector<std::byte> w(good.begin(), good.begin()+20);
      EXPECT_FALSE(ViewWireReader::Apply(w, s)); }
    // (c) field-count mismatch
    { ViewStore s(blob, blob.version); auto w = good; w[8] = std::byte{5};   // count byte -> 5, not 6
      EXPECT_FALSE(ViewWireReader::Apply(w, s)); }
    // (d) trailing garbage
    { ViewStore s(blob, blob.version); auto w = good; w.push_back(std::byte{0xAB});
      EXPECT_FALSE(ViewWireReader::Apply(w, s)); }
}

namespace {

// A self-contained blob (not the generated Hud header) exercising BOTH a top-level Vector field
// AND a Vector-kind field as a ROW of a StructArray -- the exact gap this fix closes. Before the
// fix: (a) the top-level field-kind switch had no ViewKind::Vector case at all (fell through the
// switch, wrote nothing, moved cursor 0 bytes -> desynced every subsequent field); (b) the
// row-element decode switch hit `default: c.ok = false` for a Vector row field (rejected as
// malformed).
constexpr ViewFieldDesc kOrbitPointElem[] = {
    {"t", ViewKind::Float, {}}, {"position", ViewKind::Vector, {}} };
constexpr ViewFieldDesc kVecFields[] = {
    {"origin", ViewKind::Vector, {}},
    {"points", ViewKind::ArrayOfStruct, kOrbitPointElem} };
constexpr ViewBlob kVecBlob = {"orbitpath", kVecFields, 0x2222u};

std::vector<std::byte> VecWire() {
    WB w;
    w.u8('U'); w.u8('T'); w.u8('V'); w.u8('A');
    w.u32(kVecBlob.version);
    w.u32(2);                              // top-field count: origin, points
    w.f32(10.0f); w.f32(20.0f); w.f32(30.0f);   // origin (top-level Vector)
    w.u32(2);                              // points row count
    w.f32(1.5f); w.f32(1.0f); w.f32(2.0f); w.f32(3.0f);      // row0: t, position.xyz
    w.f32(2.5f); w.f32(-1.0f); w.f32(0.0f); w.f32(4.5f);     // row1: t, position.xyz
    return w.b;
}

}  // namespace

TEST(ViewWireRoundtrip, TopLevelAndRowVectorFieldsReadBackCorrectly) {
    ViewStore store(kVecBlob, kVecBlob.version);
    auto wire = VecWire();

    ASSERT_TRUE(ViewWireReader::Apply(wire, store));

    auto* origin = static_cast<Vec3f*>(store.ScalarSlotPtr(Field(kVecBlob, "origin")));
    EXPECT_FLOAT_EQ(origin->x, 10.0f);
    EXPECT_FLOAT_EQ(origin->y, 20.0f);
    EXPECT_FLOAT_EQ(origin->z, 30.0f);

    int pi = Field(kVecBlob, "points");
    const auto& pdesc = kVecBlob.fields[pi];
    auto& rows = store.Array(pi);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_FLOAT_EQ(rows[0].cells[Elem(pdesc, "t")].f, 1.5f);
    EXPECT_FLOAT_EQ(rows[0].cells[Elem(pdesc, "position")].vec.x, 1.0f);
    EXPECT_FLOAT_EQ(rows[0].cells[Elem(pdesc, "position")].vec.y, 2.0f);
    EXPECT_FLOAT_EQ(rows[0].cells[Elem(pdesc, "position")].vec.z, 3.0f);
    EXPECT_FLOAT_EQ(rows[1].cells[Elem(pdesc, "t")].f, 2.5f);
    EXPECT_FLOAT_EQ(rows[1].cells[Elem(pdesc, "position")].vec.x, -1.0f);
    EXPECT_FLOAT_EQ(rows[1].cells[Elem(pdesc, "position")].vec.z, 4.5f);
}
