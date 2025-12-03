/**
 * Compression.glsl - GPU Decompression Utilities for Voxel Data
 *
 * Provides GLSL functions for decoding DXT-compressed voxel attributes.
 * Based on ESVO (Laine & Karras 2010) compression scheme.
 *
 * Include this file in shaders that need to decode compressed brick data:
 *   #include "Compression.glsl"
 *
 * Usage:
 *   // For color blocks (64-bit = uvec2)
 *   uvec2 colorBlock = loadDXTColorBlock(brickOffset, parentIndex);
 *   vec3 color = decodeDXT1Color(colorBlock, voxelIndex);
 *
 *   // For normal blocks (128-bit = 2x uvec2)
 *   uvec2 normalBlockA, normalBlockB;
 *   loadDXTNormalBlock(brickOffset, parentIndex, normalBlockA, normalBlockB);
 *   vec3 normal = decodeDXTNormal(normalBlockA, normalBlockB, voxelIndex);
 */

#ifndef COMPRESSION_GLSL
#define COMPRESSION_GLSL

// ============================================================================
// CONSTANTS
// ============================================================================

// Scale factor 1.0 / 2^32
const float DXT_SCALE = 1.0 / 4294967296.0;

/// DXT1 color interpolation coefficients
/// Index mapping: 0 = ref0, 1 = ref1, 2 = 2/3*ref0+1/3*ref1, 3 = 1/3*ref0+2/3*ref1
const float DXT_COLOR_COEFS[4] = float[4](
    DXT_SCALE,                  // c0 for index 0
    0.0,                        // c0 for index 1
    2.0 * DXT_SCALE / 3.0,      // c0 for index 2
    1.0 * DXT_SCALE / 3.0       // c0 for index 3
);

/// DXT normal interpolation coefficients: {-1, -1/3, 1/3, 1}
const float DXT_NORMAL_COEFS[4] = float[4](-1.0, -1.0/3.0, 1.0/3.0, 1.0);

// ============================================================================
// DXT1 COLOR DECOMPRESSION
// ============================================================================

/**
 * Decode a single color from a DXT1-compressed block
 *
 * Block format (64 bits stored as uvec2):
 *   block.x (bits 0-31): Two RGB-565 reference colors
 *     - ref0: R[15:11], G[10:5], B[4:0]
 *     - ref1: R[31:27], G[26:21], B[20:16]
 *   block.y (bits 32-63): 16 x 2-bit interpolation indices
 *
 * @param block  64-bit compressed block (as uvec2)
 * @param texelIdx  Which of 16 texels to decode (0-15)
 * @return RGB color in [0,1] range
 */
vec3 decodeDXT1Color(uvec2 block, int texelIdx) {
    uint head = block.x;
    uint bits = block.y;

    // Get interpolation coefficient for this texel
    float c0 = DXT_COLOR_COEFS[(bits >> (texelIdx * 2)) & 3u];
    float c1 = DXT_SCALE - c0;

    // Decode and interpolate RGB components
    // The bit shifts extract RGB-565 components and scale them
    return vec3(
        c0 * float(head << 16) + c1 * float(head),
        c0 * float(head << 21) + c1 * float(head << 5),
        c0 * float(head << 27) + c1 * float(head << 11)
    );
}

/**
 * Decode all 16 colors from a DXT1 block
 *
 * @param block  64-bit compressed block (as uvec2)
 * @param colors Output array of 16 colors
 */
void decodeDXT1ColorBlock(uvec2 block, out vec3 colors[16]) {
    uint head = block.x;
    uint bits = block.y;

    for (int i = 0; i < 16; i++) {
        float c0 = DXT_COLOR_COEFS[(bits >> (i * 2)) & 3u];
        float c1 = DXT_SCALE - c0;

        colors[i] = vec3(
            c0 * float(head << 16) + c1 * float(head),
            c0 * float(head << 21) + c1 * float(head << 5),
            c0 * float(head << 27) + c1 * float(head << 11)
        );
    }
}

// ============================================================================
// DXT NORMAL DECOMPRESSION
// ============================================================================

/**
 * Decode raw normal from 32-bit packed format
 *
 * Format:
 *   bit 31: sign of dominant axis
 *   bits 29-30: dominant axis (0=X, 1=Y, 2=Z)
 *   bits 14-28: U component (15 bits)
 *   bits 0-13: V component (14 bits)
 *
 * @param value  32-bit packed normal
 * @return Unnormalized normal vector (call normalize() if needed)
 */
vec3 decodeRawNormal(uint value) {
    int sign = int(value) >> 31;
    int axis = int((value >> 29) & 3u);
    int t = sign ^ 0x7FFFFFFF;
    int u = int(value << 3);
    int v = int(value << 18);

    if (axis == 0) return vec3(float(t), float(u), float(v));
    if (axis == 1) return vec3(float(v), float(t), float(u));
    return vec3(float(u), float(v), float(t));
}

/**
 * Decode normal axis vector from packed UV format
 *
 * @param headUV  32-bit packed UV header
 * @param shift   Bit shift (0 for U axis, 16 for V axis)
 * @return Decoded axis vector
 */
vec3 decodeNormalAxis(uint headUV, int shift) {
    int shifted = int(headUV) << (16 - shift);
    int exponent = (shifted >> 16) & 15;
    float scale = exp2(float(exponent + (3 - 13)));

    return vec3(
        float(shifted) * scale,
        float(shifted << 4) * scale,
        float(shifted << 8) * scale
    );
}

/**
 * Decode a single normal from DXT-compressed normal block
 *
 * Block format (128 bits stored as 2x uvec2):
 *   blockA.x: Base normal (32-bit packed)
 *   blockA.y: U interpolation bits (16 x 2-bit)
 *   blockB.x: UV axis encoding (32-bit)
 *   blockB.y: V interpolation bits (16 x 2-bit)
 *
 * @param blockA  First 64-bit block (base + U bits)
 * @param blockB  Second 64-bit block (UV axes + V bits)
 * @param texelIdx  Which of 16 texels to decode (0-15)
 * @return Normal vector (unnormalized - call normalize() if needed)
 */
vec3 decodeDXTNormal(uvec2 blockA, uvec2 blockB, int texelIdx) {
    uint headBase = blockA.x;
    uint headUV = blockB.x;
    uint bitsU = blockA.y;
    uint bitsV = blockB.y;

    // Decode base normal and axis vectors
    vec3 base = decodeRawNormal(headBase);
    vec3 u = decodeNormalAxis(headUV, 0);
    vec3 v = decodeNormalAxis(headUV, 16);

    // Get interpolation coefficients for this texel
    float cu = DXT_NORMAL_COEFS[(bitsU >> (texelIdx * 2)) & 3u];
    float cv = DXT_NORMAL_COEFS[(bitsV >> (texelIdx * 2)) & 3u];

    // Combine base with weighted U and V axes
    return base + u * cu + v * cv;
}

/**
 * Decode all 16 normals from a DXT normal block
 *
 * @param blockA  First 64-bit block
 * @param blockB  Second 64-bit block
 * @param normals Output array of 16 normals (unnormalized)
 */
void decodeDXTNormalBlock(uvec2 blockA, uvec2 blockB, out vec3 normals[16]) {
    uint headBase = blockA.x;
    uint headUV = blockB.x;
    uint bitsU = blockA.y;
    uint bitsV = blockB.y;

    vec3 base = decodeRawNormal(headBase);
    vec3 u = decodeNormalAxis(headUV, 0);
    vec3 v = decodeNormalAxis(headUV, 16);

    for (int i = 0; i < 16; i++) {
        float cu = DXT_NORMAL_COEFS[(bitsU >> (i * 2)) & 3u];
        float cv = DXT_NORMAL_COEFS[(bitsV >> (i * 2)) & 3u];
        normals[i] = base + u * cu + v * cv;
    }
}

// ============================================================================
// BLOCK ADDRESSING HELPERS
// ============================================================================

/**
 * Compute texel index within a 16-voxel compression block
 *
 * DXT blocks contain 16 voxels arranged as 2 parent nodes x 8 children each.
 * Within a brick (8x8x8 = 512 voxels), there are 32 DXT blocks.
 *
 * @param localVoxelIdx  Local voxel index within brick (0-511)
 * @return Texel index within DXT block (0-15)
 */
int getTexelIndex(int localVoxelIdx) {
    // 16 voxels per block = 2 parents x 8 children
    return localVoxelIdx & 15;
}

/**
 * Compute DXT block index within a brick
 *
 * @param localVoxelIdx  Local voxel index within brick (0-511)
 * @return Block index within brick (0-31)
 */
int getBlockIndex(int localVoxelIdx) {
    return localVoxelIdx >> 4;  // Divide by 16
}

/**
 * Convert 3D voxel position to linear index within brick
 *
 * @param pos  3D position within brick (each component 0-7)
 * @return Linear index (0-511)
 */
int positionToVoxelIndex(ivec3 pos) {
    return pos.x + pos.y * 8 + pos.z * 64;
}

/**
 * Convert linear voxel index to 3D position within brick
 *
 * @param idx  Linear index (0-511)
 * @return 3D position within brick
 */
ivec3 voxelIndexToPosition(int idx) {
    return ivec3(
        idx & 7,
        (idx >> 3) & 7,
        (idx >> 6) & 7
    );
}

// ============================================================================
// COMPRESSED BUFFER ACCESS (TEMPLATE - CUSTOMIZE PER SHADER)
// ============================================================================

// These functions should be implemented in the including shader based on
// how compressed data is stored in SSBOs/uniforms.

/*
// Example implementation for VoxelRayMarch.comp:

// Compressed color buffer - 32 blocks per brick, 8 bytes per block
layout(std430, binding = X) readonly buffer CompressedColors {
    uvec2 colorBlocks[];  // [brickIndex * 32 + blockIndex]
};

// Compressed normal buffer - 32 blocks per brick, 16 bytes per block
layout(std430, binding = Y) readonly buffer CompressedNormals {
    uvec4 normalBlocks[];  // [brickIndex * 32 + blockIndex], .xy = blockA, .zw = blockB
};

uvec2 loadColorBlock(uint brickIndex, int blockIndex) {
    return colorBlocks[brickIndex * 32 + blockIndex];
}

void loadNormalBlock(uint brickIndex, int blockIndex, out uvec2 blockA, out uvec2 blockB) {
    uvec4 packed = normalBlocks[brickIndex * 32 + blockIndex];
    blockA = packed.xy;
    blockB = packed.zw;
}

// Then to decode a voxel's color:
vec3 getVoxelColor(uint brickIndex, int localVoxelIdx) {
    int blockIdx = getBlockIndex(localVoxelIdx);
    int texelIdx = getTexelIndex(localVoxelIdx);
    uvec2 block = loadColorBlock(brickIndex, blockIdx);
    return decodeDXT1Color(block, texelIdx);
}
*/

#endif // COMPRESSION_GLSL
