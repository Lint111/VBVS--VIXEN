#pragma once

#include "ResourceState.h"
#include <optional>
#include <string>
#include <unordered_map>
#include <any>
#include <stdexcept>

namespace ResourceManagement {

/**
 * @brief Resource wrapper with state management and metadata
 * 
 * Provides:
 * - std::optional-like interface (Ready check before access)
 * - State tracking (Outdated, Locked, Stale, Pending)
 * - Generation tracking for cache invalidation
 * - Arbitrary metadata storage
 * - Thread-safe state queries
 * 
 * Usage patterns:
 * 
 * **Pattern 1: Optional-like access**
 * ```cpp
 * RM<VkPipeline> pipeline;
 * 
 * if (pipeline.Ready()) {
 *     vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.Value());
 * }
 * ```
 * 
 * **Pattern 2: State-based cleanup**
 * ```cpp
 * if (pipeline.Has(ResourceState::Outdated)) {
 *     vkDestroyPipeline(device, pipeline.Value(), nullptr);
 *     pipeline.Reset();
 *     RecreatePipeline();
 * }
 * ```
 * 
 * **Pattern 3: Generation tracking**
 * ```cpp
 * if (shaderResource.GetGeneration() != cachedShaderGeneration) {
 *     RebuildPipeline();
 *     cachedShaderGeneration = shaderResource.GetGeneration();
 * }
 * ```
 * 
 * **Pattern 4: Metadata**
 * ```cpp
 * textureResource.SetMetadata("file_path", std::string("/textures/diffuse.png"));
 * textureResource.SetMetadata("mip_levels", uint32_t(8));
 * 
 * std::string path = textureResource.GetMetadata<std::string>("file_path");
 * ```
 */
template<typename T>
class RM {
public:
    RM() = default;

    explicit RM(T value)
        : storage(std::move(value))
        , state(ResourceState::Ready) {}

    // ========================================================================
    // Value Access (Optional-like interface)
    // ========================================================================

    /**
     * @brief Check if resource is ready for use
     * 
     * Equivalent to std::optional::has_value()
     */
    bool Ready() const {
        return storage.has_value() && HasState(state, ResourceState::Ready);
    }

    /**
     * @brief Get resource value (throws if not ready)
     * 
     * Equivalent to std::optional::value()
     */
    T& Value() {
        if (!Ready()) {
            throw std::runtime_error("RM::Value() called on unready resource");
        }
        return *storage;
    }

    const T& Value() const {
        if (!Ready()) {
            throw std::runtime_error("RM::Value() called on unready resource");
        }
        return *storage;
    }

    /**
     * @brief Get resource value or default
     * 
     * Equivalent to std::optional::value_or()
     */
    T ValueOr(const T& defaultValue) const {
        return Ready() ? *storage : defaultValue;
    }

    /**
     * @brief Access value without check (unsafe, use with Ready() guard)
     * 
     * Equivalent to std::optional::operator*()
     */
    T& operator*() { return *storage; }
    const T& operator*() const { return *storage; }

    T* operator->() { return &(*storage); }
    const T* operator->() const { return &(*storage); }

    /**
     * @brief Bool conversion for if checks
     * 
     * Usage: if (resource) { use(resource.Value()); }
     */
    explicit operator bool() const { return Ready(); }

    // ========================================================================
    // Value Mutation
    // ========================================================================

    /**
     * @brief Set resource value and mark ready
     */
    void Set(T value) {
        storage = std::move(value);
        state = state | ResourceState::Ready;
        generation++;
    }

    /**
     * @brief Clear resource value and reset state
     */
    void Reset() {
        storage.reset();
        state = ResourceState::Uninitialized;
        metadata.clear();
    }

    // ========================================================================
    // State Management
    // ========================================================================

    /**
     * @brief Check if resource has specific state(s)
     * 
     * Usage: if (resource.Has(ResourceState::Outdated)) { ... }
     */
    bool Has(ResourceState checkState) const {
        return HasState(state, checkState);
    }

    /**
     * @brief Get current resource state
     */
    ResourceState GetState() const {
        return state;
    }

    /**
     * @brief Set resource state (replaces existing)
     */
    void SetState(ResourceState newState) {
        state = newState;
    }

    /**
     * @brief Add state flag(s)
     */
    void AddState(ResourceState flags) {
        state = state | flags;
    }

    /**
     * @brief Remove state flag(s)
     */
    void RemoveState(ResourceState flags) {
        state = static_cast<ResourceState>(
            static_cast<uint32_t>(state) & ~static_cast<uint32_t>(flags)
        );
    }

    /**
     * @brief Mark resource as outdated (needs update)
     */
    void MarkOutdated() {
        AddState(ResourceState::Outdated);
        RemoveState(ResourceState::Ready);
    }

    /**
     * @brief Mark resource as ready (clear outdated)
     */
    void MarkReady() {
        AddState(ResourceState::Ready);
        RemoveState(ResourceState::Outdated | ResourceState::Pending | ResourceState::Failed);
    }

    /**
     * @brief Lock resource (prevent modification)
     */
    void Lock() {
        AddState(ResourceState::Locked);
    }

    /**
     * @brief Unlock resource
     */
    void Unlock() {
        RemoveState(ResourceState::Locked);
    }

    /**
     * @brief Check if locked
     */
    bool IsLocked() const {
        return Has(ResourceState::Locked);
    }

    // ========================================================================
    // Generation Tracking (Cache Invalidation)
    // ========================================================================

    /**
     * @brief Get resource generation
     * 
     * Increments on every Set() call. Use to detect stale caches.
     */
    uint64_t GetGeneration() const {
        return generation;
    }

    /**
     * @brief Manually increment generation
     */
    void IncrementGeneration() {
        generation++;
    }

    // ========================================================================
    // Metadata (Key-Value Storage)
    // ========================================================================

    /**
     * @brief Set metadata value
     * 
     * Usage: resource.SetMetadata("file_path", std::string("/path/to/file"));
     */
    template<typename MetaType>
    void SetMetadata(const std::string& key, MetaType value) {
        metadata[key] = std::move(value);
    }

    /**
     * @brief Get metadata value (throws if not found)
     */
    template<typename MetaType>
    MetaType GetMetadata(const std::string& key) const {
        auto it = metadata.find(key);
        if (it == metadata.end()) {
            throw std::runtime_error("Metadata key not found: " + key);
        }
        return std::any_cast<MetaType>(it->second);
    }

    /**
     * @brief Get metadata value or default
     */
    template<typename MetaType>
    MetaType GetMetadataOr(const std::string& key, const MetaType& defaultValue) const {
        auto it = metadata.find(key);
        if (it == metadata.end()) {
            return defaultValue;
        }
        try {
            return std::any_cast<MetaType>(it->second);
        } catch (const std::bad_any_cast&) {
            return defaultValue;
        }
    }

    /**
     * @brief Check if metadata key exists
     */
    bool HasMetadata(const std::string& key) const {
        return metadata.find(key) != metadata.end();
    }

    /**
     * @brief Remove metadata key
     */
    void RemoveMetadata(const std::string& key) {
        metadata.erase(key);
    }

    /**
     * @brief Clear all metadata
     */
    void ClearMetadata() {
        metadata.clear();
    }

private:
    std::optional<T> storage;
    ResourceState state = ResourceState::Uninitialized;
    uint64_t generation = 0;
    std::unordered_map<std::string, std::any> metadata;
};

} // namespace ResourceManagement
