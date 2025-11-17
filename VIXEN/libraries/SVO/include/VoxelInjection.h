#pragma once

#include "ISVOStructure.h"
#include "SVOTypes.h"
#include <functional>
#include <memory>

namespace SVO {

/**
 * Direct voxel data for injection into SVO structure.
 * Bypasses mesh triangulation - use for procedural generation,
 * noise fields, SDFs, terrain, etc.
 */
struct VoxelData {
    glm::vec3 position;      // World-space position
    glm::vec3 color;         // RGB color [0,1]
    glm::vec3 normal;        // Surface normal (normalized)
    float density = 1.0f;    // Density/occupancy [0,1]
    float occlusion = 1.0f;  // Ambient occlusion [0,1]

    bool isSolid() const { return density > 0.5f; }
};

/**
 * Sparse voxel input - only occupied voxels.
 * Most efficient for sparse data (terrain, noise).
 */
struct SparseVoxelInput {
    std::vector<VoxelData> voxels;
    glm::vec3 worldMin;
    glm::vec3 worldMax;
    int resolution;  // Grid resolution along each axis
};

/**
 * Dense voxel grid input - 3D array.
 * Use for dense volumetric data (fog, clouds, etc.).
 */
struct DenseVoxelInput {
    std::vector<VoxelData> voxels;  // Size = resolution^3
    glm::vec3 worldMin;
    glm::vec3 worldMax;
    glm::ivec3 resolution;          // Resolution per axis

    size_t getIndex(int x, int y, int z) const {
        return x + y * resolution.x + z * resolution.x * resolution.y;
    }

    const VoxelData& at(int x, int y, int z) const {
        return voxels[getIndex(x, y, z)];
    }
};

/**
 * Procedural voxel sampler - callback-based.
 * Most flexible - generates voxels on-demand during build.
 * Perfect for infinite terrain, noise functions, SDFs, etc.
 */
class IVoxelSampler {
public:
    virtual ~IVoxelSampler() = default;

    /**
     * Sample voxel data at given position.
     * @return true if voxel is solid, false if empty
     */
    virtual bool sample(const glm::vec3& position, VoxelData& outData) const = 0;

    /**
     * Get bounding box of valid data.
     * Return infinite bounds if unbounded (e.g., infinite terrain).
     */
    virtual void getBounds(glm::vec3& outMin, glm::vec3& outMax) const = 0;

    /**
     * Estimate density at given scale.
     * Used for early termination and LOD.
     * @param center Center of region
     * @param size Size of region
     * @return Approximate density [0,1]
     */
    virtual float estimateDensity(const glm::vec3& center, float size) const {
        // Default: always subdivide
        return 1.0f;
    }
};

/**
 * Lambda-based sampler for convenience.
 */
class LambdaVoxelSampler : public IVoxelSampler {
public:
    using SampleFunc = std::function<bool(const glm::vec3&, VoxelData&)>;
    using BoundsFunc = std::function<void(glm::vec3&, glm::vec3&)>;
    using DensityFunc = std::function<float(const glm::vec3&, float)>;

    explicit LambdaVoxelSampler(
        SampleFunc sampleFunc,
        BoundsFunc boundsFunc,
        DensityFunc densityFunc = nullptr)
        : m_sampleFunc(std::move(sampleFunc))
        , m_boundsFunc(std::move(boundsFunc))
        , m_densityFunc(std::move(densityFunc)) {}

    bool sample(const glm::vec3& position, VoxelData& outData) const override {
        return m_sampleFunc(position, outData);
    }

    void getBounds(glm::vec3& outMin, glm::vec3& outMax) const override {
        m_boundsFunc(outMin, outMax);
    }

    float estimateDensity(const glm::vec3& center, float size) const override {
        return m_densityFunc ? m_densityFunc(center, size) : 1.0f;
    }

private:
    SampleFunc m_sampleFunc;
    BoundsFunc m_boundsFunc;
    DensityFunc m_densityFunc;
};

/**
 * Common procedural samplers.
 */
namespace Samplers {

/**
 * 3D Perlin/Simplex noise sampler.
 * Use for terrain, clouds, organic shapes.
 */
class NoiseSampler : public IVoxelSampler {
public:
    struct Params {
        float frequency = 1.0f;
        float amplitude = 1.0f;
        int octaves = 4;
        float lacunarity = 2.0f;
        float persistence = 0.5f;
        float threshold = 0.0f;  // Density threshold for solid
        glm::vec3 offset{0.0f};
    };

    explicit NoiseSampler(const Params& params = Params{});

    bool sample(const glm::vec3& position, VoxelData& outData) const override;
    void getBounds(glm::vec3& outMin, glm::vec3& outMax) const override;
    float estimateDensity(const glm::vec3& center, float size) const override;

private:
    Params m_params;
};

/**
 * Signed Distance Field (SDF) sampler.
 * Use for CSG operations, smooth blending, etc.
 */
class SDFSampler : public IVoxelSampler {
public:
    using SDFFunc = std::function<float(const glm::vec3&)>;

    explicit SDFSampler(SDFFunc sdfFunc, const glm::vec3& min, const glm::vec3& max);

    bool sample(const glm::vec3& position, VoxelData& outData) const override;
    void getBounds(glm::vec3& outMin, glm::vec3& outMax) const override;

private:
    SDFFunc m_sdfFunc;
    glm::vec3 m_min, m_max;

    glm::vec3 estimateNormal(const glm::vec3& p) const;
};

/**
 * Heightmap terrain sampler.
 * Use for terrain generation from heightmap data.
 */
class HeightmapSampler : public IVoxelSampler {
public:
    struct Params {
        std::vector<float> heights;  // Height values
        int width, height;            // Heightmap dimensions
        float minHeight = 0.0f;
        float maxHeight = 100.0f;
        float horizontalScale = 1.0f;
        glm::vec3 baseColor{0.5f, 0.4f, 0.3f};
    };

    explicit HeightmapSampler(const Params& params);

    bool sample(const glm::vec3& position, VoxelData& outData) const override;
    void getBounds(glm::vec3& outMin, glm::vec3& outMax) const override;
    float estimateDensity(const glm::vec3& center, float size) const override;

private:
    Params m_params;

    float sampleHeight(float x, float z) const;
    glm::vec3 computeNormal(float x, float z) const;
};

} // namespace Samplers

/**
 * Voxel injection configuration.
 */
struct InjectionConfig {
    int maxLevels = 16;
    float errorThreshold = 0.001f;
    bool enableContours = true;
    bool enableCompression = true;

    // LOD control
    bool enableLOD = true;
    float lodBias = 0.0f;  // Negative = finer, positive = coarser

    // Filtering
    enum class FilterMode {
        None,           // No filtering
        Box,            // Box filter (average)
        Gaussian,       // Gaussian filter
    };
    FilterMode filterMode = FilterMode::Box;

    // Memory limits
    size_t maxMemoryBytes = 0;  // 0 = unlimited
};

/**
 * Voxel data injector - builds SVO from raw voxel data.
 */
class VoxelInjector {
public:
    VoxelInjector() = default;

    /**
     * Inject sparse voxel data.
     * Most efficient for sparse data (individual voxels, particle systems).
     */
    std::unique_ptr<ISVOStructure> inject(
        const SparseVoxelInput& input,
        const InjectionConfig& config = InjectionConfig{});

    /**
     * Inject dense voxel grid.
     * Use for volumetric data (medical scans, fluid sim, fog).
     */
    std::unique_ptr<ISVOStructure> inject(
        const DenseVoxelInput& input,
        const InjectionConfig& config = InjectionConfig{});

    /**
     * Inject procedural voxels via sampler.
     * Samples on-demand during octree construction.
     * Perfect for infinite terrain, noise, SDFs.
     */
    std::unique_ptr<ISVOStructure> inject(
        const IVoxelSampler& sampler,
        const InjectionConfig& config = InjectionConfig{});

    /**
     * Merge voxel data into existing SVO structure.
     * Use for dynamic content updates, scene composition.
     */
    bool merge(
        ISVOStructure& target,
        const SparseVoxelInput& input,
        const InjectionConfig& config = InjectionConfig{});

    bool merge(
        ISVOStructure& target,
        const IVoxelSampler& sampler,
        const InjectionConfig& config = InjectionConfig{});

    /**
     * Set progress callback.
     */
    using ProgressCallback = std::function<void(float progress, const std::string& status)>;
    void setProgressCallback(ProgressCallback callback) {
        m_progressCallback = std::move(callback);
    }

    /**
     * Get injection statistics.
     */
    struct Stats {
        size_t voxelsProcessed = 0;
        size_t leavesCreated = 0;
        size_t emptyVoxelsCulled = 0;
        float buildTimeSeconds = 0.0f;
        size_t memoryUsed = 0;
    };

    const Stats& getLastStats() const { return m_stats; }

private:
    ProgressCallback m_progressCallback;
    Stats m_stats;

    // Implementation helpers
    struct BuildContext;
    std::unique_ptr<ISVOStructure> buildFromSampler(
        const IVoxelSampler& sampler,
        const glm::vec3& min,
        const glm::vec3& max,
        const InjectionConfig& config);
};

/**
 * Utility: Convert mesh to voxels.
 * Useful for hybrid pipelines.
 */
class MeshVoxelizer {
public:
    struct Params {
        int resolution = 256;        // Voxel grid resolution
        bool generateNormals = true; // Compute normals from geometry
        bool generateAO = false;     // Compute ambient occlusion
        int aoSamples = 32;          // AO ray samples
    };

    /**
     * Voxelize mesh into sparse voxels.
     */
    static SparseVoxelInput voxelize(
        const ISVOBuilder::InputGeometry& mesh,
        const Params& params = Params{});

    /**
     * Voxelize mesh into dense grid.
     */
    static DenseVoxelInput voxelizeDense(
        const ISVOBuilder::InputGeometry& mesh,
        const Params& params = Params{});
};

/**
 * Utility: Common SDF primitives for testing/demos.
 */
namespace SDF {
    float sphere(const glm::vec3& p, float radius);
    float box(const glm::vec3& p, const glm::vec3& size);
    float torus(const glm::vec3& p, float majorRadius, float minorRadius);
    float cylinder(const glm::vec3& p, float radius, float height);

    // Operations
    float unionOp(float d1, float d2);
    float subtraction(float d1, float d2);
    float intersection(float d1, float d2);
    float smoothUnion(float d1, float d2, float k);
}

} // namespace SVO
