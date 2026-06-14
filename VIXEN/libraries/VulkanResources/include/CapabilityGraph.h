#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <optional>

namespace Vixen {

class CapabilityGraph;  // owning graph; leaf nodes consult its availability sets (AR#8 — was statics)

/**
 * @brief Base class for GPU capability nodes
 *
 * Represents a single capability that can be queried at runtime.
 * Capabilities form a dependency graph where complex features depend on simpler ones.
 */
class CapabilityNode {
public:
    explicit CapabilityNode(const std::string& name) : name_(name) {}
    virtual ~CapabilityNode() = default;

    /// Get capability name
    const std::string& GetName() const { return name_; }

    /// Check if this capability is available (cached)
    bool IsAvailable() const {
        if (!cachedResult_.has_value()) {
            cachedResult_ = CheckAvailability();
        }
        return *cachedResult_;
    }

    /// Force recheck of availability (clears cache)
    void Invalidate() { cachedResult_.reset(); }

    /// Set the owning graph (AR#8: leaf nodes consult the graph's per-instance availability sets in
    /// CheckAvailability() instead of the former process-wide static vectors). Called by
    /// CapabilityGraph::RegisterCapability.
    void SetOwningGraph(const CapabilityGraph* graph) noexcept { graph_ = graph; }

    /// Add a dependency node
    void AddDependency(std::shared_ptr<CapabilityNode> dep) {
        dependencies_.push_back(dep);
    }

    /// Get all dependencies
    const std::vector<std::shared_ptr<CapabilityNode>>& GetDependencies() const {
        return dependencies_;
    }

protected:
    /// Override to implement availability check logic
    virtual bool CheckAvailability() const = 0;

    /// Check if all dependencies are satisfied
    bool AreDependenciesSatisfied() const {
        for (const auto& dep : dependencies_) {
            if (!dep->IsAvailable()) {
                return false;
            }
        }
        return true;
    }

    /// Owning graph (AR#8): leaf nodes read its per-instance availability sets in CheckAvailability()
    /// instead of process-wide statics. Null until the node is registered with a graph.
    const CapabilityGraph* graph_ = nullptr;

private:
    std::string name_;
    std::vector<std::shared_ptr<CapabilityNode>> dependencies_;
    mutable std::optional<bool> cachedResult_;
};

/**
 * @brief Capability node for Vulkan instance extensions
 */
class InstanceExtensionCapability : public CapabilityNode {
public:
    InstanceExtensionCapability(const std::string& extensionName)
        : CapabilityNode("InstanceExt:" + extensionName)
        , extensionName_(extensionName) {}

protected:
    bool CheckAvailability() const override;  // consults the owning graph's instance-extension set (AR#8)

private:
    std::string extensionName_;
};

/**
 * @brief Capability node for Vulkan instance layers
 */
class InstanceLayerCapability : public CapabilityNode {
public:
    InstanceLayerCapability(const std::string& layerName)
        : CapabilityNode("InstanceLayer:" + layerName)
        , layerName_(layerName) {}

protected:
    bool CheckAvailability() const override;  // consults the owning graph's instance-layer set (AR#8)

private:
    std::string layerName_;
};

/**
 * @brief Capability node for Vulkan device extensions
 */
class DeviceExtensionCapability : public CapabilityNode {
public:
    DeviceExtensionCapability(const std::string& extensionName)
        : CapabilityNode("DeviceExt:" + extensionName)
        , extensionName_(extensionName) {}

protected:
    bool CheckAvailability() const override;  // consults the owning graph's device-extension set (AR#8)

private:
    std::string extensionName_;
};

/**
 * @brief Capability node for Vulkan device features (non-concrete / optional)
 *
 * Mirrors DeviceExtensionCapability, but for device features queried via
 * vkGetPhysicalDeviceFeatures2 (e.g. timelineSemaphore). The supported-feature set is
 * populated from the physical device during device creation, so every non-concrete
 * feature is gated through the capability graph rather than ad-hoc inline queries —
 * centralising all capability checks under one convention.
 */
class DeviceFeatureCapability : public CapabilityNode {
public:
    DeviceFeatureCapability(const std::string& featureName)
        : CapabilityNode("DeviceFeature:" + featureName)
        , featureName_(featureName) {}

protected:
    bool CheckAvailability() const override;  // consults the owning graph's device-feature set (AR#8)

private:
    std::string featureName_;
};

/**
 * @brief Composite capability node that depends on other capabilities
 *
 * A composite capability is satisfied only if ALL its dependencies are satisfied.
 */
class CompositeCapability : public CapabilityNode {
public:
    explicit CompositeCapability(const std::string& name)
        : CapabilityNode(name) {}

protected:
    bool CheckAvailability() const override {
        return AreDependenciesSatisfied();
    }
};

/**
 * @brief GPU Capability Graph
 *
 * Manages a dependency graph of GPU capabilities.
 * Provides registry of known capabilities and query interface.
 *
 * AR#8: the available-extension/layer/feature sets are per-graph instance state (a CapabilityGraph
 * is owned per VulkanDevice). Instance-level sets are self-populated from the loader by
 * BuildStandardCapabilities (instance availability is globally queryable, no VkInstance needed);
 * device-level sets are supplied by the owning VulkanDevice once its physical device is known.
 * This replaces the former process-wide static vectors, so multiple devices/engines in one process
 * never clobber each other's capability state.
 */
class CapabilityGraph {
public:
    CapabilityGraph() = default;

    /// Register a capability node
    void RegisterCapability(std::shared_ptr<CapabilityNode> capability);

    /// Get capability by name
    std::shared_ptr<CapabilityNode> GetCapability(const std::string& name) const;

    /// Check if a capability exists and is available
    bool IsCapabilityAvailable(const std::string& name) const;

    /// Build standard Vulkan capability graph
    void BuildStandardCapabilities();

    /// Invalidate all cached results (call when device/instance changes)
    void InvalidateAll();

    // --- Available-capability sets (AR#8: per-graph instance state; replaces process-wide statics).
    // Instance-level sets are self-populated by BuildStandardCapabilities. Device-level sets are
    // supplied by the owning VulkanDevice after the physical device is selected.
    void SetAvailableInstanceExtensions(std::vector<std::string> extensions);
    void SetAvailableInstanceLayers(std::vector<std::string> layers);
    void SetAvailableDeviceExtensions(std::vector<std::string> extensions);
    void SetAvailableDeviceFeatures(std::vector<std::string> features);

    bool IsInstanceExtensionAvailable(const std::string& name) const;
    bool IsInstanceLayerAvailable(const std::string& name) const;
    bool IsDeviceExtensionAvailable(const std::string& name) const;
    bool IsDeviceFeatureAvailable(const std::string& name) const;

    /// Get all registered capabilities
    const std::unordered_map<std::string, std::shared_ptr<CapabilityNode>>& GetAllCapabilities() const {
        return capabilities_;
    }

private:
    std::unordered_map<std::string, std::shared_ptr<CapabilityNode>> capabilities_;

    // Availability sets consulted by the leaf capability nodes (AR#8: per-instance, were static).
    std::vector<std::string> availableInstanceExtensions_;
    std::vector<std::string> availableInstanceLayers_;
    std::vector<std::string> availableDeviceExtensions_;
    std::vector<std::string> availableDeviceFeatures_;

    // Helper to create and register capabilities
    template<typename T, typename... Args>
    std::shared_ptr<T> CreateCapability(const std::string& name, Args&&... args) {
        auto cap = std::make_shared<T>(std::forward<Args>(args)...);
        RegisterCapability(cap);
        return cap;
    }
};

} // namespace Vixen
