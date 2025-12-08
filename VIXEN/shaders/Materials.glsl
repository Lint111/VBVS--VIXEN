// ============================================================================
// Materials.glsl - Material ID to Color Mapping
// ============================================================================
// Single source of truth for material colors in GLSL shaders.
// Keep in sync with MaterialIdToColor() in SVORebuild.cpp
//
// Material ID ranges by scene type:
//   Cornell Box: 1-20
//   Noise/Tunnel: 30-40
//   Cityscape: 50-61
// ============================================================================

#ifndef MATERIALS_GLSL
#define MATERIALS_GLSL

// Get color for a material ID
// This is the authoritative GLSL definition - CPU code in SVORebuild.cpp should match
vec3 getMaterialColor(uint matID) {
    // Cornell Box materials (1-20)
    if (matID == 1u) return vec3(1.0, 0.0, 0.0);       // Red (left wall)
    if (matID == 2u) return vec3(0.0, 1.0, 0.0);       // Green (right wall)
    if (matID == 3u) return vec3(0.9, 0.9, 0.9);       // Light gray (white wall)
    if (matID == 4u) return vec3(1.0, 0.8, 0.0);       // Yellow/Gold
    if (matID == 5u) return vec3(0.95, 0.95, 0.95);    // White (ceiling)
    if (matID == 6u) return vec3(0.8, 0.8, 0.8);       // Medium gray (floor light)
    if (matID == 7u) return vec3(0.4, 0.4, 0.4);       // Darker gray (floor dark)
    if (matID == 10u) return vec3(0.8, 0.6, 0.2);      // Tan/wooden (left cube)
    if (matID == 11u) return vec3(0.6, 0.8, 0.9);      // Light blue (right cube)
    if (matID == 20u) return vec3(1.0, 0.98, 0.9);     // Warm white (ceiling light)

    // Noise/Tunnel materials (30-40) - earth/stone tones
    if (matID == 30u) return vec3(0.5, 0.45, 0.4);     // Stone
    if (matID == 31u) return vec3(0.6, 0.55, 0.5);     // Stalactite
    if (matID == 32u) return vec3(0.55, 0.5, 0.45);    // Stalagmite
    if (matID >= 33u && matID <= 39u) {
        // Stone variants - interpolate between two tones
        float t = float(matID - 33u) / 6.0;
        return mix(vec3(0.52, 0.48, 0.42), vec3(0.68, 0.6, 0.52), t);
    }
    if (matID == 40u) return vec3(0.8, 0.6, 0.2);      // Ore (golden)

    // Cityscape materials (50-61)
    if (matID == 50u) return vec3(0.2, 0.2, 0.22);     // Asphalt
    if (matID == 60u) return vec3(0.6, 0.58, 0.55);    // Concrete
    if (matID == 61u) return vec3(0.7, 0.85, 0.95);    // Glass

    // Default: HSV color wheel for unknown materials
    // More visible than dark grey fallback
    float hue = float(matID % 256u) / 256.0;
    float h = hue * 6.0;
    int i = int(h);
    float f = h - float(i);
    float p = 0.8 * 0.3;
    float q = 0.8 * (1.0 - 0.7 * f);
    float t = 0.8 * (1.0 - 0.7 * (1.0 - f));
    if (i == 0) return vec3(0.8, t, p);
    if (i == 1) return vec3(q, 0.8, p);
    if (i == 2) return vec3(p, 0.8, t);
    if (i == 3) return vec3(p, q, 0.8);
    if (i == 4) return vec3(t, p, 0.8);
    return vec3(0.8, p, q);
}

#endif // MATERIALS_GLSL
