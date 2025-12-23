#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <optional>

namespace Vixen {

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

    /// Set available extensions (called during instance creation)
    static void SetAvailableExtensions(const std::vector<std::string>& extensions);

protected:
    bool CheckAvailability() const override;

private:
    std::string extensionName_;
    static std::vector<std::string> availableExtensions_;
};

/**
 * @brief Capability node for Vulkan instance layers
 */
class InstanceLayerCapability : public CapabilityNode {
public:
    InstanceLayerCapability(const std::string& layerName)
        : CapabilityNode("InstanceLayer:" + layerName)
        , layerName_(layerName) {}

    /// Set available layers (called during instance creation)
    static void SetAvailableLayers(const std::vector<std::string>& layers);

protected:
    bool CheckAvailability() const override;

private:
    std::string layerName_;
    static std::vector<std::string> availableLayers_;
};

/**
 * @brief Capability node for Vulkan device extensions
 */
class DeviceExtensionCapability : public CapabilityNode {
public:
    DeviceExtensionCapability(const std::string& extensionName)
        : CapabilityNode("DeviceExt:" + extensionName)
        , extensionName_(extensionName) {}

    /// Set available extensions for current device (called during device creation)
    static void SetAvailableExtensions(const std::vector<std::string>& extensions);

protected:
    bool CheckAvailability() const override;

private:
    std::string extensionName_;
    static std::vector<std::string> availableExtensions_;
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

    /// Get all registered capabilities
    const std::unordered_map<std::string, std::shared_ptr<CapabilityNode>>& GetAllCapabilities() const {
        return capabilities_;
    }

private:
    std::unordered_map<std::string, std::shared_ptr<CapabilityNode>> capabilities_;

    // Helper to create and register capabilities
    template<typename T, typename... Args>
    std::shared_ptr<T> CreateCapability(const std::string& name, Args&&... args) {
        auto cap = std::make_shared<T>(std::forward<Args>(args)...);
        RegisterCapability(cap);
        return cap;
    }
};

} // namespace Vixen
