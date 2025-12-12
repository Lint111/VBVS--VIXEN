#version 460
#extension GL_EXT_ray_tracing : require

// ============================================================================
// VoxelRT.rmiss - Miss Shader (Phase K: Hardware Ray Tracing)
// ============================================================================
// Called when a ray misses all geometry.
// Returns sky/background color.
// ============================================================================

// Ray payload - color output
layout(location = 0) rayPayloadInEXT vec3 hitColor;

void main() {
    // Get ray direction for gradient
    vec3 rayDir = gl_WorldRayDirectionEXT;

    // Sky gradient based on ray Y direction
    // Blue at top, lighter blue at horizon
    float t = rayDir.y * 0.5 + 0.5;  // Remap from -1..1 to 0..1

    // Sky colors
    vec3 horizonColor = vec3(0.7, 0.8, 0.95);  // Light blue-white
    vec3 zenithColor = vec3(0.3, 0.5, 0.9);    // Deeper blue

    // Gradient
    hitColor = mix(horizonColor, zenithColor, t);

    // Add subtle ground gradient for below-horizon rays
    if (rayDir.y < 0.0) {
        vec3 groundColor = vec3(0.4, 0.35, 0.3);  // Brown-gray
        float groundT = -rayDir.y;
        hitColor = mix(horizonColor, groundColor, groundT);
    }
}
