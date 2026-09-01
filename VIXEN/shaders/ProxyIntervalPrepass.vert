#version 460
// B2 vertex-pulled proxy boxes. A single instanced draw enumerates the Cartesian
// product of the front-to-back body array and the exact live proxy range.

struct ShellProxyAabb {
    vec3 minLocal;
    uint brickId;
    vec3 maxLocal;
    uint octreeIndex;
};
layout(std430, binding = 0) readonly buffer ProxyAabbBuffer {
    ShellProxyAabb proxyAabbs[];
};

struct BodyInstance {
    vec3  worldPos;
    float renderScale;
    vec3  color;
    uint  octreeIndex;
    uint  providerKind;
    uint  recipeId;
    float recipeParams[6];
};
layout(std430, binding = 1) readonly buffer BodyInstanceBuffer {
    BodyInstance bodyInstances[];
};

#include "Generated/OctreeConfig.glsl"
layout(std430, binding = 2) readonly buffer OctreeConfigsSSBO {
    OctreeConfig configs[];
};

layout(push_constant) uniform ProxyRasterPushConstants {
    mat4 viewProj;
    vec4 cameraPosFov;
    vec4 cameraDirAspect;
    vec4 cameraUpWidth;
    vec4 cameraRightHeight;
} pc;

layout(location = 0) flat out vec3 proxyWorldMin;
layout(location = 1) flat out vec3 proxyWorldMax;
layout(location = 2) flat out uint bodyIndexOut;

const uint kCubeIndices[36] = uint[36](
    0u, 1u, 3u, 0u, 3u, 2u,
    4u, 6u, 7u, 4u, 7u, 5u,
    0u, 4u, 5u, 0u, 5u, 1u,
    2u, 3u, 7u, 2u, 7u, 6u,
    0u, 2u, 6u, 0u, 6u, 4u,
    1u, 5u, 7u, 1u, 7u, 3u);

void main() {
    const uint proxyCount = uint(pc.cameraUpWidth.w + 0.5);
    const uint linearIndex = uint(gl_InstanceIndex);
    const uint bodyIndex = linearIndex / proxyCount;
    const uint proxyIndex = linearIndex - bodyIndex * proxyCount;
    const ShellProxyAabb proxy = proxyAabbs[proxyIndex];
    const BodyInstance instance = bodyInstances[bodyIndex];

    // Proxy records are concatenated by octree. Drawing the flat pool for each
    // body preserves front-to-back body order; mismatched records clip out.
    if (instance.providerKind != 0u || proxy.octreeIndex != instance.octreeIndex) {
        proxyWorldMin = vec3(0.0);
        proxyWorldMax = vec3(0.0);
        bodyIndexOut = bodyIndex;
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }

    const OctreeConfig config = configs[instance.octreeIndex];
    const vec3 localCenter = 0.5 * (proxy.minLocal + proxy.maxLocal);
    const vec3 localExtents = 0.5 * (proxy.maxLocal - proxy.minLocal);
    const mat3 linear = mat3(config.localToWorld);
    const vec3 baseCenter = (config.localToWorld * vec4(localCenter, 1.0)).xyz;
    const vec3 baseExtents = abs(linear[0]) * localExtents.x +
                             abs(linear[1]) * localExtents.y +
                             abs(linear[2]) * localExtents.z;
    const vec3 worldCenter = instance.worldPos + instance.renderScale * baseCenter;
    const vec3 worldExtents = abs(instance.renderScale) * baseExtents;
    proxyWorldMin = worldCenter - worldExtents;
    proxyWorldMax = worldCenter + worldExtents;
    bodyIndexOut = bodyIndex;

    const uint corner = kCubeIndices[uint(gl_VertexIndex)];
    const vec3 worldCorner = vec3(
        (corner & 1u) != 0u ? proxyWorldMax.x : proxyWorldMin.x,
        (corner & 2u) != 0u ? proxyWorldMax.y : proxyWorldMin.y,
        (corner & 4u) != 0u ? proxyWorldMax.z : proxyWorldMin.z);
    gl_Position = pc.viewProj * vec4(worldCorner, 1.0);
}
