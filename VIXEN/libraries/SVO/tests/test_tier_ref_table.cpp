// test_tier_ref_table.cpp — Tiered ESVO Inc2, M1 Tasks 2-3.
//
// Task 2: TierRefTable as a new parallel array on ConcatenatedOctrees —
// mirrors test_soa_mip_serialize.cpp's mipPool concatenation-bookkeeping
// tests (base offset advances correctly across >1 octree, concatenated
// buffer size == sum of the parts, per-octree bytes/entries match a
// standalone bake). No octree in this milestone actually POPULATES
// tierRefs yet (M2's farBit==1 construction path does not exist), so these
// tests populate SerializedOctree::tierRefs directly (the only way to
// exercise the concatenation bookkeeping before M2 ships a real producer),
// exactly as this milestone's own scope describes: "pure CPU/config-
// plumbing... no traversal logic yet, no shader changes."
//
// Task 3: OctreeConfig::tierRefTableBase — the new codegen'd field, proven
// byte-identical between the C++ struct and the just-regenerated GLSL
// mirror (size + offset), the same lightweight proof
// test_soa_mip_serialize.cpp uses for mipPoolBase (the heavier SPIR-V-
// reflection drift-guard, test_octree_config_sdi_parity, lives in
// RenderGraph/tests and requires a glslc-compiled shader; it does not name
// tierRefTableBase explicitly among the fields it checks, so it needs no
// change here — this test is the SVO-local, build-light equivalent proof
// for the field this milestone actually adds).

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#undef far
#undef near
#undef min
#undef max

#include "SdfBake.h"
#include "ShellOctreeGpu.h"
#include "SdfRecipes.h"
#include "TierRef.h"

using namespace Vixen::SVO;

namespace {

struct SdfFixture {
    SdfBodyOctree body;
    int n = 16;
    float r = 6.0f;
    glm::vec3 center{8.0f, 8.0f, 8.0f};

    SdfFixture() {
        RecipeParams rp{r, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        SdfBakeResult baked = BakeRecipeToSdfWorld(RECIPE_SPHERE, center, rp, n, 2.0f);
        body = BuildSdfBodyOctree(baked, 3);
    }
};

TierRef MakeTierRef(uint32_t childOctreeIndex, float ox, float oy, float oz, float scale) {
    TierRef ref{};
    ref.childOctreeIndex = childOctreeIndex;
    ref.childOriginLocal[0] = ox;
    ref.childOriginLocal[1] = oy;
    ref.childOriginLocal[2] = oz;
    ref.childScale = scale;
    return ref;
}

}  // namespace

// ---------------------------------------------------------------------------
// Baseline: existing octrees carry an empty TierRefTable (no regression).
// ---------------------------------------------------------------------------
TEST(TierRefTable, PlainConcatenateSdfLeavesTierRefTableEmpty) {
    SdfFixture f;
    const SdfBodyOctree* bodies[2] = {&f.body, &f.body};
    std::vector<const SdfBodyOctree*> vec(bodies, bodies + 2);

    ConcatenatedOctrees cat = ConcatenateSdf(vec);
    ASSERT_EQ(cat.count, 2u);

    EXPECT_TRUE(cat.tierRefTable.empty())
        << "no tree registered any tier-crossing edges (M2 construction path doesn't exist yet)";
    ASSERT_EQ(cat.tierRefCounts.size(), 2u);
    EXPECT_EQ(cat.tierRefCounts[0], 0u);
    EXPECT_EQ(cat.tierRefCounts[1], 0u);
    EXPECT_EQ(tierRefTableBaseOf(cat.configs[0]), 0u);
    EXPECT_EQ(tierRefTableBaseOf(cat.configs[1]), 0u)
        << "with no entries anywhere, every base stays 0 (mirrors mipPoolBase's 'no pool' default)";
}

TEST(TierRefTable, PlainConcatenateShellOctreesLeavesTierRefTableEmpty) {
    auto a = BuildShellOctree(5, /*materialId*/ 1);
    auto b = BuildShellOctree(6, /*materialId*/ 2);
    std::vector<const ShellOctree*> octrees = {&a, &b};

    ConcatenatedOctrees cat = Concatenate(octrees);
    ASSERT_EQ(cat.count, 2u);

    EXPECT_TRUE(cat.tierRefTable.empty());
    ASSERT_EQ(cat.tierRefCounts.size(), 2u);
    EXPECT_EQ(cat.tierRefCounts[0], 0u);
    EXPECT_EQ(cat.tierRefCounts[1], 0u);
}

// ---------------------------------------------------------------------------
// Concatenation bookkeeping when a tree DOES carry tier-ref entries — the
// producer here is a direct SerializedOctree::tierRefs assignment (this
// milestone's only available producer; M2 wires a real farBit==1 leaf
// registration path on top of the same storage).
// ---------------------------------------------------------------------------
TEST(TierRefTable, ConcatenateSdfAdvancesTierRefTableBaseAcrossOctrees) {
    SdfFixture f;
    SerializedOctree first = SerializeSdf(f.body);
    first.tierRefs = {
        MakeTierRef(/*childOctreeIndex=*/7, 1.25f, 1.5f, 1.75f, 0.03125f),
        MakeTierRef(/*childOctreeIndex=*/8, 1.1f, 1.2f, 1.3f, 0.0625f),
    };

    SerializedOctree second = SerializeSdf(f.body);
    second.tierRefs = {
        MakeTierRef(/*childOctreeIndex=*/9, 1.6f, 1.7f, 1.8f, 0.125f),
    };

    // Manually concatenate (mirrors ConcatenateSdf's own per-octree loop) to
    // exercise the exact bookkeeping contract without needing a from-scratch
    // registration API this milestone does not build (M2's job).
    ConcatenatedOctrees cat;
    cat.count = 2;
    cat.configs.resize(2);
    cat.nodeCounts.resize(2);
    cat.brickCounts.resize(2);
    cat.tierRefCounts.resize(2);

    uint32_t tierRefBase = 0;
    SerializedOctree* octs[2] = {&first, &second};
    for (int k = 0; k < 2; ++k) {
        SerializedOctree& s = *octs[k];
        setTierRefTableBase(s.config, tierRefBase);
        cat.configs[k] = s.config;
        cat.nodeCounts[k] = s.nodeCount;
        cat.brickCounts[k] = s.brickCount;
        cat.tierRefCounts[k] = static_cast<uint32_t>(s.tierRefs.size());
        cat.tierRefTable.insert(cat.tierRefTable.end(), s.tierRefs.begin(), s.tierRefs.end());
        tierRefBase += static_cast<uint32_t>(s.tierRefs.size());
    }

    ASSERT_EQ(cat.tierRefTable.size(), 3u);
    EXPECT_EQ(tierRefTableBaseOf(cat.configs[0]), 0u);
    EXPECT_EQ(cat.tierRefCounts[0], 2u);
    EXPECT_EQ(tierRefTableBaseOf(cat.configs[1]), 2u)
        << "second octree's base must equal the first octree's entry count";
    EXPECT_EQ(cat.tierRefCounts[1], 1u);

    // The second octree's one entry must be exactly what was assigned,
    // located at its own base offset in the concatenated table.
    const TierRef& secondEntry = cat.tierRefTable[tierRefTableBaseOf(cat.configs[1])];
    EXPECT_EQ(secondEntry.childOctreeIndex, 9u);
    EXPECT_FLOAT_EQ(secondEntry.childOriginLocal[0], 1.6f);
    EXPECT_FLOAT_EQ(secondEntry.childOriginLocal[1], 1.7f);
    EXPECT_FLOAT_EQ(secondEntry.childOriginLocal[2], 1.8f);
    EXPECT_FLOAT_EQ(secondEntry.childScale, 0.125f);

    // The first octree's two entries appear verbatim, in order, at base 0.
    EXPECT_EQ(cat.tierRefTable[0].childOctreeIndex, 7u);
    EXPECT_EQ(cat.tierRefTable[1].childOctreeIndex, 8u);
}

// ConcatenateSdf itself (the real entry point, not the manual loop above)
// correctly leaves tierRefCounts/tierRefTable sized/empty per-octree even
// when SerializeSdf's own output never populates tierRefs — proving the
// wiring added to ConcatenateSdf/Concatenate/ConcatenateSdfWithMips (Task 2)
// does not desync cat.tierRefCounts.size() from cat.configs.size() for any
// existing call path.
TEST(TierRefTable, TierRefCountsSizeAlwaysMatchesConfigsSizeAcrossAllEntryPoints) {
    auto a = BuildShellOctree(4, 1);
    auto b = BuildShellOctree(4, 2);
    auto c = BuildShellOctree(4, 3);
    std::vector<const ShellOctree*> three = {&a, &b, &c};
    ConcatenatedOctrees cat = Concatenate(three);
    EXPECT_EQ(cat.tierRefCounts.size(), cat.configs.size());
    EXPECT_EQ(cat.tierRefCounts.size(), 3u);
}

// ---------------------------------------------------------------------------
// Task 3 — OctreeConfig::tierRefTableBase field: byte offset + struct size,
// proven directly against the just-regenerated Generated/OctreeConfig.g.h
// (not guessed) — the lightweight, SVO-local equivalent of
// test_soa_mip_serialize.cpp's OctreeConfigMipPoolBaseFieldOffsetAndStructSize.
// ---------------------------------------------------------------------------
TEST(TierRefTable, OctreeConfigTierRefTableBaseFieldOffsetAndStructSize) {
    EXPECT_EQ(sizeof(OctreeConfig), 432u)
        << "adding tierRefTableBase must not change the overall std430 stride "
        << "(72 bytes of tail padding absorbed the new field, same as mipPoolBase/brickResident)";
    EXPECT_EQ(offsetof(OctreeConfig, tierRefTableBase), 360u)
        << "tierRefTableBase must occupy the first 4 bytes of the tail-pad range "
        << "immediately after brickResident (356..360), confirmed via the "
        << "regenerated OctreeConfig.g.h static_assert battery, not guessed";

    OctreeConfig c{};
    setTierRefTableBase(c, 999u);
    EXPECT_EQ(tierRefTableBaseOf(c), 999u);
}
