#pragma once

#include "TypedNodeInstance.h"
#include "Data/Core/ResourceVariant.h"
#include "IGraphCompilable.h"
#include <vector>

namespace Vixen::RenderGraph {

/**
 * @brief Slot state lifecycle tracking
 *
 * Tracks the validation state of variadic slots through the compilation pipeline:
 * - Tentative: Created during ConnectVariadic, not yet validated
 * - Validated: Type-checked during Compile phase
 * - Compiled: Finalized with Vulkan resources created
 * - Invalid: Validation failed, slot cannot be used
 */
enum class SlotState {
    Tentative,    // Created during connection, unvalidated
    Validated,    // Type-checked during Compile
    Compiled,     // Finalized with resources
    Invalid       // Validation failed
};

/**
 * @brief Variadic slot metadata (per-bundle)
 *
 * Stores metadata for a variadic input within a specific bundle.
 * Variadic slots are dynamically discovered (e.g., from shader reflection)
 * rather than statically defined in the config.
 */
struct VariadicSlotInfo {
    Resource* resource = nullptr;      // Resource pointer for this variadic slot
    ResourceType resourceType;         // Expected resource type
    std::string slotName;              // Descriptive name (e.g., "sampled_image_0")
    uint32_t binding = 0;              // Shader binding index (for descriptor-based nodes)
    VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_MAX_ENUM;  // Descriptor type (if applicable)

    // Phase separation support
    SlotState state = SlotState::Tentative;  // Current validation state
    NodeHandle sourceNode;                   // Source node for connection tracking
    uint32_t sourceOutput = 0;               // Source output slot index

    // Field extraction support (for ConnectVariadic with member pointers)
    size_t fieldOffset = 0;                  // Offset of field in struct (0 = no extraction)
    bool hasFieldExtraction = false;         // True if field extraction is needed

    // Slot role tracking (reuses NodeInstance::SlotRole enum)
    SlotRole slotRole = SlotRole::Dependency;  // Dependency (Compile-time) or ExecuteOnly (transient)
};

/**
 * @brief Per-bundle variadic slot storage
 *
 * Extends the NodeInstance::Bundle concept with variadic slot support.
 * Each variadic node maintains its own list of these bundles in parallel
 * with the base NodeInstance bundles.
 */
struct VariadicBundle {
    std::vector<VariadicSlotInfo> variadicSlots;  // Variadic inputs with metadata
};

/**
 * @brief Interface for variadic node operations
 *
 * Provides polymorphic access to variadic slot management without
 * requiring knowledge of the specific ConfigType template parameter.
 */
class IVariadicNode {
public:
    virtual ~IVariadicNode() = default;

    /**
     * @brief Update or create a variadic slot (polymorphic interface)
     */
    virtual void UpdateVariadicSlot(size_t slotIndex, const VariadicSlotInfo& slotInfo, size_t bundleIndex = 0) = 0;

    /**
     * @brief Get variadic slot metadata (polymorphic interface)
     */
    virtual const VariadicSlotInfo* GetVariadicSlotInfo(size_t slotIndex, size_t bundleIndex = 0) const = 0;
};

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
class VariadicTypedNode : public TypedNode<ConfigType>,
                         public IGraphCompilable,
                         public IVariadicNode {
public:
    using Base = TypedNode<ConfigType>;

    /**
     * @brief Template to add variadic accessors to any context type
     *
     * Extends any base context (TypedCompileContext, TypedExecuteContext)
     * to add InVariadic/OutVariadic methods.
     */
    template<typename BaseContext>
    struct VariadicContext : public BaseContext {
        VariadicTypedNode<ConfigType>* variadicNode;

        template<typename... Args>
        VariadicContext(VariadicTypedNode<ConfigType>* n, Args&&... args)
            : BaseContext(n, std::forward<Args>(args)...)
            , variadicNode(n) {}

        /**
         * @brief Get variadic input value (mirrors ctx.In() API)
         *
         * @tparam T Handle type (e.g., VkImageView, VkBuffer)
         * @param index Variadic input index (0-based)
         * @return Typed handle value, or null handle if invalid
         */
        template<typename T>
        T InVariadic(size_t index) const {
            return variadicNode->GetVariadicInput<T>(index, this->taskIndex);
        }

        /**
         * @brief Get variadic slot info (advanced usage)
         *
         * @param index Variadic input index (0-based)
         * @return Pointer to slot metadata, or nullptr if invalid
         */
        const VariadicSlotInfo* InVariadicSlot(size_t index) const {
            return variadicNode->GetVariadicSlotInfo(index, this->taskIndex);
        }

        /**
         * @brief Get variadic input count
         *
         * @return Number of variadic inputs
         */
        size_t InVariadicCount() const {
            return variadicNode->GetVariadicInputCount(this->taskIndex);
        }

        /**
         * @brief Get raw resource pointer for variadic input (for internal validation)
         *
         * @param index Variadic input index (0-based)
         * @return Resource pointer, or nullptr if invalid
         */
        Resource* InVariadicResource(size_t index) const {
            return variadicNode->GetVariadicInputResource(index, this->taskIndex);
        }

        /**
         * @brief Update variadic slot metadata (for validation/state updates)
         *
         * @param index Variadic input index (0-based)
         * @param slotInfo Updated slot information
         */
        void UpdateVariadicSlot(size_t index, const VariadicSlotInfo& slotInfo) {
            variadicNode->UpdateVariadicSlot(index, slotInfo, this->taskIndex);
        }
    };

    // Type aliases for variadic contexts - extend each typed context
    using VariadicSetupContext = typename Base::TypedSetupContext;  // No I/O in Setup
    using VariadicCompileContext = VariadicContext<typename Base::TypedCompileContext>;
    using VariadicExecuteContext = VariadicContext<typename Base::TypedExecuteContext>;
    using VariadicCleanupContext = typename Base::TypedCleanupContext;  // No I/O in Cleanup

    // Legacy alias for backwards compatibility
    using Context = VariadicExecuteContext;

    VariadicTypedNode(const std::string& instanceName, NodeType* nodeType)
        : Base(instanceName, nodeType) {}

    virtual ~VariadicTypedNode() = default;

    /**
     * @brief IGraphCompilable implementation - no-op by default
     *
     * This allows variadic slots to be discovered during graph compilation
     * (before deferred connections like ConnectVariadic are processed).
     *
     * Override this method in derived classes to perform graph-compile-time
     * setup such as discovering dynamic slots from shader metadata.
     *
     * NOTE: Do NOT call Setup() or SetupImpl() here - inputs may not be connected yet!
     * Use compile-time metadata (like PreRegisterVariadicSlots) for discovery.
     */
    void GraphCompileSetup() override {
        // Default: no-op
        // Derived classes should override to register variadic slots using
        // compile-time metadata (e.g., PreRegisterVariadicSlots)
    }

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
     * @brief Register a variadic slot with metadata (per-bundle)
     *
     * Called during Setup to define expected variadic slots based on runtime
     * discovery (e.g., shader reflection). Creates slot metadata in bundle 0.
     *
     * @param slotInfo Variadic slot metadata
     */
    void RegisterVariadicSlot(const VariadicSlotInfo& slotInfo, size_t bundleIndex = 0) {
        // Ensure variadic bundle exists
        if (bundleIndex >= variadicBundles_.size()) {
            variadicBundles_.resize(bundleIndex + 1);
        }

        variadicBundles_[bundleIndex].variadicSlots.push_back(slotInfo);
    }

    /**
     * @brief Update or create a variadic slot (optimistic, for ConnectVariadic)
     *
     * Creates a tentative slot if it doesn't exist, or updates existing slot.
     * Always succeeds - validation happens during Compile phase.
     *
     * @param slotIndex Index of the variadic slot
     * @param slotInfo Slot metadata (includes resource, state, source info)
     * @param bundleIndex Bundle index (default: 0)
     */
    void UpdateVariadicSlot(size_t slotIndex, const VariadicSlotInfo& slotInfo, size_t bundleIndex = 0) override {
        // Ensure variadic bundle exists
        if (bundleIndex >= variadicBundles_.size()) {
            variadicBundles_.resize(bundleIndex + 1);
        }

        auto& bundle = variadicBundles_[bundleIndex];

        // Extend slots vector if needed
        if (slotIndex >= bundle.variadicSlots.size()) {
            bundle.variadicSlots.resize(slotIndex + 1);
        }

        // Update/create slot (always succeeds)
        bundle.variadicSlots[slotIndex] = slotInfo;
    }

    /**
     * @brief Add a variadic input connection with metadata validation
     *
     * Called by the render graph when connecting additional inputs beyond
     * the statically-defined slots. Validates against registered slot metadata.
     *
     * @param variadicIndex Index of the variadic slot (corresponds to registered order)
     * @param resource Resource to connect
     * @param bundleIndex Bundle index (default: 0)
     * @return true if connection succeeded, false if validation failed
     */
    bool AddVariadicInput(size_t variadicIndex, Resource* resource, size_t bundleIndex = 0) {
        if (!resource) {
            std::cout << "[VariadicTypedNode::AddVariadicInput] ERROR: Null resource\n";
            return false;
        }

        // Ensure variadic bundle exists
        if (bundleIndex >= variadicBundles_.size()) {
            std::cout << "[VariadicTypedNode::AddVariadicInput] ERROR: Bundle index "
                      << bundleIndex << " out of range (max: " << variadicBundles_.size() << ")\n";
            return false;
        }

        auto& bundle = variadicBundles_[bundleIndex];

        // Validate variadic index
        if (variadicIndex >= bundle.variadicSlots.size()) {
            std::cout << "[VariadicTypedNode::AddVariadicInput] ERROR: Variadic index "
                      << variadicIndex << " out of range (max: " << bundle.variadicSlots.size() << ")\n";
            return false;
        }

        // Validate resource type against slot metadata
        auto& slotInfo = bundle.variadicSlots[variadicIndex];
        if (resource->GetType() != slotInfo.resourceType) {
            std::cout << "[VariadicTypedNode::AddVariadicInput] ERROR: Type mismatch for variadic slot "
                      << variadicIndex << " (expected: " << static_cast<int>(slotInfo.resourceType)
                      << ", got: " << static_cast<int>(resource->GetType()) << ")\n";
            return false;
        }

        // Assign resource to variadic slot
        slotInfo.resource = resource;
        return true;
    }

    /**
     * @brief LEGACY: Add variadic input without metadata (appends to variadic list)
     *
     * For backward compatibility. Prefer AddVariadicInput(index, resource) instead.
     *
     * @param resource Resource to connect
     */
    void AddVariadicInput(Resource* resource) {
        if (!resource) return;

        // Append to bundle 0's variadic slots
        if (variadicBundles_.empty()) {
            variadicBundles_.resize(1);
        }

        VariadicSlotInfo slotInfo;
        slotInfo.resource = resource;
        slotInfo.resourceType = resource->GetType();
        slotInfo.slotName = "variadic_" + std::to_string(variadicBundles_[0].variadicSlots.size());

        variadicBundles_[0].variadicSlots.push_back(slotInfo);
    }

    /**
     * @brief Get all variadic input resources from bundle
     *
     * @param bundleIndex Bundle index (default: 0)
     * @return Vector of resource pointers
     */
    std::vector<Resource*> GetVariadicInputs(size_t bundleIndex = 0) const {
        if (bundleIndex >= variadicBundles_.size()) {
            return {};
        }

        const auto& variadicSlots = variadicBundles_[bundleIndex].variadicSlots;
        std::vector<Resource*> resources;
        resources.reserve(variadicSlots.size());

        for (const auto& slot : variadicSlots) {
            resources.push_back(slot.resource);
        }

        return resources;
    }

    /**
     * @brief Get variadic input count from bundle
     *
     * @param bundleIndex Bundle index (default: 0)
     * @return Number of variadic inputs in bundle
     */
    size_t GetVariadicInputCount(size_t bundleIndex = 0) const {
        if (bundleIndex >= variadicBundles_.size()) {
            return 0;
        }
        return variadicBundles_[bundleIndex].variadicSlots.size();
    }

    /**
     * @brief Get variadic input resource at index
     *
     * @param index Variadic input index (0-based)
     * @param bundleIndex Bundle index (default: 0)
     * @return Resource pointer, or nullptr if invalid index
     */
    Resource* GetVariadicInputResource(size_t index, size_t bundleIndex = 0) const {
        if (bundleIndex >= variadicBundles_.size()) {
            return nullptr;
        }

        const auto& variadicSlots = variadicBundles_[bundleIndex].variadicSlots;
        if (index < variadicSlots.size()) {
            return variadicSlots[index].resource;
        }
        return nullptr;
    }

    /**
     * @brief Get variadic slot metadata at index
     *
     * @param index Variadic input index (0-based)
     * @param bundleIndex Bundle index (default: 0)
     * @return Pointer to slot metadata, or nullptr if invalid index
     */
    const VariadicSlotInfo* GetVariadicSlotInfo(size_t index, size_t bundleIndex = 0) const override {
        if (bundleIndex >= variadicBundles_.size()) {
            return nullptr;
        }

        const auto& variadicSlots = variadicBundles_[bundleIndex].variadicSlots;
        if (index < variadicSlots.size()) {
            return &variadicSlots[index];
        }
        return nullptr;
    }

    /**
     * @brief Get typed variadic input handle at index
     *
     * @tparam T Handle type (e.g., VkImageView, VkBuffer)
     * @param index Variadic input index (0-based)
     * @param bundleIndex Bundle index (default: 0)
     * @return Typed handle value, or null handle if invalid index/type
     */
    template<typename T>
    T GetVariadicInput(size_t index, size_t bundleIndex = 0) const {
        Resource* res = GetVariadicInputResource(index, bundleIndex);
        if (!res) return T{};
        return res->GetHandle<T>();
    }

    /**
     * @brief Get variadic input as ResourceHandleVariant
     *
     * Useful for generic processing without knowing the exact type.
     *
     * @param index Variadic input index (0-based)
     * @param bundleIndex Bundle index (default: 0)
     * @return ResourceHandleVariant containing the handle
     */
    ResourceVariant GetVariadicInputVariant(size_t index, size_t bundleIndex = 0) const {
        Resource* res = GetVariadicInputResource(index, bundleIndex);
        if (!res || !res->IsValid()) {
            return std::monostate{};
        }

        // Extract variant from resource
        return res->GetHandleVariant();
    }

    /**
     * @brief Clear all variadic inputs from bundle
     *
     * Typically called during cleanup or graph rebuild.
     *
     * @param bundleIndex Bundle index (default: 0)
     */
    void ClearVariadicInputs(size_t bundleIndex = 0) {
        if (bundleIndex < variadicBundles_.size()) {
            variadicBundles_[bundleIndex].variadicSlots.clear();
        }
    }

    /**
     * @brief Clear all variadic inputs from all bundles
     *
     * Typically called during full node reset.
     */
    void ClearAllVariadicInputs() {
        for (auto& bundle : variadicBundles_) {
            bundle.variadicSlots.clear();
        }
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
     * 3. Type validation against registered slot metadata
     *
     * @param ctx Compile context
     * @param bundleIndex Bundle index to validate (default: 0)
     * @return true if validation passed, false otherwise
     */
    virtual bool ValidateVariadicInputsImpl(Context& ctx, size_t bundleIndex = 0) {
        size_t count = GetVariadicInputCount(bundleIndex);

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

        // Validate each variadic slot
        if (bundleIndex >= variadicBundles_.size()) {
            return count == 0;  // Valid only if expecting 0 inputs
        }

        const auto& variadicSlots = variadicBundles_[bundleIndex].variadicSlots;
        for (size_t i = 0; i < variadicSlots.size(); ++i) {
            const auto& slotInfo = variadicSlots[i];

            // Skip validation for transient slots (ExecuteOnly) - they're populated in Execute phase
            if (HasExecute(slotInfo.slotRole)) {
                std::cout << "[VariadicTypedNode::ValidateVariadicInputsImpl] Skipping validation for transient slot "
                          << i << " (" << slotInfo.slotName << ") - will be populated in Execute phase\n";
                continue;
            }

            // Check for null resources (non-transient only)
            if (!slotInfo.resource) {
                std::cout << "[VariadicTypedNode::ValidateVariadicInputsImpl] ERROR: "
                          << "Variadic input " << i << " (" << slotInfo.slotName << ") is null\n";
                return false;
            }

            // Validate type
            if (slotInfo.resource->GetType() != slotInfo.resourceType) {
                std::cout << "[VariadicTypedNode::ValidateVariadicInputsImpl] ERROR: "
                          << "Variadic input " << i << " (" << slotInfo.slotName << ") type mismatch. "
                          << "Expected: " << static_cast<int>(slotInfo.resourceType)
                          << ", got: " << static_cast<int>(slotInfo.resource->GetType()) << "\n";
                return false;
            }
        }

        return true;
    }

    // ============================================================================
    // CONTEXT FACTORY OVERRIDES - Return variadic-extended contexts
    // ============================================================================

    /**
     * @brief Override to return base setup context (no I/O in Setup)
     */
    Vixen::RenderGraph::SetupContext CreateSetupContext(uint32_t taskIndex) override {
        return VariadicSetupContext(this, taskIndex);
    }

    /**
     * @brief Override to return variadic-extended context for Compile
     */
    Vixen::RenderGraph::CompileContext CreateCompileContext(uint32_t taskIndex) override {
        return VariadicCompileContext(this, taskIndex);
    }

    /**
     * @brief Override to return variadic-extended context for Execute
     */
    Vixen::RenderGraph::ExecuteContext CreateExecuteContext(uint32_t taskIndex) override {
        return VariadicExecuteContext(this, taskIndex);
    }

    /**
     * @brief Override to return base cleanup context (no I/O in Cleanup)
     */
    Vixen::RenderGraph::CleanupContext CreateCleanupContext(uint32_t taskIndex) override {
        return VariadicCleanupContext(this, taskIndex);
    }

    // ============================================================================
    // LIFECYCLE ORCHESTRATION - Override to create variadic contexts
    // ============================================================================
    // VariadicTypedNode overrides the no-parameter lifecycle methods to create variadic contexts
    // and call the variadic *Impl(VariadicContext&) methods. This avoids object slicing.

    void CompileImpl() override {
        uint32_t taskCount = this->DetermineTaskCount();
        for (uint32_t taskIndex = 0; taskIndex < taskCount; ++taskIndex) {
            VariadicCompileContext ctx(this, taskIndex);
            CompileImpl(ctx);
        }
    }

    void ExecuteImpl() override {
        uint32_t taskCount = this->DetermineTaskCount();
        for (uint32_t taskIndex = 0; taskIndex < taskCount; ++taskIndex) {
            VariadicExecuteContext ctx(this, taskIndex);
            ExecuteImpl(ctx);
        }
    }

    // ============================================================================
    // LIFECYCLE IMPLEMENTATIONS - Override in derived variadic nodes
    // ============================================================================

    /**
     * @brief SetupImpl - override in derived classes
     *
     * @param ctx Setup context (no I/O access, no variadic extensions needed)
     */
    virtual void SetupImpl(VariadicSetupContext& ctx) {}

    /**
     * @brief CompileImpl with variadic-extended Context - override in derived classes
     *
     * @param ctx Extended context with InVariadic/OutVariadic support
     */
    virtual void CompileImpl(VariadicCompileContext& ctx) {}

    /**
     * @brief ExecuteImpl with variadic-extended Context - override in derived classes
     *
     * @param ctx Extended context with InVariadic/OutVariadic support
     */
    virtual void ExecuteImpl(VariadicExecuteContext& ctx) = 0;

    /**
     * @brief CleanupImpl - override in derived classes
     *
     * @param ctx Cleanup context (no I/O access, no variadic extensions needed)
     */
    virtual void CleanupImpl(VariadicCleanupContext& ctx) {}

    // Variadic input count constraints
    size_t minVariadicInputs_ = 0;         // Minimum required (default: none)
    size_t maxVariadicInputs_ = SIZE_MAX;  // Maximum allowed (default: unlimited)

    // Variadic bundle storage (separate from NodeInstance bundles)
    std::vector<VariadicBundle> variadicBundles_;
};

} // namespace Vixen::RenderGraph
