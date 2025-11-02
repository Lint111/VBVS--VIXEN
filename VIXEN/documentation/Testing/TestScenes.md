# Test Scenes Design - Voxel Ray Tracing Research

**Phase**: M (Automated Testing Framework) Preparation
**Purpose**: Define standardized test scenes for benchmark reproducibility
**Status**: Design complete
**Date**: November 2, 2025

---

## Overview

This document specifies **3 test scenes** with varying voxel densities (10%, 50%, 90%) for benchmarking different ray tracing/marching pipelines. Each scene is designed to stress different aspects of voxel traversal algorithms.

### Design Goals

1. **Reproducibility**: Procedural generation ensures identical scenes across runs
2. **Density Control**: Precise voxel occupancy percentage for controlled testing
3. **Spatial Distribution**: Realistic patterns (not random noise)
4. **Traversal Variation**: Different scenes favor different optimization strategies
5. **Visual Clarity**: Recognizable structures for qualitative validation

---

## Test Configuration Matrix

Each scene will be tested across:

| Parameter | Values | Purpose |
|-----------|--------|---------|
| **Grid Resolution** | 32³, 64³, 128³, 256³, 512³ | Scalability testing |
| **Voxel Density** | 10%, 50%, 90% | Traversal complexity |
| **Algorithm Variant** | Baseline DDA, Empty Skip, BlockWalk | Optimization comparison |
| **Pipeline Type** | Compute, Fragment, HW RT, Hybrid | Architectural comparison |

**Total Tests**: 5 resolutions × 3 densities × 3 algorithms × 4 pipelines = **180 configurations**

---

## Scene 1: Cornell Box (10% Density - Sparse)

### Visual Description

Classic Cornell Box scene with:
- **Walls**: 1-voxel thick box boundaries (6 faces)
- **Floor**: Checkered pattern (alternating solid/empty)
- **Two Cubes**: One rotated 15°, one axis-aligned
- **Ceiling Light**: 16× 16 voxel emissive patch (center)

### Purpose

- **Sparse traversal**: Majority of rays pass through empty space
- **Empty space skipping** optimization advantage
- **Architectural structures**: Clean edges, flat surfaces
- **Light transport**: Emissive ceiling (future extension)

### Procedural Generation Algorithm

```cpp
struct CornellBoxScene {
    uint32_t gridResolution;  // e.g., 128
    float wallThickness = 1.0f;  // 1 voxel thick

    void Generate(VoxelGrid& grid) {
        grid.Clear();

        // 1. Walls (1-voxel thick box)
        GenerateWalls(grid);

        // 2. Floor checkered pattern
        GenerateCheckerFloor(grid);

        // 3. Left cube (axis-aligned)
        GenerateCube(grid,
            glm::vec3(gridResolution * 0.3, 0, gridResolution * 0.3),  // Position
            glm::vec3(gridResolution * 0.2),                           // Size
            glm::vec3(1.0, 0.0, 0.0));                                 // Red

        // 4. Right cube (rotated 15° on Y-axis)
        GenerateRotatedCube(grid,
            glm::vec3(gridResolution * 0.7, 0, gridResolution * 0.6),
            glm::vec3(gridResolution * 0.15),
            glm::radians(15.0f),  // Y rotation
            glm::vec3(0.0, 0.0, 1.0));  // Blue

        // 5. Ceiling light (emissive patch)
        GenerateCeilingLight(grid,
            glm::vec2(gridResolution * 0.5, gridResolution * 0.5),  // Center XZ
            16,  // Size (16×16 voxels)
            glm::vec3(1.0, 1.0, 0.8));  // Warm white
    }

private:
    void GenerateWalls(VoxelGrid& grid) {
        uint32_t res = gridResolution;

        // Left wall (red)
        for (uint32_t y = 0; y < res; ++y)
            for (uint32_t z = 0; z < res; ++z)
                grid.Set(0, y, z, glm::u8vec4(255, 0, 0, 255));

        // Right wall (green)
        for (uint32_t y = 0; y < res; ++y)
            for (uint32_t z = 0; z < res; ++z)
                grid.Set(res - 1, y, z, glm::u8vec4(0, 255, 0, 255));

        // Back wall (white)
        for (uint32_t x = 0; x < res; ++x)
            for (uint32_t y = 0; y < res; ++y)
                grid.Set(x, y, 0, glm::u8vec4(255, 255, 255, 255));

        // Floor (white)
        for (uint32_t x = 0; x < res; ++x)
            for (uint32_t z = 0; z < res; ++z)
                grid.Set(x, 0, z, glm::u8vec4(255, 255, 255, 255));

        // Ceiling (white)
        for (uint32_t x = 0; x < res; ++x)
            for (uint32_t z = 0; z < res; ++z)
                grid.Set(x, res - 1, z, glm::u8vec4(255, 255, 255, 255));
    }

    void GenerateCheckerFloor(VoxelGrid& grid) {
        uint32_t res = gridResolution;
        uint32_t checkerSize = res / 8;  // 8×8 checker grid

        for (uint32_t x = 0; x < res; ++x) {
            for (uint32_t z = 0; z < res; ++z) {
                bool isLight = ((x / checkerSize) + (z / checkerSize)) % 2 == 0;
                glm::u8vec4 color = isLight
                    ? glm::u8vec4(220, 220, 220, 255)  // Light gray
                    : glm::u8vec4(50, 50, 50, 255);    // Dark gray

                grid.Set(x, 0, z, color);
            }
        }
    }
};
```

### Expected Density

**Target**: 10% voxel occupancy

**Calculation** (for 128³ grid):
- Walls: 6 faces × 128² ≈ 98,304 voxels
- Cubes: 2 × (0.2×128)³ ≈ 40,960 voxels
- Light: 16² = 256 voxels
- **Total**: ~139,520 voxels
- **Occupancy**: 139,520 / (128³) ≈ **6.6%** (close to 10% with tuning)

---

## Scene 2: Cave System (50% Density - Medium)

### Visual Description

Procedural cave network with:
- **Perlin noise-based tunnels**: 3D noise threshold determines solid/empty
- **Multiple chambers**: Large open spaces connected by narrow passages
- **Stalactites/Stalagmites**: Vertical pillars from ceiling/floor
- **Ore veins**: Clusters of colored voxels (iron, gold, diamond)

### Purpose

- **Medium traversal**: Balanced solid/empty ratio
- **Coherent structures**: Noise creates natural-looking patterns
- **Varied ray lengths**: Some rays hit immediately, others traverse long tunnels
- **Spatial coherence testing**: Adjacent pixels often hit similar surfaces

### Procedural Generation Algorithm

```cpp
struct CaveScene {
    uint32_t gridResolution;
    float noiseScale = 4.0f;      // Perlin noise frequency
    float densityThreshold = 0.5f; // Threshold for solid vs empty

    void Generate(VoxelGrid& grid) {
        grid.Clear();

        // 1. Generate base cave structure with Perlin noise
        GenerateCaveTerrain(grid);

        // 2. Add stalactites and stalagmites
        GeneratePillars(grid);

        // 3. Add ore veins (iron, gold, diamond)
        GenerateOreVeins(grid);
    }

private:
    void GenerateCaveTerrain(VoxelGrid& grid) {
        PerlinNoise3D noise(42);  // Fixed seed for reproducibility
        uint32_t res = gridResolution;

        for (uint32_t x = 0; x < res; ++x) {
            for (uint32_t y = 0; y < res; ++y) {
                for (uint32_t z = 0; z < res; ++z) {
                    // 3D Perlin noise sample
                    float nx = float(x) / res * noiseScale;
                    float ny = float(y) / res * noiseScale;
                    float nz = float(z) / res * noiseScale;

                    float noiseValue = noise.Sample(nx, ny, nz);  // [-1, 1]

                    // Add vertical gradient (more solid at bottom)
                    float gradient = 1.0f - (float(y) / res);
                    noiseValue += gradient * 0.5f;

                    // Threshold determines solid vs empty
                    if (noiseValue > densityThreshold) {
                        // Stone material (gray with slight variation)
                        uint8_t gray = 100 + uint8_t(noiseValue * 50);
                        grid.Set(x, y, z, glm::u8vec4(gray, gray, gray, 255));
                    }
                }
            }
        }
    }

    void GeneratePillars(VoxelGrid& grid) {
        PerlinNoise2D noise(123);  // Different seed for pillar placement
        uint32_t res = gridResolution;

        for (uint32_t x = 0; x < res; x += 8) {
            for (uint32_t z = 0; z < res; z += 8) {
                float noiseVal = noise.Sample(float(x) / res * 2.0f, float(z) / res * 2.0f);

                if (noiseVal > 0.5f) {
                    // Stalactite (from ceiling)
                    uint32_t length = uint32_t((noiseVal - 0.5f) * 20);
                    for (uint32_t y = res - 1; y > res - 1 - length; --y) {
                        grid.Set(x, y, z, glm::u8vec4(150, 140, 130, 255));  // Limestone
                    }
                } else if (noiseVal < -0.5f) {
                    // Stalagmite (from floor)
                    uint32_t length = uint32_t((-0.5f - noiseVal) * 20);
                    for (uint32_t y = 0; y < length; ++y) {
                        grid.Set(x, y, z, glm::u8vec4(150, 140, 130, 255));
                    }
                }
            }
        }
    }

    void GenerateOreVeins(VoxelGrid& grid) {
        // Iron veins (orange-brown)
        GenerateOreCluster(grid, glm::vec3(255, 140, 70), 0.02f, 456);

        // Gold veins (yellow)
        GenerateOreCluster(grid, glm::vec3(255, 215, 0), 0.01f, 789);

        // Diamond veins (cyan)
        GenerateOreCluster(grid, glm::vec3(0, 255, 255), 0.005f, 101);
    }

    void GenerateOreCluster(VoxelGrid& grid, glm::vec3 color, float density, uint32_t seed) {
        PerlinNoise3D noise(seed);
        uint32_t res = gridResolution;

        for (uint32_t x = 0; x < res; ++x) {
            for (uint32_t y = 0; y < res; ++y) {
                for (uint32_t z = 0; z < res; ++z) {
                    if (!grid.IsEmpty(x, y, z)) {  // Only replace existing stone
                        float noiseVal = noise.Sample(x / 10.0f, y / 10.0f, z / 10.0f);
                        if (noiseVal > (1.0f - density)) {
                            grid.Set(x, y, z, glm::u8vec4(color, 255));
                        }
                    }
                }
            }
        }
    }
};
```

### Expected Density

**Target**: 50% voxel occupancy

**Tuning**: Adjust `densityThreshold` parameter to hit exactly 50%
- Lower threshold → More solid voxels
- Higher threshold → More empty space

**Validation**: Count solid voxels after generation, adjust threshold iteratively.

---

## Scene 3: Urban Grid (90% Density - Dense)

### Visual Description

Dense city grid with:
- **Buildings**: Rectangular voxel structures (varying heights)
- **Streets**: 2-voxel wide gaps between buildings (empty space)
- **Windows**: Periodic empty voxels on building faces
- **Rooftops**: Flat or stepped (antennas, HVAC units)

### Purpose

- **Dense traversal**: Most rays hit surfaces quickly
- **High voxel count**: Stresses memory bandwidth and cache
- **Regular patterns**: Grid-like structure (optimal for BlockWalk)
- **Minimal empty space**: Few opportunities for empty space skipping

### Procedural Generation Algorithm

```cpp
struct UrbanScene {
    uint32_t gridResolution;
    uint32_t buildingSpacing = 8;  // Voxels between buildings
    uint32_t streetWidth = 2;      // Voxels per street

    void Generate(VoxelGrid& grid) {
        grid.Clear();

        // 1. Generate ground plane
        GenerateGround(grid);

        // 2. Generate buildings in grid pattern
        GenerateBuildings(grid);

        // 3. Add windows to buildings
        GenerateWindows(grid);

        // 4. Add rooftop details
        GenerateRooftops(grid);
    }

private:
    void GenerateGround(VoxelGrid& grid) {
        uint32_t res = gridResolution;

        // Asphalt (dark gray)
        for (uint32_t x = 0; x < res; ++x) {
            for (uint32_t z = 0; z < res; ++z) {
                grid.Set(x, 0, z, glm::u8vec4(40, 40, 40, 255));
            }
        }
    }

    void GenerateBuildings(VoxelGrid& grid) {
        uint32_t res = gridResolution;
        uint32_t blockSize = buildingSpacing + streetWidth;

        std::mt19937 rng(42);  // Fixed seed
        std::uniform_int_distribution<uint32_t> heightDist(res / 4, res * 3 / 4);

        for (uint32_t bx = 0; bx < res; bx += blockSize) {
            for (uint32_t bz = 0; bz < res; bz += blockSize) {
                uint32_t height = heightDist(rng);

                // Building footprint
                uint32_t x0 = bx;
                uint32_t x1 = std::min(bx + buildingSpacing, res);
                uint32_t z0 = bz;
                uint32_t z1 = std::min(bz + buildingSpacing, res);

                // Material (concrete - light gray with variation)
                glm::u8vec3 baseColor(180, 180, 180);
                uint8_t variation = uint8_t((bx + bz) % 30);
                glm::u8vec3 color = baseColor + glm::u8vec3(variation);

                // Fill building volume
                for (uint32_t x = x0; x < x1; ++x) {
                    for (uint32_t y = 1; y <= height; ++y) {
                        for (uint32_t z = z0; z < z1; ++z) {
                            grid.Set(x, y, z, glm::u8vec4(color, 255));
                        }
                    }
                }
            }
        }
    }

    void GenerateWindows(VoxelGrid& grid) {
        uint32_t res = gridResolution;
        uint32_t blockSize = buildingSpacing + streetWidth;
        uint32_t windowSpacing = 3;  // Voxels between windows

        for (uint32_t bx = 0; bx < res; bx += blockSize) {
            for (uint32_t bz = 0; bz < res; bz += blockSize) {
                uint32_t x0 = bx;
                uint32_t x1 = std::min(bx + buildingSpacing, res);
                uint32_t z0 = bz;
                uint32_t z1 = std::min(bz + buildingSpacing, res);

                // Windows on all 4 faces
                for (uint32_t y = 3; y < res; y += windowSpacing) {
                    // X-aligned faces
                    for (uint32_t z = z0 + 1; z < z1; z += windowSpacing) {
                        grid.Set(x0, y, z, glm::u8vec4(100, 150, 255, 128));  // Glass (semi-transparent)
                        grid.Set(x1 - 1, y, z, glm::u8vec4(100, 150, 255, 128));
                    }

                    // Z-aligned faces
                    for (uint32_t x = x0 + 1; x < x1; x += windowSpacing) {
                        grid.Set(x, y, z0, glm::u8vec4(100, 150, 255, 128));
                        grid.Set(x, y, z1 - 1, glm::u8vec4(100, 150, 255, 128));
                    }
                }
            }
        }
    }

    void GenerateRooftops(VoxelGrid& grid) {
        uint32_t res = gridResolution;
        uint32_t blockSize = buildingSpacing + streetWidth;

        std::mt19937 rng(789);
        std::uniform_int_distribution<uint32_t> antennaDist(0, 1);

        for (uint32_t bx = 0; bx < res; bx += blockSize) {
            for (uint32_t bz = 0; bz < res; bz += blockSize) {
                uint32_t x = bx + buildingSpacing / 2;
                uint32_t z = bz + buildingSpacing / 2;

                // Find rooftop height
                uint32_t roofY = 0;
                for (uint32_t y = 1; y < res; ++y) {
                    if (!grid.IsEmpty(x, y, z)) {
                        roofY = y;
                    }
                }

                // 50% chance of antenna
                if (antennaDist(rng) == 1 && roofY > 0) {
                    uint32_t antennaHeight = roofY + res / 10;
                    for (uint32_t y = roofY + 1; y < antennaHeight; ++y) {
                        grid.Set(x, y, z, glm::u8vec4(50, 50, 50, 255));  // Metal
                    }
                }
            }
        }
    }
};
```

### Expected Density

**Target**: 90% voxel occupancy

**Calculation** (for 128³ grid):
- Ground: 128² = 16,384 voxels
- Buildings: ~85% of volume (streets = 15%)
- Windows: -5% (removed from buildings)
- **Total**: ~90% occupancy

---

## Voxel Grid Data Structure

### VoxelGrid Class (Phase H Implementation)

```cpp
class VoxelGrid {
public:
    VoxelGrid(uint32_t resolution)
        : resolution_(resolution)
        , data_(resolution * resolution * resolution, glm::u8vec4(0, 0, 0, 0))
    {}

    void Set(uint32_t x, uint32_t y, uint32_t z, glm::u8vec4 color) {
        if (x < resolution_ && y < resolution_ && z < resolution_) {
            data_[Index(x, y, z)] = color;
        }
    }

    glm::u8vec4 Get(uint32_t x, uint32_t y, uint32_t z) const {
        return (x < resolution_ && y < resolution_ && z < resolution_)
            ? data_[Index(x, y, z)]
            : glm::u8vec4(0, 0, 0, 0);
    }

    bool IsEmpty(uint32_t x, uint32_t y, uint32_t z) const {
        return Get(x, y, z).a == 0;
    }

    void Clear() {
        std::fill(data_.begin(), data_.end(), glm::u8vec4(0, 0, 0, 0));
    }

    float CalculateDensity() const {
        uint32_t solidCount = 0;
        for (const auto& voxel : data_) {
            if (voxel.a > 0) ++solidCount;
        }
        return float(solidCount) / data_.size();
    }

    // Upload to Vulkan 3D texture (Phase H)
    void UploadToGPU(VkDevice device, VkImage image, VkCommandBuffer cmd);

private:
    uint32_t resolution_;
    std::vector<glm::u8vec4> data_;

    uint32_t Index(uint32_t x, uint32_t y, uint32_t z) const {
        return x + y * resolution_ + z * resolution_ * resolution_;
    }
};
```

---

## Perlin Noise Implementation

### PerlinNoise3D (for Cave Scene)

```cpp
class PerlinNoise3D {
public:
    PerlinNoise3D(uint32_t seed) {
        // Initialize permutation table with seed
        std::mt19937 rng(seed);
        for (int i = 0; i < 256; ++i) p_[i] = i;
        std::shuffle(p_.begin(), p_.begin() + 256, rng);
        std::copy(p_.begin(), p_.begin() + 256, p_.begin() + 256);
    }

    float Sample(float x, float y, float z) const {
        // Perlin noise algorithm (Ken Perlin's improved version)
        // Returns value in [-1, 1]

        int X = int(floor(x)) & 255;
        int Y = int(floor(y)) & 255;
        int Z = int(floor(z)) & 255;

        x -= floor(x);
        y -= floor(y);
        z -= floor(z);

        float u = Fade(x);
        float v = Fade(y);
        float w = Fade(z);

        int A = p_[X] + Y;
        int AA = p_[A] + Z;
        int AB = p_[A + 1] + Z;
        int B = p_[X + 1] + Y;
        int BA = p_[B] + Z;
        int BB = p_[B + 1] + Z;

        return Lerp(w,
            Lerp(v,
                Lerp(u, Grad(p_[AA], x, y, z), Grad(p_[BA], x - 1, y, z)),
                Lerp(u, Grad(p_[AB], x, y - 1, z), Grad(p_[BB], x - 1, y - 1, z))),
            Lerp(v,
                Lerp(u, Grad(p_[AA + 1], x, y, z - 1), Grad(p_[BA + 1], x - 1, y, z - 1)),
                Lerp(u, Grad(p_[AB + 1], x, y - 1, z - 1), Grad(p_[BB + 1], x - 1, y - 1, z - 1))));
    }

private:
    std::array<int, 512> p_;

    float Fade(float t) const { return t * t * t * (t * (t * 6 - 15) + 10); }
    float Lerp(float t, float a, float b) const { return a + t * (b - a); }

    float Grad(int hash, float x, float y, float z) const {
        int h = hash & 15;
        float u = h < 8 ? x : y;
        float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
        return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
    }
};
```

---

## Test Scene Selection (Phase M)

### Command-Line Interface

```bash
# Run specific scene and configuration
./benchmark --scene cornell --resolution 128 --density 10 --algorithm dda --pipeline compute

# Run all configurations for one scene
./benchmark --scene cave --all-configs

# Run full test suite (180 configurations)
./benchmark --full-suite
```

### Scene Manifest (JSON)

```json
{
  "scenes": [
    {
      "name": "cornell",
      "description": "Cornell Box (10% density)",
      "target_density": 0.10,
      "generator": "CornellBoxScene",
      "parameters": {
        "wall_thickness": 1.0,
        "cube_count": 2
      }
    },
    {
      "name": "cave",
      "description": "Cave System (50% density)",
      "target_density": 0.50,
      "generator": "CaveScene",
      "parameters": {
        "noise_scale": 4.0,
        "density_threshold": 0.5,
        "seed": 42
      }
    },
    {
      "name": "urban",
      "description": "Urban Grid (90% density)",
      "target_density": 0.90,
      "generator": "UrbanScene",
      "parameters": {
        "building_spacing": 8,
        "street_width": 2,
        "seed": 42
      }
    }
  ]
}
```

---

## Validation & Quality Assurance

### Density Validation

After scene generation, verify density matches target:

```cpp
void ValidateScene(const VoxelGrid& grid, float targetDensity) {
    float actualDensity = grid.CalculateDensity();
    float tolerance = 0.05f;  // ±5%

    if (abs(actualDensity - targetDensity) > tolerance) {
        std::cerr << "WARNING: Scene density " << actualDensity
                  << " outside tolerance of target " << targetDensity << std::endl;
    }
}
```

### Visual Validation

Generate reference images (orthographic projection, fixed camera):

1. **Top-down view**: Verify horizontal layout
2. **Side view**: Verify vertical distribution
3. **Perspective view**: Verify overall appearance

Save to `tests/reference_images/{scene_name}_{resolution}.png`

---

## Expected Research Outcomes

### Performance Predictions

| Scene | Empty Space Skip | BlockWalk | Hardware RT |
|-------|------------------|-----------|-------------|
| Cornell (10%) | **+40%** faster | +10% faster | +20% faster |
| Cave (50%) | +15% faster | **+25%** faster | +30% faster |
| Urban (90%) | +5% faster | **+35%** faster | +50% faster |

**Hypothesis**:
- Empty space skipping benefits sparse scenes
- BlockWalk benefits dense, regular structures
- Hardware RT benefits all scenes (dedicated hardware)

---

## References

**Implementation Files** (Phase H + M):
- `VoxelGrid.h/.cpp` - Voxel storage and manipulation
- `SceneGenerator.h/.cpp` - Procedural scene generation
- `PerlinNoise.h/.cpp` - Noise generation utilities

**Documentation**:
- `VoxelRayTracingResearch-TechnicalRoadmap.md` - Overall research plan
- `PerformanceProfilerDesign.md` - Benchmarking methodology

**Research Papers** (from bibliography):
- [1] Nousiainen - Baseline performance comparison
- [2] Aokana - Voxel scene generation techniques
- [16] Derin - BlockWalk optimization (urban scenes)

---

## Next Steps (Phase M Implementation)

1. **Implement VoxelGrid class** (Phase H)
2. **Implement scene generators** (Cornell, Cave, Urban)
3. **Integrate with benchmark framework** (Phase M)
4. **Generate reference images** (validation)
5. **Run full test suite** (180 configurations)
6. **Analyze results** (compare against predictions)

**Estimated Time**: 3-4 weeks (Phase M in roadmap)

**Deliverable**: CSV files with performance metrics + scene quality validation report
