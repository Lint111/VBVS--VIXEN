#include "pch.h"
#include "AttributeRegistry.h"
#include <stdexcept>
#include <algorithm>

namespace VoxelData {

AttributeIndex AttributeRegistry::registerKey(std::string name, AttributeType type, std::any defaultValue) {
    if (!m_keyAttributeName.empty()) {
        throw std::runtime_error("Key attribute already registered: " + m_keyAttributeName);
    }

    if (hasAttribute(name)) {
        throw std::runtime_error("Attribute already exists: " + name);
    }

    // Assign unique index
    AttributeIndex index = m_nextAttributeIndex++;
    m_keyAttributeName = name;
    m_keyAttributeIndex = index;

    // Create descriptor with index
    AttributeDescriptor desc(name, type, defaultValue, index, true);
    m_descriptors[name] = desc;

    // Create storage
    auto storage = std::make_unique<AttributeStorage>(name, type, defaultValue);
    m_attributes[name] = std::move(storage);

    // Add to index-based lookups
    m_storageByIndex.push_back(m_attributes[name].get());
    m_descriptorByIndex.push_back(desc);

    return index;
}

bool AttributeRegistry::changeKey(std::string newKeyName) {
    if (newKeyName == m_keyAttributeName) {
        return false;  // No change
    }

    if (!hasAttribute(newKeyName)) {
        throw std::runtime_error("Cannot change to non-existent attribute: " + newKeyName);
    }

    std::string oldKey = m_keyAttributeName;
    m_keyAttributeName = newKeyName;

    // Update descriptor
    m_descriptors[newKeyName].isKey = true;
    if (!oldKey.empty()) {
        m_descriptors[oldKey].isKey = false;
    }

    // Notify observers (spatial structures MUST rebuild)
    notifyKeyChanged(oldKey, newKeyName);

    return true;  // Rebuild required
}

AttributeIndex AttributeRegistry::addAttribute(std::string name, AttributeType type, std::any defaultValue) {
    if (hasAttribute(name)) {
        throw std::runtime_error("Attribute already exists: " + name);
    }

    // Assign unique index
    AttributeIndex index = m_nextAttributeIndex++;

    // Create descriptor with index
    AttributeDescriptor desc(name, type, defaultValue, index, false);
    m_descriptors[name] = desc;

    // Create storage
    auto storage = std::make_unique<AttributeStorage>(name, type, defaultValue);
    m_attributes[name] = std::move(storage);

    // Add to index-based lookups
    m_storageByIndex.push_back(m_attributes[name].get());
    m_descriptorByIndex.push_back(desc);

    // For existing bricks, allocate slots in new attribute
    for (auto& [brickID, allocation] : m_bricks) {
        size_t slot = m_attributes[name]->allocateSlot();
        allocation.addSlot(index, name, slot);
    }

    // Notify observers (non-destructive)
    notifyAttributeAdded(name, type);

    return index;
}

void AttributeRegistry::removeAttribute(const std::string& name) {
    if (!hasAttribute(name)) {
        throw std::runtime_error("Attribute does not exist: " + name);
    }

    if (isKeyAttribute(name)) {
        throw std::runtime_error("Cannot remove key attribute: " + name);
    }

    // Remove from all brick allocations
    for (auto& [brickID, allocation] : m_bricks) {
        if (allocation.hasAttribute(name)) {
            size_t slot = allocation.getSlot(name);
            m_attributes[name]->freeSlot(slot);
            allocation.attributeSlots.erase(name);
        }
    }

    // Remove storage and descriptor
    m_attributes.erase(name);
    m_descriptors.erase(name);

    // Notify observers (non-destructive)
    notifyAttributeRemoved(name);
}

uint32_t AttributeRegistry::allocateBrick() {
    uint32_t brickID = m_nextBrickID++;

    // Allocate slots in all attributes
    BrickAllocation allocation = allocateBrickInAllAttributes();

    m_bricks[brickID] = std::move(allocation);

    return brickID;
}

void AttributeRegistry::freeBrick(uint32_t brickID) {
    auto it = m_bricks.find(brickID);
    if (it == m_bricks.end()) {
        throw std::runtime_error("Brick does not exist: " + std::to_string(brickID));
    }

    // Free slots in all attributes
    freeBrickInAllAttributes(it->second);

    m_bricks.erase(it);
}

BrickView AttributeRegistry::getBrick(uint32_t brickID) {
    auto it = m_bricks.find(brickID);
    if (it == m_bricks.end()) {
        throw std::runtime_error("Brick does not exist: " + std::to_string(brickID));
    }

    return BrickView(this, it->second);
}

BrickView AttributeRegistry::getBrick(uint32_t brickID) const {
    auto it = m_bricks.find(brickID);
    if (it == m_bricks.end()) {
        throw std::runtime_error("Brick does not exist: " + std::to_string(brickID));
    }

    return BrickView(const_cast<AttributeRegistry*>(this), it->second);
}

bool AttributeRegistry::hasAttribute(const std::string& name) const {
    return m_attributes.find(name) != m_attributes.end();
}

bool AttributeRegistry::isKeyAttribute(const std::string& name) const {
    return name == m_keyAttributeName;
}

AttributeStorage* AttributeRegistry::getStorage(const std::string& name) {
    auto it = m_attributes.find(name);
    if (it == m_attributes.end()) {
        return nullptr;
    }
    return it->second.get();
}

const AttributeStorage* AttributeRegistry::getStorage(const std::string& name) const {
    auto it = m_attributes.find(name);
    if (it == m_attributes.end()) {
        return nullptr;
    }
    return it->second.get();
}

// Index-based queries (FAST - O(1) vector access)
AttributeStorage* AttributeRegistry::getStorage(AttributeIndex index) {
    if (index >= m_storageByIndex.size()) {
        return nullptr;
    }
    return m_storageByIndex[index];
}

const AttributeStorage* AttributeRegistry::getStorage(AttributeIndex index) const {
    if (index >= m_storageByIndex.size()) {
        return nullptr;
    }
    return m_storageByIndex[index];
}

const AttributeDescriptor& AttributeRegistry::getDescriptor(AttributeIndex index) const {
    if (index >= m_descriptorByIndex.size()) {
        throw std::out_of_range("Invalid attribute index: " + std::to_string(index));
    }
    return m_descriptorByIndex[index];
}

AttributeIndex AttributeRegistry::getAttributeIndex(const std::string& name) const {
    auto it = m_descriptors.find(name);
    if (it == m_descriptors.end()) {
        return INVALID_ATTRIBUTE_INDEX;
    }
    return it->second.index;
}

std::vector<std::string> AttributeRegistry::getAttributeNames() const {
    std::vector<std::string> names;
    names.reserve(m_attributes.size());
    for (const auto& [name, _] : m_attributes) {
        names.push_back(name);
    }
    return names;
}

void AttributeRegistry::reserve(size_t maxBricks) {
    for (auto& [name, storage] : m_attributes) {
        storage->reserve(maxBricks);
    }
}

BrickAllocation AttributeRegistry::allocateBrickInAllAttributes() {
    BrickAllocation allocation;

    // Allocate slots using index-based approach
    for (const auto& desc : m_descriptorByIndex) {
        auto* storage = m_storageByIndex[desc.index];
        size_t slot = storage->allocateSlot();
        allocation.addSlot(desc.index, desc.name, slot);
    }

    return allocation;
}

void AttributeRegistry::freeBrickInAllAttributes(const BrickAllocation& allocation) {
    for (const auto& attrName : allocation.getAttributeNames()) {
        size_t slot = allocation.getSlot(attrName);
        m_attributes[attrName]->freeSlot(slot);
    }
}

void AttributeRegistry::addObserver(IAttributeRegistryObserver* observer) {
    if (observer) {
        m_observers.push_back(observer);
    }
}

void AttributeRegistry::removeObserver(IAttributeRegistryObserver* observer) {
    auto it = std::find(m_observers.begin(), m_observers.end(), observer);
    if (it != m_observers.end()) {
        m_observers.erase(it);
    }
}

void AttributeRegistry::notifyKeyChanged(const std::string& oldKey, const std::string& newKey) {
    for (auto* observer : m_observers) {
        observer->onKeyChanged(oldKey, newKey);
    }
}

void AttributeRegistry::notifyAttributeAdded(const std::string& name, AttributeType type) {
    for (auto* observer : m_observers) {
        observer->onAttributeAdded(name, type);
    }
}

void AttributeRegistry::notifyAttributeRemoved(const std::string& name) {
    for (auto* observer : m_observers) {
        observer->onAttributeRemoved(name);
    }
}

} // namespace VoxelData
