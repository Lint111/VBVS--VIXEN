#include <gtest/gtest.h>
#include "Ui/ViewBlob.h"
using namespace Vixen::RenderGraph;

TEST(ViewBlob, ConstexprDescriptorHoldsFieldsInOrder) {
    static constexpr ViewFieldDesc elem[] = {
        {"grievance", ViewKind::Float, {}}, {"focused", ViewKind::Bool, {}} };
    static constexpr ViewFieldDesc fields[] = {
        {"tick", ViewKind::Int, {}},
        {"factions", ViewKind::ArrayOfStruct, elem} };
    static constexpr ViewBlob blob = {"hud", fields, 0xABCD1234u};
    EXPECT_EQ(blob.model, "hud");
    EXPECT_EQ(blob.version, 0xABCD1234u);
    ASSERT_EQ(blob.fields.size(), 2u);
    EXPECT_EQ(blob.fields[0].name, "tick");
    EXPECT_EQ(blob.fields[1].kind, ViewKind::ArrayOfStruct);
    ASSERT_EQ(blob.fields[1].elem.size(), 2u);
    EXPECT_EQ(blob.fields[1].elem[0].name, "grievance");
}

TEST(ViewValue, KindAcceptsMatchingTagOnly) {
    EXPECT_TRUE (KindAcceptsValue(ViewKind::Int,    ViewValue::I(3)));
    EXPECT_FALSE(KindAcceptsValue(ViewKind::Int,    ViewValue::S("x")));
    EXPECT_TRUE (KindAcceptsValue(ViewKind::String, ViewValue::S("x")));
    EXPECT_FALSE(KindAcceptsValue(ViewKind::ArrayOfStruct, ViewValue::I(1)));
}
