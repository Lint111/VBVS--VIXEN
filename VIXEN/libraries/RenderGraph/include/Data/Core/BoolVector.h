#pragma once
#include <vector>

namespace Vixen::RenderGraph::Data
{
    

/**
 * @brief Wrapper for std::vector<bool> to use in ResourceV3 type system
 *
 * std::vector<bool> has specializations that break std::variant usage.
 * This wrapper provides proper copy/move semantics and implicit conversions.
 */
struct BoolVector {
    std::vector<bool> data;
    
    BoolVector() = default;
    BoolVector(const BoolVector& other) : data(other.data) {}
    BoolVector(BoolVector&& other) noexcept : data(std::move(other.data)) {}
    BoolVector& operator=(const BoolVector& other) {
        if (this != &other) data = other.data;
        return *this;
    }
    BoolVector& operator=(BoolVector&& other) noexcept {
        if (this != &other) data = std::move(other.data);
        return *this;
    }
    
    // Implicit conversion from std::vector<bool>
    BoolVector(const std::vector<bool>& v) : data(v) {}
    BoolVector(std::vector<bool>&& v) : data(std::move(v)) {}
    
    // Implicit conversion to std::vector<bool>
    operator std::vector<bool>&() { return data; }
    operator const std::vector<bool>&() const { return data; }
    
    // Convenience methods
    auto begin() { return data.begin(); }
    auto end() { return data.end(); }
    auto begin() const { return data.begin(); }
    auto end() const { return data.end(); }
    size_t size() const { return data.size(); }
    bool empty() const { return data.empty(); }
    bool operator[](size_t i) const { return data[i]; }
    auto operator[](size_t i) { return data[i]; }
};

} // namespace Vixen::RenderGraph::Data
