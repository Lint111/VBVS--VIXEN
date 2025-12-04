#version 460
#extension GL_GOOGLE_include_directive : require

// ============================================================================
// VoxelRayMarch.frag - ESVO Fragment Shader Ray Marching
// ============================================================================
// Full-screen fragment shader for ESVO octree ray marching.
// Uses same traversal algorithm as VoxelRayMarch.comp with shared includes.
// Paired with Fullscreen.vert (fullscreen triangle).
// ============================================================================

#include "SVOTypes.glsl"

// Fragment shader inputs from Fullscreen.vert
layout(location = 0) in vec2 fragUV;  // [0,1] texture coordinates

// Fragment shader output
layout(location = 0) out vec4 outColor;

// ============================================================================
// BUFFER BINDINGS (Same as compute shader)
// ============================================================================

layout(std430, binding = 1) readonly buffer ESVOBuffer {
    uvec2 esvoNodes[];
};

layout(std430, binding = 2) readonly buffer BrickBuffer {
    uint brickData[];
};

layout(std430, binding = 3) readonly buffer MaterialBuffer {
    Material materials[];
};

layout(std430, binding = 4) buffer RayTraceBuffer {
    uint traceWriteIndex;
    uint traceCapacity;
    uint _padding[2];
    uint traceData[];
};

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
    float _padding4[16];
} octreeConfig;

// ============================================================================
// PUSH CONSTANTS (Same as compute shader)
// ============================================================================

#define DEBUG_MODE_NORMAL 0
#define DEBUG_MODE_OCTANT 1
#define DEBUG_MODE_DEPTH 2
#define DEBUG_MODE_ITERATIONS 3
#define DEBUG_MODE_T_SPAN 4
#define DEBUG_MODE_NORMALS 5
#define DEBUG_MODE_POSITION 6

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
// SHARED INCLUDES
// ============================================================================

#include "CoordinateTransforms.glsl"
#include "RayGeneration.glsl"
#include "ESVOCoefficients.glsl"
#include "TraceRecording.glsl"
#include "ESVOTraversal.glsl"
#include "Lighting.glsl"

// ============================================================================
// BRICK DDA (Same as compute shader)
// ============================================================================

bool marchBrickFromPos(vec3 rayDir, vec3 posInBrick, uint brickIndex,
                       out vec3 hitColor, out vec3 hitNormal, out uint axisMask,
                       out vec3 hitBrickLocalPos) {
    ivec3 currentVoxel = ivec3(floor(posInBrick));
    currentVoxel = clamp(currentVoxel, ivec3(0), ivec3(7));

    ivec3 step = ivec3(sign(rayDir));
    if (step.x == 0) step.x = 1;
    if (step.y == 0) step.y = 1;
    if (step.z == 0) step.z = 1;

    int BRICK_SIZE_VAL = octreeConfig.brickSize;
    if ((posInBrick.x <= 0.001 && rayDir.x < 0.0) || (posInBrick.x >= float(BRICK_SIZE_VAL)-0.001 && rayDir.x > 0.0) ||
        (posInBrick.y <= 0.001 && rayDir.y < 0.0) || (posInBrick.y >= float(BRICK_SIZE_VAL)-0.001 && rayDir.y > 0.0) ||
        (posInBrick.z <= 0.001 && rayDir.z < 0.0) || (posInBrick.z >= float(BRICK_SIZE_VAL)-0.001 && rayDir.z > 0.0)) {
        return false;
    }

    vec3 deltaDist;
    deltaDist.x = abs(rayDir.x) > DIR_EPSILON ? 1.0 / abs(rayDir.x) : 1e20;
    deltaDist.y = abs(rayDir.y) > DIR_EPSILON ? 1.0 / abs(rayDir.y) : 1e20;
    deltaDist.z = abs(rayDir.z) > DIR_EPSILON ? 1.0 / abs(rayDir.z) : 1e20;

    vec3 tMax;
    const float MIN_DIST = 0.0001;
    for (int axis = 0; axis < 3; axis++) {
        if (abs(rayDir[axis]) < DIR_EPSILON) {
            tMax[axis] = 1e20;
        } else {
            float posLocal = posInBrick[axis];
            float distToNext;
            if (rayDir[axis] > 0.0) {
                distToNext = float(currentVoxel[axis] + 1) - posLocal;
            } else {
                distToNext = posLocal - float(currentVoxel[axis]);
            }
            distToNext = max(distToNext, MIN_DIST);
            tMax[axis] = distToNext / abs(rayDir[axis]);
        }
    }

    axisMask = 0u;
    const int MAX_STEPS = 300;
    for (int i = 0; i < MAX_STEPS; i++) {
        if (any(lessThan(currentVoxel, ivec3(0))) || any(greaterThanEqual(currentVoxel, ivec3(8)))) {
            break;
        }

        uint voxelLinearIdx = uint(currentVoxel.z * 64 + currentVoxel.y * 8 + currentVoxel.x);
        uint voxelData = brickData[brickIndex * 512u + voxelLinearIdx];

        if (voxelData != 0u) {
            uint matID = voxelData & 0xFFu;

            if (matID == 1u) hitColor = vec3(1.0, 0.0, 0.0);
            else if (matID == 2u) hitColor = vec3(0.0, 1.0, 0.0);
            else if (matID == 3u) hitColor = vec3(0.9, 0.9, 0.9);
            else if (matID == 4u) hitColor = vec3(1.0, 0.8, 0.0);
            else if (matID == 5u) hitColor = vec3(0.8, 0.8, 0.8);
            else if (matID == 6u) hitColor = vec3(0.7, 0.7, 0.7);
            else hitColor = vec3(float(matID) / 10.0);

            hitNormal = vec3(0.0);
            if (axisMask == 1u) hitNormal.x = -float(step.x);
            else if (axisMask == 2u) hitNormal.y = -float(step.y);
            else hitNormal.z = -float(step.z);

            if (i == 0) {
                vec3 absRayDir = abs(rayDir);
                if (absRayDir.x > absRayDir.y && absRayDir.x > absRayDir.z) {
                    hitNormal = vec3(-sign(rayDir.x), 0.0, 0.0);
                } else if (absRayDir.y > absRayDir.z) {
                    hitNormal = vec3(0.0, -sign(rayDir.y), 0.0);
                } else {
                    hitNormal = vec3(0.0, 0.0, -sign(rayDir.z));
                }
            }

            hitBrickLocalPos = vec3(currentVoxel) + vec3(0.5);
            return true;
        }

        if (tMax.x < tMax.y && tMax.x < tMax.z) {
            currentVoxel.x += step.x;
            tMax.x += deltaDist.x;
            axisMask = 1u;
        } else if (tMax.y < tMax.z) {
            currentVoxel.y += step.y;
            tMax.y += deltaDist.y;
            axisMask = 2u;
        } else {
            currentVoxel.z += step.z;
            tMax.z += deltaDist.z;
            axisMask = 4u;
        }
    }

    return false;
}

// ============================================================================
// LEAF HIT HANDLING
// ============================================================================

bool handleLeafHit(TraversalState state, RayCoefficients coef,
                   vec3 rayStartWorld, vec3 rayDir, float tBias,
                   uvec2 parentDescriptor, uint validMask, uint leafMask, uint parentNodeIndex,
                   inout StackEntry stack[STACK_SIZE],
                   out vec3 hitColor, out vec3 hitNormal, out float hitT) {

    int BRICK_SIZE_VAL = octreeConfig.brickSize;

    int localChildIdx = mirroredToLocalOctant(state.idx, coef.octant_mask);
    if (localChildIdx < 0 || localChildIdx > 7) {
        return false;
    }

    uint childPointer = getChildPointer(parentDescriptor);
    uint totalInternalChildren = bitCount(validMask & ~leafMask);
    uint leafChildrenBeforeMe = bitCount(leafMask & ((1u << localChildIdx) - 1u));
    uint leafDescriptorIndex = childPointer + totalInternalChildren + leafChildrenBeforeMe;

    uvec2 leafDescriptor = fetchESVONode(leafDescriptorIndex);
    uint brickIndex = getContourPointer(leafDescriptor);

    if (brickIndex == 0u || brickIndex >= 0xFFFFFFu) {
        return false;
    }

    vec3 hitPos12 = rayStartWorld;
    hitPos12 = worldToNormalized(hitPos12);
    float hitT_esvo = state.t_min;
    vec3 rayDirLocal = mat3(octreeConfig.worldToLocal) * rayDir;
    hitPos12 = hitPos12 + rayDirLocal * hitT_esvo;
    hitPos12 = clamp(hitPos12, vec3(1.0), vec3(2.0) - vec3(0.00001));

    vec3 posInBrick = computePosInBrick(hitPos12, state.pos, state.scale_exp2, coef.octant_mask, BRICK_SIZE_VAL);
    posInBrick = clamp(posInBrick, vec3(0.0), vec3(float(BRICK_SIZE_VAL) - 0.001));

    vec3 brickHitColor, brickHitNormal;
    uint axisMask;
    vec3 hitBrickLocalPos;
    if (marchBrickFromPos(rayDir, posInBrick, brickIndex, brickHitColor, brickHitNormal, axisMask, hitBrickLocalPos)) {
        hitColor = brickHitColor;
        hitNormal = brickHitNormal;
        hitT = tBias + hitT_esvo;
        return true;
    }

    return false;
}

// ============================================================================
// MAIN TRAVERSAL LOOP
// ============================================================================

bool traverseOctree(vec3 rayOrigin, vec3 rayDir,
                    out vec3 hitColor, out vec3 hitNormal, out float hitT,
                    inout DebugRaySample debugInfo) {

    debugInfo.hitFlag = 0u;
    debugInfo.exitCode = DEBUG_EXIT_NONE;
    debugInfo.lastStepMask = 0u;
    debugInfo.iterationCount = 0u;

    vec3 rayOriginLocal = (octreeConfig.worldToLocal * vec4(rayOrigin, 1.0)).xyz;
    vec3 rayDirLocal = mat3(octreeConfig.worldToLocal) * rayDir;

    vec2 gridT = rayAABBIntersection(rayOriginLocal, rayDirLocal, vec3(0.0), vec3(1.0));
    if (gridT.y < 0.0) {
        debugInfo.exitCode = DEBUG_EXIT_INVALID_SPAN;
        return false;
    }

    bool rayStartsInside = (gridT.x < 0.0);

    vec3 rayStartWorld;
    float tEntryWorld = 0.0;
    if (rayStartsInside) {
        rayStartWorld = rayOrigin;
    } else {
        vec3 entryPointLocal = rayOriginLocal + rayDirLocal * (gridT.x + EPSILON);
        rayStartWorld = (octreeConfig.localToWorld * vec4(entryPointLocal, 1.0)).xyz;
        tEntryWorld = length(rayStartWorld - rayOrigin);
    }

    RayCoefficients coef = initRayCoefficients(rayDir, rayStartWorld);
    debugInfo.octantMask = uint(coef.octant_mask);

    StackEntry stack[STACK_SIZE];
    TraversalState state = initTraversalState(coef, stack, rayStartsInside);
    snapshotTraversalState(state, coef, debugInfo);

    if (state.t_min >= state.t_max) {
        debugInfo.exitCode = DEBUG_EXIT_INVALID_SPAN;
        return false;
    }

    int iter = 0;
    for (; iter < MAX_ITERS && state.scale <= octreeConfig.esvoMaxScale; ++iter) {

        uvec2 parent_descriptor = fetchESVONode(state.parentPtr);
        uint validMask = getValidMask(parent_descriptor);
        uint leafMask = getLeafMask(parent_descriptor);
        uint childPointer = getChildPointer(parent_descriptor);

        bool isLeaf;
        float tv_max, tx_center, ty_center, tz_center;

        if (checkChildValidity(state, coef, validMask, leafMask,
                               isLeaf, tv_max, tx_center, ty_center, tz_center)) {

            if (isLeaf) {
                float tBias = tEntryWorld;
                if (handleLeafHit(state, coef, rayStartWorld, rayDir, tBias,
                                  parent_descriptor, validMask, leafMask, state.parentPtr,
                                  stack, hitColor, hitNormal, hitT)) {
                    snapshotTraversalState(state, coef, debugInfo);
                    debugInfo.hitFlag = 1u;
                    debugInfo.exitCode = DEBUG_EXIT_HIT;
                    debugInfo.iterationCount = uint(iter + 1);
                    return true;
                }

                state.t_min = tv_max;
                snapshotTraversalState(state, coef, debugInfo);
            } else {
                executePushPhase(state, coef, stack, validMask, leafMask, childPointer,
                                 tv_max, tx_center, ty_center, tz_center);
                snapshotTraversalState(state, coef, debugInfo);
                continue;
            }
        }

        int step_mask;
        int advanceResult = executeAdvancePhase(state, coef, step_mask);
        debugInfo.lastStepMask = uint(step_mask);
        snapshotTraversalState(state, coef, debugInfo);

        if (advanceResult == 0) {
             if (state.scale < octreeConfig.esvoMaxScale) {
                 state.t_max = stack[state.scale + 1].t_max;
             }
        }

        if (advanceResult == 1) {
            int popResult = executePopPhase(state, coef, stack, step_mask);
            snapshotTraversalState(state, coef, debugInfo);
            if (popResult == 1) {
                debugInfo.exitCode = DEBUG_EXIT_STACK;
                debugInfo.iterationCount = uint(iter + 1);
                return false;
            }
        }
    }

    debugInfo.exitCode = DEBUG_EXIT_NO_HIT;
    debugInfo.iterationCount = uint(iter);
    return false;
}

// ============================================================================
// MAIN
// ============================================================================

void main() {
    vec3 rayOrigin = pc.cameraPos;
    vec3 rayDir = getRayDir(fragUV);

    vec3 color = vec3(0.0);
    float hitT = 0.0;

    DebugRaySample debugSample;
    debugSample.pixel = uvec2(gl_FragCoord.xy);
    debugSample.rayDir = rayDir;
    debugSample.octantMask = 0u;
    debugSample.hitFlag = 0u;
    debugSample.exitCode = DEBUG_EXIT_NONE;
    debugSample.lastStepMask = 0u;
    debugSample.iterationCount = 0u;
    debugSample.scale = octreeConfig.esvoMaxScale;
    debugSample.stateIdx = 0u;
    debugSample.tMin = 0.0;
    debugSample.tMax = 0.0;
    debugSample.scaleExp2 = 0.0;
    debugSample.posMirrored = vec3(0.0);
    debugSample.localNorm = vec3(0.0);

    vec3 rayOriginLocal = (octreeConfig.worldToLocal * vec4(rayOrigin, 1.0)).xyz;
    vec3 rayDirLocal = mat3(octreeConfig.worldToLocal) * rayDir;
    vec2 gridT = rayAABBIntersection(rayOriginLocal, rayDirLocal, vec3(0.0), vec3(1.0));

    if (gridT.y >= 0.0) {
        vec3 hitColor, hitNormal;
        bool hit = traverseOctree(rayOrigin, rayDir, hitColor, hitNormal, hitT, debugSample);

        if (hit) {
            color = computeLighting(hitColor, hitNormal, rayDir);
        } else {
            color = vec3(0.5, 0.7, 1.0) * (1.0 - fragUV.y * 0.5);
        }
    } else {
        color = vec3(0.5, 0.7, 1.0) * (1.0 - fragUV.y * 0.5);
    }

    // Debug visualization modes
    if (pc.debugMode > 0) {
        switch (pc.debugMode) {
            case DEBUG_MODE_OCTANT:
                color = vec3(
                    float(debugSample.octantMask & 1u),
                    float((debugSample.octantMask >> 1u) & 1u),
                    float((debugSample.octantMask >> 2u) & 1u)
                );
                break;

            case DEBUG_MODE_DEPTH:
                {
                    float normalizedScale = float(debugSample.scale - 16) / float(octreeConfig.esvoMaxScale - 16);
                    color = mix(vec3(1.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0), normalizedScale);
                }
                break;

            case DEBUG_MODE_ITERATIONS:
                {
                    float normalizedIter = float(debugSample.iterationCount) / 100.0;
                    color = mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), clamp(normalizedIter, 0.0, 1.0));
                }
                break;

            case DEBUG_MODE_T_SPAN:
                color = vec3(debugSample.tMin * 0.1, debugSample.tMax * 0.1, 0.0);
                break;

            case DEBUG_MODE_NORMALS:
                color = normalShading(debugSample.localNorm);
                break;

            case DEBUG_MODE_POSITION:
                color = debugSample.posMirrored - vec3(1.0);
                break;
        }
    }

    outColor = vec4(color, 1.0);
}
