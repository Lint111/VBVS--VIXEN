#include "Ui/ViewWireReaderSoa.h"
#include "Ui/ViewStore.h"
#include "Generated/Hud.blob.g.h"   // Vixen::Views::kHudBlob (version 0x9D4ACFD2)
#include <gtest/gtest.h>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

using namespace Vixen::RenderGraph;

namespace {

// Build the canonical SoA wire — the byte-for-byte twin of the C# ToBuffer() golden for a Soa-
// declared Hud model (Yeroket ViewWriterEmitterTests.ToBuffer_Produces_Canonical_Soa_Bytes).
// Distinct values per row/column (including one EMPTY string row) so a wrong-column/wrong-row
// bug shows up as a wrong VALUE, not just "it didn't crash": tick=7, bodyCount=12,
// activeLensName="Ops", activeLensCount=4, factions=[Reds/0.5/T/T/F/T, ""/0/F/F/F/F,
// Greens/0.75/F/T/T/F], events=[war@40, truce@99].
struct WB {
    std::vector<std::byte> b;
    void u8(uint8_t v)  { b.push_back(std::byte{v}); }
    void u32(uint32_t v){ u8(v&0xFF); u8((v>>8)&0xFF); u8((v>>16)&0xFF); u8((v>>24)&0xFF); }
    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
    void f32(float v)   { uint32_t u; std::memcpy(&u,&v,4); u32(u); }
    void str(const std::string& s){ u32(static_cast<uint32_t>(s.size())); for(char ch: s) u8(static_cast<uint8_t>(ch)); }
};

std::vector<std::byte> CanonicalSoaWire(uint32_t version) {
    WB w;
    w.u8('U'); w.u8('T'); w.u8('V'); w.u8('A');
    w.u32(version);
    w.u32(9);                      // top-field count
    w.i32(7); w.i32(12); w.str("Ops"); w.i32(4);

    // factions: SoA, declared column order name/grievance/focused/known/inLens/recentChanged
    w.u32(3);                      // row count
    // name column: (rows+1) offsets into THIS column's own blob + the blob itself
    w.u32(0); w.u32(4); w.u32(4); w.u32(10);
    for (char ch : std::string("RedsGreens")) w.u8(static_cast<uint8_t>(ch));
    // grievance column: contiguous floats
    w.f32(0.5f); w.f32(0.0f); w.f32(0.75f);
    // focused column
    w.u8(1); w.u8(0); w.u8(0);
    // known column
    w.u8(1); w.u8(0); w.u8(1);
    // inLens column
    w.u8(0); w.u8(0); w.u8(1);
    // recentChanged column
    w.u8(1); w.u8(0); w.u8(0);

    // events: SoA, declared column order kind/tick
    w.u32(2);
    w.u32(0); w.u32(3); w.u32(8);
    for (char ch : std::string("wartruce")) w.u8(static_cast<uint8_t>(ch));
    w.i32(40); w.i32(99);

    // T1 inspect-panel tail scalars (top-level, decoded identically to AoS)
    w.i32(1); w.str("Reds"); w.str("border skirmish");  // inspectSelected, inspectName, inspectCause

    return w.b;
}

int Field(const ViewBlob& blob, std::string_view name) {
    for (size_t k = 0; k < blob.fields.size(); ++k) if (blob.fields[k].name == name) return (int)k;
    return -1;
}
int Elem(const ViewFieldDesc& f, std::string_view name) {
    for (size_t k = 0; k < f.elem.size(); ++k) if (f.elem[k].name == name) return (int)k;
    return -1;
}

}  // namespace

TEST(ViewWireSoaRoundtrip, ReadsBackEveryFieldIncludingEmptyStringRow) {
    const auto& blob = Vixen::Views::kHudBlob;
    ViewStore store(blob, blob.version);
    auto wire = CanonicalSoaWire(blob.version);

    ASSERT_TRUE(ViewWireReaderSoa::Apply(wire, store));

    // scalars
    EXPECT_EQ(*static_cast<int*>(store.ScalarSlotPtr(Field(blob,"tick"))), 7);
    EXPECT_EQ(*static_cast<int*>(store.ScalarSlotPtr(Field(blob,"bodyCount"))), 12);
    EXPECT_EQ(*static_cast<Rml::String*>(store.ScalarSlotPtr(Field(blob,"activeLensName"))), "Ops");
    EXPECT_EQ(*static_cast<int*>(store.ScalarSlotPtr(Field(blob,"activeLensCount"))), 4);

    // factions column-decoded array — including the EMPTY-name middle row (non-vacuous proof
    // that the offsets+blob-per-column path handles a zero-length string mid-column correctly).
    int fi = Field(blob, "factions");
    const auto& fdesc = blob.fields[fi];
    auto& fac = store.Array(fi);
    ASSERT_EQ(fac.size(), 3u);
    EXPECT_EQ(fac[0].cells[Elem(fdesc,"name")].s, "Reds");
    EXPECT_FLOAT_EQ(fac[0].cells[Elem(fdesc,"grievance")].f, 0.5f);
    EXPECT_TRUE (fac[0].cells[Elem(fdesc,"focused")].b);
    EXPECT_TRUE (fac[0].cells[Elem(fdesc,"known")].b);
    EXPECT_FALSE(fac[0].cells[Elem(fdesc,"inLens")].b);
    EXPECT_TRUE (fac[0].cells[Elem(fdesc,"recentChanged")].b);

    EXPECT_EQ(fac[1].cells[Elem(fdesc,"name")].s, "");
    EXPECT_FLOAT_EQ(fac[1].cells[Elem(fdesc,"grievance")].f, 0.0f);
    EXPECT_FALSE(fac[1].cells[Elem(fdesc,"focused")].b);
    EXPECT_FALSE(fac[1].cells[Elem(fdesc,"known")].b);
    EXPECT_FALSE(fac[1].cells[Elem(fdesc,"inLens")].b);
    EXPECT_FALSE(fac[1].cells[Elem(fdesc,"recentChanged")].b);

    EXPECT_EQ(fac[2].cells[Elem(fdesc,"name")].s, "Greens");
    EXPECT_FLOAT_EQ(fac[2].cells[Elem(fdesc,"grievance")].f, 0.75f);
    EXPECT_FALSE(fac[2].cells[Elem(fdesc,"focused")].b);
    EXPECT_TRUE (fac[2].cells[Elem(fdesc,"known")].b);
    EXPECT_TRUE (fac[2].cells[Elem(fdesc,"inLens")].b);
    EXPECT_FALSE(fac[2].cells[Elem(fdesc,"recentChanged")].b);

    // events column-decoded array
    int ei = Field(blob, "events");
    const auto& edesc = blob.fields[ei];
    auto& ev = store.Array(ei);
    ASSERT_EQ(ev.size(), 2u);
    EXPECT_EQ(ev[0].cells[Elem(edesc,"kind")].s, "war");
    EXPECT_EQ(ev[0].cells[Elem(edesc,"tick")].i, 40);
    EXPECT_EQ(ev[1].cells[Elem(edesc,"kind")].s, "truce");
    EXPECT_EQ(ev[1].cells[Elem(edesc,"tick")].i, 99);

    // T1 inspect-panel tail scalars
    EXPECT_EQ(*static_cast<int*>(store.ScalarSlotPtr(Field(blob,"inspectSelected"))), 1);
    EXPECT_EQ(*static_cast<Rml::String*>(store.ScalarSlotPtr(Field(blob,"inspectName"))), "Reds");
    EXPECT_EQ(*static_cast<Rml::String*>(store.ScalarSlotPtr(Field(blob,"inspectCause"))), "border skirmish");
}

TEST(ViewWireSoaRoundtrip, VersionMismatchIsHardError) {
    const auto& blob = Vixen::Views::kHudBlob;
    ViewStore store(blob, blob.version);
    auto wire = CanonicalSoaWire(blob.version ^ 0x1u);   // perturbed version

    EXPECT_FALSE(ViewWireReaderSoa::Apply(wire, store));
    // store untouched — the version guard returns before any field write.
    EXPECT_EQ(*static_cast<int*>(store.ScalarSlotPtr(Field(blob,"tick"))), 0);
    EXPECT_EQ(store.Array(Field(blob,"factions")).size(), 0u);
}

TEST(ViewWireSoaRoundtrip, MalformedIsRejected) {
    const auto& blob = Vixen::Views::kHudBlob;
    auto good = CanonicalSoaWire(blob.version);

    // (a) wrong magic
    { ViewStore s(blob, blob.version); auto w = good; w[0] = std::byte{'X'};
      EXPECT_FALSE(ViewWireReaderSoa::Apply(w, s)); }
    // (b) truncated body
    { ViewStore s(blob, blob.version); std::vector<std::byte> w(good.begin(), good.begin()+20);
      EXPECT_FALSE(ViewWireReaderSoa::Apply(w, s)); }
    // (c) field-count mismatch
    { ViewStore s(blob, blob.version); auto w = good; w[8] = std::byte{5};   // count byte -> 5, not 6
      EXPECT_FALSE(ViewWireReaderSoa::Apply(w, s)); }
    // (d) trailing garbage
    { ViewStore s(blob, blob.version); auto w = good; w.push_back(std::byte{0xAB});
      EXPECT_FALSE(ViewWireReaderSoa::Apply(w, s)); }
}

namespace {

// A self-contained blob exercising a Vector-kind field as a ROW/COLUMN of an SoA StructArray --
// the gap this fix closes. Before the fix, the row-element decode switch hit `default: c.ok =
// false` for ViewKind::Vector (rejected as malformed) instead of reading 3 contiguous F32s per
// row (SoA columns are per-field contiguous, so the whole "position" column is 3*rowCount floats
// back-to-back, not interleaved with "t").
constexpr ViewFieldDesc kOrbitPointElem[] = {
    {"t", ViewKind::Float, {}}, {"position", ViewKind::Vector, {}} };
constexpr ViewFieldDesc kVecFields[] = {
    {"points", ViewKind::ArrayOfStruct, kOrbitPointElem} };
constexpr ViewBlob kVecBlob = {"orbitpath", kVecFields, 0x3333u};

std::vector<std::byte> VecSoaWire() {
    WB w;
    w.u8('U'); w.u8('T'); w.u8('V'); w.u8('A');
    w.u32(kVecBlob.version);
    w.u32(1);                              // top-field count: points
    w.u32(2);                              // points row count
    w.f32(1.5f); w.f32(2.5f);              // t column: contiguous
    w.f32(1.0f); w.f32(2.0f); w.f32(3.0f); // position column, row0 xyz
    w.f32(-1.0f); w.f32(0.0f); w.f32(4.5f);// position column, row1 xyz
    return w.b;
}

}  // namespace

TEST(ViewWireSoaRoundtrip, RowVectorColumnReadBackCorrectly) {
    ViewStore store(kVecBlob, kVecBlob.version);
    auto wire = VecSoaWire();

    ASSERT_TRUE(ViewWireReaderSoa::Apply(wire, store));

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

namespace {

// SubjectRef milestone (Task 10): the SoA top-level-field decode is identical to AoS -- 1 kind
// byte + 8 little-endian instance bytes, same as the AoS test's SubjectWire.
constexpr ViewFieldDesc kSubjectFields[] = { {"subject", ViewKind::SubjectRef, {}} };
constexpr ViewBlob kSubjectBlob = {"inspect", kSubjectFields, 0x4444u};

std::vector<std::byte> SubjectSoaWire(uint32_t version, uint8_t kind, uint64_t instance) {
    WB w;
    w.u8('U'); w.u8('T'); w.u8('V'); w.u8('A');
    w.u32(version);
    w.u32(1);           // top-field count: subject
    w.u8(kind);
    for (int i = 0; i < 8; ++i) w.u8(static_cast<uint8_t>((instance >> (8*i)) & 0xFF));
    return w.b;
}

}  // namespace

TEST(ViewWireSoaRoundtrip, SubjectRefFieldReadsBackKindAndInstance) {
    ViewStore store(kSubjectBlob, kSubjectBlob.version);
    auto wire = SubjectSoaWire(kSubjectBlob.version, 10, 42ULL);

    ASSERT_TRUE(ViewWireReaderSoa::Apply(wire, store));

    auto* subj = static_cast<SubjectRef*>(store.ScalarSlotPtr(Field(kSubjectBlob, "subject")));
    EXPECT_EQ(subj->kind, 10);
    EXPECT_EQ(subj->instance, 42ULL);
}
