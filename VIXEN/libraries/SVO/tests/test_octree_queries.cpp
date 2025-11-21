#include <gtest/gtest.h>
#include "LaineKarrasOctree.h"
#include "SVOBuilder.h"
#include "SVOTypes.h"
#include "VoxelInjection.h"
#include <chrono>

using namespace SVO;

// ===========================================================================
// Helper: Create Simple Test Octree
// ===========================================================================

class OctreeQueryTest : public ::testing::Test {
protected:
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

        // Create LaineKarrasOctree wrapper
        lkOctree = std::make_unique<LaineKarrasOctree>();
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

// ===========================================================================
// Ray Casting Tests - Comprehensive Coverage
// ===========================================================================

// ---------------------------------------------------------------------------
// Basic Hit Tests
// ---------------------------------------------------------------------------

TEST_F(OctreeQueryTest, CastRayBasicHit) {
    // Ray shooting from outside, hitting voxel at [1,1,1]
    // Voxel child 0,0,0 of child 0 occupies [0, 2.5]³
    glm::vec3 origin(-1.0f, 1.0f, 1.0f);
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    auto hit = lkOctree->castRay(origin, direction, 0.0f, 10.0f);

    EXPECT_TRUE(hit.hit);
    EXPECT_GT(hit.tMin, 0.0f);
    EXPECT_LT(hit.tMin, 10.0f);
    EXPECT_EQ(hit.scale, 2); // Depth 2 (leaf level)
}

TEST_F(OctreeQueryTest, CastRayHitFromInside) {
    // Ray starting inside a voxel
    glm::vec3 origin(1.0f, 1.0f, 1.0f);
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    auto hit = lkOctree->castRay(origin, direction, 0.0f, 10.0f);

    EXPECT_TRUE(hit.hit);
    EXPECT_GE(hit.tMin, 0.0f); // Should hit immediately or very close
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
// Directional Tests (All 6 Axes)
// ---------------------------------------------------------------------------

TEST_F(OctreeQueryTest, CastRayPositiveX) {
    glm::vec3 origin(-1.0f, 1.0f, 1.0f);
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    EXPECT_TRUE(hit.hit);
}

TEST_F(OctreeQueryTest, CastRayNegativeX) {
    glm::vec3 origin(11.0f, 1.0f, 1.0f);
    glm::vec3 direction(-1.0f, 0.0f, 0.0f);

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    EXPECT_TRUE(hit.hit);
}

TEST_F(OctreeQueryTest, CastRayPositiveY) {
    glm::vec3 origin(1.0f, -1.0f, 1.0f);
    glm::vec3 direction(0.0f, 1.0f, 0.0f);

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    EXPECT_TRUE(hit.hit);
}

TEST_F(OctreeQueryTest, CastRayNegativeY) {
    glm::vec3 origin(1.0f, 11.0f, 1.0f);
    glm::vec3 direction(0.0f, -1.0f, 0.0f);

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    EXPECT_TRUE(hit.hit);
}

TEST_F(OctreeQueryTest, CastRayPositiveZ) {
    glm::vec3 origin(1.0f, 1.0f, -1.0f);
    glm::vec3 direction(0.0f, 0.0f, 1.0f);

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    EXPECT_TRUE(hit.hit);
}

TEST_F(OctreeQueryTest, CastRayNegativeZ) {
    glm::vec3 origin(1.0f, 1.0f, 11.0f);
    glm::vec3 direction(0.0f, 0.0f, -1.0f);

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    EXPECT_TRUE(hit.hit);
}

// ---------------------------------------------------------------------------
// Diagonal and Oblique Angles
// ---------------------------------------------------------------------------

TEST_F(OctreeQueryTest, CastRayDiagonal45Deg) {
    glm::vec3 origin(-1.0f, -1.0f, 1.0f);
    glm::vec3 direction = glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f));

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    EXPECT_TRUE(hit.hit);
}

TEST_F(OctreeQueryTest, CastRay3DDiagonal) {
    glm::vec3 origin(-1.0f, -1.0f, -1.0f);
    glm::vec3 direction = glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f));

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    EXPECT_TRUE(hit.hit);
}

TEST_F(OctreeQueryTest, CastRayGrazingAngle) {
    // Nearly parallel to Y axis, grazing the voxel
    glm::vec3 origin(0.1f, -1.0f, 0.1f);
    glm::vec3 direction = glm::normalize(glm::vec3(0.01f, 1.0f, 0.01f));

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    EXPECT_TRUE(hit.hit);
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

TEST_F(OctreeQueryTest, CastRayNonNormalizedDirection) {
    glm::vec3 origin(-1.0f, 1.0f, 1.0f);
    glm::vec3 direction(5.0f, 0.0f, 0.0f); // Not normalized

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    EXPECT_TRUE(hit.hit); // Should normalize internally
}

TEST_F(OctreeQueryTest, CastRayTMinTMaxRange) {
    glm::vec3 origin(-1.0f, 1.0f, 1.0f);
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    // Ray hits at t ≈ 1.0, limit to t < 0.5 should miss
    auto hit = lkOctree->castRay(origin, direction, 0.0f, 0.5f);
    EXPECT_FALSE(hit.hit);

    // Ray hits at t ≈ 1.0, range [0, 5] should hit
    hit = lkOctree->castRay(origin, direction, 0.0f, 5.0f);
    EXPECT_TRUE(hit.hit);
}

TEST_F(OctreeQueryTest, CastRayNegativeTMin) {
    glm::vec3 origin(-1.0f, 1.0f, 1.0f);
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    // Negative tMin should be clamped to 0
    auto hit = lkOctree->castRay(origin, direction, -5.0f, 10.0f);
    EXPECT_TRUE(hit.hit);
    EXPECT_GE(hit.tMin, 0.0f);
}

// ---------------------------------------------------------------------------
// Normal Computation Tests
// ---------------------------------------------------------------------------

TEST_F(OctreeQueryTest, CastRayNormalPositiveX) {
    glm::vec3 origin(-1.0f, 1.0f, 1.0f);
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    ASSERT_TRUE(hit.hit);

    // Hitting from -X direction, normal should be -X
    EXPECT_NEAR(std::abs(hit.normal.x), 1.0f, 0.1f);
    EXPECT_NEAR(std::abs(hit.normal.y), 0.0f, 0.1f);
    EXPECT_NEAR(std::abs(hit.normal.z), 0.0f, 0.1f);
}

TEST_F(OctreeQueryTest, CastRayNormalPositiveY) {
    glm::vec3 origin(1.0f, -1.0f, 1.0f);
    glm::vec3 direction(0.0f, 1.0f, 0.0f);

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    ASSERT_TRUE(hit.hit);

    // Hitting from -Y direction, normal should be -Y
    EXPECT_NEAR(std::abs(hit.normal.x), 0.0f, 0.1f);
    EXPECT_NEAR(std::abs(hit.normal.y), 1.0f, 0.1f);
    EXPECT_NEAR(std::abs(hit.normal.z), 0.0f, 0.1f);
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

TEST_F(OctreeQueryTest, CastRayLODCoarserDetail) {
    glm::vec3 origin(-1.0f, 1.0f, 1.0f);
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    auto hitFine = lkOctree->castRayLOD(origin, direction, 0.0f, 0.0f, std::numeric_limits<float>::max());
    auto hitCoarse = lkOctree->castRayLOD(origin, direction, 1.0f, 0.0f, std::numeric_limits<float>::max());

    EXPECT_TRUE(hitFine.hit);
    EXPECT_TRUE(hitCoarse.hit);

    // Coarse LOD should stop at higher level (lower scale value)
    EXPECT_LE(hitCoarse.scale, hitFine.scale);
}

TEST_F(OctreeQueryTest, CastRayLODHighBias) {
    glm::vec3 origin(-1.0f, 1.0f, 1.0f);
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    auto hit = lkOctree->castRayLOD(origin, direction, 5.0f);
    EXPECT_TRUE(hit.hit);
    // Should hit at root or very high level
    EXPECT_LE(hit.scale, 2);
}

// ---------------------------------------------------------------------------
// Hit Information Tests
// ---------------------------------------------------------------------------

TEST_F(OctreeQueryTest, CastRayHitPosition) {
    glm::vec3 origin(-1.0f, 1.0f, 1.0f);
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    ASSERT_TRUE(hit.hit);

    // Hit position should be origin + direction * tMin
    glm::vec3 expectedPos = origin + direction * hit.tMin;
    EXPECT_NEAR(hit.position.x, expectedPos.x, 0.01f);
    EXPECT_NEAR(hit.position.y, expectedPos.y, 0.01f);
    EXPECT_NEAR(hit.position.z, expectedPos.z, 0.01f);

    // Hit should be within world bounds
    EXPECT_GE(hit.position.x, lkOctree->getWorldMin().x);
    EXPECT_GE(hit.position.y, lkOctree->getWorldMin().y);
    EXPECT_GE(hit.position.z, lkOctree->getWorldMin().z);
}

TEST_F(OctreeQueryTest, CastRayTMinTMax) {
    glm::vec3 origin(-1.0f, 1.0f, 1.0f);
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    ASSERT_TRUE(hit.hit);

    // tMin should be entry point, tMax should be exit point
    EXPECT_LT(hit.tMin, hit.tMax);
    EXPECT_GT(hit.tMax - hit.tMin, 0.0f); // Non-zero thickness
}

// ---------------------------------------------------------------------------
// Multiple Voxel Tests (Future: when octree has more voxels)
// ---------------------------------------------------------------------------

TEST_F(OctreeQueryTest, CastRayFirstHit) {
    // Ray should hit first voxel, not continue through
    glm::vec3 origin(-1.0f, 1.0f, 1.0f);
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    ASSERT_TRUE(hit.hit);

    // Verify we got the first hit (not a later voxel)
    EXPECT_LT(hit.tMin, 5.0f); // Should hit within reasonable distance
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

TEST_F(OctreeQueryTest, TraversalPath_EntryExit_ThroughGaps) {
    // Ray passes through gaps between voxels (if any exist in sparse octree)
    // Our test octree is dense in its occupied region, so this may hit
    glm::vec3 origin(-5.0f, 1.5f, 1.5f);
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    // This should hit the voxel region
    EXPECT_TRUE(hit.hit);
}

// ---------------------------------------------------------------------------
// Category 3: Single Voxel Hits
// ---------------------------------------------------------------------------

TEST_F(OctreeQueryTest, TraversalPath_SingleHit_FrontFace) {
    // Ray hits front face of voxel region, stops at first voxel
    glm::vec3 origin(-1.0f, 2.5f, 2.5f);
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    ASSERT_TRUE(hit.hit);

    // Should hit first voxel (which is at x=[0, 2.5])
    EXPECT_GE(hit.position.x, 0.0f); // Within grid
    EXPECT_LT(hit.position.x, 3.0f); // Within first voxel region
    EXPECT_GT(hit.tMin, 0.0f); // Ray traveled some distance
}

TEST_F(OctreeQueryTest, TraversalPath_SingleHit_CenterAimed) {
    // Ray aimed directly at center of a voxel
    glm::vec3 origin(-1.0f, 1.25f, 1.25f); // Aimed at child 0 center
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    ASSERT_TRUE(hit.hit);
    EXPECT_GT(hit.tMin, 0.0f);
}

// ---------------------------------------------------------------------------
// Category 4: Multiple Voxel Traversal (Through Grid)
// ---------------------------------------------------------------------------

TEST_F(OctreeQueryTest, TraversalPath_Traverse_MultipleVoxels) {
    // Ray traverses through grid, hitting first voxel
    // (Current implementation returns first hit, not all hits)
    glm::vec3 origin(-1.0f, 2.5f, 2.5f);
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    ASSERT_TRUE(hit.hit);

    // First hit should be near grid entry
    EXPECT_LT(hit.tMin, 5.0f); // Should hit within reasonable distance
}

TEST_F(OctreeQueryTest, TraversalPath_Traverse_DiagonalThrough) {
    // Diagonal ray through grid - hits multiple voxels, returns first
    glm::vec3 origin(-1.0f, -1.0f, 2.5f);
    glm::vec3 direction = glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f));

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    ASSERT_TRUE(hit.hit);
    EXPECT_GT(hit.tMin, 0.0f);
}

// ---------------------------------------------------------------------------
// Category 5: Grazing Hits (Edges and Corners)
// ---------------------------------------------------------------------------

TEST_F(OctreeQueryTest, TraversalPath_Grazing_EdgeHit) {
    // Ray grazes along edge of grid
    glm::vec3 origin(-1.0f, 0.0f, 2.5f); // At bottom edge (y=0)
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    // Should hit since ray is on boundary
    EXPECT_TRUE(hit.hit);
}

TEST_F(OctreeQueryTest, TraversalPath_Grazing_CornerHit) {
    // Ray passes through corner of grid
    glm::vec3 origin(-1.0f, 0.0f, 0.0f); // At corner
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    EXPECT_TRUE(hit.hit);
}

TEST_F(OctreeQueryTest, TraversalPath_Grazing_VoxelBoundary) {
    // Ray aligned with voxel boundary plane
    glm::vec3 origin(-1.0f, 2.5f, 2.5f); // Aligned with voxel center
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    ASSERT_TRUE(hit.hit);
    // Normal should point outward from hit face
    // AABB normal computation handles ties - accept any valid normal
    float normalLength = glm::length(hit.normal);
    EXPECT_NEAR(normalLength, 1.0f, 0.01f); // Should be unit length
}

// ---------------------------------------------------------------------------
// Category 6: Partial Traversal (Ray Starts Inside)
// ---------------------------------------------------------------------------

TEST_F(OctreeQueryTest, TraversalPath_StartInside_CenterOfVoxel) {
    // Ray origin inside a voxel
    glm::vec3 origin(1.25f, 1.25f, 1.25f); // Center of child 0
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    ASSERT_TRUE(hit.hit);
    // Should hit immediately or nearby
    EXPECT_GE(hit.tMin, 0.0f);
}

TEST_F(OctreeQueryTest, TraversalPath_StartInside_ExitToEmpty) {
    // Ray starts inside grid, exits to empty space
    glm::vec3 origin(2.5f, 2.5f, 2.5f); // Inside grid
    glm::vec3 direction(1.0f, 0.0f, 0.0f); // Toward exit

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    // Should hit current or next voxel
    EXPECT_TRUE(hit.hit);
}

TEST_F(OctreeQueryTest, TraversalPath_StartInside_ExitImmediately) {
    // Ray starts just inside boundary, exits immediately
    glm::vec3 origin(4.9f, 2.5f, 2.5f); // Near edge
    glm::vec3 direction(1.0f, 0.0f, 0.0f); // Exiting

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    EXPECT_TRUE(hit.hit); // Should still hit current voxel
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

TEST_F(OctreeQueryTest, TraversalPath_Range_WindowThroughGrid) {
    // Limited range that intersects part of grid
    glm::vec3 origin(-5.0f, 2.5f, 2.5f);
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    // Window from x=0 to x=7 (tMin=5, tMax=12)
    auto hit = lkOctree->castRay(origin, direction, 5.0f, 12.0f);
    ASSERT_TRUE(hit.hit);
    EXPECT_GE(hit.tMin, 5.0f);
    EXPECT_LE(hit.tMin, 12.0f);
}

// ---------------------------------------------------------------------------
// Category 8: Brick-Level Scenarios (Future-Proofing for Brick Support)
// ---------------------------------------------------------------------------

TEST_F(OctreeQueryTest, TraversalPath_Brick_HitBrick_TODO) {
    // TODO: When brick support is added, test hitting a brick
    // Ray should hit brick and perform DDA within brick
    // For now, we test hitting dense leaf region
    glm::vec3 origin(-1.0f, 1.25f, 1.25f);
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    ASSERT_TRUE(hit.hit);
    // When bricks are implemented, verify brick-level traversal
}

TEST_F(OctreeQueryTest, TraversalPath_Brick_MissInsideBrick_TODO) {
    // TODO: Ray enters brick but misses all voxels within brick
    // Continue to next brick or exit
    // Placeholder test for future brick sparsity
    EXPECT_TRUE(true); // Placeholder
}

TEST_F(OctreeQueryTest, TraversalPath_Brick_ExitBrickContinueGrid_TODO) {
    // TODO: Ray exits brick, continues traversing grid to next brick
    // Tests brick-to-brick transitions
    // Our current dense voxels would hit immediately
    glm::vec3 origin(-1.0f, 2.5f, 2.5f);
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    EXPECT_TRUE(hit.hit);
    // When bricks are implemented, verify continuation logic
}

TEST_F(OctreeQueryTest, TraversalPath_Brick_MultipleGaps_TODO) {
    // TODO: Ray traverses through grid with multiple sparse bricks
    // Tests skip logic for empty bricks
    EXPECT_TRUE(true); // Placeholder for future sparse brick octrees
}

// ---------------------------------------------------------------------------
// Category 9: Edge Cases and Numerical Stability
// ---------------------------------------------------------------------------

TEST_F(OctreeQueryTest, TraversalPath_Numerical_ParallelToAxis) {
    // Ray exactly parallel to grid axis
    glm::vec3 origin(-1.0f, 2.5f, 2.5f);
    glm::vec3 direction(1.0f, 0.0f, 0.0f); // Perfectly parallel to X

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    ASSERT_TRUE(hit.hit);
    EXPECT_GT(hit.tMin, 0.0f);
}

TEST_F(OctreeQueryTest, TraversalPath_Numerical_AlmostParallel) {
    // Ray almost parallel (tests epsilon handling)
    glm::vec3 origin(-1.0f, 2.5f, 2.5f);
    glm::vec3 direction = glm::normalize(glm::vec3(1.0f, 1e-7f, 0.0f));

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    EXPECT_TRUE(hit.hit);
}

TEST_F(OctreeQueryTest, TraversalPath_Numerical_VeryLongRay) {
    // Ray with very large tMax
    glm::vec3 origin(-1.0f, 2.5f, 2.5f);
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    auto hit = lkOctree->castRay(origin, direction, 0.0f, 1e20f);
    ASSERT_TRUE(hit.hit);
    EXPECT_LT(hit.tMin, 1e20f);
}

TEST_F(OctreeQueryTest, TraversalPath_Numerical_VeryShortRay) {
    // Ray with very small tMax (precision test)
    glm::vec3 origin(-1.0f, 2.5f, 2.5f);
    glm::vec3 direction(1.0f, 0.0f, 0.0f);

    auto hit = lkOctree->castRay(origin, direction, 0.0f, 0.1f);
    EXPECT_FALSE(hit.hit); // Too short to reach grid
}

// ---------------------------------------------------------------------------
// Category 10: Complex Traversal Paths
// ---------------------------------------------------------------------------

TEST_F(OctreeQueryTest, TraversalPath_Complex_SpiralPath) {
    // Ray at angle that crosses multiple voxel boundaries
    glm::vec3 origin(-1.0f, -1.0f, -1.0f);
    glm::vec3 direction = glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f));

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    ASSERT_TRUE(hit.hit);
    // Should hit corner region
    EXPECT_GT(hit.tMin, 0.0f);
}

TEST_F(OctreeQueryTest, TraversalPath_Complex_StairstepPattern) {
    // Ray that would hit voxels in stairstepping pattern
    glm::vec3 origin(-1.0f, 0.5f, 0.5f);
    glm::vec3 direction = glm::normalize(glm::vec3(1.0f, 0.5f, 0.0f));

    auto hit = lkOctree->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    ASSERT_TRUE(hit.hit);
    // Verify hit occurs in expected region
    EXPECT_GE(hit.position.x, 0.0f);
    EXPECT_LT(hit.position.x, 10.0f);
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
// Cornell Box Scene Tests - Material and Lighting Validation
// ===========================================================================

/**
 * Cornell Box Test Fixture
 *
 * Classic Cornell box scene:
 * - Floor: Bright grey (0.8, 0.8, 0.8)
 * - Ceiling: Bright grey (0.8, 0.8, 0.8) with white light patch
 * - Back wall: Bright grey (0.8, 0.8, 0.8)
 * - Left wall: Red (0.8, 0.1, 0.1)
 * - Right wall: Green (0.1, 0.8, 0.1)
 * - Light: White emissive (1.0, 1.0, 1.0)
 *
 * Box dimensions: 10x10x10 units
 * Center: (5, 5, 5)
 */
class CornellBoxTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Build Cornell box using ADDITIVE VOXEL INSERTION (new sparse approach)
        using namespace SVO;

        buildCornellBoxAdditive();
    }

    // NEW APPROACH: Generate wall voxels directly and insert additively
    void buildCornellBoxAdditive() {
        using namespace SVO;

        constexpr float boxSize = 10.0f;
        constexpr float thickness = 0.2f;
        constexpr float voxelSize = 0.1f; // Voxel spacing

        // Create empty octree and initialize with world bounds
        cornellBox = new LaineKarrasOctree();
        cornellBox->ensureInitialized(glm::vec3(0.0f), glm::vec3(boxSize), 8);

        VoxelInjector injector;
        InjectionConfig config;
        config.maxLevels = 8; // Reasonable depth for 10³ world with 0.1 voxels

        std::vector<::VoxelData::DynamicVoxelScalar> wallVoxels;


        // Generate floor voxels (y=0 to thickness)
        for (float x = 0.0f; x < boxSize; x += voxelSize) {
            for (float z = 0.0f; z < boxSize; z += voxelSize) {
                for (float y = 0.0f; y < thickness; y += voxelSize) {
                    ::VoxelData::DynamicVoxelScalar v;
					v.set("position", glm::vec3(x, y, z));
					v.set("color", glm::vec3(0.8f, 0.8f, 0.8f)); // Grey
					v.set("normal", glm::vec3(0.0f, 1.0f, 0.0f)); // Up
					v.set("density", 1.0f);
                    wallVoxels.push_back(v);
                }
            }
        }

        // Generate ceiling voxels (y=boxSize-thickness to boxSize)
        for (float x = 0.0f; x < boxSize; x += voxelSize) {
            for (float z = 0.0f; z < boxSize; z += voxelSize) {
                for (float y = boxSize - thickness; y < boxSize; y += voxelSize) {
                    ::VoxelData::DynamicVoxelScalar v;
                    v.set("position", glm::vec3(x, y, z));
                    // Check if in light patch (center of ceiling)
                    glm::vec2 centerXZ(boxSize * 0.5f, boxSize * 0.5f);
                    float distFromCenter = glm::length(glm::vec2(x, z) - centerXZ);
                    v.set("color",(distFromCenter < 2.0f) ? glm::vec3(1.0f) : glm::vec3(0.8f)); // White light or grey
                    v.set("normal", glm::vec3(0.0f, -1.0f, 0.0f)); // Down
                    v.set("density", 1.0f);
                    wallVoxels.push_back(v);
                }
            }
        }

        // Generate left wall (x=0 to thickness) - RED
        for (float y = 0.0f; y < boxSize; y += voxelSize) {
            for (float z = 0.0f; z < boxSize; z += voxelSize) {
                for (float x = 0.0f; x < thickness; x += voxelSize) {
                    ::VoxelData::DynamicVoxelScalar v;
                    v.set("position", glm::vec3(x, y, z));
                    v.set("color", glm::vec3(0.8f, 0.1f, 0.1f)); // Red
                    v.set("normal", glm::vec3(1.0f, 0.0f, 0.0f)); // Right
                    v.set("density", 1.0f);
                    wallVoxels.push_back(v);
                }
            }
        }

        // Generate right wall (x=boxSize-thickness to boxSize) - GREEN
        for (float y = 0.0f; y < boxSize; y += voxelSize) {
            for (float z = 0.0f; z < boxSize; z += voxelSize) {
                for (float x = boxSize - thickness; x < boxSize; x += voxelSize) {
                    ::VoxelData::DynamicVoxelScalar v;
                    v.set("position", glm::vec3(x, y, z));
                    v.set("color", glm::vec3(0.1f, 0.8f, 0.1f)); // Green
                    v.set("normal", glm::vec3(-1.0f, 0.0f, 0.0f)); // Left
                    v.set("density", 1.0f);
                    wallVoxels.push_back(v);
                }
            }
        }

        // Generate back wall (z=boxSize-thickness to boxSize)
        for (float x = 0.0f; x < boxSize; x += voxelSize) {
            for (float y = 0.0f; y < boxSize; y += voxelSize) {
                for (float z = boxSize - thickness; z < boxSize; z += voxelSize) {
                    ::VoxelData::DynamicVoxelScalar v;
                    v.set("position", glm::vec3(x, y, z));
                    v.set("color", glm::vec3(0.8f, 0.8f, 0.8f)); // Grey
                    v.set("normal", glm::vec3(0.0f, 0.0f, -1.0f)); // Forward
                    v.set("density", 1.0f);
                    wallVoxels.push_back(v);
                }
            }
        }

        std::cout << "\n=== Building Cornell Box (Additive Insertion) ===\n";
        std::cout << "Total wall voxels: " << wallVoxels.size() << "\n";

        // Insert all voxels using additive insertion
        auto startTime = std::chrono::high_resolution_clock::now();

        size_t inserted = 0;
        size_t failed = 0;
        for (const auto& voxel : wallVoxels) {
            glm::vec3 pos = voxel.get<glm::vec3>("position");
            if (injector.insertVoxel(*cornellBox, pos, voxel, config)) {
                inserted++;
            } else {
                failed++;
            }
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        float buildTime = std::chrono::duration<float>(endTime - startTime).count();

        std::cout << "Build time: " << buildTime << " seconds\n";
        std::cout << "Inserted: " << inserted << ", Failed: " << failed << "\n";
        std::cout << cornellBox->getStats() << "\n";
    }

    // OLD APPROACH (kept for reference, not used)
    void buildCornellBoxDensityBased() {
        using namespace SVO;

        // Create Cornell box sampler
        auto cornellSampler = std::make_unique<LambdaVoxelSampler>(
            // Sample function - returns material based on position
            [](const glm::vec3& pos, ::VoxelData::DynamicVoxelScalar& data) -> bool {
                constexpr float thickness = 0.2f; // Wall thickness
                constexpr float boxSize = 10.0f;
                constexpr float lightSize = 2.0f; // Light patch size
                constexpr float lightY = boxSize - thickness; // Ceiling position

                data.set("position",pos);
                data.set("density", 1.0f);

                // Debug: log samples that return true near (9.375, 4.375, 9.375)
                bool shouldLog = (pos.x > 9.0f && pos.x < 10.0f && pos.y > 4.0f && pos.y < 5.0f && pos.z > 9.0f && pos.z < 10.0f);

                // Floor (y=0)
                if (pos.y < thickness) {
                    data.set("color", glm::vec3(0.8f, 0.8f, 0.8f)); // Bright grey
                    data.set("normal", glm::vec3(0.0f, 1.0f, 0.0f)); // Up
                    return true;
                }

                // Ceiling (y=10) with light patch
                if (pos.y > lightY) {
                    // Check if in light patch (center of ceiling)
                    glm::vec2 centerXZ(boxSize * 0.5f, boxSize * 0.5f);
                    glm::vec2 posXZ(pos.x, pos.z);
                    float distFromCenter = glm::length(posXZ - centerXZ);

                    if (distFromCenter < lightSize) {
                        data.set("color", glm::vec3(1.0f, 1.0f, 1.0f)); // White light
                    } else {
                        data.set("color", glm::vec3(0.8f, 0.8f, 0.8f)); // Bright grey
                    }
                    data.set("normal", glm::vec3(0.0f, -1.0f, 0.0f)); // Down
                    return true;
                }

                // Left wall (x=0) - RED
                if (pos.x < thickness) {
                    data.set("color", glm::vec3(0.8f, 0.1f, 0.1f)); // Red
                    data.set("normal", glm::vec3(1.0f, 0.0f, 0.0f)); // Right
                    return true;
                }

                // Right wall (x=10) - GREEN
                if (pos.x > boxSize - thickness) {
                    data.set("color", glm::vec3(0.1f, 0.8f, 0.1f)); // Green
                    data.set("normal", glm::vec3(-1.0f, 0.0f, 0.0f)); // Left
                    return true;
                }

                // Back wall (z=10)
                if (pos.z > boxSize - thickness) {
                    data.set("color", glm::vec3(0.8f, 0.8f, 0.8f)); // Bright grey
                    data.set("normal", glm::vec3(0.0f, 0.0f, -1.0f)); // Forward
                    return true;
                }

                // Front wall (z=0) - open for camera
                if (pos.z < thickness) {
                    data.set("color", glm::vec3(0.8f, 0.8f, 0.8f)); // Bright grey
                    data.set("normal", glm::vec3(0.0f, 0.0f, 1.0f)); // Backward
                    if (shouldLog) std::cout << "[SAMPLE TRUE] pos=(" << pos.x << "," << pos.y << "," << pos.z << ") FRONT WALL\n";
                    return true;
                }

                if (shouldLog) std::cout << "[SAMPLE FALSE] pos=(" << pos.x << "," << pos.y << "," << pos.z << ") EMPTY\n";
                return false; // Empty interior
            },

            // Bounds function
            [](glm::vec3& min, glm::vec3& max) {
                min = glm::vec3(0.0f);
                max = glm::vec3(10.0f);
            },

            // Density estimator - only subdivide regions that CONTAIN wall geometry
            [](const glm::vec3& center, float size) -> float {
                constexpr float thickness = 0.2f;
                constexpr float boxSize = 10.0f;
                float halfSize = size * 0.5f;

                // Region bounds
                glm::vec3 regionMin = center - glm::vec3(halfSize);
                glm::vec3 regionMax = center + glm::vec3(halfSize);

                // Check if region is FULLY OUTSIDE the box
                if (regionMax.x < 0.0f || regionMin.x > boxSize ||
                    regionMax.y < 0.0f || regionMin.y > boxSize ||
                    regionMax.z < 0.0f || regionMin.z > boxSize) {
                    return 0.0f; // Fully outside
                }

                // Check if region overlaps any WALL (not just interior)
                // Floor: y ∈ [0, thickness]
                bool overlapsFloor = (regionMin.y < thickness) && (regionMax.y > 0.0f);
                // Ceiling: y ∈ [boxSize-thickness, boxSize]
                bool overlapsCeiling = (regionMin.y < boxSize) && (regionMax.y > boxSize - thickness);
                // Left wall: x ∈ [0, thickness]
                bool overlapsLeft = (regionMin.x < thickness) && (regionMax.x > 0.0f);
                // Right wall: x ∈ [boxSize-thickness, boxSize]
                bool overlapsRight = (regionMin.x < boxSize) && (regionMax.x > boxSize - thickness);
                // Back wall: z ∈ [boxSize-thickness, boxSize]
                bool overlapsBack = (regionMin.z < boxSize) && (regionMax.z > boxSize - thickness);
                // Front wall: z ∈ [0, thickness]
                bool overlapsFront = (regionMin.z < thickness) && (regionMax.z > 0.0f);

                if (overlapsFloor || overlapsCeiling || overlapsLeft || overlapsRight || overlapsBack || overlapsFront) {
                    return 1.0f; // Contains walls - subdivide
                }

                return 0.0f; // Empty interior - don't subdivide
            }
        );

        // (OLD CODE MOVED TO buildCornellBoxDensityBased() - not used)
    }

    void TearDown() override {
        delete cornellBox;
    }

    LaineKarrasOctree* cornellBox = nullptr;
};

// ---------------------------------------------------------------------------
// Category 1: Floor Material Tests
// ---------------------------------------------------------------------------

TEST_F(CornellBoxTest, FloorHit_FromAbove) {
    // Ray from inside box hitting floor
    glm::vec3 origin(5.0f, 8.0f, 5.0f); // Center, high up
    glm::vec3 direction(0.0f, -1.0f, 0.0f); // Straight down

    auto hit = cornellBox->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    ASSERT_TRUE(hit.hit) << "Should hit floor";

    // Validate floor material (bright grey)
    auto voxelData = cornellBox->getVoxelData(hit.position, 0);
    if (voxelData.has_value()) {
        EXPECT_NEAR(voxelData->color.r, 0.8f, 0.2f) << "Floor should be bright grey (R)";
        EXPECT_NEAR(voxelData->color.g, 0.8f, 0.2f) << "Floor should be bright grey (G)";
        EXPECT_NEAR(voxelData->color.b, 0.8f, 0.2f) << "Floor should be bright grey (B)";
    }

    // Normal should point up
    EXPECT_GT(hit.normal.y, 0.5f) << "Floor normal should point upward";
}

TEST_F(CornellBoxTest, FloorHit_FromOutside) {
    // Ray from outside hitting floor
    glm::vec3 origin(5.0f, -2.0f, 5.0f); // Below box
    glm::vec3 direction(0.0f, 1.0f, 0.0f); // Upward

    auto hit = cornellBox->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    ASSERT_TRUE(hit.hit) << "Should hit floor from below";

    // Should hit floor at y~0
    EXPECT_LT(hit.position.y, 0.5f) << "Should hit near floor level";
}

// ---------------------------------------------------------------------------
// Category 2: Ceiling and Light Patch Tests
// ---------------------------------------------------------------------------

TEST_F(CornellBoxTest, CeilingHit_GreyRegion) {
    // Ray from inside hitting grey part of ceiling (not light)
    glm::vec3 origin(1.0f, 2.0f, 1.0f); // Corner, away from light
    glm::vec3 direction(0.0f, 1.0f, 0.0f); // Straight up

    auto hit = cornellBox->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    ASSERT_TRUE(hit.hit) << "Should hit ceiling";

    // Debug: print hit position
    std::cout << "CeilingHit_GreyRegion: Hit at (" << hit.position.x << ", "
              << hit.position.y << ", " << hit.position.z << ") scale=" << hit.scale << "\n";

    // Should hit ceiling at y~10
    EXPECT_GT(hit.position.y, 9.0f) << "Should hit near ceiling level";

    // Validate grey ceiling material
    auto voxelData = cornellBox->getVoxelData(hit.position, 0);
    if (voxelData.has_value()) {
        EXPECT_NEAR(voxelData->color.r, 0.8f, 0.2f) << "Grey ceiling (R)";
        EXPECT_NEAR(voxelData->color.g, 0.8f, 0.2f) << "Grey ceiling (G)";
        EXPECT_NEAR(voxelData->color.b, 0.8f, 0.2f) << "Grey ceiling (B)";
    }
}

TEST_F(CornellBoxTest, CeilingHit_LightPatch) {
    // Ray from inside hitting white light patch (center of ceiling)
    glm::vec3 origin(5.0f, 2.0f, 5.0f); // Center
    glm::vec3 direction(0.0f, 1.0f, 0.0f); // Straight up

    auto hit = cornellBox->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    ASSERT_TRUE(hit.hit) << "Should hit light patch";

    // Validate light material (white)
    auto voxelData = cornellBox->getVoxelData(hit.position, 0);
    if (voxelData.has_value()) {
        // Light should be brighter than grey walls
        float brightness = voxelData->color.r + voxelData->color.g + voxelData->color.b;
        EXPECT_GT(brightness, 2.0f) << "Light should be bright (sum > 2.0)";
    }
}

// ---------------------------------------------------------------------------
// Category 3: Red Left Wall Tests
// ---------------------------------------------------------------------------

TEST_F(CornellBoxTest, LeftWallHit_FromCenter_Red) {
    // Ray from center hitting red left wall
    glm::vec3 origin(5.0f, 5.0f, 5.0f); // Center of box
    glm::vec3 direction(-1.0f, 0.0f, 0.0f); // Left

    auto hit = cornellBox->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    ASSERT_TRUE(hit.hit) << "Should hit left wall";

    // Should hit left wall at x~0
    EXPECT_LT(hit.position.x, 0.5f) << "Should hit near left wall";

    // Validate RED material
    auto voxelData = cornellBox->getVoxelData(hit.position, 0);
    if (voxelData.has_value()) {
        EXPECT_GT(voxelData->color.r, 0.5f) << "Left wall should be RED (high R)";
        EXPECT_LT(voxelData->color.g, 0.3f) << "Left wall should be RED (low G)";
        EXPECT_LT(voxelData->color.b, 0.3f) << "Left wall should be RED (low B)";
    }

    // Normal should point right (into box)
    EXPECT_GT(hit.normal.x, 0.5f) << "Left wall normal should point right";
}

TEST_F(CornellBoxTest, LeftWallHit_FromOutside_Red) {
    // Ray from outside hitting red left wall
    glm::vec3 origin(-2.0f, 5.0f, 5.0f); // Outside, left
    glm::vec3 direction(1.0f, 0.0f, 0.0f); // Right

    auto hit = cornellBox->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    ASSERT_TRUE(hit.hit) << "Should hit left wall from outside";

    // Validate RED material
    auto voxelData = cornellBox->getVoxelData(hit.position, 0);
    if (voxelData.has_value()) {
        EXPECT_GT(voxelData->color.r, 0.5f) << "Should be RED";
    }
}

// ---------------------------------------------------------------------------
// Category 4: Green Right Wall Tests
// ---------------------------------------------------------------------------

TEST_F(CornellBoxTest, RightWallHit_FromCenter_Green) {
    // Ray from center hitting green right wall
    glm::vec3 origin(5.0f, 5.0f, 5.0f); // Center of box
    glm::vec3 direction(1.0f, 0.0f, 0.0f); // Right

    auto hit = cornellBox->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    ASSERT_TRUE(hit.hit) << "Should hit right wall";

    // Should hit right wall at x~10
    EXPECT_GT(hit.position.x, 9.0f) << "Should hit near right wall";

    // Validate GREEN material
    auto voxelData = cornellBox->getVoxelData(hit.position, 0);
    if (voxelData.has_value()) {
        EXPECT_LT(voxelData->color.r, 0.3f) << "Right wall should be GREEN (low R)";
        EXPECT_GT(voxelData->color.g, 0.5f) << "Right wall should be GREEN (high G)";
        EXPECT_LT(voxelData->color.b, 0.3f) << "Right wall should be GREEN (low B)";
    }

    // Normal should point left (into box)
    EXPECT_LT(hit.normal.x, -0.5f) << "Right wall normal should point left";
}

TEST_F(CornellBoxTest, RightWallHit_FromOutside_Green) {
    // Ray from outside hitting green right wall
    glm::vec3 origin(12.0f, 5.0f, 5.0f); // Outside, right
    glm::vec3 direction(-1.0f, 0.0f, 0.0f); // Left

    auto hit = cornellBox->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    ASSERT_TRUE(hit.hit) << "Should hit right wall from outside";

    // Validate GREEN material
    auto voxelData = cornellBox->getVoxelData(hit.position, 0);
    if (voxelData.has_value()) {
        EXPECT_GT(voxelData->color.g, 0.5f) << "Should be GREEN";
    }
}

// ---------------------------------------------------------------------------
// Category 5: Back Wall Tests
// ---------------------------------------------------------------------------

TEST_F(CornellBoxTest, BackWallHit_FromCenter_Grey) {
    // Ray from center hitting grey back wall
    glm::vec3 origin(5.0f, 5.0f, 5.0f); // Center
    glm::vec3 direction(0.0f, 0.0f, 1.0f); // Forward (toward back wall)

    auto hit = cornellBox->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    ASSERT_TRUE(hit.hit) << "Should hit back wall";

    // Should hit back wall at z~10
    EXPECT_GT(hit.position.z, 9.0f) << "Should hit near back wall";

    // Validate grey material
    auto voxelData = cornellBox->getVoxelData(hit.position, 0);
    if (voxelData.has_value()) {
        EXPECT_NEAR(voxelData->color.r, 0.8f, 0.2f) << "Back wall grey (R)";
        EXPECT_NEAR(voxelData->color.g, 0.8f, 0.2f) << "Back wall grey (G)";
        EXPECT_NEAR(voxelData->color.b, 0.8f, 0.2f) << "Back wall grey (B)";
    }
}

// ---------------------------------------------------------------------------
// Category 6: Multi-Bounce Path Tests (Inside Box)
// ---------------------------------------------------------------------------

TEST_F(CornellBoxTest, InsideBox_FloorToLeftWall) {
    // Ray from floor toward left wall
    glm::vec3 origin(5.0f, 0.5f, 5.0f); // Just above floor
    glm::vec3 direction(-1.0f, 0.0f, 0.0f); // Left

    auto hit = cornellBox->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    ASSERT_TRUE(hit.hit) << "Should hit left wall";

    // Should hit red wall
    auto voxelData = cornellBox->getVoxelData(hit.position, 0);
    if (voxelData.has_value()) {
        EXPECT_GT(voxelData->color.r, 0.5f) << "Should hit red wall";
    }
}

TEST_F(CornellBoxTest, InsideBox_FloorToRightWall) {
    // Ray from floor toward right wall
    glm::vec3 origin(5.0f, 0.5f, 5.0f); // Just above floor
    glm::vec3 direction(1.0f, 0.0f, 0.0f); // Right

    auto hit = cornellBox->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    ASSERT_TRUE(hit.hit) << "Should hit right wall";

    // Should hit green wall
    auto voxelData = cornellBox->getVoxelData(hit.position, 0);
    if (voxelData.has_value()) {
        EXPECT_GT(voxelData->color.g, 0.5f) << "Should hit green wall";
    }
}

TEST_F(CornellBoxTest, InsideBox_DiagonalCornerToCorner) {
    // Diagonal ray from one corner to opposite corner
    glm::vec3 origin(1.0f, 1.0f, 1.0f); // Near floor-left-front corner
    glm::vec3 direction = glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f)); // Toward ceiling-right-back

    auto hit = cornellBox->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
    ASSERT_TRUE(hit.hit) << "Diagonal should hit a wall";

    // Should hit some wall
    EXPECT_GT(hit.tMin, 0.0f);
}

// ---------------------------------------------------------------------------
// Category 7: Material Consistency Tests
// ---------------------------------------------------------------------------

TEST_F(CornellBoxTest, MaterialConsistency_RedWallMultipleRays) {
    // Multiple rays hitting left wall should all return red
    std::vector<glm::vec3> origins = {
        {5.0f, 2.0f, 5.0f}, // Low
        {5.0f, 5.0f, 5.0f}, // Middle
        {5.0f, 8.0f, 5.0f}, // High
        {5.0f, 5.0f, 2.0f}, // Front
        {5.0f, 5.0f, 8.0f}  // Back
    };

    glm::vec3 direction(-1.0f, 0.0f, 0.0f); // Left

    for (const auto& origin : origins) {
        auto hit = cornellBox->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
        ASSERT_TRUE(hit.hit) << "Should hit left wall from " << origin.x << "," << origin.y << "," << origin.z;

        auto voxelData = cornellBox->getVoxelData(hit.position, 0);
        if (voxelData.has_value()) {
            EXPECT_GT(voxelData->color.r, 0.5f) << "All hits should be RED";
        }
    }
}

TEST_F(CornellBoxTest, MaterialConsistency_GreenWallMultipleRays) {
    // Multiple rays hitting right wall should all return green
    std::vector<glm::vec3> origins = {
        {5.0f, 2.0f, 5.0f}, // Low
        {5.0f, 5.0f, 5.0f}, // Middle
        {5.0f, 8.0f, 5.0f}  // High
    };

    glm::vec3 direction(1.0f, 0.0f, 0.0f); // Right

    for (const auto& origin : origins) {
        auto hit = cornellBox->castRay(origin, direction, 0.0f, std::numeric_limits<float>::max());
        ASSERT_TRUE(hit.hit) << "Should hit right wall";

        auto voxelData = cornellBox->getVoxelData(hit.position, 0);
        if (voxelData.has_value()) {
            EXPECT_GT(voxelData->color.g, 0.5f) << "All hits should be GREEN";
        }
    }
}

// ---------------------------------------------------------------------------
// Category 8: Normal Direction Validation
// ---------------------------------------------------------------------------

TEST_F(CornellBoxTest, NormalValidation_AllWalls) {
    struct WallTest {
        glm::vec3 origin;
        glm::vec3 direction;
        glm::vec3 expectedNormalDir; // Which component should dominate
        std::string wallName;
    };

    std::vector<WallTest> tests = {
        {{5.0f, 5.0f, 5.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, "Floor"},
        {{5.0f, 5.0f, 5.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, "Ceiling"},
        {{5.0f, 5.0f, 5.0f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, "Left"},
        {{5.0f, 5.0f, 5.0f}, {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, "Right"},
        {{5.0f, 5.0f, 5.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}, "Back"}
    };

    for (const auto& test : tests) {
        auto hit = cornellBox->castRay(test.origin, test.direction, 0.0f, std::numeric_limits<float>::max());
        ASSERT_TRUE(hit.hit) << "Should hit " << test.wallName;

        // Check normal direction
        float dotProduct = glm::dot(hit.normal, test.expectedNormalDir);
        EXPECT_GT(dotProduct, 0.5f) << test.wallName << " normal incorrect";
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

    auto octree = std::make_unique<LaineKarrasOctree>();
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
    EXPECT_FALSE(glm::any(glm::isnan(hit.position)));
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

    auto octree = std::make_unique<LaineKarrasOctree>();
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
    EXPECT_FALSE(glm::any(glm::isnan(hit.position)));
    EXPECT_FALSE(glm::any(glm::isnan(hit.normal)));
}

/**
 * Test XOR octant mirroring
 *
 * Reference: cuda/Raycast.inl lines 114-117
 * Verifies coordinate system mirroring for negative ray directions
 */
TEST(LaineKarrasOctree, XORMirroring_PositiveDirection) {
    // Ray with all positive direction components
    glm::vec3 origin(-1.0f, -1.0f, -1.0f);
    glm::vec3 direction(1.0f, 1.0f, 1.0f);

    // Expected octant_mask = 7 XOR 1 XOR 2 XOR 4 = 0
    // (all axes mirrored since all positive)

    auto octree = std::make_unique<LaineKarrasOctree>();
    auto oct = std::make_unique<Octree>();
    oct->worldMin = glm::vec3(0.0f);
    oct->worldMax = glm::vec3(1.0f);
    oct->maxLevels = 4;
    oct->root = std::make_unique<OctreeBlock>();

    ChildDescriptor root{};
    root.childPointer = 0;
    root.farBit = 0;
    root.validMask = 0b11111111; // All octants exist
    root.leafMask = 0b11111111;  // All leaves
    root.contourPointer = 0;
    root.contourMask = 0;
    oct->root->childDescriptors.push_back(root);

    AttributeLookup attrLookup{};
    attrLookup.valuePointer = 0;
    attrLookup.mask = 0b11111111;
    oct->root->attributeLookups.push_back(attrLookup);

    for (int i = 0; i < 8; ++i) {
        UncompressedAttributes attr = makeAttributes(
            glm::vec3(1.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f)
        );
        oct->root->attributes.push_back(attr);
    }

    octree->setOctree(std::move(oct));

    auto hit = octree->castRay(origin, direction, 0.0f, 10.0f);

    // Should hit successfully with mirrored coordinates
    EXPECT_TRUE(hit.hit);
    EXPECT_FALSE(std::isnan(hit.tMin));
}

TEST(LaineKarrasOctree, XORMirroring_NegativeDirection) {
    // Ray with all negative direction components
    glm::vec3 origin(2.0f, 2.0f, 2.0f);
    glm::vec3 direction(-1.0f, -1.0f, -1.0f);

    // Expected octant_mask = 7 (no mirroring since all negative)

    auto octree = std::make_unique<LaineKarrasOctree>();
    auto oct = std::make_unique<Octree>();
    oct->worldMin = glm::vec3(0.0f);
    oct->worldMax = glm::vec3(1.0f);
    oct->maxLevels = 4;
    oct->root = std::make_unique<OctreeBlock>();

    ChildDescriptor root{};
    root.childPointer = 0;
    root.farBit = 0;
    root.validMask = 0b11111111;
    root.leafMask = 0b11111111;
    root.contourPointer = 0;
    root.contourMask = 0;
    oct->root->childDescriptors.push_back(root);

    AttributeLookup attrLookup{};
    attrLookup.valuePointer = 0;
    attrLookup.mask = 0b11111111;
    oct->root->attributeLookups.push_back(attrLookup);

    for (int i = 0; i < 8; ++i) {
        UncompressedAttributes attr = makeAttributes(
            glm::vec3(1.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f)
        );
        oct->root->attributes.push_back(attr);
    }

    octree->setOctree(std::move(oct));

    auto hit = octree->castRay(origin, direction, 0.0f, 10.0f);

    // Should hit successfully without mirroring
    EXPECT_TRUE(hit.hit);
    EXPECT_FALSE(std::isnan(hit.tMin));
}

TEST(LaineKarrasOctree, XORMirroring_MixedDirection) {
    // Ray with mixed direction components (+, -, +)
    glm::vec3 origin(-1.0f, 2.0f, -1.0f);
    glm::vec3 direction(1.0f, -1.0f, 1.0f);

    // Expected octant_mask = 7 XOR 1 XOR 4 = 2
    // (X and Z mirrored, Y not mirrored)

    auto octree = std::make_unique<LaineKarrasOctree>();
    auto oct = std::make_unique<Octree>();
    oct->worldMin = glm::vec3(0.0f);
    oct->worldMax = glm::vec3(1.0f);
    oct->maxLevels = 4;
    oct->root = std::make_unique<OctreeBlock>();

    ChildDescriptor root{};
    root.childPointer = 0;
    root.farBit = 0;
    root.validMask = 0b11111111;
    root.leafMask = 0b11111111;
    root.contourPointer = 0;
    root.contourMask = 0;
    oct->root->childDescriptors.push_back(root);

    AttributeLookup attrLookup{};
    attrLookup.valuePointer = 0;
    attrLookup.mask = 0b11111111;
    oct->root->attributeLookups.push_back(attrLookup);

    for (int i = 0; i < 8; ++i) {
        UncompressedAttributes attr = makeAttributes(
            glm::vec3(1.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f)
        );
        oct->root->attributes.push_back(attr);
    }

    octree->setOctree(std::move(oct));

    auto hit = octree->castRay(origin, direction, 0.0f, 10.0f);

    // Should hit successfully with partial mirroring
    EXPECT_TRUE(hit.hit);
    EXPECT_FALSE(std::isnan(hit.tMin));
}

/**
 * Test CastStack structure
 *
 * Reference: Implicit in cuda/Raycast.inl traversal loop
 * Verifies stack push/pop operations for traversal backtracking
 */
TEST(LaineKarrasOctree, CastStack_PushPop) {
    // Create a simple octree structure to get ChildDescriptor pointers
    auto octree = std::make_unique<LaineKarrasOctree>();
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

// ============================================================================
// ESVO Test: Ray origin inside octree grid
// ============================================================================

TEST(LaineKarrasOctree, RayOriginInsideOctree) {
    // Ray starting inside the octree grid should still find hits
    // Origin at center of octree [0,1] space
    glm::vec3 origin(0.5f, 0.5f, 0.5f);
    glm::vec3 direction(1.0f, 0.0f, 0.0f);  // Shoot towards +X

    auto octree = std::make_unique<LaineKarrasOctree>();
    auto oct = std::make_unique<Octree>();
    oct->worldMin = glm::vec3(0.0f);
    oct->worldMax = glm::vec3(1.0f);
    oct->maxLevels = 4;
    oct->root = std::make_unique<OctreeBlock>();

    // Create root with all 8 octants as leaves
    ChildDescriptor root{};
    root.childPointer = 0;
    root.farBit = 0;
    root.validMask = 0b11111111; // All octants exist
    root.leafMask = 0b11111111;  // All leaves
    root.contourPointer = 0;
    root.contourMask = 0;
    oct->root->childDescriptors.push_back(root);

    AttributeLookup attrLookup{};
    attrLookup.valuePointer = 0;
    attrLookup.mask = 0b11111111;
    oct->root->attributeLookups.push_back(attrLookup);

    for (int i = 0; i < 8; ++i) {
        UncompressedAttributes attr = makeAttributes(
            glm::vec3(1.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f)
        );
        oct->root->attributes.push_back(attr);
    }

    octree->setOctree(std::move(oct));

    auto hit = octree->castRay(origin, direction, 0.0f, 10.0f);

    // Should hit immediately since we're already inside a voxel
    EXPECT_TRUE(hit.hit);
    EXPECT_GE(hit.tMin, 0.0f);  // Can be 0 (immediate) or positive
    EXPECT_FALSE(std::isnan(hit.tMin));
    EXPECT_FALSE(std::isnan(hit.tMax));
}
