#pragma once
#include <vector>
#include <cstdint>

namespace Vixen::RenderGraph {

/**
 * @brief State tracking for resources
 *
 * Tracks lifecycle state of resources (dirty, ready, etc.)
 * Used for cache invalidation, lazy updates, and resource management.
 */
enum class ResourceState : uint8_t {
    Dirty = 0,      // Needs update/re-recording
    Ready = 1,      // Up-to-date and usable
    Stale = 2,      // Marked for deletion/rebuild
    Invalid = 3     // Cannot be used (error state)
};

/**
 * @brief Container that tracks state alongside values
 *
 * Generic container for tracking resource state (dirty, ready, etc.)
 * alongside the actual resource data.
 *
 * Example usage:
 * @code
 * StatefulContainer<VkCommandBuffer> commandBuffers;
 * commandBuffers.resize(3);
 * commandBuffers[0] = cmdBuf;
 * commandBuffers.MarkDirty(0);
 * if (commandBuffers.IsDirty(0)) {
 *     RecordCommands(commandBuffers[0]);
 *     commandBuffers.MarkReady(0);
 * }
 * @endcode
 */
template<typename T>
class StatefulContainer {
public:
    /**
     * @brief Entry combining value and state
     */
    struct Entry {
        T value = {};
        ResourceState state = ResourceState::Dirty;

        Entry() = default;
        explicit Entry(const T& val, ResourceState s = ResourceState::Dirty)
            : value(val), state(s) {}

        // Implicit conversion to T for convenience
        operator T&() { return value; }
        operator const T&() const { return value; }

        // Assignment from T
        Entry& operator=(const T& val) {
            value = val;
            return *this;
        }
    };

    // Container interface
    void resize(size_t count) { entries.resize(count); }
    size_t size() const { return entries.size(); }
    bool empty() const { return entries.empty(); }
    void clear() { entries.clear(); }

    // Element access
    Entry& operator[](size_t index) { return entries[index]; }
    const Entry& operator[](size_t index) const { return entries[index]; }

    T& GetValue(size_t index) { return entries[index].value; }
    const T& GetValue(size_t index) const { return entries[index].value; }

    // State queries
    ResourceState GetState(size_t index) const { return entries[index].state; }
    bool IsDirty(size_t index) const { return entries[index].state == ResourceState::Dirty; }
    bool IsReady(size_t index) const { return entries[index].state == ResourceState::Ready; }
    bool IsStale(size_t index) const { return entries[index].state == ResourceState::Stale; }
    bool IsInvalid(size_t index) const { return entries[index].state == ResourceState::Invalid; }

    // State mutations
    void MarkDirty(size_t index) { entries[index].state = ResourceState::Dirty; }
    void MarkReady(size_t index) { entries[index].state = ResourceState::Ready; }
    void MarkStale(size_t index) { entries[index].state = ResourceState::Stale; }
    void MarkInvalid(size_t index) { entries[index].state = ResourceState::Invalid; }

    // Batch state operations
    void MarkAllDirty() {
        for (auto& entry : entries) {
            entry.state = ResourceState::Dirty;
        }
    }

    void MarkAllReady() {
        for (auto& entry : entries) {
            entry.state = ResourceState::Ready;
        }
    }

    // Check if any entry is dirty
    bool AnyDirty() const {
        for (const auto& entry : entries) {
            if (entry.state == ResourceState::Dirty) return true;
        }
        return false;
    }

    // Count entries in specific state
    size_t CountDirty() const {
        size_t count = 0;
        for (const auto& entry : entries) {
            if (entry.state == ResourceState::Dirty) count++;
        }
        return count;
    }

    // Iterator support (for range-based for loops)
    auto begin() { return entries.begin(); }
    auto end() { return entries.end(); }
    auto begin() const { return entries.begin(); }
    auto end() const { return entries.end(); }

private:
    std::vector<Entry> entries;
};

} // namespace Vixen::RenderGraph
