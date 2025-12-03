#version 460

// Fragment shader for voxel ray marching
// Part of Phase J (Fragment Shader Pipeline) preparation
// Comparison target vs compute shader (Phase G)

// Input from vertex shader (fullscreen quad)
layout(location = 0) in vec2 fragUV;  // [0,1] texture coordinates

// Output color
layout(location = 0) out vec4 outColor;

// Camera uniform data
layout(set = 0, binding = 0) uniform CameraData {
    mat4 invProjection;
    mat4 invView;
    vec3 cameraPos;
    uint gridResolution;  // 32, 64, 128, 256, 512
} camera;

// Voxel data (3D texture for baseline, will add octree SSBO in Phase H)
layout(set = 0, binding = 1) uniform sampler3D voxelGrid;

// Constants
const float EPSILON = 0.001;
const int MAX_STEPS = 256;
const vec3 BACKGROUND_COLOR = vec3(0.1, 0.1, 0.15);

// Ray-AABB intersection test
bool rayAABBIntersection(vec3 rayOrigin, vec3 rayDir, vec3 aabbMin, vec3 aabbMax, out float tNear, out float tFar) {
    vec3 invDir = 1.0 / (rayDir + vec3(EPSILON));
    vec3 t0 = (aabbMin - rayOrigin) * invDir;
    vec3 t1 = (aabbMax - rayOrigin) * invDir;

    vec3 tMin = min(t0, t1);
    vec3 tMax = max(t0, t1);

    tNear = max(max(tMin.x, tMin.y), tMin.z);
    tFar = min(min(tMax.x, tMax.y), tMax.z);

    return tFar >= tNear && tFar >= 0.0;
}

// Calculate voxel face normal from hit position
vec3 calculateVoxelNormal(vec3 hitPos, ivec3 voxelPos) {
    vec3 localPos = hitPos - vec3(voxelPos);
    vec3 absLocalPos = abs(localPos - 0.5);

    float maxComponent = max(max(absLocalPos.x, absLocalPos.y), absLocalPos.z);

    if (absLocalPos.x == maxComponent) return vec3(sign(localPos.x - 0.5), 0.0, 0.0);
    if (absLocalPos.y == maxComponent) return vec3(0.0, sign(localPos.y - 0.5), 0.0);
    return vec3(0.0, 0.0, sign(localPos.z - 0.5));
}

// Simple diffuse shading
vec3 shadeVoxel(vec3 normal, vec3 albedo) {
    vec3 lightDir = normalize(vec3(1.0, 1.0, 0.5));
    float diffuse = max(dot(normal, lightDir), 0.0);
    vec3 ambient = vec3(0.2);
    return albedo * (ambient + diffuse * 0.8);
}

// DDA voxel traversal algorithm
vec4 marchVoxels(vec3 rayOrigin, vec3 rayDir) {
    // Intersect ray with voxel grid bounds [0, gridResolution]
    float tNear, tFar;
    vec3 gridMin = vec3(0.0);
    vec3 gridMax = vec3(float(camera.gridResolution));

    if (!rayAABBIntersection(rayOrigin, rayDir, gridMin, gridMax, tNear, tFar)) {
        return vec4(BACKGROUND_COLOR, 1.0);  // Ray misses grid
    }

    // Start position (slightly inside grid if outside)
    vec3 startPos = rayOrigin + rayDir * max(tNear, 0.0);

    // DDA setup
    vec3 raySign = sign(rayDir);
    vec3 rayInvDir = 1.0 / (rayDir + vec3(EPSILON));

    ivec3 voxelPos = ivec3(floor(startPos));
    ivec3 step = ivec3(raySign);

    // Calculate voxel boundary in ray direction
    vec3 voxelBoundary = vec3(voxelPos) + max(vec3(step), vec3(0.0));

    // tMax: distance to next voxel boundary per axis
    vec3 tMax = (voxelBoundary - startPos) * rayInvDir;

    // tDelta: distance between voxel boundaries per axis
    vec3 tDelta = abs(rayInvDir);

    // DDA traversal loop
    for (int i = 0; i < MAX_STEPS; i++) {
        // Bounds check
        if (any(lessThan(voxelPos, ivec3(0))) ||
            any(greaterThanEqual(voxelPos, ivec3(camera.gridResolution)))) {
            break;  // Ray exited grid
        }

        // Sample voxel
        vec3 uvw = (vec3(voxelPos) + 0.5) / float(camera.gridResolution);
        vec4 voxelData = texture(voxelGrid, uvw);

        // Hit test (alpha > 0.5 = solid voxel)
        if (voxelData.a > 0.5) {
            // Calculate hit position for normal calculation
            vec3 hitPos = startPos + rayDir * min(min(tMax.x, tMax.y), tMax.z);
            vec3 normal = calculateVoxelNormal(hitPos, voxelPos);

            // Shade voxel
            vec3 albedo = voxelData.rgb;
            vec3 finalColor = shadeVoxel(normal, albedo);

            return vec4(finalColor, 1.0);
        }

        // Step to next voxel (same logic as compute shader)
        if (tMax.x < tMax.y) {
            if (tMax.x < tMax.z) {
                voxelPos.x += step.x;
                tMax.x += tDelta.x;
            } else {
                voxelPos.z += step.z;
                tMax.z += tDelta.z;
            }
        } else {
            if (tMax.y < tMax.z) {
                voxelPos.y += step.y;
                tMax.y += tDelta.y;
            } else {
                voxelPos.z += step.z;
                tMax.z += tDelta.z;
            }
        }
    }

    // No hit - return background
    return vec4(BACKGROUND_COLOR, 1.0);
}

void main() {
    // Convert UV [0,1] to NDC [-1,1]
    vec2 ndc = fragUV * 2.0 - 1.0;

    // Unproject to world space (same as compute shader)
    vec4 clipNear = vec4(ndc, -1.0, 1.0);
    vec4 clipFar = vec4(ndc, 1.0, 1.0);

    vec4 viewNear = camera.invProjection * clipNear;
    vec4 viewFar = camera.invProjection * clipFar;

    viewNear /= viewNear.w;
    viewFar /= viewFar.w;

    vec4 worldNear = camera.invView * viewNear;
    vec4 worldFar = camera.invView * viewFar;

    // Ray definition
    vec3 rayOrigin = camera.cameraPos;
    vec3 rayDir = normalize(worldFar.xyz - worldNear.xyz);

    // March through voxel grid
    outColor = marchVoxels(rayOrigin, rayDir);
}
