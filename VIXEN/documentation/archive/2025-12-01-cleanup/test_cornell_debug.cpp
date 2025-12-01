#include <iostream>
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include "LaineKarrasOctree.h"
#include "VoxelInjection.h"
#include "ISVOStructure.h"
#include "SVOTypes.h"

using namespace SVO;

int main() {
    std::cout << "=== CORNELL BOX DEBUG TEST ===\n\n";

    // Create simplified Cornell box - just back wall and one object
    std::vector<glm::vec3> voxels;

    // Back wall (z=9) - just a 3x3 section
    for (int x = 4; x <= 6; x++) {
        for (int y = 4; y <= 6; y++) {
            voxels.push_back(glm::vec3(x, y, 9.5f));
        }
    }

    // Small box at (3,1,3) - just one voxel to start
    voxels.push_back(glm::vec3(3, 2, 3));

    std::cout << "Total voxels: " << voxels.size() << "\n";

    auto octree = std::make_unique<LaineKarrasOctree>();
    VoxelInjector injector;
    InjectionConfig config;
    config.maxLevels = 8;
    config.minVoxelSize = 0.01f;

    std::cout << "\nInserting voxels...\n";
    for (const auto& pos : voxels) {
        VoxelData voxel;
        voxel.position = pos;
        voxel.normal = glm::vec3(0, 1, 0);
        voxel.color = glm::vec3(1, 1, 1);
        voxel.density = 1.0f;
        injector.insertVoxel(*octree, pos, voxel, config);
    }

    std::cout << "Before compaction:\n";
    const Octree* oct = octree->getOctree();
    std::cout << "  Descriptors: " << oct->root->childDescriptors.size() << "\n";

    std::cout << "\nCompacting to ESVO format...\n";
    injector.compactToESVOFormat(*octree);

    std::cout << "After compaction:\n";
    std::cout << "  Descriptors: " << oct->root->childDescriptors.size() << "\n";

    // Print descriptor structure
    std::cout << "\nOctree structure (first 10 descriptors):\n";
    for (size_t i = 0; i < std::min(size_t(10), oct->root->childDescriptors.size()); ++i) {
        const auto& desc = oct->root->childDescriptors[i];
        std::cout << "  [" << i << "]: valid=0x" << std::hex << (int)desc.validMask
                  << " leaf=0x" << (int)desc.leafMask << std::dec
                  << " childPtr=" << desc.childPointer << "\n";
    }

    // Test ray that should hit back wall
    std::cout << "\n=== RAY CAST TEST ===\n";
    glm::vec3 origin(5, 5, -2);
    glm::vec3 direction(0, 0, 1);

    std::cout << "Ray: origin(" << origin.x << "," << origin.y << "," << origin.z << ") ";
    std::cout << "dir(" << direction.x << "," << direction.y << "," << direction.z << ")\n";
    std::cout << "Expected: Should hit back wall at z=9.5\n\n";

    auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);

    std::cout << "\n=== RESULT ===\n";
    std::cout << "Hit: " << (hit.hit ? "YES" : "NO") << "\n";
    if (hit.hit) {
        std::cout << "Position: (" << hit.position.x << ", " << hit.position.y
                  << ", " << hit.position.z << ")\n";
        std::cout << "t: " << hit.tMin << "\n";
    }

    return hit.hit ? 0 : 1;
}