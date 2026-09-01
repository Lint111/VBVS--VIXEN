#version 460

// B2 compact output: uint entryKey, uint exitBits, uint candidateMask[6].
// The whole buffer is zero-cleared before this pass. Positive-float bit order
// plus atomicMax yields max(tExit); reversed positive-float bits yield min(tEnter).

layout(early_fragment_tests) in;

layout(push_constant) uniform ProxyRasterPushConstants {
    mat4 viewProj;
    vec4 cameraPosFov;
    vec4 cameraDirAspect;
    vec4 cameraUpWidth;
    vec4 cameraRightHeight;
} pc;

layout(location = 0) flat in vec3 proxyWorldMin;
layout(location = 1) flat in vec3 proxyWorldMax;
layout(location = 2) flat in uint bodyIndex;

layout(std430, binding = 3) coherent buffer ProxyIntervalBuffer {
    uint proxyIntervalWords[];
};

bool intersectAxis(float origin, float direction, float boundMin, float boundMax,
                   inout float tEnter, inout float tExit) {
    if (abs(direction) < 1.0e-8) {
        return origin >= boundMin && origin <= boundMax;
    }
    float t0 = (boundMin - origin) / direction;
    float t1 = (boundMax - origin) / direction;
    if (t0 > t1) {
        float swapValue = t0;
        t0 = t1;
        t1 = swapValue;
    }
    tEnter = max(tEnter, t0);
    tExit = min(tExit, t1);
    return tEnter <= tExit;
}

vec3 pixelRayDirection(vec2 uv) {
    const float tanHalfFov = tan(radians(pc.cameraPosFov.w * 0.5));
    vec2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    return normalize(pc.cameraDirAspect.xyz +
                     pc.cameraRightHeight.xyz * ndc.x * tanHalfFov * pc.cameraDirAspect.w +
                     pc.cameraUpWidth.xyz * ndc.y * tanHalfFov);
}

uvec2 renderDimensions() {
    const uint packed = floatBitsToUint(pc.cameraRightHeight.w);
    return uvec2(packed & 0xffffu, packed >> 16u);
}

void main() {
    if (bodyIndex >= 192u) return;
    const uvec2 dims = renderDimensions();
    const uvec2 pixel = uvec2(gl_FragCoord.xy);
    if (any(greaterThanEqual(pixel, dims))) return;

    const vec2 uv = (vec2(pixel) + 0.5) / vec2(dims);
    const vec3 rayOrigin = pc.cameraPosFov.xyz;
    const vec3 rayDirection = pixelRayDirection(uv);
    float tEnter = -3.402823466e+38;
    float tExit = 3.402823466e+38;
    if (!intersectAxis(rayOrigin.x, rayDirection.x, proxyWorldMin.x, proxyWorldMax.x,
                       tEnter, tExit) ||
        !intersectAxis(rayOrigin.y, rayDirection.y, proxyWorldMin.y, proxyWorldMax.y,
                       tEnter, tExit) ||
        !intersectAxis(rayOrigin.z, rayDirection.z, proxyWorldMin.z, proxyWorldMax.z,
                       tEnter, tExit) || tExit < 0.0 || isnan(tEnter) || isnan(tExit) ||
        isinf(tExit)) {
        return;
    }

    tEnter = max(tEnter, 0.0);
    const uint pixelBase = (pixel.y * dims.x + pixel.x) * 8u;
    atomicMax(proxyIntervalWords[pixelBase],
              0xffffffffu - floatBitsToUint(tEnter));
    atomicMax(proxyIntervalWords[pixelBase + 1u], floatBitsToUint(tExit));
    atomicOr(proxyIntervalWords[pixelBase + 2u + (bodyIndex >> 5u)],
             1u << (bodyIndex & 31u));
}
