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

// ============================================================================
// 3D Coordinate Access (hides indexing scheme)
// ============================================================================

namespace {
    // Morton encoding: interleave bits of x,y,z for Z-order curve
    // Provides better cache locality for spatial neighbors
    inline uint32_t mortonEncode(uint32_t x, uint32_t y, uint32_t z) {
        auto expandBits = [](uint32_t v) -> uint32_t {
            v = (v | (v << 16)) & 0x030000FF;
            v = (v | (v << 8))  & 0x0300F00F;
            v = (v | (v << 4))  & 0x030C30C3;
            v = (v | (v << 2))  & 0x09249249;
            return v;
        };
        return expandBits(x) | (expandBits(y) << 1) | (expandBits(z) << 2);
    }

    // Convert linear index to morton index
    // Decomposes linear → (x,y,z) → morton
    inline size_t linearToMorton(size_t linearIndex) {
        constexpr int stride = 8;  // 2^3 for depth=3
        constexpr int planeSize = 64;  // 8*8

        uint32_t x = linearIndex % stride;
        uint32_t y = (linearIndex / stride) % stride;
        uint32_t z = linearIndex / planeSize;

        return mortonEncode(x, y, z);
    }

    // Convert 3D coordinates to storage index
    // Can switch between Linear and Morton by changing this function
    inline size_t coordsToStorageIndex(int x, int y, int z) {
        #if 1  // Use Morton ordering (better cache locality)
            return mortonEncode(static_cast<uint32_t>(x),
                               static_cast<uint32_t>(y),
                               static_cast<uint32_t>(z));
        #else  // Use Linear ordering (simpler debugging)
            constexpr int stride = 8;
            constexpr int planeSize = 64;
            return z * planeSize + y * stride + x;
        #endif
    }
}

template<>
void BrickView::setAt3D<float>(const std::string& attrName, int x, int y, int z, float value) {
    size_t index = coordsToStorageIndex(x, y, z);
    set<float>(attrName, index, value);
}

template<>
float BrickView::getAt3D<float>(const std::string& attrName, int x, int y, int z) const {
    size_t index = coordsToStorageIndex(x, y, z);
    return get<float>(attrName, index);
}

template<>
void BrickView::setAt3D<uint32_t>(const std::string& attrName, int x, int y, int z, uint32_t value) {
    size_t index = coordsToStorageIndex(x, y, z);
    set<uint32_t>(attrName, index, value);
}

template<>
uint32_t BrickView::getAt3D<uint32_t>(const std::string& attrName, int x, int y, int z) const {
    size_t index = coordsToStorageIndex(x, y, z);
    return get<uint32_t>(attrName, index);
}

} // namespace VoxelData
