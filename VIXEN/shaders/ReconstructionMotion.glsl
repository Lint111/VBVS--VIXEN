#ifndef RECONSTRUCTION_MOTION_GLSL
#define RECONSTRUCTION_MOTION_GLSL

// Engine convention: previous UV minus current UV, using unjittered cameras.
// Positive X/Y point right/down. The provider converts units/axes/direction.
// Valid means geometric correspondence only: the provider must still reject
// disocclusions against previous depth. No previous visibility is fabricated.
void writeReconstructionMotion(ivec2 pixel, bool hit, WorldHit surface, vec3 world) {
    vec2 motion = vec2(0.0);
    float valid = 0.0;
    if (hit && surface.instIdx < bodyInstanceTransforms.length()) {
        BodyInstanceTransform instance = bodyInstanceTransforms[surface.instIdx];
        vec3 local = instanceTransformPoint(instance.worldToLocalRows, world);
        vec3 previousWorld = instance.motion.valid != 0u
            ? (instance.motion.previousLocalToWorld * vec4(local, 1.0)).xyz : world;
        vec4 previousClip = prevCameraConfig.prevViewProj * vec4(previousWorld, 1.0);
        vec3 displacement = world - pc.cameraPos;
        float z = dot(displacement, pc.cameraDir);
        float spread = tan(radians(pc.fov * 0.5));
        if (z > 0.0 && previousClip.w > 0.0) {
            vec2 currentUV = vec2(dot(displacement, pc.cameraRight) / (z * spread * pc.aspect),
                                 -dot(displacement, pc.cameraUp) / (z * spread)) * 0.5 + 0.5;
            vec2 previousUV = previousClip.xy / previousClip.w * 0.5 + 0.5;
            vec2 candidate = previousUV - currentUV;
            if (!any(isnan(candidate)) && !any(isinf(candidate))) {
                motion = candidate;
                valid = instance.motion.valid != 0u &&
                    all(greaterThanEqual(previousUV, vec2(0.0))) &&
                    all(lessThan(previousUV, vec2(1.0))) ? 1.0 : 0.0;
            }
        }
    }
    // Sky is composed after reconstruction at native resolution: zero/invalid
    // here, including on first use, deletion/reuse, deformation and bad projection.
    imageStore(motionVectorImage, pixel, vec4(motion, 0.0, 0.0));
    imageStore(motionValidityImage, pixel, vec4(valid));
}
#endif
