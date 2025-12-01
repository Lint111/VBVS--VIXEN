#include <iostream>
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include "LaineKarrasOctree.h"
#include "VoxelInjection.h"

using namespace SVO;

int main() {
    std::cout << "=== SIMPLE LINE TEST ===\n\n";

    // Create a line of voxels along Z axis
    std::vector<glm::vec3> voxels;
    for (int z = 3; z <= 7; z++) {
        voxels.push_back(glm::vec3(5, 5, z));
    }

    auto octree = std::make_unique<LaineKarrasOctree>();
    VoxelInjector injector;
    InjectionConfig config;
    config.maxLevels = 4;  // Use shallow depth for larger voxels
    config.minVoxelSize = 0.1f;

    std::cout << "Inserting " << voxels.size() << " voxels in a line at x=5, y=5, z=3..7\n";
    for (const auto& pos : voxels) {
        VoxelData voxel;
        voxel.position = pos;
        voxel.normal = glm::vec3(0, 1, 0);
        voxel.color = glm::vec3(1, 1, 1);
        voxel.density = 1.0f;
        injector.insertVoxel(*octree, pos, voxel, config);
    }

    std::cout << "\nCompacting...\n";
    injector.compactToESVOFormat(*octree);

    const Octree* oct = octree->getOctree();
    std::cout << "Total descriptors: " << oct->root->childDescriptors.size() << "\n";

    // Test 1: Ray directly through the line
    std::cout << "\n=== TEST 1: Direct ray ===\n";
    {
        glm::vec3 origin(5, 5, 0);
        glm::vec3 direction(0, 0, 1);
        std::cout << "Ray from (5,5,0) in direction (0,0,1) - should hit voxel at z=3\n";

        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        std::cout << "Result: " << (hit.hit ? "HIT" : "MISS") << "\n";
        if (hit.hit) {
            std::cout << "  Position: (" << hit.position.x << ", " << hit.position.y
                      << ", " << hit.position.z << ")\n";
            std::cout << "  Expected: around z=3\n";
        }
    }

    // Test 2: Ray slightly offset
    std::cout << "\n=== TEST 2: Slightly offset ray ===\n";
    {
        glm::vec3 origin(5.1f, 5.1f, 0);
        glm::vec3 direction(0, 0, 1);
        std::cout << "Ray from (5.1,5.1,0) in direction (0,0,1)\n";

        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        std::cout << "Result: " << (hit.hit ? "HIT" : "MISS") << "\n";
        if (hit.hit) {
            std::cout << "  Position: (" << hit.position.x << ", " << hit.position.y
                      << ", " << hit.position.z << ")\n";
        }
    }

    // Test 3: Ray from the side
    std::cout << "\n=== TEST 3: Ray from side ===\n";
    {
        glm::vec3 origin(0, 5, 5);
        glm::vec3 direction(1, 0, 0);
        std::cout << "Ray from (0,5,5) in direction (1,0,0) - should hit voxel at x=5\n";

        auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);
        std::cout << "Result: " << (hit.hit ? "HIT" : "MISS") << "\n";
        if (hit.hit) {
            std::cout << "  Position: (" << hit.position.x << ", " << hit.position.y
                      << ", " << hit.position.z << ")\n";
            std::cout << "  Expected: around x=5\n";
        }
    }

    return 0;
}