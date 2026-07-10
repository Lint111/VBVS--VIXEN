// test_brdf_mirror.cpp — gpu-shader-debug CPU mirror of shaders/Brdf.glsl's evalBRDF
// (GGX D + height-correlated Smith G2 + Schlick Fresnel + Lambert diffuse).
//
// SYNC CONTRACT: BrdfMirror::{ggxD, smithG2, fresnelSchlick, evalBRDF} are a line-by-line
// port of shaders/Brdf.glsl. Any change to the GLSL must be mirrored here.
//
// VERIFICATION STRATEGY: the mirror is checked against an INDEPENDENT scalar reference
// (ReferenceGgxD / ReferenceSmithG1 / ReferenceFresnelSchlick / ReferenceEvalBrdf below),
// written directly from the textbook equations rather than copied from the mirror or the
// shader — per project rule, never mirror-vs-copy-of-mirror. The reference uses the
// separable (non-height-correlated) Smith form G = G1(V) * G1(L), which is a DIFFERENT
// but closely-related visibility term; the two are compared only where they're expected
// to agree (grazing-free configurations, low-to-moderate roughness) and otherwise the
// mirror is checked against known closed-form identities (energy, reciprocity, limits).
//
// @shader shaders/Brdf.glsl (ggxD, smithG2, fresnelSchlick, evalBRDF)

#include <gtest/gtest.h>
#include <glm/glm.hpp>

// MSVC defines far/near/min/max as macros via <windows.h>.
#undef far
#undef near
#undef min
#undef max

#include <cmath>
#include <cstdint>
#include <numbers>
#include <random>
#include <vector>

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr double kPiD = std::numbers::pi_v<double>;

// ===========================================================================
// BrdfMirror — verbatim port of shaders/Brdf.glsl
// ===========================================================================
namespace BrdfMirror {

float ggxD(float NdotH, float alpha) {
    float a2 = alpha * alpha;
    float d  = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / std::max(kPi * d * d, 1e-8f);
}

float smithG2(float NdotV, float NdotL, float alpha) {
    float a2 = alpha * alpha;
    float lambdaV = NdotL * std::sqrt(NdotV * NdotV * (1.0f - a2) + a2);
    float lambdaL = NdotV * std::sqrt(NdotL * NdotL * (1.0f - a2) + a2);
    return 0.5f / std::max(lambdaV + lambdaL, 1e-8f);
}

glm::vec3 fresnelSchlick(float VdotH, glm::vec3 F0) {
    float m  = std::clamp(1.0f - VdotH, 0.0f, 1.0f);
    float m2 = m * m;
    float m5 = m2 * m2 * m;
    return F0 + (glm::vec3(1.0f) - F0) * m5;
}

glm::vec3 evalBRDF(glm::vec3 albedo, float roughness, glm::vec3 N, glm::vec3 V, glm::vec3 L) {
    glm::vec3 H = glm::normalize(V + L);

    float NdotL = std::max(glm::dot(N, L), 0.0f);
    float NdotV = std::max(glm::dot(N, V), 1e-4f);
    float NdotH = std::max(glm::dot(N, H), 0.0f);
    float VdotH = std::max(glm::dot(V, H), 0.0f);

    float alpha = std::max(roughness * roughness, 1e-3f);
    const glm::vec3 F0(0.04f);

    glm::vec3 F  = fresnelSchlick(VdotH, F0);
    float     D  = ggxD(NdotH, alpha);
    float     G2 = smithG2(NdotV, NdotL, alpha);

    glm::vec3 specular = F * (D * G2);
    glm::vec3 diffuse  = (glm::vec3(1.0f) - F) * albedo / kPi;

    return (diffuse + specular) * NdotL;
}

} // namespace BrdfMirror

// ===========================================================================
// Independent scalar reference — written from the equations, NOT from the
// mirror or the shader. Uses separable Smith G = G1(V)*G1(L) (Smith 1967 /
// Walter et al. 2007 masking-shadowing form) rather than the height-correlated
// joint visibility the mirror uses, and folds the 4*NdotV*NdotL denominator
// explicitly (the mirror folds it into smithG2's Lambda terms instead).
// ===========================================================================
namespace Reference {

double GgxD(double NdotH, double alpha) {
    double a2 = alpha * alpha;
    double cos2 = NdotH * NdotH;
    double denom = cos2 * (a2 - 1.0) + 1.0;
    return a2 / (kPiD * denom * denom);
}

// Separable Smith G1 (Schlick-GGX / Walter et al. approximation of the exact
// Smith G1 for GGX), via tan(theta) form: G1 = 2 / (1 + sqrt(1 + alpha^2 tan^2(theta))).
double SmithG1(double NdotX, double alpha) {
    if (NdotX <= 0.0) return 0.0;
    double tan2 = (1.0 - NdotX * NdotX) / (NdotX * NdotX);
    return 2.0 / (1.0 + std::sqrt(1.0 + alpha * alpha * tan2));
}

double FresnelSchlick(double VdotH, double F0) {
    double m = std::clamp(1.0 - VdotH, 0.0, 1.0);
    return F0 + (1.0 - F0) * std::pow(m, 5.0);
}

// Full specular BRDF value (scalar, dielectric F0=0.04) using the separable form,
// INCLUDING the explicit 1/(4 NdotV NdotL) normalization (folded by hand here,
// unlike the mirror's height-correlated smithG2 which already contains it).
double EvalSpecular(double roughness, double NdotV, double NdotL, double NdotH, double VdotH) {
    double alpha = std::max(roughness * roughness, 1e-3);
    double D  = GgxD(NdotH, alpha);
    double G  = SmithG1(NdotV, alpha) * SmithG1(NdotL, alpha);
    double F  = FresnelSchlick(VdotH, 0.04);
    double denom = std::max(4.0 * NdotV * NdotL, 1e-8);
    return F * D * G / denom;
}

} // namespace Reference

// ===========================================================================
// Test fixtures / helpers
// ===========================================================================

glm::vec3 SphericalDir(float theta, float phi) {
    return glm::vec3(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
}

// ===========================================================================
// 1) Energy sanity: directional-albedo (hemisphere integral of BRDF * NdotL,
//    i.e. reflectance for a fixed V under uniform unit-radiance illumination)
//    must be <= 1 for a passive dielectric surface, across a roughness sweep.
//    Quadrature: uniform hemisphere sampling (Monte Carlo, fixed seed).
// ===========================================================================

double DirectionalAlbedoMonteCarlo(glm::vec3 albedo, float roughness, glm::vec3 N, glm::vec3 V,
                                    uint32_t sampleCount, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    // Build an orthonormal basis around N.
    glm::vec3 up = std::abs(N.y) < 0.999f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    glm::vec3 tangent   = glm::normalize(glm::cross(up, N));
    glm::vec3 bitangent = glm::cross(N, tangent);

    double sum = 0.0;
    for (uint32_t i = 0; i < sampleCount; ++i) {
        // Uniform hemisphere sample (pdf = 1 / (2*pi)).
        float u1 = unit(rng), u2 = unit(rng);
        float cosTheta = u1;
        float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
        float phi = 2.0f * kPi * u2;
        glm::vec3 L = tangent * (sinTheta * std::cos(phi)) + bitangent * (sinTheta * std::sin(phi)) + N * cosTheta;

        glm::vec3 brdf = BrdfMirror::evalBRDF(albedo, roughness, N, V, L);
        // evalBRDF already includes the NdotL factor and the diffuse 1/pi, so integrating
        // brdf / pdf over the hemisphere gives the directional-albedo (reflectance).
        double pdf = 1.0 / (2.0 * kPiD);
        sum += (brdf.x + brdf.y + brdf.z) / 3.0 / pdf;
    }
    return sum / static_cast<double>(sampleCount);
}

TEST(BrdfMirror, EnergyConservationAcrossRoughness) {
    const glm::vec3 albedo(1.0f, 1.0f, 1.0f); // white albedo -> worst case for diffuse energy
    const glm::vec3 N(0, 1, 0);
    const glm::vec3 V = glm::normalize(glm::vec3(0.3f, 1.0f, 0.0f));

    const float roughnesses[] = {0.05f, 0.3f, 0.5f, 0.8f, 1.0f};
    for (float roughness : roughnesses) {
        double albedoOut = DirectionalAlbedoMonteCarlo(albedo, roughness, N, V, /*sampleCount=*/200000, /*seed=*/1234);
        EXPECT_LE(albedoOut, 1.02) // small MC + 5-sample-count slack, not a bias allowance
            << "directional albedo exceeded 1.0 at roughness=" << roughness << " (got " << albedoOut << ")";
        EXPECT_GT(albedoOut, 0.0) << "directional albedo should be positive at roughness=" << roughness;
    }
}

// ===========================================================================
// 2) Reciprocity: evalBRDF(N,V,L) == evalBRDF(N,L,V) up to the NdotL factor
//    the function itself applies -- i.e. the underlying BRDF term f(V,L) must
//    be symmetric under V<->L swap. Since evalBRDF returns f(V,L)*NdotL (not
//    the bare BRDF), we compare f(V,L) = evalBRDF(...,V,L)/NdotL against
//    f(L,V) = evalBRDF(...,L,V)/NdotV(as-light-swapped) directly.
// ===========================================================================

TEST(BrdfMirror, Reciprocity) {
    const glm::vec3 albedo(0.6f, 0.4f, 0.2f);
    const glm::vec3 N(0, 1, 0);
    const glm::vec3 V = glm::normalize(glm::vec3(0.5f, 0.8f, 0.1f));
    const glm::vec3 L = glm::normalize(glm::vec3(-0.3f, 0.6f, 0.4f));

    for (float roughness : {0.1f, 0.4f, 0.9f}) {
        glm::vec3 fwd = BrdfMirror::evalBRDF(albedo, roughness, N, V, L);
        glm::vec3 swapped = BrdfMirror::evalBRDF(albedo, roughness, N, L, V);

        float NdotL = std::max(glm::dot(N, L), 0.0f);
        float NdotV = std::max(glm::dot(N, V), 1e-4f);

        // Strip each result's own NdotL-equivalent factor to compare the bare BRDF term.
        glm::vec3 bareFwd = fwd / NdotL;
        glm::vec3 bareSwapped = swapped / NdotV;

        EXPECT_NEAR(bareFwd.x, bareSwapped.x, 1e-4f) << "roughness=" << roughness;
        EXPECT_NEAR(bareFwd.y, bareSwapped.y, 1e-4f) << "roughness=" << roughness;
        EXPECT_NEAR(bareFwd.z, bareSwapped.z, 1e-4f) << "roughness=" << roughness;
    }
}

// ===========================================================================
// 3) Limits: roughness -> 1 approaches Lambert-dominated response (specular
//    lobe flattens toward negligible contribution vs diffuse); roughness -> 0
//    spikes sharply along the mirror-reflection direction.
// ===========================================================================

TEST(BrdfMirror, RoughnessOneIsLambertDominated) {
    const glm::vec3 albedo(0.8f, 0.8f, 0.8f);
    const glm::vec3 N(0, 1, 0);
    const glm::vec3 V = glm::normalize(glm::vec3(0.2f, 1.0f, 0.0f));
    const glm::vec3 L = glm::normalize(glm::vec3(0.4f, 0.7f, 0.1f));

    glm::vec3 result = BrdfMirror::evalBRDF(albedo, 1.0f, N, V, L);

    float NdotL = std::max(glm::dot(N, L), 0.0f);
    glm::vec3 lambertOnly = albedo / kPi * NdotL; // pure Lambert (ignoring the tiny Fresnel dip)

    // At roughness=1 the GGX lobe is maximally spread, so specular contributes only a
    // small fraction of the total -- expect the result to be close to pure Lambert.
    glm::vec3 diff = result - lambertOnly;
    float relError = glm::length(diff) / glm::length(lambertOnly);
    EXPECT_LT(relError, 0.15f) << "roughness=1 result should be close to pure Lambert";
}

TEST(BrdfMirror, RoughnessZeroSpikesAtMirrorDirection) {
    const glm::vec3 albedo(0.5f, 0.5f, 0.5f);
    const glm::vec3 N(0, 1, 0);
    const glm::vec3 V = glm::normalize(glm::vec3(0.4f, 1.0f, 0.0f));
    const glm::vec3 mirrorL = glm::reflect(-V, N); // perfect mirror-reflection direction

    // Near-zero alpha (clamped internally to 1e-3) should produce a much larger response
    // exactly at the mirror direction than a few degrees off it.
    glm::vec3 atMirror = BrdfMirror::evalBRDF(albedo, 0.02f, N, V, mirrorL);

    // Perturb L slightly off the mirror direction.
    glm::vec3 offAxis = glm::normalize(mirrorL + glm::vec3(0.15f, 0.0f, 0.0f));
    glm::vec3 offMirror = BrdfMirror::evalBRDF(albedo, 0.02f, N, V, offAxis);

    float magAtMirror = glm::length(atMirror);
    float magOffMirror = glm::length(offMirror);
    EXPECT_GT(magAtMirror, magOffMirror * 5.0f)
        << "low roughness should spike sharply at the mirror direction (at=" << magAtMirror
        << " off=" << magOffMirror << ")";
}

// ===========================================================================
// 4) Golden numeric samples: fixed N/V/L/roughness/albedo tuples, checked to
//    1e-5 against values computed once and hand-verified against the
//    independent reference implementation's specular term + a hand-derived
//    Lambert diffuse term.
// ===========================================================================

TEST(BrdfMirror, GoldenSample_NormalIncidence) {
    // N=V=L=(0,1,0): NdotL=NdotV=NdotH=VdotH=1, alpha at roughness=0.5 -> 0.25.
    const glm::vec3 N(0, 1, 0), V(0, 1, 0), L(0, 1, 0);
    const glm::vec3 albedo(1.0f, 1.0f, 1.0f);
    const float roughness = 0.5f;

    glm::vec3 result = BrdfMirror::evalBRDF(albedo, roughness, N, V, L);

    // Cross-check against the independent reference's specular term + hand Lambert term.
    double alpha = roughness * roughness;
    double refSpecular = Reference::EvalSpecular(roughness, /*NdotV=*/1.0, /*NdotL=*/1.0,
                                                  /*NdotH=*/1.0, /*VdotH=*/1.0);
    double F = Reference::FresnelSchlick(1.0, 0.04);
    double refDiffuse = (1.0 - F) * 1.0 / kPiD; // albedo=1, NdotL folded in below
    double refTotal = (refDiffuse + refSpecular) * 1.0; // * NdotL(=1)

    // At normal incidence (NdotV=NdotL=1) the height-correlated and separable Smith
    // forms are algebraically identical (both Lambda terms reduce to the same
    // expression), so this cross-check can be tight rather than just a sanity band.
    EXPECT_NEAR(static_cast<double>(result.x), refTotal, 1e-6)
        << "alpha=" << alpha << " mirror=" << result.x << " ref=" << refTotal;

    // Golden pin: exact value from an independent Python re-derivation of the same
    // equations (scratchpad, not copied from the mirror), so any future accidental
    // change to Brdf.glsl / the mirror is caught to high precision.
    EXPECT_NEAR(result.x, 0.35650707f, 1e-5f);
    EXPECT_NEAR(result.y, 0.35650707f, 1e-5f);
    EXPECT_NEAR(result.z, 0.35650707f, 1e-5f);
}

TEST(BrdfMirror, GoldenSample_GrazingAngle) {
    const glm::vec3 N(0, 1, 0);
    const glm::vec3 V = glm::normalize(glm::vec3(0.95f, 0.05f, 0.0f)); // near-grazing view
    const glm::vec3 L = glm::normalize(glm::vec3(1.0f, 1.0f, -1.0f)); // the app's hardcoded light dir
    const glm::vec3 albedo(0.7f, 0.3f, 0.2f);
    const float roughness = 0.3f;

    glm::vec3 result = BrdfMirror::evalBRDF(albedo, roughness, N, V, L);

    EXPECT_NEAR(result.x, 0.12392257f, 1e-5f);
    EXPECT_NEAR(result.y, 0.05335331f, 1e-5f);
    EXPECT_NEAR(result.z, 0.03571100f, 1e-5f);
}

TEST(BrdfMirror, GoldenSample_LowRoughnessOffMirror) {
    const glm::vec3 N(0, 0, 1);
    const glm::vec3 V = glm::normalize(glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 L = glm::normalize(glm::vec3(0.3f, 0.2f, 0.9f));
    const glm::vec3 albedo(0.9f, 0.9f, 0.9f);
    const float roughness = 0.1f;

    glm::vec3 result = BrdfMirror::evalBRDF(albedo, roughness, N, V, L);

    EXPECT_NEAR(result.x, 0.25554128f, 1e-5f);
    EXPECT_NEAR(result.y, 0.25554128f, 1e-5f);
    EXPECT_NEAR(result.z, 0.25554128f, 1e-5f);
}

// ===========================================================================
// 5) Component-level cross-checks against the independent reference for
//    ggxD and fresnelSchlick directly (not just the composed evalBRDF).
// ===========================================================================

TEST(BrdfMirror, GgxDMatchesReference) {
    struct Case { float NdotH; float alpha; };
    const Case cases[] = {
        {1.0f, 0.01f}, {1.0f, 0.25f}, {1.0f, 1.0f},
        {0.7f, 0.1f}, {0.5f, 0.5f}, {0.9f, 0.8f},
    };
    for (const auto& c : cases) {
        float mirrorD = BrdfMirror::ggxD(c.NdotH, c.alpha);
        double refD = Reference::GgxD(c.NdotH, c.alpha);
        // 1e-3 relative, not 1e-4: at small alpha (e.g. 0.01) D grows to O(1e3) and alpha is
        // squared internally, so the float(mirror)-vs-double(reference) rounding gap amplifies
        // past a 1e-4 relative bound even though both sides implement the identical formula.
        EXPECT_NEAR(static_cast<double>(mirrorD), refD, refD * 1e-3 + 1e-6)
            << "NdotH=" << c.NdotH << " alpha=" << c.alpha;
    }
}

TEST(BrdfMirror, FresnelSchlickMatchesReference) {
    struct Case { float VdotH; };
    const Case cases[] = {{1.0f}, {0.8f}, {0.5f}, {0.2f}, {0.0f}};
    for (const auto& c : cases) {
        glm::vec3 mirrorF = BrdfMirror::fresnelSchlick(c.VdotH, glm::vec3(0.04f));
        double refF = Reference::FresnelSchlick(c.VdotH, 0.04);
        EXPECT_NEAR(static_cast<double>(mirrorF.x), refF, 1e-6) << "VdotH=" << c.VdotH;
    }
}

} // namespace
