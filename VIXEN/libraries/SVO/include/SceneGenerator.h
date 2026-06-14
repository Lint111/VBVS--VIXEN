#pragma once

#include <cstdint>
#include <vector>
#include <map>
#include <string>
#include <memory>
#include <functional>
#include <mutex>
#include <glm/glm.hpp>

namespace VIXEN {
namespace RenderGraph {

// ============================================================================
// Procedural Scene Generation for Voxel Ray Tracing Research
// ============================================================================
//
// Based on: documentation/Testing/TestScenes.md
//
// Test scenes with controlled densities:
// 1. Cornell Box (~10% density) - Sparse traversal, empty space skipping
// 2. Noise (~50% density) - Medium traversal, Perlin noise patterns
// 3. Tunnels (~30-50% density) - Cave/tunnel systems
// 4. Cityscape (~80-95% density) - Dense traversal, stress test
//
// Design goals:
// - Reproducibility: Fixed seeds for deterministic generation
// - Density control: Consistent density for fair benchmarking
// - Spatial distribution: Realistic patterns (not random noise)
// - Visual clarity: Recognizable structures for validation
// ============================================================================

/**
 * @brief Simple dense voxel grid container
 *
 * Stores voxels in ZYX order for cache-coherent access.
 * Each voxel is a uint8_t (0=empty, 1-255=material ID or grayscale).
 */
class VoxelGrid {
public:
    VoxelGrid(uint32_t resolution);
    ~VoxelGrid() = default;

    /**
     * @brief Clear all voxels to empty (0)
     */
    void Clear();

    /**
     * @brief Set voxel value at 3D coordinates
     * @param x X coordinate [0, resolution)
     * @param y Y coordinate [0, resolution)
     * @param z Z coordinate [0, resolution)
     * @param value Voxel value (0=empty, 1-255=solid)
     */
    void Set(uint32_t x, uint32_t y, uint32_t z, uint8_t value);

    /**
     * @brief Get voxel value at 3D coordinates
     * @param x X coordinate [0, resolution)
     * @param y Y coordinate [0, resolution)
     * @param z Z coordinate [0, resolution)
     * @return Voxel value (0=empty, 1-255=solid)
     */
    uint8_t Get(uint32_t x, uint32_t y, uint32_t z) const;

    /**
     * @brief Get raw voxel data (ZYX order)
     * @return Flat array of voxels
     */
    const std::vector<uint8_t>& GetData() const { return data_; }

    /**
     * @brief Get grid resolution
     * @return Resolution (cubic grid size)
     */
    uint32_t GetResolution() const { return resolution_; }

    /**
     * @brief Calculate current voxel density
     * @return Percentage of non-empty voxels (0.0-100.0)
     */
    float GetDensityPercent() const;

    /**
     * @brief Count non-empty voxels
     * @return Number of solid voxels
     */
    uint32_t CountSolidVoxels() const;

private:
    std::vector<uint8_t> data_;  // Voxel data (ZYX order)
    uint32_t resolution_;         // Grid size (cubic)

    // Convert 3D coords to flat index (ZYX order)
    inline uint32_t Index(uint32_t x, uint32_t y, uint32_t z) const {
        return z * resolution_ * resolution_ + y * resolution_ + x;
    }
};

// ============================================================================
// Scene Generator Interface and Parameters
// ============================================================================

/**
 * @brief Scene generation parameters passed from config
 *
 * Contains all parameters needed by various generators.
 * Generators use only the parameters relevant to them.
 */
struct SceneGeneratorParams {
    uint32_t resolution = 128;
    uint32_t seed = 42;  // For reproducibility

    // Noise-specific params (perlin3d generator)
    float noiseScale = 4.0f;
    float densityThreshold = 0.5f;
    uint32_t octaves = 4;
    float persistence = 0.5f;

    // Urban/cityscape-specific params
    uint32_t streetWidth = 0;  // 0 = auto
    uint32_t blockCount = 4;
    float buildingDensity = 0.4f;
    float heightVariance = 0.8f;
    uint32_t blockSize = 8;

    // Tunnel/cave-specific params
    uint32_t cellCount = 8;
    float wallThickness = 0.3f;

    // General extensibility
    std::map<std::string, float> customParams;

    /**
     * @brief Get custom parameter with default value
     * @param key Parameter name
     * @param defaultValue Value if parameter not found
     * @return Parameter value or default
     */
    float GetCustomParam(const std::string& key, float defaultValue = 0.0f) const {
        auto it = customParams.find(key);
        return (it != customParams.end()) ? it->second : defaultValue;
    }
};

/**
 * @brief Abstract scene generator interface
 *
 * All scene generators implement this interface to allow
 * factory-based selection and uniform generation API.
 */
class ISceneGenerator {
public:
    virtual ~ISceneGenerator() = default;

    /**
     * @brief Generate scene into voxel grid
     * @param grid Voxel grid to populate (will be cleared first)
     * @param params Generation parameters
     */
    virtual void Generate(VoxelGrid& grid, const SceneGeneratorParams& params) = 0;

    /**
     * @brief Get generator name for logging
     * @return Generator identifier (e.g., "cornell", "noise")
     */
    virtual std::string GetName() const = 0;

    /**
     * @brief Get expected density range for validation
     * @return Pair of (minDensity%, maxDensity%)
     */
    virtual std::pair<float, float> GetExpectedDensityRange() const = 0;

    /**
     * @brief Get human-readable description
     * @return Description of what the generator creates
     */
    virtual std::string GetDescription() const = 0;
};

// ============================================================================
// Scene Generator Factory
// ============================================================================

/**
 * @brief Factory for creating scene generators by name
 *
 * Supports built-in generators and custom registration.
 */
class SceneGeneratorFactory {
public:
    using GeneratorFactoryFunc = std::function<std::unique_ptr<ISceneGenerator>()>;

    /**
     * @brief Get generator by name
     * @param name Generator name (e.g., "cornell", "noise")
     * @return Generator instance or nullptr if not found
     */
    static std::unique_ptr<ISceneGenerator> Create(const std::string& name);

    /**
     * @brief Get list of available generator names
     * @return Vector of registered generator names
     */
    static std::vector<std::string> GetAvailableGenerators();

    /**
     * @brief Register custom generator
     * @param name Generator name for lookup
     * @param factory Function that creates generator instance
     */
    static void Register(const std::string& name, GeneratorFactoryFunc factory);

    /**
     * @brief Check if generator exists
     * @param name Generator name
     * @return true if registered
     */
    static bool Exists(const std::string& name);

private:
    static std::map<std::string, GeneratorFactoryFunc>& GetRegistry();
    static void EnsureBuiltinsRegistered();
    static bool builtinsRegistered_;
};

// ============================================================================
// Concrete Scene Generators
// ============================================================================

/**
 * @brief Cornell Box scene generator (~10% density - sparse)
 *
 * Classic Cornell Box with:
 * - 3-voxel thick walls (left=red, right=green, others=white)
 * - Checkered floor pattern
 * - Two cubes (one axis-aligned, one rotated)
 * - Ceiling light (emissive patch)
 *
 * Material IDs:
 * - 0: Empty
 * - 1: Red (left wall)
 * - 2: Green (right wall)
 * - 3-5: White (back wall, floor, ceiling)
 * - 6-7: Light/dark gray (checkerboard)
 * - 10-11: Cube materials
 * - 20: Emissive ceiling light
 *
 * Target density: ~10%
 * Purpose: Sparse traversal, empty space skipping optimization test
 */
class CornellBoxSceneGenerator : public ISceneGenerator {
public:
    void Generate(VoxelGrid& grid, const SceneGeneratorParams& params) override;
    std::string GetName() const override { return "cornell"; }
    std::pair<float, float> GetExpectedDensityRange() const override { return {5.0f, 20.0f}; }
    std::string GetDescription() const override { return "Cornell Box with walls, cubes, and light"; }

private:
    void GenerateWalls(VoxelGrid& grid);
    void GenerateCheckerFloor(VoxelGrid& grid);
    void GenerateCube(
        VoxelGrid& grid,
        const glm::vec3& center,
        const glm::vec3& size,
        uint8_t material
    );
    void GenerateRotatedCube(
        VoxelGrid& grid,
        const glm::vec3& center,
        const glm::vec3& size,
        float yRotationRadians,
        uint8_t material
    );
    void GenerateCeilingLight(VoxelGrid& grid);
};

/**
 * @brief Perlin noise 3D generator utility
 *
 * 3D Perlin noise implementation for procedural terrain.
 * Uses fixed seed for reproducibility.
 */
class PerlinNoise3D {
public:
    PerlinNoise3D(uint32_t seed = 42);

    /**
     * @brief Sample 3D Perlin noise
     * @param x X coordinate (world space)
     * @param y Y coordinate (world space)
     * @param z Z coordinate (world space)
     * @return Noise value [-1.0, 1.0]
     */
    float Sample(float x, float y, float z) const;

    /**
     * @brief Sample octave noise (fractal brownian motion)
     * @param x X coordinate
     * @param y Y coordinate
     * @param z Z coordinate
     * @param octaves Number of octaves
     * @param persistence Amplitude decay per octave
     * @return Noise value [-1.0, 1.0]
     */
    float SampleOctaves(float x, float y, float z, uint32_t octaves, float persistence) const;

private:
    std::vector<int> permutation_;  // Permutation table for noise

    float Fade(float t) const;
    float Lerp(float t, float a, float b) const;
    float Grad(int hash, float x, float y, float z) const;
};

/**
 * @brief Noise-based scene generator (~50% density - medium)
 *
 * Procedural Perlin noise terrain.
 * Uses params: noiseScale, densityThreshold, octaves, persistence
 *
 * Target density: ~40-60%
 * Purpose: Medium traversal complexity, noise pattern testing
 */
class NoiseSceneGenerator : public ISceneGenerator {
public:
    void Generate(VoxelGrid& grid, const SceneGeneratorParams& params) override;
    std::string GetName() const override { return "noise"; }
    std::pair<float, float> GetExpectedDensityRange() const override { return {35.0f, 65.0f}; }
    std::string GetDescription() const override { return "3D Perlin noise terrain"; }
};

/**
 * @brief Tunnel/cave system scene generator (~30-50% density)
 *
 * Procedural cave network with:
 * - Voronoi-based or noise-based tunnels
 * - Stalactites and stalagmites
 * - Ore veins (decorative)
 *
 * Uses params: cellCount, wallThickness, seed
 *
 * Target density: ~30-50%
 * Purpose: Medium traversal complexity, coherent structure testing
 */
class TunnelSceneGenerator : public ISceneGenerator {
public:
    void Generate(VoxelGrid& grid, const SceneGeneratorParams& params) override;
    std::string GetName() const override { return "tunnels"; }
    std::pair<float, float> GetExpectedDensityRange() const override { return {25.0f, 55.0f}; }
    std::string GetDescription() const override { return "Cave/tunnel system with formations"; }

private:
    void GenerateCaveTerrain(VoxelGrid& grid, const SceneGeneratorParams& params);
    void GenerateStalactites(VoxelGrid& grid, uint32_t seed);
    void GenerateStalagmites(VoxelGrid& grid, uint32_t seed);
    void GenerateOreVeins(VoxelGrid& grid, uint32_t seed);
};

/**
 * @brief Cityscape scene generator (~80-95% density - dense)
 *
 * Procedural city with:
 * - Street grid layout
 * - Buildings with varying heights
 * - Windows, doors, architectural details
 *
 * Uses params: streetWidth, blockCount, buildingDensity, heightVariance, blockSize
 *
 * Target density: ~80-95%
 * Purpose: Dense traversal, worst-case performance testing
 */
class CityscapeSceneGenerator : public ISceneGenerator {
public:
    void Generate(VoxelGrid& grid, const SceneGeneratorParams& params) override;
    std::string GetName() const override { return "cityscape"; }
    std::pair<float, float> GetExpectedDensityRange() const override { return {75.0f, 98.0f}; }
    std::string GetDescription() const override { return "Urban cityscape with buildings"; }

private:
    void GenerateStreetGrid(VoxelGrid& grid, uint32_t streetWidth, uint32_t blockCount);
    void GenerateBuilding(
        VoxelGrid& grid,
        const glm::ivec3& origin,
        const glm::ivec3& size,
        uint32_t height
    );
    void AddBuildingDetails(
        VoxelGrid& grid,
        const glm::ivec3& origin,
        const glm::ivec3& size,
        uint32_t height
    );
};

// ============================================================================
// Voxel Data Cache (Performance Optimization)
// ============================================================================
// Caches generated VoxelGrid data to avoid regenerating the same scene
// multiple times during benchmark runs.

/**
 * @brief Cache for generated voxel grid data
 *
 * Stores VoxelGrid data keyed by (sceneType, resolution) to avoid
 * regenerating the same scene multiple times during benchmark runs.
 * This significantly speeds up test suites that iterate over multiple
 * shaders or render sizes with the same scene configuration.
 *
 * Thread-safe via mutex protection.
 */
class VoxelDataCache {
public:
    /**
     * @brief Get or generate voxel grid data
     *
     * If the (sceneType, resolution) combination is cached, returns
     * cached data. Otherwise generates the scene, caches it, and returns.
     *
     * @param sceneType Scene type name (e.g., "cornell", "noise")
     * @param resolution Grid resolution (e.g., 64, 128, 256)
     * @param params Generation parameters for the scene
     * @return Pointer to VoxelGrid (owned by cache), or nullptr if generation failed
     */
    static const VoxelGrid* GetOrGenerate(
        const std::string& sceneType,
        uint32_t resolution,
        const SceneGeneratorParams& params);

    /**
     * @brief Clear all cached data
     *
     * Call when memory needs to be freed or when starting a new benchmark suite.
     */
    static void Clear();

    /**
     * @brief Get cache statistics
     * @return Pair of (hits, misses) since last clear
     */
    static std::pair<uint32_t, uint32_t> GetStats();

    /**
     * @brief Get current cache size in bytes (approximate)
     * @return Total bytes used by cached VoxelGrid data
     */
    static size_t GetMemoryUsage();

    /**
     * @brief Enable/disable caching (default: enabled)
     * @param enabled If false, GetOrGenerate always generates fresh data
     */
    static void SetEnabled(bool enabled);

    /**
     * @brief Check if caching is enabled
     */
    static bool IsEnabled();

private:
    struct CacheKey {
        std::string sceneType;
        uint32_t resolution;

        bool operator<(const CacheKey& other) const {
            if (sceneType != other.sceneType) return sceneType < other.sceneType;
            return resolution < other.resolution;
        }
    };

    static std::map<CacheKey, std::unique_ptr<VoxelGrid>>& GetCache();
    static std::mutex& GetMutex();
    static uint32_t& GetHits();
    static uint32_t& GetMisses();
    static bool& GetEnabledFlag();
};

// ============================================================================
// Legacy Static Generator Classes (Deprecated - Use ISceneGenerator)
// ============================================================================
// These are kept for backward compatibility but should not be used in new code.
// Use SceneGeneratorFactory::Create("name") instead.

/**
 * @brief [DEPRECATED] Use SceneGeneratorFactory::Create("cornell")
 */
class CornellBoxGenerator {
public:
    [[deprecated("Use SceneGeneratorFactory::Create(\"cornell\") instead")]]
    static void Generate(VoxelGrid& grid);
};

/**
 * @brief [DEPRECATED] Use SceneGeneratorFactory::Create("tunnels")
 */
class CaveSystemGenerator {
public:
    [[deprecated("Use SceneGeneratorFactory::Create(\"tunnels\") instead")]]
    static void Generate(
        VoxelGrid& grid,
        float noiseScale = 4.0f,
        float densityThreshold = 0.5f
    );
};

/**
 * @brief [DEPRECATED] Use SceneGeneratorFactory::Create("cityscape")
 */
class UrbanGridGenerator {
public:
    [[deprecated("Use SceneGeneratorFactory::Create(\"cityscape\") instead")]]
    static void Generate(
        VoxelGrid& grid,
        uint32_t streetWidth = 0,
        uint32_t blockCount = 4
    );
};

} // namespace RenderGraph
} // namespace VIXEN
