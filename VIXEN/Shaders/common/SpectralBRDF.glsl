// SpectralBRDF.glsl
// Wavelength-dependent BRDF (Bidirectional Reflectance Distribution Function)
//
// Provides spectral evaluation of material reflectance properties:
// - Spectral Lambertian: wavelength-dependent albedo
// - Spectral Metal: complex refractive index (n, k) for realistic metal colors
// - Spectral Dielectric: Fresnel equations with wavelength-dependent IOR
// - Spectral conversion helpers for existing RGB materials
//
// References:
// - PBRT-v4: Chapter on spectral rendering and BRDFs
// - Gulbrandsen (2014): "Artist Friendly Metallic Fresnel"
// - Physically Based Rendering (Pharr et al.): BRDF chapter
// - Real-Time Rendering 4th Ed.: Fresnel reflectance of metals

#ifndef SPECTRAL_BRDF_GLSL
#define SPECTRAL_BRDF_GLSL

#include "SpectralUtils.glsl"

// ============================================================================
// SPECTRAL LAMBERTIAN (DIFFUSE)
// ============================================================================

// Lambertian BRDF with spectral albedo
// f_r(ω_i, ω_o) = albedo(λ) / π
//
// For spectral rendering, albedo varies with wavelength
// This creates realistic color appearance under different illumination
Spectrum lambertian_brdf_spectral(Spectrum albedo, vec3 wi, vec3 wo, vec3 normal) {
    // Lambertian is view-independent, just return albedo/π
    // (Factor of 1/π is often absorbed into Monte Carlo estimator)
    return albedo;
}

// Evaluate Lambertian BRDF at single wavelength
float lambertian_brdf_wavelength(float albedo, vec3 wi, vec3 wo, vec3 normal) {
    return albedo;
}

// Convert RGB albedo to spectral albedo (for legacy materials)
Spectrum lambertian_rgb_to_spectral(vec3 rgb_albedo) {
    return rgb_to_spectrum(rgb_albedo);
}

// ============================================================================
// SPECTRAL METAL (CONDUCTOR)
// ============================================================================

// Metals have wavelength-dependent reflectance due to complex refractive index:
// m = n + i*k
//   n = real part (refractive index)
//   k = imaginary part (extinction coefficient)
//
// This creates the characteristic colors of metals:
// - Gold: reflects red/yellow, absorbs blue (high k for blue)
// - Copper: reflects red, absorbs green/blue
// - Silver: reflects all wavelengths roughly equally (neutral)

// Complex refractive index for metal
struct ComplexIOR {
    float n;  // Real part (refractive index)
    float k;  // Imaginary part (extinction coefficient)
};

// Fresnel reflectance for conductor (metal)
// Uses complex IOR to calculate wavelength-dependent reflectance
float fresnel_conductor(float cos_theta_i, ComplexIOR ior) {
    float cos_theta_sq = cos_theta_i * cos_theta_i;
    float sin_theta_sq = 1.0 - cos_theta_sq;

    float n = ior.n;
    float k = ior.k;

    float n_sq = n * n;
    float k_sq = k * k;

    // Fresnel equations for conductors
    // R_parallel and R_perpendicular
    float temp1 = n_sq - k_sq - sin_theta_sq;
    float a_sq_plus_b_sq = sqrt(temp1 * temp1 + 4.0 * n_sq * k_sq);
    float a = sqrt(0.5 * (a_sq_plus_b_sq + temp1));

    float r_perp_sq = (a_sq_plus_b_sq + cos_theta_sq - 2.0 * a * cos_theta_i) /
                      (a_sq_plus_b_sq + cos_theta_sq + 2.0 * a * cos_theta_i);

    float r_par_sq = r_perp_sq *
                     (a_sq_plus_b_sq * cos_theta_sq + sin_theta_sq * sin_theta_sq - 2.0 * a * cos_theta_i * sin_theta_sq) /
                     (a_sq_plus_b_sq * cos_theta_sq + sin_theta_sq * sin_theta_sq + 2.0 * a * cos_theta_i * sin_theta_sq);

    // Average of parallel and perpendicular polarization
    return 0.5 * (r_perp_sq + r_par_sq);
}

// Metal BRDF at single wavelength
// For mirror reflection: BRDF includes Dirac delta, handled in sampling
// This returns the Fresnel term for perfect specular reflection
float metal_brdf_wavelength(vec3 wo, vec3 wi, vec3 normal, ComplexIOR ior) {
    float cos_theta = abs(dot(wi, normal));
    return fresnel_conductor(cos_theta, ior);
}

// ============================================================================
// METAL COMPLEX IOR DATA
// ============================================================================

// Get complex IOR for common metals at specific wavelength
// Data from measured spectral reflectance curves
ComplexIOR metal_ior_gold(float wavelength_nm) {
    ComplexIOR ior;

    // Gold: strong absorption in blue/green, reflection in red/yellow
    if (wavelength_nm < 450.0) {
        ior.n = 1.5;
        ior.k = 1.9;  // Strong absorption (blue)
    } else if (wavelength_nm < 550.0) {
        ior.n = 0.5;
        ior.k = 2.3;  // Strong absorption (green)
    } else {
        ior.n = 0.2;
        ior.k = 3.5;  // High reflection (red/yellow)
    }

    return ior;
}

ComplexIOR metal_ior_copper(float wavelength_nm) {
    ComplexIOR ior;

    // Copper: absorbs green/blue, reflects red
    if (wavelength_nm < 500.0) {
        ior.n = 1.0;
        ior.k = 2.5;  // Absorption (blue/green)
    } else if (wavelength_nm < 600.0) {
        ior.n = 0.5;
        ior.k = 2.8;  // Moderate absorption
    } else {
        ior.n = 0.2;
        ior.k = 3.6;  // High reflection (red)
    }

    return ior;
}

ComplexIOR metal_ior_silver(float wavelength_nm) {
    ComplexIOR ior;

    // Silver: fairly uniform reflectance across spectrum (neutral color)
    ior.n = 0.15;
    ior.k = 3.5;  // High reflection, low wavelength dependence

    // Slight variation
    if (wavelength_nm < 450.0) {
        ior.k = 3.2;  // Slightly less reflective in UV/blue
    }

    return ior;
}

ComplexIOR metal_ior_aluminum(float wavelength_nm) {
    ComplexIOR ior;

    // Aluminum: fairly neutral, slightly bluish tint
    ior.n = 1.5;
    ior.k = 6.5;  // Very high reflection

    if (wavelength_nm > 600.0) {
        ior.k = 6.0;  // Slightly less in red (creates blue tint)
    }

    return ior;
}

ComplexIOR metal_ior_iron(float wavelength_nm) {
    ComplexIOR ior;

    // Iron: neutral gray with slight warmth
    ior.n = 2.0;
    ior.k = 3.0;

    return ior;
}

// Get spectral reflectance for metal
// Returns spectrum of reflectance values [0, 1] for each wavelength
Spectrum metal_reflectance_spectrum(int metal_type) {
    Spectrum reflectance;

    for (int i = 0; i < SPECTRAL_SAMPLES; ++i) {
        float lambda = spectral_wavelength(i);
        ComplexIOR ior;

        if (metal_type == 0) {
            ior = metal_ior_gold(lambda);
        } else if (metal_type == 1) {
            ior = metal_ior_copper(lambda);
        } else if (metal_type == 2) {
            ior = metal_ior_silver(lambda);
        } else if (metal_type == 3) {
            ior = metal_ior_aluminum(lambda);
        } else if (metal_type == 4) {
            ior = metal_ior_iron(lambda);
        } else {
            // Default to neutral metal
            ior.n = 1.0;
            ior.k = 3.0;
        }

        // Reflectance at normal incidence
        reflectance.samples[i] = fresnel_conductor(1.0, ior);
    }

    return reflectance;
}

// ============================================================================
// SPECTRAL FRESNEL (DIELECTRIC)
// ============================================================================

// Fresnel reflectance for dielectric (non-metal)
// Standard Fresnel equations with wavelength-dependent IOR
float fresnel_dielectric(float cos_theta_i, float ior_i, float ior_t) {
    // Handle total internal reflection
    float sin_theta_i = sqrt(max(0.0, 1.0 - cos_theta_i * cos_theta_i));
    float sin_theta_t = (ior_i / ior_t) * sin_theta_i;

    if (sin_theta_t >= 1.0) {
        return 1.0;  // Total internal reflection
    }

    float cos_theta_t = sqrt(max(0.0, 1.0 - sin_theta_t * sin_theta_t));

    // Fresnel equations
    float r_parallel = ((ior_t * cos_theta_i) - (ior_i * cos_theta_t)) /
                       ((ior_t * cos_theta_i) + (ior_i * cos_theta_t));

    float r_perpendicular = ((ior_i * cos_theta_i) - (ior_t * cos_theta_t)) /
                            ((ior_i * cos_theta_i) + (ior_t * cos_theta_t));

    return 0.5 * (r_parallel * r_parallel + r_perpendicular * r_perpendicular);
}

// Evaluate spectral Fresnel for dielectric
// Uses wavelength-dependent IOR from SpectralUtils.glsl
Spectrum fresnel_dielectric_spectral(float cos_theta_i, float base_ior, float ior_i) {
    Spectrum fresnel;

    for (int i = 0; i < SPECTRAL_SAMPLES; ++i) {
        float lambda = spectral_wavelength(i);

        // Get wavelength-dependent IOR
        // Could use material-specific functions from SpectralUtils.glsl
        float ior_t = base_ior;  // Simplified (should use ior_bk7_glass(lambda), etc.)

        fresnel.samples[i] = fresnel_dielectric(cos_theta_i, ior_i, ior_t);
    }

    return fresnel;
}

// ============================================================================
// SPECTRAL MATERIAL CONVERSION
// ============================================================================

// Convert RGB metal albedo to spectral reflectance
// This is an approximation - real metals need measured IOR data
Spectrum metal_rgb_to_spectral(vec3 rgb_color) {
    // For colored metals, approximate spectral response
    // Gold/copper have wavelength-dependent reflectance
    // This is a heuristic, not physically accurate

    Spectrum spec;

    for (int i = 0; i < SPECTRAL_SAMPLES; ++i) {
        float lambda = spectral_wavelength(i);
        vec3 lambda_color = wavelength_to_rgb_approximate(lambda);

        // Weighted by similarity to target RGB
        float weight = dot(lambda_color, rgb_color);
        spec.samples[i] = weight;
    }

    // Normalize
    float max_val = 0.0;
    for (int i = 0; i < SPECTRAL_SAMPLES; ++i) {
        max_val = max(max_val, spec.samples[i]);
    }
    if (max_val > 0.0) {
        spec = spectrum_scale(spec, 1.0 / max_val);
    }

    return spec;
}

// ============================================================================
// SPECTRAL BRDF EVALUATION HELPERS
// ============================================================================

// Evaluate BRDF for all wavelengths given incident and outgoing directions
// material_type: 0=Lambertian, 1=Metal, 2=Dielectric
Spectrum evaluate_spectral_brdf(
    int material_type,
    int material_id,
    vec3 wo,  // Outgoing direction
    vec3 wi,  // Incident direction
    vec3 normal,
    Spectrum albedo_or_ior
) {
    Spectrum result;

    if (material_type == 0) {
        // Lambertian: return spectral albedo
        result = albedo_or_ior;
    } else if (material_type == 1) {
        // Metal: return spectral reflectance
        result = metal_reflectance_spectrum(material_id);
    } else if (material_type == 2) {
        // Dielectric: return 1 - Fresnel (transmitted portion)
        float cos_theta = abs(dot(wi, normal));
        for (int i = 0; i < SPECTRAL_SAMPLES; ++i) {
            float ior = albedo_or_ior.samples[i];  // Assume albedo_or_ior stores IOR
            float fresnel = fresnel_dielectric(cos_theta, 1.0, ior);
            result.samples[i] = 1.0 - fresnel;  // Transmission
        }
    } else {
        result = spectrum_constant(1.0);
    }

    return result;
}

// Sample BRDF importance sampling for spectral rendering
// Returns sampled direction and PDF
vec3 sample_spectral_brdf(
    int material_type,
    vec3 wo,
    vec3 normal,
    inout uint rng_state,
    out float pdf
) {
    extern vec3 random_unit_vector(inout uint seed);
    extern vec3 random_in_hemisphere(inout uint seed, vec3 normal);

    if (material_type == 0) {
        // Lambertian: cosine-weighted hemisphere
        vec3 dir = random_in_hemisphere(rng_state, normal);
        float cos_theta = max(dot(dir, normal), 0.0);
        pdf = cos_theta / 3.14159265359;
        return dir;
    } else if (material_type == 1) {
        // Metal: perfect specular reflection
        vec3 reflected = reflect(wo, normal);
        pdf = 1.0;  // Dirac delta (perfect specular)
        return reflected;
    } else if (material_type == 2) {
        // Dielectric: half reflection, half refraction (simplified)
        // Real implementation would use Fresnel probability
        extern float random_float(inout uint seed);
        if (random_float(rng_state) < 0.5) {
            vec3 reflected = reflect(wo, normal);
            pdf = 0.5;
            return reflected;
        } else {
            vec3 dir = random_in_hemisphere(rng_state, -normal);  // Refraction hemisphere
            pdf = 0.5;
            return dir;
        }
    }

    // Default: diffuse
    vec3 dir = random_in_hemisphere(rng_state, normal);
    pdf = 1.0 / (2.0 * 3.14159265359);
    return dir;
}

// ============================================================================
// DEBUG AND VISUALIZATION
// ============================================================================

// Visualize metal spectral response
vec3 debug_metal_spectrum(int metal_type) {
    Spectrum spec = metal_reflectance_spectrum(metal_type);
    return spectrum_to_rgb(spec);
}

// Compare RGB vs spectral metal appearance
// Returns: (RGB approximation, Spectral accurate)
void compare_metal_appearance(int metal_type, out vec3 rgb_approx, out vec3 spectral_accurate) {
    // Common RGB approximations
    if (metal_type == 0) {
        rgb_approx = vec3(1.0, 0.85, 0.35);  // Gold
    } else if (metal_type == 1) {
        rgb_approx = vec3(0.95, 0.65, 0.5);  // Copper
    } else if (metal_type == 2) {
        rgb_approx = vec3(0.97, 0.96, 0.95);  // Silver
    } else if (metal_type == 3) {
        rgb_approx = vec3(0.91, 0.92, 0.92);  // Aluminum
    } else if (metal_type == 4) {
        rgb_approx = vec3(0.56, 0.57, 0.58);  // Iron
    } else {
        rgb_approx = vec3(0.8);
    }

    // Spectral reflectance converted to RGB
    Spectrum spec = metal_reflectance_spectrum(metal_type);
    spectral_accurate = spectrum_to_rgb(spec);
}

#endif // SPECTRAL_BRDF_GLSL
