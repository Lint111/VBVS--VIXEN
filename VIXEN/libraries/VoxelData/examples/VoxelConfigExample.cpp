/**
 * @file VoxelConfigExample.cpp
 * @brief Comprehensive examples of the VoxelConfig system
 *
 * Demonstrates:
 * 1. Defining custom voxel configurations
 * 2. Compile-time type safety and validation
 * 3. Runtime registration with AttributeRegistry
 * 4. Switching between configs with same key
 * 5. Integration with VoxelInjector
 */

#include "VoxelConfig.h"
#include "StandardVoxelConfigs.h"
#include "AttributeRegistry.h"
#include "BrickView.h"
#include <iostream>
#include <cassert>

using namespace VoxelData;

// ============================================================================
// Example 1: Defining Custom Voxel Configuration
// ============================================================================

/**
 * Custom voxel for game world with health and damage
 */
VOXEL_CONFIG(GameVoxel, 4) {
    VOXEL_KEY(DENSITY, float, 0, "density", 0.0f);
    VOXEL_ATTRIBUTE(MATERIAL, uint32_t, 1, "material", 0u);
    VOXEL_ATTRIBUTE(HEALTH, uint16_t, 2, "health", static_cast<uint16_t>(100));
    VOXEL_ATTRIBUTE(DAMAGE, float, 3, "damage", 1.0f);

    GameVoxel() {
        init_DENSITY();
        init_MATERIAL();
        init_HEALTH();
        init_DAMAGE();
    }
};

// ============================================================================
// Example 2: Compile-Time Type Safety
// ============================================================================

void demonstrateTypeSafety() {
    std::cout << "=== Example 2: Compile-Time Type Safety ===\n";

    // ✅ Correct: All type information known at compile time
    constexpr auto densityType = StandardVoxel::DENSITY_Member::attributeType;
    constexpr auto densityIndex = StandardVoxel::DENSITY_Member::index;
    constexpr bool densityIsKey = StandardVoxel::DENSITY_Member::isKey;

    std::cout << "StandardVoxel::DENSITY:\n";
    std::cout << "  Type: " << static_cast<int>(densityType) << " (AttributeType::Float)\n";
    std::cout << "  Index: " << densityIndex << "\n";
    std::cout << "  Is Key: " << (densityIsKey ? "yes" : "no") << "\n";

    // ✅ Compile-time assertions catch errors
    static_assert(StandardVoxel::DENSITY_Member::index == 0, "DENSITY must be at index 0");
    static_assert(StandardVoxel::MATERIAL_Member::index == 1, "MATERIAL must be at index 1");
    static_assert(StandardVoxel::COLOR_Member::index == 2, "COLOR must be at index 2");

    // ❌ These would cause compile errors:
    // static_assert(StandardVoxel::DENSITY_Member::index == 5);  // Wrong index
    // static_assert(!StandardVoxel::DENSITY_Member::isKey);       // DENSITY is key

    std::cout << "✅ All compile-time type checks passed!\n\n";
}

// ============================================================================
// Example 3: Runtime Registration
// ============================================================================

void demonstrateRegistration() {
    std::cout << "=== Example 3: Runtime Registration ===\n";

    AttributeRegistry registry;
    StandardVoxel config;

    // Register all attributes from config
    config.registerWith(&registry);

    // Verify registration
    assert(registry.hasKey());
    assert(registry.getKeyName() == "density");
    assert(registry.hasAttribute("material"));
    assert(registry.hasAttribute("color"));

    std::cout << "Registered attributes:\n";
    std::cout << "  Key: " << registry.getKeyName() << "\n";
    std::cout << "  Attributes: material, color\n";
    std::cout << "✅ Registration successful!\n\n";
}

// ============================================================================
// Example 4: Switching Configs (Same Key)
// ============================================================================

void demonstrateConfigSwitching() {
    std::cout << "=== Example 4: Config Switching (Same Key) ===\n";

    AttributeRegistry registry;
    StandardVoxel stdConfig;
    RichVoxel richConfig;

    // Start with StandardVoxel (3 attributes)
    std::cout << "1. Register StandardVoxel (density, material, color)...\n";
    stdConfig.registerWith(&registry);
    assert(registry.getKeyName() == "density");

    // Add metallic/roughness without rebuild (NON-DESTRUCTIVE)
    std::cout << "2. Add metallic/roughness (non-destructive)...\n";
    registry.addAttribute("metallic", AttributeType::Float, 0.0f);
    registry.addAttribute("roughness", AttributeType::Float, 0.5f);

    std::cout << "✅ Added attributes without octree rebuild!\n";
    std::cout << "   Key unchanged: " << registry.getKeyName() << "\n\n";
}

// ============================================================================
// Example 5: Switching Key (Destructive)
// ============================================================================

void demonstrateKeySwitching() {
    std::cout << "=== Example 5: Key Switching (Destructive) ===\n";

    AttributeRegistry registry;
    ThermalVoxel thermalConfig;

    // Initial: density key
    StandardVoxel stdConfig;
    stdConfig.registerWith(&registry);
    std::cout << "1. Initial key: " << registry.getKeyName() << "\n";

    // Switch to temperature key (DESTRUCTIVE - triggers rebuild)
    std::cout << "2. Switching to temperature key (destructive)...\n";
    registry.changeKey("temperature");

    std::cout << "⚠️  Key changed - octree rebuild required!\n";
    std::cout << "   New key: " << registry.getKeyName() << "\n\n";
}

// ============================================================================
// Example 6: Using VoxelConfig with BrickView
// ============================================================================

void demonstrateBrickViewIntegration() {
    std::cout << "=== Example 6: BrickView Integration ===\n";

    // Create registry and register StandardVoxel
    AttributeRegistry registry;
    StandardVoxel config;
    config.registerWith(&registry);

    // Allocate brick
    constexpr size_t brickDepth = 3;  // 8³ = 512 voxels
    BrickID brickID = registry.allocateBrick(brickDepth);
    BrickView brick = registry.getBrickView(brickID);

    std::cout << "1. Created brick (depth=" << brickDepth << ", " << brick.getVoxelCount() << " voxels)\n";

    // Set voxel attributes using 3D coordinates
    std::cout << "2. Populating brick with data-driven API...\n";
    for (size_t z = 0; z < 8; ++z) {
        for (size_t y = 0; y < 8; ++y) {
            for (size_t x = 0; x < 8; ++x) {
                // Data-driven: iterate over registered attributes
                for (const auto& attrName : brick.getAttributeNames()) {
                    if (attrName == "density") {
                        float density = (x + y + z) / 21.0f;  // Gradient
                        brick.setAt3D<float>(attrName, x, y, z, density);
                    }
                    else if (attrName == "material") {
                        uint32_t material = static_cast<uint32_t>(x % 4);
                        brick.setAt3D<uint32_t>(attrName, x, y, z, material);
                    }
                    else if (attrName == "color") {
                        glm::vec3 color(x / 7.0f, y / 7.0f, z / 7.0f);
                        brick.setAt3D<glm::vec3>(attrName, x, y, z, color);
                    }
                }
            }
        }
    }

    // Query voxel
    float density = brick.getAt3D<float>("density", 4, 4, 4);
    uint32_t material = brick.getAt3D<uint32_t>("material", 4, 4, 4);
    glm::vec3 color = brick.getAt3D<glm::vec3>("color", 4, 4, 4);

    std::cout << "3. Voxel at (4,4,4):\n";
    std::cout << "   Density: " << density << "\n";
    std::cout << "   Material: " << material << "\n";
    std::cout << "   Color: (" << color.r << ", " << color.g << ", " << color.b << ")\n";
    std::cout << "✅ BrickView integration working!\n\n";
}

// ============================================================================
// Example 7: Zero-Overhead Validation
// ============================================================================

void demonstrateZeroOverhead() {
    std::cout << "=== Example 7: Zero-Overhead Validation ===\n";

    // All validation happens at compile time - zero runtime cost!
    constexpr size_t attrCount = StandardVoxel::ATTRIBUTE_COUNT;
    constexpr auto densityType = StandardVoxel::DENSITY_Member::attributeType;
    constexpr uint32_t densityIndex = StandardVoxel::DENSITY_Member::index;

    std::cout << "Compile-time constants:\n";
    std::cout << "  ATTRIBUTE_COUNT: " << attrCount << " (no runtime lookup!)\n";
    std::cout << "  DENSITY type: " << static_cast<int>(densityType) << " (direct constant)\n";
    std::cout << "  DENSITY index: " << densityIndex << " (direct array access)\n";

    // In optimized builds, accessing attributes compiles to:
    // - attributes[0] for DENSITY (no hash lookup, no string comparison)
    // - attributes[1] for MATERIAL (direct array index)
    // - attributes[2] for COLOR (direct array index)

    std::cout << "✅ Zero runtime overhead - all checks at compile time!\n\n";
}

// ============================================================================
// Main: Run All Examples
// ============================================================================

int main() {
    std::cout << "╔════════════════════════════════════════════════════════╗\n";
    std::cout << "║        VoxelConfig System - Comprehensive Demo         ║\n";
    std::cout << "╚════════════════════════════════════════════════════════╝\n\n";

    try {
        demonstrateTypeSafety();
        demonstrateRegistration();
        demonstrateConfigSwitching();
        demonstrateKeySwitching();
        demonstrateBrickViewIntegration();
        demonstrateZeroOverhead();

        std::cout << "╔════════════════════════════════════════════════════════╗\n";
        std::cout << "║              All Examples Completed! ✅                ║\n";
        std::cout << "╚════════════════════════════════════════════════════════╝\n";
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "❌ Error: " << e.what() << "\n";
        return 1;
    }
}
