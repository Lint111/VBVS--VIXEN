// ============================================================================
// SdfRecipes.glsl - analytic SDF recipe library for the Procedural body provider.
// 1:1 MIRROR of libraries/SVO/include/SdfRecipes.h. Keep formulas, operation
// order, and constants identical (parity is unit-tested on the CPU mirror and
// visually gated by the live app run).
// ============================================================================
#ifndef SDF_RECIPES_GLSL
#define SDF_RECIPES_GLSL

#define RECIPE_SPHERE           0u
#define RECIPE_DISPLACED_SPHERE 1u

// params = (radius, displaceAmp, displaceFreq)
float sdfDisplacement(vec3 d, float freq) {
    return sin(freq * d.x) * sin(freq * d.y) * sin(freq * d.z);
}

float evalSdf(uint recipeId, vec3 p, vec3 center, vec3 params) {
    vec3 d = p - center;
    float dist = length(d) - params.x;
    if (recipeId == RECIPE_DISPLACED_SPHERE) {
        dist += params.y * sdfDisplacement(d, params.z);
    }
    return dist;
}

vec3 sdfGradient(uint recipeId, vec3 p, vec3 center, vec3 params) {
    const float h = 1e-3;
    vec2 e = vec2(h, 0.0);
    float gx = evalSdf(recipeId, p + e.xyy, center, params) - evalSdf(recipeId, p - e.xyy, center, params);
    float gy = evalSdf(recipeId, p + e.yxy, center, params) - evalSdf(recipeId, p - e.yxy, center, params);
    float gz = evalSdf(recipeId, p + e.yyx, center, params) - evalSdf(recipeId, p - e.yyx, center, params);
    return normalize(vec3(gx, gy, gz));
}

// Sphere-trace within the bounding sphere. Writes hitNormal/hitT on success.
bool traceProceduralBody(uint recipeId, vec3 center, vec3 params, vec3 ro, vec3 rd,
                         out vec3 hitNormal, out float hitT) {
    hitNormal = vec3(0.0, 1.0, 0.0);
    hitT      = 0.0;

    float maxDisp = (recipeId == RECIPE_DISPLACED_SPHERE) ? params.y : 0.0;
    float boundR  = params.x + maxDisp + 0.01;

    vec3  oc   = ro - center;
    float b    = dot(oc, rd);
    float c    = dot(oc, oc) - boundR * boundR;
    float disc = b * b - c;
    if (disc < 0.0) return false;
    float sq    = sqrt(disc);
    float tNear = max(-b - sq, 0.0);
    float tFar  = -b + sq;
    if (tFar < 0.0) return false;

    float t = tNear;
    const int   MAX_STEPS = 128;
    const float EPS       = 1e-3;  // hit threshold (independent of gradient h in sdfGradient, which coincidentally equals EPS)
    // Step factor = 1/Lipschitz (see SdfRecipes.h): L = 1 + maxDisp*freq*sqrt(3).
    float stepScale = 1.0 / (1.0 + maxDisp * params.z * 1.7320508);
    for (int i = 0; i < MAX_STEPS; ++i) {
        vec3  p = ro + rd * t;
        float d = evalSdf(recipeId, p, center, params);
        if (d < EPS) {
            hitNormal = sdfGradient(recipeId, p, center, params);
            hitT      = t;
            return true;
        }
        t += d * stepScale;
        if (t > tFar) return false;
    }
    return false;
}

// ============================================================================
// Uber-shader recipe path (Lazy-Procedural-Delta-Baseline Inc0 M5 Task 11).
// ============================================================================
// evalRecipeField(uint recipeId, vec3 p) and getRecipeBoundSphere(...) are GENERATED
// functions, spliced into BodyInstanceRayMarch.comp at the VIXEN_UBER_RECIPE_SPLICE_MARKER
// (see that file) by UberShaderSplice.h — NOT defined in this vendored file. This sphere-
// march is the one hand-written piece both the splice and the two legacy analytic recipes
// (RECIPE_SPHERE/RECIPE_DISPLACED_SPHERE, still served by traceProceduralBody above) sit
// beside; it never runs unless at least one recipe was registered (the splice appends
// evalRecipeField only then — see BodyInstanceRayMarch.comp's marker comment), so its
// undeclared-identifier risk when unspliced is real and is exactly why callers must guard
// with the same registration count the splice itself is keyed on.
//
// Generic Lipschitz-bound-free sphere-march: unlike traceProceduralBody's stepScale (derived
// analytically from RECIPE_DISPLACED_SPHERE's known Lipschitz constant), an arbitrary spliced
// recipe program has no closed-form Lipschitz bound — CSG/modifier composition can amplify
// distance-field slope in ways this v1 doesn't attempt to bound. stepRelaxation (registered
// per-recipe, Task 10; ≤1 by construction) is the conservative substitute: each step advances
// by `d * relaxation` rather than the full field-distance `d`, trading march-step count for
// safety against overshoot. A relaxation of 1.0 is the fastest/least-safe setting; smaller
// values shrink the step and cost more iterations but overshoot less on a high-slope field.
//
// Guarded by VIXEN_UBER_RECIPE_SPLICED (defined by the splice itself, only when at least one
// recipe was registered — see BodyInstanceRayMarch.comp's marker comment): unlike main()'s own
// #ifdef-gated call site, this FUNCTION DEFINITION has no conditional call inside it to hide
// behind — it references evalRecipeField unconditionally in its body, so without this guard an
// unspliced build (zero registrations) would fail to compile on the undeclared identifier even
// though nothing ever calls this function in that case.
// stepsUsed (Task 12 evidence (c)): count of march iterations actually taken, written by the
// caller into instanceIterCount[instIdx] — the SAME per-instance debug buffer (binding 14)
// the ESVO path already populates (0u on an early-reject, a real iteration count on an actual
// traversal), so "instanceIterCount==0" means "rejected" uniformly across BOTH provider paths
// with no new instrumentation surface.
#ifdef VIXEN_UBER_RECIPE_SPLICED
bool traceUberRecipeBody(uint recipeId, vec3 boundCenter, float boundRadius, float relaxation,
                         vec3 ro, vec3 rd, out vec3 hitNormal, out float hitT, out uint stepsUsed) {
    hitNormal = vec3(0.0, 1.0, 0.0);
    hitT      = 0.0;
    stepsUsed = 0u;

    vec3  oc   = ro - boundCenter;
    float b    = dot(oc, rd);
    float c    = dot(oc, oc) - boundRadius * boundRadius;
    float disc = b * b - c;
    if (disc < 0.0) return false;
    float sq    = sqrt(disc);
    float tNear = max(-b - sq, 0.0);
    float tFar  = -b + sq;
    if (tFar < 0.0) return false;

    float t = tNear;
    const int   MAX_STEPS = 128;
    const float EPS       = 1e-3;
    for (int i = 0; i < MAX_STEPS; ++i) {
        stepsUsed = uint(i + 1);
        vec3  p = ro + rd * t;
        float d = evalRecipeField(recipeId, p);
        if (d < EPS) {
            // Central-difference gradient — mirrors sdfGradient's h/EPS coincidence above.
            const float h = 1e-3;
            vec2 e = vec2(h, 0.0);
            float gx = evalRecipeField(recipeId, p + e.xyy) - evalRecipeField(recipeId, p - e.xyy);
            float gy = evalRecipeField(recipeId, p + e.yxy) - evalRecipeField(recipeId, p - e.yxy);
            float gz = evalRecipeField(recipeId, p + e.yyx) - evalRecipeField(recipeId, p - e.yyx);
            hitNormal = normalize(vec3(gx, gy, gz));
            hitT      = t;
            return true;
        }
        t += d * relaxation;
        if (t > tFar) return false;
    }
    return false;
}
#endif // VIXEN_UBER_RECIPE_SPLICED

#endif // SDF_RECIPES_GLSL
