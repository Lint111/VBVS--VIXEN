#pragma once

#include "VoxelConfig.h"
#include <glm/glm.hpp>

namespace VoxelData {

// ============================================================================
// Standard Voxel Configurations
// ============================================================================

/**
 * @brief Basic voxel with density and material
 *
 * Minimal voxel for simple SDF scenes.
 * Key: density (determines octree structure)
 *
 * Attributes:
 * - density (float): 0.0 = empty, 1.0 = solid
 * - material (uint32_t): Material ID for shading
 */
#define BASIC_VOXEL_ATTRIBUTES(X) \
    X(KEY,       DENSITY,  float,    0) \
    X(ATTRIBUTE, MATERIAL, uint32_t, 1)

VOXEL_CONFIG(BasicVoxel, 2, BASIC_VOXEL_ATTRIBUTES)

/**
 * @brief Standard voxel with density, material, and color
 *
 * Most common voxel type for colored scenes.
 * Key: density (determines octree structure)
 *
 * Attributes:
 * - density (float): 0.0 = empty, 1.0 = solid
 * - material (uint32_t): Material ID for shading
 * - color (glm::vec3): RGB color (3 separate arrays)
 */
#define STANDARD_VOXEL_ATTRIBUTES(X) \
    X(KEY,       DENSITY,  float,     0) \
    X(ATTRIBUTE, MATERIAL, uint32_t,  1) \
    X(ATTRIBUTE, COLOR,    glm::vec3, 2)

VOXEL_CONFIG(StandardVoxel, 3, STANDARD_VOXEL_ATTRIBUTES)

/**
 * @brief Rich voxel with full material properties
 *
 * For PBR rendering with normal maps and metallic/roughness.
 * Key: density (determines octree structure)
 *
 * Attributes:
 * - density (float): 0.0 = empty, 1.0 = solid
 * - material (uint32_t): Material ID for shading
 * - color (glm::vec3): RGB albedo
 * - normal (glm::vec3): Packed normal vector
 * - metallic (float): Metallic factor (0.0 = dielectric, 1.0 = metal)
 * - roughness (float): Roughness factor (0.0 = smooth, 1.0 = rough)
 */
#define RICH_VOXEL_ATTRIBUTES(X) \
    X(KEY,       DENSITY,   float,     0) \
    X(ATTRIBUTE, MATERIAL,  uint32_t,  1) \
    X(ATTRIBUTE, COLOR,     glm::vec3, 2, glm::vec3(1.0f)) \
    X(ATTRIBUTE, NORMAL,    glm::vec3, 3, glm::vec3(0.0f, 1.0f, 0.0f)) \
    X(ATTRIBUTE, METALLIC,  float,     4) \
    X(ATTRIBUTE, ROUGHNESS, float,     5, 0.5f)

VOXEL_CONFIG(RichVoxel, 6, RICH_VOXEL_ATTRIBUTES)

/**
 * @brief Temperature-based voxel for simulation
 *
 * Demonstrates switching key attribute.
 * Key: temperature (determines octree structure based on heat)
 *
 * Use case: Thermal simulations where spatial structure follows temperature gradients
 *
 * Attributes:
 * - temperature (float): Temperature value (key)
 * - density (float): Mass density
 * - material (uint32_t): Material type
 */
#define THERMAL_VOXEL_ATTRIBUTES(X) \
    X(KEY,       TEMPERATURE, float,    0) \
    X(ATTRIBUTE, DENSITY,     float,    1) \
    X(ATTRIBUTE, MATERIAL,    uint32_t, 2)

VOXEL_CONFIG(ThermalVoxel, 3, THERMAL_VOXEL_ATTRIBUTES)

/**
 * @brief Compact voxel with 8-bit material only
 *
 * Minimal memory footprint for large-scale scenes.
 * Key: material (non-zero = solid)
 *
 * Use case: Minecraft-like voxel worlds where material ID determines solidity
 *
 * Attributes:
 * - material (uint8_t): Material ID (0 = empty, >0 = solid)
 */
#define COMPACT_VOXEL_ATTRIBUTES(X) \
    X(KEY, MATERIAL, uint8_t, 0)

VOXEL_CONFIG(CompactVoxel, 1, COMPACT_VOXEL_ATTRIBUTES)

/**
 * @brief Test voxel with position, density, color, normal, and occlusion
 *
 * For legacy test compatibility. Includes position (spatial metadata) and occlusion.
 * Key: density (determines octree structure)
 *
 * Attributes:
 * - density (float): 0.0 = empty, 1.0 = solid
 * - color (glm::vec3): RGB color
 * - normal (glm::vec3): Normal vector
 * - occlusion (float): Ambient occlusion factor
 *
 * Note: position field is for test convenience only (spatial metadata, not stored)
 */
#define TEST_VOXEL_ATTRIBUTES(X) \
    X(KEY,       DENSITY,   float,     0) \
    X(ATTRIBUTE, COLOR,     glm::vec3, 1) \
    X(ATTRIBUTE, NORMAL,    glm::vec3, 2) \
    X(ATTRIBUTE, OCCLUSION, float,     3)

VOXEL_CONFIG(TestVoxel, 4, TEST_VOXEL_ATTRIBUTES)

// ============================================================================
// Runtime Key Switching Example
// ============================================================================

/**
 * @brief Demonstrates runtime switching between configs with same key
 *
 * Both StandardVoxel and RichVoxel use "density" as key.
 * Can switch between them without rebuilding octree (non-destructive).
 *
 * Example:
 * ```cpp
 * AttributeRegistry registry;
 * StandardVoxel stdConfig;
 * RichVoxel richConfig;
 *
 * // Initialize with standard voxel
 * stdConfig.registerWith(&registry);
 * // ... build octree ...
 *
 * // Add metallic/roughness without rebuild (same key!)
 * registry.addAttribute("metallic", AttributeType::Float, 0.0f);
 * registry.addAttribute("roughness", AttributeType::Float, 0.5f);
 * // Octree structure unchanged, shaders updated
 *
 * // To switch to ThermalVoxel (different key):
 * // Must changeKey("temperature") → triggers octree rebuild
 * ```
 */

} // namespace VoxelData
