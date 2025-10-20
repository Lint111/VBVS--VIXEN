#pragma once

#include "Headers.h"

#include "Resource.h"
#include <array>
#include <string>
#include <string_view>
#include <type_traits>

// Forward declare global type
struct SwapChainPublicVariables;

namespace Vixen::RenderGraph {

// Forward declaration
class NodeInstance;

/**
 * @brief Compile-time type trait to map Vulkan types to ResourceType
 */
template<typename T>
struct VulkanTypeTraits {
    static constexpr ResourceType resourceType = ResourceType::Buffer;
    static constexpr bool isValid = false;
};

// Specializations for common Vulkan types
template<> struct VulkanTypeTraits<VkImage> {
    static constexpr ResourceType resourceType = ResourceType::Image;
    static constexpr bool isValid = true;
};

template<> struct VulkanTypeTraits<VkBuffer> {
    static constexpr ResourceType resourceType = ResourceType::Buffer;
    static constexpr bool isValid = true;
};

template<> struct VulkanTypeTraits<VkSurfaceKHR> {
    static constexpr ResourceType resourceType = ResourceType::Image;
    static constexpr bool isValid = true;
};

template<> struct VulkanTypeTraits<VkImageView> {
    static constexpr ResourceType resourceType = ResourceType::Image;
    static constexpr bool isValid = true;
};

template<> struct VulkanTypeTraits<VkAccelerationStructureKHR> {
    static constexpr ResourceType resourceType = ResourceType::AccelerationStructure;
    static constexpr bool isValid = true;
};

template<> struct VulkanTypeTraits<VkSemaphore> {
    static constexpr ResourceType resourceType = ResourceType::Buffer;  // Opaque handle
    static constexpr bool isValid = true;
};

template<> struct VulkanTypeTraits<VkSwapchainKHR> {
    static constexpr ResourceType resourceType = ResourceType::Buffer; // Opaque handle
    static constexpr bool isValid = true;
};

template<> struct VulkanTypeTraits<VkRenderPass> {
    static constexpr ResourceType resourceType = ResourceType::Buffer;  // Opaque handle
    static constexpr bool isValid = true;
};

template<> struct VulkanTypeTraits<VkInstance> {
    static constexpr ResourceType resourceType = ResourceType::Buffer;  // Opaque handle
    static constexpr bool isValid = true;
};

template<> struct VulkanTypeTraits<VkPhysicalDevice> {
    static constexpr ResourceType resourceType = ResourceType::Buffer;  // Opaque handle
    static constexpr bool isValid = true;
};

template<> struct VulkanTypeTraits<VkDevice> {
    static constexpr ResourceType resourceType = ResourceType::Buffer;  // Opaque handle
    static constexpr bool isValid = true;
};

template<> struct VulkanTypeTraits<uint32_t> {
    static constexpr ResourceType resourceType = ResourceType::Buffer;  // Scalar parameter
    static constexpr bool isValid = true;
};

template<> struct VulkanTypeTraits<VkCommandPool> {
    static constexpr ResourceType resourceType = ResourceType::Buffer;  // Opaque handle
    static constexpr bool isValid = true;
};

template<> struct VulkanTypeTraits<VkFormat> {
    static constexpr ResourceType resourceType = ResourceType::Buffer;  // Enum value
    static constexpr bool isValid = true;
};

// SwapChainPublicVariables* (defined in VulkanSwapChain.h in global namespace)
template<> struct VulkanTypeTraits<::SwapChainPublicVariables*> {
    static constexpr ResourceType resourceType = ResourceType::Buffer;  // Opaque pointer
    static constexpr bool isValid = true;
};

// Windows platform handles
template<> struct VulkanTypeTraits<HWND> {
    static constexpr ResourceType resourceType = ResourceType::Buffer;  // Platform handle
    static constexpr bool isValid = true;
};

template<> struct VulkanTypeTraits<HINSTANCE> {
    static constexpr ResourceType resourceType = ResourceType::Buffer;  // Platform handle
    static constexpr bool isValid = true;
};

/**
 * @brief Compile-time resource slot descriptor
 *
 * All information is constexpr - completely resolved at compile time.
 * Zero runtime overhead.
 */
template<typename T, uint32_t Idx, bool Nullable = false>
struct ResourceSlot {
    using Type = T;

    static constexpr uint32_t index = Idx;
    static constexpr ResourceType resourceType = VulkanTypeTraits<T>::resourceType;
    static constexpr bool nullable = Nullable;

    // Compile-time validation
    static_assert(VulkanTypeTraits<T>::isValid, "Unsupported Vulkan resource type");

    // Default constructor for use as constant
    constexpr ResourceSlot() = default;
};

/**
 * @brief Compile-time resource configuration base
 *
 * Pure constexpr - all information known at compile time.
 * The compiler can optimize away all the template machinery.
 */
template<size_t NumInputs, size_t NumOutputs, bool ArrayAbleFlag = false>
struct ResourceConfigBase {
    static constexpr size_t INPUT_COUNT = NumInputs;
    static constexpr size_t OUTPUT_COUNT = NumOutputs;
    static constexpr bool ALLOW_INPUT_ARRAYS = ArrayAbleFlag;

    // Helper to get input/output vectors for NodeType
    std::vector<ResourceDescriptor> GetInputVector() const {
        if constexpr (INPUT_COUNT == 0) {
            return {};
        } else {
            return std::vector<ResourceDescriptor>(inputs.begin(), inputs.end());
        }
    }

    std::vector<ResourceDescriptor> GetOutputVector() const {
        if constexpr (OUTPUT_COUNT == 0) {
            return {};
        } else {
            return std::vector<ResourceDescriptor>(outputs.begin(), outputs.end());
        }
    }

protected:
    std::array<ResourceDescriptor, NumInputs> inputs{};
    std::array<ResourceDescriptor, NumOutputs> outputs{};
};

/**
 * @brief Type-safe resource accessor
 *
 * All type checking and index validation happens at compile time.
 * Runtime code is just direct array access - no overhead.
 */
template<typename ConfigType>
class ResourceAccessor {
public:
    constexpr explicit ResourceAccessor(NodeInstance* node) : nodeInstance(node) {}

    /**
     * @brief Get resource using compile-time slot
     *
     * Template magic: SlotType contains all compile-time info.
     * Compiler resolves typename SlotType::Type at compile time.
     * Runtime code is just: return GetImpl<VkImage>(0);
     */
    template<typename SlotType>
    [[nodiscard]] constexpr typename SlotType::Type Get(SlotType /*slot*/) const {
        // Compile-time checks (optimized away)
        static_assert(SlotType::index < ConfigType::OUTPUT_COUNT,
            "Output slot index out of bounds");

        // Runtime: just direct array access
        return GetOutputImpl<typename SlotType::Type>(SlotType::index);
    }

    /**
     * @brief Set resource using compile-time slot
     */
    template<typename SlotType>
    constexpr void Set(SlotType /*slot*/, typename SlotType::Type value) {
        // Compile-time checks
        static_assert(SlotType::index < ConfigType::OUTPUT_COUNT,
            "Output slot index out of bounds");

        // Runtime: just direct array write
        SetOutputImpl<typename SlotType::Type>(SlotType::index, value);
    }

    /**
     * @brief Get input using compile-time slot
     */
    template<typename SlotType>
    [[nodiscard]] constexpr typename SlotType::Type GetInput(SlotType /*slot*/) const {
        static_assert(SlotType::index < ConfigType::INPUT_COUNT,
            "Input slot index out of bounds");

        return GetInputImpl<typename SlotType::Type>(SlotType::index);
    }

    /**
     * @brief Set input using compile-time slot
     */
    template<typename SlotType>
    constexpr void SetInput(SlotType /*slot*/, typename SlotType::Type value) {
        static_assert(SlotType::index < ConfigType::INPUT_COUNT,
            "Input slot index out of bounds");

        SetInputImpl<typename SlotType::Type>(SlotType::index, value);
    }

    /**
     * @brief Check if slot is nullable (compile-time)
     */
    template<typename SlotType>
    [[nodiscard]] static constexpr bool IsNullable(SlotType /*slot*/) {
        return SlotType::nullable;
    }

private:
    // Implementation details (defined in NodeInstance)
    template<typename T>
    T GetInputImpl(uint32_t index) const;

    template<typename T>
    T GetOutputImpl(uint32_t index) const;

    template<typename T>
    void SetInputImpl(uint32_t index, T value);

    template<typename T>
    void SetOutputImpl(uint32_t index, T value);

    NodeInstance* nodeInstance;
};

/**
 * @brief Helper to create runtime ResourceDescriptor from compile-time slot
 *
 * This is the only place where compile-time info becomes runtime data.
 * Called during node initialization to populate descriptor arrays.
 */
template<typename SlotType, typename DescType = ImageDescription>
ResourceDescriptor MakeDescriptor(
    std::string_view name,
    ResourceLifetime lifetime,
    const DescType& desc = DescType{}
) {
    return ResourceDescriptor{
        std::string(name),
        SlotType::resourceType,  // Compile-time constant
        lifetime,
        desc,
        SlotType::nullable       // Compile-time constant
    };
}

// ====================================================================
// ZERO-OVERHEAD MACRO API
// ====================================================================

/**
 * @brief Define a pure constexpr node configuration
 *
 * All type information is constexpr - compiler optimizes everything away.
 *
 * Usage:
 * ```cpp
 * CONSTEXPR_NODE_CONFIG(WindowNodeConfig, 0, 1) {
 *     CONSTEXPR_OUTPUT(SURFACE, VkSurfaceKHR, 0, false);
 * };
 * ```
 */
#define CONSTEXPR_NODE_CONFIG(ConfigName, NumInputs, NumOutputs, ArrayAbleFlag) \
    struct ConfigName : public ::Vixen::RenderGraph::ResourceConfigBase<NumInputs, NumOutputs, ArrayAbleFlag>

/**
 * @brief Define a compile-time input slot
 *
 * Creates a type alias and constexpr constant.
 * Zero runtime cost - all information known at compile time.
 */
#define CONSTEXPR_INPUT(SlotName, SlotType, Index, Nullable) \
    using SlotName##_Slot = ::Vixen::RenderGraph::ResourceSlot<SlotType, Index, Nullable>; \
    static constexpr SlotName##_Slot SlotName{}

/**
 * @brief Define a compile-time output slot
 */
#define CONSTEXPR_OUTPUT(SlotName, SlotType, Index, Nullable) \
    using SlotName##_Slot = ::Vixen::RenderGraph::ResourceSlot<SlotType, Index, Nullable>; \
    static constexpr SlotName##_Slot SlotName{}

/**
 * @brief Runtime descriptor initialization (only part with runtime cost)
 *
 * This is called during NodeType construction to populate the descriptor arrays.
 * The descriptor contains runtime strings, but the type/index info comes from
 * compile-time constants.
 */
#define INIT_SLOT_DESCRIPTOR(Array, Slot, Name, Lifetime, Desc) \
    Array[decltype(Slot)::index] = ::Vixen::RenderGraph::MakeDescriptor<decltype(Slot)>( \
        Name, Lifetime, Desc \
    )

/**
 * @brief Simplified initialization for common cases
 */
#define INIT_INPUT_DESC(Slot, Name, Lifetime, Desc) \
    INIT_SLOT_DESCRIPTOR(inputs, Slot, Name, Lifetime, Desc)

#define INIT_OUTPUT_DESC(Slot, Name, Lifetime, Desc) \
    INIT_SLOT_DESCRIPTOR(outputs, Slot, Name, Lifetime, Desc)

// ====================================================================
// COMPILE-TIME TYPE VALIDATION
// ====================================================================

/**
 * @brief Validate slot type at compile time
 *
 * Usage in static_assert:
 * static_assert(ValidateSlotType<WindowNodeConfig::SURFACE, VkSurfaceKHR>());
 */
template<typename SlotType, typename ExpectedType>
constexpr bool ValidateSlotType() {
    return std::is_same_v<typename SlotType::Type, ExpectedType>;
}

/**
 * @brief Validate slot index at compile time
 */
template<typename SlotType, uint32_t ExpectedIndex>
constexpr bool ValidateSlotIndex() {
    return SlotType::index == ExpectedIndex;
}

/**
 * @brief Get slot type as compile-time string (for error messages)
 */
template<typename T>
constexpr const char* GetTypeName() {
    if constexpr (std::is_same_v<T, VkImage>) return "VkImage";
    else if constexpr (std::is_same_v<T, VkBuffer>) return "VkBuffer";
    else if constexpr (std::is_same_v<T, VkSurfaceKHR>) return "VkSurfaceKHR";
    else if constexpr (std::is_same_v<T, VkImageView>) return "VkImageView";
    else if constexpr (std::is_same_v<T, VkSemaphore>) return "VkSemaphore";
    else if constexpr (std::is_same_v<T, VkRenderPass>) return "VkRenderPass";
    else return "Unknown";
}

} // namespace Vixen::RenderGraph
