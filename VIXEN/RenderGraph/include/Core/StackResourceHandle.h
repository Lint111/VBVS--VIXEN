#pragma once

#include "Core/StackResourceTracker.h"
#include <expected>
#include <variant>
#include <memory>
#include <vector>
#include <string_view>

namespace VIXEN {

/**
 * @brief Allocation error types for stack resource requests
 */
enum class AllocationError {
    StackOverflow,      // Stack budget exceeded, couldn't allocate
    HeapOverflow,       // Heap budget also exceeded, couldn't allocate
    InvalidSize,        // Requested size is invalid (0 or too large)
    SystemError         // Underlying system allocation failed
};

/**
 * @brief Location of allocated resource
 */
enum class ResourceLocation {
    Stack,  // Allocated on stack (fast, cache-friendly)
    Heap    // Fell back to heap (slower, but safe)
};

/**
 * @brief Smart handle for stack-or-heap allocated resources
 *
 * Provides unified interface regardless of allocation location.
 * Automatically falls back to heap if stack budget exceeded.
 *
 * Phase H: Safe stack allocation with automatic fallback and tracking.
 *
 * @tparam T Element type
 * @tparam Capacity Maximum capacity (for stack allocation)
 */
template<typename T, size_t Capacity>
class StackResourceHandle {
public:
    /**
     * @brief Create handle with stack allocation
     */
    static StackResourceHandle CreateStack(
        std::string_view name,
        StackResourceTracker& tracker,
        uint32_t nodeId
    ) {
        StackResourceHandle handle;
        handle.location_ = ResourceLocation::Stack;
        handle.stackData_.emplace();
        handle.name_ = name;
        handle.nodeId_ = nodeId;

        // Track allocation
        tracker.TrackAllocation(
            name,
            handle.stackData_->data(),
            handle.stackData_->capacity_bytes(),
            nodeId
        );

        return handle;
    }

    /**
     * @brief Create handle with heap allocation (fallback)
     */
    static StackResourceHandle CreateHeap(std::string_view name) {
        StackResourceHandle handle;
        handle.location_ = ResourceLocation::Heap;
        handle.heapData_ = std::make_unique<std::vector<T>>();
        handle.heapData_->reserve(Capacity);  // Reserve same capacity as stack would have
        handle.name_ = name;
        return handle;
    }

    // Query location
    bool isStack() const { return location_ == ResourceLocation::Stack; }
    bool isHeap() const { return location_ == ResourceLocation::Heap; }
    ResourceLocation getLocation() const { return location_; }

    // Unified interface (works for both stack and heap)
    void push_back(const T& value) {
        if (isStack()) {
            stackData_->push_back(value);
        } else {
            heapData_->push_back(value);
        }
    }

    void push_back(T&& value) {
        if (isStack()) {
            stackData_->push_back(std::move(value));
        } else {
            heapData_->push_back(std::move(value));
        }
    }

    void clear() {
        if (isStack()) {
            stackData_->clear();
        } else {
            heapData_->clear();
        }
    }

    T* data() {
        return isStack() ? stackData_->data() : heapData_->data();
    }

    const T* data() const {
        return isStack() ? stackData_->data() : heapData_->data();
    }

    size_t size() const {
        return isStack() ? stackData_->size() : heapData_->size();
    }

    size_t capacity() const {
        return isStack() ? stackData_->capacity() : heapData_->capacity();
    }

    bool empty() const {
        return isStack() ? stackData_->empty() : heapData_->empty();
    }

    // Array access
    T& operator[](size_t index) {
        return isStack() ? (*stackData_)[index] : (*heapData_)[index];
    }

    const T& operator[](size_t index) const {
        return isStack() ? (*stackData_)[index] : (*heapData_)[index];
    }

    T& at(size_t index) {
        return isStack() ? stackData_->at(index) : heapData_->at(index);
    }

    const T& at(size_t index) const {
        return isStack() ? stackData_->at(index) : heapData_->at(index);
    }

    // Iterator support
    auto begin() { return isStack() ? stackData_->begin() : heapData_->begin(); }
    auto end() { return isStack() ? stackData_->end() : heapData_->end(); }
    auto begin() const { return isStack() ? stackData_->begin() : heapData_->begin(); }
    auto end() const { return isStack() ? stackData_->end() : heapData_->end(); }

    // Debugging info
    std::string_view getName() const { return name_; }
    uint32_t getNodeId() const { return nodeId_; }

private:
    StackResourceHandle() = default;

    ResourceLocation location_;
    std::optional<StackArray<T, Capacity>> stackData_;
    std::unique_ptr<std::vector<T>> heapData_;
    std::string_view name_;
    uint32_t nodeId_ = 0;
};

/**
 * @brief Result type for stack resource allocation
 *
 * Returns either:
 * - Success: StackResourceHandle<T, Capacity> (may be stack or heap)
 * - Failure: AllocationError
 */
template<typename T, size_t Capacity>
using StackResourceResult = std::expected<StackResourceHandle<T, Capacity>, AllocationError>;

/**
 * @brief Helper to create error messages from AllocationError
 */
inline const char* AllocationErrorMessage(AllocationError error) {
    switch (error) {
        case AllocationError::StackOverflow:
            return "Stack budget exceeded - fallback to heap also failed";
        case AllocationError::HeapOverflow:
            return "Heap budget exceeded - cannot allocate";
        case AllocationError::InvalidSize:
            return "Invalid allocation size requested";
        case AllocationError::SystemError:
            return "System allocation failed";
        default:
            return "Unknown allocation error";
    }
}

}  // namespace VIXEN
