#pragma once

#include "Headers.h"

#include "Data/Core/ResourceTypes.h"
#include <array>
#include <string>
#include <string_view>
#include <type_traits>
#include "Data/Core/CompileTimeResourceSystem.h"

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
	None         = 0u,        // No role assigned
    Dependency   = 1u << 0,  // Accessed during Compile (creates dependency)
    Execute      = 1u << 1,  // Accessed during Execute (can be combined with Dependency)
    CleanupOnly  = 1u << 2,  // Only accessed during Cleanup
    Output       = 1u << 3,  // Output slot (role only applies to inputs)
    Debug        = 1u << 4   // Debug resource - auto-routed to debug output by gatherer
};

// Bitwise operators for SlotRole
constexpr inline SlotRole operator|(SlotRole a, SlotRole b) {
    return static_cast<SlotRole>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

constexpr inline SlotRole operator&(SlotRole a, SlotRole b) {
    return static_cast<SlotRole>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

inline bool operator==(SlotRole a, uint8_t b) {
    return static_cast<uint8_t>(a) == b;
}

inline bool operator!=(SlotRole a, uint8_t b) {
    return static_cast<uint8_t>(a) != b;
}

inline SlotRole& operator|=(SlotRole& a, SlotRole b) {
    return a = a | b;
}

inline SlotRole& operator&=(SlotRole& a, SlotRole b) {
    return a = a & b;
}

inline SlotRole operator~(SlotRole a) {
    return static_cast<SlotRole>(~static_cast<uint8_t>(a));
}

// Helper functions for cleaner SlotRole checks
inline bool HasRole(SlotRole role, SlotRole flag) {
    return (static_cast<uint8_t>(role) & static_cast<uint8_t>(flag)) != 0;
}

inline bool HasDependency(SlotRole role) {
    return HasRole(role, SlotRole::Dependency);
}

inline bool HasExecute(SlotRole role) {
    return HasRole(role, SlotRole::Execute);
}

inline bool HasDebug(SlotRole role) {
    return HasRole(role, SlotRole::Debug);
}

inline bool IsDependencyOnly(SlotRole role) {
    return role == SlotRole::Dependency;
}

inline bool IsExecuteOnly(SlotRole role) {
    return role == SlotRole::Execute;
}

inline bool operator&(SlotRole a, uint8_t b) {
    return (static_cast<uint8_t>(a) & b) != 0;
}

inline bool operator|(SlotRole a, uint8_t b) {
    return (static_cast<uint8_t>(a) | b) != 0;
}

// implicit conversion to bool
inline bool operator!(SlotRole a) {
    return static_cast<uint8_t>(a) == 0;
}
inline bool ToBool(SlotRole a) {
    return static_cast<uint8_t>(a) != 0;
}



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
 * @brief Storage strategy for accumulation slots (Sprint 6.0.2)
 *
 * Determines how accumulated data is stored and validated.
 *
 * - Value: Elements are copied into the container (default, safe)
 *   - Validates: None (always safe)
 *   - Warning: Logs if total copy size > 1KB
 *
 * - Reference: Elements are stored as references (zero-copy, requires Persistent sources)
 *   - Validates: Source slot must have Persistent lifetime
 *   - Compile Error: If connected source is Transient
 *
 * - Span: Elements are stored as std::span (view, requires Persistent sources)
 *   - Validates: Source slot must have Persistent lifetime
 *   - Compile Error: If connected source is Transient
 *
 * Usage:
 * ```cpp
 * ACCUMULATION_INPUT_SLOT_V2(PASSES, std::vector<DispatchPass>, DispatchPass, 0,
 *     SlotNullability::Required, SlotRole::Dependency,
 *     SlotStorageStrategy::Value);  // Copies DispatchPass elements
 *
 * ACCUMULATION_INPUT_SLOT_V2(LARGE_BUFFERS, std::vector<VkBuffer>, VkBuffer, 1,
 *     SlotNullability::Required, SlotRole::Dependency,
 *     SlotStorageStrategy::Reference);  // Requires Persistent source
 * ```
 */
enum class SlotStorageStrategy : uint8_t {
    Value      = 0,  // Copy elements (safe, may warn if large)
    Reference  = 1,  // Store references (zero-copy, requires Persistent)
    Span       = 2   // Store std::span (view, requires Persistent)
};

// ====================================================================
// SPRINT 6.0.1: UNIFIED CONNECTION SYSTEM - SlotFlags Infrastructure
// ====================================================================

/**
 * @brief Slot behavioral flags for unified connection system
 *
 * These flags extend slot capabilities beyond basic type/role metadata.
 * Used to enable accumulation (multi-connect) and explicit ordering.
 *
 * Usage:
 * ```cpp
 * INPUT_SLOT(DISPATCH_PASSES, std::vector<DispatchPass>, 0,
 *     SlotNullability::Required, SlotRole::Dependency,
 *     SlotMutability::ReadOnly, SlotScope::NodeLevel,
 *     SlotFlags::Accumulation | SlotFlags::MultiConnect);
 * ```
 */
enum class SlotFlags : uint32_t {
    None           = 0,         ///< No special behavior
    Accumulation   = 1u << 0,   ///< Accepts T → vector<T>, flattens vector<T>
    MultiConnect   = 1u << 1,   ///< Allows multiple sources to same slot
    ExplicitOrder  = 1u << 2,   ///< Requires ordering metadata on connections
};

// Bitwise operators for SlotFlags
constexpr inline SlotFlags operator|(SlotFlags a, SlotFlags b) {
    return static_cast<SlotFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr inline SlotFlags operator&(SlotFlags a, SlotFlags b) {
    return static_cast<SlotFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

constexpr inline SlotFlags operator~(SlotFlags a) {
    return static_cast<SlotFlags>(~static_cast<uint32_t>(a));
}

constexpr inline SlotFlags& operator|=(SlotFlags& a, SlotFlags b) {
    return a = a | b;
}

constexpr inline SlotFlags& operator&=(SlotFlags& a, SlotFlags b) {
    return a = a & b;
}

// Helper functions for SlotFlags checks
constexpr inline bool HasFlag(SlotFlags flags, SlotFlags flag) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
}

constexpr inline bool HasAccumulation(SlotFlags flags) {
    return HasFlag(flags, SlotFlags::Accumulation);
}

constexpr inline bool HasMultiConnect(SlotFlags flags) {
    return HasFlag(flags, SlotFlags::MultiConnect);
}

constexpr inline bool HasExplicitOrder(SlotFlags flags) {
    return HasFlag(flags, SlotFlags::ExplicitOrder);
}

/**
 * @brief Ordering strategy for accumulation slots
 *
 * Determines how multiple connections to an accumulation slot are ordered.
 */
enum class OrderStrategy : uint8_t {
    ConnectionOrder,  ///< Order by when Connect() was called (legacy behavior)
    ByMetadata,       ///< Sort by explicit metadata key (recommended)
    BySourceSlot,     ///< Use source slot's embedded metadata
    Unordered         ///< Set semantics - no guaranteed order
};

/**
 * @brief Data handling strategy for accumulation slots
 *
 * Determines how values from source connections are stored:
 * - ByValue: Copy values into the accumulation (vector<T>)
 * - ByReference: Store pointers to sources (vector<T*>)
 * - BySpan: Store non-owning view (span<T> from single source)
 *
 * Example use cases:
 * - ByValue: DispatchPass structs that are small and need copying
 * - ByReference: Large resources where copying is expensive
 * - BySpan: When source is already a contiguous array
 */
enum class AccumulationStorage : uint8_t {
    ByValue,      ///< Copy values (T → vector<T>)
    ByReference,  ///< Store pointers (T → vector<T*>)
    BySpan        ///< Non-owning view (requires contiguous source)
};

/**
 * @brief Configuration for accumulation slots
 *
 * Specifies constraints, ordering, and storage for slots that accept multiple connections.
 * Used with SlotFlags::Accumulation.
 *
 * The target slot can be any Iterable type (vector, array, span, custom container).
 * Storage strategy determines how source values are stored:
 * - ByValue: Copies values (safest, works with any iterable target)
 * - ByReference: Stores pointers (efficient, requires lifetime management)
 * - BySpan: Non-owning view (most efficient, requires contiguous source)
 */
struct AccumulationConfig {
    size_t minConnections = 0;                              ///< Minimum required connections
    size_t maxConnections = SIZE_MAX;                       ///< Maximum allowed connections
    OrderStrategy orderStrategy = OrderStrategy::ByMetadata; ///< How to order connections
    AccumulationStorage storage = AccumulationStorage::ByValue; ///< How to store values
    bool allowDuplicateKeys = false;                        ///< Allow same sortKey on multiple connections
    bool flattenIterables = true;                           ///< Flatten source containers into accumulation

    // Constexpr constructor for compile-time usage
    constexpr AccumulationConfig() = default;

    constexpr AccumulationConfig(size_t min, size_t max,
                                  OrderStrategy order = OrderStrategy::ByMetadata,
                                  bool duplicates = false)
        : minConnections(min), maxConnections(max),
          orderStrategy(order), allowDuplicateKeys(duplicates) {}

    constexpr AccumulationConfig(size_t min, size_t max,
                                  OrderStrategy order,
                                  AccumulationStorage storageMode,
                                  bool duplicates = false,
                                  bool flatten = true)
        : minConnections(min), maxConnections(max),
          orderStrategy(order), storage(storageMode),
          allowDuplicateKeys(duplicates), flattenIterables(flatten) {}
};

/**
 * @brief Helper constexpr values for slot count aliasing
 * 
 * Usage: InputCount<3> instead of magic number 3
 */
// NOTE: legacy InputCount/OutputCount helpers were removed to simplify the API.
// Use explicit numeric literals or Config::INPUT_COUNT / Config::OUTPUT_COUNT when
// documenting expected counts inside node configs.


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
 *
 * Sprint 6.0.1 Extensions:
 * - SlotFlags for accumulation/multi-connect behavior
 *
 * Sprint 6.0.2 Extensions:
 * - SlotStorageStrategy for accumulation slots (Value/Reference/Span)
 */
template<
    typename T,
    uint32_t Idx,
    SlotNullability Nullability = SlotNullability::Required,
    SlotRole Role = SlotRole::Dependency,
    SlotMutability Mutability = SlotMutability::ReadOnly,
    SlotScope Scope = SlotScope::NodeLevel,
    SlotFlags Flags = SlotFlags::None,
    SlotStorageStrategy StorageStrategy = SlotStorageStrategy::Value
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

    // Sprint 6.0.1: Connection flags
    static constexpr SlotFlags flags = Flags;

    // Sprint 6.0.2: Storage strategy for accumulation
    static constexpr SlotStorageStrategy storageStrategy = StorageStrategy;

    // Helper accessors for flags
    static constexpr bool isAccumulation = HasAccumulation(Flags);
    static constexpr bool isMultiConnect = HasMultiConnect(Flags);
    static constexpr bool requiresExplicitOrder = HasExplicitOrder(Flags);

    // Compile-time validation
    static_assert(ResourceTypeTraits<T>::isValid, "Unsupported Vulkan resource type");

    // Sprint 6.0.1: Accumulation slots should use MultiConnect
    static_assert(
        !HasAccumulation(Flags) || HasMultiConnect(Flags),
        "Accumulation slots must also have MultiConnect flag set"
    );

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

// ====================================================================
// SPRINT 6.0.1: EXTENDED MACROS WITH FLAGS SUPPORT
// ====================================================================

/**
 * @brief Define input slot with full metadata including flags (Sprint 6.0.1)
 *
 * Extends CONSTEXPR_INPUT_FULL with SlotFlags parameter for accumulation/multi-connect.
 *
 * Usage:
 * ```cpp
 * CONSTEXPR_INPUT_FULL_WITH_FLAGS(DISPATCH_PASSES, std::vector<DispatchPass>, 0,
 *     SlotNullability::Required, SlotRole::Dependency,
 *     SlotMutability::ReadOnly, SlotScope::NodeLevel,
 *     SlotFlags::Accumulation | SlotFlags::MultiConnect);
 * ```
 */
#define CONSTEXPR_INPUT_FULL_WITH_FLAGS(SlotName, SlotType, Index, Nullability, Role, Mutability, Scope, Flags) \
    using SlotName##_Slot = ::Vixen::RenderGraph::ResourceSlot< \
        SlotType, \
        Index, \
        Nullability, \
        Role, \
        Mutability, \
        Scope, \
        Flags \
    >; \
    static constexpr SlotName##_Slot SlotName{}

/**
 * @brief Input slot with flags for accumulation/multi-connect (Sprint 6.0.1)
 *
 * Use for slots that accept multiple connections (accumulation pattern).
 *
 * Example:
 * ```cpp
 * // Accumulation slot that gathers DispatchPass from multiple sources
 * INPUT_SLOT_FLAGS(DISPATCH_PASSES, std::vector<DispatchPass>, 0,
 *     SlotNullability::Required, SlotRole::Dependency,
 *     SlotMutability::ReadOnly, SlotScope::NodeLevel,
 *     SlotFlags::Accumulation | SlotFlags::MultiConnect);
 * ```
 */
#define INPUT_SLOT_FLAGS(SlotName, SlotType, Index, Nullability, Role, Mutability, Scope, Flags) \
    CONSTEXPR_INPUT_FULL_WITH_FLAGS( \
        SlotName, \
        SlotType, \
        Index, \
        Nullability, \
        Role, \
        Mutability, \
        Scope, \
        Flags \
    )

/**
 * @brief Convenience macro for accumulation input slots
 *
 * Pre-configured with Accumulation | MultiConnect flags.
 * Use for slots that gather data from multiple source nodes.
 *
 * IMPORTANT: Accumulation slots are ALWAYS Execute role (never Dependency):
 * - The accumulated vector is rebuilt each frame (reset semantics)
 * - No dependency propagation needed - consumer processes fresh data each cycle
 * - Source changes don't need to trigger target rebuild
 *
 * Result Lifetime: Always Transient - the accumulated vector is ephemeral.
 * Do not cache accumulated data across frames.
 */
#define ACCUMULATION_INPUT_SLOT(SlotName, SlotType, Index, Nullability) \
    INPUT_SLOT_FLAGS( \
        SlotName, \
        SlotType, \
        Index, \
        Nullability, \
        ::Vixen::RenderGraph::SlotRole::Execute, \
        ::Vixen::RenderGraph::SlotMutability::ReadOnly, \
        ::Vixen::RenderGraph::SlotScope::NodeLevel, \
        ::Vixen::RenderGraph::SlotFlags::Accumulation | ::Vixen::RenderGraph::SlotFlags::MultiConnect \
    )

// ====================================================================
// SPRINT 6.0.2: PROPER ACCUMULATION SLOT SYSTEM
// ====================================================================

/**
 * @brief Proper accumulation input slot with container type and storage strategy (Sprint 6.0.2)
 *
 * Declares an accumulation slot using explicit container types (e.g., std::vector<T>)
 * instead of element types. This eliminates the type system lie where slots declare
 * element types but return containers at runtime.
 *
 * IMPORTANT: Accumulation slots are ALWAYS Execute role (never Dependency):
 * - The accumulated vector is rebuilt each frame (reset semantics)
 * - No dependency propagation needed - consumer processes fresh data each cycle
 * - Source changes don't need to trigger target rebuild
 *
 * Result Lifetime: Always Transient - the accumulated vector is ephemeral.
 * Do not cache accumulated data across frames.
 *
 * Parameters:
 * - SlotName: Name of the slot (e.g., PASSES, INPUTS)
 * - ContainerType: Full container type (e.g., std::vector<bool>, std::vector<DispatchPass>)
 * - ElementType: Element type for validation (e.g., bool, DispatchPass)
 * - Index: Slot index
 * - Nullability: SlotNullability::Required or Optional
 * - StorageStrategy: SlotStorageStrategy::Value, Reference, or Span
 *
 * Storage Strategies:
 * - Value: Copies elements into container (safe, warns if >1KB total)
 * - Reference: Stores references (zero-copy, requires Persistent sources)
 * - Span: Stores std::span view (zero-copy, requires Persistent sources)
 *
 * Example:
 * ```cpp
 * // Value strategy (copies booleans)
 * ACCUMULATION_INPUT_SLOT_V2(INPUTS, std::vector<bool>, bool, 1,
 *     SlotNullability::Required,
 *     SlotStorageStrategy::Value);
 *
 * // Reference strategy (requires Persistent sources)
 * ACCUMULATION_INPUT_SLOT_V2(LARGE_BUFFERS, std::vector<VkBuffer>, VkBuffer, 2,
 *     SlotNullability::Required,
 *     SlotStorageStrategy::Reference);
 * ```
 *
 * Compile-time Validations:
 * - Container type must satisfy Iterable concept
 * - Container's iterable_value_t must match ElementType
 * - Reference/Span strategies validate Persistent requirement at connection time
 */
#define ACCUMULATION_INPUT_SLOT_V2(SlotName, ContainerType, ElementType, Index, Nullability, StorageStrategy) \
    using SlotName##_Slot = ::Vixen::RenderGraph::ResourceSlot< \
        ContainerType, \
        Index, \
        Nullability, \
        ::Vixen::RenderGraph::SlotRole::Execute, \
        ::Vixen::RenderGraph::SlotMutability::ReadOnly, \
        ::Vixen::RenderGraph::SlotScope::NodeLevel, \
        ::Vixen::RenderGraph::SlotFlags::Accumulation | ::Vixen::RenderGraph::SlotFlags::MultiConnect, \
        StorageStrategy \
    >; \
    static constexpr SlotName##_Slot SlotName{}; \
    /* Compile-time validation: Container must be iterable */ \
    static_assert(::Vixen::RenderGraph::Iterable<ContainerType>, \
        "Accumulation slot container type must satisfy Iterable concept"); \
    /* Compile-time validation: Container element type must be convertible to declared element type */ \
    /* Note: Uses convertible_to instead of is_same to handle std::vector<bool> proxy references */ \
    static_assert(std::convertible_to<::Vixen::RenderGraph::iterable_value_t<ContainerType>, ElementType>, \
        "Container's element type must be convertible to declared ElementType (handles proxy types like std::vector<bool>)")

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

/* PERSISTENT_INPUT_SLOT and PERSISTENT_OUTPUT_SLOT removed.
 * Use INPUT_SLOT/OUTPUT_SLOT to declare slot metadata and specify the
 * ResourceLifetime in INIT_INPUT_DESC/INIT_OUTPUT_DESC. Validation for
 * persistent lifetimes is performed by SlotValidator which is invoked by
 * the INIT_*_DESC macros.
 */

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
 * @brief Type trait to check if a type can be persistent
 *
 * LIFETIME MODEL (simplified):
 * 1. Persistent: Reference to stable node member or external resource
 *    - Must be pointer/reference type (const T&, T*)
 *    - Lives across frames, address remains valid
 *    - Examples: const std::vector<VkSemaphore>&, VkDevice*, const SwapChainPublicVars&
 *
 * 2. Transient: Temporary value copied through graph
 *    - Value types only (T, not T& or T*)
 *    - Short-lived, recreated each use via std::any
 *    - Examples: VkFormat, uint32_t, frame index
 *
 * Note: Containers (std::vector, etc.) should be Persistent references to avoid copies.
 * Enums and POD types can be Transient values.
 */
template<typename T>
struct CanBePersistent {
    using Decayed = std::remove_cv_t<std::remove_reference_t<T>>;
    static constexpr bool value =
        std::is_pointer_v<Decayed> ||
        std::is_reference_v<T>;
};

template<typename T>
inline constexpr bool CanBePersistent_v = CanBePersistent<T>::value;

/**
 * @brief Universal slot validator
 *
 * This template performs all necessary compile-time validations for a slot.
 * Add new validation rules here as needed without changing the macro API.
 */
template<typename SlotType,
         ResourceLifetime Lifetime = ResourceLifetime::Transient,
         SlotRole Role = SlotRole::Dependency,
         SlotNullability Nullability = SlotNullability::Required,
         SlotMutability Mutability = SlotMutability::ReadWrite>
struct SlotValidator {
    // Rule 1: Persistent slots must use pointer/reference types
    static constexpr bool persistence_check =
        (Lifetime != ResourceLifetime::Persistent) || CanBePersistent_v<SlotType>;

    static_assert(persistence_check,
        "Slot is marked as Persistent but uses a type that cannot be persistent. "
        "Persistent slots must use pointer or reference types, not value types.");

    // Rule 2: Execute role slots work with any lifetime (removed obsolete Static check)

    // Rule 3: ReadOnly slots must have const modifier to prevent data corruption
    // Valid forms: const T, const T&, const T*
    template<typename T>
    struct IsConst {
        using Decayed = std::remove_reference_t<T>;
        using PointeeDecayed = std::remove_pointer_t<Decayed>;

        static constexpr bool value =
            std::is_const_v<Decayed> ||                      // const T or const T& (after removing reference)
            (std::is_pointer_v<Decayed> &&
             std::is_const_v<PointeeDecayed>);                // const T*
    };

    static constexpr bool readonly_check =
        (Mutability != SlotMutability::ReadOnly) || IsConst<SlotType>::value;

    static_assert(readonly_check,
        "Slot is marked as ReadOnly but type is not const-qualified. "
        "ReadOnly slots must use const T, const T&, or const T* to prevent data corruption.");

    // The validation passes if all checks pass
    static constexpr bool is_valid = persistence_check && readonly_check;
};

/**
 * @brief Helper to validate persistent slot during INIT_*_DESC
 *
 * This macro validates that if a slot is marked as Persistent,
 * its type must be able to be persistent (pointer/reference).
 */

// VALIDATE_PERSISTENT_SLOT removed: use INIT_INPUT_DESC / INIT_OUTPUT_DESC which
// invoke SlotValidator to perform lifetime-dependent compile-time checks.

/**
 * @brief Macro to automatically generate all standard validations for a node config
 *
 * Place this at the end of your config struct to automatically validate:
 * - Input/output counts match
 * - Array mode matches
 * - All persistent slots use appropriate types
 *
 * Usage:
 * ```cpp
 * CONSTEXPR_NODE_CONFIG(MyNodeConfig, 2, 1, SlotArrayMode::Single) {
 *     INPUT_SLOT(INPUT1, ...);
 *     INPUT_SLOT(INPUT2, ...);
 *     OUTPUT_SLOT(OUTPUT1, ...);
 *
 *     MyNodeConfig() {
 *         // Initialize descriptors...
 *     }
 *
 *     VALIDATE_NODE_CONFIG(MyNodeConfig, MyNodeCounts);
 * };
 * ```
 */
#define VALIDATE_NODE_CONFIG(ConfigName, CountsNamespace) \
    static_assert(INPUT_COUNT == CountsNamespace::INPUTS, "Input count mismatch"); \
    static_assert(OUTPUT_COUNT == CountsNamespace::OUTPUTS, "Output count mismatch"); \
    static_assert(ARRAY_MODE == CountsNamespace::ARRAY_MODE, "Array mode mismatch")

/**
 * @brief Simplified initialization for common cases with automatic validation
 *
 * These macros now automatically validate slots based on their properties.
 * The validation happens via SlotValidator which can be extended with new rules.
 */
#define INIT_INPUT_DESC(Slot, Name, Lifetime, Desc) \
    do { \
        /* Trigger compile-time validation for this slot */ \
        [[maybe_unused]] constexpr auto validator_##Slot = \
            ::Vixen::RenderGraph::SlotValidator< \
                Slot##_Slot::Type, \
                Lifetime, \
                Slot##_Slot::role, \
                Slot##_Slot::nullability \
            >{}; \
        INIT_SLOT_DESCRIPTOR(inputs, Slot, Name, Lifetime, Desc); \
    } while(0)

#define INIT_OUTPUT_DESC(Slot, Name, Lifetime, Desc) \
    do { \
        /* Trigger compile-time validation for this slot */ \
        [[maybe_unused]] constexpr auto validator_##Slot = \
            ::Vixen::RenderGraph::SlotValidator< \
                Slot##_Slot::Type, \
                Lifetime, \
                Slot##_Slot::role, \
                Slot##_Slot::nullability \
            >{}; \
        INIT_SLOT_DESCRIPTOR(outputs, Slot, Name, Lifetime, Desc); \
    } while(0)

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
