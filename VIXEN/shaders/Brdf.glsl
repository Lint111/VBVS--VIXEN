// ============================================================================
// Brdf.glsl - GGX + Smith + Schlick Microfacet BRDF
// ============================================================================
// Physically-based BRDF: Lambert diffuse + GGX specular (Trowbridge-Reitz
// normal distribution, height-correlated Smith visibility, Schlick Fresnel).
// Replaces the Blinn-Phong specular previously in Lighting.glsl.
//
// Dielectric-only for now: F0 is the fixed 0.04 dielectric reflectance.
// No metalness channel exists in the material contract yet (Stored-SDF Inc3
// SoA channel pool carries color + roughness only) -- when one lands,
// F0 should become mix(vec3(0.04), albedo, metalness) here.
// ============================================================================

#ifndef BRDF_GLSL
#define BRDF_GLSL

const float BRDF_PI = 3.14159265358979323846;

// GGX / Trowbridge-Reitz normal distribution function.
// NdotH: clamped dot(N,H) in [0,1]. alpha: roughness^2, alpha in (0,1].
float ggxD(float NdotH, float alpha) {
    float a2 = alpha * alpha;
    float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(BRDF_PI * d * d, 1e-8);
}

// Height-correlated Smith joint visibility term (G2 folded with the 1/(4 NdotV NdotL)
// denominator, as in Heitz 2014's V = G2 / (4 NdotV NdotL) formulation).
// NdotV, NdotL: clamped dot products in [0,1]. alpha: roughness^2.
float smithG2(float NdotV, float NdotL, float alpha) {
    float a2 = alpha * alpha;
    float lambdaV = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    float lambdaL = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
    return 0.5 / max(lambdaV + lambdaL, 1e-8);
}

// Schlick's Fresnel approximation.
// VdotH: clamped dot(V,H) in [0,1]. F0: normal-incidence reflectance.
vec3 fresnelSchlick(float VdotH, vec3 F0) {
    float m = clamp(1.0 - VdotH, 0.0, 1.0);
    float m2 = m * m;
    float m5 = m2 * m2 * m;
    return F0 + (vec3(1.0) - F0) * m5;
}

// Evaluate the full BRDF (Lambert diffuse + GGX specular) for one light direction.
// albedo: base surface color. roughness: perceptual roughness in [0,1] (alpha = roughness^2).
// N: surface normal. V: direction toward the viewer. L: direction toward the light.
// All directions normalized; N, V, L on the same side (NdotL/NdotV > 0 expected by caller).
vec3 evalBRDF(vec3 albedo, float roughness, vec3 N, vec3 V, vec3 L) {
    vec3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    float alpha = max(roughness * roughness, 1e-3);
    const vec3 F0 = vec3(0.04);

    vec3 F  = fresnelSchlick(VdotH, F0);
    float D = ggxD(NdotH, alpha);
    float G2 = smithG2(NdotV, NdotL, alpha);

    vec3 specular = F * (D * G2);

    // Energy-conserving diffuse: what isn't reflected specularly is available to the
    // Lambert term (dielectric, so (1-F) applies uniformly across channels here).
    vec3 diffuse = (vec3(1.0) - F) * albedo / BRDF_PI;

    return (diffuse + specular) * NdotL;
}

#endif // BRDF_GLSL
