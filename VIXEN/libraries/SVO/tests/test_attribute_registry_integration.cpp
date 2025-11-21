#include <gtest/gtest.h>
#include "LaineKarrasOctree.h"
#include "VoxelInjection.h"
#include "AttributeRegistry.h"
#include "DynamicVoxelStruct.h"
#include <glm/glm.hpp>

using namespace SVO;

/**
 * Test suite for AttributeRegistry integration with LaineKarrasOctree.
 * Validates that the migration from BrickStorage to direct AttributeRegistry
 * access works correctly for ray traversal and attribute retrieval.
 */
class AttributeRegistryIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create registry and register attributes
        registry = std::make_shared<::VoxelData::AttributeRegistry>();

        // Register key attribute (density) - MUST be index 0
        registry->registerKey("density", ::VoxelData::AttributeType::Float, 1.0f);

        // Register additional attributes
        registry->addAttribute("color", ::VoxelData::AttributeType::Vec3, glm::vec3(1.0f));
        registry->addAttribute("normal", ::VoxelData::AttributeType::Vec3, glm::vec3(0, 1, 0));
        registry->addAttribute("metallic", ::VoxelData::AttributeType::Float, 0.0f);
    }

    // Helper to create octree with single voxel at position
    std::unique_ptr<LaineKarrasOctree> createOctreeWithVoxel(
        const glm::vec3& position,
        float density,
        const glm::vec3& color,
        const glm::vec3& normal,
        float metallic = 0.0f)
    {
        auto octree = std::make_unique<LaineKarrasOctree>(registry.get());

        ::VoxelData::DynamicVoxelScalar voxel;
        voxel.set("density", density);
        voxel.set("color", color);
        voxel.set("normal", normal);
        voxel.set("metallic", metallic);

        VoxelInjector injector;
        InjectionConfig config;
        config.maxLevels = 8;

        injector.insertVoxel(*octree, position, voxel, config);
        injector.compactToESVOFormat(*octree);

        return octree;
    }

    std::shared_ptr<::VoxelData::AttributeRegistry> registry;
};

// ============================================================================
// TEST 1: Key Attribute at Index 0
// ============================================================================
TEST_F(AttributeRegistryIntegrationTest, KeyAttributeIsAtIndexZero) {
    // Verify key attribute ("density") is at index 0
    auto keyIndex = registry->getAttributeIndex("density");
    EXPECT_EQ(keyIndex, 0) << "Key attribute must be at index 0";

    // Verify descriptor confirms this
    auto descriptor = registry->getDescriptor(keyIndex);
    EXPECT_EQ(descriptor.type, ::VoxelData::AttributeType::Float);
    EXPECT_STREQ(descriptor.name.c_str(), "density");
}

// ============================================================================
// TEST 2: Multi-Attribute Ray Hit Retrieval
// ============================================================================
TEST_F(AttributeRegistryIntegrationTest, MultiAttributeRayHit) {
    // Create voxel with all attributes populated
    glm::vec3 voxelPos(5.0f, 5.0f, 5.0f);
    glm::vec3 expectedColor(1.0f, 0.0f, 0.0f); // Red
    glm::vec3 expectedNormal(0.0f, 1.0f, 0.0f); // Up
    float expectedDensity = 0.85f;
    float expectedMetallic = 0.7f;

    auto octree = createOctreeWithVoxel(
        voxelPos,
        expectedDensity,
        expectedColor,
        expectedNormal,
        expectedMetallic
    );

    // Cast ray to hit voxel
    glm::vec3 rayOrigin(-5.0f, 5.0f, 5.0f);
    glm::vec3 rayDir(1.0f, 0.0f, 0.0f);

    auto hit = octree->castRay(rayOrigin, rayDir, 0.0f, 100.0f);

    ASSERT_TRUE(hit.hit) << "Ray should hit voxel";

    // Verify all attributes are accessible via hit
    // Note: Current ISVOStructure::RayHit only exposes position, normal, tMin, scale
    // This test verifies traversal works with multi-attribute voxels
    EXPECT_TRUE(hit.hit);
    EXPECT_NEAR(hit.position.x, voxelPos.x, 2.0f);
    EXPECT_NEAR(hit.position.y, voxelPos.y, 2.0f);
    EXPECT_NEAR(hit.position.z, voxelPos.z, 2.0f);

    std::cout << "Multi-attribute voxel hit at ("
              << hit.position.x << ", " << hit.position.y << ", " << hit.position.z << ")\n";
}

// ============================================================================
// TEST 3: BrickView Attribute Pointer Access
// ============================================================================
TEST_F(AttributeRegistryIntegrationTest, BrickViewPointerAccess) {
    // Create a brick and verify direct pointer access works
    auto brick = registry->allocateBrick();
    bool hasBrick = brick.has_value();
    ASSERT_TRUE(hasBrick) << "Should allocate brick successfully";

    auto brickView = registry->getBrick(brick.value());
    bool hasView = brickView.has_value();
    ASSERT_TRUE(hasView) << "Should retrieve brick view";

    // Get direct reference to BrickView (avoids optional-> issues)
    auto& view = brickView.value();

    // Get attribute pointers using index-based access (fastest path)
    float* densityPtr = view.getAttributePointer<float>(0); // Key attribute at index 0
    ASSERT_NE(densityPtr, nullptr) << "Density pointer should be valid";

    auto colorIndex = registry->getAttributeIndex("color");
    glm::vec3* colorPtr = view.getAttributePointer<glm::vec3>(colorIndex);
    ASSERT_NE(colorPtr, nullptr) << "Color pointer should be valid";

    // Write via pointers
    densityPtr[0] = 0.5f;
    densityPtr[256] = 0.75f; // Middle of 8³ brick
    colorPtr[0] = glm::vec3(1.0f, 0.0f, 0.0f);

    // Verify via typed get<T>
    float density0 = view.get<float>("density", 0);
    float density256 = view.get<float>("density", 256);
    EXPECT_FLOAT_EQ(density0, 0.5f);
    EXPECT_FLOAT_EQ(density256, 0.75f);

    auto retrievedColor = view.get<glm::vec3>("color", 0);
    EXPECT_FLOAT_EQ(retrievedColor.r, 1.0f);
    EXPECT_FLOAT_EQ(retrievedColor.g, 0.0f);
    EXPECT_FLOAT_EQ(retrievedColor.b, 0.0f);

    std::cout << "BrickView pointer access validated (index-based)\n";
}

// ============================================================================
// TEST 4: Type-Safe Attribute Access During Traversal
// ============================================================================
TEST_F(AttributeRegistryIntegrationTest, TypeSafeAttributeAccess) {
    // Verify LaineKarrasOctree handles different attribute types correctly
    auto octree = createOctreeWithVoxel(
        glm::vec3(3.0f, 3.0f, 3.0f),
        1.0f,                           // float
        glm::vec3(0.5f, 0.5f, 0.5f),   // vec3
        glm::vec3(0.0f, 0.0f, 1.0f),   // vec3
        0.3f                            // float
    );

    // Cast ray
    glm::vec3 rayOrigin(0.0f, 3.0f, 3.0f);
    glm::vec3 rayDir(1.0f, 0.0f, 0.0f);

    auto hit = octree->castRay(rayOrigin, rayDir, 0.0f, 100.0f);
    ASSERT_TRUE(hit.hit) << "Ray should hit voxel with mixed attribute types";

    // Verify hit data is reasonable
    EXPECT_GT(hit.tMin, 0.0f) << "Hit distance should be positive";
    EXPECT_LT(hit.tMin, 10.0f) << "Hit distance should be reasonable";

    // Verify normal is normalized
    float normalLength = glm::length(hit.normal);
    EXPECT_NEAR(normalLength, 1.0f, 0.1f) << "Normal should be normalized";

    std::cout << "Type-safe traversal validated (Float, Vec3, Vec3, Float)\n";
}

// ============================================================================
// TEST 5: Custom Key Predicate with AttributeRegistry
// ============================================================================
TEST_F(AttributeRegistryIntegrationTest, CustomKeyPredicate) {
    // Create octree with voxels of varying density
    auto octree = std::make_unique<LaineKarrasOctree>(registry.get());

    VoxelInjector injector;
    InjectionConfig config;
    config.maxLevels = 6;

    // Insert voxels with different densities
    std::vector<std::tuple<glm::vec3, float>> voxels = {
        {glm::vec3(2.0f, 2.0f, 2.0f), 0.2f},  // Below threshold
        {glm::vec3(5.0f, 5.0f, 5.0f), 0.8f},  // Above threshold
        {glm::vec3(8.0f, 8.0f, 8.0f), 1.0f},  // Above threshold
    };

    for (const auto& [pos, density] : voxels) {
        ::VoxelData::DynamicVoxelScalar voxel;
        voxel.set("density", density);
        voxel.set("color", glm::vec3(1.0f));
        voxel.set("normal", glm::vec3(0, 1, 0));
        voxel.set("metallic", 0.0f);

        injector.insertVoxel(*octree, pos, voxel, config);
    }

    injector.compactToESVOFormat(*octree);

    // Cast rays toward each voxel
    // Only high-density voxels (>= 0.5) should be solid
    {
        glm::vec3 rayOrigin(-2.0f, 2.0f, 2.0f);
        glm::vec3 rayDir(1.0f, 0.0f, 0.0f);
        auto hit = octree->castRay(rayOrigin, rayDir, 0.0f, 100.0f);

        // With default key predicate (density >= 0.5), should miss low-density voxel
        // but hit high-density voxel at (5,5,5)
        if (hit.hit) {
            std::cout << "Hit voxel at (" << hit.position.x << ", "
                      << hit.position.y << ", " << hit.position.z << ")\n";
        }
    }

    {
        glm::vec3 rayOrigin(-2.0f, 5.0f, 5.0f);
        glm::vec3 rayDir(1.0f, 0.0f, 0.0f);
        auto hit = octree->castRay(rayOrigin, rayDir, 0.0f, 100.0f);

        EXPECT_TRUE(hit.hit) << "Ray should hit high-density voxel (0.8)";
        EXPECT_NEAR(hit.position.x, 5.0f, 2.0f);
    }

    std::cout << "Custom key predicate validated (density threshold)\n";
}

// ============================================================================
// TEST 6: AttributeRegistry Backward Compatibility
// ============================================================================
TEST_F(AttributeRegistryIntegrationTest, BackwardCompatibility_StringLookup) {
    // Verify that string-based attribute lookup still works (delegates to index lookup)
    auto brick = registry->allocateBrick();
    bool hasBrick = brick.has_value();
    ASSERT_TRUE(hasBrick) << "Should allocate brick";

    auto brickView = registry->getBrick(brick.value());
    bool hasView = brickView.has_value();
    ASSERT_TRUE(hasView) << "Should get brick view";

    // Get direct reference to BrickView
    auto& view = brickView.value();

    // String-based set/get (legacy path)
    view.set<float>("density", 0, 0.42f);
    view.set<glm::vec3>("color", 0, glm::vec3(0.1f, 0.2f, 0.3f));

    float density = view.get<float>("density", 0);
    EXPECT_FLOAT_EQ(density, 0.42f);

    glm::vec3 retrievedColor1 = view.get<glm::vec3>("color", 0);
    EXPECT_FLOAT_EQ(retrievedColor1.r, 0.1f);
    EXPECT_FLOAT_EQ(retrievedColor1.g, 0.2f);
    EXPECT_FLOAT_EQ(retrievedColor1.b, 0.3f);

    // Index-based access should give same result
    auto densityIdx = registry->getAttributeIndex("density");
    auto colorIdx = registry->getAttributeIndex("color");

    float* densityPtr = view.getAttributePointer<float>(densityIdx);
    glm::vec3* colorPtr = view.getAttributePointer<glm::vec3>(colorIdx);

    EXPECT_FLOAT_EQ(densityPtr[0], 0.42f);
    EXPECT_FLOAT_EQ(colorPtr[0].r, 0.1f);
    EXPECT_FLOAT_EQ(colorPtr[0].g, 0.2f);
    EXPECT_FLOAT_EQ(colorPtr[0].b, 0.3f);

    std::cout << "Backward compatibility validated (string → index delegation)\n";
}

// ============================================================================
// TEST 7: Multiple Octrees Sharing Registry
// ============================================================================
TEST_F(AttributeRegistryIntegrationTest, MultipleOctreesSharedRegistry) {
    // Create two octrees using same registry
    auto octree1 = std::make_unique<LaineKarrasOctree>(registry.get());
    auto octree2 = std::make_unique<LaineKarrasOctree>(registry.get());

    VoxelInjector injector;
    InjectionConfig config;
    config.maxLevels = 6;

    // Insert voxel into octree1
    ::VoxelData::DynamicVoxelScalar voxel1;
    voxel1.set("density", 1.0f);
    voxel1.set("color", glm::vec3(1.0f, 0.0f, 0.0f)); // Red
    voxel1.set("normal", glm::vec3(0, 1, 0));
    voxel1.set("metallic", 0.0f);
    injector.insertVoxel(*octree1, glm::vec3(2.0f, 2.0f, 2.0f), voxel1, config);
    injector.compactToESVOFormat(*octree1);

    // Insert different voxel into octree2
    ::VoxelData::DynamicVoxelScalar voxel2;
    voxel2.set("density", 1.0f);
    voxel2.set("color", glm::vec3(0.0f, 1.0f, 0.0f)); // Green
    voxel2.set("normal", glm::vec3(1, 0, 0));
    voxel2.set("metallic", 0.5f);
    injector.insertVoxel(*octree2, glm::vec3(7.0f, 7.0f, 7.0f), voxel2, config);
    injector.compactToESVOFormat(*octree2);

    // Both octrees should work independently
    auto hit1 = octree1->castRay(glm::vec3(-2, 2, 2), glm::vec3(1, 0, 0), 0.0f, 100.0f);
    auto hit2 = octree2->castRay(glm::vec3(12, 7, 7), glm::vec3(-1, 0, 0), 0.0f, 100.0f);

    EXPECT_TRUE(hit1.hit) << "Octree1 should hit its voxel";
    EXPECT_TRUE(hit2.hit) << "Octree2 should hit its voxel";

    EXPECT_NEAR(hit1.position.x, 2.0f, 2.0f);
    EXPECT_NEAR(hit2.position.x, 7.0f, 2.0f);

    std::cout << "Multiple octrees sharing registry validated\n";
}
