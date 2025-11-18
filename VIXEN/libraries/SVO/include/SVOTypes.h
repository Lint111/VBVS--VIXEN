#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace SVO {

// ============================================================================
// Core Data Structures based on Laine & Karras 2010
// ============================================================================

/**
 * 64-bit child descriptor stored for each non-leaf voxel.
 *
 * Structure (split into two 32-bit parts):
 * Part 1 (hierarchy):
 *   - child_pointer:  15 bits - Points to first child descriptor
 *   - far_bit:         1 bit  - Indicates if child_pointer is indirect
 *   - valid_mask:      8 bits - Which child slots contain voxels
 *   - leaf_mask:       8 bits - Which valid children are leaves
 *
 * Part 2 (contour):
 *   - contour_pointer: 24 bits - Points to contour data
 *   - contour_mask:     8 bits - Which children have contours
 */
struct ChildDescriptor {
    // First 32 bits - hierarchy data
    uint32_t childPointer  : 15;  // Offset to first child descriptor
    uint32_t farBit        : 1;   // 1 = childPointer is indirect reference
    uint32_t validMask     : 8;   // Bit per child: 1 = slot contains voxel
    uint32_t leafMask      : 8;   // Bit per child: 1 = voxel is leaf

    // Second 32 bits - contour data
    uint32_t contourPointer : 24; // Offset to contour values
    uint32_t contourMask    : 8;  // Bit per child: 1 = has contour

    // Helper methods
    bool hasChild(int childIdx) const {
        return (validMask & (1 << childIdx)) != 0;
    }

    bool isLeaf(int childIdx) const {
        return (leafMask & (1 << childIdx)) != 0;
    }

    bool hasContour(int childIdx) const {
        return (contourMask & (1 << childIdx)) != 0;
    }

    int getChildCount() const {
        return __popcnt(validMask & ~leafMask);
    }
};

static_assert(sizeof(ChildDescriptor) == 8, "ChildDescriptor must be 64 bits");

/**
 * 32-bit contour value defining a pair of parallel planes.
 *
 * A contour constrains the spatial extent of a voxel by intersecting
 * it with two parallel planes. This provides much tighter surface
 * approximation than a cube alone.
 *
 * Structure:
 *   - thickness: 7 bits (unsigned) - Distance between planes
 *   - position:  7 bits (signed)   - Center position along normal
 *   - nx:        6 bits (signed)   - Normal X component
 *   - ny:        6 bits (signed)   - Normal Y component
 *   - nz:        6 bits (signed)   - Normal Z component
 */
struct Contour {
    uint32_t thickness : 7;   // Unsigned: distance between planes
    uint32_t position  : 7;   // Signed: position along normal
    uint32_t nx        : 6;   // Signed: normal X
    uint32_t ny        : 6;   // Signed: normal Y
    uint32_t nz        : 6;   // Signed: normal Z

    // Decode normal vector
    glm::vec3 getNormal() const;

    // Get plane positions in voxel space [0,1]
    void getPlanes(const glm::vec3& normal, float& planeMin, float& planeMax) const;
};

static_assert(sizeof(Contour) == 4, "Contour must be 32 bits");

/**
 * Uncompressed attribute storage (64 bits per voxel).
 * Used for colors and normals before compression.
 */
struct UncompressedAttributes {
    // Color: 32 bits ABGR
    uint8_t blue;
    uint8_t green;
    uint8_t red;
    uint8_t alpha;

    // Normal: 32 bits (point on cube encoding)
    uint32_t sign_and_axis : 3;  // Which cube face (0-5)
    uint32_t u_coordinate  : 15; // U coordinate on face
    uint32_t v_coordinate  : 14; // V coordinate on face

    glm::vec3 getColor() const {
        return glm::vec3(red / 255.0f, green / 255.0f, blue / 255.0f);
    }

    glm::vec3 getNormal() const;
};

static_assert(sizeof(UncompressedAttributes) == 8, "Attributes must be 64 bits");

/**
 * Attribute lookup entry (32 bits).
 * Maps child descriptors to attribute values.
 */
struct AttributeLookup {
    uint32_t valuePointer : 24; // Offset to attribute values
    uint32_t mask         : 8;  // Bit per child: 1 = has attribute

    bool hasAttribute(int childIdx) const {
        return (mask & (1 << childIdx)) != 0;
    }
};

static_assert(sizeof(AttributeLookup) == 4, "AttributeLookup must be 32 bits");

/**
 * Page header (32 bits).
 * Placed every 8KB in child descriptor array.
 * Points to block info section.
 */
struct PageHeader {
    int32_t infoOffset; // Relative pointer to info section (in 32-bit units)
};

/**
 * Block info section.
 * Contains metadata about a contiguous octree block.
 */
struct BlockInfo {
    int32_t blockPtr;                    // Pointer to first child descriptor
    int32_t attachmentCount;             // Number of attachments
    int32_t attachmentPtrs[16];          // Relative pointers to attachments
    uint32_t attachmentTypes[16];        // Type IDs of attachments
};

/**
 * Voxel cube in world space.
 * Used during octree traversal.
 */
struct VoxelCube {
    glm::vec3 position;     // Corner position (values in [0,1] or [1,2])
    int scale;              // Scale level (smaller = finer detail)

    float getSize() const {
        return exp2f(static_cast<float>(scale - 23));
    }
};

/**
 * Ray-voxel intersection result.
 */
struct RayHit {
    float t;                        // Hit parameter along ray
    glm::vec3 position;             // Hit position in world space
    ChildDescriptor* parent;        // Parent voxel's child descriptor
    int childIdx;                   // Which child slot was hit (0-7)
    int scale;                      // Scale of hit voxel

    bool hit() const { return t < 2.0f; }
};

/**
 * Octree build parameters.
 */
struct BuildParams {
    int maxLevels = 16;                    // Maximum octree depth
    float geometryErrorThreshold = 0.001f; // Max geometric error (voxel units)
    float colorErrorThreshold = 8.0f;      // Max color error (0-255 scale)
    float normalErrorThreshold = 0.1f;     // Max normal error (radians)
    bool enableContours = true;            // Generate contours
    bool enableCompression = true;         // Compress attributes
    int numThreads = 0;                    // 0 = auto-detect
};

// ============================================================================
// Helper Functions
// ============================================================================

// Create attributes from color and normal
UncompressedAttributes makeAttributes(const glm::vec3& color, const glm::vec3& normal);

// Create contour from geometric parameters
Contour makeContour(const glm::vec3& normal, float centerPos, float thickness);

// Decode contour properties
glm::vec3 decodeContourNormal(const Contour& contour);
float decodeContourThickness(const Contour& contour);
float decodeContourPosition(const Contour& contour);

// Population count for 8-bit value (helper for validMask/leafMask)
int popc8(uint8_t mask);

} // namespace SVO
