#include "pch.h"
#include "AttributeStorage.h"
#include <stdexcept>
#include <cstring>

namespace  Vixen::VoxelData {

AttributeStorage::AttributeStorage(std::string name, AttributeType type, std::any defaultValue)
    : m_name(std::move(name))
    , m_type(type)
    , m_defaultValue(std::move(defaultValue))
    , m_elementSize(getAttributeSize(type))
    , m_allocatedSlots(0)
{
    // Start with capacity for 1024 bricks
    reserve(1024);
}

size_t AttributeStorage::allocateSlot() {
    size_t slotIndex;

    // Reuse freed slot if available
    if (!m_freeSlots.empty()) {
        slotIndex = m_freeSlots.front();
        m_freeSlots.pop();
    } else {
        // Allocate new slot
        slotIndex = m_slotOccupied.size();
        growIfNeeded();
    }

    m_slotOccupied[slotIndex] = true;
    m_allocatedSlots++;

    // Initialize slot with default value
    // (TODO: implement default value initialization based on type)

    return slotIndex;
}

void AttributeStorage::freeSlot(size_t slotIndex) {
    if (slotIndex >= m_slotOccupied.size() || !m_slotOccupied[slotIndex]) {
        throw std::runtime_error("Attempted to free invalid or already-freed slot");
    }

    m_slotOccupied[slotIndex] = false;
    m_freeSlots.push(slotIndex);
    m_allocatedSlots--;
}

void AttributeStorage::reserve(size_t maxBricks) {
    size_t requiredBytes = maxBricks * VOXELS_PER_BRICK * m_elementSize;

    if (m_data.capacity() < requiredBytes) {
        m_data.reserve(requiredBytes);
    }

    if (m_slotOccupied.size() < maxBricks) {
        m_slotOccupied.resize(maxBricks, false);
    }
}

void* AttributeStorage::getSlotData(size_t slotIndex) {
    if (slotIndex >= m_slotOccupied.size() || !m_slotOccupied[slotIndex]) {
        throw std::runtime_error("Attempted to access invalid slot");
    }

    size_t byteOffset = slotIndex * VOXELS_PER_BRICK * m_elementSize;
    return m_data.data() + byteOffset;
}

const void* AttributeStorage::getSlotData(size_t slotIndex) const {
    if (slotIndex >= m_slotOccupied.size() || !m_slotOccupied[slotIndex]) {
        throw std::runtime_error("Attempted to access invalid slot");
    }

    size_t byteOffset = slotIndex * VOXELS_PER_BRICK * m_elementSize;
    return m_data.data() + byteOffset;
}

void AttributeStorage::growIfNeeded() {
    size_t currentCapacity = m_slotOccupied.size();
    size_t newSlotIndex = currentCapacity;

    // Double capacity
    size_t newCapacity = currentCapacity == 0 ? 1024 : currentCapacity * 2;

    m_data.resize(newCapacity * VOXELS_PER_BRICK * m_elementSize);
    m_slotOccupied.resize(newCapacity, false);
}

} // namespace VoxelData
