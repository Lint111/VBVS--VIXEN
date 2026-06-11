#pragma once

/**
 * @file SlotFields.h
 * @brief SINGLE SOURCE OF TRUTH for slot field definitions
 *
 * This file uses X-macros to define slot fields exactly ONCE.
 * Both compile-time (ResourceSlot) and runtime (SlotInfo) representations
 * are generated from this single definition.
 *
 * To add a new slot field:
 * 1. Add it to SLOT_CORE_FIELDS or SLOT_EXTENDED_FIELDS below
 * 2. Both ResourceSlot and SlotInfo will automatically get it
 * 3. The FromSlot<T>() factory will automatically copy it
 *
 * Field categories:
 * - SLOT_CORE_FIELDS: Essential slot metadata (always present)
 * - SLOT_EXTENDED_FIELDS: Sprint 6.0.1+ extensions (flags, etc.)
 * - SLOT_RUNTIME_FIELDS: Runtime-only fields (not in ResourceSlot template)
 */

#include "ResourceTypes.h"
#include "ResourceConfig.h"  // For SlotNullability, SlotRole, SlotMutability, SlotScope, SlotFlags

namespace Vixen::RenderGraph {

// ============================================================================
// SLOT FIELD DEFINITIONS - THE SINGLE SOURCE OF TRUTH
// ============================================================================
//
// Format: X(Type, Name, DefaultValue)
//
// These macros define ALL slot fields. They are expanded differently
// depending on context:
// - In ResourceSlot: static constexpr Type Name = ...
// - In SlotInfo: Type Name = DefaultValue
// - In FromSlot: .Name = SlotType::Name
//

/**
 * @brief Core slot fields - present since Phase F
 *
 * These are the fundamental slot properties.
 */
#define SLOT_CORE_FIELDS(X) \
    X(uint32_t,        index,       0) \
    X(ResourceType,    resourceType, ResourceType::Buffer) \
    X(SlotNullability, nullability, SlotNullability::Required) \
    X(SlotRole,        role,        SlotRole::Dependency) \
    X(SlotMutability,  mutability,  SlotMutability::ReadOnly) \
    X(SlotScope,       scope,       SlotScope::NodeLevel)

/**
 * @brief Extended slot fields - Sprint 6.0.1+
 *
 * These are newer fields for accumulation, multi-connect, etc.
 */
#define SLOT_EXTENDED_FIELDS(X) \
    X(SlotFlags,       flags,       SlotFlags::None)

/**
 * @brief All slot fields combined
 *
 * Use this macro when you need to iterate over ALL slot fields.
 */
#define SLOT_ALL_FIELDS(X) \
    SLOT_CORE_FIELDS(X) \
    SLOT_EXTENDED_FIELDS(X)

// ============================================================================
// EXPANSION MACROS - Used by ResourceSlot and SlotInfo
// ============================================================================

/**
 * @brief Expand to static constexpr member declaration
 *
 * Used in ResourceSlot template to declare compile-time members.
 * Note: The actual value assignment happens in the template, not here.
 */
#define SLOT_DECL_STATIC_MEMBER(Type, Name, Default) \
    static constexpr Type Name = Name##_param;

/**
 * @brief Expand to regular member declaration with default
 *
 * Used in SlotInfo struct to declare runtime members.
 */
#define SLOT_DECL_RUNTIME_MEMBER(Type, Name, Default) \
    Type Name = Default;

/**
 * @brief Expand to field copy from compile-time to runtime
 *
 * Used in SlotInfo::FromSlot<T>() to copy all fields.
 */
#define SLOT_COPY_FROM_STATIC(Type, Name, Default) \
    result.Name = SlotType::Name;

/**
 * @brief Expand to static_assert for sync verification
 *
 * Used to verify ResourceSlot has the expected members.
 */
#define SLOT_VERIFY_MEMBER(Type, Name, Default) \
    static_assert(requires { SlotType::Name; }, \
        "ResourceSlot must have member: " #Name);

/**
 * @brief Expand to type verification
 *
 * Used to verify member types match between definitions.
 */
#define SLOT_VERIFY_TYPE(Type, Name, Default) \
    static_assert(std::is_same_v<std::remove_cv_t<decltype(SlotType::Name)>, Type>, \
        "Type mismatch for member: " #Name);

} // namespace Vixen::RenderGraph
