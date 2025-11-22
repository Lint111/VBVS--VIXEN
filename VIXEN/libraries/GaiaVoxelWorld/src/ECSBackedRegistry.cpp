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
        m_world.add<Density>(entity, Density{std::any_cast<float>(value)});
    }
    else if (name == "color_r") {
        m_world.add<Color_R>(entity, Color_R{std::any_cast<float>(value)});
    }
    else if (name == "color_g") {
        m_world.add<Color_G>(entity, Color_G{std::any_cast<float>(value)});
    }
    else if (name == "color_b") {
        m_world.add<Color_B>(entity, Color_B{std::any_cast<float>(value)});
    }
    else if (name == "normal_x") {
        m_world.add<Normal_X>(entity, Normal_X{std::any_cast<float>(value)});
    }
    else if (name == "normal_y") {
        m_world.add<Normal_Y>(entity, Normal_Y{std::any_cast<float>(value)});
    }
    else if (name == "normal_z") {
        m_world.add<Normal_Z>(entity, Normal_Z{std::any_cast<float>(value)});
    }
    else if (name == "material") {
        m_world.add<Material>(entity, Material{std::any_cast<uint32_t>(value)});
    }
    else if (name == "emission_r") {
        m_world.add<Emission_R>(entity, Emission_R{std::any_cast<float>(value)});
    }
    else if (name == "emission_g") {
        m_world.add<Emission_G>(entity, Emission_G{std::any_cast<float>(value)});
    }
    else if (name == "emission_b") {
        m_world.add<Emission_B>(entity, Emission_B{std::any_cast<float>(value)});
    }
    else if (name == "emission_intensity") {
        m_world.add<Emission_Intensity>(entity, Emission_Intensity{std::any_cast<float>(value)});
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

    if (entity == gaia::ecs::Entity()) {
        throw std::runtime_error("Invalid entity");
    }

    // Dispatch based on attribute name (using Gaia World API)
    if (name == "density") {
        if (!m_world.has<Density>(entity)) throw std::runtime_error("Missing Density");
        return m_world.get<Density>(entity).value;
    }
    else if (name == "color_r") {
        if (!m_world.has<Color_R>(entity)) throw std::runtime_error("Missing Color_R");
        return m_world.get<Color_R>(entity).value;
    }
    else if (name == "color_g") {
        if (!m_world.has<Color_G>(entity)) throw std::runtime_error("Missing Color_G");
        return m_world.get<Color_G>(entity).value;
    }
    else if (name == "color_b") {
        if (!m_world.has<Color_B>(entity)) throw std::runtime_error("Missing Color_B");
        return m_world.get<Color_B>(entity).value;
    }
    else if (name == "normal_x") {
        if (!m_world.has<Normal_X>(entity)) throw std::runtime_error("Missing Normal_X");
        return m_world.get<Normal_X>(entity).value;
    }
    else if (name == "normal_y") {
        if (!m_world.has<Normal_Y>(entity)) throw std::runtime_error("Missing Normal_Y");
        return m_world.get<Normal_Y>(entity).value;
    }
    else if (name == "normal_z") {
        if (!m_world.has<Normal_Z>(entity)) throw std::runtime_error("Missing Normal_Z");
        return m_world.get<Normal_Z>(entity).value;
    }
    else if (name == "material") {
        if (!m_world.has<Material>(entity)) throw std::runtime_error("Missing Material");
        return m_world.get<Material>(entity).id;
    }
    else if (name == "emission_r") {
        if (!m_world.has<Emission_R>(entity)) throw std::runtime_error("Missing Emission_R");
        return m_world.get<Emission_R>(entity).value;
    }
    else if (name == "emission_g") {
        if (!m_world.has<Emission_G>(entity)) throw std::runtime_error("Missing Emission_G");
        return m_world.get<Emission_G>(entity).value;
    }
    else if (name == "emission_b") {
        if (!m_world.has<Emission_B>(entity)) throw std::runtime_error("Missing Emission_B");
        return m_world.get<Emission_B>(entity).value;
    }
    else if (name == "emission_intensity") {
        if (!m_world.has<Emission_Intensity>(entity)) throw std::runtime_error("Missing Emission_Intensity");
        return m_world.get<Emission_Intensity>(entity).value;
    }
    else if (name == "color") {
        // Reconstruct vec3 from split components
        if (!m_world.has<Color_R>(entity) || !m_world.has<Color_G>(entity) || !m_world.has<Color_B>(entity)) {
            throw std::runtime_error("Missing color components");
        }
        return glm::vec3(
            m_world.get<Color_R>(entity).value,
            m_world.get<Color_G>(entity).value,
            m_world.get<Color_B>(entity).value
        );
    }
    else if (name == "normal") {
        // Reconstruct vec3 from split components
        if (!m_world.has<Normal_X>(entity) || !m_world.has<Normal_Y>(entity) || !m_world.has<Normal_Z>(entity)) {
            throw std::runtime_error("Missing normal components");
        }
        return glm::vec3(
            m_world.get<Normal_X>(entity).value,
            m_world.get<Normal_Y>(entity).value,
            m_world.get<Normal_Z>(entity).value
        );
    }
    else {
        throw std::runtime_error("Unknown component: " + name);
    }
}

} // namespace GaiaVoxel
