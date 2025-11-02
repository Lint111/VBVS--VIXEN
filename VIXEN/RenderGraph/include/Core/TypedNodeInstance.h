#pragma once

#include "NodeInstance.h"
#include "ResourceConfig.h"
#include "ResourceVariant.h"

namespace Vixen::RenderGraph {

/**
 * @brief Macro-based system to auto-generate storage from config
 *
 * The config now GENERATES the member variables - you don't declare them manually!
 *
 * OLD APPROACH (error-prone):
 * ```cpp
 * CONSTEXPR_OUTPUT(SURFACE, VkSurfaceKHR, 0, false);  // Config says VkSurfaceKHR
 * // But you manually declare:
 * VkImage surface;  // Oops! Wrong type! Runtime error!
 * ```
 *
 * NEW APPROACH (foolproof):
 * ```cpp
 * CONSTEXPR_OUTPUT(SURFACE, VkSurfaceKHR, 0, false);  // Config says VkSurfaceKHR
 * GENERATE_OUTPUT_STORAGE;  // Automatically creates: VkSurfaceKHR output_0;
 * ```
 *
 * The storage is DERIVED from config - impossible to mismatch!
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
    struct Context {
        TypedNode<ConfigType>* node;
        uint32_t taskIndex;

        Context(TypedNode<ConfigType>* n, uint32_t idx)
            : node(n), taskIndex(idx) {}

        /**
         * @brief Get input value bound to this task's index
         *
         * Automatically uses taskIndex for array slot access.
         * Type is deduced from SlotType::Type.
         *
         * @param slot Slot definition from config
         * @return Typed handle value
         */
        template<typename SlotType>
        typename SlotType::Type In(SlotType slot) const {
            static_assert(SlotType::index < ConfigType::INPUT_COUNT, "Input index out of bounds");
            Resource* res = node->NodeInstance::GetInput(SlotType::index, taskIndex);
            if (!res) return typename SlotType::Type{};
            return res->GetHandle<typename SlotType::Type>();
        }

        /**
         * @brief Set output value bound to this task's index
         *
         * Automatically uses taskIndex for array slot access.
         * Type is validated against SlotType::Type at compile time.
         *
         * @param slot Slot definition from config
         * @param value Typed handle value
         */
        template<typename SlotType>
        void Out(SlotType slot, typename SlotType::Type value) {
            static_assert(SlotType::index < ConfigType::OUTPUT_COUNT, "Output index out of bounds");
            node->EnsureOutputSlot(SlotType::index, taskIndex);
            Resource* res = node->NodeInstance::GetOutput(SlotType::index, taskIndex);
            res->SetHandle<typename SlotType::Type>(value);
        }

        /**
         * @brief Get input descriptor bound to this task's index
         *
         * @param slot Slot definition from config
         * @return Descriptor pointer (auto-deduced type)
         */
        template<typename SlotType>
        const auto* InDesc(SlotType slot) const {
            using HandleType = typename SlotType::Type;
            using DescriptorType = typename ResourceTypeTraits<HandleType>::DescriptorT;
            Resource* res = node->NodeInstance::GetInput(SlotType::index, taskIndex);
            if (!res) return static_cast<const DescriptorType*>(nullptr);
            return res->GetDescriptor<DescriptorType>();
        }

        /**
         * @brief Get mutable output descriptor bound to this task's index
         *
         * @param slot Slot definition from config
         * @return Mutable descriptor pointer (auto-deduced type)
         */
        template<typename SlotType>
        auto* OutDescMut(SlotType slot) {
            using HandleType = typename SlotType::Type;
            using DescriptorType = typename ResourceTypeTraits<HandleType>::DescriptorT;
            node->EnsureOutputSlot(SlotType::index, taskIndex);
            Resource* res = node->NodeInstance::GetOutput(SlotType::index, taskIndex);
            if (!res) return static_cast<DescriptorType*>(nullptr);
            return res->GetDescriptorMutable<DescriptorType>();
        }

        /**
         * @brief Get const output descriptor bound to this task's index
         *
         * @param slot Slot definition from config
         * @return Const descriptor pointer (auto-deduced type)
         */
        template<typename SlotType>
        const auto* OutDesc(SlotType slot) const {
            using HandleType = typename SlotType::Type;
            using DescriptorType = typename ResourceTypeTraits<HandleType>::DescriptorT;
            Resource* res = node->NodeInstance::GetOutput(SlotType::index, taskIndex);
            if (!res) return static_cast<const DescriptorType*>(nullptr);
            return res->GetDescriptor<DescriptorType>();
        }
    };

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

    // ===== PHASE F: CONTEXT SYSTEM =====

    /**
     * @brief Setup override - creates Context for setup phase
     */
    void Setup() override final {
        ResetInputsUsedInCompile();
        Context ctx(this, 0);  // Setup uses index 0
        SetupImpl(ctx);
    }

    /**
     * @brief Compile override - creates Context for compile phase
     */
    void Compile() override final {
        Context ctx(this, 0);  // Compile uses index 0
        CompileImpl(ctx);
        RegisterCleanup();
    }

    /**
     * @brief Execute override - creates Context for each task
     *
     * Intercepts NodeInstance::Execute() to create typed Context
     * objects instead of passing raw indices to ExecuteImpl().
     *
     * This enables:
     * - Clean node API: ctx.In(slot) instead of In(slot, index)
     * - Parallelization: each task gets independent context
     * - Type safety: Context is templated on ConfigType
     */
    void Execute() override final {
        // Analyze slot configuration to determine task count
        uint32_t taskCount = DetermineTaskCount();

        if (taskCount == 0) {
            // No tasks to execute (e.g., no inputs connected)
            return;
        }

        // Execute each task with its own Context
        for (uint32_t taskIndex = 0; taskIndex < taskCount; ++taskIndex) {
            // Create context bound to this task's index
            Context ctx(this, taskIndex);

            // Set currentTaskIndex for backward compatibility with legacy In()/Out()
            currentTaskIndex = taskIndex;

            // Execute with task-bound context
            ExecuteImpl(ctx);
        }

        // Reset task index after execution
        currentTaskIndex = 0;
    }

protected:
    /**
     * @brief SetupImpl with Context - override this in derived classes
     *
     * Called during Setup phase. Context provides clean In() access.
     * Context uses index 0 (non-task-based).
     *
     * @param ctx Setup context with bound slot accessors
     */
    virtual void SetupImpl(Context& ctx) {}

    /**
     * @brief CompileImpl with Context - override this in derived classes
     *
     * Called during Compile phase. Context provides clean In()/Out() access.
     * Context uses index 0 (non-task-based).
     *
     * @param ctx Compile context with bound slot accessors
     */
    virtual void CompileImpl(Context& ctx) {}

    /**
     * @brief ExecuteImpl with Context - override this in derived classes
     *
     * **Phase F: New signature for task-based execution.**
     *
     * Derived classes override this method instead of ExecuteImpl(uint32_t).
     * The Context provides bound In()/Out() accessors for clean slot access.
     *
     * Example:
     * ```cpp
     * void MyNode::ExecuteImpl(Context& ctx) override {
     *     auto device = ctx.In(MyConfig::DEVICE);
     *     auto input = ctx.In(MyConfig::INPUT_DATA);
     *     auto result = Process(device, input);
     *     ctx.Out(MyConfig::OUTPUT_DATA, result);
     * }
     * ```
     *
     * @param ctx Task context with bound slot accessors
     */
    virtual void ExecuteImpl(Context& ctx) = 0;

    /**
     * @brief Hide base class SetupImpl() - not used in TypedNode
     */
    void SetupImpl() final override {
        // Should never be called - Setup() above creates Context instead
    }

    /**
     * @brief Hide base class CompileImpl() - not used in TypedNode
     */
    void CompileImpl() final override {
        // Should never be called - Compile() above creates Context instead
    }

    /**
     * @brief Hide base class ExecuteImpl(uint32_t) - not used in TypedNode
     *
     * TypedNode intercepts Execute() to create Context, so this
     * base class method should never be called. Mark as final to prevent
     * derived classes from accidentally overriding it.
     */
    void ExecuteImpl(uint32_t taskIndex) final override {
        // Should never be called - Execute() above creates Context instead
        // This is here only to satisfy NodeInstance pure virtual requirement
    }

public:
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
     */
    template<typename SlotType>
    typename SlotType::Type In(SlotType slot) const {
        static_assert(SlotType::index < ConfigType::INPUT_COUNT, "Input index out of bounds");
        // Phase F: Use currentTaskIndex set by Execute() - provides task-local context
        uint32_t arrayIndex = static_cast<uint32_t>(currentTaskIndex);
        Resource* res = NodeInstance::GetInput(SlotType::index, arrayIndex);

        // Phase F: Use slot's metadata for dependency tracking (not parameter)
        // Mark used-in-compile if slot has Dependency role
        if ((static_cast<uint8_t>(SlotType::role) & static_cast<uint8_t>(SlotRole::Dependency)) != 0) {
            NodeInstance::MarkInputUsedInCompile(SlotType::index);
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
    template<typename SlotType>
    void SetOutput(SlotType /*slot*/, size_t arrayIndex, typename SlotType::Type value) {
        static_assert(SlotType::index < ConfigType::OUTPUT_COUNT, "Output index out of bounds");
        // Create resource if needed
        EnsureOutputSlot(SlotType::index, arrayIndex);
        Resource* res = NodeInstance::GetOutput(SlotType::index, static_cast<uint32_t>(arrayIndex));
        
        // Store typed handle in resource variant
        // SlotType::Type automatically provides the correct type!
        res->SetHandle<typename SlotType::Type>(value);
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
        uint32_t arrayIndex = static_cast<uint32_t>(GetActiveBundleIndex());
        Resource* res = NodeInstance::GetInput(SlotType::index, arrayIndex);

        // Phase F: Use slot's metadata for dependency tracking (not parameter)
        if ((static_cast<uint8_t>(SlotType::role) & static_cast<uint8_t>(SlotRole::Dependency)) != 0) {
            NodeInstance::MarkInputUsedInCompile(SlotType::index);
        }
        if (!res) return static_cast<const DescriptorType*>(nullptr);

        return res->GetDescriptor<DescriptorType>();
    }

    // Overload of Out that uses the node's active bundle index so callers inside
    // node logic don't need to pass the array index explicitly for the common
    // single-bundle case.
    template<typename SlotType>
    void Out(SlotType slot, typename SlotType::Type value) {
        static_assert(SlotType::index < ConfigType::OUTPUT_COUNT, "Output index out of bounds");
        // Phase F: Use currentTaskIndex for context-aware slot access
        size_t arrayIndex = currentTaskIndex;
        EnsureOutputSlot(SlotType::index, arrayIndex);
        Resource* res = NodeInstance::GetOutput(SlotType::index, static_cast<uint32_t>(arrayIndex));
        res->SetHandle<typename SlotType::Type>(value);
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
     * @brief Ensure output slot has space for arrayIndex
     */
    void EnsureOutputSlot(uint32_t slotIndex, size_t arrayIndex) {
        auto& slot = NodeInstance::outputs[slotIndex];
        if (slot.size() <= arrayIndex) {
            slot.resize(arrayIndex + 1, nullptr);
        }
        
        // If the resource pointer is null, create a temporary Resource for this output
        // This happens when a node wants to write to an output that wasn't connected
        // TODO: Ideally the graph should manage all Resources, but for now we create locally
        if (slot[arrayIndex] == nullptr) {
            // HACK: Create Resource inline - this should be managed by RenderGraph
            // For now, just allocate it here. This is a memory leak if not cleaned up!
            slot[arrayIndex] = new Resource();
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
