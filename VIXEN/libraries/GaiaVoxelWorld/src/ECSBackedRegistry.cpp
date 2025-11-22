#include "ECSBackedRegistry.h"
#include <glm/glm.hpp>
#include <iostream>
#include "VoxelComponents.h"

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
            m_world.add<MortonKey>(entity, MortonKey::fromPosition(pos));
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
    m_world.add<MortonKey>(entity, MortonKey::fromPosition(position));

    // Add each attribute as component
    for (const auto& [name, value] : attributes) {
        addComponentFromAttribute(entity, name, value);
    }

    return entity;
}

VoxelData::DynamicVoxelScalar ECSBackedRegistry::getVoxelFromEntity(
    gaia::ecs::Entity entity) const {

    VoxelData::DynamicVoxelScalar voxel(this);

    if (! m_world.valid(entity)) return voxel;

    // Add position if MortonKey exists
    if (m_world.has<MortonKey>(entity)) {
        glm::vec3 pos = m_world.get<MortonKey>(entity).toWorldPos();
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
    if (!m_world.valid(entity) || !m_world.has<MortonKey>(entity)) {
        return std::nullopt;
    }
    return m_world.get<MortonKey>(entity).toWorldPos();
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
    if (m_world.valid(entity)) {
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
    if (!m_world.valid(entity)) return false;

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
    else if (name == "color") {
        m_world.add<Color>(entity, Color(std::any_cast<glm::vec3>(value)));
    }
    else if (name == "normal") {
        m_world.add<Normal>(entity, Normal(std::any_cast<glm::vec3>(value)));
    }
    else if (name == "material") {
        m_world.add<Material>(entity, Material{std::any_cast<uint32_t>(value)});
    }
    else if (name == "emission") {
        m_world.add<Emission>(entity, Emission(std::any_cast<glm::vec3>(value)));
    }
    else {
        std::cerr << "[ECSBackedRegistry] Unknown component: " << name << "\n";
    }
}

// ============================================================================
// Helper: Get Component Value as std::any
// ============================================================================

std::any ECSBackedRegistry::getComponentAsAny(
    gaia::ecs::World& world,
    gaia::ecs::Entity entity,
    const std::string& name) {

    if (entity == gaia::ecs::Entity()) {
        throw std::runtime_error("Invalid entity");
    }

    // Dispatch based on attribute name (using Gaia World API)
    if (name == "density") {
        if (!world.has<Density>(entity)) throw std::runtime_error("Missing Density");
        return world.get<Density>(entity).value;
    }
    else if (name == "color") {
        if (!world.has<Color>(entity)) throw std::runtime_error("Missing Color");
        return glm::vec3(world.get<Color>(entity));
    }
    else if (name == "normal") {
        if (!world.has<Normal>(entity)) throw std::runtime_error("Missing Normal");
        return glm::vec3(world.get<Normal>(entity));
    }
    else if (name == "material") {
        if (!world.has<Material>(entity)) throw std::runtime_error("Missing Material");
        return world.get<Material>(entity);
    }
    else if (name == "emission") {
        if (!world.has<Emission>(entity)) throw std::runtime_error("Missing Emission");
        return glm::vec3(world.get<Emission>(entity));
    }
    else {
        throw std::runtime_error("Unknown component: " + name);
    }
}

} // namespace GaiaVoxel
