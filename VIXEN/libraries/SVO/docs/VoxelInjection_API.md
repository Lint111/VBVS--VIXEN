# Voxel Injection API

## Overview

The Voxel Injection API allows you to **directly inject voxel data** into SVO structures, bypassing mesh triangulation. This is essential for:

- **Procedural Generation**: Noise, terrain, fractals
- **Volumetric Data**: Fog, clouds, medical scans
- **Signed Distance Fields**: CSG operations, smooth blending
- **Dynamic Content**: Real-time updates, scene composition
- **Performance**: Avoid mesh → voxel conversion overhead

## Three Input Methods

### 1. Sparse Voxels (Pre-computed)

Best for: Individual voxels, particle systems, sparse data

```cpp
SparseVoxelInput input;
input.worldMin = glm::vec3(-10, 0, -10);
input.worldMax = glm::vec3(10, 50, 10);
input.resolution = 256;

// Add individual voxels
VoxelData voxel;
voxel.position = glm::vec3(5, 10, 5);
voxel.color = glm::vec3(1, 0, 0);
voxel.normal = glm::vec3(0, 1, 0);
voxel.density = 1.0f;
input.voxels.push_back(voxel);

// Inject
VoxelInjector injector;
auto svo = injector.inject(input);
```

**Memory**: O(n) where n = number of solid voxels
**Speed**: Fast - direct insertion

### 2. Dense Grid (3D Array)

Best for: Volumetric data, medical scans, uniform grids

```cpp
DenseVoxelInput input;
input.worldMin = glm::vec3(0, 0, 0);
input.worldMax = glm::vec3(100, 100, 100);
input.resolution = glm::ivec3(64, 64, 64);

// Allocate grid
input.voxels.resize(64 * 64 * 64);

// Fill grid
for (int z = 0; z < 64; z++) {
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            size_t idx = input.getIndex(x, y, z);

            // Compute voxel data
            glm::vec3 pos = /* ... */;
            float density = computeDensity(pos);

            input.voxels[idx].position = pos;
            input.voxels[idx].density = density;
            input.voxels[idx].color = /* ... */;
        }
    }
}

VoxelInjector injector;
auto svo = injector.inject(input);
```

**Memory**: O(r³) where r = resolution
**Speed**: Moderate - grid traversal

### 3. Procedural Sampler (Callback-based)

Best for: Infinite terrain, noise functions, SDFs, real-time generation

```cpp
// Lambda-based sampler
auto sampler = std::make_unique<LambdaVoxelSampler>(
    // Sample function
    [](const glm::vec3& pos, VoxelData& data) -> bool {
        // Your procedural generation code here
        float noise = perlin3D(pos * 0.1f);

        if (noise > 0.0f) {  // Solid
            data.position = pos;
            data.color = glm::vec3(0.3f, 0.7f, 0.3f);
            data.normal = computeNormal(pos);
            data.density = 1.0f;
            return true;
        }
        return false;  // Empty
    },
    // Bounds function
    [](glm::vec3& min, glm::vec3& max) {
        min = glm::vec3(-1000, 0, -1000);  // Can be infinite
        max = glm::vec3(1000, 100, 1000);
    },
    // Optional: density estimator for LOD
    [](const glm::vec3& center, float size) -> float {
        // Return 0.0 to skip empty regions
        // Return 1.0 to force subdivision
        return 0.5f;  // Maybe occupied
    }
);

VoxelInjector injector;
auto svo = injector.inject(*sampler);
```

**Memory**: O(1) - samples on demand
**Speed**: Fast - only samples when needed
**Flexibility**: Can represent infinite worlds

## Built-in Samplers

### Noise Sampler

```cpp
Samplers::NoiseSampler::Params params;
params.frequency = 0.05f;
params.amplitude = 50.0f;
params.octaves = 4;
params.lacunarity = 2.0f;
params.persistence = 0.5f;
params.threshold = 0.0f;  // Below = solid

auto sampler = std::make_unique<Samplers::NoiseSampler>(params);
```

### SDF Sampler

```cpp
// Define SDF function
auto sdfFunc = [](const glm::vec3& p) -> float {
    return SDF::sphere(p, 10.0f);  // Negative = inside
};

auto sampler = std::make_unique<Samplers::SDFSampler>(
    sdfFunc,
    glm::vec3(-12, -12, -12),  // bounds
    glm::vec3(12, 12, 12)
);
```

### Heightmap Sampler

```cpp
Samplers::HeightmapSampler::Params params;
params.width = 256;
params.height = 256;
params.heights = loadHeightmap("terrain.png");
params.minHeight = 0.0f;
params.maxHeight = 100.0f;
params.horizontalScale = 1.0f;

auto sampler = std::make_unique<Samplers::HeightmapSampler>(params);
```

## Scene Merging

Dynamically add voxels to existing SVO:

```cpp
// Create base scene
auto baseSampler = createTerrainSampler();
VoxelInjector injector;
auto scene = injector.inject(*baseSampler);

// Later: add dynamic objects
SparseVoxelInput rocks = generateRocks();
injector.merge(*scene, rocks);  // Merges into existing SVO

// Or merge procedural content
auto treeSampler = createTreeSampler();
injector.merge(*scene, *treeSampler);
```

## Configuration Options

```cpp
InjectionConfig config;

// Octree depth
config.maxLevels = 16;  // 2^16 = 65536 resolution

// Quality
config.errorThreshold = 0.001f;  // Geometric error tolerance
config.enableContours = true;    // Use contours for tight bounds
config.enableCompression = true; // Compress attributes

// LOD control
config.enableLOD = true;
config.lodBias = 0.0f;  // -1.0 = finer, +1.0 = coarser

// Filtering
config.filterMode = InjectionConfig::FilterMode::Box;

// Memory limits
config.maxMemoryBytes = 4ULL * 1024 * 1024 * 1024;  // 4 GB
```

## Common Use Cases

### Procedural Terrain

```cpp
auto terrainSampler = std::make_unique<LambdaVoxelSampler>(
    [](const glm::vec3& pos, VoxelData& data) -> bool {
        float height = fbm(pos.x * 0.01f, pos.z * 0.01f) * 50.0f;

        if (pos.y < height) {
            data.position = pos;
            data.density = 1.0f;

            // Color based on height
            if (pos.y < 10.0f) {
                data.color = glm::vec3(0.8f, 0.7f, 0.5f);  // Sand
            } else if (pos.y < 30.0f) {
                data.color = glm::vec3(0.3f, 0.7f, 0.3f);  // Grass
            } else {
                data.color = glm::vec3(0.5f, 0.5f, 0.5f);  // Rock
            }

            data.normal = glm::vec3(0, 1, 0);  // Simplified
            return true;
        }
        return false;
    },
    /* bounds */
);
```

### CSG Operations

```cpp
auto csgSampler = std::make_unique<LambdaVoxelSampler>(
    [](const glm::vec3& p, VoxelData& data) -> bool {
        // Sphere - Box (subtraction)
        float sphere = SDF::sphere(p, 10.0f);
        float box = SDF::box(p, glm::vec3(6.0f));
        float dist = SDF::subtraction(box, sphere);

        if (dist < 0.0f) {
            data.position = p;
            data.density = 1.0f;
            data.color = glm::vec3(0.8f, 0.2f, 0.2f);
            data.normal = estimateNormal(p, dist);
            return true;
        }
        return false;
    },
    /* bounds */
);
```

### Volumetric Fog

```cpp
DenseVoxelInput fog;
fog.resolution = glm::ivec3(64, 64, 64);
fog.voxels.resize(64 * 64 * 64);

for (int z = 0; z < 64; z++) {
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            glm::vec3 pos = /* ... */;

            // Density falloff from center
            float dist = glm::length(pos - center);
            float density = std::max(0.0f, 1.0f - dist / radius);

            fog.voxels[fog.getIndex(x, y, z)].density = density;
            fog.voxels[fog.getIndex(x, y, z)].color = glm::vec3(0.9f);
        }
    }
}

// Configure for volumetric data
InjectionConfig config;
config.enableLOD = true;  // Important for smooth gradients
```

## Performance Tips

1. **Use sparse input for sparse data** - Don't allocate full grids if mostly empty
2. **Implement density estimator** - Skip empty regions early
3. **Enable LOD** - Coarser detail = fewer voxels
4. **Limit bounds** - Don't sample infinite regions unnecessarily
5. **Progress callbacks** - Monitor long builds

## Integration with Mesh Builder

You can mix mesh-based and voxel-based content:

```cpp
// Build base geometry from mesh
auto meshBuilder = SVOFactory::createBuilder(Type::LaineKarrasOctree);
auto scene = meshBuilder->build(loadMesh("building.obj"), config);

// Add procedural terrain underneath
auto terrainSampler = createTerrainSampler();
VoxelInjector injector;
injector.merge(*scene, *terrainSampler);

// Add volumetric effects
auto fogSampler = createFogSampler();
injector.merge(*scene, *fogSampler);
```

## Next Steps

See `examples/VoxelInjectionExamples.cpp` for complete working examples of:
- Noise terrain
- SDF primitives
- Sparse voxels
- Dense grids
- CSG operations
- Heightmaps
- Scene merging
