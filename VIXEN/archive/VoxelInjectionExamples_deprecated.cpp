/**
 * Examples demonstrating voxel data injection into SVO structures.
 * Use cases: procedural generation, terrain, noise, SDFs, etc.
 */

#include "pch.h"
#include <VoxelInjection.h>
#include <LaineKarrasOctree.h>
#include <iostream>

using namespace Vixen::SVO;

// ============================================================================
// Example 1: Simple Procedural Noise Terrain
// ============================================================================

void example1_NoiseTerrainSimple() {
    std::cout << "Example 1: Simple noise terrain\n";

    // Create noise sampler
    Samplers::NoiseSampler::Params noiseParams;
    noiseParams.frequency = 0.05f;
    noiseParams.amplitude = 50.0f;
    noiseParams.octaves = 4;
    noiseParams.threshold = 0.0f;  // Below zero = solid

    auto noiseSampler = std::make_unique<Samplers::NoiseSampler>(noiseParams);

    // Configure injection
    InjectionConfig config;
    config.maxLevels = 12;  // ~0.02m voxels at 100m scale
    config.enableContours = true;
    config.enableLOD = true;

    // Inject into SVO
    VoxelInjector injector;
    injector.setProgressCallback([](float progress, const std::string& status) {
        std::cout << "  Progress: " << (progress * 100.0f) << "% - " << status << "\n";
    });

    auto svo = injector.inject(*noiseSampler, config);

    std::cout << "  Generated SVO: " << svo->getStats() << "\n";
}

// ============================================================================
// Example 2: Lambda-Based SDF Sphere
// ============================================================================

void example2_SDFSphere() {
    std::cout << "Example 2: SDF sphere\n";

    glm::vec3 center(0.0f, 0.0f, 0.0f);
    float radius = 10.0f;

    // Create lambda sampler for SDF sphere
    auto sampler = std::make_unique<LambdaVoxelSampler>(
        // Sample function
        [center, radius](const glm::vec3& p, VoxelData& data) -> bool {
            float dist = glm::length(p - center) - radius;

            if (dist < 0.0f) {  // Inside sphere
                data.position = p;
                data.density = 1.0f;
                data.color = glm::vec3(0.8f, 0.2f, 0.2f);  // Red
                data.normal = glm::normalize(p - center);
                return true;
            }
            return false;
        },
        // Bounds function
        [center, radius](glm::vec3& min, glm::vec3& max) {
            min = center - glm::vec3(radius);
            max = center + glm::vec3(radius);
        },
        // Density estimate function (optional optimization)
        [center, radius](const glm::vec3& regionCenter, float regionSize) -> float {
            float dist = glm::length(regionCenter - center) - radius;
            if (dist > regionSize) return 0.0f;  // Entirely outside
            if (dist < -regionSize) return 1.0f; // Entirely inside
            return 0.5f;  // Boundary - subdivide
        }
    );

    VoxelInjector injector;
    auto svo = injector.inject(*sampler, InjectionConfig{});

    std::cout << "  SDF sphere voxels: " << svo->getVoxelCount() << "\n";
}

// ============================================================================
// Example 3: Sparse Voxel Injection (Pre-computed Data)
// ============================================================================

void example3_SparseVoxels() {
    std::cout << "Example 3: Sparse voxel data\n";

    SparseVoxelInput input;
    input.worldMin = glm::vec3(-10.0f);
    input.worldMax = glm::vec3(10.0f);
    input.resolution = 256;

    // Generate some procedural voxels (e.g., from particle system)
    for (int i = 0; i < 1000; ++i) {
        VoxelData voxel;
        voxel.position = glm::vec3(
            (rand() / float(RAND_MAX)) * 20.0f - 10.0f,
            (rand() / float(RAND_MAX)) * 20.0f - 10.0f,
            (rand() / float(RAND_MAX)) * 20.0f - 10.0f
        );
        voxel.color = glm::vec3(
            rand() / float(RAND_MAX),
            rand() / float(RAND_MAX),
            rand() / float(RAND_MAX)
        );
        voxel.normal = glm::normalize(voxel.position);
        voxel.density = 1.0f;

        input.voxels.push_back(voxel);
    }

    VoxelInjector injector;
    auto svo = injector.inject(input);

    std::cout << "  Sparse voxels: " << input.voxels.size()
              << " -> SVO voxels: " << svo->getVoxelCount() << "\n";
}

// ============================================================================
// Example 4: Dense Voxel Grid (Volumetric Data)
// ============================================================================

void example4_DenseGrid() {
    std::cout << "Example 4: Dense voxel grid\n";

    DenseVoxelInput input;
    input.worldMin = glm::vec3(0.0f);
    input.worldMax = glm::vec3(100.0f);
    input.resolution = glm::ivec3(64, 64, 64);

    // Allocate grid
    input.voxels.resize(64 * 64 * 64);

    // Fill with procedural fog/smoke
    for (int z = 0; z < 64; ++z) {
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                size_t idx = input.getIndex(x, y, z);

                glm::vec3 pos = input.worldMin + glm::vec3(x, y, z) / 64.0f
                              * (input.worldMax - input.worldMin);

                // Simple fog density falloff
                float centerDist = glm::length(pos - glm::vec3(50.0f));
                float density = std::max(0.0f, 1.0f - centerDist / 30.0f);

                input.voxels[idx].position = pos;
                input.voxels[idx].density = density;
                input.voxels[idx].color = glm::vec3(0.8f, 0.8f, 0.9f);  // White-blue
                input.voxels[idx].normal = glm::vec3(0, 1, 0);
            }
        }
    }

    InjectionConfig config;
    config.enableLOD = true;  // Important for volumetric data

    VoxelInjector injector;
    auto svo = injector.inject(input, config);

    std::cout << "  Dense grid: " << input.voxels.size()
              << " -> SVO voxels: " << svo->getVoxelCount() << "\n";
}

// ============================================================================
// Example 5: CSG Operations with SDFs
// ============================================================================

void example5_CSGOperations() {
    std::cout << "Example 5: CSG operations\n";

    // Create sampler for CSG: sphere - box (subtraction)
    auto sampler = std::make_unique<LambdaVoxelSampler>(
        [](const glm::vec3& p, VoxelData& data) -> bool {
            // SDF for sphere
            float sphere = SDF::sphere(p, 10.0f);

            // SDF for box
            float box = SDF::box(p, glm::vec3(6.0f));

            // Subtract box from sphere
            float dist = SDF::subtraction(box, sphere);

            if (dist < 0.0f) {
                data.position = p;
                data.density = 1.0f;
                data.color = glm::vec3(0.3f, 0.7f, 0.3f);  // Green

                // Estimate normal via gradient
                const float eps = 0.01f;
                float dx = SDF::subtraction(
                    SDF::box(p + glm::vec3(eps, 0, 0), glm::vec3(6.0f)),
                    SDF::sphere(p + glm::vec3(eps, 0, 0), 10.0f)
                ) - dist;
                float dy = SDF::subtraction(
                    SDF::box(p + glm::vec3(0, eps, 0), glm::vec3(6.0f)),
                    SDF::sphere(p + glm::vec3(0, eps, 0), 10.0f)
                ) - dist;
                float dz = SDF::subtraction(
                    SDF::box(p + glm::vec3(0, 0, eps), glm::vec3(6.0f)),
                    SDF::sphere(p + glm::vec3(0, 0, eps), 10.0f)
                ) - dist;

                data.normal = glm::normalize(glm::vec3(dx, dy, dz));
                return true;
            }
            return false;
        },
        [](glm::vec3& min, glm::vec3& max) {
            min = glm::vec3(-12.0f);
            max = glm::vec3(12.0f);
        }
    );

    VoxelInjector injector;
    auto svo = injector.inject(*sampler);

    std::cout << "  CSG result voxels: " << svo->getVoxelCount() << "\n";
}

// ============================================================================
// Example 6: Heightmap Terrain
// ============================================================================

void example6_HeightmapTerrain() {
    std::cout << "Example 6: Heightmap terrain\n";

    // Generate simple heightmap
    Samplers::HeightmapSampler::Params params;
    params.width = 256;
    params.height = 256;
    params.minHeight = 0.0f;
    params.maxHeight = 50.0f;
    params.horizontalScale = 1.0f;
    params.baseColor = glm::vec3(0.4f, 0.6f, 0.3f);  // Grassy

    // Generate heightmap data (simple sine waves)
    params.heights.resize(256 * 256);
    for (int y = 0; y < 256; ++y) {
        for (int x = 0; x < 256; ++x) {
            float fx = x / 256.0f * 10.0f;
            float fy = y / 256.0f * 10.0f;
            params.heights[x + y * 256] = std::sin(fx) * std::cos(fy) * 0.5f + 0.5f;
        }
    }

    auto sampler = std::make_unique<Samplers::HeightmapSampler>(params);

    VoxelInjector injector;
    auto svo = injector.inject(*sampler);

    std::cout << "  Heightmap terrain voxels: " << svo->getVoxelCount() << "\n";
}

// ============================================================================
// Example 7: Scene Merging (Dynamic Content)
// ============================================================================

void example7_SceneMerging() {
    std::cout << "Example 7: Scene merging\n";

    // Create base terrain
    auto terrainSampler = std::make_unique<Samplers::NoiseSampler>();
    VoxelInjector injector;
    auto scene = injector.inject(*terrainSampler);

    std::cout << "  Base terrain: " << scene->getVoxelCount() << " voxels\n";

    // Add dynamic objects (e.g., buildings, rocks)
    SparseVoxelInput rocks;
    rocks.worldMin = glm::vec3(-50, 0, -50);
    rocks.worldMax = glm::vec3(50, 20, 50);
    rocks.resolution = 128;

    // Generate some rock voxels
    for (int i = 0; i < 500; ++i) {
        VoxelData rock;
        rock.position = glm::vec3(
            (rand() / float(RAND_MAX)) * 100.0f - 50.0f,
            (rand() / float(RAND_MAX)) * 20.0f,
            (rand() / float(RAND_MAX)) * 100.0f - 50.0f
        );
        rock.color = glm::vec3(0.5f, 0.5f, 0.5f);  // Gray
        rock.normal = glm::vec3(0, 1, 0);
        rock.density = 1.0f;
        rocks.voxels.push_back(rock);
    }

    // Merge rocks into scene
    injector.merge(*scene, rocks);

    std::cout << "  After merging: " << scene->getVoxelCount() << " voxels\n";
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "=== SVO Voxel Injection Examples ===\n\n";

    example1_NoiseTerrainSimple();
    std::cout << "\n";

    example2_SDFSphere();
    std::cout << "\n";

    example3_SparseVoxels();
    std::cout << "\n";

    example4_DenseGrid();
    std::cout << "\n";

    example5_CSGOperations();
    std::cout << "\n";

    example6_HeightmapTerrain();
    std::cout << "\n";

    example7_SceneMerging();
    std::cout << "\n";

    return 0;
}
