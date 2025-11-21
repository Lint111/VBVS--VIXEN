#pragma once

#include "StandardVoxelConfigs.h"
#include <glm/glm.hpp>
#include <vector>

namespace VoxelData {

// ============================================================================
// BasicVoxel Data Structures
// ============================================================================

/**
 * @brief Single basic voxel (scalar)
 */
struct BasicVoxelScalar {
    float density = 0.0f;
    uint32_t material = 0u;

    BasicVoxelScalar() = default;
    BasicVoxelScalar(float d, uint32_t m) : density(d), material(m) {}
};

/**
 * @brief Batch of basic voxels (arrays/SoA)
 */
struct BasicVoxelArrays {
    std::vector<float> density;
    std::vector<uint32_t> material;

    size_t count() const { return density.size(); }

    void reserve(size_t capacity) {
        density.reserve(capacity);
        material.reserve(capacity);
    }

    void push_back(const BasicVoxelScalar& voxel) {
        density.push_back(voxel.density);
        material.push_back(voxel.material);
    }

    BasicVoxelScalar operator[](size_t index) const {
        return BasicVoxelScalar(density[index], material[index]);
    }

    void set(size_t index, const BasicVoxelScalar& voxel) {
        density[index] = voxel.density;
        material[index] = voxel.material;
    }
};

// ============================================================================
// StandardVoxel Data Structures
// ============================================================================

/**
 * @brief Single standard voxel (scalar)
 */
struct StandardVoxelScalar {
    float density = 0.0f;
    uint32_t material = 0u;
    glm::vec3 color = glm::vec3(0.0f);

    StandardVoxelScalar() = default;
    StandardVoxelScalar(float d, uint32_t m, glm::vec3 c)
        : density(d), material(m), color(c) {}
};

/**
 * @brief Batch of standard voxels (arrays/SoA)
 */
struct StandardVoxelArrays {
    std::vector<float> density;
    std::vector<uint32_t> material;
    std::vector<glm::vec3> color;

    size_t count() const { return density.size(); }

    void reserve(size_t capacity) {
        density.reserve(capacity);
        material.reserve(capacity);
        color.reserve(capacity);
    }

    void push_back(const StandardVoxelScalar& voxel) {
        density.push_back(voxel.density);
        material.push_back(voxel.material);
        color.push_back(voxel.color);
    }

    StandardVoxelScalar operator[](size_t index) const {
        return StandardVoxelScalar(
            density[index],
            material[index],
            color[index]
        );
    }

    void set(size_t index, const StandardVoxelScalar& voxel) {
        density[index] = voxel.density;
        material[index] = voxel.material;
        color[index] = voxel.color;
    }
};

// ============================================================================
// RichVoxel Data Structures
// ============================================================================

/**
 * @brief Single rich voxel (scalar)
 */
struct RichVoxelScalar {
    float density = 0.0f;
    uint32_t material = 0u;
    glm::vec3 color = glm::vec3(1.0f);
    glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
    float metallic = 0.0f;
    float roughness = 0.5f;

    RichVoxelScalar() = default;
    RichVoxelScalar(float d, uint32_t m, glm::vec3 c, glm::vec3 n, float met, float rough)
        : density(d), material(m), color(c), normal(n), metallic(met), roughness(rough) {}
};

/**
 * @brief Batch of rich voxels (arrays/SoA)
 */
struct RichVoxelArrays {
    std::vector<float> density;
    std::vector<uint32_t> material;
    std::vector<glm::vec3> color;
    std::vector<glm::vec3> normal;
    std::vector<float> metallic;
    std::vector<float> roughness;

    size_t count() const { return density.size(); }

    void reserve(size_t capacity) {
        density.reserve(capacity);
        material.reserve(capacity);
        color.reserve(capacity);
        normal.reserve(capacity);
        metallic.reserve(capacity);
        roughness.reserve(capacity);
    }

    void push_back(const RichVoxelScalar& voxel) {
        density.push_back(voxel.density);
        material.push_back(voxel.material);
        color.push_back(voxel.color);
        normal.push_back(voxel.normal);
        metallic.push_back(voxel.metallic);
        roughness.push_back(voxel.roughness);
    }

    RichVoxelScalar operator[](size_t index) const {
        return RichVoxelScalar(
            density[index],
            material[index],
            color[index],
            normal[index],
            metallic[index],
            roughness[index]
        );
    }

    void set(size_t index, const RichVoxelScalar& voxel) {
        density[index] = voxel.density;
        material[index] = voxel.material;
        color[index] = voxel.color;
        normal[index] = voxel.normal;
        metallic[index] = voxel.metallic;
        roughness[index] = voxel.roughness;
    }
};

// ============================================================================
// Helper: Convert Scalar to BrickView setters
// ============================================================================

/**
 * @brief Populate brick from scalar voxel using config-driven approach
 *
 * Example:
 * ```cpp
 * StandardVoxelScalar voxel(1.0f, 2u, glm::vec3(1,0,0));
 * PopulateBrickFromScalar(brick, x, y, z, voxel);
 * ```
 */
inline void PopulateBrickFromScalar(BrickView& brick, size_t x, size_t y, size_t z,
                                    const StandardVoxelScalar& voxel) {
    brick.setAt3D<float>("density", x, y, z, voxel.density);
    brick.setAt3D<uint32_t>("material", x, y, z, voxel.material);
    brick.setAt3D<glm::vec3>("color", x, y, z, voxel.color);
}

inline void PopulateBrickFromScalar(BrickView& brick, size_t x, size_t y, size_t z,
                                    const RichVoxelScalar& voxel) {
    brick.setAt3D<float>("density", x, y, z, voxel.density);
    brick.setAt3D<uint32_t>("material", x, y, z, voxel.material);
    brick.setAt3D<glm::vec3>("color", x, y, z, voxel.color);
    brick.setAt3D<glm::vec3>("normal", x, y, z, voxel.normal);
    brick.setAt3D<float>("metallic", x, y, z, voxel.metallic);
    brick.setAt3D<float>("roughness", x, y, z, voxel.roughness);
}

} // namespace VoxelData
