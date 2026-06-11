#pragma once

#include "NodeInstance.h"
#include "Data/Core/ResourceConfig.h"
#include "Data/Core/CompileTimeResourceSystem.h"
#include "Data/Core/ConnectionConcepts.h"  // For Iterable concept (Sprint 6.0.2)
#include <future>
#include <vector>

namespace Vixen::RenderGraph {

/**
 * @brief Macro-based system to auto-generate storage from config
 *
 * The config now GENERATES the member variables - you don't declare them manually!
 *
 */

// ====================================================================
// STORAGE GENERATION MACROS
// ====================================================================

/**
 * @brief Generate input storage variables from config
 *
 * Creates member variables: input_0, input_1, etc.
 * Types are extracted from the config at compile time.
 *
 * Usage in node class:
 * ```cpp
 * class MyNode : public NodeInstance {
 *     GENERATE_INPUT_STORAGE(MyNodeConfig);
 *     // This generates:
 *     // typename MyNodeConfig::INPUT_0_Slot::Type input_0;
 *     // typename MyNodeConfig::INPUT_1_Slot::Type input_1;
 *     // etc.
 * };
 * ```
 */
#define GENERATE_INPUT_STORAGE_ITEM(ConfigType, Index) \
    typename ConfigType::INPUT_##Index##_Slot::Type input_##Index = VK_NULL_HANDLE

/**
 * @brief Generate output storage variables from config
 */
#define GENERATE_OUTPUT_STORAGE_ITEM(ConfigType, Index) \
    typename ConfigType::OUTPUT_##Index##_Slot::Type output_##Index = VK_NULL_HANDLE

/**
 * @brief Helper to access generated storage by slot
 *
 * Usage:
 * ```cpp
 * SLOT_STORAGE(SURFACE) = mySurface;  // Expands to: output_0 = mySurface;
 * ```
 */
#define INPUT_STORAGE(SlotName) input_##SlotName##_INDEX
#define OUTPUT_STORAGE(SlotName) output_##SlotName##_INDEX

// ====================================================================
// SIMPLIFIED APPROACH: Direct Member Access
// ====================================================================

/**
 * @brief Define input slot WITH storage generation
 *
 * This macro both:
 * 1. Defines the compile-time slot (type info)
 * 2. Generates the storage variable
 *
 * Usage in config:
 * ```cpp
 * CONSTEXPR_NODE_CONFIG(MyConfig, 1, 0) {
 *     INPUT_SLOT_WITH_STORAGE(ALBEDO, VkImage, 0, false);
 *     // This creates:
 *     // 1. Slot: using ALBEDO_Slot = ResourceSlot<VkImage, 0, false>;
 *     // 2. Constant: static constexpr ALBEDO_Slot ALBEDO{};
 *     // 3. Storage helper: Accessed via node.In<0>() or node.In(ALBEDO)
 * };
 * ```
 */
#define INPUT_SLOT_WITH_STORAGE(SlotName, SlotType, Index, Nullable) \
    CONSTEXPR_INPUT(SlotName, SlotType, Index, Nullable); \
    static constexpr size_t SlotName##_INDEX = Index; \
    static constexpr bool SlotName##_NULLABLE = Nullable

#define OUTPUT_SLOT_WITH_STORAGE(SlotName, SlotType, Index, Nullable) \
    CONSTEXPR_OUTPUT(SlotName, SlotType, Index, Nullable); \
    static constexpr size_t SlotName##_INDEX = Index; \
    static constexpr bool SlotName##_NULLABLE = Nullable

/**
 * @brief Typed node base with compile-time storage arrays
 *
 * Storage is a std::array indexed by the config's slot indices.
 * Type safety is enforced at compile time.
 *
 * **Phase F: Context System**
 * Nodes now receive a Context in ExecuteImpl() instead of a raw index.
 * The context provides In()/Out() accessors bound to a specific task index,
 * enabling clean parallelization without manual index management.
 *
 * Example:
 * ```cpp
 * class WindowNode : public TypedNode<WindowNodeConfig> {
 *     using Ctx = typename TypedNode<WindowNodeConfig>::Context;
 *
 *     void CompileImpl() override {
 *         CreateSurface();
 *         Out(WindowNodeConfig::SURFACE) = surface;
 *     }
 *
 *     void ExecuteImpl(Ctx& ctx) override {
 *         auto device = ctx.In(WindowNodeConfig::DEVICE);
 *         auto surface = ctx.In(WindowNodeConfig::SURFACE);
 *         // ctx.In/Out are bound to this task's index automatically
 *     }
 * };
 * ```
 */
template<typename ConfigType>
class TypedNode : public NodeInstance {
public:
    /**
     * @brief Phase F: Context - Bound slot accessors for node lifecycle methods
     *
     * Provides In()/Out() accessors bound to a specific array index (taskIndex).
     * Used in Setup, Compile, and Execute to provide clean slot access API.
     *
     * For Setup/Compile: taskIndex is always 0 (non-task-based operations)
     * For Execute: taskIndex corresponds to the current task being processed
     *
     * This enables:
     * - Clean API: no manual index passing
     * - Parallelization: each task has independent context
     * - Type safety: leverages ConfigType's slot definitions
     *
     * Usage in ExecuteImpl:
     * ```cpp
     * void ExecuteImpl(Context& ctx) override {
     *     auto input = ctx.In(MyConfig::INPUT_SLOT);
     *     ctx.Out(MyConfig::OUTPUT_SLOT, result);
     * }
     * ```
     */

    // ============================================================================
    // TYPED CONTEXT SPECIALIZATIONS
    // ============================================================================

    /**
     * @brief Base mixin for typed In/Out access
     *
     * Provides compile-time type-safe slot access for contexts that support I/O.
     */
    template<typename ContextBase>
    struct TypedIOContext : public ContextBase {
        TypedNode<ConfigType>* typedNode;

        template<typename... Args>
        TypedIOContext(TypedNode<ConfigType>* n, Args&&... args)
            : ContextBase(n, std::forward<Args>(args)...)
            , typedNode(n) {}

        /**
         * @brief Get input value bound to this task's index
         *
         * Phase H: Broadcast semantics - if producer outputs once during Compile (taskIndex=0)
         * but consumer reads during Execute with varying taskIndex, fall back to taskIndex=0.
         *
         * Sprint 6.0.1: Accumulation slot support
         * - For regular slots: returns SlotType::Type
         * - For accumulation slots: returns std::vector<SlotType::Type>
         */
        template<typename SlotType>
        auto In(SlotType slot) const {
            static_assert(SlotType::index < ConfigType::INPUT_COUNT, "Input index out of bounds");

            // Try current taskIndex first
            Resource* res = typedNode->NodeInstance::GetInput(SlotType::index, this->taskIndex);

            // Phase H: Broadcast fallback - if not found, try taskIndex=0
            // This handles nodes that output once during Compile but are read during Execute
            if (!res && this->taskIndex > 0) {
                res = typedNode->NodeInstance::GetInput(SlotType::index, 0);
            }

            // Sprint 6.0.1-6.0.2: Handle accumulation slots
            // V1 (old): SlotType::Type is element type (bool), wrap in vector
            // V2 (new): SlotType::Type is container type (std::vector<bool>), no wrapping
            if constexpr (SlotType::isAccumulation) {
                using SlotTypeRaw = typename SlotType::Type;

                // Sprint 6.0.2: Check if type is already a container (V2 macro)
                // If iterable, it's already a container - no wrapping needed
                if constexpr (Iterable<SlotTypeRaw>) {
                    // V2: Type is already container (std::vector<T>)
                    if (!res) return SlotTypeRaw{};
                    return res->GetHandle<SlotTypeRaw>();
                } else {
                    // V1: Type is element, wrap in vector (backward compat)
                    using VectorType = std::vector<SlotTypeRaw>;
                    if (!res) return VectorType{};
                    return res->GetHandle<VectorType>();
                }
            } else {
                // Regular slot: return T directly
                if (!res) return typename SlotType::Type{};
                return res->GetHandle<typename SlotType::Type>();
            }
        }

        /**
         * @brief Set output value bound to this task's index
         *
         * Uses perfect forwarding to preserve the original argument:
         * - For reference slots (const T&): Captures address of the ORIGINAL object (not parameter)
         * - For value slots (T): Forwards as rvalue for move semantics
         */
        template<typename SlotType, typename U>
        void Out(SlotType slot, U&& value) {
            static_assert(SlotType::index < ConfigType::OUTPUT_COUNT, "Output index out of bounds");
            typedNode->EnsureOutputSlot(SlotType::index, this->taskIndex);
            Resource* res = typedNode->NodeInstance::GetOutput(SlotType::index, this->taskIndex);
            // For reference types: pass value directly (std::addressof in SetHandle gets correct address)
            // For value types: cast to SlotType::Type to handle const/non-const mismatches
            using SlotT = typename SlotType::Type;
            if constexpr (std::is_lvalue_reference_v<SlotT>) {
                // Reference type: pass value directly - SetHandle will use std::addressof
                res->SetHandle<SlotT>(value);
            } else {
                // Value type: use static_cast to handle const conversion
                res->SetHandle<SlotT>(static_cast<SlotT>(std::forward<U>(value)));
            }
        }

        /**
         * @brief Set output value and attach an interface to the resource
         *
         * @deprecated Use ctx.Out() with wrapper types that have conversion_type instead.
         * Wrapper types (e.g., ShaderCountersBuffer, RayTraceBuffer) now declare their
         * conversion target via `using conversion_type = VkBuffer;` and the type system
         * handles descriptor extraction automatically. No separate interface attachment needed.
         *
         * Example (old):
         *   ctx.OutWithInterface(Config::DEBUG_BUFFER, buffer.GetVkBuffer(), &debugCapture);
         * Example (new):
         *   ctx.Out(Config::DEBUG_BUFFER, bufferPtr);  // Wrapper type handles extraction
         */
        template<typename SlotType, typename U, typename InterfaceType>
        [[deprecated("Use ctx.Out() with wrapper types that have conversion_type instead")]]
        void OutWithInterface(SlotType slot, U&& value, InterfaceType* iface) {
            static_assert(SlotType::index < ConfigType::OUTPUT_COUNT, "Output index out of bounds");
            typedNode->EnsureOutputSlot(SlotType::index, this->taskIndex);
            Resource* res = typedNode->NodeInstance::GetOutput(SlotType::index, this->taskIndex);

            using SlotT = typename SlotType::Type;
            if constexpr (std::is_lvalue_reference_v<SlotT>) {
                res->SetHandle<SlotT>(value);
            } else {
                res->SetHandle<SlotT>(static_cast<SlotT>(std::forward<U>(value)));
            }

            // Attach interface to the resource
            if (iface) {
                res->SetInterface<InterfaceType>(iface);
            }
        }

        /**
         * @brief Get input descriptor bound to this task's index
         */
        template<typename SlotType>
        const auto* InDesc(SlotType slot) const {
            using HandleType = typename SlotType::Type;
            using DescriptorType = typename ResourceTypeTraits<HandleType>::DescriptorT;
            Resource* res = typedNode->NodeInstance::GetInput(SlotType::index, this->taskIndex);
            if (!res) return static_cast<const DescriptorType*>(nullptr);
            return res->GetDescriptor<DescriptorType>();
        }

        /**
         * @brief Get mutable output descriptor bound to this task's index
         */
        template<typename SlotType>
        auto* OutDescMut(SlotType slot) {
            using HandleType = typename SlotType::Type;
            using DescriptorType = typename ResourceTypeTraits<HandleType>::DescriptorT;
            typedNode->EnsureOutputSlot(SlotType::index, this->taskIndex);
            Resource* res = typedNode->NodeInstance::GetOutput(SlotType::index, this->taskIndex);
            if (!res) return static_cast<DescriptorType*>(nullptr);
            return res->GetDescriptorMutable<DescriptorType>();
        }

        /**
         * @brief Get const output descriptor bound to this task's index
         */
        template<typename SlotType>
        const auto* OutDesc(SlotType slot) const {
            using HandleType = typename SlotType::Type;
            using DescriptorType = typename ResourceTypeTraits<HandleType>::DescriptorT;
            Resource* res = typedNode->NodeInstance::GetOutput(SlotType::index, this->taskIndex);
            if (!res) return static_cast<const DescriptorType*>(nullptr);
            return res->GetDescriptor<DescriptorType>();
        }
    };

    // Specialized context types for each lifecycle phase
    using TypedSetupContext = SetupContext;  // Setup has no I/O
    using TypedCompileContext = TypedIOContext<CompileContext>;
    using TypedExecuteContext = TypedIOContext<ExecuteContext>;
    using TypedCleanupContext = CleanupContext;  // Cleanup has no I/O

    // Legacy alias for backwards compatibility during migration
    using Context = TypedExecuteContext;

    TypedNode(
        const std::string& instanceName,
        NodeType* nodeType
    ) : NodeInstance(instanceName, nodeType) {
        // Initialize arrays
        inputs.fill(VK_NULL_HANDLE);
        outputs.fill(VK_NULL_HANDLE);

        // Phase F Note: Input/output schemas are registered by NodeType constructor
        // using ConfigType::GetInputVector() and GetOutputVector()
        // No need to duplicate registration here
    }

    virtual ~TypedNode() = default;

protected:
    // ============================================================================
    // CONTEXT FACTORY OVERRIDES - Return typed contexts
    // ============================================================================

    SetupContext CreateSetupContext(uint32_t taskIndex) override {
        return TypedSetupContext(this, taskIndex);
    }

    CompileContext CreateCompileContext(uint32_t taskIndex) override {
        return TypedCompileContext(this, taskIndex);
    }

    ExecuteContext CreateExecuteContext(uint32_t taskIndex) override {
        return TypedExecuteContext(this, taskIndex);
    }

    CleanupContext CreateCleanupContext(uint32_t taskIndex) override {
        return TypedCleanupContext(this, taskIndex);
    }

    // ============================================================================
    // SLOT VALIDATION - TypedNode layer
    // ============================================================================

    /**
     * @brief Validate input slot (TypedNode override)
     *
     * TypedNode can add config-specific validation here if needed.
     * For now, just call base implementation.
     */
    bool ValidateInputSlot(uint32_t slotIndex, std::string& errorMessage) const override {
        // TypedNode: Could add config-specific validation here
        // For now, delegate to base
        return NodeInstance::ValidateInputSlot(slotIndex, errorMessage);
    }

    /**
     * @brief Validate output slot (TypedNode override)
     *
     * TypedNode can add config-specific validation here if needed.
     * For now, just call base implementation.
     */
    bool ValidateOutputSlot(uint32_t slotIndex, std::string& errorMessage) const override {
        // TypedNode: Could add config-specific validation here
        // For now, delegate to base
        return NodeInstance::ValidateOutputSlot(slotIndex, errorMessage);
    }

public:
    // ===== PHASE F: CONTEXT SYSTEM =====

    // ============================================================================
    // LIFECYCLE ORCHESTRATION - Override to create typed contexts
    // ============================================================================
    // TypedNode overrides the no-parameter lifecycle methods to create typed contexts
    // and call the typed *Impl(TypedContext&) methods. This avoids object slicing
    // that would occur if we relied on virtual CreateContext() methods returning by value.

    void CompileImpl() override {
        uint32_t taskCount = DetermineTaskCount();
        for (uint32_t taskIndex = 0; taskIndex < taskCount; ++taskIndex) {
            TypedCompileContext ctx(this, taskIndex);
            CompileImpl(ctx);
        }
    }

    void ExecuteImpl() override {
        // Sequential execution over all tasks (used when NOT using virtual task executor)
        uint32_t taskCount = DetermineTaskCount();
        for (uint32_t taskIndex = 0; taskIndex < taskCount; ++taskIndex) {
            TypedExecuteContext ctx(this, taskIndex);
            ExecuteImpl(ctx);
        }
    }

    // =========================================================================
    // Sprint 6.5: Task Parallelism API (FINAL - not overridable)
    // =========================================================================
    //
    // Returns N tasks for Execute phase (1 per bundle).
    // Executor runs these tasks - parallelism is automatic based on dependencies.
    // Single-bundle nodes naturally get 1 task.
    // =========================================================================

    std::vector<VirtualTask> GetExecutionTasks(VirtualTaskPhase phase) override {
        // For Execute phase: return N tasks (1 per bundle)
        if (phase == VirtualTaskPhase::Execute) {
            return CreateParallelTasks(phase, [this](uint32_t i) {
                TypedExecuteContext ctx(this, i);
                ExecuteImpl(ctx);
            });
        }

        // For other phases: 1 task that runs the whole phase
        return NodeInstance::GetExecutionTasks(phase);
    }

protected:
    /**
     * @brief SetupImpl with TypedSetupContext - override this in derived classes
     *
     * Called during Setup phase. No I/O access in Setup.
     *
     * @param ctx Setup context (no I/O access)
     */
    virtual void SetupImpl(TypedSetupContext& ctx) {}

    /**
     * @brief CompileImpl with TypedCompileContext - override this in derived classes
     *
     * Called during Compile phase. Context provides typed In()/Out() access.
     *
     * @param ctx Compile context with typed slot accessors
     */
    virtual void CompileImpl(TypedCompileContext& ctx) {}

    /**
     * @brief ExecuteImpl with TypedExecuteContext - override this in derived classes
     *
     * **Phase F: Context-based execution.**
     *
     * Derived classes override this method for task execution.
     * The Context provides typed In()/Out() accessors for clean slot access.
     *
     * Example:
     * ```cpp
     * void MyNode::ExecuteImpl(TypedExecuteContext& ctx) override {
     *     auto device = ctx.In(MyConfig::DEVICE);
     *     auto input = ctx.In(MyConfig::INPUT_DATA);
     *     auto result = Process(device, input);
     *     ctx.Out(MyConfig::OUTPUT_DATA, result);
     * }
     * ```
     *
     * @param ctx Execute context with typed slot accessors
     */
    virtual void ExecuteImpl(TypedExecuteContext& ctx) {
        // Default: no-op (VariadicTypedNode and concrete nodes provide override)
    }

    /**
     * @brief CleanupImpl with TypedCleanupContext - override this in derived classes
     *
     * Called during Cleanup phase. No I/O access during cleanup.
     *
     * @param ctx Cleanup context (no I/O access)
     */
    virtual void CleanupImpl(TypedCleanupContext& ctx) {}

    // Task orchestration is handled at NodeInstance level
    // TypedNode only provides *Impl(Context&) virtual methods for downstream nodes

private:
    // ===== INDEX-BASED ACCESS =====

    /**
     * @brief Access input by compile-time index
     *
     * Usage: VkImage img = In<0>();
     */
    template<size_t Index>
    auto& In() {
        static_assert(Index < ConfigType::INPUT_COUNT, "Input index out of bounds");
        return inputs[Index];
    }

    template<size_t Index>
    const auto& In() const {
        static_assert(Index < ConfigType::INPUT_COUNT, "Input index out of bounds");
        return inputs[Index];
    }

    /**
     * @brief Access output by compile-time index
     *
     * Usage: VkSurfaceKHR& surf = Out<0>();
     */
    template<size_t Index>
    auto& Out() {
        static_assert(Index < ConfigType::OUTPUT_COUNT, "Output index out of bounds");
        return outputs[Index];
    }

    template<size_t Index>
    const auto& Out() const {
        static_assert(Index < ConfigType::OUTPUT_COUNT, "Output index out of bounds");
        return outputs[Index];
    }

    // ===== SLOT-BASED ACCESS =====

    // ===== ZERO-BOILERPLATE SLOT ACCESS =====

    /**
     * @brief Get input value by slot (automatic type deduction from slot!)
     *
     * The slot definition tells us the exact type, so no template parameters needed!
     * Automatically extracts the correct type from the resource variant.
     *
     * Usage:
     *   VkImage img = In(MyConfig::ALBEDO_INPUT);  // Type auto-deduced!
     *   VkBuffer buf = In(MyConfig::VERTEX_BUFFER);
     *
     * For array slots:
     *   VkImage img = In(MyConfig::TEXTURES, 2);  // Get index 2
     *
     * DEPRECATED: Use context-based In() from TypedIOContext instead.
     * This method remains for backward compatibility but should not be used.
     */
    template<typename SlotType>
    typename SlotType::Type In(SlotType slot) const {
        static_assert(SlotType::index < ConfigType::INPUT_COUNT, "Input index out of bounds");
        // DEPRECATED: Use context-based In() from TypedIOContext instead
        // This method remains for backward compatibility but should not be used
        uint32_t arrayIndex = 0; // Fallback to first task
        Resource* res = NodeInstance::GetInput(SlotType::index, arrayIndex);

        // Phase F: Use slot's metadata for dependency tracking (not parameter)
        // Mark used-in-compile if slot has Dependency role
        if ((static_cast<uint8_t>(SlotType::role) & static_cast<uint8_t>(SlotRole::Dependency)) != 0) {
            NodeInstance::MarkInputUsedInCompile(SlotType::index, arrayIndex);
        }
        if (!res) return typename SlotType::Type{};  // Return null handle

        // Automatic type extraction from variant using slot's type info!
        return res->GetHandle<typename SlotType::Type>();
    }

    /**
     * @brief Set output value by slot (automatic type validation from slot!)
     * 
     * The slot definition enforces the type at compile-time!
     * Automatically stores the value in the resource variant.
     * 
     * Usage:
     *   Out(MyConfig::COLOR_IMAGE, myImage);  // Type checked against slot!
     *   Out(MyConfig::DEPTH_IMAGE, 0, myDepthImage);  // Array index 0
     * 
     * Compiler error if type mismatch:
     *   Out(MyConfig::COLOR_IMAGE, myBuffer);  // ERROR: VkBuffer != VkImage
     */
    template<typename SlotType>
    void Out(SlotType slot, typename SlotType::Type value, size_t arrayIndex) {
        static_assert(SlotType::index < ConfigType::OUTPUT_COUNT, "Output index out of bounds");
        
        // Ensure resource exists at this array index
        EnsureOutputSlot(SlotType::index, arrayIndex);
        Resource* res = NodeInstance::GetOutput(SlotType::index, static_cast<uint32_t>(arrayIndex));
        
        // Automatic type storage in variant using slot's type info!
        res->SetHandle<typename SlotType::Type>(value);
    }

    /**
     * @brief Get output value by slot (for reading back outputs)
     * 
     * Usage:
     *   VkImage img = GetOut(MyConfig::COLOR_IMAGE);  // Read output value
     */
    template<typename SlotType>
    typename SlotType::Type GetOut(SlotType slot, size_t arrayIndex = 0) const {
        static_assert(SlotType::index < ConfigType::OUTPUT_COUNT, "Output index out of bounds");
        Resource* res = NodeInstance::GetOutput(SlotType::index, static_cast<uint32_t>(arrayIndex));
        if (!res) return typename SlotType::Type{};
        
        return res->GetHandle<typename SlotType::Type>();
    }

    // ===== ARRAY-AWARE ACCESS =====

    /**
     * @brief Get count of resources in a slot (1 for scalar, N for array)
     *
     * Usage: size_t count = GetInputCount(DeviceConfig::DEVICE);
     */
    template<typename SlotType>
    size_t GetInputCount(SlotType /*slot*/) const {
        static_assert(SlotType::index < ConfigType::INPUT_COUNT, "Input index out of bounds");
        return NodeInstance::GetInputCount(SlotType::index);
    }

    template<typename SlotType>
    size_t GetOutputCount(SlotType /*slot*/) const {
        static_assert(SlotType::index < ConfigType::OUTPUT_COUNT, "Output index out of bounds");
        return NodeInstance::GetOutputCount(SlotType::index);
    }

    /**
     * @brief Get input resource at specific array index
     * 
     * AUTOMATIC TYPE DEDUCTION from slot definition!
     * The slot's SlotType::Type tells us exactly what to extract from the variant.
     * 
     * Usage:
     * for (size_t i = 0; i < GetInputCount(Config::DEVICE); i++) {
     *     auto dev = GetInput(Config::DEVICE, i);  // Type auto-deduced from slot!
     * }
     */
    template<typename SlotType>
    typename SlotType::Type GetInput(SlotType /*slot*/, size_t arrayIndex = 0) const {
        static_assert(SlotType::index < ConfigType::INPUT_COUNT, "Input index out of bounds");
        Resource* res = NodeInstance::GetInput(SlotType::index, static_cast<uint32_t>(arrayIndex));
        if (!res) return typename SlotType::Type{};  // Return null handle
        
        // Extract typed handle from resource variant
        // SlotType::Type automatically provides the correct type!
        return res->GetHandle<typename SlotType::Type>();
    }

    /**
     * @brief Set output resource at specific array index
     * 
     * AUTOMATIC TYPE VALIDATION from slot definition!
     * Compiler enforces that value matches SlotType::Type.
     * 
     * Usage: SetOutput(Config::COLOR_IMAGE, 0, myImage);
     */
    template<typename SlotType, typename U>
    void SetOutput(SlotType /*slot*/, size_t arrayIndex, U&& value) {
        static_assert(SlotType::index < ConfigType::OUTPUT_COUNT, "Output index out of bounds");
        // Create resource if needed
        EnsureOutputSlot(SlotType::index, arrayIndex);
        Resource* res = NodeInstance::GetOutput(SlotType::index, static_cast<uint32_t>(arrayIndex));

        // Store typed handle in resource variant
        using SlotT = typename SlotType::Type;
        if constexpr (std::is_lvalue_reference_v<SlotT>) {
            res->SetHandle<SlotT>(value);
        } else {
            res->SetHandle<SlotT>(static_cast<SlotT>(std::forward<U>(value)));
        }
    }

    /**
     * @brief LEGACY: Get input with explicit type (for backward compatibility)
     * 
     * Prefer using GetInput(slot) without template parameter - it auto-deduces!
     * This version exists for cases where you need to override the slot type.
     */
    template<typename T, typename SlotType>
    T GetInputExplicit(SlotType /*slot*/, size_t arrayIndex = 0) const {
        static_assert(SlotType::index < ConfigType::INPUT_COUNT, "Input index out of bounds");
        Resource* res = NodeInstance::GetInput(SlotType::index, arrayIndex);
        if (!res) return T{};
        return res->GetHandle<T>();
    }

    // ===== DESCRIPTOR ACCESS (Zero Boilerplate) =====

    /**
     * @brief Get input descriptor by slot (automatic type deduction!)
     * 
     * The descriptor type is automatically deduced from the slot's handle type
     * using ResourceTypeTraits<SlotType::Type>::DescriptorT
     * 
     * Usage:
     *   auto* desc = InDesc(MyConfig::IMAGE_INPUT);
     *   // desc is automatically ImageDescriptor* (deduced from VkImage)
     *   if (desc) { use desc->width, desc->height, etc. }
     */
    template<typename SlotType>
    const auto* InDesc(SlotType slot) const {
        using HandleType = typename SlotType::Type;
        using DescriptorType = typename ResourceTypeTraits<HandleType>::DescriptorT;
        // DEPRECATED: Use context-based InDesc() from TypedIOContext instead
        uint32_t arrayIndex = 0; // Fallback to first task
        Resource* res = NodeInstance::GetInput(SlotType::index, arrayIndex);

        // Phase F: Use slot's metadata for dependency tracking (not parameter)
        if ((static_cast<uint8_t>(SlotType::role) & static_cast<uint8_t>(SlotRole::Dependency)) != 0) {
            NodeInstance::MarkInputUsedInCompile(SlotType::index, arrayIndex);
        }
        if (!res) return static_cast<const DescriptorType*>(nullptr);

        return res->GetDescriptor<DescriptorType>();
    }

    // Overload of Out that uses the node's active bundle index so callers inside
    // node logic don't need to pass the array index explicitly for the common
    // single-bundle case.
    // DEPRECATED: Use context-based Out() from TypedIOContext instead
    template<typename SlotType, typename U>
    void Out(SlotType slot, U&& value) {
        static_assert(SlotType::index < ConfigType::OUTPUT_COUNT, "Output index out of bounds");
        // Fallback to first task
        size_t arrayIndex = 0;
        EnsureOutputSlot(SlotType::index, arrayIndex);
        Resource* res = NodeInstance::GetOutput(SlotType::index, static_cast<uint32_t>(arrayIndex));
        // Handle reference vs value types differently
        using SlotT = typename SlotType::Type;
        if constexpr (std::is_lvalue_reference_v<SlotT>) {
            res->SetHandle<SlotT>(value);
        } else {
            res->SetHandle<SlotT>(static_cast<SlotT>(std::forward<U>(value)));
        }
    }

    /**
     * @brief Get output descriptor by slot (automatic type deduction!)
     * 
     * Usage:
     *   auto* desc = OutDesc(MyConfig::COLOR_IMAGE);
     *   // desc is automatically ImageDescriptor* (deduced from VkImage)
     */
    template<typename SlotType>
    const auto* OutDesc(SlotType slot, size_t arrayIndex = 0) const {
        using HandleType = typename SlotType::Type;
        using DescriptorType = typename ResourceTypeTraits<HandleType>::DescriptorT;
        
        Resource* res = NodeInstance::GetOutput(SlotType::index, arrayIndex);
        if (!res) return static_cast<const DescriptorType*>(nullptr);
        
        return res->GetDescriptor<DescriptorType>();
    }

    /**
     * @brief Get output descriptor mutable (for modifying descriptor data)
     * 
     * Usage:
     *   auto* desc = OutDescMut(MyConfig::COLOR_IMAGE);
     *   desc->width = 1920;
     *   desc->height = 1080;
     */
    template<typename SlotType>
    auto* OutDescMut(SlotType slot, size_t arrayIndex = 0) {
        using HandleType = typename SlotType::Type;
        using DescriptorType = typename ResourceTypeTraits<HandleType>::DescriptorT;
        
        Resource* res = NodeInstance::GetOutput(SlotType::index, arrayIndex);
        if (!res) return static_cast<DescriptorType*>(nullptr);
        
        return res->GetDescriptorMutable<DescriptorType>();
    }

private:
    /**
     * @brief Ensure output slot has space for arrayIndex (Phase F: bundle-first)
     */
    void EnsureOutputSlot(uint32_t slotIndex, size_t arrayIndex) {
        // Phase F: Ensure bundle exists for this array index
        if (arrayIndex >= NodeInstance::bundles.size()) {
            NodeInstance::bundles.resize(arrayIndex + 1);
        }

        // Ensure the outputs vector in this bundle has room for this slot
        auto& outputs = NodeInstance::bundles[arrayIndex].outputs;
        if (slotIndex >= outputs.size()) {
            outputs.resize(slotIndex + 1, nullptr);
        }

        // If the resource pointer is null, create a temporary Resource for this output
        // This happens when a node wants to write to an output that wasn't connected
        // TODO: Ideally the graph should manage all Resource<HandleType> instances, but for now we create locally
        if (outputs[slotIndex] == nullptr) {
            // Create Resource using the output schema descriptor from NodeType
            const ResourceDescriptor* schemaDesc = nodeType->GetOutputDescriptor(slotIndex);
            if (schemaDesc) {
                // Clone the descriptor and use it to initialize the Resource
                std::unique_ptr<ResourceDescriptorBase> descClone;
                // Visit the descriptor variant to clone it
                std::visit([&descClone](const auto& desc) {
                    using DescType = std::decay_t<decltype(desc)>;
                    if constexpr (!std::is_same_v<DescType, std::monostate>) {
                        descClone = std::make_unique<DescType>(desc);
                    }
                }, schemaDesc->descriptor);

                // Create resource from type + descriptor
                if (descClone) {
                    // ResourceV3 migration: create an empty Resource and rely on
                    // the new ResourceV3 system to initialize descriptors later.
                    // The previous code used Resource::CreateFromType(...) which
                    // is part of the legacy runtime dispatch. During migration we
                    // allocate an empty Resource here. The descriptor from
                    // schemaDesc is preserved in the NodeType schema and the
                    // graph's resource manager will be responsible for proper
                    // initialization when needed.
                    outputs[slotIndex] = new Resource();
                } else {
                    outputs[slotIndex] = new Resource();
                }
            } else {
                // Fallback: Create empty resource
                outputs[slotIndex] = new Resource();
            }
        }
    }

protected:
    // Storage arrays - types are void* / VkHandle-based for now
    // In a full implementation, use std::tuple<Slot0::Type, Slot1::Type, ...>
    std::array<void*, ConfigType::INPUT_COUNT> inputs{};
    std::array<void*, ConfigType::OUTPUT_COUNT> outputs{};
};

/**
 * @brief TypedNodeType - Automatic schema population from config
 *
 * Eliminates boilerplate from NodeType constructors by automatically
 * populating inputSchema and outputSchema from ConfigType.
 *
 * Usage:
 * ```cpp
 * // OLD: Manual schema population
 * class BoolOpNodeType : public NodeType {
 *     BoolOpNodeType() : NodeType("BoolOp") {
 *         BoolOpNodeConfig config;
 *         inputSchema = config.GetInputVector();
 *         outputSchema = config.GetOutputVector();
 *     }
 * };
 *
 * // NEW: Automatic schema population
 * class BoolOpNodeType : public TypedNodeType<BoolOpNodeConfig> {
 *     BoolOpNodeType() : TypedNodeType<BoolOpNodeConfig>("BoolOp") {}
 * };
 * ```
 */
template<typename ConfigType>
class TypedNodeType : public NodeType {
public:
    TypedNodeType(const std::string& typeName)
        : NodeType(typeName) {
        // Automatically populate schemas from config
        ConfigType config;
        inputSchema = config.GetInputVector();
        outputSchema = config.GetOutputVector();
    }
};

} // namespace Vixen::RenderGraph
