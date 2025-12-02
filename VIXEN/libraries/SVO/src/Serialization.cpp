#include "pch.h"
#include "LaineKarrasOctree.h"
#include "SVOBuilder.h"
#include <fstream>
#include <cstring>

namespace Vixen::SVO {

/**
 * Serialization format (.oct files):
 *
 * Header (64 bytes):
 * - Magic number: "LKSVO001" (8 bytes)
 * - Version: uint32_t
 * - Max levels: uint32_t
 * - Total voxels: uint64_t
 * - World min: float[3]
 * - World max: float[3]
 * - Reserved: 24 bytes
 *
 * Data sections:
 * - Child descriptors (variable size)
 * - Contours (variable size)
 * - Attributes (variable size)
 */

struct OctreeFileHeader {
    char magic[8];
    uint32_t version;
    uint32_t maxLevels;
    uint64_t totalVoxels;
    float worldMin[3];
    float worldMax[3];
    uint8_t reserved[24];
};

bool Octree::saveToFile(const std::string& filename) const {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // Write header
    OctreeFileHeader header{};
    std::memcpy(header.magic, "LKSVO001", 8);
    header.version = 1;
    header.maxLevels = maxLevels;
    header.totalVoxels = totalVoxels;
    header.worldMin[0] = worldMin.x;
    header.worldMin[1] = worldMin.y;
    header.worldMin[2] = worldMin.z;
    header.worldMax[0] = worldMax.x;
    header.worldMax[1] = worldMax.y;
    header.worldMax[2] = worldMax.z;

    file.write(reinterpret_cast<const char*>(&header), sizeof(header));

    // TODO: Write octree data sections
    // - Child descriptors
    // - Contours
    // - Attributes

    return true;
}

bool Octree::loadFromFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // Read header
    OctreeFileHeader header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));

    // Verify magic number
    if (std::memcmp(header.magic, "LKSVO001", 8) != 0) {
        return false;
    }

    // Load metadata
    maxLevels = header.maxLevels;
    totalVoxels = header.totalVoxels;
    worldMin = glm::vec3(header.worldMin[0], header.worldMin[1], header.worldMin[2]);
    worldMax = glm::vec3(header.worldMax[0], header.worldMax[1], header.worldMax[2]);

    // TODO: Load octree data sections
    // - Child descriptors
    // - Contours
    // - Attributes

    return true;
}

size_t OctreeBlock::getTotalSize() const {
    return childDescriptors.size() * sizeof(ChildDescriptor)
         + contours.size() * sizeof(Contour)
         + attributes.size() * sizeof(UncompressedAttributes);
}

void OctreeBlock::serialize(std::vector<uint8_t>& buffer) const {
    // TODO: Implement block serialization
    // Pack child descriptors, contours, and attributes into buffer
}

} // namespace Vixen::SVO
