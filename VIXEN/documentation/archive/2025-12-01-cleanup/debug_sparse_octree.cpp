// Minimal test to debug sparse octree traversal
#include <iostream>
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include "LaineKarrasOctree.h"
#include "VoxelInjection.h"
#include "ISVOStructure.h"
#include "SVOTypes.h"

using namespace SVO;

void printOctreeStructure(const LaineKarrasOctree* octree) {
    const Octree* oct = octree->getOctree();
    if (!oct || !oct->root) {
        std::cout << "Empty octree\n";
        return;
    }

    std::cout << "\n=== OCTREE STRUCTURE ===\n";
    std::cout << "Total descriptors: " << oct->root->childDescriptors.size() << "\n";

    // Print first 20 descriptors
    for (size_t i = 0; i < std::min(size_t(20), oct->root->childDescriptors.size()); ++i) {
        const auto& desc = oct->root->childDescriptors[i];
        std::cout << "Descriptor[" << i << "]: ";
        std::cout << "validMask=0x" << std::hex << (int)desc.validMask << std::dec;
        std::cout << " leafMask=0x" << std::hex << (int)desc.leafMask << std::dec;
        std::cout << " childPtr=" << desc.childPointer;
        std::cout << " valid_children=[";
        for (int j = 0; j < 8; ++j) {
            if (desc.validMask & (1 << j)) std::cout << j << " ";
        }
        std::cout << "]";
        std::cout << " leaf_children=[";
        for (int j = 0; j < 8; ++j) {
            if (desc.leafMask & (1 << j)) std::cout << j << " ";
        }
        std::cout << "]\n";
    }
}

int main() {
    std::cout << "=== SPARSE OCTREE DEBUG TEST ===\n\n";

    // Create simple test case: 2 voxels at opposite corners
    std::vector<glm::vec3> voxels = {
        glm::vec3(2, 2, 2),
        glm::vec3(8, 8, 8),
    };

    auto octree = std::make_unique<LaineKarrasOctree>();
    VoxelInjector injector;
    InjectionConfig config;
    config.maxLevels = 6;
    config.minVoxelSize = 0.01f;

    std::cout << "Inserting voxels:\n";
    for (const auto& pos : voxels) {
        std::cout << "  (" << pos.x << ", " << pos.y << ", " << pos.z << ")\n";
        VoxelData voxel;
        voxel.position = pos;
        voxel.normal = glm::vec3(0, 1, 0);
        voxel.color = glm::vec3(1, 1, 1);
        voxel.density = 1.0f;
        injector.insertVoxel(*octree, pos, voxel, config);
    }

    std::cout << "\nCompacting to ESVO format...\n";
    injector.compactToESVOFormat(*octree);

    printOctreeStructure(octree.get());

    // Test ray that should hit voxel at (8,8,8)
    std::cout << "\n=== RAY CAST TEST ===\n";
    glm::vec3 origin(15, 8, 8);
    glm::vec3 direction(-1, 0, 0);

    std::cout << "Ray: origin(" << origin.x << "," << origin.y << "," << origin.z << ") ";
    std::cout << "dir(" << direction.x << "," << direction.y << "," << direction.z << ")\n";
    std::cout << "Expected hit: voxel at (8,8,8)\n\n";

    auto hit = octree->castRay(origin, direction, 0.0f, 100.0f);

    std::cout << "\n=== RESULT ===\n";
    std::cout << "Hit: " << (hit.hit ? "YES" : "NO") << "\n";
    if (hit.hit) {
        std::cout << "Position: (" << hit.position.x << ", " << hit.position.y << ", " << hit.position.z << ")\n";
        std::cout << "t: " << hit.tMin << "\n";
        std::cout << "Scale: " << hit.scale << "\n";
    }

    return hit.hit ? 0 : 1;
}
