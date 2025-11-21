#include "DynamicVoxelStruct.h"
#include <stdexcept>

namespace VoxelData {

// ============================================================================
// DynamicVoxelScalar Implementation
// ============================================================================

DynamicVoxelScalar::DynamicVoxelScalar(const AttributeRegistry* registry)
    : m_registry(registry) {
    syncWithRegistry(registry);
}

std::vector<std::string> DynamicVoxelScalar::getAttributeNames() const {
    std::vector<std::string> names;
    names.reserve(m_values.size());
    for (const auto& [name, _] : m_values) {
        names.push_back(name);
    }
    return names;
}

void DynamicVoxelScalar::syncWithRegistry(const AttributeRegistry* registry) {
    if (!registry) return;

    // Update registry pointer
    m_registry = registry;

    // Get all attributes from registry
    auto registryAttrs = registry->getAttributeNames();

    // Add missing attributes with defaults
    for (const auto& attrName : registryAttrs) {
        if (!has(attrName)) {
            // Get descriptor to determine type and default
            auto* storage = registry->getStorage(attrName);
            if (storage) {
                // Add with default value (stored in storage)
                // For now, initialize to zero/default
                // TODO: Get actual default from AttributeDescriptor
                m_values[attrName] = std::any();  // Placeholder
            }
        }
    }

    // Remove attributes no longer in registry
    auto it = m_values.begin();
    while (it != m_values.end()) {
        bool found = false;
        for (const auto& regAttr : registryAttrs) {
            if (it->first == regAttr) {
                found = true;
                break;
            }
        }
        if (!found) {
            it = m_values.erase(it);
        } else {
            ++it;
        }
    }
}

// ============================================================================
// DynamicVoxelArrays Implementation
// ============================================================================

DynamicVoxelArrays::DynamicVoxelArrays(const AttributeRegistry* registry) {
    syncWithRegistry(registry);
}

DynamicVoxelScalar DynamicVoxelArrays::operator[](size_t index) const {
    if (index >= m_count) {
        throw std::out_of_range("Voxel index out of range");
    }

    DynamicVoxelScalar voxel;

    // Extract values from each array
    for (const auto& [attrName, arrayAny] : m_arrays) {
        // Determine type and extract
        // Try float
        try {
            const auto& arr = std::any_cast<const std::vector<float>&>(arrayAny);
            if (index < arr.size()) {
                voxel.set(attrName, arr[index]);
            }
            continue;
        } catch (...) {}

        // Try uint32
        try {
            const auto& arr = std::any_cast<const std::vector<uint32_t>&>(arrayAny);
            if (index < arr.size()) {
                voxel.set(attrName, arr[index]);
            }
            continue;
        } catch (...) {}

        // Try vec3
        try {
            const auto& arr = std::any_cast<const std::vector<glm::vec3>&>(arrayAny);
            if (index < arr.size()) {
                voxel.set(attrName, arr[index]);
            }
            continue;
        } catch (...) {}
    }

    return voxel;
}

void DynamicVoxelArrays::set(size_t index, const DynamicVoxelScalar& voxel) {
    if (index >= m_count) {
        throw std::out_of_range("Voxel index out of range");
    }

    // Set values in each array
    for (const auto& attrName : voxel.getAttributeNames()) {
        if (!has(attrName)) continue;

        // Try float
        try {
            float val = voxel.get<float>(attrName);
            auto& arr = getArray<float>(attrName);
            if (index < arr.size()) {
                arr[index] = val;
            }
            continue;
        } catch (...) {}

        // Try uint32
        try {
            uint32_t val = voxel.get<uint32_t>(attrName);
            auto& arr = getArray<uint32_t>(attrName);
            if (index < arr.size()) {
                arr[index] = val;
            }
            continue;
        } catch (...) {}

        // Try vec3
        try {
            glm::vec3 val = voxel.get<glm::vec3>(attrName);
            auto& arr = getArray<glm::vec3>(attrName);
            if (index < arr.size()) {
                arr[index] = val;
            }
            continue;
        } catch (...) {}
    }
}

void DynamicVoxelArrays::push_back(const DynamicVoxelScalar& voxel) {
    // Append to each array
    for (auto& [attrName, arrayAny] : m_arrays) {
        // Try float
        try {
            auto& arr = std::any_cast<std::vector<float>&>(arrayAny);
            float val = voxel.has(attrName) ? voxel.get<float>(attrName) : 0.0f;
            arr.push_back(val);
            continue;
        } catch (...) {}

        // Try uint32
        try {
            auto& arr = std::any_cast<std::vector<uint32_t>&>(arrayAny);
            uint32_t val = voxel.has(attrName) ? voxel.get<uint32_t>(attrName) : 0u;
            arr.push_back(val);
            continue;
        } catch (...) {}

        // Try vec3
        try {
            auto& arr = std::any_cast<std::vector<glm::vec3>&>(arrayAny);
            glm::vec3 val = voxel.has(attrName) ? voxel.get<glm::vec3>(attrName) : glm::vec3(0);
            arr.push_back(val);
            continue;
        } catch (...) {}
    }

    ++m_count;
}

void DynamicVoxelArrays::reserve(size_t capacity) {
    for (auto& [attrName, arrayAny] : m_arrays) {
        // Try float
        try {
            auto& arr = std::any_cast<std::vector<float>&>(arrayAny);
            arr.reserve(capacity);
            continue;
        } catch (...) {}

        // Try uint32
        try {
            auto& arr = std::any_cast<std::vector<uint32_t>&>(arrayAny);
            arr.reserve(capacity);
            continue;
        } catch (...) {}

        // Try vec3
        try {
            auto& arr = std::any_cast<std::vector<glm::vec3>&>(arrayAny);
            arr.reserve(capacity);
            continue;
        } catch (...) {}
    }
}

size_t DynamicVoxelArrays::count() const {
    return m_count;
}

std::vector<std::string> DynamicVoxelArrays::getAttributeNames() const {
    std::vector<std::string> names;
    names.reserve(m_arrays.size());
    for (const auto& [name, _] : m_arrays) {
        names.push_back(name);
    }
    return names;
}

void DynamicVoxelArrays::syncWithRegistry(const AttributeRegistry* registry) {
    if (!registry) return;

    // Get all attributes from registry
    auto registryAttrs = registry->getAttributeNames();

    // Add missing attribute arrays
    for (const auto& attrName : registryAttrs) {
        if (!has(attrName)) {
            // Determine type from storage
            auto* storage = registry->getStorage(attrName);
            if (storage) {
                // Create empty vector of appropriate type
                // TODO: Get type from AttributeDescriptor
                // For now, default to float
                m_arrays[attrName] = std::vector<float>();
            }
        }
    }

    // Remove attributes no longer in registry
    auto it = m_arrays.begin();
    while (it != m_arrays.end()) {
        bool found = false;
        for (const auto& regAttr : registryAttrs) {
            if (it->first == regAttr) {
                found = true;
                break;
            }
        }
        if (!found) {
            it = m_arrays.erase(it);
        } else {
            ++it;
        }
    }
}

// NOTE: BrickView integration methods (setVoxel, getVoxel, setBatch, getBatch)
// are implemented in BrickView.cpp

} // namespace VoxelData
