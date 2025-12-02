#include "pch.h"
#include "BrickView.h"
#include "AttributeRegistry.h"
#include "AttributeStorage.h"
#include <glm/glm.hpp>
#include <stdexcept>

namespace  Vixen::VoxelData {
// Forward declarations - implementations will include DynamicVoxelStruct.h later
class DynamicVoxelScalar;
class DynamicVoxelArrays;

BrickView::BrickView(AttributeRegistry* registry, BrickAllocation allocation, uint8_t brickDepth)
    : m_registry(registry)
    , m_allocation(std::move(allocation))
    , m_brickDepth(brickDepth)
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

template<>
void BrickView::set<glm::vec3>(const std::string& attrName, size_t voxelIndex, glm::vec3 value) {
    auto* storage = getStorage(attrName);
    size_t slot = m_allocation.getSlot(attrName);
    auto view = storage->getSlotView<glm::vec3>(slot);
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

template<>
glm::vec3 BrickView::get<glm::vec3>(const std::string& attrName, size_t voxelIndex) const {
    auto* storage = getStorage(attrName);
    size_t slot = m_allocation.getSlot(attrName);
    auto view = storage->getSlotView<glm::vec3>(slot);
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

template<>
ArrayView<glm::vec3> BrickView::getAttributeArray<glm::vec3>(const std::string& attrName) {
    auto* storage = getStorage(attrName);
    size_t slot = m_allocation.getSlot(attrName);
    return storage->getSlotView<glm::vec3>(slot);
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

template<>
ConstArrayView<glm::vec3> BrickView::getAttributeArray<glm::vec3>(const std::string& attrName) const {
    auto* storage = getStorage(attrName);
    size_t slot = m_allocation.getSlot(attrName);
    return storage->getSlotView<glm::vec3>(slot);
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

// Include DynamicVoxelStruct.h AFTER template specializations to avoid early instantiation
#include "DynamicVoxelStruct.h"
#include "VoxelDataTypes.h"

namespace Vixen::VoxelData {

// ============================================================================
// High-Level Integration with DynamicVoxelScalar/Arrays
// ============================================================================

namespace {
    // Helper: Dispatch lambda based on AttributeType
    template<typename Func>
    void dispatchByType(AttributeType type, Func&& func) {
        switch (type) {
            case AttributeType::Float:
                func.template operator()<float>();
                break;
            case AttributeType::Uint32:
                func.template operator()<uint32_t>();
                break;
            case AttributeType::Uint16:
                func.template operator()<uint16_t>();
                break;
            case AttributeType::Uint8:
                func.template operator()<uint8_t>();
                break;
            case AttributeType::Vec3:
                func.template operator()<glm::vec3>();
                break;
        }
    }
}

void BrickView::setVoxel(int x, int y, int z, const DynamicVoxelScalar& voxel) {
    // Iterate over voxel attributes
    for (const auto& attrName : voxel.getAttributeNames()) {
        // Check if brick has this attribute
        if (!hasAttribute(attrName)) continue;

        // Get attribute type from registry
        auto* storage = getStorage(attrName);
        if (!storage) continue;

        AttributeType attrType = storage->getType();

        // Dispatch to correct type
        dispatchByType(attrType, [&]<typename T>() {
            T val = voxel.get<T>(attrName);
            setAt3D<T>(attrName, x, y, z, val);
        });
    }
}

DynamicVoxelScalar BrickView::getVoxel(int x, int y, int z) const {
    DynamicVoxelScalar voxel;

    // Iterate over all brick attributes
    for (const auto& attrName : getAttributeNames()) {
        auto* storage = getStorage(attrName);
        if (!storage) continue;

        AttributeType attrType = storage->getType();

        // Dispatch to correct type
        dispatchByType(attrType, [&]<typename T>() {
            T val = getAt3D<T>(attrName, x, y, z);
            voxel.set(attrName, val);
        });
    }

    return voxel;
}

void BrickView::setBatch(const DynamicVoxelArrays& batch) {
    if (batch.count() < VOXELS_PER_BRICK) {
        throw std::runtime_error("DynamicVoxelArrays must contain at least 512 voxels");
    }

    // Iterate over batch attributes
    for (const auto& attrName : batch.getAttributeNames()) {
        // Check if brick has this attribute
        if (!hasAttribute(attrName)) continue;

        auto* storage = getStorage(attrName);
        if (!storage) continue;

        AttributeType attrType = storage->getType();

        // Dispatch to correct type and copy array
        dispatchByType(attrType, [&]<typename T>() {
            const auto& srcArr = batch.getArray<T>(attrName);
            auto dstArr = getAttributeArray<T>(attrName);
            for (size_t i = 0; i < VOXELS_PER_BRICK; ++i) {
                dstArr[i] = srcArr[i];
            }
        });
    }
}

DynamicVoxelArrays BrickView::getBatch() const {
    DynamicVoxelArrays batch;

    // Extract all 512 voxels using scalar extraction
    // TODO: Optimize with direct array copy once DynamicVoxelArrays supports bulk init
    for (size_t i = 0; i < VOXELS_PER_BRICK; ++i) {
        // Convert linear index to 3D coordinates
        int x = i % 8;
        int y = (i / 8) % 8;
        int z = i / 64;

        DynamicVoxelScalar voxel = getVoxel(x, y, z);
        batch.push_back(voxel);
    }

    return batch;
}

// ============================================================================
// Fast Attribute Access (Performance-Critical Ray Traversal)
// ============================================================================

// Index-based access (FASTEST - no string hashing)
template<>
const float* BrickView::getAttributePointer<float>(AttributeIndex attrIndex) const {
    if (!m_allocation.hasAttribute(attrIndex)) return nullptr;

    auto* storage = m_registry->getStorage(attrIndex);
    if (!storage) return nullptr;

    size_t slot = m_allocation.getSlot(attrIndex);
    auto view = storage->getSlotView<float>(slot);
    return view.data();
}

template<>
float* BrickView::getAttributePointer<float>(AttributeIndex attrIndex) {
    if (!m_allocation.hasAttribute(attrIndex)) return nullptr;

    auto* storage = m_registry->getStorage(attrIndex);
    if (!storage) return nullptr;

    size_t slot = m_allocation.getSlot(attrIndex);
    auto view = storage->getSlotView<float>(slot);
    return view.data();
}

template<>
const uint32_t* BrickView::getAttributePointer<uint32_t>(AttributeIndex attrIndex) const {
    if (!m_allocation.hasAttribute(attrIndex)) return nullptr;

    auto* storage = m_registry->getStorage(attrIndex);
    if (!storage) return nullptr;

    size_t slot = m_allocation.getSlot(attrIndex);
    auto view = storage->getSlotView<uint32_t>(slot);
    return view.data();
}

template<>
uint32_t* BrickView::getAttributePointer<uint32_t>(AttributeIndex attrIndex) {
    if (!m_allocation.hasAttribute(attrIndex)) return nullptr;

    auto* storage = m_registry->getStorage(attrIndex);
    if (!storage) return nullptr;

    size_t slot = m_allocation.getSlot(attrIndex);
    auto view = storage->getSlotView<uint32_t>(slot);
    return view.data();
}

template<>
const glm::vec3* BrickView::getAttributePointer<glm::vec3>(AttributeIndex attrIndex) const {
    if (!m_allocation.hasAttribute(attrIndex)) return nullptr;

    auto* storage = m_registry->getStorage(attrIndex);
    if (!storage) return nullptr;

    size_t slot = m_allocation.getSlot(attrIndex);
    auto view = storage->getSlotView<glm::vec3>(slot);
    return view.data();
}

template<>
glm::vec3* BrickView::getAttributePointer<glm::vec3>(AttributeIndex attrIndex) {
    if (!m_allocation.hasAttribute(attrIndex)) return nullptr;

    auto* storage = m_registry->getStorage(attrIndex);
    if (!storage) return nullptr;

    size_t slot = m_allocation.getSlot(attrIndex);
    auto view = storage->getSlotView<glm::vec3>(slot);
    return view.data();
}

// Name-based access (legacy - delegates to index-based)
template<>
const float* BrickView::getAttributePointer<float>(const std::string& attrName) const {
    AttributeIndex idx = m_registry->getAttributeIndex(attrName);
    if (idx == INVALID_ATTRIBUTE_INDEX) return nullptr;
    return getAttributePointer<float>(idx);
}

template<>
float* BrickView::getAttributePointer<float>(const std::string& attrName) {
    AttributeIndex idx = m_registry->getAttributeIndex(attrName);
    if (idx == INVALID_ATTRIBUTE_INDEX) return nullptr;
    return getAttributePointer<float>(idx);
}

template<>
const uint32_t* BrickView::getAttributePointer<uint32_t>(const std::string& attrName) const {
    AttributeIndex idx = m_registry->getAttributeIndex(attrName);
    if (idx == INVALID_ATTRIBUTE_INDEX) return nullptr;
    return getAttributePointer<uint32_t>(idx);
}

template<>
uint32_t* BrickView::getAttributePointer<uint32_t>(const std::string& attrName) {
    AttributeIndex idx = m_registry->getAttributeIndex(attrName);
    if (idx == INVALID_ATTRIBUTE_INDEX) return nullptr;
    return getAttributePointer<uint32_t>(idx);
}

// ============================================================================
// Coordinate Mapping (Morton/Linear Indexing)
// ============================================================================

std::any* BrickView::getKeyAttributePointer() const
{
    // Key attribute is ALWAYS index 0 in AttributeRegistry
    AttributeIndex keyIndex = 0;
    auto* storage = m_registry->getStorage(keyIndex);
    if (!storage) return nullptr;
    size_t slot = m_allocation.getSlot(keyIndex);
    return static_cast<std::any*>(storage->getSlotData(slot));
}

size_t BrickView::coordsToIndex(int x, int y, int z) const
{
    // Use existing coordsToStorageIndex (defined earlier in file)
    return coordsToStorageIndex(x, y, z);
}

void BrickView::indexToCoords(size_t index, int& x, int& y, int& z) const {
    // Inverse of coordsToStorageIndex
    // Assumes linear XYZ ordering: x + y*8 + z*64
    // TODO: Support Morton decoding if needed

    constexpr int sideLength = 8;
    z = index / (sideLength * sideLength);
    index -= z * sideLength * sideLength;
    y = index / sideLength;
    x = index % sideLength;
}

// Template method implementations for 3D coordinate access
template<typename T>
void Vixen::VoxelData::BrickView::setAt3D(const std::string& attrName, int x, int y, int z, T value) {
    size_t index = coordsToIndex(x, y, z);
    set<T>(attrName, index, value);
}

template<typename T>
T Vixen::VoxelData::BrickView::getAt3D(const std::string& attrName, int x, int y, int z) const {
    size_t index = coordsToIndex(x, y, z);
    return get<T>(attrName, index);
}

// Explicit template instantiations for common types
template void Vixen::VoxelData::BrickView::setAt3D<float>(const std::string&, int, int, int, float);
template void Vixen::VoxelData::BrickView::setAt3D<uint32_t>(const std::string&, int, int, int, uint32_t);
template void Vixen::VoxelData::BrickView::setAt3D<uint16_t>(const std::string&, int, int, int, uint16_t);
template void Vixen::VoxelData::BrickView::setAt3D<uint8_t>(const std::string&, int, int, int, uint8_t);
template void Vixen::VoxelData::BrickView::setAt3D<glm::vec3>(const std::string&, int, int, int, glm::vec3);
template float Vixen::VoxelData::BrickView::getAt3D<float>(const std::string&, int, int, int) const;
template uint32_t Vixen::VoxelData::BrickView::getAt3D<uint32_t>(const std::string&, int, int, int) const;
template uint16_t Vixen::VoxelData::BrickView::getAt3D<uint16_t>(const std::string&, int, int, int) const;
template uint8_t Vixen::VoxelData::BrickView::getAt3D<uint8_t>(const std::string&, int, int, int) const;
template glm::vec3 Vixen::VoxelData::BrickView::getAt3D<glm::vec3>(const std::string&, int, int, int) const;

template std::span<glm::vec3> Vixen::VoxelData::BrickView::getAttributeArray<glm::vec3>(const std::string&);

} // namespace Vixen::VoxelData