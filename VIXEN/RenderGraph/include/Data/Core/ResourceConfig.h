#pragma once

#include "Headers.h"

#include "Data/Core/ResourceTypes.h"
#include <array>
#include <string>
#include <string_view>
#include <type_traits>
#include "Data/Core/ResourceVariant.h"

// Forward declare global type
struct SwapChainPublicVariables;

// Forward declare ShaderManagement types
namespace ShaderManagement {
    struct CompiledProgram;
}

namespace Vixen::Vulkan::Resources {
    class VulkanDevice;  // Forward declare VulkanDevice
}

namespace Vixen::RenderGraph {
    struct ShaderProgramDescriptor;  // Forward declare from ShaderLibraryNodeConfig.h
}

namespace Vixen::RenderGraph {

// Forward declarations - using variant-based descriptors from ResourceVariant.h
class NodeInstance;
struct ImageDescriptor;
struct BufferDescriptor;
struct ShaderProgramDescriptor;

/**
 * @brief Slot array capability enum (replaces magic bool)
 *
 * Clearly indicates whether a slot can have multiple elements (array).
 */
enum class SlotArrayMode : uint8_t {
    Single = 0,  // Single slot only (e.g., one framebuffer)
    Array = 1    // Array of slots (e.g., multiple color attachments)
};

/**
 * @brief Slot nullability enum (Phase F: replaces opaque bool)
 *
 * Clearly indicates whether a slot connection is required or optional.
 */
enum class SlotNullability : uint8_t {
    Required = 0,  // Slot must be connected (validation error if not)
    Optional = 1   // Slot connection is optional (nullable)
};

/**
 * @brief Slot role enum (Phase F: moved from NodeInstance)
 *
 * Indicates when the slot is accessed during node lifecycle.
 * Used for dependency tracking and compile-time validation.
 */
enum class SlotRole : uint8_t {
    Output       = 0,        // Output slot (role only applies to inputs)
    Dependency   = 1u << 0,  // Accessed during Compile (creates dependency)
    ExecuteOnly  = 1u << 1,  // Only accessed during Execute (no dependency)
    CleanupOnly  = 1u << 2   // Only accessed during Cleanup
};

/**
 * @brief Slot mutability enum (Phase F: parallel safety)
 *
 * Indicates read/write access pattern for automatic synchronization.
 */
enum class SlotMutability : uint8_t {
    ReadOnly   = 1u << 0,  // Node only reads (parallel-safe)
    WriteOnly  = 1u << 1,  // Node only writes (output slots)
    ReadWrite  = 1u << 2   // Node reads and writes (needs locking if parallel)
};

/**
 * @brief Slot scope enum (Phase F: slot task system)
 *
 * Indicates resource allocation scope for slot task system.
 */
enum class SlotScope : uint8_t {
    NodeLevel,      // Shared across all slot tasks (e.g., VkDevice, command pool)
    TaskLevel,      // Per-task configuration (e.g., format, sampler settings)
    InstanceLevel   // Parameterized input - array size drives task count
};

/**
 * @brief Helper constexpr values for slot count aliasing
 * 
 * Usage: InputCount<3> instead of magic number 3
 */
template<size_t N>
struct InputCount {
    static constexpr size_t value = N;
};

template<size_t N>
struct OutputCount {
    static constexpr size_t value = N;
};

// Convenience aliases for common cases
using NoInputs = InputCount<0>;
using OneInput = InputCount<1>;
using TwoInputs = InputCount<2>;
using ThreeInputs = InputCount<3>;

using NoOutputs = OutputCount<0>;
using OneOutput = OutputCount<1>;
using TwoOutputs = OutputCount<2>;
using ThreeOutputs = OutputCount<3>;


/**
 * @brief Compile-time resource slot descriptor (Phase F: Extended Metadata)
 *
 * All information is constexpr - completely resolved at compile time.
 * Zero runtime overhead.
 *
 * Phase F Extensions:
 * - SlotNullability replaces opaque bool nullable
 * - SlotRole moved from call-site to config
 * - SlotMutability for parallel safety
 * - SlotScope for slot task resource allocation
 */
template<
    typename T,
    uint32_t Idx,
    SlotNullability Nullability = SlotNullability::Required,
    SlotRole Role = SlotRole::Dependency,
    SlotMutability Mutability = SlotMutability::ReadOnly,
    SlotScope Scope = SlotScope::NodeLevel
>
struct ResourceSlot {
    using Type = T;

    static constexpr uint32_t index = Idx;
    static constexpr ResourceType resourceType = ResourceTypeTraits<T>::resourceType;

    // Legacy compatibility
    static constexpr bool nullable = (Nullability == SlotNullability::Optional);

    // Phase F metadata
    static constexpr SlotNullability nullability = Nullability;
    static constexpr SlotRole role = Role;
    static constexpr SlotMutability mutability = Mutability;
    static constexpr SlotScope scope = Scope;

    // Compile-time validation
    static_assert(ResourceTypeTraits<T>::isValid, "Unsupported Vulkan resource type");

    // Default constructor for use as constant
    constexpr ResourceSlot() = default;
};

/**
 * @brief Compile-time resource configuration base (Phase F: Auto-Indexing)
 *
 * Pure constexpr - all information known at compile time.
 * The compiler can optimize away all the template machinery.
 *
 * Phase F Extensions:
 * - Embedded __COUNTER__ bases for automatic slot indexing
 * - Each config gets independent counters (0..N inputs, 0..M outputs)
 */
template<size_t NumInputs, size_t NumOutputs, SlotArrayMode ArrayMode = SlotArrayMode::Single>
struct ResourceConfigBase {
    static constexpr size_t INPUT_COUNT = NumInputs;
    static constexpr size_t OUTPUT_COUNT = NumOutputs;
    static constexpr SlotArrayMode ARRAY_MODE = ArrayMode;

    // Legacy compatibility (deprecated - use ARRAY_MODE instead)
    static constexpr bool ALLOW_INPUT_ARRAYS = (ArrayMode == SlotArrayMode::Array);

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
     * @brief Get resource using compile-time slot (Phase F: Mutability enforcement)
     *
     * Template magic: SlotType contains all compile-time info.
     * Compiler resolves typename SlotType::Type at compile time.
     * Runtime code is just: return GetImpl<VkImage>(0);
     *
     * **Compile-time enforced**: Cannot Get() from WriteOnly slots.
     */
    template<typename SlotType>
    [[nodiscard]] constexpr typename SlotType::Type Get(SlotType /*slot*/) const {
        // Compile-time checks (optimized away)
        static_assert(SlotType::index < ConfigType::OUTPUT_COUNT,
            "Output slot index out of bounds");

        // Phase F: Enforce mutability at compile-time
        static_assert(SlotType::mutability != SlotMutability::WriteOnly,
            "Cannot Get() from a WriteOnly slot. Use Set() instead.");

        // Runtime: just direct array access
        return GetOutputImpl<typename SlotType::Type>(SlotType::index);
    }

    /**
     * @brief Set resource using compile-time slot (Phase F: Mutability enforcement)
     *
     * **Compile-time enforced**: Cannot Set() to ReadOnly slots.
     */
    template<typename SlotType>
    constexpr void Set(SlotType /*slot*/, typename SlotType::Type value) {
        // Compile-time checks
        static_assert(SlotType::index < ConfigType::OUTPUT_COUNT,
            "Output slot index out of bounds");

        // Phase F: Enforce mutability at compile-time
        static_assert(SlotType::mutability != SlotMutability::ReadOnly,
            "Cannot Set() a ReadOnly slot. Slot is read-only.");

        // Runtime: just direct array write
        SetOutputImpl<typename SlotType::Type>(SlotType::index, value);
    }

    /**
     * @brief Get input using compile-time slot (Phase F: Mutability enforcement)
     *
     * **Compile-time enforced**: Cannot GetInput() from WriteOnly slots.
     */
    template<typename SlotType>
    [[nodiscard]] constexpr typename SlotType::Type GetInput(SlotType /*slot*/) const {
        static_assert(SlotType::index < ConfigType::INPUT_COUNT,
            "Input slot index out of bounds");

        // Phase F: Enforce mutability at compile-time
        static_assert(SlotType::mutability != SlotMutability::WriteOnly,
            "Cannot GetInput() from a WriteOnly slot. Use SetInput() instead.");

        return GetInputImpl<typename SlotType::Type>(SlotType::index);
    }

    /**
     * @brief Set input using compile-time slot (Phase F: Mutability enforcement)
     *
     * **Compile-time enforced**: Cannot SetInput() to ReadOnly slots.
     */
    template<typename SlotType>
    constexpr void SetInput(SlotType /*slot*/, typename SlotType::Type value) {
        static_assert(SlotType::index < ConfigType::INPUT_COUNT,
            "Input slot index out of bounds");

        // Phase F: Enforce mutability at compile-time
        static_assert(SlotType::mutability != SlotMutability::ReadOnly,
            "Cannot SetInput() to a ReadOnly slot. Input slot is read-only.");

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
 * Phase F: Auto-indexing support via embedded counter bases.
 * Each config captures __COUNTER__ at definition time for per-config isolation.
 *
 * Usage (Modern - Named enum):
 * ```cpp
 * CONSTEXPR_NODE_CONFIG(WindowNodeConfig, 0, 1, SlotArrayMode::Single) {
 *     CONSTEXPR_OUTPUT(SURFACE, VkSurfaceKHR, 0, false);
 * };
 * ```
 *
 * For better readability, use named constants in struct body:
 * ```cpp
 * struct MyNodeConfig : public ResourceConfigBase<1, 2, SlotArrayMode::Array> {
 *     static constexpr auto INPUTS = OneInput::value;   // Documentation
 *     static constexpr auto OUTPUTS = TwoOutputs::value; // Documentation
 *     // ... slot definitions ...
 * };
 * ```
 */
#define CONSTEXPR_NODE_CONFIG(ConfigName, NumInputs, NumOutputs, ArrayMode) \
    struct ConfigName : public ::Vixen::RenderGraph::ResourceConfigBase<NumInputs, NumOutputs, ArrayMode>

/**
 * @brief Define a compile-time input slot (legacy - 4 parameters)
 *
 * Creates a type alias and constexpr constant.
 * Zero runtime cost - all information known at compile time.
 *
 * DEPRECATED: Use AUTO_INPUT or CONSTEXPR_INPUT_FULL for new code.
 */
#define CONSTEXPR_INPUT(SlotName, SlotType, Index, Nullable) \
    using SlotName##_Slot = ::Vixen::RenderGraph::ResourceSlot< \
        SlotType, \
        Index, \
        (Nullable) ? ::Vixen::RenderGraph::SlotNullability::Optional : ::Vixen::RenderGraph::SlotNullability::Required \
    >; \
    static constexpr SlotName##_Slot SlotName{}

/**
 * @brief Define a compile-time output slot (legacy - 4 parameters)
 *
 * DEPRECATED: Use AUTO_OUTPUT or CONSTEXPR_OUTPUT_FULL for new code.
 */
#define CONSTEXPR_OUTPUT(SlotName, SlotType, Index, Nullable) \
    using SlotName##_Slot = ::Vixen::RenderGraph::ResourceSlot< \
        SlotType, \
        Index, \
        (Nullable) ? ::Vixen::RenderGraph::SlotNullability::Optional : ::Vixen::RenderGraph::SlotNullability::Required \
    >; \
    static constexpr SlotName##_Slot SlotName{}

/**
 * @brief Define input slot with full Phase F metadata (manual index)
 *
 * Phase F: Supports all metadata parameters (nullability, role, mutability, scope)
 */
#define CONSTEXPR_INPUT_FULL(SlotName, SlotType, Index, Nullability, Role, Mutability, Scope) \
    using SlotName##_Slot = ::Vixen::RenderGraph::ResourceSlot< \
        SlotType, \
        Index, \
        Nullability, \
        Role, \
        Mutability, \
        Scope \
    >; \
    static constexpr SlotName##_Slot SlotName{}

/**
 * @brief Define output slot with full Phase F metadata (manual index)
 *
 * Note: Outputs use SlotRole::Output - role is for inputs only (determines when consumer accesses the resource)
 */
#define CONSTEXPR_OUTPUT_FULL(SlotName, SlotType, Index, Nullability, Mutability) \
    using SlotName##_Slot = ::Vixen::RenderGraph::ResourceSlot< \
        SlotType, \
        Index, \
        Nullability, \
        ::Vixen::RenderGraph::SlotRole::Output, \
        Mutability, \
        ::Vixen::RenderGraph::SlotScope::NodeLevel \
    >; \
    static constexpr SlotName##_Slot SlotName{}


/**
 * @brief Input slot with Phase F metadata (manual index)
 *
 * Use when you need explicit control over slot indices.
 */
#define INPUT_SLOT(SlotName, SlotType, Index, Nullability, Role, Mutability, Scope) \
    CONSTEXPR_INPUT_FULL( \
        SlotName, \
        SlotType, \
        Index, \
        Nullability, \
        Role, \
        Mutability, \
        Scope \
    )

/**
 * @brief Output slot with Phase F metadata (manual index)
 *
 * Use when you need explicit control over slot indices.
 * Note: Outputs don't have SlotRole parameter - only inputs need role.
 */
#define OUTPUT_SLOT(SlotName, SlotType, Index, Nullability, Mutability) \
    CONSTEXPR_OUTPUT_FULL( \
        SlotName, \
        SlotType, \
        Index, \
        Nullability, \
        Mutability \
    )

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

} // namespace Vixen::RenderGraph
