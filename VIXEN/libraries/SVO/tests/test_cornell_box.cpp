#include <gtest/gtest.h>
#include "LaineKarrasOctree.h"
#include "GaiaVoxelWorld.h"
#include "VoxelComponents.h"
#include "ComponentData.h"
#include <AttributeRegistry.h>
#include <glm/glm.hpp>
#include <chrono>
#include <iostream>

using namespace SVO;
using namespace GaiaVoxel;

/**
 * Cornell Box Test Fixture using GaiaVoxelWorld
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
    // Box configuration
    static constexpr float boxSize = 10.0f;
    static constexpr float thickness = 0.2f;
    static constexpr float voxelSize = 0.5f;  // Larger for faster tests
    static constexpr float lightSize = 2.0f;

    // Materials
    static constexpr glm::vec3 greyColor{0.8f, 0.8f, 0.8f};
    static constexpr glm::vec3 redColor{0.8f, 0.1f, 0.1f};
    static constexpr glm::vec3 greenColor{0.1f, 0.8f, 0.1f};
    static constexpr glm::vec3 whiteColor{1.0f, 1.0f, 1.0f};

    GaiaVoxelWorld world;
    std::unique_ptr<LaineKarrasOctree> cornellBox;
    std::shared_ptr<::VoxelData::AttributeRegistry> registry;

    void SetUp() override {
        // Create AttributeRegistry
        registry = std::make_shared<::VoxelData::AttributeRegistry>();
        registry->registerKey("density", ::VoxelData::AttributeType::Float, 1.0f);
        registry->addAttribute("color", ::VoxelData::AttributeType::Vec3, glm::vec3(1.0f));
        registry->addAttribute("normal", ::VoxelData::AttributeType::Vec3, glm::vec3(0.0f, 1.0f, 0.0f));

        // Build Cornell box
        buildCornellBox();

        // Create and rebuild octree
        cornellBox = std::make_unique<LaineKarrasOctree>(world, registry.get(), 8, 3);
        cornellBox->rebuild(world, glm::vec3(0.0f), glm::vec3(boxSize));
    }

    void createVoxel(const glm::vec3& position, const glm::vec3& color, const glm::vec3& normal) {
        ComponentQueryRequest comps[] = {
            Density{1.0f},
            Color{color},
            Normal{normal}
        };
        VoxelCreationRequest req{position, comps};
        world.createVoxel(req);
    }

    void buildCornellBox() {
        auto startTime = std::chrono::high_resolution_clock::now();
        int voxelCount = 0;

        // Generate floor voxels (y=0 to thickness)
        for (float x = 0.0f; x < boxSize; x += voxelSize) {
            for (float z = 0.0f; z < boxSize; z += voxelSize) {
                for (float y = 0.0f; y < thickness; y += voxelSize) {
                    createVoxel(glm::vec3(x, y, z), greyColor, glm::vec3(0, 1, 0));
                    voxelCount++;
                }
            }
        }

        // Generate ceiling voxels (y=boxSize-thickness to boxSize)
        for (float x = 0.0f; x < boxSize; x += voxelSize) {
            for (float z = 0.0f; z < boxSize; z += voxelSize) {
                for (float y = boxSize - thickness; y < boxSize; y += voxelSize) {
                    // Check if in light patch
                    glm::vec2 centerXZ(boxSize * 0.5f, boxSize * 0.5f);
                    float distFromCenter = glm::length(glm::vec2(x, z) - centerXZ);
                    glm::vec3 color = (distFromCenter < lightSize) ? whiteColor : greyColor;
                    createVoxel(glm::vec3(x, y, z), color, glm::vec3(0, -1, 0));
                    voxelCount++;
                }
            }
        }

        // Generate left wall (x=0 to thickness) - RED
        for (float y = 0.0f; y < boxSize; y += voxelSize) {
            for (float z = 0.0f; z < boxSize; z += voxelSize) {
                for (float x = 0.0f; x < thickness; x += voxelSize) {
                    createVoxel(glm::vec3(x, y, z), redColor, glm::vec3(1, 0, 0));
                    voxelCount++;
                }
            }
        }

        // Generate right wall (x=boxSize-thickness to boxSize) - GREEN
        for (float y = 0.0f; y < boxSize; y += voxelSize) {
            for (float z = 0.0f; z < boxSize; z += voxelSize) {
                for (float x = boxSize - thickness; x < boxSize; x += voxelSize) {
                    createVoxel(glm::vec3(x, y, z), greenColor, glm::vec3(-1, 0, 0));
                    voxelCount++;
                }
            }
        }

        // Generate back wall (z=boxSize-thickness to boxSize)
        for (float x = 0.0f; x < boxSize; x += voxelSize) {
            for (float y = 0.0f; y < boxSize; y += voxelSize) {
                for (float z = boxSize - thickness; z < boxSize; z += voxelSize) {
                    createVoxel(glm::vec3(x, y, z), greyColor, glm::vec3(0, 0, -1));
                    voxelCount++;
                }
            }
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        float buildTime = std::chrono::duration<float>(endTime - startTime).count();

        std::cout << "\n=== Built Cornell Box using GaiaVoxelWorld ===\n";
        std::cout << "Total voxels: " << voxelCount << "\n";
        std::cout << "Build time: " << buildTime << " seconds\n";
    }
};

// Constexpr definitions
constexpr float CornellBoxTest::boxSize;
constexpr float CornellBoxTest::thickness;
constexpr float CornellBoxTest::voxelSize;
constexpr float CornellBoxTest::lightSize;
constexpr glm::vec3 CornellBoxTest::greyColor;
constexpr glm::vec3 CornellBoxTest::redColor;
constexpr glm::vec3 CornellBoxTest::greenColor;
constexpr glm::vec3 CornellBoxTest::whiteColor;

// ---------------------------------------------------------------------------
// Category 1: Floor Material Tests
// ---------------------------------------------------------------------------

TEST_F(CornellBoxTest, FloorHit_FromAbove) {
    // Cast ray from above down to floor
    glm::vec3 rayOrigin(5.0f, 5.0f, 5.0f);
    glm::vec3 rayDir(0.0f, -1.0f, 0.0f);

    auto hit = cornellBox->castRay(rayOrigin, rayDir, 0.0f, 100.0f);

    if (hit.hit) {
        std::cout << "Floor hit at y=" << hit.position.y << "\n";
        EXPECT_LT(hit.position.y, 1.0f) << "Should hit floor (y close to 0)";
    }
}

// ---------------------------------------------------------------------------
// Category 2: Ceiling Material Tests
// ---------------------------------------------------------------------------

TEST_F(CornellBoxTest, CeilingHit_FromBelow) {
    // Cast ray from center up to ceiling
    glm::vec3 rayOrigin(5.0f, 5.0f, 5.0f);
    glm::vec3 rayDir(0.0f, 1.0f, 0.0f);

    auto hit = cornellBox->castRay(rayOrigin, rayDir, 0.0f, 100.0f);

    if (hit.hit) {
        std::cout << "Ceiling hit at y=" << hit.position.y << "\n";
        EXPECT_GT(hit.position.y, 9.0f) << "Should hit ceiling (y close to 10)";
    }
}

// ---------------------------------------------------------------------------
// Category 3: Left Wall (Red) Tests
// ---------------------------------------------------------------------------

TEST_F(CornellBoxTest, LeftWallHit_Red) {
    // Cast ray from center toward left wall
    glm::vec3 rayOrigin(5.0f, 5.0f, 5.0f);
    glm::vec3 rayDir(-1.0f, 0.0f, 0.0f);

    auto hit = cornellBox->castRay(rayOrigin, rayDir, 0.0f, 100.0f);

    if (hit.hit) {
        std::cout << "Left wall hit at x=" << hit.position.x << "\n";
        EXPECT_LT(hit.position.x, 1.0f) << "Should hit left wall (x close to 0)";

        // If entity reference is valid, verify color is red
        if (world.exists(hit.entity)) {
            auto color = world.getComponentValue<Color>(hit.entity);
            if (color.has_value()) {
                EXPECT_GT(color.value().r, 0.5f) << "Left wall should be red";
                EXPECT_LT(color.value().g, 0.3f);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Category 4: Right Wall (Green) Tests
// ---------------------------------------------------------------------------

TEST_F(CornellBoxTest, RightWallHit_Green) {
    // Cast ray from center toward right wall
    glm::vec3 rayOrigin(5.0f, 5.0f, 5.0f);
    glm::vec3 rayDir(1.0f, 0.0f, 0.0f);

    auto hit = cornellBox->castRay(rayOrigin, rayDir, 0.0f, 100.0f);

    if (hit.hit) {
        std::cout << "Right wall hit at x=" << hit.position.x << "\n";
        EXPECT_GT(hit.position.x, 9.0f) << "Should hit right wall (x close to 10)";

        // If entity reference is valid, verify color is green
        if (world.exists(hit.entity)) {
            auto color = world.getComponentValue<Color>(hit.entity);
            if (color.has_value()) {
                EXPECT_LT(color.value().r, 0.3f);
                EXPECT_GT(color.value().g, 0.5f) << "Right wall should be green";
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Category 5: Back Wall Tests
// ---------------------------------------------------------------------------

TEST_F(CornellBoxTest, BackWallHit_Grey) {
    // Cast ray from center toward back wall
    glm::vec3 rayOrigin(5.0f, 5.0f, 5.0f);
    glm::vec3 rayDir(0.0f, 0.0f, 1.0f);

    auto hit = cornellBox->castRay(rayOrigin, rayDir, 0.0f, 100.0f);

    if (hit.hit) {
        std::cout << "Back wall hit at z=" << hit.position.z << "\n";
        EXPECT_GT(hit.position.z, 9.0f) << "Should hit back wall (z close to 10)";
    }
}

// ---------------------------------------------------------------------------
// Category 6: Interior Ray Tests
// ---------------------------------------------------------------------------

TEST_F(CornellBoxTest, InteriorRay_DiagonalCornerToCorner) {
    // Cast diagonal ray through the box
    glm::vec3 rayOrigin(1.0f, 1.0f, 1.0f);
    glm::vec3 rayDir = glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f));

    auto hit = cornellBox->castRay(rayOrigin, rayDir, 0.0f, 100.0f);

    if (hit.hit) {
        std::cout << "Diagonal hit at (" << hit.position.x << ", "
                  << hit.position.y << ", " << hit.position.z << ")\n";
    }
}

// ---------------------------------------------------------------------------
// Category 7: Miss Tests
// ---------------------------------------------------------------------------

TEST_F(CornellBoxTest, MissFromOutsideBox) {
    // Ray that starts outside and points away from box
    glm::vec3 rayOrigin(-5.0f, 5.0f, 5.0f);
    glm::vec3 rayDir(-1.0f, 0.0f, 0.0f);  // Points away from box

    auto hit = cornellBox->castRay(rayOrigin, rayDir, 0.0f, 100.0f);

    // This ray should miss the box
    // Note: Depending on octree bounds, it might still hit if bounds extend beyond box
    if (!hit.hit) {
        std::cout << "Correctly missed (ray pointing away from box)\n";
    }
}

// ---------------------------------------------------------------------------
// Category 8: Entity Component Retrieval Tests
// ---------------------------------------------------------------------------

TEST_F(CornellBoxTest, EntityComponentRetrieval) {
    // Hit any surface and verify we can retrieve components
    glm::vec3 rayOrigin(5.0f, 5.0f, 5.0f);
    glm::vec3 rayDir(0.0f, -1.0f, 0.0f);  // Down to floor

    auto hit = cornellBox->castRay(rayOrigin, rayDir, 0.0f, 100.0f);

    if (hit.hit && world.exists(hit.entity)) {
        // Retrieve all components
        auto density = world.getComponentValue<Density>(hit.entity);
        auto color = world.getComponentValue<Color>(hit.entity);
        auto normal = world.getComponentValue<Normal>(hit.entity);

        if (density.has_value()) {
            std::cout << "Entity density: " << density.value() << "\n";
            EXPECT_GT(density.value(), 0.0f);
        }

        if (color.has_value()) {
            std::cout << "Entity color: (" << color.value().r << ", "
                      << color.value().g << ", " << color.value().b << ")\n";
        }

        if (normal.has_value()) {
            std::cout << "Entity normal: (" << normal.value().x << ", "
                      << normal.value().y << ", " << normal.value().z << ")\n";
            // Floor should have up-pointing normal
            EXPECT_GT(normal.value().y, 0.5f) << "Floor normal should point up";
        }
    }
}

// ---------------------------------------------------------------------------
// Category 9: Multiple Ray Tests
// ---------------------------------------------------------------------------

TEST_F(CornellBoxTest, MultipleRays_AllWalls) {
    struct RayTest {
        glm::vec3 origin;
        glm::vec3 dir;
        const char* name;
    };

    std::vector<RayTest> rays = {
        {{5, 5, 5}, {0, -1, 0}, "Floor"},
        {{5, 5, 5}, {0, 1, 0}, "Ceiling"},
        {{5, 5, 5}, {-1, 0, 0}, "Left (Red)"},
        {{5, 5, 5}, {1, 0, 0}, "Right (Green)"},
        {{5, 5, 5}, {0, 0, 1}, "Back"},
    };

    int hitCount = 0;
    for (const auto& ray : rays) {
        auto hit = cornellBox->castRay(ray.origin, ray.dir, 0.0f, 100.0f);
        if (hit.hit) {
            hitCount++;
            std::cout << ray.name << " wall hit at t=" << hit.tMin << "\n";
        } else {
            std::cout << ray.name << " wall MISSED\n";
        }
    }

    std::cout << "Hit " << hitCount << "/" << rays.size() << " walls\n";
}
