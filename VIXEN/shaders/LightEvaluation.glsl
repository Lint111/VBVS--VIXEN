// Shared direction and range falloff for the existing LightingConfig light set.
#ifndef VIXEN_LIGHT_EVALUATION_GLSL
#define VIXEN_LIGHT_EVALUATION_GLSL

#include "Generated/LightingConfig.glsl"

vec3 lightDirectionAt(Light light, vec3 worldPos, out float distanceToLight) {
    if (light.kind == 1u) {
        vec3 toLight = light.direction_or_position - worldPos;
        distanceToLight = length(toLight);
        return distanceToLight > 1e-6 ? toLight / distanceToLight : vec3(0.0);
    }

    distanceToLight = 0.0;
    return normalize(light.direction_or_position);
}

float lightRangeAttenuation(Light light, float distanceToLight) {
    if (light.kind != 1u) return 1.0;
    if (light.range <= 0.0 || distanceToLight >= light.range) return 0.0;
    float fade = 1.0 - distanceToLight / light.range;
    return fade * fade;
}

#endif
