#version 450
// SkyProjection.frag — Tiered ESVO Observer Addressing, Inc1 M3 (Task 6).
//
// Shapes the hard-square point sprite into a soft circular "point of light" falloff using
// gl_PointCoord (distance from the sprite's center, [0,1]^2), modulating alpha so the edges
// fade rather than hard-clip — much more convincing as a star/fleet-burn point than a flat
// square. Not mandatory for the milestone gate (per plan Task 6: "nice-to-have"), but cheap.

layout(location = 0) in float vMagnitude;
layout(location = 0) out vec4 outColour;

void main() {
    // gl_PointCoord is [0,1]^2 across the point sprite; recenter to [-1,1] and use distance
    // from center for a radial falloff.
    vec2 centered = gl_PointCoord * 2.0 - 1.0;
    float dist = length(centered);
    if (dist > 1.0) {
        discard;  // outside the circle — fully transparent, and cheap to skip entirely
    }

    // Soft falloff: bright core, fading smoothly to the edge (smoothstep avoids a harsh ring).
    float falloff = 1.0 - smoothstep(0.4, 1.0, dist);

    // Slight warm-white tint; brightness scales with magnitude (M2's ApparentMagnitude:
    // HIGHER = brighter, per TierMagnitude.h's doc comment — not inverted real-astronomy mag).
    vec3 colour = vec3(1.0, 0.97, 0.9) * clamp(vMagnitude, 0.0, 1.0);
    float alpha = falloff * clamp(vMagnitude, 0.0, 1.0);

    outColour = vec4(colour, alpha);
}
