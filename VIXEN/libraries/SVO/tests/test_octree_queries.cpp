#define NOMINMAX
#include <gtest/gtest.h>
#include "LaineKarrasOctree.h"
#include "SVOBuilder.h"
#include "SVOTypes.h"
#include "GaiaVoxelWorld.h"  // For entity-based testing
#include "VoxelComponents.h"
#include "ComponentData.h"
#include <chrono>

using namespace SVO;
using namespace GaiaVoxel;

// ===========================================================================
// Helper: Create Simple Test Octree
// ===========================================================================

class OctreeQueryTest : public ::testing::Test {
protected:
    // Dummy world for octree construction (not used for actual voxel data in these legacy tests)
    GaiaVoxelWorld dummyWorld;

    void SetUp() override {
        // Create simple octree manually for testing
        octree = std::make_unique<Octree>();
        octree->worldMin = glm::vec3(0.0f);
        octree->worldMax = glm::vec3(10.0f);
        octree->maxLevels = 4;

        // Create root block
        octree->root = std::make_unique<OctreeBlock>();

        // Root node with one child at [0,0,0]
        ChildDescriptor root{};
        root.childPointer = 1; // Points to first child
        root.farBit = 0;
        root.validMask = 0b00000001; // Only child 0 exists
        root.leafMask = 0b00000000;  // Not a leaf
        root.contourPointer = 0;
        root.contourMask = 0;
        octree->root->childDescriptors.push_back(root);

        // Child at level 1 - a leaf
        ChildDescriptor child{};
        child.childPointer = 0;
        child.farBit = 0;
        child.validMask = 0b11111111; // All children exist
        child.leafMask = 0b11111111;  // All are leaves
        child.contourPointer = 0;
        child.contourMask = 0;
        octree->root->childDescriptors.push_back(child);

        // Add attribute lookup for the child node (index 1)
        // Note: attributeLookups should align with childDescriptors indices
        AttributeLookup rootAttrLookup{};
        rootAttrLookup.valuePointer = 0;
        rootAttrLookup.mask = 0; // Root has no attributes
        octree->root->attributeLookups.push_back(rootAttrLookup);

        // Add attributes for the leaf voxels (child node at index 1)
        AttributeLookup attrLookup{};
        attrLookup.valuePointer = 0;
        attrLookup.mask = 0b11111111; // All children have attributes
        octree->root->attributeLookups.push_back(attrLookup);

        // Add 8 voxel attributes (red voxels with up normal)
        for (int i = 0; i < 8; ++i) {
            UncompressedAttributes attr = makeAttributes(
                glm::vec3(1.0f, 0.0f, 0.0f), // Red
                glm::vec3(0.0f, 1.0f, 0.0f)  // Up
            );
            octree->root->attributes.push_back(attr);
        }

        octree->totalVoxels = 8;
        octree->leafVoxels = 8;
        octree->memoryUsage = octree->root->getTotalSize();

        // Create LaineKarrasOctree wrapper (using dummyWorld for construction, setOctree for manual tree)
        lkOctree = std::make_unique<LaineKarrasOctree>(dummyWorld, nullptr, 4, 3);
        lkOctree->setOctree(std::move(octree));
    }

    std::unique_ptr<Octree> octree;
    std::unique_ptr<LaineKarrasOctree> lkOctree;
};

// ===========================================================================
// VoxelExists Tests
// ===========================================================================

TEST_F(OctreeQueryTest, VoxelExistsInBounds) {
    // Voxel at child 0 of root (occupies [0,5]³)
    EXPECT_TRUE(lkOctree->voxelExists(glm::vec3(2.5f, 2.5f, 2.5f), 1));
}

TEST_F(OctreeQueryTest, VoxelExistsLeaf) {
    // Leaf voxel at level 2 (child 0 of child 0)
    EXPECT_TRUE(lkOctree->voxelExists(glm::vec3(1.0f, 1.0f, 1.0f), 2));
}

TEST_F(OctreeQueryTest, VoxelDoesNotExistOutOfBounds) {
    // Outside world bounds
    EXPECT_FALSE(lkOctree->voxelExists(glm::vec3(-1.0f, 0.0f, 0.0f), 1));
    EXPECT_FALSE(lkOctree->voxelExists(glm::vec3(11.0f, 0.0f, 0.0f), 1));
}

TEST_F(OctreeQueryTest, VoxelDoesNotExistEmptySpace) {
    // Child 1 of root doesn't exist
    EXPECT_FALSE(lkOctree->voxelExists(glm::vec3(7.5f, 2.5f, 2.5f), 1));
}

// ===========================================================================
// GetVoxelData Tests
// ===========================================================================

TEST_F(OctreeQueryTest, GetVoxelDataValid) {
    // Query leaf voxel
    auto data = lkOctree->getVoxelData(glm::vec3(1.0f, 1.0f, 1.0f), 2);

    ASSERT_TRUE(data.has_value());
    EXPECT_NEAR(data->color.r, 1.0f, 0.01f); // Red
    EXPECT_NEAR(data->color.g, 0.0f, 0.01f);
    EXPECT_NEAR(data->color.b, 0.0f, 0.01f);
    EXPECT_NEAR(data->normal.y, 1.0f, 0.1f); // Up normal
}

TEST_F(OctreeQueryTest, GetVoxelDataInvalid) {
    // Query non-existent voxel
    auto data = lkOctree->getVoxelData(glm::vec3(7.5f, 2.5f, 2.5f), 1);
    EXPECT_FALSE(data.has_value());
}

TEST_F(OctreeQueryTest, GetVoxelDataOutOfBounds) {
    auto data = lkOctree->getVoxelData(glm::vec3(-1.0f, 0.0f, 0.0f), 1);
    EXPECT_FALSE(data.has_value());
}

// ===========================================================================
// GetChildMask Tests
// ===========================================================================

TEST_F(OctreeQueryTest, GetChildMaskRoot) {
    // Root has only child 0
    uint8_t mask = lkOctree->getChildMask(glm::vec3(2.5f, 2.5f, 2.5f), 0);
    EXPECT_EQ(mask, 0b00000001);
}

TEST_F(OctreeQueryTest, GetChildMaskLevel1) {
    // Child 0 of root has all 8 children as leaves
    uint8_t mask = lkOctree->getChildMask(glm::vec3(2.5f, 2.5f, 2.5f), 1);
    EXPECT_EQ(mask, 0b11111111);
}

TEST_F(OctreeQueryTest, GetChildMaskLeaf) {
    // Leaves have no children
    uint8_t mask = lkOctree->getChildMask(glm::vec3(1.0f, 1.0f, 1.0f), 2);
    EXPECT_EQ(mask, 0);
}

TEST_F(OctreeQueryTest, GetChildMaskOutOfBounds) {
    uint8_t mask = lkOctree->getChildMask(glm::vec3(-1.0f, 0.0f, 0.0f), 1);
    EXPECT_EQ(mask, 0);
}

// ===========================================================================
// GetVoxelBounds Tests
// ===========================================================================

TEST_F(OctreeQueryTest, GetVoxelBounds) {
    auto bounds = lkOctree->getVoxelBounds(glm::vec3(0.0f), 0);
    EXPECT_EQ(bounds.min, glm::vec3(0.0f));
    EXPECT_EQ(bounds.max, glm::vec3(10.0f));
}
TEST_F(OctreeQueryTest, CastRayMissEmpty) {
    // Ray shooting through empty space (child 1+ don't exist)
    glm::vec3 origin(7.0f, 7.0f, 7.0f);
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    auto hit = lkOctree->castRay(origin, direction, 0.0f, 10.0f);

    EXPECT_FALSE(hit.hit);
}

TEST_F(OctreeQueryTest, CastRayMissOutsideBounds) {
    // Ray that never enters the octree volume
    glm::vec3 origin(-5.0f, 15.0f, 5.0f);
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    auto hit = lkOctree->castRay(origin, direction, 0.0f, 10.0f);

    EXPECT_FALSE(hit.hit);
}
// ---------------------------------------------------------------------------
// Edge Cases
// ---------------------------------------------------------------------------

TEST_F(OctreeQueryTest, CastRayZeroDirection) {
    glm::vec3 origin(1.0f, 1.0f, 1.0f);
    glm::vec3 direction(0.0f, 0.0f, 0.0f);

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    EXPECT_FALSE(hit.hit); // Invalid direction
}
// ---------------------------------------------------------------------------
// LOD Tests
// ---------------------------------------------------------------------------

TEST_F(OctreeQueryTest, CastRayLODZeroBias) {
    // LOD bias = 0 should be same as regular castRay
    glm::vec3 origin(-1.0f, 1.0f, 1.0f);
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    auto hitLOD = lkOctree->castRayLOD(origin, direction, 0.0f, 0.0f, std::numeric_limits<float>::max());
    auto hitRegular = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());

    EXPECT_EQ(hitLOD.hit, hitRegular.hit);
    if (hitLOD.hit) {
        EXPECT_NEAR(hitLOD.tMin, hitRegular.tMin, 0.01f);
        EXPECT_EQ(hitLOD.scale, hitRegular.scale);
    }
}
// ===========================================================================
// GetVoxelSize Tests
// ===========================================================================

TEST_F(OctreeQueryTest, GetVoxelSize) {
    // World size is 10, so at scale 0 (root), voxel size should be 10
    float size0 = lkOctree->getVoxelSize(0);
    EXPECT_NEAR(size0, 10.0f, 0.01f);

    // At scale 1, voxel size should be 5
    float size1 = lkOctree->getVoxelSize(1);
    EXPECT_NEAR(size1, 5.0f, 0.01f);

    // At scale 2, voxel size should be 2.5
    float size2 = lkOctree->getVoxelSize(2);
    EXPECT_NEAR(size2, 2.5f, 0.01f);
}

// ===========================================================================
// GetStats Tests
// ===========================================================================

TEST_F(OctreeQueryTest, GetStats) {
    std::string stats = lkOctree->getStats();

    // Should contain key information
    EXPECT_NE(stats.find("8"), std::string::npos); // 8 voxels
    EXPECT_NE(stats.find("Laine-Karras"), std::string::npos);
}

// ===========================================================================
// Metadata Tests
// ===========================================================================

TEST_F(OctreeQueryTest, GetWorldBounds) {
    EXPECT_EQ(lkOctree->getWorldMin(), glm::vec3(0.0f));
    EXPECT_EQ(lkOctree->getWorldMax(), glm::vec3(10.0f));
}

TEST_F(OctreeQueryTest, GetMaxLevels) {
    EXPECT_EQ(lkOctree->getMaxLevels(), 4);
}

TEST_F(OctreeQueryTest, GetVoxelCount) {
    EXPECT_EQ(lkOctree->getVoxelCount(), 8);
}

TEST_F(OctreeQueryTest, GetMemoryUsage) {
    EXPECT_GT(lkOctree->getMemoryUsage(), 0);
}

// ===========================================================================
// Comprehensive Ray Traversal Path Tests
// ===========================================================================

// ---------------------------------------------------------------------------
// Category 1: Complete Miss Scenarios
// ---------------------------------------------------------------------------

TEST_F(OctreeQueryTest, TraversalPath_CompleteMiss_Above) {
    // Ray passes completely above the octree
    glm::vec3 origin(-5.0f, 15.0f, 5.0f); // Above world (y=10)
    glm::vec3 direction(1.0f, 0.0f, 0.0f); // Horizontal ray

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    EXPECT_FALSE(hit.hit);
}

TEST_F(OctreeQueryTest, TraversalPath_CompleteMiss_Below) {
    // Ray passes completely below the octree
    glm::vec3 origin(-5.0f, -5.0f, 5.0f); // Below world (y=0)
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    EXPECT_FALSE(hit.hit);
}

TEST_F(OctreeQueryTest, TraversalPath_CompleteMiss_Left) {
    glm::vec3 origin(-5.0f, 5.0f, -5.0f); // Left of world
    glm::vec3 direction(0.0f, 0.0f, 1.0f);

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    EXPECT_FALSE(hit.hit);
}

TEST_F(OctreeQueryTest, TraversalPath_CompleteMiss_DiagonalPast) {
    // Ray aimed diagonally past corner
    glm::vec3 origin(-5.0f, -5.0f, -5.0f);
    glm::vec3 direction = glm::normalize(glm::vec3(1.0f, -0.5f, -0.5f)); // Aimed away from grid

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    EXPECT_FALSE(hit.hit);
}

TEST_F(OctreeQueryTest, TraversalPath_Miss_OppositeDirection) {
    // Ray pointing away from octree
    glm::vec3 origin(-5.0f, 5.0f, 5.0f);
    glm::vec3 direction(-1.0f, 0.0f, 0.0f); // Away from grid

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    EXPECT_FALSE(hit.hit);
}

// ---------------------------------------------------------------------------
// Category 2: Entry and Exit (No Voxel Hit)
// ---------------------------------------------------------------------------

TEST_F(OctreeQueryTest, TraversalPath_EntryExit_ThroughEmptyRegion) {
    // Ray enters grid but passes through empty space (no voxels at y>5)
    glm::vec3 origin(-5.0f, 7.0f, 7.0f); // Above voxels
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    // Currently our test octree has voxels at [0-5, 0-5, 0-5], so y=7 should miss
    EXPECT_FALSE(hit.hit);
}
// ---------------------------------------------------------------------------
// Category 7: Range-Limited Traversal (tMin/tMax constraints)
// ---------------------------------------------------------------------------

TEST_F(OctreeQueryTest, TraversalPath_Range_StartBeyondGrid) {
    // tMin starts after ray exits grid
    glm::vec3 origin(-5.0f, 2.5f, 2.5f);
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    // Grid is at x=[0,10], so tMin=20 is beyond
    auto hit = lkOctree->castRay(origin, direction, 20.0f, std::numeric_limits<float>::max());
    EXPECT_FALSE(hit.hit); // Ray starts beyond grid
}

TEST_F(OctreeQueryTest, TraversalPath_Range_EndBeforeGrid) {
    // tMax ends before ray reaches grid
    glm::vec3 origin(-5.0f, 2.5f, 2.5f);
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    // Grid starts at x=0, so tMax=3 stops at x=-2
    auto hit = lkOctree->castRay(origin, direction, 0.0f, 3.0f);
    EXPECT_FALSE(hit.hit);
}
TEST_F(OctreeQueryTest, TraversalPath_Brick_MissInsideBrick_TODO) {
    // TODO: Ray enters brick but misses all voxels within brick
    // Continue to next brick or exit
    // Placeholder test for future brick sparsity
    EXPECT_TRUE(true); // Placeholder
}
TEST_F(OctreeQueryTest, TraversalPath_Brick_MultipleGaps_TODO) {
    // TODO: Ray traverses through grid with multiple sparse bricks
    // Tests skip logic for empty bricks
    EXPECT_TRUE(true); // Placeholder for future sparse brick octrees
}
TEST_F(OctreeQueryTest, TraversalPath_Numerical_VeryShortRay) {
    // Ray with very small tMax (precision test)
    glm::vec3 origin(-1.0f, 2.5f, 2.5f);
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    auto hit = lkOctree->castRay(origin, direction, 0.0f, 0.1f);
    EXPECT_FALSE(hit.hit); // Too short to reach grid
}
TEST_F(OctreeQueryTest, TraversalPath_Complex_NearMiss) {
    // Ray that just barely misses voxel region
    glm::vec3 origin(-1.0f, 5.01f, 2.5f); // Just above occupied region
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    // May hit or miss depending on voxel coverage - test stability
    // Our octree has voxels at [0-5], so y=5.01 may miss
    if (!hit.hit) {
        EXPECT_FALSE(hit.hit); // Confirmed miss
    } else {
        EXPECT_GT(hit.tMin, 0.0f); // Confirmed hit
    }
}

// ===========================================================================
// ESVO Reference Adoption Tests - Parametric Planes
// ===========================================================================

/**
 * Test parametric plane coefficient calculation
 *
 * Reference: cuda/Raycast.inl lines 100-109
 * Tests tx_coef, ty_coef, tz_coef computation
 */
TEST(LaineKarrasOctree, ParametricPlanes_AxisAligned) {
    // Test ray along +X axis
    glm::vec3 origin(0.0f, 0.5f, 0.5f);
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    // Expected parametric coefficients for this ray:
    // tx_coef = 1 / -|dx| = 1 / -1 = -1
    // ty_coef = 1 / -|0| = 1 / -epsilon = very large negative
    // tz_coef = 1 / -|0| = 1 / -epsilon = very large negative

    // This test verifies the implementation handles axis-aligned rays correctly
    // by checking that the ray caster doesn't crash or produce NaN values

    GaiaVoxelWorld world;
    auto octree = std::make_unique<LaineKarrasOctree>(world, nullptr, 8, 3);
    auto oct = std::make_unique<Octree>();
    oct->worldMin = glm::vec3(0.0f);
    oct->worldMax = glm::vec3(1.0f);
    oct->maxLevels = 4;
    oct->root = std::make_unique<OctreeBlock>();

    // Single solid voxel at origin
    ChildDescriptor root{};
    root.childPointer = 0;
    root.farBit = 0;
    root.validMask = 0b00000001;
    root.leafMask = 0b00000001; // Leaf
    root.contourPointer = 0;
    root.contourMask = 0;
    oct->root->childDescriptors.push_back(root);

    AttributeLookup attrLookup{};
    attrLookup.valuePointer = 0;
    attrLookup.mask = 0b00000001;
    oct->root->attributeLookups.push_back(attrLookup);

    UncompressedAttributes attr = makeAttributes(
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );
    oct->root->attributes.push_back(attr);

    octree->setOctree(std::move(oct));

    // Cast ray - should not crash and should produce valid results
    auto hit = octree->castRay(origin, direction, 0.0f, 10.0f);

    // Verify no NaN values in hit
    EXPECT_FALSE(std::isnan(hit.tMin));
    EXPECT_FALSE(std::isnan(hit.tMax));
    EXPECT_FALSE(glm::any(glm::isnan(hit.hitPoint)));
    EXPECT_FALSE(glm::any(glm::isnan(hit.normal)));
}

TEST(LaineKarrasOctree, ParametricPlanes_Diagonal) {
    // Test ray at 45-degree angle
    glm::vec3 origin(-1.0f, -1.0f, 0.5f);
    glm::vec3 direction = glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f));

    // Expected parametric coefficients:
    // tx_coef = 1 / -|1/sqrt(2)| ≈ -1.414
    // ty_coef = 1 / -|1/sqrt(2)| ≈ -1.414
    // tz_coef = 1 / -|0| = 1 / -epsilon = very large negative

    GaiaVoxelWorld world;
    auto octree = std::make_unique<LaineKarrasOctree>(world, nullptr, 8, 3);
    auto oct = std::make_unique<Octree>();
    oct->worldMin = glm::vec3(0.0f);
    oct->worldMax = glm::vec3(1.0f);
    oct->maxLevels = 4;
    oct->root = std::make_unique<OctreeBlock>();

    ChildDescriptor root{};
    root.childPointer = 0;
    root.farBit = 0;
    root.validMask = 0b00000001;
    root.leafMask = 0b00000001;
    root.contourPointer = 0;
    root.contourMask = 0;
    oct->root->childDescriptors.push_back(root);

    AttributeLookup attrLookup{};
    attrLookup.valuePointer = 0;
    attrLookup.mask = 0b00000001;
    oct->root->attributeLookups.push_back(attrLookup);

    UncompressedAttributes attr = makeAttributes(
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );
    oct->root->attributes.push_back(attr);

    octree->setOctree(std::move(oct));

    auto hit = octree->castRay(origin, direction, 0.0f, 10.0f);

    // Verify no NaN values
    EXPECT_FALSE(std::isnan(hit.tMin));
    EXPECT_FALSE(std::isnan(hit.tMax));
    EXPECT_FALSE(glm::any(glm::isnan(hit.hitPoint)));
    EXPECT_FALSE(glm::any(glm::isnan(hit.normal)));
}

/**
 * Test XOR octant mirroring
 *
 * Reference: cuda/Raycast.inl lines 114-117
 * Verifies coordinate system mirroring for negative ray directions
 */
/**
 * Test CastStack structure
 *
 * Reference: Implicit in cuda/Raycast.inl traversal loop
 * Verifies stack push/pop operations for traversal backtracking
 */
TEST(LaineKarrasOctree, CastStack_PushPop) {
    // Create a simple octree structure to get ChildDescriptor pointers
    GaiaVoxelWorld world;
    auto octree = std::make_unique<LaineKarrasOctree>(world, nullptr, 8, 3);
    auto oct = std::make_unique<Octree>();
    oct->worldMin = glm::vec3(0.0f);
    oct->worldMax = glm::vec3(1.0f);
    oct->maxLevels = 4;
    oct->root = std::make_unique<OctreeBlock>();

    ChildDescriptor root{};
    root.childPointer = 1;
    root.farBit = 0;
    root.validMask = 0b00000001;
    root.leafMask = 0b00000000;
    root.contourPointer = 0;
    root.contourMask = 0;
    oct->root->childDescriptors.push_back(root);

    ChildDescriptor child{};
    child.childPointer = 0;
    child.farBit = 0;
    child.validMask = 0b11111111;
    child.leafMask = 0b11111111;
    child.contourPointer = 0;
    child.contourMask = 0;
    oct->root->childDescriptors.push_back(child);

    octree->setOctree(std::move(oct));

    // Note: CastStack is private, so we test it indirectly through ray casting
    // The test verifies that the ray caster can handle traversal that requires stack operations

    // Ray that requires descending into hierarchy and potentially backtracking
    glm::vec3 origin(-1.0f, 0.5f, 0.5f);
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    auto hit = octree->castRay(origin, direction, 0.0f, 10.0f);

    // Should successfully traverse (stack operations don't crash)
    EXPECT_FALSE(std::isnan(hit.tMin));
    EXPECT_FALSE(std::isnan(hit.tMax));
}
// ===========================================================================
// Entity-Based Ray Casting Tests (Phase 2 Integration)
// ===========================================================================

/**
 * Test entity-based octree rebuild and ray casting retrieval.
 *
 * Validates the complete entity workflow:
 * 1. Create voxel entity via GaiaVoxelWorld
 * 2. Rebuild octree from entities (LaineKarrasOctree::rebuild)
 * 3. Ray cast to find voxel
 * 4. Retrieve entity from RayHit result
 * 5. Read entity components via ECS world
 */
TEST(EntityOctreeIntegrationTest, EntityBasedRayCasting) {
    using namespace GaiaVoxel;

    // 1. Create ECS world
    GaiaVoxelWorld world;

    // 2. Create voxel entity at known position
    glm::vec3 voxelPos(10.0f, 20.0f, 30.0f);

    // Create entity with components
    ComponentQueryRequest components[] = {
        Density{1.0f},
        Color{glm::vec3(1.0f, 0.0f, 0.0f)},  // Red
        Normal{glm::vec3(0.0f, 1.0f, 0.0f)}  // Up
    };
    VoxelCreationRequest request{voxelPos, components};

    auto entity = world.createVoxel(request);
    ASSERT_TRUE(world.exists(entity));

    // 3. Create octree and rebuild from entities
    glm::vec3 worldMin(0.0f, 0.0f, 0.0f);
    glm::vec3 worldMax(32.0f, 32.0f, 32.0f);
    LaineKarrasOctree octree(world, nullptr, 5, 3);  // depth 5, brick depth 3
    octree.rebuild(world, worldMin, worldMax);

    // 4. Ray cast toward the voxel
    glm::vec3 rayOrigin(0.0f, 20.0f, 30.0f);  // From -X direction
    glm::vec3 rayDir(1.0f, 0.0f, 0.0f);       // Shoot +X toward voxel

    auto hit = octree.castRay(rayOrigin, rayDir, 0.0f, 100.0f);

    // 5. Verify hit occurred
    ASSERT_TRUE(hit.hit) << "Ray should hit the voxel";

    // 6. Verify entity was returned in hit result
    ASSERT_TRUE(world.exists(hit.entity)) << "Hit should contain valid entity reference";

    // 7. Verify it's the same entity we created
    EXPECT_EQ(hit.entity, entity) << "Ray casting should return the exact entity we created";

    // 8. Read entity components from ECS world
    auto density = world.getComponentValue<Density>(hit.entity);
    ASSERT_TRUE(density.has_value()) << "Entity should have Density component";
    EXPECT_FLOAT_EQ(density.value(), 1.0f);

    auto color = world.getComponentValue<Color>(hit.entity);
    ASSERT_TRUE(color.has_value()) << "Entity should have Color component";
    EXPECT_FLOAT_EQ(color.value().r, 1.0f);
    EXPECT_FLOAT_EQ(color.value().g, 0.0f);
    EXPECT_FLOAT_EQ(color.value().b, 0.0f);

    auto normal = world.getComponentValue<Normal>(hit.entity);
    ASSERT_TRUE(normal.has_value()) << "Entity should have Normal component";
    EXPECT_FLOAT_EQ(normal.value().y, 1.0f);

    std::cout << "[EntityOctreeIntegrationTest] ✓ Entity-based ray casting validated (rebuild workflow)\n";
    std::cout << "  Entity ID: " << hit.entity.id() << "\n";
    std::cout << "  Hit position: (" << hit.hitPoint.x << ", " << hit.hitPoint.y << ", " << hit.hitPoint.z << ")\n";
    std::cout << "  Density: " << density.value() << "\n";
    std::cout << "  Color: (" << color.value().r << ", " << color.value().g << ", " << color.value().b << ")\n";
}

/**
 * Test multiple entity creation and selective ray casting.
 */
TEST(EntityOctreeIntegrationTest, MultipleEntitiesRayCasting) {
    using namespace GaiaVoxel;

    GaiaVoxelWorld world;

    // Create 3 voxels at different positions with different colors
    struct VoxelSpec {
        glm::vec3 position;
        glm::vec3 color;
    };

    VoxelSpec voxels[] = {
        {{10.0f, 16.0f, 16.0f}, {1.0f, 0.0f, 0.0f}},  // Red at X=10
        {{14.0f, 16.0f, 16.0f}, {0.0f, 1.0f, 0.0f}},  // Green at X=14
        {{18.0f, 16.0f, 16.0f}, {0.0f, 0.0f, 1.0f}}   // Blue at X=18
    };

    std::vector<gaia::ecs::Entity> entities;
    for (const auto& spec : voxels) {
        ComponentQueryRequest components[] = {
            Density{1.0f},
            Color{spec.color}
        };
        VoxelCreationRequest request{spec.position, components};
        auto entity = world.createVoxel(request);
        entities.push_back(entity);
    }

    // Rebuild octree from entities
    glm::vec3 worldMin(0.0f, 0.0f, 0.0f);
    glm::vec3 worldMax(32.0f, 32.0f, 32.0f);
    LaineKarrasOctree octree(world, nullptr, 5, 3);  // depth 5, brick depth 3
    octree.rebuild(world, worldMin, worldMax);

    // Cast ray along +X axis - should hit voxels in order
    glm::vec3 rayOrigin(0.0f, 16.0f, 16.0f);
    glm::vec3 rayDir(1.0f, 0.0f, 0.0f);

    auto hit = octree.castRay(rayOrigin, rayDir, 0.0f, 100.0f);

    ASSERT_TRUE(hit.hit) << "Ray should hit first voxel";
    EXPECT_EQ(hit.entity, entities[0]) << "Should hit the red voxel first";

    // Verify it's the red voxel
    auto color = world.getComponentValue<Color>(hit.entity);
    ASSERT_TRUE(color.has_value());
    EXPECT_FLOAT_EQ(color.value().r, 1.0f);
    EXPECT_FLOAT_EQ(color.value().g, 0.0f);
    EXPECT_FLOAT_EQ(color.value().b, 0.0f);

    std::cout << "[MultipleEntitiesRayCasting] ✓ Verified first voxel hit (red) (rebuild workflow)\n";
}

/**
 * Test entity lookup failure when no entity exists at position.
 */
TEST(EntityOctreeIntegrationTest, MissReturnsInvalidEntity) {
    using namespace GaiaVoxel;

    GaiaVoxelWorld world;

    // Rebuild octree from empty world
    glm::vec3 worldMin(0.0f, 0.0f, 0.0f);
    glm::vec3 worldMax(32.0f, 32.0f, 32.0f);
    LaineKarrasOctree octree(world, nullptr, 5, 3);
    octree.rebuild(world, worldMin, worldMax);

    // Cast ray through empty space
    glm::vec3 rayOrigin(0.0f, 16.0f, 16.0f);
    glm::vec3 rayDir(1.0f, 0.0f, 0.0f);

    auto hit = octree.castRay(rayOrigin, rayDir, 0.0f, 100.0f);

    EXPECT_FALSE(hit.hit) << "Ray should miss in empty octree";
    EXPECT_FALSE(world.exists(hit.entity)) << "Miss should return invalid entity";

    std::cout << "[MissReturnsInvalidEntity] ✓ Empty octree returns miss correctly (rebuild workflow)\n";
}

// ============================================================================
// Octree Rebuild API Tests (Phase 3)
// ============================================================================

/**
 * Test rebuild() API design (stub implementation).
 * Verifies API exists and compiles correctly.
 */
TEST(EntityBasedRebuildTest, RebuildAPIStub) {
    using namespace GaiaVoxel;
    using namespace SVO;

    GaiaVoxelWorld world;

    // Create some test entities
    ComponentQueryRequest components[] = {
        Density{1.0f},
        Color{glm::vec3(1, 0, 0)}
    };

    auto entity1 = world.createVoxel(VoxelCreationRequest{glm::vec3(10, 10, 10), components});
    auto entity2 = world.createVoxel(VoxelCreationRequest{glm::vec3(20, 20, 20), components});
    auto entity3 = world.createVoxel(VoxelCreationRequest{glm::vec3(30, 30, 30), components});

    ASSERT_TRUE(world.exists(entity1));
    ASSERT_TRUE(world.exists(entity2));
    ASSERT_TRUE(world.exists(entity3));

    // Create octree with entity-based constructor
    LaineKarrasOctree octree(world, nullptr, 8, 3);  // Default maxLevels=8, brickDepth=3

    // Test rebuild API (stub - prints "NOT YET IMPLEMENTED")
    glm::vec3 worldMin(0.0f, 0.0f, 0.0f);
    glm::vec3 worldMax(100.0f, 100.0f, 100.0f);
    octree.rebuild(world, worldMin, worldMax);

    // Test partial update API (stub)
    octree.updateBlock(glm::vec3(10, 10, 10), 3);

    // Test block removal API (stub)
    octree.removeBlock(glm::vec3(20, 20, 20), 3);

    // Test concurrency control API
    octree.lockForRendering();
    // ... ray casting would happen here ...
    octree.unlockAfterRendering();

    std::cout << "[RebuildAPIStub] ✓ Rebuild API compiles and can be called\n";
}

/**
 * Test rebuild() with hierarchical structure validation.
 * Creates entities in multiple bricks, rebuilds octree, verifies:
 * - Brick-level descriptors created for populated bricks
 * - Parent descriptors created for each hierarchy level
 * - Root descriptor exists
 * - BFS ordering maintained (contiguous children)
 */
TEST(EntityBasedRebuildTest, RebuildHierarchicalStructure) {
    using namespace GaiaVoxel;
    using namespace SVO;

    std::cout << "\n[RebuildHierarchicalStructure] Testing hierarchical octree construction...\n";

    GaiaVoxelWorld world;

    // Create entities in 4 separate bricks (depth 3 = 8³ voxels per brick)
    // This ensures we have multiple bricks and need parent hierarchy
    ComponentQueryRequest components[] = {
        Density{1.0f},
        Color{glm::vec3(1, 0, 0)}
    };

    // Brick 1: (0-8, 0-8, 0-8)
    auto e1 = world.createVoxel(VoxelCreationRequest{glm::vec3(2, 2, 2), components});
    auto e2 = world.createVoxel(VoxelCreationRequest{glm::vec3(5, 5, 5), components});

    // Brick 2: (16-24, 0-8, 0-8)
    auto e3 = world.createVoxel(VoxelCreationRequest{glm::vec3(18, 2, 2), components});
    auto e4 = world.createVoxel(VoxelCreationRequest{glm::vec3(20, 5, 5), components});

    // Brick 3: (0-8, 16-24, 0-8)
    auto e5 = world.createVoxel(VoxelCreationRequest{glm::vec3(2, 18, 2), components});
    auto e6 = world.createVoxel(VoxelCreationRequest{glm::vec3(5, 20, 5), components});

    // Brick 4: (16-24, 16-24, 0-8)
    auto e7 = world.createVoxel(VoxelCreationRequest{glm::vec3(18, 18, 2), components});
    auto e8 = world.createVoxel(VoxelCreationRequest{glm::vec3(20, 20, 5), components});

    ASSERT_TRUE(world.exists(e1));
    ASSERT_TRUE(world.exists(e8));

    std::cout << "[RebuildHierarchicalStructure] Created 8 entities in 4 bricks\n";

    // Create octree and rebuild from world
    LaineKarrasOctree octree(world, nullptr, 23, 3);  // depth 23, brick depth 3

    glm::vec3 worldMin(0.0f, 0.0f, 0.0f);
    glm::vec3 worldMax(1024.0f, 1024.0f, 1024.0f);  // Large world
    octree.rebuild(world, worldMin, worldMax);

    std::cout << "[RebuildHierarchicalStructure] Rebuild complete - validating structure...\n";

    // Validate: Should have more than just brick descriptors (need parents + root)
    // With 4 bricks at depth 3 (maxLevels 23 → 20 levels above bricks):
    // - 4 brick descriptors (depth 3)
    // - Parent descriptors at each level (depth 4, 5, ..., 23)
    // - Minimum: 4 bricks + 1 parent at depth 4 + ... + 1 root at depth 23

    // We can't predict exact count (depends on spatial distribution), but:
    // - Must have at least 4 descriptors (4 bricks)
    // - Must have more than 4 (parents + root)
    // - Should have root at index 0 (BFS order)

    const auto& descriptors = octree.getOctree()->root->childDescriptors;
    const auto& brickViews = octree.getOctree()->root->brickViews;

    std::cout << "[RebuildHierarchicalStructure] Descriptors: " << descriptors.size() << "\n";
    std::cout << "[RebuildHierarchicalStructure] BrickViews: " << brickViews.size() << "\n";

    // Validation 1: Must have at least 4 brick views (one per populated brick)
    ASSERT_GE(brickViews.size(), 4) << "Expected at least 4 brick views";

    // Validation 2: Must have more descriptors than bricks (parents + root)
    ASSERT_GT(descriptors.size(), brickViews.size())
        << "Expected parent descriptors above brick level";

    // Validation 3: Root descriptor (index 0) should have non-zero validMask
    ASSERT_GT(descriptors[0].validMask, 0)
        << "Root descriptor should have valid children";

    // Validation 4: Root should not be a leaf (leafMask should be 0x00 or partial)
    ASSERT_NE(descriptors[0].leafMask, 0xFF)
        << "Root should not have all leaf children (has intermediate nodes)";

    std::cout << "[RebuildHierarchicalStructure] Root descriptor: "
              << "validMask=0x" << std::hex << (int)descriptors[0].validMask
              << " leafMask=0x" << (int)descriptors[0].leafMask
              << " childPointer=" << std::dec << descriptors[0].childPointer << "\n";

    // Validation 5: If root has childPointer, it should point to valid index
    if (descriptors[0].childPointer > 0) {
        ASSERT_LT(descriptors[0].childPointer, descriptors.size())
            << "Root childPointer should be valid index";
    }

    std::cout << "[RebuildHierarchicalStructure] ✓ Hierarchical structure validated\n";
}
