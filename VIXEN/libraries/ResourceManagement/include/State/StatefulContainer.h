#pragma once
#include <vector>
#include <cstdint>

namespace ResourceManagement {

/**
 * @brief Simple state tracking for container entries
 *
 * Tracks lifecycle state of container elements (dirty, ready, etc.)
 * Used for cache invalidation, lazy updates, and resource management.
 *
 * Note: This is simpler than ResourceState bitflags - use ContainerState
 * for simple dirty-tracking, ResourceState for complex state combinations.
 */
enum class ContainerState : uint8_t {
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
        ContainerState state = ContainerState::Dirty;

        Entry() = default;
        explicit Entry(const T& val, ContainerState s = ContainerState::Dirty)
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
    ContainerState GetState(size_t index) const { return entries[index].state; }
    bool IsDirty(size_t index) const { return entries[index].state == ContainerState::Dirty; }
    bool IsReady(size_t index) const { return entries[index].state == ContainerState::Ready; }
    bool IsStale(size_t index) const { return entries[index].state == ContainerState::Stale; }
    bool IsInvalid(size_t index) const { return entries[index].state == ContainerState::Invalid; }

    // State mutations
    void MarkDirty(size_t index) { entries[index].state = ContainerState::Dirty; }
    void MarkReady(size_t index) { entries[index].state = ContainerState::Ready; }
    void MarkStale(size_t index) { entries[index].state = ContainerState::Stale; }
    void MarkInvalid(size_t index) { entries[index].state = ContainerState::Invalid; }

    // Batch state operations
    void MarkAllDirty() {
        for (auto& entry : entries) {
            entry.state = ContainerState::Dirty;
        }
    }

    void MarkAllReady() {
        for (auto& entry : entries) {
            entry.state = ContainerState::Ready;
        }
    }

    // Check if any entry is dirty
    bool AnyDirty() const {
        for (const auto& entry : entries) {
            if (entry.state == ContainerState::Dirty) return true;
        }
        return false;
    }

    // Count entries in specific state
    size_t CountDirty() const {
        size_t count = 0;
        for (const auto& entry : entries) {
            if (entry.state == ContainerState::Dirty) count++;
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

} // namespace ResourceManagement

// Backwards compatibility: alias in RenderGraph namespace
namespace Vixen::RenderGraph {
    using ResourceManagement::ContainerState;
    using ResourceManagement::StatefulContainer;
    // Deprecated alias for old code
    using ResourceState = ResourceManagement::ContainerState;
}
