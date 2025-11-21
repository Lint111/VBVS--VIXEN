#pragma once

#include "VoxelConfig.h"
#include "AttributeRegistry.h"
#include <unordered_map>
#include <functional>
#include <glm/glm.hpp>

namespace VoxelData {

// ============================================================================
// Dynamic Voxel Structures - Runtime Modifiable
// ============================================================================

/**
 * @brief Single voxel data (dynamic schema)
 *
 * Stores attribute values as std::any, schema defined by parent registry.
 * Automatically updates when registry changes attributes.
 */
class DynamicVoxelScalar {
public:
    DynamicVoxelScalar() = default;
    explicit DynamicVoxelScalar(const AttributeRegistry* registry);

    /**
     * @brief Initialize from VoxelConfig
     *
     * Creates registry from config and syncs with it.
     *
     * Example:
     * ```cpp
     * StandardVoxel config;
     * DynamicVoxelScalar voxel(&config);
     * // voxel now has density, material, color attributes
     * ```
     */
    template<size_t N>
    explicit DynamicVoxelScalar(const VoxelConfigBase<N>* config) : m_registry(nullptr) {
        if (config) {
            // Extract schema from config descriptors
            for (const auto& desc : config->getAttributeDescriptors()) {
                m_values[desc.name] = desc.defaultValue;
            }
        }
    }

    // Get/Set attributes by name
    template<typename T>
    T get(const std::string& attrName) const {
        auto it = m_values.find(attrName);
        if (it == m_values.end()) {
            throw std::runtime_error("Attribute not found: " + attrName);
        }
        return std::any_cast<T>(it->second);
    }

    template<typename T>
    void set(const std::string& attrName, const T& value) {
        m_values[attrName] = value;
    }

    // Check if attribute exists
    bool has(const std::string& attrName) const {
        return m_values.find(attrName) != m_values.end();
    }

    // Get all attribute names
    std::vector<std::string> getAttributeNames() const;

    // Sync with registry schema (adds new attributes with defaults)
    void syncWithRegistry(const AttributeRegistry* registry);

    /**
     * @brief Evaluate if voxel passes the key predicate
     *
     * Uses the registry's key attribute and predicate to determine if this voxel
     * represents "solid" data (should be included in octree structure).
     *
     * Returns true if:
     * - No registry is set (default pass)
     * - Key attribute exists and passes the registry's predicate
     *
     * Example:
     * ```cpp
     * // Registry has key "density" with predicate: density > 0.5f
     * DynamicVoxelScalar voxel(&registry);
     * voxel.set("density", 0.8f);
     * bool isSolid = voxel.passesKeyPredicate();  // true
     * ```
     */
    bool passesKeyPredicate() const {
        if (!m_registry) {
            return true;  // No registry - default pass
        }

        const std::string& keyName = m_registry->getKeyAttributeName();
        if (!has(keyName)) {
            return false;  // Key attribute not set
        }

        // Get key value and evaluate against registry's predicate
        auto it = m_values.find(keyName);
        if (it == m_values.end()) {
            return false;
        }

        return m_registry->evaluateKey(it->second);
    }

private:
    std::unordered_map<std::string, std::any> m_values;
    const AttributeRegistry* m_registry = nullptr;
};

/**
 * @brief Batch of voxels (dynamic schema SoA)
 *
 * Stores arrays of attributes, schema defined by parent registry.
 * Automatically updates when registry changes attributes.
 */
class DynamicVoxelArrays {
public:
    DynamicVoxelArrays() = default;
    explicit DynamicVoxelArrays(const AttributeRegistry* registry);

    /**
     * @brief Initialize from VoxelConfig
     *
     * Creates empty arrays for each attribute in config.
     *
     * Example:
     * ```cpp
     * StandardVoxel config;
     * DynamicVoxelArrays batch(&config);
     * batch.reserve(1000);
     * // batch now has empty density, material, color arrays
     * ```
     */
    template<size_t N>
    explicit DynamicVoxelArrays(const VoxelConfigBase<N>* config) {
        if (config) {
            // Create empty arrays for each attribute
            for (const auto& desc : config->getAttributeDescriptors()) {
                // Determine type and create appropriate vector
                switch (desc.type) {
                    case AttributeType::Float:
                        m_arrays[desc.name] = std::vector<float>();
                        break;
                    case AttributeType::Uint32:
                        m_arrays[desc.name] = std::vector<uint32_t>();
                        break;
                    case AttributeType::Uint16:
                        m_arrays[desc.name] = std::vector<uint16_t>();
                        break;
                    case AttributeType::Uint8:
                        m_arrays[desc.name] = std::vector<uint8_t>();
                        break;
                    case AttributeType::Vec3:
                        m_arrays[desc.name] = std::vector<glm::vec3>();
                        break;
                }
            }
        }
    }

    // Get array for attribute
    template<typename T>
    std::vector<T>& getArray(const std::string& attrName) {
        auto it = m_arrays.find(attrName);
        if (it == m_arrays.end()) {
            throw std::runtime_error("Attribute array not found: " + attrName);
        }
        return *std::any_cast<std::vector<T>>(&it->second);
    }

    template<typename T>
    const std::vector<T>& getArray(const std::string& attrName) const {
        auto it = m_arrays.find(attrName);
        if (it == m_arrays.end()) {
            throw std::runtime_error("Attribute array not found: " + attrName);
        }
        return std::any_cast<const std::vector<T>&>(it->second);
    }

    // Get single voxel at index
    DynamicVoxelScalar operator[](size_t index) const;

    // Set single voxel at index
    void set(size_t index, const DynamicVoxelScalar& voxel);

    // Add voxel to end
    void push_back(const DynamicVoxelScalar& voxel);

    // Reserve capacity for all arrays
    void reserve(size_t capacity);

    // Get voxel count
    size_t count() const;

    // Check if attribute exists
    bool has(const std::string& attrName) const {
        return m_arrays.find(attrName) != m_arrays.end();
    }

    // Get all attribute names
    std::vector<std::string> getAttributeNames() const;

    // Sync with registry schema (adds new attribute arrays)
    void syncWithRegistry(const AttributeRegistry* registry);

private:
    std::unordered_map<std::string, std::any> m_arrays;  // attrName → std::vector<T>
    size_t m_count = 0;
};

// ============================================================================
// Registry Observer - Automatically Syncs Structs
// ============================================================================

/**
 * @brief Observer that keeps DynamicVoxel structs synced with registry
 *
 * Usage:
 * ```cpp
 * AttributeRegistry registry;
 * DynamicVoxelArrays batch(&registry);
 *
 * // Create observer to keep batch synced
 * auto observer = std::make_unique<DynamicVoxelSyncObserver>(&registry);
 * observer->registerArrays(&batch);
 * registry.addObserver(observer.get());
 *
 * // Now when registry changes, batch auto-updates
 * registry.addAttribute("newAttr", AttributeType::Float, 0.0f);
 * // batch now has "newAttr" array!
 * ```
 */
class DynamicVoxelSyncObserver : public IAttributeRegistryObserver {
public:
    explicit DynamicVoxelSyncObserver(const AttributeRegistry* registry)
        : m_registry(registry) {}

    // Register structs to keep synced
    void registerScalar(DynamicVoxelScalar* scalar) {
        m_scalars.push_back(scalar);
    }

    void registerArrays(DynamicVoxelArrays* arrays) {
        m_arrays.push_back(arrays);
    }

    // IAttributeRegistryObserver implementation
    void onKeyChanged(const std::string& oldKey, const std::string& newKey) override {
        // Key change requires full rebuild - resync all structs
        for (auto* scalar : m_scalars) {
            scalar->syncWithRegistry(m_registry);
        }
        for (auto* arrays : m_arrays) {
            arrays->syncWithRegistry(m_registry);
        }
    }

    void onAttributeAdded(const std::string& name, AttributeType type) override {
        // Add attribute to all structs
        for (auto* scalar : m_scalars) {
            scalar->syncWithRegistry(m_registry);
        }
        for (auto* arrays : m_arrays) {
            arrays->syncWithRegistry(m_registry);
        }
    }

    void onAttributeRemoved(const std::string& name) override {
        // Remove attribute from all structs
        // (syncWithRegistry will handle cleanup)
        for (auto* scalar : m_scalars) {
            scalar->syncWithRegistry(m_registry);
        }
        for (auto* arrays : m_arrays) {
            arrays->syncWithRegistry(m_registry);
        }
    }

private:
    const AttributeRegistry* m_registry;
    std::vector<DynamicVoxelScalar*> m_scalars;
    std::vector<DynamicVoxelArrays*> m_arrays;
};

// ============================================================================
// Helper: Convert DynamicVoxelScalar to BrickView
// ============================================================================

inline void PopulateBrickFromDynamic(BrickView& brick, size_t x, size_t y, size_t z,
                                     const DynamicVoxelScalar& voxel) {
    for (const auto& attrName : voxel.getAttributeNames()) {
        // Determine type and set appropriately
        // This requires type information from registry
        // For now, handle common types
        try {
            // Try float
            float val = voxel.get<float>(attrName);
            brick.setAt3D<float>(attrName, x, y, z, val);
        } catch (...) {
            try {
                // Try uint32
                uint32_t val = voxel.get<uint32_t>(attrName);
                brick.setAt3D<uint32_t>(attrName, x, y, z, val);
            } catch (...) {
                try {
                    // Try vec3
                    glm::vec3 val = voxel.get<glm::vec3>(attrName);
                    brick.setAt3D<glm::vec3>(attrName, x, y, z, val);
                } catch (...) {
                    // Unsupported type, skip
                }
            }
        }
    }
}

} // namespace VoxelData
