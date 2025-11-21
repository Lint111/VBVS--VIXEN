#pragma once

#include "VoxelDataTypes.h"
#include "ArrayView.h"
#include <unordered_map>
#include <string>
#include <cstddef>

namespace VoxelData {

// Forward declarations
class AttributeRegistry;
class AttributeStorage;
class DynamicVoxelScalar;
class DynamicVoxelArrays;

/**
 * BrickAllocation - Tracks which storage slots a brick occupies
 *
 * Maps attribute name → slot index in AttributeStorage
 * Lightweight (just a map of strings → integers)
 */
struct BrickAllocation {
    std::unordered_map<std::string, size_t> attributeSlots;

    // Get slot for specific attribute
    size_t getSlot(const std::string& attrName) const {
        return attributeSlots.at(attrName);
    }

    // Check if attribute is allocated
    bool hasAttribute(const std::string& attrName) const {
        return attributeSlots.find(attrName) != attributeSlots.end();
    }

    // Add attribute slot
    void addSlot(const std::string& attrName, size_t slot) {
        attributeSlots[attrName] = slot;
    }

    // Get all attribute names
    std::vector<std::string> getAttributeNames() const {
        std::vector<std::string> names;
        names.reserve(attributeSlots.size());
        for (const auto& [name, _] : attributeSlots) {
            names.push_back(name);
        }
        return names;
    }
};

/**
 * BrickView - Zero-copy view of brick data
 *
 * Does NOT own data - references AttributeStorage slots
 * Lightweight (16 bytes: registry pointer + allocation)
 *
 * Usage:
 *   BrickView brick = storage->getBrick(brickID);
 *   brick.set<float>("density", 42, 1.0f);      // Set voxel 42
 *   float d = brick.get<float>("density", 42);  // Get voxel 42
 */
class BrickView {
public:
    static constexpr size_t VOXELS_PER_BRICK = 512;

    BrickView(AttributeRegistry* registry, BrickAllocation allocation);

    // Type-safe element access (1D linear index)
    template<typename T>
    void set(const std::string& attrName, size_t voxelIndex, T value);

    template<typename T>
    T get(const std::string& attrName, size_t voxelIndex) const;

    // 3D coordinate access (user-friendly, hides indexing scheme)
    template<typename T>
    void setAt3D(const std::string& attrName, int x, int y, int z, T value);

    template<typename T>
    T getAt3D(const std::string& attrName, int x, int y, int z) const;

    // Get array view for attribute
    template<typename T>
    ArrayView<T> getAttributeArray(const std::string& attrName);

    template<typename T>
    ConstArrayView<T> getAttributeArray(const std::string& attrName) const;

    // Check if attribute exists
    bool hasAttribute(const std::string& attrName) const {
        return m_allocation.hasAttribute(attrName);
    }

    // Get all attribute names
    std::vector<std::string> getAttributeNames() const {
        return m_allocation.getAttributeNames();
    }

    // Get allocation (for debugging)
    const BrickAllocation& getAllocation() const { return m_allocation; }

    // Get voxel count
    size_t getVoxelCount() const { return VOXELS_PER_BRICK; }

    // ============================================================================
    // High-Level Integration with DynamicVoxelScalar/Arrays
    // ============================================================================

    /**
     * @brief Set a single voxel from DynamicVoxelScalar at 3D coordinates
     *
     * Automatically reads all attributes from the scalar and writes to brick.
     * Handles type conversions behind the scenes.
     *
     * Example:
     * ```cpp
     * DynamicVoxelScalar voxel;
     * voxel.set("density", 0.8f);
     * voxel.set("material", 42u);
     * brick.setVoxel(x, y, z, voxel);  // Writes both attributes automatically
     * ```
     */
    void setVoxel(int x, int y, int z, const DynamicVoxelScalar& voxel);

    /**
     * @brief Get a single voxel as DynamicVoxelScalar at 3D coordinates
     *
     * Automatically reads all brick attributes into a scalar.
     *
     * Example:
     * ```cpp
     * DynamicVoxelScalar voxel = brick.getVoxel(x, y, z);
     * float density = voxel.get<float>("density");
     * ```
     */
    DynamicVoxelScalar getVoxel(int x, int y, int z) const;

    /**
     * @brief Populate entire brick from DynamicVoxelArrays (batch operation)
     *
     * Copies all 512 voxels from arrays into brick.
     * Arrays must contain at least 512 voxels.
     *
     * Example:
     * ```cpp
     * DynamicVoxelArrays batch(&registry);
     * // ... fill batch with 512 voxels ...
     * brick.setBatch(batch);  // Copy all voxels to brick
     * ```
     */
    void setBatch(const DynamicVoxelArrays& batch);

    /**
     * @brief Extract entire brick into DynamicVoxelArrays (batch operation)
     *
     * Copies all 512 voxels from brick into arrays.
     * Arrays will be resized to 512 voxels.
     *
     * Example:
     * ```cpp
     * DynamicVoxelArrays batch = brick.getBatch();
     * // batch now contains 512 voxels from brick
     * ```
     */
    DynamicVoxelArrays getBatch() const;

private:
    AttributeRegistry* m_registry;
    BrickAllocation m_allocation;

    // Helper: get storage for attribute
    AttributeStorage* getStorage(const std::string& attrName) const;
};

} // namespace VoxelData
