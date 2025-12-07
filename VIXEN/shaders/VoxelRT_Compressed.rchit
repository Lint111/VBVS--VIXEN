#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require

// ============================================================================
// VoxelRT_Compressed.rchit - Compressed Closest Hit Shader (Phase K: Hardware RT)
// ============================================================================
// Called when a ray hits the closest voxel AABB.
// Uses DXT-compressed color/normal buffers instead of material ID lookup.
//
// COMPRESSION: Uses same DXT compression scheme as VoxelRayMarch_Compressed.comp
// - Color:  32 blocks * 8 bytes  = 256 bytes per brick
// - Normal: 32 blocks * 16 bytes = 512 bytes per brick
// - Total:  768 bytes per brick (4:1 compression vs uncompressed)
//
// BINDING DIFFERENCES FROM VoxelRT.rchit:
// - Binding 3: brickData (material IDs for voxel lookup)
// - Binding 6: compressedColors (DXT1 color blocks)
// - Binding 7: compressedNormals (DXT normal blocks)
// ============================================================================

#include "Compression.glsl"

// Hit attributes from intersection shader
hitAttributeEXT vec3 hitNormal;

// Ray payload - color output
layout(location = 0) rayPayloadInEXT vec3 hitColor;

// Brick data buffer - indexed by gl_PrimitiveID to get brick index
// Each AABB corresponds to a voxel, and we need to find which brick it belongs to
layout(std430, binding = 3) readonly buffer BrickDataBuffer {
    uint brickData[];  // Material IDs (512 per brick)
} brickDataBuffer;

// DXT1 compressed colors - 32 blocks per brick, 8 bytes (uvec2) per block
layout(std430, binding = 6) readonly buffer CompressedColorBuffer {
    uvec2 compressedColors[];
};

// DXT compressed normals - 32 blocks per brick, 16 bytes (uvec4) per block
layout(std430, binding = 7) readonly buffer CompressedNormalBuffer {
    uvec4 compressedNormals[];
};

// OctreeConfigUBO - matches compute shader binding 5
layout(std140, binding = 5) uniform OctreeConfigUBO {
    int esvoMaxScale;
    int userMaxLevels;
    int brickDepthLevels;
    int brickSize;
    int minESVOScale;
    int brickESVOScale;
    int bricksPerAxis;
    int _padding1;
    vec3 gridMin;
    float _padding2;
    vec3 gridMax;
    float _padding3;
    mat4 localToWorld;
    mat4 worldToLocal;
} octreeConfig;

// Push constants (same as compute shader)
layout(push_constant) uniform PushConstants {
    vec3 cameraPos;
    float time;
    vec3 cameraDir;
    float fov;
    vec3 cameraUp;
    float aspect;
    vec3 cameraRight;
    int debugMode;
} pc;

// ============================================================================
// COMPRESSED BUFFER ACCESS FUNCTIONS
// ============================================================================

uvec2 loadColorBlock(uint brickIndex, int voxelLinearIdx) {
    int blockIdx = voxelLinearIdx >> 4;
    return compressedColors[brickIndex * 32u + uint(blockIdx)];
}

void loadNormalBlocks(uint brickIndex, int voxelLinearIdx, out uvec2 blockA, out uvec2 blockB) {
    int blockIdx = voxelLinearIdx >> 4;
    uvec4 packed = compressedNormals[brickIndex * 32u + uint(blockIdx)];
    blockA = packed.xy;
    blockB = packed.zw;
}

vec3 getCompressedVoxelColor(uint brickIndex, int voxelLinearIdx) {
    uvec2 block = loadColorBlock(brickIndex, voxelLinearIdx);
    int texelIdx = voxelLinearIdx & 15;
    return decodeDXT1Color(block, texelIdx);
}

vec3 getCompressedVoxelNormal(uint brickIndex, int voxelLinearIdx) {
    uvec2 blockA, blockB;
    loadNormalBlocks(brickIndex, voxelLinearIdx, blockA, blockB);
    int texelIdx = voxelLinearIdx & 15;
    return normalize(decodeDXTNormal(blockA, blockB, texelIdx));
}

// Lighting computation - matches compute shader (Lighting.glsl)
vec3 computeLighting(vec3 baseColor, vec3 normal, vec3 rayDir) {
    // Simple directional light from upper-right (same as compute)
    vec3 lightDir = normalize(vec3(1.0, 1.0, -1.0));

    // Basic diffuse lighting
    float NdotL = max(dot(normal, lightDir), 0.0);

    // Ambient term (matches compute shader)
    float ambient = 0.3;

    // Final color with lighting
    float lighting = ambient + (1.0 - ambient) * NdotL;
    return baseColor * lighting;
}

void main() {
    // Get ray direction (in local voxel space, matches rgen)
    vec3 rayDir = gl_WorldRayDirectionEXT;

    // In RTX mode with AABBs, gl_PrimitiveID corresponds to the voxel index
    // We need to compute brick index and local voxel index
    // For simplicity, assume 1:1 mapping where each AABB = one voxel
    // The AABB converter creates AABBs with materialIds in parallel buffer

    // For compressed RTX, we need to know which brick this voxel belongs to
    // The primitive ID maps to a linear voxel index in the scene
    // We compute brick index from the spatial position

    uint primitiveId = gl_PrimitiveID;

    // Get hit position to determine which brick we're in
    vec3 hitPos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;

    // Convert hit position to voxel coordinates
    float resolution = float(1 << octreeConfig.userMaxLevels);
    ivec3 voxelCoord = ivec3(floor(clamp(hitPos, vec3(0.0), vec3(resolution - 0.001))));

    // Compute brick coordinates (8x8x8 voxels per brick)
    ivec3 brickCoord = voxelCoord >> 3;  // Divide by 8
    ivec3 localVoxelCoord = voxelCoord & 7;  // Mod 8

    // Compute brick index (assuming bricksPerAxis is consistent)
    int bricksPerAxis = octreeConfig.bricksPerAxis;
    uint brickIndex = uint(brickCoord.x + brickCoord.y * bricksPerAxis +
                          brickCoord.z * bricksPerAxis * bricksPerAxis);

    // Compute local voxel index within brick
    int voxelLinearIdx = localVoxelCoord.x + localVoxelCoord.y * 8 + localVoxelCoord.z * 64;

    // Get compressed color and normal
    vec3 baseColor = getCompressedVoxelColor(brickIndex, voxelLinearIdx);
    vec3 compressedNormal = getCompressedVoxelNormal(brickIndex, voxelLinearIdx);

    // Use intersection shader normal as primary, blend with compressed normal
    vec3 N = normalize(hitNormal);
    vec3 finalNormal = normalize(N + compressedNormal * 0.3);

    // Apply lighting
    hitColor = computeLighting(baseColor, finalNormal, rayDir);

    // Debug modes (aligned with compute shader debug modes)
    if (pc.debugMode > 0) {
        switch (pc.debugMode) {
            case 2:  // DEBUG_MODE_DEPTH
                {
                    float depth = gl_HitTEXT / 100.0;
                    hitColor = vec3(depth, depth * 0.5, 1.0 - depth);
                }
                break;

            case 5:  // DEBUG_MODE_NORMALS
                hitColor = finalNormal * 0.5 + 0.5;
                break;

            case 6:  // DEBUG_MODE_POSITION
                hitColor = fract(hitPos);
                break;

            case 7:  // DEBUG_MODE_BRICKS
                hitColor = vec3(float(brickCoord.x & 1), float(brickCoord.y & 1), float(brickCoord.z & 1));
                break;

            case 8:  // DEBUG_MODE_MATERIALS (show compressed color directly)
                hitColor = baseColor;
                break;
        }
    }
}
