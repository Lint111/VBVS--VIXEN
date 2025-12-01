#pragma once

#include <cstdint>
#include <bit>  // C++20 std::popcount
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
 * Part 2 (context-dependent interpretation):
 *   For INTERNAL nodes (contour mode):
 *     - contour_pointer: 24 bits - Points to contour data
 *     - contour_mask:     8 bits - Which children have contours
 *
 *   For LEAF nodes at brick level (brick mode):
 *     - brick_index:     24 bits - Index into sparse brick array
 *     - brick_flags:      8 bits - Reserved for future use (LOD, compression)
 *
 * The interpretation depends on context:
 * - Contours approximate mesh surfaces (mesh→voxel conversion)
 * - Bricks store dense voxel data (native voxel content)
 */
struct ChildDescriptor {
    // First 32 bits - hierarchy data
    uint32_t childPointer  : 15;  // Offset to first child descriptor
    uint32_t farBit        : 1;   // 1 = childPointer is indirect reference
    uint32_t validMask     : 8;   // Bit per child: 1 = slot contains voxel
    uint32_t leafMask      : 8;   // Bit per child: 1 = voxel is leaf

    // Second 32 bits - contour/brick data (context-dependent)
    uint32_t contourPointer : 24; // Contour mode: offset to contour values
                                  // Brick mode: index into sparse brick array
    uint32_t contourMask    : 8;  // Contour mode: which children have contours
                                  // Brick mode: flags (reserved)

    // Sentinel value for "no brick" (24-bit max)
    static constexpr uint32_t INVALID_BRICK_INDEX = 0xFFFFFFu;

    // ========================================================================
    // Hierarchy helpers (always valid)
    // ========================================================================
    bool hasChild(int childIdx) const {
        return (validMask & (1 << childIdx)) != 0;
    }

    bool isLeaf(int childIdx) const {
        return (leafMask & (1 << childIdx)) != 0;
    }

    int getChildCount() const {
        return std::popcount(static_cast<uint8_t>(validMask & ~leafMask));
    }

    int getLeafCount() const {
        return std::popcount(static_cast<uint8_t>(validMask & leafMask));
    }

    // ========================================================================
    // Contour mode helpers (for mesh voxelization)
    // ========================================================================
    bool hasContour(int childIdx) const {
        return (contourMask & (1 << childIdx)) != 0;
    }

    uint32_t getContourPointer() const {
        return contourPointer;
    }

    uint8_t getContourMask() const {
        return static_cast<uint8_t>(contourMask);
    }

    void setContour(uint32_t pointer, uint8_t mask) {
        contourPointer = pointer & 0xFFFFFFu;
        contourMask = mask;
    }

    // ========================================================================
    // Brick mode helpers (for voxel data - leaf nodes at brick level)
    // ========================================================================
    uint32_t getBrickIndex() const {
        return contourPointer;  // Same field, different interpretation
    }

    uint8_t getBrickFlags() const {
        return static_cast<uint8_t>(contourMask);  // Same field
    }

    bool hasBrick() const {
        return contourPointer != INVALID_BRICK_INDEX;
    }

    void setBrickIndex(uint32_t index, uint8_t flags = 0) {
        contourPointer = index & 0xFFFFFFu;
        contourMask = flags;
    }

    void clearBrick() {
        contourPointer = INVALID_BRICK_INDEX;
        contourMask = 0;
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
    int maxLevels = 16;                    // Maximum octree depth (total hierarchy depth)
    int brickDepthLevels = 3;              // Bottom N levels reserved for dense brick data (brick size = 2^N)
                                           // Example: 3 → 2³=8 → 8×8×8 voxel bricks
                                           // 0 = disabled (pure octree, no bricks)
                                           // Octree depth = maxLevels - brickDepthLevels
    float minVoxelSize = 0.01f;            // Minimum voxel size in world units (prevents over-subdivision)
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

// ============================================================================
// Coordinate Space Transformations for ESVO Traversal
// ============================================================================
//
// Three coordinate spaces are used in this implementation:
//
// 1. WORLD SPACE (External API)
//    - Actual 3D world coordinates where the octree volume exists
//    - Used for ray origin/direction in castRay() API
//    - Transformed to local space via m_worldToLocal matrix
//
// 2. LOCAL SPACE (Internal Storage)
//    - Octree's own coordinate system, ray-independent
//    - All descriptors, bricks, and entity mappings are stored here
//    - Normalized to [1,2]³ for ESVO traversal math
//
// 3. MIRRORED SPACE (ESVO Traversal)
//    - Ray-direction-dependent view where axes are flipped
//    - Enables traversal to always go from high→low indices
//    - state.idx is always in mirrored space during traversal
//
// The octant_mask encodes mirroring (ESVO paper convention):
//    - octant_mask = 7 initially
//    - For each POSITIVE ray direction component, XOR that bit
//    - Result: bit=0 means that axis IS mirrored, bit=1 means NOT mirrored
//
// Conversion formulas:
//    - Local→Mirrored: mirroredIdx = localIdx ^ (~octant_mask & 7)
//    - Mirrored→Local: localIdx = mirroredIdx ^ (~octant_mask & 7)
//    - Same formula both ways because XOR is its own inverse
//
// ============================================================================
// Mask Mirroring for ESVO Ray-Direction Space
// ============================================================================

/**
 * Mirror an 8-bit octant mask based on ray direction.
 *
 * Converts a LOCAL-SPACE mask (stored in descriptors) to MIRRORED-SPACE
 * for use with mirrored-space indices (state.idx).
 *
 * The octant_mask encodes which axes are mirrored (ESVO paper convention):
 *   - Bit 0 (1): X axis mirrored if CLEAR (positive ray.x)
 *   - Bit 1 (2): Y axis mirrored if CLEAR (positive ray.y)
 *   - Bit 2 (4): Z axis mirrored if CLEAR (positive ray.z)
 *
 * This function permutes mask bits so that local-space masks (stored in descriptors)
 * can be checked against mirrored-space indices (state.idx).
 *
 * Example: octant_mask=6 (Y,Z mirrored), local octant 0 → mirrored octant 6
 *          So if validMask has bit 0 set, mirrored validMask should have bit 6 set.
 *
 * @param mask Local-space 8-bit mask (validMask or leafMask from ChildDescriptor)
 * @param octant_mask Ray-direction mirroring mask (0-7)
 * @return Mirrored mask for use with mirrored-space indices (state.idx)
 */
inline uint8_t mirrorMask(uint8_t mask, int octant_mask) {
    // Fast path: no mirroring needed when ray direction is all negative
    // (octant_mask = 7 means no axes are positive, so no mirroring)
    if (octant_mask == 7) {
        return mask;
    }

    // octant_mask convention: bit=0 means that axis IS mirrored (positive ray direction)
    //                         bit=1 means that axis is NOT mirrored (negative ray direction)
    // To convert world octant to mirrored octant, flip bits where octant_mask has 0
    uint8_t flipMask = (~octant_mask) & 7;

    // Permute bits: for each local octant i, move its bit to mirrored position
    uint8_t result = 0;
    for (int i = 0; i < 8; i++) {
        int mirroredIdx = i ^ flipMask;
        if (mask & (1 << i)) {
            result |= (1 << mirroredIdx);
        }
    }
    return result;
}

/**
 * Convert mirrored-space octant index to local-space octant index.
 *
 * Use this when you have a mirrored-space index (state.idx) and need to look up
 * data stored in local space (descriptors, bricks, leafToBrickView).
 *
 * The octant_mask encodes which axes are NOT mirrored (bit=1 means NOT mirrored).
 * We need to flip bits where the axis IS mirrored (bit=0), so we XOR with
 * the INVERSE of octant_mask (i.e., ~octant_mask & 7).
 *
 * @param mirroredIdx Octant index in mirrored space (state.idx)
 * @param octant_mask Ray-direction mirroring mask (0-7, bit=0 means axis is mirrored)
 * @return Local-space octant index (for descriptor/brick lookup)
 */
inline int mirroredToLocalOctant(int mirroredIdx, int octant_mask) {
    // Flip bits where axis IS mirrored (octant_mask bit = 0)
    return mirroredIdx ^ (~octant_mask & 7);
}

/**
 * Convert local-space octant index to mirrored-space octant index.
 *
 * @param localIdx Octant index in local space (from descriptor)
 * @param octant_mask Ray-direction mirroring mask (0-7, bit=0 means axis is mirrored)
 * @return Mirrored-space octant index
 */
inline int localToMirroredOctant(int localIdx, int octant_mask) {
    // Flip bits where axis IS mirrored (octant_mask bit = 0)
    // XOR with inverse is its own inverse, so same formula as mirroredToLocal
    return localIdx ^ (~octant_mask & 7);
}

// Legacy names for compatibility
inline int mirroredToWorldOctant(int mirroredIdx, int octant_mask) {
    return mirroredToLocalOctant(mirroredIdx, octant_mask);
}

inline int worldToMirroredOctant(int worldIdx, int octant_mask) {
    return localToMirroredOctant(worldIdx, octant_mask);
}

} // namespace SVO
