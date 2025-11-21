#include "BrickView.h"
#include "AttributeRegistry.h"
#include <stdexcept>

namespace VoxelData {

BrickView::BrickView(AttributeRegistry* registry, BrickAllocation allocation)
    : m_registry(registry)
    , m_allocation(std::move(allocation))
{
}

AttributeStorage* BrickView::getStorage(const std::string& attrName) const {
    if (!m_allocation.hasAttribute(attrName)) {
        throw std::runtime_error("Brick does not have attribute: " + attrName);
    }

    return m_registry->getStorage(attrName);
}

// Template specializations for set()
template<>
void BrickView::set<float>(const std::string& attrName, size_t voxelIndex, float value) {
    auto* storage = getStorage(attrName);
    size_t slot = m_allocation.getSlot(attrName);
    auto view = storage->getSlotView<float>(slot);
    view[voxelIndex] = value;
}

template<>
void BrickView::set<uint32_t>(const std::string& attrName, size_t voxelIndex, uint32_t value) {
    auto* storage = getStorage(attrName);
    size_t slot = m_allocation.getSlot(attrName);
    auto view = storage->getSlotView<uint32_t>(slot);
    view[voxelIndex] = value;
}

template<>
void BrickView::set<uint16_t>(const std::string& attrName, size_t voxelIndex, uint16_t value) {
    auto* storage = getStorage(attrName);
    size_t slot = m_allocation.getSlot(attrName);
    auto view = storage->getSlotView<uint16_t>(slot);
    view[voxelIndex] = value;
}

template<>
void BrickView::set<uint8_t>(const std::string& attrName, size_t voxelIndex, uint8_t value) {
    auto* storage = getStorage(attrName);
    size_t slot = m_allocation.getSlot(attrName);
    auto view = storage->getSlotView<uint8_t>(slot);
    view[voxelIndex] = value;
}

// Template specializations for get()
template<>
float BrickView::get<float>(const std::string& attrName, size_t voxelIndex) const {
    auto* storage = getStorage(attrName);
    size_t slot = m_allocation.getSlot(attrName);
    auto view = storage->getSlotView<float>(slot);
    return view[voxelIndex];
}

template<>
uint32_t BrickView::get<uint32_t>(const std::string& attrName, size_t voxelIndex) const {
    auto* storage = getStorage(attrName);
    size_t slot = m_allocation.getSlot(attrName);
    auto view = storage->getSlotView<uint32_t>(slot);
    return view[voxelIndex];
}

template<>
uint16_t BrickView::get<uint16_t>(const std::string& attrName, size_t voxelIndex) const {
    auto* storage = getStorage(attrName);
    size_t slot = m_allocation.getSlot(attrName);
    auto view = storage->getSlotView<uint16_t>(slot);
    return view[voxelIndex];
}

template<>
uint8_t BrickView::get<uint8_t>(const std::string& attrName, size_t voxelIndex) const {
    auto* storage = getStorage(attrName);
    size_t slot = m_allocation.getSlot(attrName);
    auto view = storage->getSlotView<uint8_t>(slot);
    return view[voxelIndex];
}

// Template specializations for getAttributeArray()
template<>
ArrayView<float> BrickView::getAttributeArray<float>(const std::string& attrName) {
    auto* storage = getStorage(attrName);
    size_t slot = m_allocation.getSlot(attrName);
    return storage->getSlotView<float>(slot);
}

template<>
ArrayView<uint32_t> BrickView::getAttributeArray<uint32_t>(const std::string& attrName) {
    auto* storage = getStorage(attrName);
    size_t slot = m_allocation.getSlot(attrName);
    return storage->getSlotView<uint32_t>(slot);
}

template<>
ArrayView<uint16_t> BrickView::getAttributeArray<uint16_t>(const std::string& attrName) {
    auto* storage = getStorage(attrName);
    size_t slot = m_allocation.getSlot(attrName);
    return storage->getSlotView<uint16_t>(slot);
}

template<>
ArrayView<uint8_t> BrickView::getAttributeArray<uint8_t>(const std::string& attrName) {
    auto* storage = getStorage(attrName);
    size_t slot = m_allocation.getSlot(attrName);
    return storage->getSlotView<uint8_t>(slot);
}

// Const versions
template<>
ConstArrayView<float> BrickView::getAttributeArray<float>(const std::string& attrName) const {
    auto* storage = getStorage(attrName);
    size_t slot = m_allocation.getSlot(attrName);
    return storage->getSlotView<float>(slot);
}

template<>
ConstArrayView<uint32_t> BrickView::getAttributeArray<uint32_t>(const std::string& attrName) const {
    auto* storage = getStorage(attrName);
    size_t slot = m_allocation.getSlot(attrName);
    return storage->getSlotView<uint32_t>(slot);
}

template<>
ConstArrayView<uint16_t> BrickView::getAttributeArray<uint16_t>(const std::string& attrName) const {
    auto* storage = getStorage(attrName);
    size_t slot = m_allocation.getSlot(attrName);
    return storage->getSlotView<uint16_t>(slot);
}

template<>
ConstArrayView<uint8_t> BrickView::getAttributeArray<uint8_t>(const std::string& attrName) const {
    auto* storage = getStorage(attrName);
    size_t slot = m_allocation.getSlot(attrName);
    return storage->getSlotView<uint8_t>(slot);
}

} // namespace VoxelData
