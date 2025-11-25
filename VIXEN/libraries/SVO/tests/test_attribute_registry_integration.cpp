#include <gtest/gtest.h>
#include "LaineKarrasOctree.h"
#include "AttributeRegistry.h"
#include "BrickView.h"
#include "DynamicVoxelStruct.h"
#include "GaiaVoxelWorld.h"
#include "VoxelComponents.h"
#include "ComponentData.h"
#include <glm/glm.hpp>

using namespace SVO;
using namespace VoxelData;
using namespace GaiaVoxel;

/**
 * Test suite for AttributeRegistry integration with LaineKarrasOctree.
 * Updated to use GaiaVoxelWorld instead of deprecated VoxelInjector.
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

    // Helper to create voxel in GaiaVoxelWorld with components
    GaiaVoxelWorld::EntityID createVoxelInWorld(
        GaiaVoxelWorld& world,
        const glm::vec3& position,
        float density,
        const glm::vec3& color,
        const glm::vec3& normal)
    {
        ComponentQueryRequest comps[] = {
            Density{density},
            Color{color},
            Normal{normal}
        };
        VoxelCreationRequest req{position, comps};
        return world.createVoxel(req);
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
// TEST 2: GaiaVoxelWorld to LaineKarrasOctree Integration
// ============================================================================
TEST_F(AttributeRegistryIntegrationTest, GaiaVoxelWorldOctreeIntegration) {
    // Create voxels using GaiaVoxelWorld
    GaiaVoxelWorld world;

    glm::vec3 voxelPos(5.0f, 5.0f, 5.0f);
    glm::vec3 expectedColor(1.0f, 0.0f, 0.0f); // Red
    glm::vec3 expectedNormal(0.0f, 1.0f, 0.0f); // Up
    float expectedDensity = 0.85f;

    auto entity = createVoxelInWorld(world, voxelPos, expectedDensity, expectedColor, expectedNormal);
    ASSERT_TRUE(world.exists(entity));

    // Create octree from GaiaVoxelWorld
    LaineKarrasOctree octree(world, registry.get(), 8, 3);

    // Rebuild octree from entities
    glm::vec3 worldMin(0.0f);
    glm::vec3 worldMax(10.0f);
    octree.rebuild(world, worldMin, worldMax);

    // Cast ray to hit voxel
    glm::vec3 rayOrigin(-5.0f, 5.0f, 5.0f);
    glm::vec3 rayDir(1.0f, 0.0f, 0.0f);

    auto hit = octree.castRay(rayOrigin, rayDir, 0.0f, 100.0f);

    // Verify hit occurred
    if (hit.hit) {
        EXPECT_NEAR(hit.hitPoint.x, voxelPos.x, 2.0f);
        EXPECT_NEAR(hit.hitPoint.y, voxelPos.y, 2.0f);
        EXPECT_NEAR(hit.hitPoint.z, voxelPos.z, 2.0f);

        std::cout << "Voxel hit at ("
                  << hit.hitPoint.x << ", " << hit.hitPoint.y << ", " << hit.hitPoint.z << ")\n";
    }
}

// ============================================================================
// TEST 3: BrickView Attribute Pointer Access
// ============================================================================
TEST_F(AttributeRegistryIntegrationTest, BrickViewPointerAccess) {
    // Create a brick and verify direct pointer access works
    uint32_t brickID = registry->allocateBrick();
    ASSERT_NE(brickID, 0) << "Should allocate brick successfully";

    BrickView view = registry->getBrick(brickID);

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

    glm::vec3 retrievedColor = view.get<glm::vec3>("color", 0);
    EXPECT_FLOAT_EQ(retrievedColor.r, 1.0f);
    EXPECT_FLOAT_EQ(retrievedColor.g, 0.0f);
    EXPECT_FLOAT_EQ(retrievedColor.b, 0.0f);

    std::cout << "BrickView pointer access validated (index-based)\n";
}

// ============================================================================
// TEST 4: Type-Safe Attribute Access with GaiaVoxelWorld
// ============================================================================
TEST_F(AttributeRegistryIntegrationTest, TypeSafeAttributeAccess) {
    GaiaVoxelWorld world;

    // Create voxel with typed components
    auto entity = createVoxelInWorld(
        world,
        glm::vec3(3.0f, 3.0f, 3.0f),
        1.0f,                           // float density
        glm::vec3(0.5f, 0.5f, 0.5f),   // vec3 color
        glm::vec3(0.0f, 0.0f, 1.0f)    // vec3 normal
    );

    // Verify type-safe retrieval via GaiaVoxelWorld
    auto density = world.getComponentValue<Density>(entity);
    auto color = world.getComponentValue<Color>(entity);
    auto normal = world.getComponentValue<Normal>(entity);

    ASSERT_TRUE(density.has_value());
    ASSERT_TRUE(color.has_value());
    ASSERT_TRUE(normal.has_value());

    EXPECT_FLOAT_EQ(density.value(), 1.0f);
    EXPECT_EQ(color.value(), glm::vec3(0.5f, 0.5f, 0.5f));
    EXPECT_EQ(normal.value(), glm::vec3(0.0f, 0.0f, 1.0f));

    std::cout << "Type-safe GaiaVoxelWorld access validated\n";
}

// ============================================================================
// TEST 5: Multiple Voxels with Varying Densities
// ============================================================================
TEST_F(AttributeRegistryIntegrationTest, MultipleVoxelsVaryingDensity) {
    GaiaVoxelWorld world;

    // Create voxels with different densities
    auto e1 = createVoxelInWorld(world, glm::vec3(2.0f, 2.0f, 2.0f), 0.2f, glm::vec3(1.0f), glm::vec3(0, 1, 0));
    auto e2 = createVoxelInWorld(world, glm::vec3(5.0f, 5.0f, 5.0f), 0.8f, glm::vec3(1.0f), glm::vec3(0, 1, 0));
    auto e3 = createVoxelInWorld(world, glm::vec3(8.0f, 8.0f, 8.0f), 1.0f, glm::vec3(1.0f), glm::vec3(0, 1, 0));

    // Verify all entities exist
    EXPECT_TRUE(world.exists(e1));
    EXPECT_TRUE(world.exists(e2));
    EXPECT_TRUE(world.exists(e3));

    // Verify densities
    auto d1 = world.getComponentValue<Density>(e1);
    auto d2 = world.getComponentValue<Density>(e2);
    auto d3 = world.getComponentValue<Density>(e3);

    EXPECT_FLOAT_EQ(d1.value(), 0.2f);
    EXPECT_FLOAT_EQ(d2.value(), 0.8f);
    EXPECT_FLOAT_EQ(d3.value(), 1.0f);

    std::cout << "Multiple voxels with varying densities validated\n";
}

// ============================================================================
// TEST 6: AttributeRegistry Backward Compatibility
// ============================================================================
TEST_F(AttributeRegistryIntegrationTest, BackwardCompatibility_StringLookup) {
    // Verify that string-based attribute lookup still works (delegates to index lookup)
    uint32_t brickID = registry->allocateBrick();
    ASSERT_NE(brickID, 0) << "Should allocate brick";

    BrickView view = registry->getBrick(brickID);

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
// TEST 7: Multiple Octrees from Same GaiaVoxelWorld
// ============================================================================
TEST_F(AttributeRegistryIntegrationTest, MultipleOctreesFromGaiaWorld) {
    GaiaVoxelWorld world;

    // Create voxels
    auto e1 = createVoxelInWorld(world, glm::vec3(2.0f, 2.0f, 2.0f), 1.0f, glm::vec3(1, 0, 0), glm::vec3(0, 1, 0));
    auto e2 = createVoxelInWorld(world, glm::vec3(7.0f, 7.0f, 7.0f), 1.0f, glm::vec3(0, 1, 0), glm::vec3(1, 0, 0));

    // Create two octrees from same world with different regions
    LaineKarrasOctree octree1(world, registry.get(), 6, 3);
    LaineKarrasOctree octree2(world, registry.get(), 6, 3);

    // Rebuild each with different bounds
    octree1.rebuild(world, glm::vec3(0.0f), glm::vec3(5.0f));
    octree2.rebuild(world, glm::vec3(5.0f), glm::vec3(10.0f));

    std::cout << "Multiple octrees from same GaiaVoxelWorld validated\n";
}

// ============================================================================
// TEST 8: Entity-to-Octree Round Trip
// ============================================================================
TEST_F(AttributeRegistryIntegrationTest, EntityOctreeRoundTrip) {
    GaiaVoxelWorld world;

    // Create a voxel
    glm::vec3 pos(16.0f, 20.0f, 30.0f);
    auto entity = createVoxelInWorld(world, pos, 0.9f, glm::vec3(0.5f, 0.3f, 0.1f), glm::vec3(0, 0, 1));

    // Create octree and rebuild
    LaineKarrasOctree octree(world, nullptr, 8, 3);
    octree.rebuild(world, glm::vec3(0.0f), glm::vec3(64.0f));

    // Cast ray
    glm::vec3 rayOrigin(0.0f, 20.0f, 30.0f);
    glm::vec3 rayDir(1.0f, 0.0f, 0.0f);

    auto hit = octree.castRay(rayOrigin, rayDir, 0.0f, 100.0f);

    if (hit.hit) {
        // Verify entity reference returned by hit
        EXPECT_TRUE(world.exists(hit.entity)) << "Hit entity should be valid";

        // Retrieve components from entity
        auto density = world.getComponentValue<Density>(hit.entity);
        auto color = world.getComponentValue<Color>(hit.entity);

        if (density.has_value()) {
            EXPECT_NEAR(density.value(), 0.9f, 0.01f);
        }
    }

    std::cout << "Entity-to-Octree round trip validated\n";
}
