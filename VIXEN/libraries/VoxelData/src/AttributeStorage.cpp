#include "pch.h"
#include "AttributeStorage.h"
#include <algorithm>
#include <stdexcept>
#include <cstring>

namespace  Vixen::VoxelData {

AttributeStorage::AttributeStorage(std::string name, AttributeType type, std::any defaultValue)
    : m_name(std::move(name))
    , m_type(type)
    , m_defaultValue(std::move(defaultValue))
    , m_elementSize(getAttributeSize(type))
    , m_allocatedSlots(0)
    , m_nextSlotIndex(0)
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
        // Reserved vector size is capacity, not the number of slot IDs already assigned.
        if (m_nextSlotIndex >= m_slotOccupied.size()) {
            growIfNeeded();
        }
        slotIndex = m_nextSlotIndex++;
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
    const size_t targetSlots = std::max(maxBricks, m_slotOccupied.size());
    const size_t requiredBytes = targetSlots * VOXELS_PER_BRICK * m_elementSize;

    if (m_data.size() < requiredBytes) {
        m_data.resize(requiredBytes);
    }

    if (m_slotOccupied.size() < targetSlots) {
        m_slotOccupied.resize(targetSlots, false);
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

    // Double capacity
    size_t newCapacity = currentCapacity == 0 ? 1024 : currentCapacity * 2;

    m_data.resize(newCapacity * VOXELS_PER_BRICK * m_elementSize);
    m_slotOccupied.resize(newCapacity, false);
}

} // namespace VoxelData
