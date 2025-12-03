// VoxelTraversal.glsl
// Reusable GLSL functions for octree traversal and voxel ray marching
//
// Usage: #include "VoxelTraversal.glsl" in your compute shader
//
// Requirements:
// - GLSL 460 or higher
// - SSBOs: octreeNodes, voxelBricks, materialPalette (see bindings below)
//
// Research References:
// - [16] Derin et al.: "BlockWalk" - empty space skipping via childMask
// - Amanatides & Woo: "Fast Voxel Traversal" - DDA algorithm

// ============================================================================
// OCTREE DATA ACCESS FUNCTIONS
// ============================================================================

// OctreeNode accessor functions
// Memory layout: childOffsets[8] (32 bytes) + childMask (1 byte) + leafMask (1 byte) + padding (2 bytes) + brickOffset (4 bytes) = 40 bytes
// Packed as 10 uints per node

/**
 * @brief Get child offset for a specific child of a node
 * @param nodeIndex Index of parent node in octreeNodes buffer
 * @param childIndex Child index (0-7)
 * @return Offset to child node, or 0 if child doesn't exist
 */
uint getChildOffset(uint nodeIndex, uint childIndex) {
    uint baseOffset = nodeIndex * 10;  // 10 uints per node (40 bytes)
    return octreeNodes.data[baseOffset + childIndex];
}

/**
 * @brief Get child occupancy mask for a node
 * @param nodeIndex Index of node in octreeNodes buffer
 * @return 8-bit mask where bit N indicates if child N exists
 */
uint getChildMask(uint nodeIndex) {
    uint baseOffset = nodeIndex * 10;
    uint packed = octreeNodes.data[baseOffset + 8];  // childMask + leafMask + padding
    return packed & 0xFF;  // Extract childMask (byte 0)
}

/**
 * @brief Get leaf mask for a node
 * @param nodeIndex Index of node in octreeNodes buffer
 * @return 8-bit mask where bit N indicates if child N is a leaf brick
 */
uint getLeafMask(uint nodeIndex) {
    uint baseOffset = nodeIndex * 10;
    uint packed = octreeNodes.data[baseOffset + 8];
    return (packed >> 8) & 0xFF;  // Extract leafMask (byte 1)
}

/**
 * @brief Get brick offset for a leaf node
 * @param nodeIndex Index of node in octreeNodes buffer
 * @return Offset into voxelBricks buffer, or 0 if not a leaf
 */
uint getBrickOffset(uint nodeIndex) {
    uint baseOffset = nodeIndex * 10;
    return octreeNodes.data[baseOffset + 9];
}

/**
 * @brief Check if a specific child exists (has data)
 * @param childMask 8-bit child occupancy mask from getChildMask()
 * @param childIndex Child index (0-7)
 * @return true if child exists, false if empty
 */
bool hasChild(uint childMask, uint childIndex) {
    return (childMask & (1u << childIndex)) != 0u;
}

/**
 * @brief Check if a specific child is a leaf brick
 * @param leafMask 8-bit leaf mask from getLeafMask()
 * @param childIndex Child index (0-7)
 * @return true if child is a leaf brick, false if internal node
 */
bool isLeaf(uint leafMask, uint childIndex) {
    return (leafMask & (1u << childIndex)) != 0u;
}

/**
 * @brief Get voxel value from a brick
 * @param brickIndex Index of brick in voxelBricks buffer
 * @param localPos Position within brick [0-7, 0-7, 0-7]
 * @return Voxel value (material ID, 0 = empty)
 */
uint getVoxelFromBrick(uint brickIndex, ivec3 localPos) {
    uint baseOffset = brickIndex * 128;  // 128 uints per brick (512 bytes, 4 voxels per uint)
    uint voxelIndex = uint(localPos.z * 64 + localPos.y * 8 + localPos.x);  // 0-511
    uint uintIndex = voxelIndex / 4u;  // 4 voxels packed per uint
    uint byteIndex = voxelIndex % 4u;

    uint packed = voxelBricks.data[baseOffset + uintIndex];
    return (packed >> (byteIndex * 8u)) & 0xFFu;  // Extract byte
}

/**
 * @brief Material structure matching VoxelMaterial on CPU side
 */
struct Material {
    vec3 albedo;      // RGB albedo color
    float roughness;  // Surface roughness [0-1]
    float metallic;   // Metalness [0-1]
    float emissive;   // Emissive intensity
};

/**
 * @brief Get material from material palette
 * @param materialID Material index (0-255)
 * @return Material properties
 */
Material getMaterial(uint materialID) {
    uint baseOffset = materialID * 2u;  // 2 vec4s per material (32 bytes)
    vec4 data0 = materialPalette.data[baseOffset];
    vec4 data1 = materialPalette.data[baseOffset + 1u];

    Material mat;
    mat.albedo = data0.xyz;
    mat.roughness = data0.w;
    mat.metallic = data1.x;
    mat.emissive = data1.y;
    return mat;
}

/**
 * @brief Compute child octant index from position within node bounds
 *
 * Octant encoding (Morton order):
 *   000 (0) = (x=0, y=0, z=0) - bottom-left-back
 *   001 (1) = (x=1, y=0, z=0) - bottom-right-back
 *   010 (2) = (x=0, y=1, z=0) - bottom-left-front
 *   011 (3) = (x=1, y=1, z=0) - bottom-right-front
 *   100 (4) = (x=0, y=0, z=1) - top-left-back
 *   101 (5) = (x=1, y=0, z=1) - top-right-back
 *   110 (6) = (x=0, y=1, z=1) - top-left-front
 *   111 (7) = (x=1, y=1, z=1) - top-right-front
 *
 * @param posInNode Position within node bounds [0, 1]³
 * @return Octant index (0-7)
 */
uint getChildOctant(vec3 posInNode) {
    uvec3 octant = uvec3(greaterThanEqual(posInNode, vec3(0.5)));
    return octant.x + octant.y * 2u + octant.z * 4u;
}

// ============================================================================
// DDA VOXEL TRAVERSAL (Amanatides & Woo algorithm)
// ============================================================================

/**
 * @brief Initialize DDA traversal state for voxel grid
 *
 * Sets up tMax and tDelta values for efficient voxel stepping.
 *
 * @param rayOrigin Ray origin in grid space [0, gridSize]
 * @param rayDir Normalized ray direction
 * @param gridSize Voxel grid resolution
 * @param[out] voxelPos Starting voxel position (integer coords)
 * @param[out] step Step direction per axis (+1 or -1)
 * @param[out] tMax Parametric distance to next voxel boundary per axis
 * @param[out] tDelta Distance between voxel boundaries per axis
 */
void initializeDDA(
    vec3 rayOrigin,
    vec3 rayDir,
    uint gridSize,
    out ivec3 voxelPos,
    out ivec3 step,
    out vec3 tMax,
    out vec3 tDelta
) {
    const float EPSILON = 1e-6;

    // Ray direction signs and inverse
    vec3 raySign = sign(rayDir);
    vec3 rayInvDir = 1.0 / (rayDir + vec3(EPSILON));

    // Starting voxel (integer coordinates)
    voxelPos = ivec3(floor(rayOrigin));

    // Step direction per axis (+1 or -1)
    step = ivec3(raySign);

    // tMax: parametric distance along ray to next voxel boundary per axis
    vec3 voxelBoundary = vec3(voxelPos) + max(vec3(step), vec3(0.0));
    tMax = (voxelBoundary - rayOrigin) * rayInvDir;

    // tDelta: distance along ray between voxel boundaries per axis
    tDelta = abs(rayInvDir);
}

/**
 * @brief Step to next voxel along ray using DDA algorithm
 *
 * Advances voxelPos to the next voxel by stepping along the axis with
 * the smallest tMax value (closest boundary).
 *
 * @param[inout] voxelPos Current voxel position (updated to next voxel)
 * @param[in] step Step direction per axis (+1 or -1)
 * @param[inout] tMax Parametric distance to next boundary per axis (updated)
 * @param[in] tDelta Distance between voxel boundaries per axis
 */
void stepDDA(
    inout ivec3 voxelPos,
    ivec3 step,
    inout vec3 tMax,
    vec3 tDelta
) {
    // Find axis with smallest tMax (closest boundary)
    if (tMax.x < tMax.y) {
        if (tMax.x < tMax.z) {
            // Step along X axis
            voxelPos.x += step.x;
            tMax.x += tDelta.x;
        } else {
            // Step along Z axis
            voxelPos.z += step.z;
            tMax.z += tDelta.z;
        }
    } else {
        if (tMax.y < tMax.z) {
            // Step along Y axis
            voxelPos.y += step.y;
            tMax.y += tDelta.y;
        } else {
            // Step along Z axis
            voxelPos.z += step.z;
            tMax.z += tDelta.z;
        }
    }
}
