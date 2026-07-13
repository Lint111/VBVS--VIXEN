#include "Ui/ViewWireReaderSoa.h"
#include "Ui/ViewStore.h"
#include "Generated/Hud.blob.g.h"           // Vixen::Views::kHudBlob (version 0x55D27B8C)
#include "Generated/HudTypedAccessor.g.h"   // Vixen::Views::HudSection (View Contract Inc-5b Milestone A)
#include "Generated/VectorProof.blob.g.h"   // Vixen::Views::kVectorProofBlob (Milestone 2.4b Vector proof)
#include "Generated/VectorProof.typed.g.h"  // Vixen::Views::VectorProofSection
#include <gtest/gtest.h>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

using namespace Vixen::RenderGraph;

// Milestone A proof (View Contract Inc-5b): the SAME canonical SoA wire bytes and kHudBlob that
// test_view_wire_soa_roundtrip.cpp already proves correct via the raw ViewStore API are decoded
// here a SECOND time -- through the NEW GENERATED Vixen::Views::HudSection accessor class
// (TypedAccessorEmitter.cs, --typed-accessor-cpp) built over the SAME filled ViewStore. Every
// decoded value asserted below must equal the raw-API test's proven values exactly: this is the
// equivalence proof that a generated accessor class reproduces what the raw ViewStore API already
// proves correct, not a re-derivation of the wire format itself (that stays test_view_wire_soa_
// roundtrip.cpp's job). Per Milestone 1's decision (plan doc Progress Log), this deliberately
// reuses that test's fixture rather than resurrecting the lost undertow-real-writer cross-check
// (which belongs to Milestone B, where it's unavoidable).

namespace {

struct WB {
    std::vector<std::byte> b;
    void u8(uint8_t v)  { b.push_back(std::byte{v}); }
    void u32(uint32_t v){ u8(v&0xFF); u8((v>>8)&0xFF); u8((v>>16)&0xFF); u8((v>>24)&0xFF); }
    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
    void f32(float v)   { uint32_t u; std::memcpy(&u,&v,4); u32(u); }
    void str(const std::string& s){ u32(static_cast<uint32_t>(s.size())); for(char ch: s) u8(static_cast<uint8_t>(ch)); }
};

// Byte-for-byte identical to test_view_wire_soa_roundtrip.cpp's CanonicalSoaWire -- same fixture,
// same decoded-value expectations (tick=7, bodyCount=12, activeLensName="Ops", activeLensCount=4,
// factions=[Reds/0.5/T/T/F/T, ""/0/F/F/F/F, Greens/0.75/F/T/T/F], events=[war@40, truce@99]).
std::vector<std::byte> CanonicalSoaWire(uint32_t version) {
    WB w;
    w.u8('U'); w.u8('T'); w.u8('V'); w.u8('A');
    w.u32(version);
    w.u32(6);                      // top-field count
    w.i32(7); w.i32(12); w.str("Ops"); w.i32(4);

    // factions: SoA, declared column order name/grievance/focused/known/inLens/recentChanged
    w.u32(3);                      // row count
    w.u32(0); w.u32(4); w.u32(4); w.u32(10);
    for (char ch : std::string("RedsGreens")) w.u8(static_cast<uint8_t>(ch));
    w.f32(0.5f); w.f32(0.0f); w.f32(0.75f);
    w.u8(1); w.u8(0); w.u8(0);
    w.u8(1); w.u8(0); w.u8(1);
    w.u8(0); w.u8(0); w.u8(1);
    w.u8(1); w.u8(0); w.u8(0);

    // events: SoA, declared column order kind/tick
    w.u32(2);
    w.u32(0); w.u32(3); w.u32(8);
    for (char ch : std::string("wartruce")) w.u8(static_cast<uint8_t>(ch));
    w.i32(40); w.i32(99);

    return w.b;
}

}  // namespace

TEST(TypedAccessorEmitter, GeneratedAccessorsMatchRawStoreDecodedValues) {
    const auto& blob = Vixen::Views::kHudBlob;
    ViewStore store(blob, blob.version);
    auto wire = CanonicalSoaWire(blob.version);

    ASSERT_TRUE(ViewWireReaderSoa::Apply(wire, store));

    Vixen::Views::HudSection hud(store);

    // Scalars -- must equal the raw-API test's proven values exactly.
    EXPECT_EQ(hud.tick(), 7);
    EXPECT_EQ(hud.bodyCount(), 12);
    EXPECT_EQ(hud.activeLensName(), "Ops");
    EXPECT_EQ(hud.activeLensCount(), 4);

    // factions array (Hud declares TWO struct-array fields -- factions + events -- so the
    // generated getters are namespaced factions_<elem>()/events_<elem>() and count is split into
    // count_factions()/count_events(), per TypedAccessorEmitter's multi-array-field handling).
    ASSERT_EQ(hud.count_factions(), 3u);
    EXPECT_EQ(hud.factions_name(0), "Reds");
    EXPECT_FLOAT_EQ(hud.factions_grievance(0), 0.5f);
    EXPECT_TRUE (hud.factions_focused(0));
    EXPECT_TRUE (hud.factions_known(0));
    EXPECT_FALSE(hud.factions_inLens(0));
    EXPECT_TRUE (hud.factions_recentChanged(0));

    EXPECT_EQ(hud.factions_name(1), "");   // non-vacuous: empty-string mid-column row
    EXPECT_FLOAT_EQ(hud.factions_grievance(1), 0.0f);
    EXPECT_FALSE(hud.factions_focused(1));
    EXPECT_FALSE(hud.factions_known(1));
    EXPECT_FALSE(hud.factions_inLens(1));
    EXPECT_FALSE(hud.factions_recentChanged(1));

    EXPECT_EQ(hud.factions_name(2), "Greens");
    EXPECT_FLOAT_EQ(hud.factions_grievance(2), 0.75f);
    EXPECT_FALSE(hud.factions_focused(2));
    EXPECT_TRUE (hud.factions_known(2));
    EXPECT_TRUE (hud.factions_inLens(2));
    EXPECT_FALSE(hud.factions_recentChanged(2));

    // events array
    ASSERT_EQ(hud.count_events(), 2u);
    EXPECT_EQ(hud.events_kind(0), "war");
    EXPECT_EQ(hud.events_tick(0), 40);
    EXPECT_EQ(hud.events_kind(1), "truce");
    EXPECT_EQ(hud.events_tick(1), 99);
}

namespace {

// Wire bytes for a single-field VectorProof model: header (UTVA + version + fieldCount=1) then 3
// consecutive F32s (x,y,z) for the one Vector-kind field -- byte-for-byte what
// ViewWireReaderSoa.cpp's ViewKind::Vector case (and the C# ViewWriterEmitter/ViewWireFormat's
// EmitVector) both produce/expect. Non-trivial, non-zero-in-every-component value (1.5, -2.25, 3.0)
// so a wrong-component/mis-swapped-axis bug shows up as a wrong VALUE, not just "it didn't crash".
std::vector<std::byte> VectorProofWire(uint32_t version, float x, float y, float z) {
    WB w;
    w.u8('U'); w.u8('T'); w.u8('V'); w.u8('A');
    w.u32(version);
    w.u32(1);           // top-field count
    w.f32(x); w.f32(y); w.f32(z);
    return w.b;
}

}  // namespace

// Milestone 2.4b proof (View Contract Inc-5b): full round-trip for a genuine ViewKind::Vector
// field -- decode via ViewWireReaderSoa::Apply (the new Vector decode case) into a ViewStore, then
// read back via the NEW GENERATED Vixen::Views::VectorProofSection accessor (TypedAccessorEmitter's
// new Vector-field getter, returning the header-local Vec3f). Proves the two new C++ decode/read
// halves (ViewBlob.h's ViewKind::Vector + ViewStore's Vector-kind ScalarSlot + the reader's Vector
// case + the emitted accessor) agree end-to-end on a non-trivial, non-zero-in-every-component value.
TEST(TypedAccessorEmitter, VectorFieldRoundTripsExactly) {
    const auto& blob = Vixen::Views::kVectorProofBlob;
    ViewStore store(blob, blob.version);
    auto wire = VectorProofWire(blob.version, 1.5f, -2.25f, 3.0f);

    ASSERT_TRUE(ViewWireReaderSoa::Apply(wire, store));

    Vixen::Views::VectorProofSection proof(store);
    Vixen::Views::Vec3f v = proof.position();

    EXPECT_FLOAT_EQ(v.x, 1.5f);
    EXPECT_FLOAT_EQ(v.y, -2.25f);
    EXPECT_FLOAT_EQ(v.z, 3.0f);
}
