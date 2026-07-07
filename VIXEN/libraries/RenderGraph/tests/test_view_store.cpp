#include <gtest/gtest.h>
#include "Ui/ViewStore.h"
using namespace Vixen::RenderGraph;

static constexpr ViewFieldDesc kElem[] = {
    {"grievance", ViewKind::Float, {}}, {"focused", ViewKind::Bool, {}} };
static constexpr ViewFieldDesc kFields[] = {
    {"tick", ViewKind::Int, {}},
    {"name", ViewKind::String, {}},
    {"factions", ViewKind::ArrayOfStruct, kElem} };
static constexpr ViewBlob kBlob = {"hud", kFields, 0x1111u};

TEST(ViewStore, ScalarSetAndReadBack) {
    ViewStore s(kBlob, 0x1111u);
    s.SetScalar("tick", ViewValue::I(42));
    s.SetScalar("name", ViewValue::S("Reds"));
    EXPECT_EQ(*static_cast<int*>(s.ScalarSlotPtr(0)), 42);
    EXPECT_EQ(*static_cast<Rml::String*>(s.ScalarSlotPtr(1)), "Reds");
    EXPECT_EQ(s.Version(), 0x1111u);
}

TEST(ViewStore, ArrayResizeAndRowSet) {
    ViewStore s(kBlob, 0x1111u);
    auto rows = s.ResizeArray("factions", 2);
    rows.Set(0, "grievance", ViewValue::F(0.7f));
    rows.Set(0, "focused", ViewValue::B(true));
    rows.Set(1, "grievance", ViewValue::F(0.1f));
    auto& arr = s.Array(2);
    ASSERT_EQ(arr.size(), 2u);
    EXPECT_FLOAT_EQ(arr[0].Cell(0).f, 0.7f);
    EXPECT_TRUE(arr[0].Cell(1).b);
}

TEST(ViewStore, RejectsWrongNameAndKind) {
    ViewStore s(kBlob, 0x1111u);
    s.SetScalar("nope", ViewValue::I(1));       // unknown name -> no-op (logged)
    s.SetScalar("tick", ViewValue::S("bad"));   // wrong kind -> no-op (logged)
    EXPECT_EQ(*static_cast<int*>(s.ScalarSlotPtr(0)), 0);  // unchanged
}
