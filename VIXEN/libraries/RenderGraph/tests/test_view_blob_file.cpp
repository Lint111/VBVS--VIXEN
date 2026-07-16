#include <gtest/gtest.h>
#include "Ui/ViewBlobFile.h"
using namespace Vixen::RenderGraph;

static const char* kGood =
    "# viewblob v1\n"
    "model hud\n"
    "version 0xABCD1234\n"
    "elem HudFaction\n"
    "  grievance float\n"
    "  focused bool\n"
    "field tick int\n"
    "field factions array HudFaction\n";

TEST(ViewBlobFile, ParsesModelVersionFieldsAndElems) {
    auto f = ViewBlobFile::Parse(kGood);
    ASSERT_TRUE(f.has_value());
    const ViewBlob& b = f->Blob();
    EXPECT_EQ(b.model, "hud");
    EXPECT_EQ(b.version, 0xABCD1234u);
    ASSERT_EQ(b.fields.size(), 2u);
    EXPECT_EQ(b.fields[0].name, "tick");
    EXPECT_EQ(b.fields[0].kind, ViewKind::Int);
    EXPECT_EQ(b.fields[1].name, "factions");
    EXPECT_EQ(b.fields[1].kind, ViewKind::ArrayOfStruct);
    ASSERT_EQ(b.fields[1].elem.size(), 2u);
    EXPECT_EQ(b.fields[1].elem[0].name, "grievance");
    EXPECT_EQ(b.fields[1].elem[0].kind, ViewKind::Float);
}

TEST(ViewBlobFile, RejectsUnknownKind) {
    auto f = ViewBlobFile::Parse("model hud\nversion 0x1\nfield tick banana\n");
    EXPECT_FALSE(f.has_value());
}

TEST(ViewBlobFile, RejectsArrayReferencingUndeclaredElem) {
    auto f = ViewBlobFile::Parse("model hud\nversion 0x1\nfield x array Ghost\n");
    EXPECT_FALSE(f.has_value());
}

// View Contract Inc-5b Milestone 2.4b: the "vector" token must parse to ViewKind::Vector for
// BOTH a top-level field line and an indented elem (row) line -- before the fix, ScalarKind had
// no "vector" case at all, so a .viewblob emitted with the new KindTag "vector" string would fail
// to parse entirely (previously KindTag silently mislabeled Vector fields as "int" instead, which
// parsed but was WRONG; this test guards the new token end-to-end, not just the old silent-int
// symptom).
static const char* kVectorBoth =
    "# viewblob v1\n"
    "model orbitpath\n"
    "version 0xABCD1234\n"
    "elem OrbitPoint\n"
    "  t float\n"
    "  position vector\n"
    "field origin vector\n"
    "field points array OrbitPoint\n";

TEST(ViewBlobFile, ParsesVectorTokenForTopLevelAndElemFields) {
    auto f = ViewBlobFile::Parse(kVectorBoth);
    ASSERT_TRUE(f.has_value());
    const ViewBlob& b = f->Blob();
    ASSERT_EQ(b.fields.size(), 2u);
    EXPECT_EQ(b.fields[0].name, "origin");
    EXPECT_EQ(b.fields[0].kind, ViewKind::Vector);
    EXPECT_EQ(b.fields[1].name, "points");
    ASSERT_EQ(b.fields[1].elem.size(), 2u);
    EXPECT_EQ(b.fields[1].elem[1].name, "position");
    EXPECT_EQ(b.fields[1].elem[1].kind, ViewKind::Vector);
}
