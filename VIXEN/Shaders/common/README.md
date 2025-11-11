# Common Shader Utilities

This directory contains reusable GLSL code that can be included in multiple shaders.

## Available Utilities

### RayTracingUtils.glsl

Comprehensive ray tracing utilities based on "Ray Tracing in One Weekend" by Peter Shirley et al.

**Features:**
- **Ray-Sphere Intersection**: Analytic intersection using quadratic equation
- **Ray-AABB Intersection**: Robust slab method for bounding boxes
- **Random Number Generation**: PCG hash-based RNG for GPU
- **Material Scattering**:
  - Lambertian (diffuse) - Cosine-weighted hemisphere sampling
  - Metallic (specular) - Reflection with configurable fuzziness
  - Dielectric (glass) - Snell's law refraction + Schlick approximation
- **Coordinate Frame Utilities**: Tangent space transformations
- **Geometric Utilities**: Reflection, refraction, near-zero checks

**Usage:**
```glsl
#include "common/RayTracingUtils.glsl"

void main() {
    // Create a ray
    Ray ray;
    ray.origin = vec3(0.0, 0.0, 0.0);
    ray.direction = normalize(vec3(1.0, 0.0, 0.0));

    // Create a sphere
    Sphere sphere;
    sphere.center = vec3(5.0, 0.0, 0.0);
    sphere.radius = 1.0;
    sphere.materialIndex = 0;

    // Test intersection
    HitRecord rec;
    if (hit_sphere(sphere, ray, 0.001, 1000.0, rec)) {
        // Handle hit
        vec3 color = scatter_lambertian(...);
    }
}
```

## Using Includes in VIXEN Shaders

The VIXEN shader system supports `#include` directives through the `ShaderPreprocessor` class.

### Setup

1. Add include path when creating `ShaderPreprocessor`:
```cpp
PreprocessorConfig config;
config.includePaths.push_back("VIXEN/Shaders/");
ShaderPreprocessor preprocessor(config);
```

2. In your shader, use relative paths:
```glsl
#include "common/RayTracingUtils.glsl"
```

### Include Path Resolution

The preprocessor searches for includes in this order:
1. **Relative to current file**: If the including file is in `VIXEN/Shaders/subdir/`, it looks in `subdir/common/`
2. **Include search paths**: Searches each path in `config.includePaths`

### Include Guards

Always use include guards to prevent multiple inclusion:
```glsl
#ifndef MY_UTILS_GLSL
#define MY_UTILS_GLSL

// ... your code ...

#endif // MY_UTILS_GLSL
```

### Circular Include Protection

The preprocessor automatically detects circular includes and reports an error.

### Debugging

Enable line directives for better error messages:
```cpp
config.enableLineDirectives = true;
```

This adds `#line` directives that map compiled shader lines back to source files.

## Example Shaders

- **PathTracer.comp**: Full path tracer demonstrating all features
- **VoxelRayMarch.comp**: Voxel ray marching (updated with bug fixes)

## Mathematical References

- **Ray-Sphere Intersection**: Quadratic equation solution
  - Discriminant: `b² - 4ac`
  - Roots: `t = (-b ± √discriminant) / 2a`

- **Lambertian Scattering**: Cosine-weighted hemisphere
  - PDF: `cos(θ) / π`
  - Implementation: `normal + random_unit_vector()`

- **Schlick Approximation**: Fresnel reflectance
  - `R(θ) = R₀ + (1 - R₀)(1 - cos θ)⁵`
  - `R₀ = ((n₁ - n₂)/(n₁ + n₂))²`

- **Snell's Law**: Refraction
  - `n₁ sin θ₁ = n₂ sin θ₂`
  - `η = n₁/n₂` (refraction ratio)

## Performance Notes

- **Random Number Generation**: PCG hash is fast but not cryptographically secure
- **Ray-Sphere Intersection**: O(1) analytic solution
- **Material Scattering**: Rejection sampling (100 iteration limit)

## Future Enhancements

- BVH acceleration structures
- Perlin noise for procedural textures
- Volume rendering (fog, smoke)
- Importance sampling for lights
- Russian roulette path termination
