// Example usage of DynamicVoxelScalar iterator
// This demonstrates the intended pattern for GaiaVoxelWorld::createVoxel

#include "pch.h"
#include "GaiaVoxelWorld.h"
#include "DynamicVoxelStruct.h"

// Simplified example showing how the iterator works
void exampleIteratorUsage() {
    VoxelData::DynamicVoxelScalar voxel;
    voxel.set("density", 1.0f);
    voxel.set("color_r", 0.5f);
    voxel.set("color_g", 0.3f);
    voxel.set("color_b", 0.8f);

    // Range-based for loop using the iterator
    for (const auto& attr : voxel) {
        // attr is DynamicVoxelScalar::AttributeEntry
        // - attr.name (std::string)
        // - attr.value (std::any)
        // - attr.getType() -> AttributeType
        // - attr.get<T>() -> T

        switch (attr.getType()) {
            case VoxelData::AttributeType::Float: {
                float val = attr.get<float>();
                std::cout << attr.name << " (Float): " << val << "\n";
                break;
            }
            case VoxelData::AttributeType::Uint32: {
                uint32_t val = attr.get<uint32_t>();
                std::cout << attr.name << " (Uint32): " << val << "\n";
                break;
            }
            case VoxelData::AttributeType::Vec3: {
                glm::vec3 val = attr.get<glm::vec3>();
                std::cout << attr.name << " (Vec3): ("
                          << val.x << ", " << val.y << ", " << val.z << ")\n";
                break;
            }
            default:
                break;
        }
    }
}

// Example implementation for GaiaVoxelWorld::createVoxel
GaiaVoxelWorld::EntityID createVoxelExample(
    gaia::ecs::World& world,
    const glm::vec3& position,
    const VoxelData::DynamicVoxelScalar& data) {

    auto entity = world.add();
    world.add<MortonKey>(entity, MortonKey::fromPosition(position));

    // Iterate through all attributes
    for (const auto& attr : data) {
        switch (attr.getType()) {
            case VoxelData::AttributeType::Float: {
                float val = attr.get<float>();
                if (attr.name == "density") {
                    world.add<Density>(entity, {val});
                }
                // ... etc for other float attributes
                break;
            }
            case VoxelData::AttributeType::Uint32: {
                uint32_t val = attr.get<uint32_t>();
                if (attr.name == "material") {
                    world.add<Material>(entity, {val});
                }
                break;
            }
            case VoxelData::AttributeType::Vec3: {
                glm::vec3 val = attr.get<glm::vec3>();
                // Use multi-member components directly
                if (attr.name == "color") {
                    world.add<Color>(entity, Color(val));
                } else if (attr.name == "normal") {
                    world.add<Normal>(entity, Normal(val));
                }
                break;
            }
            default:
                break;
        }
    }

    return entity;
}
