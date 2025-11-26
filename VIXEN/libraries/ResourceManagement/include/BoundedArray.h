#pragma once

#include <array>
#include <cstddef>
#include <stdexcept>
#include <cassert>

namespace ResourceManagement {

/**
 * @file BoundedArray.h
 * @brief Fixed-capacity array with dynamic count tracking
 *
 * Combines std::array storage with std::vector-like semantics.
 * Designed for stack allocation in hot paths where maximum size is known.
 *
 * Benefits over std::vector:
 * - Zero heap allocations
 * - Cache-friendly (contiguous, predictable size)
 * - Compile-time capacity known
 *
 * Usage:
 * @code
 * BoundedArray<VkImageView, 4> views;
 * views.Add(view1);
 * views.Add(view2);
 * vkCreateFramebuffer(..., views.Size(), views.Data());
 * @endcode
 */
template<typename T, size_t N>
class BoundedArray {
public:
    using value_type = T;
    using size_type = size_t;
    using iterator = T*;
    using const_iterator = const T*;

    BoundedArray() : count_(0) {}

    // ========================================================================
    // Element access
    // ========================================================================

    T& operator[](size_t index) {
        assert(index < count_ && "BoundedArray index out of bounds");
        return data_[index];
    }

    const T& operator[](size_t index) const {
        assert(index < count_ && "BoundedArray index out of bounds");
        return data_[index];
    }

    T& At(size_t index) {
        if (index >= count_) {
            throw std::out_of_range("BoundedArray::At index out of bounds");
        }
        return data_[index];
    }

    const T& At(size_t index) const {
        if (index >= count_) {
            throw std::out_of_range("BoundedArray::At index out of bounds");
        }
        return data_[index];
    }

    T& Front() { return data_[0]; }
    const T& Front() const { return data_[0]; }

    T& Back() { return data_[count_ - 1]; }
    const T& Back() const { return data_[count_ - 1]; }

    T* Data() { return data_.data(); }
    const T* Data() const { return data_.data(); }

    // ========================================================================
    // Capacity
    // ========================================================================

    bool Empty() const { return count_ == 0; }
    bool Full() const { return count_ >= N; }
    size_t Size() const { return count_; }
    constexpr size_t Capacity() const { return N; }
    constexpr size_t MaxSize() const { return N; }

    size_t RemainingCapacity() const { return N - count_; }

    // ========================================================================
    // Modifiers
    // ========================================================================

    void Add(const T& value) {
        if (count_ >= N) {
            throw std::overflow_error("BoundedArray::Add overflow - capacity exceeded");
        }
        data_[count_++] = value;
    }

    void Add(T&& value) {
        if (count_ >= N) {
            throw std::overflow_error("BoundedArray::Add overflow - capacity exceeded");
        }
        data_[count_++] = std::move(value);
    }

    /**
     * @brief Add element if space available, return success
     */
    bool TryAdd(const T& value) {
        if (count_ >= N) return false;
        data_[count_++] = value;
        return true;
    }

    bool TryAdd(T&& value) {
        if (count_ >= N) return false;
        data_[count_++] = std::move(value);
        return true;
    }

    /**
     * @brief Emplace element in-place
     */
    template<typename... Args>
    T& Emplace(Args&&... args) {
        if (count_ >= N) {
            throw std::overflow_error("BoundedArray::Emplace overflow");
        }
        data_[count_] = T(std::forward<Args>(args)...);
        return data_[count_++];
    }

    void PopBack() {
        if (count_ > 0) {
            --count_;
        }
    }

    void Clear() { count_ = 0; }

    /**
     * @brief Resize array (new elements default-initialized)
     */
    void Resize(size_t newSize) {
        if (newSize > N) {
            throw std::overflow_error("BoundedArray::Resize exceeds capacity");
        }
        // Default-initialize new elements if growing
        for (size_t i = count_; i < newSize; ++i) {
            data_[i] = T{};
        }
        count_ = newSize;
    }

    /**
     * @brief Fill with value up to count
     */
    void Fill(const T& value, size_t count) {
        if (count > N) {
            throw std::overflow_error("BoundedArray::Fill exceeds capacity");
        }
        for (size_t i = 0; i < count; ++i) {
            data_[i] = value;
        }
        count_ = count;
    }

    // ========================================================================
    // Iterators
    // ========================================================================

    iterator begin() { return data_.data(); }
    iterator end() { return data_.data() + count_; }

    const_iterator begin() const { return data_.data(); }
    const_iterator end() const { return data_.data() + count_; }

    const_iterator cbegin() const { return data_.data(); }
    const_iterator cend() const { return data_.data() + count_; }

    // ========================================================================
    // Comparison
    // ========================================================================

    bool operator==(const BoundedArray& other) const {
        if (count_ != other.count_) return false;
        for (size_t i = 0; i < count_; ++i) {
            if (data_[i] != other.data_[i]) return false;
        }
        return true;
    }

    bool operator!=(const BoundedArray& other) const {
        return !(*this == other);
    }

    // ========================================================================
    // Memory info (for budget tracking)
    // ========================================================================

    static constexpr size_t StorageSize() { return sizeof(std::array<T, N>); }
    static constexpr size_t ElementSize() { return sizeof(T); }

private:
    std::array<T, N> data_;
    size_t count_;
};

} // namespace ResourceManagement
