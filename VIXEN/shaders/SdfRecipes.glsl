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

#endif // SDF_RECIPES_GLSL
