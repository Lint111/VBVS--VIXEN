#pragma once

#include "TypedNodeInstance.h"
#include "ResourceVariant.h"
#include <vector>

namespace Vixen::RenderGraph {

/**
 * @brief Extension of TypedNode that supports variadic inputs
 *
 * Adds support for arbitrary number of additional input connections beyond
 * the statically-defined slots in the config. Useful for nodes that need
 * to accept a dynamic number of resources (e.g., DescriptorResourceGathererNode).
 *
 * Usage:
 * ```cpp
 * class MyNode : public VariadicTypedNode<MyNodeConfig> {
 *     void CompileImpl(Context& ctx) override {
 *         // Access regular typed slots
 *         auto bundle = INPUT(SHADER_DATA_BUNDLE);
 *
 *         // Access variadic inputs
 *         auto& variadics = GetVariadicInputs();
 *         for (size_t i = 0; i < variadics.size(); ++i) {
 *             auto resource = GetVariadicInput<VkImageView>(i);
 *             // ... validate/process resource
 *         }
 *     }
 * };
 * ```
 */
template<typename ConfigType>
class VariadicTypedNode : public TypedNode<ConfigType> {
public:
    using Base = TypedNode<ConfigType>;
    using Context = typename Base::Context;

    VariadicTypedNode(const std::string& instanceName, NodeType* nodeType)
        : Base(instanceName, nodeType) {}

    virtual ~VariadicTypedNode() = default;

    /**
     * @brief Set variadic input count constraints
     *
     * Enforces minimum and maximum number of variadic inputs during validation.
     * Default: min=0, max=unlimited (SIZE_MAX)
     *
     * @param min Minimum required inputs (inclusive)
     * @param max Maximum allowed inputs (inclusive)
     */
    void SetVariadicInputConstraints(size_t min, size_t max = SIZE_MAX) {
        minVariadicInputs_ = min;
        maxVariadicInputs_ = max;
    }

    /**
     * @brief Get minimum variadic input count
     */
    size_t GetMinVariadicInputs() const { return minVariadicInputs_; }

    /**
     * @brief Get maximum variadic input count
     */
    size_t GetMaxVariadicInputs() const { return maxVariadicInputs_; }

    /**
     * @brief Add a variadic input connection
     *
     * Called by the render graph when connecting additional inputs beyond
     * the statically-defined slots.
     *
     * @param resource Resource to connect
     */
    void AddVariadicInput(Resource* resource) {
        if (resource) {
            variadicInputs_.push_back(resource);
        }
    }

    /**
     * @brief Get all variadic input resources
     *
     * @return Vector of resource pointers
     */
    const std::vector<Resource*>& GetVariadicInputs() const {
        return variadicInputs_;
    }

    /**
     * @brief Get variadic input count
     *
     * @return Number of variadic inputs connected
     */
    size_t GetVariadicInputCount() const {
        return variadicInputs_.size();
    }

    /**
     * @brief Get variadic input resource at index
     *
     * @param index Variadic input index (0-based)
     * @return Resource pointer, or nullptr if invalid index
     */
    Resource* GetVariadicInputResource(size_t index) const {
        if (index < variadicInputs_.size()) {
            return variadicInputs_[index];
        }
        return nullptr;
    }

    /**
     * @brief Get typed variadic input handle at index
     *
     * @tparam T Handle type (e.g., VkImageView, VkBuffer)
     * @param index Variadic input index (0-based)
     * @return Typed handle value, or null handle if invalid index/type
     */
    template<typename T>
    T GetVariadicInput(size_t index) const {
        Resource* res = GetVariadicInputResource(index);
        if (!res) return T{};
        return res->GetHandle<T>();
    }

    /**
     * @brief Get variadic input as ResourceHandleVariant
     *
     * Useful for generic processing without knowing the exact type.
     *
     * @param index Variadic input index (0-based)
     * @return ResourceHandleVariant containing the handle
     */
    ResourceHandleVariant GetVariadicInputVariant(size_t index) const {
        Resource* res = GetVariadicInputResource(index);
        if (!res || !res->IsValid()) {
            return std::monostate{};
        }

        // Extract variant from resource
        // Note: Resource class would need to expose the variant directly
        // For now, we need to know the type. This is a placeholder.
        // TODO: Add Resource::GetHandleVariant() method
        return std::monostate{};  // Placeholder
    }

    /**
     * @brief Clear all variadic inputs
     *
     * Typically called during cleanup or graph rebuild.
     */
    void ClearVariadicInputs() {
        variadicInputs_.clear();
    }

protected:
    /**
     * @brief Generic validation hook for variadic inputs
     *
     * Override this in derived classes to implement domain-specific validation.
     * Called during CompileImpl() to validate variadic inputs.
     *
     * Default implementation does:
     * 1. Count validation (min/max constraints)
     * 2. Null checks
     *
     * @param ctx Compile context
     * @return true if validation passed, false otherwise
     */
    virtual bool ValidateVariadicInputsImpl(Context& ctx) {
        size_t count = variadicInputs_.size();

        // Validate count constraints
        if (count < minVariadicInputs_) {
            std::cout << "[VariadicTypedNode::ValidateVariadicInputsImpl] ERROR: "
                      << "Too few variadic inputs. Expected at least " << minVariadicInputs_
                      << ", got " << count << "\n";
            return false;
        }

        if (count > maxVariadicInputs_) {
            std::cout << "[VariadicTypedNode::ValidateVariadicInputsImpl] ERROR: "
                      << "Too many variadic inputs. Expected at most " << maxVariadicInputs_
                      << ", got " << count << "\n";
            return false;
        }

        // Check for null resources
        for (size_t i = 0; i < count; ++i) {
            if (!variadicInputs_[i]) {
                std::cout << "[VariadicTypedNode::ValidateVariadicInputsImpl] ERROR: "
                          << "Variadic input " << i << " is null\n";
                return false;
            }
        }

        return true;
    }

    // Variadic input storage
    std::vector<Resource*> variadicInputs_;

    // Variadic input count constraints
    size_t minVariadicInputs_ = 0;         // Minimum required (default: none)
    size_t maxVariadicInputs_ = SIZE_MAX;  // Maximum allowed (default: unlimited)
};

} // namespace Vixen::RenderGraph
