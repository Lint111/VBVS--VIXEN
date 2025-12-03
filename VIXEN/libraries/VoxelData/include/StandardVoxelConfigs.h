#pragma once

#include "VoxelConfig.h"
#include <VoxelComponents.h>  // Canonical component definitions
#include <glm/glm.hpp>

namespace  Vixen::VoxelData {

// ============================================================================
// Component-Based Voxel Configurations
// ============================================================================
// Uses VoxelComponents directly - no string-based naming
// Component types enforce single source of truth

/**
 * @brief Basic voxel with density and material
 *
 * Minimal voxel for simple SDF scenes.
 * Key: Density component (determines octree structure)
 */
#define BASIC_VOXEL_COMPONENTS(X) \
    X(KEY,       GaiaVoxel::Density,  0) \
    X(ATTRIBUTE, GaiaVoxel::Material, 1)

VOXEL_CONFIG(BasicVoxel, 2, BASIC_VOXEL_COMPONENTS)

/**
 * @brief Standard voxel with density, material, and color
 *
 * Most common voxel type for colored scenes.
 * Key: Density component (determines octree structure)
 */
#define STANDARD_VOXEL_COMPONENTS(X) \
    X(KEY,       GaiaVoxel::Density,  0) \
    X(ATTRIBUTE, GaiaVoxel::Material, 1) \
    X(ATTRIBUTE, GaiaVoxel::Color,    2)

VOXEL_CONFIG(StandardVoxel, 3, STANDARD_VOXEL_COMPONENTS)

/**
 * @brief Rich voxel with full material properties
 *
 * For PBR rendering with normal maps and metallic/roughness.
 * Key: Density component (determines octree structure)
 *
 * Components:
 * - Density: 0.0 = empty, 1.0 = solid
 * - Material: Material ID for shading
 * - Color: RGB albedo (multi-member {r,g,b})
 * - Normal: Packed normal vector (multi-member {x,y,z})
 * - EmissionIntensity: Emissive strength
 */
#define RICH_VOXEL_COMPONENTS(X) \
    X(KEY,       GaiaVoxel::Density,           0) \
    X(ATTRIBUTE, GaiaVoxel::Material,          1) \
    X(ATTRIBUTE, GaiaVoxel::Color,             2) \
    X(ATTRIBUTE, GaiaVoxel::Normal,            3) \
    X(ATTRIBUTE, GaiaVoxel::EmissionIntensity, 4)

VOXEL_CONFIG(RichVoxel, 5, RICH_VOXEL_COMPONENTS)


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
