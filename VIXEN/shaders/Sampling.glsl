// ============================================================================
// Sampling.glsl - Importance Sampling Utilities
// ============================================================================
// The program's default BRDF importance sampler: GGX Visible Normal
// Distribution Function (VNDF) sampling, spherical-cap formulation (Dupuy &
// Benyoub, "Sampling Visible GGX Normals with Spherical Caps", HPG 2023).
//
// This is a faster drop-in for the earlier Heitz 2018 VNDF method
// ("Sampling the GGX Distribution of Visible Normals", JCGT 2018): both
// sample the IDENTICAL visible-normal distribution (same output distribution
// by construction / bijection between the two papers' constructions), the
// spherical-cap variant just does it with fewer transcendental ops and no
// degenerate branch at normal incidence.
//
// Everything here operates in TANGENT SPACE (Z-up around the shading normal
// N) unless documented otherwise -- toTangentSpace/fromTangentSpace convert.
//
// Not yet consumed by any live shader (Sampled Lighting Inc0 M4). Inc3
// (ReSTIR DI initial candidates) and Inc5 (VNDF specular reuse) are the
// first real consumers.
// ============================================================================

#ifndef SAMPLING_GLSL
#define SAMPLING_GLSL

const float SAMPLING_PI = 3.14159265358979323846;
const float SAMPLING_TWO_PI = 6.28318530717958647693;

// ----------------------------------------------------------------------------
// Tangent-frame helpers
// ----------------------------------------------------------------------------

// Build an orthonormal basis (tangent, bitangent) around unit normal N.
// Branchless (Duff et al. 2017, "Building an Orthonormal Basis, Revisited"),
// stable at N == (0,0,-1) unlike the naive "pick an up vector" construction.
void buildOrthonormalBasis(vec3 N, out vec3 tangent, out vec3 bitangent) {
    float s = N.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (s + N.z);
    float b = N.x * N.y * a;
    tangent   = vec3(1.0 + s * N.x * N.x * a, s * b, -s * N.x);
    bitangent = vec3(b, s + N.y * N.y * a, -N.y);
}

// World-space direction -> tangent space around N (Z-up: tangent-space Z == N).
vec3 toTangentSpace(vec3 worldDir, vec3 N, vec3 tangent, vec3 bitangent) {
    return vec3(dot(worldDir, tangent), dot(worldDir, bitangent), dot(worldDir, N));
}

// Tangent-space direction (Z-up around N) -> world space.
vec3 fromTangentSpace(vec3 tangentDir, vec3 N, vec3 tangent, vec3 bitangent) {
    return tangent * tangentDir.x + bitangent * tangentDir.y + N * tangentDir.z;
}

// ----------------------------------------------------------------------------
// GGX VNDF sampling — spherical-cap formulation (Dupuy & Benyoub 2023)
// ----------------------------------------------------------------------------

// Sample a visible normal Ne from the GGX VNDF, given the view direction Ve
// in TANGENT SPACE (Z-up around the shading normal; Ve.z == NdotV, expected
// > 0 -- see the front-facing note below) and isotropic roughness alpha
// (alpha = roughness^2, matching Brdf.glsl's convention; alpha in (0,1]).
// u1, u2: independent uniform random numbers in [0,1).
//
// Returns the sampled half-vector Ne in the SAME tangent space as Ve.
//
// Algorithm (HPG 2023, listing 1 / Algorithm 1): warp Ve into the sphere
// configuration by the alpha-stretch, sample a point uniformly on the
// spherical cap visible from the warped view direction, then unwarp back
// to the ellipsoid (GGX) configuration.
vec3 sampleGGXVNDF(vec3 Ve, float alpha, float u1, float u2) {
    // 1. Warp the view direction to the hemisphere ("stretch" the ellipsoid
    //    configuration into a sphere by scaling X/Y by alpha).
    vec3 Vh = normalize(vec3(alpha * Ve.x, alpha * Ve.y, max(Ve.z, 0.0)));

    // 2. Sample a point uniformly on the spherical cap visible from Vh.
    //    This is the paper's simplified (branchless, no orthonormal basis
    //    needed) cap-sampling formulation, valid because the cap is always
    //    the same size (a hemisphere-cap independent of Vh's direction once
    //    parameterized this way).
    float phi = SAMPLING_TWO_PI * u1;
    float z = fma(1.0 - u2, 1.0 + Vh.z, -Vh.z); // (1-u2)*(1+Vh.z) - Vh.z
    float sinTheta = sqrt(clamp(1.0 - z * z, 0.0, 1.0));
    float x = sinTheta * cos(phi);
    float y = sinTheta * sin(phi);
    vec3 c = vec3(x, y, z);

    // 3. Compute the halfway direction in the hemisphere configuration.
    vec3 Nh = c + Vh;

    // 4. Unwarp back to the ellipsoid (GGX) configuration.
    vec3 Ne = normalize(vec3(alpha * Nh.x, alpha * Nh.y, max(Nh.z, 0.0)));
    return Ne;
}

// PDF of the sampled visible normal Ne (tangent space, same frame as Ve),
// i.e. the VNDF itself: D_visible(Ne) = G1(Ve) * max(0, dot(Ve,Ne)) * D(Ne) / Ve.z.
// Ve, Ne: tangent-space directions (Z-up around the shading normal).
// alpha: isotropic roughness (alpha = roughness^2), matching sampleGGXVNDF.
//
// Uses the same GGX D and (separable) Smith G1 as the rest of the sampler;
// callers doing full BRDF evaluation should still go through Brdf.glsl's
// ggxD/smithG2 for the *shading* term -- this pdf is for importance-sampling
// bookkeeping (e.g. MIS weights), not shading.
float vndfPdf(vec3 Ve, vec3 Ne, float alpha) {
    float NdotV = max(Ve.z, 1e-6);
    float NdotH = max(Ne.z, 0.0);

    float a2 = alpha * alpha;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    float D = a2 / max(SAMPLING_PI * d * d, 1e-8);

    // Separable Smith G1(Ve) via the tan(theta) form (matches the height-
    // correlated smithG2 in Brdf.glsl in the low/negligible-correlation
    // limit; used here only as the VNDF's own G1 factor per its definition).
    float cos2 = NdotV * NdotV;
    float tan2 = (1.0 - cos2) / max(cos2, 1e-8);
    float G1 = 2.0 / (1.0 + sqrt(1.0 + a2 * tan2));

    float VdotH = max(dot(Ve, Ne), 0.0);
    return G1 * VdotH * D / NdotV;
}

// ----------------------------------------------------------------------------
// Sample weight bound
// ----------------------------------------------------------------------------
// When sampleGGXVNDF is used as the importance sampler for the FULL BRDF
// (not just the D term), the resulting per-sample Monte Carlo weight is
// weight = F * G2 / G1
// where G1 is the *same* Smith G1(Ve) folded into the VNDF pdf above, and G2
// is the joint (height-correlated or separable -- caller's choice, but MUST
// be the Smith form paired with the same G1, i.e. "matched Smith G") visibility
// term evaluated at the same (Ve, Ne, L) triple. This weight is PROVABLY
// bounded in [0,1] for any matched Smith G1/G2 pair (Heitz 2018 sec 2,
// Dupuy & Benyoub 2023 sec 3): G2 <= G1 always holds for Smith-derived visibility
// (shadowing-masking can only reduce visible energy further), and F <= 1 for a
// physically valid Fresnel term -- so no BRDF-term fireflies are possible from
// this sampler alone (residual variance can still come from the radiance/light
// term it's multiplied against). This bound is verified numerically in
// test_vndf_mirror.cpp (RenderGraph tests) against 10^4 random samples; see
// that file for the property test.
//
// IMPORTANT: the bound requires G1/G2 to be a MATCHED Smith pair (both height-
// correlated, or both separable) -- mixing forms (e.g. this file's separable
// G1 with Brdf.glsl's height-correlated smithG2) breaks the guarantee. Inc3/
// Inc5 consumers must pick one Smith form and use it end-to-end for the
// weight computation.
// ----------------------------------------------------------------------------

#endif // SAMPLING_GLSL
