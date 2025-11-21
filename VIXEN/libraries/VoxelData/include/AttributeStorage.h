#pragma once

#include "VoxelDataTypes.h"
#include "ArrayView.h"
#include <vector>
#include <queue>
#include <string>
#include <cstdint>

namespace VoxelData {

/**
 * AttributeStorage - Owns raw data for one attribute across ALL bricks
 *
 * Key design:
 * - Single contiguous buffer for all bricks
 * - Bricks get 512-element "slots" in this buffer
 * - Freed slots are reused (no fragmentation)
 * - Zero-copy: BrickViews reference slots directly
 *
 * Memory layout:
 *   [Brick0: 512 elements][Brick1: 512 elements][Brick2: 512 elements]...
 *    ^                     ^                     ^
 *    slot 0                slot 1                slot 2
 */
class AttributeStorage {
public:
    static constexpr size_t VOXELS_PER_BRICK = 512;  // 8x8x8

    AttributeStorage(std::string name, AttributeType type, std::any defaultValue);

    ~AttributeStorage() = default;

    // Non-copyable (owns large buffers)
    AttributeStorage(const AttributeStorage&) = delete;
    AttributeStorage& operator=(const AttributeStorage&) = delete;

    // Movable
    AttributeStorage(AttributeStorage&&) noexcept = default;
    AttributeStorage& operator=(AttributeStorage&&) noexcept = default;

    // Slot allocation
    size_t allocateSlot();
    void freeSlot(size_t slotIndex);

    // Reserve capacity (pre-allocate to avoid reallocation)
    void reserve(size_t maxBricks);

    // Get raw pointer to slot data
    void* getSlotData(size_t slotIndex);
    const void* getSlotData(size_t slotIndex) const;

    // Get typed view of slot
    template<typename T>
    ArrayView<T> getSlotView(size_t slotIndex) {
        return ArrayView<T>(
            static_cast<T*>(getSlotData(slotIndex)),
            VOXELS_PER_BRICK
        );
    }

    template<typename T>
    ConstArrayView<T> getSlotView(size_t slotIndex) const {
        return ConstArrayView<T>(
            static_cast<const T*>(getSlotData(slotIndex)),
            VOXELS_PER_BRICK
        );
    }

    // Properties
    const std::string& getName() const { return m_name; }
    AttributeType getType() const { return m_type; }
    size_t getElementSize() const { return m_elementSize; }
    size_t getAllocatedSlots() const { return m_allocatedSlots; }
    size_t getTotalSlots() const { return m_slotOccupied.size(); }

    // Get raw data buffer (for GPU upload)
    const std::vector<uint8_t>& getData() const { return m_data; }
    std::vector<uint8_t>& getData() { return m_data; }

private:
    std::string m_name;
    AttributeType m_type;
    std::any m_defaultValue;
    size_t m_elementSize;

    // Storage
    std::vector<uint8_t> m_data;       // Raw bytes: [slot0][slot1][slot2]...
    std::vector<bool> m_slotOccupied;  // Which slots are allocated
    std::queue<size_t> m_freeSlots;    // Reusable slots
    size_t m_allocatedSlots;           // Count of allocated slots

    // Helper: grow storage if needed
    void growIfNeeded();
};

} // namespace VoxelData
