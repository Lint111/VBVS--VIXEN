// ============================================================================
// SVOTypes.glsl - Shared SVO/ESVO Data Structures
// ============================================================================
// This file defines data structures that match the C++ SVOTypes.h layout.
// Include this file in any shader that needs to access SVO data.
//
// IMPORTANT: Keep this file in sync with libraries/SVO/include/SVOTypes.h
// ============================================================================

#ifndef SVO_TYPES_GLSL
#define SVO_TYPES_GLSL

// ============================================================================
// CHILD DESCRIPTOR (64 bits = uvec2)
// ============================================================================
// Matches C++ ChildDescriptor struct layout exactly.
//
// First 32 bits (descriptor.x):
//   bits 0-14:  childPointer (15 bits) - Offset to first child descriptor
//   bit 15:     farBit (1 bit) - 1 = childPointer is indirect reference
//   bits 16-23: validMask (8 bits) - Bit per child: 1 = slot contains voxel
//   bits 24-31: leafMask (8 bits) - Bit per child: 1 = voxel is leaf
//
// Second 32 bits (descriptor.y):
//   For INTERNAL nodes (contour mode):
//     bits 0-23:  contourPointer (24 bits) - Offset to contour data
//     bits 24-31: contourMask (8 bits) - Bit per child: 1 = has contour
//
//   For LEAF nodes at brick level (brick mode):
//     bits 0-23:  brickIndex (24 bits) - Index into sparse brick array
//     bits 24-31: flags (8 bits) - Reserved for future use
//
// The interpretation of the second 32 bits depends on context:
// - Contours are used for mesh surface approximation
// - Bricks are used for native voxel data storage
// ============================================================================

// Sentinel value for "no brick" or "no contour"
const uint SVO_INVALID_INDEX = 0xFFFFFFu;  // 24-bit max

// ============================================================================
// DESCRIPTOR FIELD EXTRACTION
// ============================================================================

// First 32 bits (hierarchy)
uint getChildPointer(uvec2 descriptor) {
    return descriptor.x & 0x7FFFu;  // bits 0-14
}

bool getFarBit(uvec2 descriptor) {
    return (descriptor.x & 0x8000u) != 0u;  // bit 15
}

uint getValidMask(uvec2 descriptor) {
    return (descriptor.x >> 16) & 0xFFu;  // bits 16-23
}

uint getLeafMask(uvec2 descriptor) {
    return (descriptor.x >> 24) & 0xFFu;  // bits 24-31
}

// Second 32 bits - Contour interpretation
uint getContourPointer(uvec2 descriptor) {
    return descriptor.y & 0xFFFFFFu;  // bits 0-23
}

uint getContourMask(uvec2 descriptor) {
    return (descriptor.y >> 24) & 0xFFu;  // bits 24-31
}

// Second 32 bits - Brick interpretation (for leaf nodes)
uint getBrickIndex(uvec2 descriptor) {
    return descriptor.y & 0xFFFFFFu;  // bits 0-23 (same field as contourPointer)
}

uint getBrickFlags(uvec2 descriptor) {
    return (descriptor.y >> 24) & 0xFFu;  // bits 24-31
}

// ============================================================================
// CHILD VALIDITY HELPERS
// ============================================================================

bool childExists(uint validMask, int childIndex) {
    return ((validMask >> childIndex) & 1u) != 0u;
}

bool childIsLeaf(uint leafMask, int childIndex) {
    return ((leafMask >> childIndex) & 1u) != 0u;
}

bool childHasContour(uint contourMask, int childIndex) {
    return ((contourMask >> childIndex) & 1u) != 0u;
}

// ============================================================================
// CHILD COUNTING (for packed array indexing)
// ============================================================================

// Count INTERNAL (non-leaf) children before childIndex in packed array
// Only counts children that EXIST (validMask) AND are NOT leaves (leafMask clear)
uint countInternalChildrenBefore(uint validMask, uint leafMask, int childIndex) {
    if (childIndex <= 0) return 0u;
    uint mask = (1u << childIndex) - 1u;
    uint internalMask = validMask & ~leafMask;
    return bitCount(internalMask & mask);
}

// Count LEAF children before childIndex (for brick/leaf lookup)
uint countLeavesBefore(uint validMask, uint leafMask, int childIndex) {
    if (childIndex <= 0) return 0u;
    uint mask = (1u << childIndex) - 1u;
    uint leafChildren = validMask & leafMask;
    return bitCount(leafChildren & mask);
}

// ============================================================================
// OCTANT MIRRORING (ESVO ray-direction space)
// ============================================================================
// The octant_mask encodes which axes are mirrored based on ray direction:
//   - Starts at 7
//   - XOR each bit for positive ray direction component
//   - Result: bit=0 means axis IS mirrored, bit=1 means NOT mirrored
//
// Conversion between mirrored-space (traversal) and local-space (storage):
//   localIdx = mirroredIdx ^ (~octant_mask & 7)
//   mirroredIdx = localIdx ^ (~octant_mask & 7)  // XOR is its own inverse

int mirroredToLocalOctant(int mirroredIdx, int octant_mask) {
    return mirroredIdx ^ ((~octant_mask) & 7);
}

int localToMirroredOctant(int localIdx, int octant_mask) {
    return localIdx ^ ((~octant_mask) & 7);
}

// Legacy alias for compatibility
int mirroredToWorldOctant(int mirroredIdx, int octant_mask) {
    return mirroredIdx ^ octant_mask;
}

// ============================================================================
// BRICK CONSTANTS
// ============================================================================

// Standard brick dimensions (8x8x8 voxels)
const int BRICK_SIZE = 8;
const int BRICK_VOXEL_COUNT = 512;  // 8 * 8 * 8

// Brick voxel indexing: linear index = z*64 + y*8 + x
uint brickVoxelIndex(ivec3 localCoord) {
    return uint(localCoord.z * 64 + localCoord.y * 8 + localCoord.x);
}

ivec3 brickVoxelCoord(uint linearIndex) {
    int z = int(linearIndex / 64u);
    int y = int((linearIndex % 64u) / 8u);
    int x = int(linearIndex % 8u);
    return ivec3(x, y, z);
}

// ============================================================================
// MATERIAL DATA (matches C++ Material struct)
// ============================================================================

struct Material {
    vec3 albedo;
    float metalness;
    float roughness;
    float emissive;
    vec2 padding;
};

#endif // SVO_TYPES_GLSL
