#pragma once

#include "RM.h"
#include "BoundedArray.h"
#include <array>
#include <cstddef>
#include <string_view>

namespace ResourceManagement {

/**
 * @file StackAllocatedRM.h
 * @brief Stack-allocated resource wrapper with integrated tracking
 *
 * Extends RM<T> to support stack-allocated arrays with automatic
 * size tracking and optional StackTracker integration.
 *
 * Key Features:
 * - Fixed-capacity arrays on the stack (std::array)
 * - Automatic size tracking (count variable)
 * - Compatible with RM<T> state management
 * - Vector-like API for easy migration
 * - Zero-overhead in release builds
 *
 * Use Cases:
 * - Per-frame temporary buffers
 * - Bounded Vulkan resource arrays
 * - Hot path optimizations
 *
 * Usage:
 * @code
 * // Instead of: std::vector<VkWriteDescriptorSet> writes;
 * StackAllocatedRM<VkWriteDescriptorSet, 32> writes("DescriptorNode:writes");
 *
 * writes.Add(write1);
 * writes.Add(write2);
 * vkUpdateDescriptorSets(device, writes.Size(), writes.Data(), ...);
 * @endcode
 */
template<typename T, size_t Capacity>
class StackAllocatedRM : public RM<BoundedArray<T, Capacity>> {
    using Base = RM<BoundedArray<T, Capacity>>;
    using ArrayType = BoundedArray<T, Capacity>;

public:
    /**
     * @brief Construct with debug name
     *
     * @param debugName Name for debugging and tracking
     */
    explicit StackAllocatedRM(std::string_view debugName = "unnamed")
        : Base()
        , debugName_(debugName)
    {
        // Initialize with empty array
        Base::Set(ArrayType{});
    }

    // ========================================================================
    // Vector-like interface (delegates to BoundedArray)
    // ========================================================================

    /**
     * @brief Add element to the array
     * @throws std::overflow_error if array is full
     */
    void Add(const T& value) {
        EnsureReady();
        Base::Value().Add(value);
        Base::IncrementGeneration();
    }

    void Add(T&& value) {
        EnsureReady();
        Base::Value().Add(std::move(value));
        Base::IncrementGeneration();
    }

    /**
     * @brief Try to add element, returns false if full
     */
    bool TryAdd(const T& value) {
        EnsureReady();
        bool result = Base::Value().TryAdd(value);
        if (result) Base::IncrementGeneration();
        return result;
    }

    bool TryAdd(T&& value) {
        EnsureReady();
        bool result = Base::Value().TryAdd(std::move(value));
        if (result) Base::IncrementGeneration();
        return result;
    }

    /**
     * @brief Emplace element in-place
     */
    template<typename... Args>
    T& Emplace(Args&&... args) {
        EnsureReady();
        T& ref = Base::Value().Emplace(std::forward<Args>(args)...);
        Base::IncrementGeneration();
        return ref;
    }

    /**
     * @brief Clear all elements
     */
    void Clear() {
        EnsureReady();
        Base::Value().Clear();
        Base::IncrementGeneration();
    }

    /**
     * @brief Resize the array
     */
    void Resize(size_t newSize) {
        EnsureReady();
        Base::Value().Resize(newSize);
        Base::IncrementGeneration();
    }

    /**
     * @brief Remove last element
     */
    void PopBack() {
        EnsureReady();
        Base::Value().PopBack();
        Base::IncrementGeneration();
    }

    // ========================================================================
    // Accessors
    // ========================================================================

    T* Data() { return Base::Value().Data(); }
    const T* Data() const { return Base::Value().Data(); }

    size_t Size() const { return Base::Ready() ? Base::Value().Size() : 0; }
    constexpr size_t GetCapacity() const { return Capacity; }
    bool Empty() const { return Size() == 0; }
    bool Full() const { return Size() >= Capacity; }

    size_t RemainingCapacity() const { return Capacity - Size(); }

    T& operator[](size_t index) { return Base::Value()[index]; }
    const T& operator[](size_t index) const { return Base::Value()[index]; }

    T& At(size_t index) { return Base::Value().At(index); }
    const T& At(size_t index) const { return Base::Value().At(index); }

    T& Front() { return Base::Value().Front(); }
    const T& Front() const { return Base::Value().Front(); }

    T& Back() { return Base::Value().Back(); }
    const T& Back() const { return Base::Value().Back(); }

    // ========================================================================
    // Iterators
    // ========================================================================

    auto begin() { return Base::Value().begin(); }
    auto end() { return Base::Value().end(); }
    auto begin() const { return Base::Value().begin(); }
    auto end() const { return Base::Value().end(); }

    // ========================================================================
    // Memory info (for budget tracking)
    // ========================================================================

    static constexpr size_t StorageBytes() { return sizeof(ArrayType); }
    static constexpr size_t ElementSize() { return sizeof(T); }

    std::string_view GetDebugName() const { return debugName_; }

private:
    std::string_view debugName_;

    void EnsureReady() {
        if (!Base::Ready()) {
            Base::Set(ArrayType{});
        }
    }
};

// ============================================================================
// CONVENIENCE TYPE ALIASES
// ============================================================================

/**
 * @brief Stack-allocated array of up to 4 elements
 */
template<typename T>
using Stack4 = StackAllocatedRM<T, 4>;

/**
 * @brief Stack-allocated array of up to 8 elements
 */
template<typename T>
using Stack8 = StackAllocatedRM<T, 8>;

/**
 * @brief Stack-allocated array of up to 16 elements
 */
template<typename T>
using Stack16 = StackAllocatedRM<T, 16>;

/**
 * @brief Stack-allocated array of up to 32 elements
 */
template<typename T>
using Stack32 = StackAllocatedRM<T, 32>;

/**
 * @brief Stack-allocated array of up to 64 elements
 */
template<typename T>
using Stack64 = StackAllocatedRM<T, 64>;

} // namespace ResourceManagement
