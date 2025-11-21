/**
 * @file DynamicVoxelExample.cpp
 * @brief Example demonstrating DynamicVoxelStruct initialization from VoxelConfig
 */

#include "DynamicVoxelStruct.h"
#include "StandardVoxelConfigs.h"
#include "AttributeRegistry.h"
#include <iostream>
#include <cassert>

using namespace VoxelData;

// ============================================================================
// Example 1: Initialize DynamicVoxelScalar from VoxelConfig
// ============================================================================

void demonstrateScalarInit() {
    std::cout << "=== Example 1: DynamicVoxelScalar from VoxelConfig ===\n";

    // Create config
    StandardVoxel config;

    // Initialize scalar directly from config
    DynamicVoxelScalar voxel(&config);

    // Verify attributes were initialized
    assert(voxel.has("density"));
    assert(voxel.has("material"));
    assert(voxel.has("color"));

    std::cout << "✅ DynamicVoxelScalar initialized with:\n";
    for (const auto& name : voxel.getAttributeNames()) {
        std::cout << "   - " << name << "\n";
    }

    // Set values
    voxel.set("density", 0.8f);
    voxel.set("material", 42u);
    voxel.set("color", glm::vec3(1.0f, 0.5f, 0.2f));

    // Get values
    float density = voxel.get<float>("density");
    uint32_t material = voxel.get<uint32_t>("material");
    glm::vec3 color = voxel.get<glm::vec3>("color");

    std::cout << "✅ Values:\n";
    std::cout << "   density: " << density << "\n";
    std::cout << "   material: " << material << "\n";
    std::cout << "   color: (" << color.r << ", " << color.g << ", " << color.b << ")\n\n";
}

// ============================================================================
// Example 2: Initialize DynamicVoxelArrays from VoxelConfig
// ============================================================================

void demonstrateArraysInit() {
    std::cout << "=== Example 2: DynamicVoxelArrays from VoxelConfig ===\n";

    // Create config
    RichVoxel config;

    // Initialize arrays directly from config
    DynamicVoxelArrays batch(&config);

    // Verify arrays were created
    assert(batch.has("density"));
    assert(batch.has("material"));
    assert(batch.has("color"));
    assert(batch.has("normal"));
    assert(batch.has("metallic"));
    assert(batch.has("roughness"));

    std::cout << "✅ DynamicVoxelArrays initialized with:\n";
    for (const auto& name : batch.getAttributeNames()) {
        std::cout << "   - " << name << "\n";
    }

    // Reserve capacity
    batch.reserve(100);

    // Add voxels
    DynamicVoxelScalar voxel1;
    voxel1.set("density", 1.0f);
    voxel1.set("material", 1u);
    voxel1.set("color", glm::vec3(1.0f, 0.0f, 0.0f));
    voxel1.set("normal", glm::vec3(0.0f, 1.0f, 0.0f));
    voxel1.set("metallic", 0.0f);
    voxel1.set("roughness", 0.5f);
    batch.push_back(voxel1);

    DynamicVoxelScalar voxel2;
    voxel2.set("density", 0.8f);
    voxel2.set("material", 2u);
    voxel2.set("color", glm::vec3(0.0f, 1.0f, 0.0f));
    voxel2.set("normal", glm::vec3(0.0f, 1.0f, 0.0f));
    voxel2.set("metallic", 0.8f);
    voxel2.set("roughness", 0.2f);
    batch.push_back(voxel2);

    std::cout << "✅ Added " << batch.count() << " voxels\n";

    // Query voxel
    DynamicVoxelScalar queried = batch[0];
    float density = queried.get<float>("density");
    glm::vec3 color = queried.get<glm::vec3>("color");

    std::cout << "✅ Voxel[0]:\n";
    std::cout << "   density: " << density << "\n";
    std::cout << "   color: (" << color.r << ", " << color.g << ", " << color.b << ")\n\n";
}

// ============================================================================
// Example 3: Automatic Synchronization with Registry
// ============================================================================

void demonstrateAutoSync() {
    std::cout << "=== Example 3: Automatic Synchronization ===\n";

    // 1. Create registry and dynamic structs
    AttributeRegistry registry;
    DynamicVoxelArrays batch(&registry);
    DynamicVoxelScalar singleVoxel(&registry);

    // 2. Create observer to keep structs synced
    auto observer = std::make_unique<DynamicVoxelSyncObserver>(&registry);
    observer->registerArrays(&batch);
    observer->registerScalar(&singleVoxel);
    registry.addObserver(observer.get());

    // 3. Register initial schema
    StandardVoxel config;
    config.registerWith(&registry);

    std::cout << "1. Initial schema registered (density, material, color)\n";
    assert(batch.has("density"));
    assert(batch.has("material"));
    assert(batch.has("color"));
    assert(singleVoxel.has("density"));

    // 4. ADD attribute at runtime → auto-syncs!
    std::cout << "2. Adding 'metallic' attribute...\n";
    registry.addAttribute("metallic", AttributeType::Float, 0.0f);

    assert(batch.has("metallic"));
    assert(singleVoxel.has("metallic"));
    std::cout << "✅ batch now has metallic array!\n";
    std::cout << "✅ singleVoxel now has metallic field!\n";

    // 5. REMOVE attribute at runtime → auto-syncs!
    std::cout << "3. Removing 'material' attribute...\n";
    registry.removeAttribute("material");

    assert(!batch.has("material"));
    assert(!singleVoxel.has("material"));
    std::cout << "✅ batch.material array removed!\n";
    std::cout << "✅ singleVoxel.material field removed!\n\n";

    // Clean up observer before registry/structs are destroyed
    registry.removeObserver(observer.get());
}

// ============================================================================
// Example 4: Different Config Types
// ============================================================================

void demonstrateDifferentConfigs() {
    std::cout << "=== Example 4: Different Config Types ===\n";

    // BasicVoxel (minimal)
    BasicVoxel basicConfig;
    DynamicVoxelScalar basicVoxel(&basicConfig);
    std::cout << "BasicVoxel attributes:\n";
    for (const auto& name : basicVoxel.getAttributeNames()) {
        std::cout << "   - " << name << "\n";
    }

    // RichVoxel (full PBR)
    RichVoxel richConfig;
    DynamicVoxelScalar richVoxel(&richConfig);
    std::cout << "RichVoxel attributes:\n";
    for (const auto& name : richVoxel.getAttributeNames()) {
        std::cout << "   - " << name << "\n";
    }

    // ThermalVoxel (different key)
    ThermalVoxel thermalConfig;
    DynamicVoxelScalar thermalVoxel(&thermalConfig);
    std::cout << "ThermalVoxel attributes:\n";
    for (const auto& name : thermalVoxel.getAttributeNames()) {
        std::cout << "   - " << name << "\n";
    }

    std::cout << "✅ All config types work!\n\n";
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "╔════════════════════════════════════════════════════════╗\n";
    std::cout << "║      DynamicVoxelStruct - VoxelConfig Integration      ║\n";
    std::cout << "╚════════════════════════════════════════════════════════╝\n\n";

    try {
        demonstrateScalarInit();
        demonstrateArraysInit();
        demonstrateAutoSync();
        demonstrateDifferentConfigs();

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
