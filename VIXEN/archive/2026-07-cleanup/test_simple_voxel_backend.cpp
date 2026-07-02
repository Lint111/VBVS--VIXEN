/**
 * Simple standalone test for voxel backend functionality.
 * Tests basic workflow: create registry → inject voxels → compact → query.
 */

#include <iostream>
#include <SVO/LaineKarrasOctree.h>
#include <SVO/VoxelInjection.h>
#include <AttributeRegistry.h>
#include <glm/glm.hpp>

int main() {
    using namespace SVO;

    std::cout << "=== Simple Voxel Backend Test ===\n\n";

    // Step 1: Create AttributeRegistry
    std::cout << "[1/5] Creating AttributeRegistry...\n";
    ::VoxelData::AttributeRegistry registry;
    registry.registerKey("density", ::VoxelData::AttributeType::Float, 1.0f);
    registry.addAttribute("color", ::VoxelData::AttributeType::Vec3, glm::vec3(1.0f, 0.0f, 0.0f));
    std::cout << "  ✓ Registry created with density (key) and color\n\n";

    // Step 2: Create octree
    std::cout << "[2/5] Creating LaineKarrasOctree...\n";
    LaineKarrasOctree octree(&registry);
    octree.ensureInitialized(glm::vec3(0), glm::vec3(10), 8);
    std::cout << "  ✓ Octree initialized: bounds=[0,10], depth=8\n\n";

    // Step 3: Insert voxels via batch
    std::cout << "[3/5] Inserting 100 voxels via batch...\n";
    VoxelInjector injector(&registry);
    InjectionConfig config;
    config.maxLevels = 8;
    config.brickDepthLevels = 3;  // 8³ bricks

    std::vector<VoxelInjector::VoxelData> voxels;
    for (int i = 0; i < 100; ++i) {
        VoxelInjector::VoxelData vd(&registry);
        vd.position = glm::vec3(
            (i % 10) * 0.5f,
            ((i / 10) % 10) * 0.5f,
            0.0f
        );
        vd.attributes.set("density", 1.0f);
        vd.attributes.set("color", glm::vec3(1.0f, 0.0f, 0.0f));
        voxels.push_back(vd);
    }

    size_t inserted = injector.insertVoxelsBatch(octree, voxels, config);
    std::cout << "  ✓ Inserted " << inserted << "/100 voxels\n\n";

    // Step 4: Compact octree
    std::cout << "[4/5] Compacting to ESVO format...\n";
    bool compacted = injector.compactToESVOFormat(octree);
    std::cout << "  " << (compacted ? "✓" : "✗") << " Compaction " << (compacted ? "succeeded" : "failed") << "\n\n";

    // Step 5: Test ray query
    std::cout << "[5/5] Testing ray query...\n";
    glm::vec3 rayOrigin(2.5f, 2.5f, -5.0f);
    glm::vec3 rayDir(0.0f, 0.0f, 1.0f);
    float tMin = 0.0f;
    float tMax = 100.0f;

    RayHit hit;
    bool hitResult = octree.castRay(rayOrigin, rayDir, tMin, tMax, hit);

    if (hitResult) {
        std::cout << "  ✓ Ray hit voxel at t=" << hit.t << ", scale=" << hit.scale << "\n";
        std::cout << "    Hit position: (" << hit.hitPoint.x << ", " << hit.hitPoint.y << ", " << hit.hitPoint.z << ")\n";
    } else {
        std::cout << "  ✗ Ray missed (expected hit)\n";
    }

    std::cout << "\n=== Test Complete ===\n";
    return hitResult ? 0 : 1;
}
