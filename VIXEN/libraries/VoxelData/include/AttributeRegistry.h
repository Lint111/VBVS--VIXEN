#pragma once

#include "VoxelDataTypes.h"
#include "AttributeStorage.h"
#include "BrickView.h"
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>
#include <functional>

namespace  Vixen::VoxelData {

// Forward declarations
class AttributeRegistry;

/**
 * Attribute change event types
 */
enum class AttributeChangeType {
    KeyChanged,        // DESTRUCTIVE: Key attribute changed
    AttributeAdded,    // NON-DESTRUCTIVE: New attribute added
    AttributeRemoved,  // NON-DESTRUCTIVE: Attribute removed
};

/**
 * IAttributeRegistryObserver - Callback interface for attribute changes
 *
 * Implement this to receive notifications when attributes change.
 * Spatial structures (octrees, grids) should observe the registry
 * and rebuild when key changes.
 */
class IAttributeRegistryObserver {
public:
    virtual ~IAttributeRegistryObserver() = default;

    /**
     * Called when key attribute changes (DESTRUCTIVE)
     * Spatial structure MUST rebuild completely
     */
    virtual void onKeyChanged(const std::string& oldKey, const std::string& newKey) = 0;

    /**
     * Called when attribute added (NON-DESTRUCTIVE)
     * Optional: Update shaders, GUI, etc.
     */
    virtual void onAttributeAdded(const std::string& name, AttributeType type) = 0;

    /**
     * Called when attribute removed (NON-DESTRUCTIVE)
     * Optional: Update shaders, GUI, etc.
     */
    virtual void onAttributeRemoved(const std::string& name) = 0;
};

/**
 * Key predicate - Custom filter for vec3 keys
 *
 * Example: Filter voxels where normal points into upper hemisphere
 * ```cpp
 * registry->setKeyPredicate([](const glm::vec3& normal) {
 *     return normal.y > 0.0f;  // Upper hemisphere
 * });
 * ```
 */
using KeyPredicate = std::function<bool(const std::any& value)>;

/**
 * AttributeRegistry - Central manager for voxel attributes
 *
 * Responsibilities:
 * - Registers/unregisters attributes at runtime
 * - Owns AttributeStorage for each attribute
 * - Allocates/frees bricks across all attributes
 * - Provides BrickViews to access brick data
 *
 * Key design:
 * - One AttributeStorage per attribute (owns data)
 * - BrickViews reference slots in AttributeStorage (zero-copy)
 * - Adding/removing attributes doesn't move existing data
 * - Vec3 keys support custom predicates (e.g., hemisphere filters)
 *
 * Usage:
 *   auto registry = std::make_shared<AttributeRegistry>();
 *   registry->registerKey("density", AttributeType::Float, 0.0f);
 *   registry->addAttribute("material", AttributeType::Uint32, 0u);
 *
 *   // Vec3 key with custom predicate
 *   registry->registerKey("normal", AttributeType::Vec3, glm::vec3(0,1,0));
 *   registry->setKeyPredicate([](const std::any& val) {
 *       glm::vec3 normal = std::any_cast<glm::vec3>(val);
 *       return normal.y > 0.0f;  // Upper hemisphere only
 *   });
 *
 *   uint32_t brickID = registry->allocateBrick();
 *   BrickView brick = registry->getBrick(brickID);
 *   brick.set("density", 0, 1.0f);
 */
class AttributeRegistry {
public:
    AttributeRegistry() = default;
    ~AttributeRegistry() = default;

    // Non-copyable (owns AttributeStorage)
    AttributeRegistry(const AttributeRegistry&) = delete;
    AttributeRegistry& operator=(const AttributeRegistry&) = delete;

    // ========================================================================
    // ATTRIBUTE LIFECYCLE MANAGEMENT
    // ========================================================================

    /**
     * Register key attribute (DESTRUCTIVE - requires full rebuild if changed)
     *
     * The key attribute determines octree structure sparsity.
     * Changing the key invalidates all spatial structures.
     *
     * @returns Unique attribute index for this attribute
     * @throws If key already registered
     */
    AttributeIndex registerKey(std::string name, AttributeType type, std::any defaultValue);

    /**
     * Change key attribute (DESTRUCTIVE - requires caller to rebuild structure)
     *
     * This invalidates all spatial relationships. Caller MUST:
     * 1. Rebuild octree/spatial structure from scratch
     * 2. Re-allocate all bricks
     * 3. Re-populate voxel data
     *
     * @returns true if key changed (rebuild required), false if same key
     */
    bool changeKey(std::string newKeyName);

    /**
     * Add attribute (NON-DESTRUCTIVE - existing data unchanged)
     *
     * Allocates slots for new attribute across all existing bricks.
     * Does NOT move or copy existing attribute data.
     * BrickViews remain valid.
     *
     * @returns Unique attribute index for this attribute
     * Cost: O(num_bricks) slot allocations
     */
    AttributeIndex addAttribute(std::string name, AttributeType type, std::any defaultValue);

    /**
     * Remove attribute (NON-DESTRUCTIVE - existing data unchanged)
     *
     * Frees slots for removed attribute, returns to free pool.
     * Does NOT move or copy remaining attribute data.
     * BrickViews for other attributes remain valid.
     *
     * @throws If trying to remove key attribute
     * Cost: O(num_bricks) slot deallocations
     */
    void removeAttribute(const std::string& name);

    // Brick allocation
    uint32_t allocateBrick();
    void freeBrick(uint32_t brickID);

    // Get brick view (zero-copy)
    BrickView getBrick(uint32_t brickID);
    BrickView getBrick(uint32_t brickID) const;

    // Query by name (legacy API)
    bool hasAttribute(const std::string& name) const;
    bool isKeyAttribute(const std::string& name) const;
    AttributeStorage* getStorage(const std::string& name);
    const AttributeStorage* getStorage(const std::string& name) const;

    // Query by index (FAST - zero-cost lookup)
    AttributeStorage* getStorage(AttributeIndex index);
    const AttributeStorage* getStorage(AttributeIndex index) const;
    const AttributeDescriptor& getDescriptor(AttributeIndex index) const;
    AttributeIndex getAttributeIndex(const std::string& name) const;

    // Get all attribute names
    std::vector<std::string> getAttributeNames() const;

    // Get key attribute name and index
    const std::string& getKeyAttributeName() const { return m_keyAttributeName; }
    AttributeIndex getKeyAttributeIndex() const { return m_keyAttributeIndex; }

    // Statistics
    size_t getBrickCount() const { return m_bricks.size(); }
    size_t getAttributeCount() const { return m_attributes.size(); }

    // Reserve capacity (call before bulk allocation)
    void reserve(size_t maxBricks);

    // ========================================================================
    // KEY PREDICATE - CUSTOM FILTERING FOR VEC3 KEYS
    // ========================================================================

    /**
     * Set custom predicate for key evaluation
     *
     * Used for vec3 keys with custom filters (e.g., hemisphere normals).
     * Predicate receives key value as std::any, returns true if voxel should be included.
     *
     * Example (hemisphere filter):
     * ```cpp
     * registry->setKeyPredicate([](const std::any& val) {
     *     glm::vec3 normal = std::any_cast<glm::vec3>(val);
     *     return glm::dot(normal, glm::vec3(0,1,0)) > 0.0f;  // Upper hemisphere
     * });
     * ```
     */
    void setKeyPredicate(KeyPredicate predicate) {
        m_keyPredicate = std::move(predicate);
    }

    /**
     * Evaluate key value against predicate
     *
     * Returns true if:
     * - No predicate set (default behavior - all voxels pass)
     * - Predicate returns true
     *
     * Returns false if predicate returns false (voxel should be filtered out)
     */
    bool evaluateKey(const std::any& keyValue) const {
        if (!m_keyPredicate) {
            return true;  // No predicate - all voxels pass
        }
        return m_keyPredicate(keyValue);
    }

    // ========================================================================
    // OBSERVER PATTERN - NOTIFICATION SYSTEM
    // ========================================================================

    /**
     * Register observer for attribute changes
     * Observer receives callbacks for key changes, additions, removals
     */
    void addObserver(IAttributeRegistryObserver* observer);

    /**
     * Unregister observer
     */
    void removeObserver(IAttributeRegistryObserver* observer);

private:
    // Key attribute (determines octree structure)
    std::string m_keyAttributeName;
    AttributeIndex m_keyAttributeIndex = INVALID_ATTRIBUTE_INDEX;

    // Key predicate (custom filter for vec3 keys)
    KeyPredicate m_keyPredicate;

    // Attribute storage (owns data)
    std::unordered_map<std::string, std::unique_ptr<AttributeStorage>> m_attributes;

    // Attribute descriptors (name → descriptor)
    std::unordered_map<std::string, AttributeDescriptor> m_descriptors;

    // Index-based lookups (FAST)
    std::vector<AttributeStorage*> m_storageByIndex;      // index → storage pointer
    std::vector<AttributeDescriptor> m_descriptorByIndex; // index → descriptor
    AttributeIndex m_nextAttributeIndex = 0;              // Monotonic counter

    // Brick allocations (brickID → allocation)
    std::unordered_map<uint32_t, BrickAllocation> m_bricks;
    uint32_t m_nextBrickID = 0;

    // Observers (for attribute change notifications)
    std::vector<IAttributeRegistryObserver*> m_observers;

    // Helper: allocate slot in attribute for brick
    BrickAllocation allocateBrickInAllAttributes();

    // Helper: free brick slots in all attributes
    void freeBrickInAllAttributes(const BrickAllocation& allocation);

    // Helper: notify observers
    void notifyKeyChanged(const std::string& oldKey, const std::string& newKey);
    void notifyAttributeAdded(const std::string& name, AttributeType type);
    void notifyAttributeRemoved(const std::string& name);
};

} // namespace VoxelData
