#pragma once

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

namespace VIXEN {
namespace RenderGraph {

// ============================================================================
// Procedural Scene Generation for Voxel Ray Tracing Research
// ============================================================================
//
// Based on: documentation/Testing/TestScenes.md
//
// Three test scenes with controlled densities:
// 1. Cornell Box (10% density) - Sparse traversal, empty space skipping
// 2. Cave System (50% density) - Medium traversal, coherent structures
// 3. Urban Grid (90% density) - Dense traversal, stress test
//
// Design goals:
// - Reproducibility: Fixed seeds for deterministic generation
// - Density control: ±5% accuracy for fair benchmarking
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

/**
 * @brief Cornell Box scene generator (10% density - sparse)
 *
 * Classic Cornell Box with:
 * - 1-voxel thick walls (left=red, right=green, others=white)
 * - Checkered floor pattern
 * - Two cubes (one axis-aligned, one rotated)
 * - Ceiling light (emissive patch)
 *
 * Target density: 10% (±5%)
 * Purpose: Sparse traversal, empty space skipping optimization test
 */
class CornellBoxGenerator {
public:
    /**
     * @brief Generate Cornell Box scene
     * @param grid Voxel grid to populate (will be cleared first)
     */
    static void Generate(VoxelGrid& grid);

private:
    static void GenerateWalls(VoxelGrid& grid);
    static void GenerateCheckerFloor(VoxelGrid& grid);
    static void GenerateCube(
        VoxelGrid& grid,
        const glm::vec3& center,
        const glm::vec3& size,
        uint8_t material
    );
    static void GenerateRotatedCube(
        VoxelGrid& grid,
        const glm::vec3& center,
        const glm::vec3& size,
        float yRotationRadians,
        uint8_t material
    );
    static void GenerateCeilingLight(VoxelGrid& grid);
};

/**
 * @brief Perlin noise generator for procedural terrain
 *
 * 3D Perlin noise implementation for cave generation.
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

private:
    std::vector<int> permutation_;  // Permutation table for noise

    float Fade(float t) const;
    float Lerp(float t, float a, float b) const;
    float Grad(int hash, float x, float y, float z) const;
};

/**
 * @brief Cave system scene generator (50% density - medium)
 *
 * Procedural cave network with:
 * - Perlin noise-based tunnels and chambers
 * - Stalactites and stalagmites
 * - Ore veins (iron, gold, diamond)
 *
 * Target density: 50% (±5%)
 * Purpose: Medium traversal complexity, coherent structure testing
 */
class CaveSystemGenerator {
public:
    /**
     * @brief Generate cave system scene
     * @param grid Voxel grid to populate (will be cleared first)
     * @param noiseScale Perlin noise frequency (default: 4.0)
     * @param densityThreshold Solid/empty threshold (default: 0.5 for 50%)
     */
    static void Generate(
        VoxelGrid& grid,
        float noiseScale = 4.0f,
        float densityThreshold = 0.5f
    );

private:
    static void GenerateCaveTerrain(
        VoxelGrid& grid,
        float noiseScale,
        float threshold
    );
    static void GenerateStalactites(VoxelGrid& grid);
    static void GenerateStalagtites(VoxelGrid& grid);
    static void GenerateOreVeins(VoxelGrid& grid);
};

/**
 * @brief Urban grid scene generator (90% density - dense)
 *
 * Procedural city with:
 * - Street grid layout
 * - Buildings with varying heights
 * - Windows, doors, architectural details
 *
 * Target density: 90% (±5%)
 * Purpose: Dense traversal, worst-case performance testing
 */
class UrbanGridGenerator {
public:
    /**
     * @brief Generate urban grid scene
     * @param grid Voxel grid to populate (will be cleared first)
     * @param streetWidth Width of streets in voxels (default: resolution/16)
     * @param blockCount Number of city blocks (default: 4x4)
     */
    static void Generate(
        VoxelGrid& grid,
        uint32_t streetWidth = 0,  // 0 = auto (resolution/16)
        uint32_t blockCount = 4
    );

private:
    static void GenerateStreetGrid(VoxelGrid& grid, uint32_t streetWidth, uint32_t blockCount);
    static void GenerateBuilding(
        VoxelGrid& grid,
        const glm::ivec3& origin,
        const glm::ivec3& size,
        uint32_t height
    );
    static void AddBuildingDetails(VoxelGrid& grid, const glm::ivec3& origin, const glm::ivec3& size, uint32_t height);
};

} // namespace RenderGraph
} // namespace VIXEN
