#include "Data/SceneGenerator.h"
#include <cmath>
#include <algorithm>
#include <random>
#include <iostream>

namespace VIXEN {
namespace RenderGraph {

// ============================================================================
// VoxelGrid Implementation
// ============================================================================

VoxelGrid::VoxelGrid(uint32_t resolution)
    : resolution_(resolution)
{
    data_.resize(resolution * resolution * resolution, 0);
}

void VoxelGrid::Clear() {
    std::fill(data_.begin(), data_.end(), 0);
}

void VoxelGrid::Set(uint32_t x, uint32_t y, uint32_t z, uint8_t value) {
    if (x >= resolution_ || y >= resolution_ || z >= resolution_) {
        return; // Bounds check
    }
    data_[Index(x, y, z)] = value;
}

uint8_t VoxelGrid::Get(uint32_t x, uint32_t y, uint32_t z) const {
    if (x >= resolution_ || y >= resolution_ || z >= resolution_) {
        return 0; // Out of bounds = empty
    }
    return data_[Index(x, y, z)];
}

float VoxelGrid::GetDensityPercent() const {
    uint32_t solidCount = CountSolidVoxels();
    uint32_t totalCount = resolution_ * resolution_ * resolution_;
    return (static_cast<float>(solidCount) / static_cast<float>(totalCount)) * 100.0f;
}

uint32_t VoxelGrid::CountSolidVoxels() const {
    uint32_t count = 0;
    for (uint8_t voxel : data_) {
        if (voxel != 0) {
            ++count;
        }
    }
    return count;
}

// ============================================================================
// Scene Generator Factory Implementation
// ============================================================================

bool SceneGeneratorFactory::builtinsRegistered_ = false;

std::map<std::string, SceneGeneratorFactory::GeneratorFactoryFunc>& SceneGeneratorFactory::GetRegistry() {
    static std::map<std::string, GeneratorFactoryFunc> registry;
    return registry;
}

void SceneGeneratorFactory::EnsureBuiltinsRegistered() {
    if (builtinsRegistered_) {
        return;
    }
    builtinsRegistered_ = true;

    auto& registry = GetRegistry();

    // Register built-in generators
    registry["cornell"] = []() { return std::make_unique<CornellBoxSceneGenerator>(); };
    registry["noise"] = []() { return std::make_unique<NoiseSceneGenerator>(); };
    registry["tunnels"] = []() { return std::make_unique<TunnelSceneGenerator>(); };
    registry["cityscape"] = []() { return std::make_unique<CityscapeSceneGenerator>(); };

    // Aliases for backward compatibility
    registry["cave"] = []() { return std::make_unique<TunnelSceneGenerator>(); };
    registry["urban"] = []() { return std::make_unique<CityscapeSceneGenerator>(); };
}

std::unique_ptr<ISceneGenerator> SceneGeneratorFactory::Create(const std::string& name) {
    EnsureBuiltinsRegistered();

    auto& registry = GetRegistry();
    auto it = registry.find(name);
    if (it != registry.end()) {
        return it->second();
    }
    return nullptr;
}

std::vector<std::string> SceneGeneratorFactory::GetAvailableGenerators() {
    EnsureBuiltinsRegistered();

    std::vector<std::string> names;
    for (const auto& [name, _] : GetRegistry()) {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

void SceneGeneratorFactory::Register(const std::string& name, GeneratorFactoryFunc factory) {
    EnsureBuiltinsRegistered();
    GetRegistry()[name] = std::move(factory);
}

bool SceneGeneratorFactory::Exists(const std::string& name) {
    EnsureBuiltinsRegistered();
    return GetRegistry().find(name) != GetRegistry().end();
}

// ============================================================================
// Voxel Data Cache Implementation
// ============================================================================

std::map<VoxelDataCache::CacheKey, std::unique_ptr<VoxelGrid>>& VoxelDataCache::GetCache() {
    static std::map<CacheKey, std::unique_ptr<VoxelGrid>> cache;
    return cache;
}

std::mutex& VoxelDataCache::GetMutex() {
    static std::mutex mutex;
    return mutex;
}

uint32_t& VoxelDataCache::GetHits() {
    static uint32_t hits = 0;
    return hits;
}

uint32_t& VoxelDataCache::GetMisses() {
    static uint32_t misses = 0;
    return misses;
}

bool& VoxelDataCache::GetEnabledFlag() {
    static bool enabled = true;
    return enabled;
}

const VoxelGrid* VoxelDataCache::GetOrGenerate(
    const std::string& sceneType,
    uint32_t resolution,
    const SceneGeneratorParams& params)
{
    // If caching is disabled, always generate fresh data
    if (!GetEnabledFlag()) {
        auto generator = SceneGeneratorFactory::Create(sceneType);
        if (!generator) {
            // Logging would require logger context; use stderr for now
            std::cerr << "[VoxelDataCache] ERROR: Unknown scene type: " << sceneType << std::endl;
            return nullptr;
        }
        // Return nullptr to signal caller should generate fresh
        GetMisses()++;
        return nullptr;
    }

    CacheKey key{sceneType, resolution};

    std::lock_guard<std::mutex> lock(GetMutex());

    // Check if already cached
    auto& cache = GetCache();
    auto it = cache.find(key);
    if (it != cache.end()) {
        GetHits()++;
        std::cerr << "[VoxelDataCache] HIT: " << sceneType << " @ " << resolution
                  << "^3 (hits=" << GetHits() << ", misses=" << GetMisses() << ")" << std::endl;
        return it->second.get();
    }

    // Cache miss - generate new data
    GetMisses()++;
    std::cerr << "[VoxelDataCache] MISS: Generating " << sceneType << " @ " << resolution
              << "^3..." << std::endl;

    auto generator = SceneGeneratorFactory::Create(sceneType);
    if (!generator) {
        std::cerr << "[VoxelDataCache] ERROR: Unknown scene type: " << sceneType << std::endl;
        return nullptr;
    }

    // Create and populate grid
    auto grid = std::make_unique<VoxelGrid>(resolution);
    generator->Generate(*grid, params);

    std::cerr << "[VoxelDataCache] Generated " << sceneType << " @ " << resolution
              << "^3, density=" << grid->GetDensityPercent() << "%" << std::endl;

    // Store in cache and return pointer
    VoxelGrid* result = grid.get();
    cache[key] = std::move(grid);

    return result;
}

void VoxelDataCache::Clear() {
    std::lock_guard<std::mutex> lock(GetMutex());
    auto [hits, misses] = GetStats();
    std::cerr << "[VoxelDataCache] Clearing cache (final stats: hits=" << hits
              << ", misses=" << misses << ")" << std::endl;
    GetCache().clear();
    GetHits() = 0;
    GetMisses() = 0;
}

std::pair<uint32_t, uint32_t> VoxelDataCache::GetStats() {
    return {GetHits(), GetMisses()};
}

size_t VoxelDataCache::GetMemoryUsage() {
    std::lock_guard<std::mutex> lock(GetMutex());
    size_t total = 0;
    for (const auto& [key, grid] : GetCache()) {
        // Each voxel is 1 byte, resolution^3 voxels
        total += grid->GetData().size();
    }
    return total;
}

void VoxelDataCache::SetEnabled(bool enabled) {
    GetEnabledFlag() = enabled;
    std::cerr << "[VoxelDataCache] Caching " << (enabled ? "enabled" : "disabled") << std::endl;
}

bool VoxelDataCache::IsEnabled() {
    return GetEnabledFlag();
}

// ============================================================================
// Perlin Noise 3D Implementation
// ============================================================================

PerlinNoise3D::PerlinNoise3D(uint32_t seed) {
    // Initialize permutation table with deterministic shuffle
    permutation_.resize(512);

    std::vector<int> p(256);
    for (int i = 0; i < 256; ++i) {
        p[i] = i;
    }

    // Shuffle using fixed seed for reproducibility
    std::mt19937 rng(seed);
    std::shuffle(p.begin(), p.end(), rng);

    // Duplicate for wrap-around
    for (int i = 0; i < 256; ++i) {
        permutation_[i] = p[i];
        permutation_[256 + i] = p[i];
    }
}

float PerlinNoise3D::Sample(float x, float y, float z) const {
    // Find unit cube that contains point
    int X = static_cast<int>(std::floor(x)) & 255;
    int Y = static_cast<int>(std::floor(y)) & 255;
    int Z = static_cast<int>(std::floor(z)) & 255;

    // Find relative position in cube
    x -= std::floor(x);
    y -= std::floor(y);
    z -= std::floor(z);

    // Compute fade curves
    float u = Fade(x);
    float v = Fade(y);
    float w = Fade(z);

    // Hash coordinates of cube corners
    int A = permutation_[X] + Y;
    int AA = permutation_[A] + Z;
    int AB = permutation_[A + 1] + Z;
    int B = permutation_[X + 1] + Y;
    int BA = permutation_[B] + Z;
    int BB = permutation_[B + 1] + Z;

    // Blend results from 8 corners of cube
    float res = Lerp(w,
        Lerp(v,
            Lerp(u, Grad(permutation_[AA], x, y, z),
                    Grad(permutation_[BA], x - 1, y, z)),
            Lerp(u, Grad(permutation_[AB], x, y - 1, z),
                    Grad(permutation_[BB], x - 1, y - 1, z))),
        Lerp(v,
            Lerp(u, Grad(permutation_[AA + 1], x, y, z - 1),
                    Grad(permutation_[BA + 1], x - 1, y, z - 1)),
            Lerp(u, Grad(permutation_[AB + 1], x, y - 1, z - 1),
                    Grad(permutation_[BB + 1], x - 1, y - 1, z - 1))));

    return res;
}

float PerlinNoise3D::SampleOctaves(float x, float y, float z, uint32_t octaves, float persistence) const {
    float total = 0.0f;
    float frequency = 1.0f;
    float amplitude = 1.0f;
    float maxValue = 0.0f;

    for (uint32_t i = 0; i < octaves; ++i) {
        total += Sample(x * frequency, y * frequency, z * frequency) * amplitude;
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= 2.0f;
    }

    return total / maxValue;  // Normalize to [-1, 1]
}

float PerlinNoise3D::Fade(float t) const {
    // 6t^5 - 15t^4 + 10t^3
    return t * t * t * (t * (t * 6 - 15) + 10);
}

float PerlinNoise3D::Lerp(float t, float a, float b) const {
    return a + t * (b - a);
}

float PerlinNoise3D::Grad(int hash, float x, float y, float z) const {
    // Convert low 4 bits of hash code into 12 gradient directions
    int h = hash & 15;
    float u = h < 8 ? x : y;
    float v = h < 4 ? y : h == 12 || h == 14 ? x : z;
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

// ============================================================================
// Cornell Box Scene Generator Implementation
// ============================================================================

void CornellBoxSceneGenerator::Generate(VoxelGrid& grid, const SceneGeneratorParams& params) {
    grid.Clear();

    // 1. Generate walls (3-voxel thick box)
    GenerateWalls(grid);

    // 2. Generate checkered floor
    GenerateCheckerFloor(grid);

    // 3. Left cube (axis-aligned, material ID 10)
    uint32_t res = grid.GetResolution();
    GenerateCube(
        grid,
        glm::vec3(res * 0.3f, res * 0.15f, res * 0.3f),  // Center position
        glm::vec3(res * 0.2f, res * 0.3f, res * 0.2f),   // Size
        10  // Material ID
    );

    // 4. Right cube (rotated 15 deg on Y-axis, material ID 11)
    GenerateRotatedCube(
        grid,
        glm::vec3(res * 0.7f, res * 0.1f, res * 0.6f),
        glm::vec3(res * 0.15f, res * 0.2f, res * 0.15f),
        glm::radians(15.0f),  // Y rotation
        11  // Material ID
    );

    // 5. Ceiling light (emissive, material ID 20)
    GenerateCeilingLight(grid);
}

void CornellBoxSceneGenerator::GenerateWalls(VoxelGrid& grid) {
    uint32_t res = grid.GetResolution();
    // Thicken walls to 3 voxels to ensure brick occupancy at all resolutions
    const uint32_t wallThickness = 3;

    // Left wall (material ID 1 = red)
    for (uint32_t y = 0; y < res; ++y) {
        for (uint32_t z = 0; z < res; ++z) {
            for (uint32_t t = 0; t < wallThickness; ++t) {
                grid.Set(t, y, z, 1);
            }
        }
    }

    // Right wall (material ID 2 = green)
    for (uint32_t y = 0; y < res; ++y) {
        for (uint32_t z = 0; z < res; ++z) {
            for (uint32_t t = 0; t < wallThickness; ++t) {
                grid.Set(res - 1 - t, y, z, 2);
            }
        }
    }

    // Back wall (material ID 3 = white)
    for (uint32_t x = 0; x < res; ++x) {
        for (uint32_t y = 0; y < res; ++y) {
            for (uint32_t t = 0; t < wallThickness; ++t) {
                grid.Set(x, y, t, 3);
            }
        }
    }

    // Floor (material ID 4 = white)
    for (uint32_t x = 0; x < res; ++x) {
        for (uint32_t z = 0; z < res; ++z) {
            for (uint32_t t = 0; t < wallThickness; ++t) {
                grid.Set(x, t, z, 4);
            }
        }
    }

    // Ceiling (material ID 5 = white)
    for (uint32_t x = 0; x < res; ++x) {
        for (uint32_t z = 0; z < res; ++z) {
            for (uint32_t t = 0; t < wallThickness; ++t) {
                grid.Set(x, res - 1 - t, z, 5);
            }
        }
    }
}

void CornellBoxSceneGenerator::GenerateCheckerFloor(VoxelGrid& grid) {
    uint32_t res = grid.GetResolution();
    uint32_t checkerSize = res / 8;  // 8x8 checker grid

    if (checkerSize == 0) checkerSize = 1;

    for (uint32_t x = 0; x < res; ++x) {
        for (uint32_t z = 0; z < res; ++z) {
            bool isLight = ((x / checkerSize) + (z / checkerSize)) % 2 == 0;
            uint8_t material = isLight ? 6 : 7;  // Light gray / dark gray
            grid.Set(x, 0, z, material);
        }
    }
}

void CornellBoxSceneGenerator::GenerateCube(
    VoxelGrid& grid,
    const glm::vec3& center,
    const glm::vec3& size,
    uint8_t material)
{
    glm::ivec3 min = glm::ivec3(center - size * 0.5f);
    glm::ivec3 max = glm::ivec3(center + size * 0.5f);

    for (int x = min.x; x <= max.x; ++x) {
        for (int y = min.y; y <= max.y; ++y) {
            for (int z = min.z; z <= max.z; ++z) {
                if (x >= 0 && y >= 0 && z >= 0 &&
                    x < static_cast<int>(grid.GetResolution()) &&
                    y < static_cast<int>(grid.GetResolution()) &&
                    z < static_cast<int>(grid.GetResolution()))
                {
                    grid.Set(x, y, z, material);
                }
            }
        }
    }
}

void CornellBoxSceneGenerator::GenerateRotatedCube(
    VoxelGrid& grid,
    const glm::vec3& center,
    const glm::vec3& size,
    float yRotationRadians,
    uint8_t material)
{
    // Rotation matrix around Y-axis
    float cosTheta = std::cos(yRotationRadians);
    float sinTheta = std::sin(yRotationRadians);

    glm::ivec3 minBox = glm::ivec3(center - size);
    glm::ivec3 maxBox = glm::ivec3(center + size);

    // Iterate over bounding box and test rotated points
    for (int x = minBox.x; x <= maxBox.x; ++x) {
        for (int y = minBox.y; y <= maxBox.y; ++y) {
            for (int z = minBox.z; z <= maxBox.z; ++z) {
                // Transform to local space (relative to center)
                glm::vec3 local = glm::vec3(x, y, z) - center;

                // Apply inverse rotation to test if point is inside axis-aligned cube
                float rotatedX = local.x * cosTheta + local.z * sinTheta;
                float rotatedZ = -local.x * sinTheta + local.z * cosTheta;

                // Check if rotated point is inside cube bounds
                if (std::abs(rotatedX) <= size.x * 0.5f &&
                    std::abs(local.y) <= size.y * 0.5f &&
                    std::abs(rotatedZ) <= size.z * 0.5f)
                {
                    if (x >= 0 && y >= 0 && z >= 0 &&
                        x < static_cast<int>(grid.GetResolution()) &&
                        y < static_cast<int>(grid.GetResolution()) &&
                        z < static_cast<int>(grid.GetResolution()))
                    {
                        grid.Set(x, y, z, material);
                    }
                }
            }
        }
    }
}

void CornellBoxSceneGenerator::GenerateCeilingLight(VoxelGrid& grid) {
    uint32_t res = grid.GetResolution();
    uint32_t lightSize = res / 8;  // 1/8 of resolution (e.g., 16 for 128^3)

    if (lightSize == 0) lightSize = 1;

    uint32_t startX = (res - lightSize) / 2;
    uint32_t startZ = (res - lightSize) / 2;

    for (uint32_t x = startX; x < startX + lightSize; ++x) {
        for (uint32_t z = startZ; z < startZ + lightSize; ++z) {
            grid.Set(x, res - 1, z, 20);  // Material ID 20 = emissive
        }
    }
}

// ============================================================================
// Noise Scene Generator Implementation
// ============================================================================

void NoiseSceneGenerator::Generate(VoxelGrid& grid, const SceneGeneratorParams& params) {
    grid.Clear();

    PerlinNoise3D noise(params.seed);
    uint32_t res = grid.GetResolution();

    for (uint32_t x = 0; x < res; ++x) {
        for (uint32_t y = 0; y < res; ++y) {
            for (uint32_t z = 0; z < res; ++z) {
                // Sample octave noise for more interesting patterns
                float noiseValue = noise.SampleOctaves(
                    x / (res / params.noiseScale),
                    y / (res / params.noiseScale),
                    z / (res / params.noiseScale),
                    params.octaves,
                    params.persistence
                );

                // Convert noise range [-1, 1] to [0, 1]
                noiseValue = (noiseValue + 1.0f) * 0.5f;

                // Threshold determines solid vs empty
                if (noiseValue > params.densityThreshold) {
                    // Vary material based on noise for visual interest
                    uint8_t material = 30 + static_cast<uint8_t>(noiseValue * 10);
                    grid.Set(x, y, z, material);
                }
            }
        }
    }
}

// ============================================================================
// Tunnel Scene Generator Implementation
// ============================================================================

void TunnelSceneGenerator::Generate(VoxelGrid& grid, const SceneGeneratorParams& params) {
    grid.Clear();

    // 1. Generate cave terrain with Perlin noise
    GenerateCaveTerrain(grid, params);

    // 2. Add stalactites (from ceiling)
    GenerateStalactites(grid, params.seed + 100);

    // 3. Add stalagmites (from floor)
    GenerateStalagmites(grid, params.seed + 200);

    // 4. Add ore veins (decorative)
    GenerateOreVeins(grid, params.seed + 300);
}

void TunnelSceneGenerator::GenerateCaveTerrain(VoxelGrid& grid, const SceneGeneratorParams& params) {
    PerlinNoise3D noise(params.seed);
    uint32_t res = grid.GetResolution();

    // Use wallThickness to control density (higher = more solid)
    float threshold = params.wallThickness;

    for (uint32_t x = 0; x < res; ++x) {
        for (uint32_t y = 0; y < res; ++y) {
            for (uint32_t z = 0; z < res; ++z) {
                // Sample 3D Perlin noise
                float noiseValue = noise.Sample(
                    x / (res / params.noiseScale),
                    y / (res / params.noiseScale),
                    z / (res / params.noiseScale)
                );

                // Convert noise range [-1, 1] to [0, 1]
                noiseValue = (noiseValue + 1.0f) * 0.5f;

                // Threshold determines solid vs empty
                if (noiseValue > threshold) {
                    grid.Set(x, y, z, 30);  // Material ID 30 = stone
                }
            }
        }
    }
}

void TunnelSceneGenerator::GenerateStalactites(VoxelGrid& grid, uint32_t seed) {
    uint32_t res = grid.GetResolution();
    std::mt19937 rng(seed);
    std::uniform_int_distribution<uint32_t> posDist(0, res - 1);
    std::uniform_int_distribution<uint32_t> lengthDist(res / 20, res / 10);

    uint32_t stalactiteCount = res / 4;  // Density based on resolution

    for (uint32_t i = 0; i < stalactiteCount; ++i) {
        uint32_t x = posDist(rng);
        uint32_t z = posDist(rng);
        uint32_t length = lengthDist(rng);

        // Grow downward from ceiling
        for (uint32_t y = res - 1; y > res - 1 - length && y > 0; --y) {
            if (grid.Get(x, y, z) == 0) {  // Only grow in empty space
                grid.Set(x, y, z, 31);  // Material ID 31 = stalactite
            } else {
                break;  // Stop at first solid voxel
            }
        }
    }
}

void TunnelSceneGenerator::GenerateStalagmites(VoxelGrid& grid, uint32_t seed) {
    uint32_t res = grid.GetResolution();
    std::mt19937 rng(seed);
    std::uniform_int_distribution<uint32_t> posDist(0, res - 1);
    std::uniform_int_distribution<uint32_t> lengthDist(res / 20, res / 10);

    uint32_t stalagmiteCount = res / 4;

    for (uint32_t i = 0; i < stalagmiteCount; ++i) {
        uint32_t x = posDist(rng);
        uint32_t z = posDist(rng);
        uint32_t length = lengthDist(rng);

        // Grow upward from floor
        for (uint32_t y = 0; y < length && y < res; ++y) {
            if (grid.Get(x, y, z) == 0) {  // Only grow in empty space
                grid.Set(x, y, z, 32);  // Material ID 32 = stalagmite
            } else {
                break;
            }
        }
    }
}

void TunnelSceneGenerator::GenerateOreVeins(VoxelGrid& grid, uint32_t seed) {
    uint32_t res = grid.GetResolution();
    std::mt19937 rng(seed);
    std::uniform_int_distribution<uint32_t> posDist(0, res - 1);

    uint32_t oreCount = res / 2;

    for (uint32_t i = 0; i < oreCount; ++i) {
        uint32_t x = posDist(rng);
        uint32_t y = posDist(rng);
        uint32_t z = posDist(rng);

        if (grid.Get(x, y, z) == 30) {  // Replace stone with ore
            grid.Set(x, y, z, 40);  // Material ID 40 = ore
        }
    }
}

// ============================================================================
// Cityscape Scene Generator Implementation
// ============================================================================

void CityscapeSceneGenerator::Generate(VoxelGrid& grid, const SceneGeneratorParams& params) {
    grid.Clear();

    uint32_t res = grid.GetResolution();
    uint32_t streetWidth = params.streetWidth;
    if (streetWidth == 0) {
        streetWidth = res / 16;  // Auto: 1/16 of resolution
        if (streetWidth == 0) streetWidth = 1;
    }

    // 1. Generate street grid
    GenerateStreetGrid(grid, streetWidth, params.blockCount);

    // 2. Generate buildings in each block
    uint32_t blockSize = (res - (params.blockCount + 1) * streetWidth) / params.blockCount;

    std::mt19937 rng(params.seed);

    for (uint32_t bx = 0; bx < params.blockCount; ++bx) {
        for (uint32_t bz = 0; bz < params.blockCount; ++bz) {
            glm::ivec3 origin(
                streetWidth + bx * (blockSize + streetWidth),
                0,
                streetWidth + bz * (blockSize + streetWidth)
            );

            glm::ivec3 size(blockSize, 0, blockSize);

            // Random building height based on heightVariance
            float minHeight = res * (0.6f - params.heightVariance * 0.3f);
            float maxHeight = res * (0.6f + params.heightVariance * 0.3f);
            std::uniform_int_distribution<uint32_t> heightDist(
                static_cast<uint32_t>(minHeight),
                static_cast<uint32_t>(maxHeight)
            );
            uint32_t height = heightDist(rng);

            GenerateBuilding(grid, origin, size, height);
        }
    }
}

void CityscapeSceneGenerator::GenerateStreetGrid(
    VoxelGrid& grid,
    uint32_t streetWidth,
    uint32_t blockCount)
{
    uint32_t res = grid.GetResolution();

    // Horizontal streets (along X-axis)
    for (uint32_t street = 0; street <= blockCount; ++street) {
        uint32_t blockSize = (res - (blockCount + 1) * streetWidth) / blockCount;
        uint32_t z = street * (blockSize + streetWidth);

        for (uint32_t x = 0; x < res; ++x) {
            for (uint32_t w = 0; w < streetWidth; ++w) {
                if (z + w < res) {
                    grid.Set(x, 0, z + w, 50);  // Material ID 50 = asphalt
                }
            }
        }
    }

    // Vertical streets (along Z-axis)
    for (uint32_t street = 0; street <= blockCount; ++street) {
        uint32_t blockSize = (res - (blockCount + 1) * streetWidth) / blockCount;
        uint32_t x = street * (blockSize + streetWidth);

        for (uint32_t z = 0; z < res; ++z) {
            for (uint32_t w = 0; w < streetWidth; ++w) {
                if (x + w < res) {
                    grid.Set(x + w, 0, z, 50);  // Material ID 50 = asphalt
                }
            }
        }
    }
}

void CityscapeSceneGenerator::GenerateBuilding(
    VoxelGrid& grid,
    const glm::ivec3& origin,
    const glm::ivec3& size,
    uint32_t height)
{
    // Fill solid block
    for (int x = origin.x; x < origin.x + size.x; ++x) {
        for (int y = origin.y; y < static_cast<int>(origin.y + height); ++y) {
            for (int z = origin.z; z < origin.z + size.z; ++z) {
                if (x >= 0 && y >= 0 && z >= 0 &&
                    x < static_cast<int>(grid.GetResolution()) &&
                    y < static_cast<int>(grid.GetResolution()) &&
                    z < static_cast<int>(grid.GetResolution()))
                {
                    grid.Set(x, y, z, 60);  // Material ID 60 = concrete
                }
            }
        }
    }

    // Add building details (windows, etc.)
    AddBuildingDetails(grid, origin, size, height);
}

void CityscapeSceneGenerator::AddBuildingDetails(
    VoxelGrid& grid,
    const glm::ivec3& origin,
    const glm::ivec3& size,
    uint32_t height)
{
    // Simple window pattern (every 4th voxel on exterior faces)
    for (uint32_t y = origin.y + 2; y < origin.y + height; y += 4) {
        // Front/back faces
        for (int x = origin.x + 2; x < origin.x + size.x; x += 4) {
            if (x >= 0 && y >= 0 &&
                x < static_cast<int>(grid.GetResolution()) &&
                y < static_cast<int>(grid.GetResolution())) {
                grid.Set(x, y, origin.z, 61);  // Material ID 61 = glass (front)
                grid.Set(x, y, origin.z + size.z - 1, 61);  // Back
            }
        }

        // Left/right faces
        for (int z = origin.z + 2; z < origin.z + size.z; z += 4) {
            if (z >= 0 && y >= 0 &&
                z < static_cast<int>(grid.GetResolution()) &&
                y < static_cast<int>(grid.GetResolution())) {
                grid.Set(origin.x, y, z, 61);  // Left
                grid.Set(origin.x + size.x - 1, y, z, 61);  // Right
            }
        }
    }
}

// ============================================================================
// Legacy Static Generator Implementations (Deprecated)
// ============================================================================

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4996)  // Disable deprecation warnings for legacy code
#endif

void CornellBoxGenerator::Generate(VoxelGrid& grid) {
    CornellBoxSceneGenerator generator;
    SceneGeneratorParams params;
    params.resolution = grid.GetResolution();
    generator.Generate(grid, params);
}

void CaveSystemGenerator::Generate(
    VoxelGrid& grid,
    float noiseScale,
    float densityThreshold)
{
    TunnelSceneGenerator generator;
    SceneGeneratorParams params;
    params.resolution = grid.GetResolution();
    params.noiseScale = noiseScale;
    params.wallThickness = densityThreshold;  // Map threshold to wallThickness
    generator.Generate(grid, params);
}

void UrbanGridGenerator::Generate(
    VoxelGrid& grid,
    uint32_t streetWidth,
    uint32_t blockCount)
{
    CityscapeSceneGenerator generator;
    SceneGeneratorParams params;
    params.resolution = grid.GetResolution();
    params.streetWidth = streetWidth;
    params.blockCount = blockCount;
    generator.Generate(grid, params);
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

} // namespace RenderGraph
} // namespace VIXEN
