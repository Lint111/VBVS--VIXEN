#pragma once

/**
 * @file SlotInfo.h
 * @brief Unified runtime slot representation
 *
 * This struct is the SINGLE runtime representation for all slot types:
 * - Static input/output slots (from ResourceSlot template)
 * - Variadic/binding slots (from shader reflection)
 *
 * Fields are defined in SlotFields.h and expanded here using X-macros.
 * This ensures compile-time and runtime definitions stay in sync.
 */

#include "SlotFields.h"
#include "ResourceConfig.h"
#include <vulkan/vulkan.h>
#include <string_view>
#include <functional>
#include <concepts>

namespace Vixen::RenderGraph {

// Forward declarations
class NodeInstance;

/**
 * @brief Slot validation state lifecycle
 *
 * Tracks validation state through the compilation pipeline.
 * (Moved from VariadicTypedNode.h for unification)
 */
enum class SlotState : uint8_t {
    Tentative,    ///< Created during connection, unvalidated
    Validated,    ///< Type-checked during Compile
    Compiled,     ///< Finalized with resources
    Invalid       ///< Validation failed
};

/**
 * @brief Slot kind discriminator
 */
enum class SlotKind : uint8_t {
    StaticInput,   ///< From INPUT_SLOT macro
    StaticOutput,  ///< From OUTPUT_SLOT macro
    Binding        ///< From shader reflection / variadic connection
};

/**
 * @brief Unified runtime slot information
 *
 * Replaces both SlotDescriptor and VariadicSlotInfo with a single type.
 * All fields from SLOT_ALL_FIELDS are automatically included.
 *
 * Construction:
 * - FromSlot<T>() - From compile-time ResourceSlot
 * - FromBinding() - From shader binding reference
 * - Default constructor for containers
 */
struct SlotInfo {
    // ========================================================================
    // CORE FIELDS - Auto-generated from SlotFields.h
    // ========================================================================
    // These fields are defined in SLOT_ALL_FIELDS macro.
    // Adding a field there automatically adds it here.

    SLOT_ALL_FIELDS(SLOT_DECL_RUNTIME_MEMBER)

    // ========================================================================
    // IDENTITY FIELDS - Runtime-only (not in ResourceSlot template)
    // ========================================================================

    std::string_view name;                ///< Debug name for the slot
    SlotKind kind = SlotKind::StaticInput; ///< Discriminator

    // ========================================================================
    // BINDING-SPECIFIC FIELDS (for SlotKind::Binding)
    // ========================================================================

    uint32_t binding = UINT32_MAX;        ///< Shader binding index
    VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_MAX_ENUM;

    // ========================================================================
    // CONNECTION TRACKING
    // ========================================================================

    SlotState state = SlotState::Tentative;
    NodeInstance* sourceNode = nullptr;   ///< Source node for tracking
    uint32_t sourceOutput = 0;            ///< Source output slot index

    // ========================================================================
    // FIELD EXTRACTION (for member pointer connections)
    // ========================================================================

    size_t fieldOffset = 0;               ///< Offset in source struct
    size_t fieldSize = 0;                 ///< Size of extracted field
    bool hasFieldExtraction = false;      ///< Whether extraction is enabled
    std::function<void*(void*)> extractor; ///< Type-erased extraction function

    // ========================================================================
    // HELPER ACCESSORS
    // ========================================================================

    [[nodiscard]] constexpr bool IsAccumulation() const {
        return HasAccumulation(flags);
    }

    [[nodiscard]] constexpr bool IsMultiConnect() const {
        return HasMultiConnect(flags);
    }

    [[nodiscard]] constexpr bool RequiresExplicitOrder() const {
        return HasExplicitOrder(flags);
    }

    [[nodiscard]] constexpr bool IsOptional() const {
        return nullability == SlotNullability::Optional;
    }

    [[nodiscard]] constexpr bool IsInput() const {
        return kind == SlotKind::StaticInput || kind == SlotKind::Binding;
    }

    [[nodiscard]] constexpr bool IsOutput() const {
        return kind == SlotKind::StaticOutput;
    }

    [[nodiscard]] constexpr bool IsBinding() const {
        return kind == SlotKind::Binding;
    }

    [[nodiscard]] constexpr bool IsStatic() const {
        return kind == SlotKind::StaticInput || kind == SlotKind::StaticOutput;
    }

    // ========================================================================
    // FACTORY: From compile-time ResourceSlot
    // ========================================================================

    /**
     * @brief Create SlotInfo from compile-time ResourceSlot
     *
     * Uses SLOT_ALL_FIELDS to automatically copy all fields.
     * If ResourceSlot adds a new field and SlotFields.h is updated,
     * this factory automatically handles it.
     */
    template<typename SlotType>
        requires requires {
            { SlotType::index } -> std::convertible_to<uint32_t>;
            { SlotType::role } -> std::convertible_to<SlotRole>;
        }
    static SlotInfo FromSlot(std::string_view slotName, SlotKind slotKind) {
        // Verify all expected fields exist at compile time
        SLOT_ALL_FIELDS(SLOT_VERIFY_MEMBER)

        SlotInfo result{};

        // Copy all fields from compile-time to runtime
        SLOT_ALL_FIELDS(SLOT_COPY_FROM_STATIC)

        result.name = slotName;
        result.kind = slotKind;
        result.state = SlotState::Compiled;  // Static slots are pre-validated

        return result;
    }

    /**
     * @brief Convenience for input slots
     */
    template<typename SlotType>
    static SlotInfo FromInputSlot(std::string_view slotName) {
        return FromSlot<SlotType>(slotName, SlotKind::StaticInput);
    }

    /**
     * @brief Convenience for output slots
     */
    template<typename SlotType>
    static SlotInfo FromOutputSlot(std::string_view slotName) {
        return FromSlot<SlotType>(slotName, SlotKind::StaticOutput);
    }

    // ========================================================================
    // FACTORY: From binding reference
    // ========================================================================

    /**
     * @brief Create SlotInfo from shader binding reference (instance members)
     *
     * For variadic connections where target is a shader-discovered binding.
     * Handles types with lowercase instance members (binding, descriptorType).
     */
    template<typename BindingRefType>
        requires requires(BindingRefType b) {
            { b.binding } -> std::convertible_to<uint32_t>;
            { b.descriptorType } -> std::convertible_to<uint32_t>;
        }
    static SlotInfo FromBinding(const BindingRefType& ref, std::string_view bindingName = "") {
        SlotInfo result{};

        result.binding = ref.binding;
        result.descriptorType = static_cast<VkDescriptorType>(ref.descriptorType);
        result.name = bindingName;
        result.kind = SlotKind::Binding;
        result.state = SlotState::Tentative;  // Bindings need validation

        // Default behavioral fields for bindings
        result.role = SlotRole::Dependency;
        result.nullability = SlotNullability::Required;
        result.mutability = SlotMutability::ReadOnly;
        result.scope = SlotScope::NodeLevel;
        result.flags = SlotFlags::None;

        return result;
    }

    /**
     * @brief Create SlotInfo from SDI-style binding type (static members)
     *
     * Sprint 6.0.1: For SDI-generated shader binding types with uppercase
     * static constexpr members (BINDING, DESCRIPTOR_TYPE).
     *
     * Usage:
     * @code
     * SlotInfo info = SlotInfo::FromBindingType<VoxelRayMarch::esvoNodes>("esvoNodes");
     * @endcode
     */
    template<typename BindingType>
        requires requires {
            { BindingType::BINDING } -> std::convertible_to<uint32_t>;
            { BindingType::DESCRIPTOR_TYPE } -> std::convertible_to<uint32_t>;
        }
    static SlotInfo FromBindingType(std::string_view bindingName = "") {
        SlotInfo result{};

        result.binding = BindingType::BINDING;
        result.descriptorType = static_cast<VkDescriptorType>(BindingType::DESCRIPTOR_TYPE);
        result.name = bindingName;
        result.kind = SlotKind::Binding;
        result.state = SlotState::Tentative;  // Bindings need validation

        // Default behavioral fields for bindings
        result.role = SlotRole::Dependency;
        result.nullability = SlotNullability::Required;
        result.mutability = SlotMutability::ReadOnly;
        result.scope = SlotScope::NodeLevel;
        result.flags = SlotFlags::None;

        return result;
    }

    // ========================================================================
    // FACTORY: From field extraction
    // ========================================================================

    /**
     * @brief Add field extraction to existing SlotInfo
     */
    template<typename StructT, typename FieldT>
    SlotInfo& WithFieldExtraction(FieldT StructT::* memberPtr) {
        fieldOffset = reinterpret_cast<size_t>(
            &(static_cast<StructT*>(nullptr)->*memberPtr));
        fieldSize = sizeof(FieldT);
        hasFieldExtraction = true;
        extractor = [memberPtr](void* structPtr) -> void* {
            auto* typedStruct = static_cast<StructT*>(structPtr);
            return &(typedStruct->*memberPtr);
        };

        // Update resource type to the extracted field's type
        resourceType = ResourceTypeTraits<FieldT>::resourceType;

        return *this;
    }

    // ========================================================================
    // DEFAULT CONSTRUCTOR
    // ========================================================================

    constexpr SlotInfo() = default;
};

// ============================================================================
// BACKWARD COMPATIBILITY ALIASES
// ============================================================================

// SlotDescriptor is now SlotInfo
using SlotDescriptor = SlotInfo;

// VariadicSlotInfo can be migrated to SlotInfo
// (Keep VariadicSlotInfo in VariadicTypedNode.h during transition)

} // namespace Vixen::RenderGraph
