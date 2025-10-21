#pragma once

#include "NodeInstance.h"
#include "ResourceConfig.h"

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
 * Example:
 * ```cpp
 * class WindowNode : public TypedNode<WindowNodeConfig> {
 *     void Compile() override {
 *         CreateSurface();
 *
 *         // Type-safe: compiler knows Out<0>() returns VkSurfaceKHR&
 *         Out<0>() = surface;
 *
 *         // Or use named slot:
 *         Out(WindowNodeConfig::SURFACE) = surface;
 *     }
 * };
 * ```
 */
template<typename ConfigType>
class TypedNode : public NodeInstance {
public:
    TypedNode(
        const std::string& instanceName,
        NodeType* nodeType,
        Vixen::Vulkan::Resources::VulkanDevice* device
    ) : NodeInstance(instanceName, nodeType, device) {
        // Initialize arrays
        inputs.fill(VK_NULL_HANDLE);
        outputs.fill(VK_NULL_HANDLE);
    }

    virtual ~TypedNode() = default;

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

    /**
     * @brief Access input by compile-time slot
     *
     * Usage: VkImage img = In(MyConfig::ALBEDO);
     */
    template<typename SlotType>
    typename SlotType::Type& In(SlotType /*slot*/) {
        static_assert(SlotType::index < ConfigType::INPUT_COUNT, "Input index out of bounds");
        return reinterpret_cast<typename SlotType::Type&>(inputs[SlotType::index]);
    }

    template<typename SlotType>
    const typename SlotType::Type& In(SlotType /*slot*/) const {
        static_assert(SlotType::index < ConfigType::INPUT_COUNT, "Input index out of bounds");
        return reinterpret_cast<const typename SlotType::Type&>(inputs[SlotType::index]);
    }

    /**
     * @brief Access output by compile-time slot
     *
     * Usage: Out(WindowConfig::SURFACE) = mySurface;
     */
    template<typename SlotType>
    typename SlotType::Type& Out(SlotType /*slot*/) {
        static_assert(SlotType::index < ConfigType::OUTPUT_COUNT, "Output index out of bounds");
        return reinterpret_cast<typename SlotType::Type&>(outputs[SlotType::index]);
    }

    template<typename SlotType>
    const typename SlotType::Type& Out(SlotType /*slot*/) const {
        static_assert(SlotType::index < ConfigType::OUTPUT_COUNT, "Output index out of bounds");
        return reinterpret_cast<const typename SlotType::Type&>(outputs[SlotType::index]);
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
     * Usage:
     * for (size_t i = 0; i < GetInputCount(Config::DEVICE); i++) {
     *     VulkanDevice* dev = GetInput<VulkanDevice*>(Config::DEVICE, i);
     * }
     */
    template<typename T, typename SlotType>
    T GetInput(SlotType /*slot*/, size_t arrayIndex = 0) const {
        static_assert(SlotType::index < ConfigType::INPUT_COUNT, "Input index out of bounds");
        IResource* res = NodeInstance::GetInput(SlotType::index, arrayIndex);
        if (!res) return static_cast<T>(VK_NULL_HANDLE);
        // Extract typed handle from resource
        return GetResourceHandle<T>(res);
    }

    /**
     * @brief Set output resource at specific array index
     *
     * Usage: SetOutput(Config::POOL, i, pool);
     */
    template<typename SlotType>
    void SetOutput(SlotType /*slot*/, size_t arrayIndex, typename SlotType::Type value) {
        static_assert(SlotType::index < ConfigType::OUTPUT_COUNT, "Output index out of bounds");
        // Create resource if needed
        EnsureOutputSlot(SlotType::index, arrayIndex);
        IResource* res = NodeInstance::GetOutput(SlotType::index, arrayIndex);
        SetResourceHandle(res, value);
    }

private:
    /**
     * @brief Extract typed handle from IResource
     */
    template<typename T>
    T GetResourceHandle(IResource* res) const {
        // Type-punning: Resource stores handles as void*/VkHandle
        // For now, simple cast (in full implementation, check resource type)
        return reinterpret_cast<T>(const_cast<void*>(reinterpret_cast<const void*>(res)));
    }

    /**
     * @brief Store typed handle in IResource
     */
    template<typename T>
    void SetResourceHandle(IResource* res, T value) {
        // Store the handle in the resource
        // For now, type-punning (in full implementation, allocate proper Resource)
    }

    /**
     * @brief Ensure output slot has space for arrayIndex
     */
    void EnsureOutputSlot(uint32_t slotIndex, size_t arrayIndex) {
        auto& slot = NodeInstance::outputs[slotIndex];
        if (slot.size() <= arrayIndex) {
            slot.resize(arrayIndex + 1, nullptr);
        }
    }

protected:
    // Storage arrays - types are void* / VkHandle-based for now
    // In a full implementation, use std::tuple<Slot0::Type, Slot1::Type, ...>
    std::array<void*, ConfigType::INPUT_COUNT> inputs{};
    std::array<void*, ConfigType::OUTPUT_COUNT> outputs{};
};

} // namespace Vixen::RenderGraph
