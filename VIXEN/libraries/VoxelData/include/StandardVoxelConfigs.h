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
VOXEL_CONFIG(BasicVoxel, 2) {
    VOXEL_KEY(DENSITY, float, 0);
    VOXEL_ATTRIBUTE(MATERIAL, uint32_t, 1);
};

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
VOXEL_CONFIG(StandardVoxel, 3) {
    VOXEL_KEY(DENSITY, float, 0);
    VOXEL_ATTRIBUTE(MATERIAL, uint32_t, 1);
    VOXEL_ATTRIBUTE(COLOR, glm::vec3, 2);
};

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
VOXEL_CONFIG(RichVoxel, 6) {
    VOXEL_KEY(DENSITY, float, 0);
    VOXEL_ATTRIBUTE(MATERIAL, uint32_t, 1);
    VOXEL_ATTRIBUTE(COLOR, glm::vec3, 2, glm::vec3(1.0f));  // Custom: white default
    VOXEL_ATTRIBUTE(NORMAL, glm::vec3, 3, glm::vec3(0.0f, 1.0f, 0.0f));  // Custom: up vector
    VOXEL_ATTRIBUTE(METALLIC, float, 4);
    VOXEL_ATTRIBUTE(ROUGHNESS, float, 5, 0.5f);  // Custom: mid-roughness
};

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
VOXEL_CONFIG(ThermalVoxel, 3) {
    VOXEL_KEY(TEMPERATURE, float, 0);
    VOXEL_ATTRIBUTE(DENSITY, float, 1);
    VOXEL_ATTRIBUTE(MATERIAL, uint32_t, 2);
};

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
VOXEL_CONFIG(CompactVoxel, 1) {
    VOXEL_KEY(MATERIAL, uint8_t, 0);
};

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
