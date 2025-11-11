// Material System - Modular GLSL Materials
//
// Physically-based materials for ray tracing and path tracing
// Based on "Ray Tracing in One Weekend" series by Peter Shirley

## Material Types

### 1. Lambertian (Lambertian.glsl)
Perfect diffuse reflection with cosine-weighted hemisphere sampling.

**Properties:**
- `vec3 albedo` - Diffuse reflectance color

**Behavior:**
- Scatters light uniformly based on surface normal
- Higher probability for rays near normal direction
- Lower probability for grazing angles
- Non-energy-conserving approximation

**Use Cases:**
- Matte surfaces (paper, unpolished wood, concrete)
- Rough materials without specular highlights
- Base layer for more complex materials

**Code Example:**
```glsl
#include "materials/Lambertian.glsl"

ScatterRecord scatter;
bool did_scatter = lambertian_scatter(
    ray_in,
    hit_record,
    vec3(0.8, 0.3, 0.3),  // Red albedo
    rng_state,
    scatter
);
```

---

### 2. Metal (Metal.glsl)
Mirror reflection with configurable surface roughness.

**Properties:**
- `vec3 albedo` - Metal tint color
- `float fuzz` - Roughness (0.0 = perfect mirror, 1.0 = very rough)

**Behavior:**
- Reflects rays according to angle of incidence
- Fuzziness adds random perturbation to reflection
- Rays scattered below surface are absorbed
- Simulates brushed or polished metal

**Presets:**
- `metal_gold()` - Gold with slight fuzz
- `metal_silver()` - Silver, very smooth
- `metal_copper()` - Copper with medium fuzz
- `metal_aluminum()` - Aluminum, rougher
- `metal_iron()` - Iron, quite rough

**Code Example:**
```glsl
#include "materials/Metal.glsl"

// Custom metal
ScatterRecord scatter;
bool did_scatter = metal_scatter(
    ray_in,
    hit_record,
    vec3(0.8, 0.6, 0.2),  // Gold-like color
    0.3,                   // Medium fuzz
    rng_state,
    scatter
);

// Or use preset
MetalMaterial gold = metal_gold();
```

---

### 3. Dielectric (Dielectric.glsl)
Glass and transparent materials with refraction.

**Properties:**
- `float ir` - Index of refraction
  - Air: 1.0
  - Water: 1.333
  - Glass: 1.5
  - Diamond: 2.42
- `vec3 tint` - Optional color tint
- `float absorption` - Beer's law absorption

**Behavior:**
- Refracts light using Snell's law
- Reflects at angles (Fresnel effect via Schlick approximation)
- Total internal reflection at steep angles
- Probabilistic reflect/refract based on angle

**Presets:**
- `dielectric_glass()` - Clear glass (IR 1.5)
- `dielectric_water()` - Water (IR 1.333)
- `dielectric_diamond()` - Diamond (IR 2.42)
- `dielectric_ice()` - Ice (IR 1.31)

**Code Example:**
```glsl
#include "materials/Dielectric.glsl"

// Clear glass
ScatterRecord scatter;
bool did_scatter = dielectric_scatter(
    ray_in,
    hit_record,
    1.5,  // Glass IR
    rng_state,
    scatter
);

// Colored glass with absorption
bool did_scatter = dielectric_scatter_absorbing(
    ray_in,
    hit_record,
    1.5,                      // Glass IR
    vec3(0.2, 0.8, 0.2),      // Green tint
    5.0,                      // Absorption distance
    rng_state,
    scatter
);
```

---

### 4. Emissive (Emissive.glsl)
Light-emitting materials for area lights.

**Properties:**
- `vec3 emit_color` - Color of emitted light
- `float intensity` - Emission strength multiplier

**Behavior:**
- Emits light without scattering
- Rays terminate on hit (absorbed)
- Used for light sources in path tracers

**Presets:**
- `emissive_white_light(intensity)` - Pure white
- `emissive_warm_light(intensity)` - Warm yellowish
- `emissive_cool_light(intensity)` - Cool bluish
- `emissive_fire(intensity)` - Orange-red flames
- `emissive_neon_blue(intensity)` - Neon glow
- `emissive_neon_pink(intensity)` - Pink neon

**Code Example:**
```glsl
#include "materials/Emissive.glsl"

// Create area light
EmissiveMaterial light = emissive_warm_light(5.0);

// In scatter function
ScatterRecord scatter;
emissive_scatter(ray_in, hit_record, light.emit_color, light.intensity, rng_state, scatter);

// Result:
// scatter.scattered = false (ray terminates)
// scatter.emitted = emit_color * intensity
```

---

### 5. Isotropic (Isotropic.glsl)
Volumetric scattering for fog, smoke, clouds.

**Properties:**
- `vec3 albedo` - Scattering color
- `float density` - Volume density (Beer's law)

**Behavior:**
- Scatters light uniformly in all directions
- Used for participating media (volumes)
- Can use phase functions for anisotropic scattering

**Phase Functions:**
- `isotropic_scatter()` - Uniform all directions (g=0)
- `isotropic_scatter_phase(g)` - Henyey-Greenstein
  - g > 0: Forward scattering
  - g < 0: Backward scattering
  - g = 0: Isotropic

**Presets:**
- `isotropic_fog(color, density)` - General fog
- `isotropic_smoke()` - Gray smoke
- `isotropic_white_cloud()` - White puffy clouds

**Code Example:**
```glsl
#include "materials/Isotropic.glsl"

// Uniform scattering
ScatterRecord scatter;
isotropic_scatter(ray_in, hit_record, vec3(0.8), rng_state, scatter);

// Forward-scattering fog
isotropic_scatter_phase(
    ray_in, hit_record,
    vec3(0.7, 0.7, 0.8),  // Blueish fog
    0.5,                   // Forward scatter (g > 0)
    rng_state, scatter
);

// Volume hit testing
float hit_distance;
if (volume_hit(ray, t_min, t_max, density, rng_state, hit_distance)) {
    // Scatter inside volume
}
```

---

## Material Common (MaterialCommon.glsl)

Base structures and utilities shared across all materials.

**Structures:**
- `Ray` - Ray with origin and direction
- `HitRecord` - Surface intersection data
- `ScatterRecord` - Material scatter result

**Material Type Constants:**
```glsl
const int MAT_LAMBERTIAN = 0;
const int MAT_METAL = 1;
const int MAT_DIELECTRIC = 2;
const int MAT_EMISSIVE = 3;
const int MAT_ISOTROPIC = 4;
```

**Utilities:**
- `set_face_normal()` - Ensures normal points against ray
- `near_zero()` - Check if vector is nearly zero

---

## Usage Patterns

### Single Material Shader
```glsl
#include "materials/MaterialCommon.glsl"
#include "materials/Lambertian.glsl"

void main() {
    // ... ray generation ...

    HitRecord rec;
    if (hit_something(ray, rec)) {
        ScatterRecord scatter;
        if (lambertian_scatter(ray, rec, albedo, rng, scatter)) {
            ray = scatter.scattered_ray;
            color *= scatter.attenuation;
        }
    }
}
```

### Multi-Material Shader
```glsl
#include "materials/MaterialCommon.glsl"
#include "materials/Lambertian.glsl"
#include "materials/Metal.glsl"
#include "materials/Dielectric.glsl"

void main() {
    // ... ray generation ...

    if (hit_something(ray, rec)) {
        ScatterRecord scatter;
        bool did_scatter = false;

        if (rec.materialIndex == MAT_LAMBERTIAN) {
            did_scatter = lambertian_scatter(ray, rec, albedo, rng, scatter);
        } else if (rec.materialIndex == MAT_METAL) {
            did_scatter = metal_scatter(ray, rec, albedo, fuzz, rng, scatter);
        } else if (rec.materialIndex == MAT_DIELECTRIC) {
            did_scatter = dielectric_scatter(ray, rec, ir, rng, scatter);
        }

        if (did_scatter) {
            ray = scatter.scattered_ray;
            color *= scatter.attenuation;
        } else {
            color = scatter.emitted;  // Emissive material
        }
    }
}
```

---

## Physics References

### Lambertian Scattering
- **PDF**: `cos(θ) / π`
- **Cosine-weighted hemisphere**: Higher probability near normal
- **Implementation**: `normal + random_unit_vector()`

### Metal Reflection
- **Law**: Angle of incidence = angle of reflection
- **Formula**: `r = v - 2(v·n)n`
- **Fuzz**: Random perturbation in unit sphere scaled by roughness

### Dielectric Refraction
- **Snell's Law**: `n₁ sin(θ₁) = n₂ sin(θ₂)`
- **Refraction Ratio**: `η = n₁ / n₂`
- **Total Internal Reflection**: When `η sin(θ₁) > 1`

### Fresnel (Schlick Approximation)
```
R₀ = ((n₁ - n₂) / (n₁ + n₂))²
R(θ) = R₀ + (1 - R₀)(1 - cos θ)⁵
```

### Henyey-Greenstein Phase Function
```
P(cos θ) = (1 - g²) / (4π(1 + g² - 2g cos θ)^(3/2))
```
- `g = 0`: Isotropic
- `g > 0`: Forward scattering
- `g < 0`: Backward scattering

---

## Dependencies

All material files require:
- `MaterialCommon.glsl` - Base structures
- RNG functions from `RayTracingUtils.glsl`:
  - `random_float(inout uint seed)`
  - `random_unit_vector(inout uint seed)`
  - `random_in_unit_sphere(inout uint seed)`
  - `random_in_hemisphere(inout uint seed, vec3 normal)`

---

## Performance Notes

- **Lambertian**: Fast, rejection sampling limited to 100 iterations
- **Metal**: Fast, single reflection calculation + optional fuzz
- **Dielectric**: Medium, sqrt and power operations in Schlick
- **Emissive**: Fastest, no scattering computation
- **Isotropic**: Fast uniform, medium for phase function

---

## Future Enhancements

- [ ] Oren-Nayar for rough diffuse
- [ ] Cook-Torrance microfacet BRDF
- [ ] Anisotropic materials
- [ ] Subsurface scattering (BSSRDF)
- [ ] Textured materials (albedo maps, normal maps)
- [ ] Clearcoat layers
- [ ] Thin-film interference
- [ ] Measured BRDFs (MERL database)
