/**
 * @file test_residency_default.cpp
 * @brief Lazy-Procedural-Delta-Baseline Inc0 M2 Task 4 — DeriveResidencyDefault/
 *        IsOctreeMipCapable (ResidencyDefault.h). Pure CPU logic, no device needed.
 */
#include <gtest/gtest.h>
#include "ResidencyDefault.h"
#include "Recipe/RecipeRegistry.h"
#include "Recipe/RecipeBaker.h"
#include "Recipe/SdfInstruction.h"
#include "ShellOctree.h"       // BuildShellOctree, Concatenate (binary path)
#include "ShellDerive.h"       // DeriveShellPool (Task 4b fixtures)
#include <glm/glm.hpp>

using namespace Vixen::SVO;
using Recipe::SdfInstruction;
using Recipe::SdfOpCode;

static SdfInstruction sphereInstr(glm::vec3 c, float r) {
    SdfInstruction in{};
    in.opCode  = (uint8_t)SdfOpCode::Sphere;
    in.data[0] = c.x; in.data[1] = c.y; in.data[2] = c.z; in.data[3] = r;
    return in;
}

// ---------------------------------------------------------------------------
// Mip-capable pool (BakeRegistryToPool -> ConcatenateSdfWithMips) -> lazy.
// ---------------------------------------------------------------------------
TEST(ResidencyDefault, AllMipCapablePoolDerivesLazy) {
    RecipeRegistry reg;
    RecipeRegistry::RecipeEntry a{};
    a.bytecode = { sphereInstr(glm::vec3(0, 0, 0), 20.0f) };
    RecipeRegistry::RecipeEntry b{};
    b.bytecode = { sphereInstr(glm::vec3(0, 0, 0), 26.0f) };
    ASSERT_EQ(reg.Register(10u, a), RecipeRegistry::RegisterResult::Ok);
    ASSERT_EQ(reg.Register(11u, b), RecipeRegistry::RegisterResult::Ok);

    RecipeBakeConfig cfg{};  // defaults: BakeRegistryToPool -> ConcatenateSdfWithMips (M1)
    RecipeBakeResult r = BakeRegistryToPool(reg, cfg);
    ASSERT_TRUE(r.ok) << r.err;
    ASSERT_EQ(r.pool.count, 2u);
    ASSERT_GT(r.pool.mipPool.size(), 0u) << "fixture must actually carry mips";

    EXPECT_TRUE(IsOctreeMipCapable(r.pool, 0u));
    EXPECT_TRUE(IsOctreeMipCapable(r.pool, 1u));
    EXPECT_FALSE(DeriveResidencyDefault(r.pool))
        << "every tree mip-capable -> residency default should be LAZY (false)";
}

// ---------------------------------------------------------------------------
// All-binary shell-octree pool (Concatenate, no channelPool/mipPool at all) -> eager.
// ---------------------------------------------------------------------------
TEST(ResidencyDefault, BinaryShellPoolDerivesEager) {
    ShellOctree a = BuildShellOctree(/*depth=*/4, /*materialId=*/1u);
    ShellOctree b = BuildShellOctree(/*depth=*/4, /*materialId=*/2u);
    std::vector<const ShellOctree*> ptrs = { &a, &b };
    ConcatenatedOctrees pool = Concatenate(ptrs);

    ASSERT_EQ(pool.count, 2u);
    EXPECT_TRUE(pool.channelPool.empty()) << "binary shells carry no SDF channel pool";
    EXPECT_TRUE(pool.mipPool.empty())     << "Concatenate (non-SDF) never bakes mips";

    EXPECT_FALSE(IsOctreeMipCapable(pool, 0u));
    EXPECT_FALSE(IsOctreeMipCapable(pool, 1u));
    EXPECT_TRUE(DeriveResidencyDefault(pool))
        << "binary/non-mip-capable pool -> residency default should stay EAGER (true), "
           "matching pre-M2 behavior exactly";
}

// ---------------------------------------------------------------------------
// Mixed pool (one mip-capable tree + one non-mip-capable tree) -> eager (the
// weaker tree has no mip fallback, so the WHOLE pool must stay eager or that
// tree's leaves boot invisible).
// ---------------------------------------------------------------------------
TEST(ResidencyDefault, MixedPoolDerivesEager) {
    RecipeRegistry reg;
    RecipeRegistry::RecipeEntry a{};
    a.bytecode = { sphereInstr(glm::vec3(0, 0, 0), 20.0f) };
    ASSERT_EQ(reg.Register(1u, a), RecipeRegistry::RegisterResult::Ok);
    RecipeBakeConfig cfg{};
    RecipeBakeResult r = BakeRegistryToPool(reg, cfg);
    ASSERT_TRUE(r.ok) << r.err;
    ASSERT_EQ(r.pool.count, 1u);
    ASSERT_TRUE(IsOctreeMipCapable(r.pool, 0u));

    // Simulate "mixed": append a config with channelCount==0 (binary-shaped) as tree index 1,
    // matching what a mixed provided-pool would look like — no mipPool slice for it at all.
    ConcatenatedOctrees mixed = r.pool;
    OctreeConfig binaryCfg{};  // channelCount defaults to 0
    mixed.configs.push_back(binaryCfg);
    mixed.nodeCounts.push_back(8u);
    mixed.brickCounts.push_back(0u);
    mixed.tierRefCounts.push_back(0u);
    mixed.count = 2u;

    EXPECT_TRUE(IsOctreeMipCapable(mixed, 0u));
    EXPECT_FALSE(IsOctreeMipCapable(mixed, 1u));
    EXPECT_TRUE(DeriveResidencyDefault(mixed))
        << "one non-mip-capable tree in the pool must keep the WHOLE pool eager";
}

// ---------------------------------------------------------------------------
// Empty pool (count==0, e.g. before EnsureOctreesBuilt has run) -> eager, matching
// the constructor default (never derives laziness from nothing).
// ---------------------------------------------------------------------------
TEST(ResidencyDefault, EmptyPoolDerivesEager) {
    ConcatenatedOctrees empty{};
    EXPECT_EQ(empty.count, 0u);
    EXPECT_TRUE(DeriveResidencyDefault(empty));
}

// ---------------------------------------------------------------------------
// channelCount>0 but no baked mip data (hand-built pool, e.g. a test fixture that
// never called BakeAndAttachMipPool) must NOT be reported mip-capable — a lazy
// leaf with channels but no mip slice would boot invisible, not grey.
// ---------------------------------------------------------------------------
TEST(ResidencyDefault, ChannelsWithoutBakedMipsIsNotMipCapable) {
    ConcatenatedOctrees pool{};
    pool.count = 1u;
    OctreeConfig cfg{};
    cfg.channelCount = 2u;  // claims live channels...
    pool.configs.push_back(cfg);
    pool.nodeCounts.push_back(16u);
    pool.brickCounts.push_back(0u);
    pool.tierRefCounts.push_back(0u);
    // ...but pool.mipPool stays empty — no bake ever ran.
    ASSERT_TRUE(pool.mipPool.empty());

    EXPECT_FALSE(IsOctreeMipCapable(pool, 0u));
    EXPECT_TRUE(DeriveResidencyDefault(pool));
}

// ===========================================================================
// M2 Task 4b — StampAndSelectActiveConfigs (shell-cache config reconciliation
// on residency grant). Multi-octree: octree index >=1's poolBrickBase differs
// between the source pool and the compact shell pool, so picking the wrong
// vector is a real correctness bug, not just an addressing curiosity.
// ===========================================================================

// No shell cache derived (shellCache_[0]/[1] both default-empty, matching a
// binary/Procedural body or a Stored-SDF body before DeriveShellCache ever ran)
// -> must select the SOURCE configs, unchanged from pre-M2 behavior.
TEST(StampAndSelectActiveConfigs, NoShellCacheSelectsSourceConfigs) {
    ConcatenatedOctrees source{};
    source.count = 2u;
    OctreeConfig c0{}; c0.brickResident = 0u;
    OctreeConfig c1{}; c1.brickResident = 0u;
    source.configs = { c0, c1 };

    ShellPool shellCache[2];  // both default-constructed: compact.configs empty
    ASSERT_TRUE(shellCache[0].compact.configs.empty());

    std::vector<OctreeConfig>* active = StampAndSelectActiveConfigs(source, shellCache);

    EXPECT_EQ(active, &source.configs);
    for (const auto& cfg : source.configs) {
        EXPECT_TRUE(brickResidentOf(cfg)) << "source configs must be stamped resident";
    }
}

// A shell cache WAS derived (both CPU double-buffer slots populated, as
// CreateShellBuffers leaves them) -> must select the COMPACT configs (slot 0),
// with brickResident stamped into BOTH slots (so whichever slot ExecuteImpl
// next reads — read/write alternate by frame parity — is already correct),
// and the SOURCE configs must be left untouched (the bug this milestone fixes:
// re-uploading source configs would clobber CreateShellBuffers' poolBrickBase
// rewrite for octree index >=1).
TEST(StampAndSelectActiveConfigs, ShellCachePresentSelectsCompactConfigsBothSlotsStamped) {
    ConcatenatedOctrees source{};
    source.count = 2u;
    OctreeConfig srcC0{}; srcC0.poolBrickBase = 0u;   srcC0.brickResident = 0u;
    OctreeConfig srcC1{}; srcC1.poolBrickBase = 999u; srcC1.brickResident = 0u;  // source's own base
    source.configs = { srcC0, srcC1 };

    ShellPool shellCache[2];
    for (int slot = 0; slot < 2; ++slot) {
        OctreeConfig cc0{}; cc0.poolBrickBase = 0u;   cc0.brickResident = 0u;
        OctreeConfig cc1{}; cc1.poolBrickBase = 42u;  cc1.brickResident = 0u;  // COMPACT's re-packed base (different from source's 999)
        shellCache[slot].compact.configs = { cc0, cc1 };
    }

    std::vector<OctreeConfig>* active = StampAndSelectActiveConfigs(source, shellCache);

    EXPECT_EQ(active, &shellCache[0].compact.configs)
        << "must select the COMPACT view CreateShellBuffers last wrote to binding-5, not the source";

    // Both CPU double-buffer slots stamped resident...
    for (const auto& cfg : shellCache[0].compact.configs) {
        EXPECT_TRUE(brickResidentOf(cfg));
    }
    for (const auto& cfg : shellCache[1].compact.configs) {
        EXPECT_TRUE(brickResidentOf(cfg));
    }
    // ...and the compact poolBrickBase for octree index 1 is UNCHANGED by the stamp
    // (still 42, the compact re-pack — NOT the source's 999), proving the compact
    // addressing survives a residency grant untouched.
    EXPECT_EQ(active->at(1).poolBrickBase, 42u);

    // Source configs are ALSO stamped (kept in sync for completeness/future readers)
    // but their poolBrickBase (999) is untouched — no cross-contamination either way.
    EXPECT_TRUE(brickResidentOf(source.configs[1]));
    EXPECT_EQ(source.configs[1].poolBrickBase, 999u);
}
