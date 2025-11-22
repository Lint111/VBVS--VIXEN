#include "ECSBackedRegistry.h"
#include <glm/glm.hpp>
#include <iostream>

namespace GaiaVoxel {

// ============================================================================
// Constructor
// ============================================================================

ECSBackedRegistry::ECSBackedRegistry(gaia::ecs::World& world)
    : VoxelData::AttributeRegistry()
    , m_world(world) {
    std::cout << "[ECSBackedRegistry] Initialized with Gaia ECS backend\n";
}

// ============================================================================
// Entity ↔ DynamicVoxelScalar Conversion
// ============================================================================

gaia::ecs::Entity ECSBackedRegistry::createEntity(
    const VoxelData::DynamicVoxelScalar& voxel) {

    auto entity = m_world.add();

    // Extract position if present and add MortonKey
    if (voxel.has("position")) {
        try {
            glm::vec3 pos = voxel.get<glm::vec3>("position");
            entity.add<MortonKey>(MortonKey::fromPosition(pos));
        } catch (...) {
            // Position extraction failed, skip MortonKey
        }
    }

    // Map each attribute to ECS component
    for (const auto& attrName : voxel.getAttributeNames()) {
        if (attrName == "position") continue;  // Already handled as MortonKey

        try {
            std::any value = voxel.get<std::any>(attrName);
            addComponentFromAttribute(entity, attrName, value);
        } catch (const std::exception& e) {
            std::cerr << "[ECSBackedRegistry] Failed to add component '" << attrName
                      << "': " << e.what() << "\n";
        }
    }

    return entity;
}

gaia::ecs::Entity ECSBackedRegistry::createEntity(
    const glm::vec3& position,
    const std::unordered_map<std::string, std::any>& attributes) {

    auto entity = m_world.add();

    // Add MortonKey for position
    entity.add<MortonKey>(MortonKey::fromPosition(position));

    // Add each attribute as component
    for (const auto& [name, value] : attributes) {
        addComponentFromAttribute(entity, name, value);
    }

    return entity;
}

VoxelData::DynamicVoxelScalar ECSBackedRegistry::getVoxelFromEntity(
    gaia::ecs::Entity entity) const {

    VoxelData::DynamicVoxelScalar voxel(this);

    if (!entity.valid()) return voxel;

    // Add position if MortonKey exists
    if (entity.has<MortonKey>()) {
        glm::vec3 pos = entity.get<MortonKey>().toWorldPos();
        voxel.set("position", pos);
    }

    // Add each registered attribute
    for (const auto& [name, componentID] : m_nameToComponentID) {
        try {
            std::any value = getComponentAsAny(entity, name);
            voxel.set(name, value);
        } catch (...) {
            // Component not present on this entity, skip
        }
    }

    return voxel;
}

std::optional<glm::vec3> ECSBackedRegistry::getPosition(gaia::ecs::Entity entity) const {
    if (!entity.valid() || !entity.has<MortonKey>()) {
        return std::nullopt;
    }
    return entity.get<MortonKey>().toWorldPos();
}

// ============================================================================
// Batch Operations
// ============================================================================

std::vector<gaia::ecs::Entity> ECSBackedRegistry::createEntitiesBatch(
    const std::vector<VoxelData::DynamicVoxelScalar>& voxels) {

    std::vector<gaia::ecs::Entity> entities;
    entities.reserve(voxels.size());

    for (const auto& voxel : voxels) {
        entities.push_back(createEntity(voxel));
    }

    return entities;
}

void ECSBackedRegistry::destroyEntity(gaia::ecs::Entity entity) {
    if (entity.valid()) {
        m_world.del(entity);
    }
}

// ============================================================================
// Component Query API
// ============================================================================

uint32_t ECSBackedRegistry::getComponentID(const std::string& name) const {
    auto it = m_nameToComponentID.find(name);
    if (it == m_nameToComponentID.end()) {
        throw std::runtime_error("Component not registered: " + name);
    }
    return it->second;
}

const std::string& ECSBackedRegistry::getComponentName(uint32_t componentID) const {
    auto it = m_componentIDToName.find(componentID);
    if (it == m_componentIDToName.end()) {
        static const std::string empty;
        return empty;
    }
    return it->second;
}

bool ECSBackedRegistry::hasAttribute(gaia::ecs::Entity entity, const std::string& name) const {
    if (!entity.valid()) return false;

    auto it = m_nameToComponentID.find(name);
    if (it == m_nameToComponentID.end()) return false;

    // Check if entity has this component
    // NOTE: Gaia ECS requires compile-time component type
    // For runtime check, we need to use component ID
    // This is a limitation - will return false for now
    // TODO: Implement runtime component check via Gaia API
    return false;
}

// ============================================================================
// Helper: Add Component from Attribute
// ============================================================================

void ECSBackedRegistry::addComponentFromAttribute(
    gaia::ecs::Entity entity,
    const std::string& name,
    const std::any& value) {

    // Dispatch based on attribute name
    // This maps string name → compile-time component type

    if (name == "density") {
        entity.add<Density>(Density{std::any_cast<float>(value)});
    }
    else if (name == "color_r") {
        entity.add<Color_R>(Color_R{std::any_cast<float>(value)});
    }
    else if (name == "color_g") {
        entity.add<Color_G>(Color_G{std::any_cast<float>(value)});
    }
    else if (name == "color_b") {
        entity.add<Color_B>(Color_B{std::any_cast<float>(value)});
    }
    else if (name == "normal_x") {
        entity.add<Normal_X>(Normal_X{std::any_cast<float>(value)});
    }
    else if (name == "normal_y") {
        entity.add<Normal_Y>(Normal_Y{std::any_cast<float>(value)});
    }
    else if (name == "normal_z") {
        entity.add<Normal_Z>(Normal_Z{std::any_cast<float>(value)});
    }
    else if (name == "material") {
        entity.add<Material>(Material{std::any_cast<uint32_t>(value)});
    }
    else if (name == "emission_r") {
        entity.add<Emission_R>(Emission_R{std::any_cast<float>(value)});
    }
    else if (name == "emission_g") {
        entity.add<Emission_G>(Emission_G{std::any_cast<float>(value)});
    }
    else if (name == "emission_b") {
        entity.add<Emission_B>(Emission_B{std::any_cast<float>(value)});
    }
    else if (name == "emission_intensity") {
        entity.add<Emission_Intensity>(Emission_Intensity{std::any_cast<float>(value)});
    }
    else {
        std::cerr << "[ECSBackedRegistry] Unknown component: " << name << "\n";
    }
}

// ============================================================================
// Helper: Get Component Value as std::any
// ============================================================================

std::any ECSBackedRegistry::getComponentAsAny(
    gaia::ecs::Entity entity,
    const std::string& name) const {

    if (!entity.valid()) {
        throw std::runtime_error("Invalid entity");
    }

    // Dispatch based on attribute name
    if (name == "density") {
        if (!entity.has<Density>()) throw std::runtime_error("Missing Density");
        return entity.get<Density>().value;
    }
    else if (name == "color_r") {
        if (!entity.has<Color_R>()) throw std::runtime_error("Missing Color_R");
        return entity.get<Color_R>().value;
    }
    else if (name == "color_g") {
        if (!entity.has<Color_G>()) throw std::runtime_error("Missing Color_G");
        return entity.get<Color_G>().value;
    }
    else if (name == "color_b") {
        if (!entity.has<Color_B>()) throw std::runtime_error("Missing Color_B");
        return entity.get<Color_B>().value;
    }
    else if (name == "normal_x") {
        if (!entity.has<Normal_X>()) throw std::runtime_error("Missing Normal_X");
        return entity.get<Normal_X>().value;
    }
    else if (name == "normal_y") {
        if (!entity.has<Normal_Y>()) throw std::runtime_error("Missing Normal_Y");
        return entity.get<Normal_Y>().value;
    }
    else if (name == "normal_z") {
        if (!entity.has<Normal_Z>()) throw std::runtime_error("Missing Normal_Z");
        return entity.get<Normal_Z>().value;
    }
    else if (name == "material") {
        if (!entity.has<Material>()) throw std::runtime_error("Missing Material");
        return entity.get<Material>().id;
    }
    else if (name == "emission_r") {
        if (!entity.has<Emission_R>()) throw std::runtime_error("Missing Emission_R");
        return entity.get<Emission_R>().value;
    }
    else if (name == "emission_g") {
        if (!entity.has<Emission_G>()) throw std::runtime_error("Missing Emission_G");
        return entity.get<Emission_G>().value;
    }
    else if (name == "emission_b") {
        if (!entity.has<Emission_B>()) throw std::runtime_error("Missing Emission_B");
        return entity.get<Emission_B>().value;
    }
    else if (name == "emission_intensity") {
        if (!entity.has<Emission_Intensity>()) throw std::runtime_error("Missing Emission_Intensity");
        return entity.get<Emission_Intensity>().value;
    }
    else if (name == "color") {
        // Reconstruct vec3 from split components
        if (!entity.has<Color_R>() || !entity.has<Color_G>() || !entity.has<Color_B>()) {
            throw std::runtime_error("Missing color components");
        }
        return glm::vec3(
            entity.get<Color_R>().value,
            entity.get<Color_G>().value,
            entity.get<Color_B>().value
        );
    }
    else if (name == "normal") {
        // Reconstruct vec3 from split components
        if (!entity.has<Normal_X>() || !entity.has<Normal_Y>() || !entity.has<Normal_Z>()) {
            throw std::runtime_error("Missing normal components");
        }
        return glm::vec3(
            entity.get<Normal_X>().value,
            entity.get<Normal_Y>().value,
            entity.get<Normal_Z>().value
        );
    }
    else {
        throw std::runtime_error("Unknown component: " + name);
    }
}

} // namespace GaiaVoxel
