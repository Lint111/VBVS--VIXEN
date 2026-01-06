#pragma once

/**
 * @file GroupKeyModifier.h
 * @brief Modifier for extracting group keys from accumulation slot elements
 *
 * Sprint 6.1: Group-based dispatch for MultiDispatchNode
 */

#include "Connection/ConnectionModifier.h"
#include <functional>
#include <optional>

namespace Vixen::RenderGraph {

/**
 * @brief Modifier for partitioning accumulation data by group key
 *
 * Extracts a group ID from each element in an accumulation slot, enabling
 * MultiDispatchNode to partition and process data per-group.
 *
 * Example usage:
 * ```cpp
 * // Partition DispatchPass elements by their groupId field
 * batch.Connect(passGenerator, PassGenConfig::DISPATCH_PASS,
 *               multiDispatch, MultiDispatchNodeConfig::GROUP_INPUTS,
 *               GroupKey(&DispatchPass::groupId));
 * ```
 *
 * Sprint 6.1 Context:
 * - Used by MultiDispatchNode to build group index during Compile
 * - Group ID determines which dispatch passes belong to which group
 * - Groups are processed independently (parallel or sequential dispatch)
 *
 * Lifecycle:
 * - PreValidation: Verifies target is accumulation slot
 * - PreResolve: Stores group key extractor in connection metadata
 * - PostResolve: No-op (extraction happens at runtime during Compile)
 */
class GroupKeyModifier : public ConnectionModifier {
public:
    /**
     * @brief Construct from member pointer (optional<uint32_t>)
     *
     * This constructor handles member pointers to std::optional<uint32_t>.
     * The extractor function will return std::nullopt if the field is not set,
     * allowing elements without group IDs to be treated specially.
     *
     * @tparam StructType The type containing the group ID field
     * @param memberPtr Pointer-to-member for the optional<uint32_t> field
     */
    template<typename StructType>
    explicit GroupKeyModifier(std::optional<uint32_t> StructType::* memberPtr)
        : fieldOffset_(CalculateOffset(memberPtr))
        , extractsOptional_(true)
    {
        // Extractor for optional fields
        keyExtractor_ = [memberPtr](const void* elem) -> std::optional<uint32_t> {
            const auto* typedElem = static_cast<const StructType*>(elem);
            return typedElem->*memberPtr;  // Returns std::optional<uint32_t>
        };
    }

    /**
     * @brief Construct from member pointer (uint32_t)
     *
     * This constructor handles member pointers to plain uint32_t.
     * The extractor function always returns a valid group ID.
     *
     * @tparam StructType The type containing the group ID field
     * @param memberPtr Pointer-to-member for the uint32_t field
     */
    template<typename StructType>
    explicit GroupKeyModifier(uint32_t StructType::* memberPtr)
        : fieldOffset_(CalculateOffset(memberPtr))
        , extractsOptional_(false)
    {
        // Extractor for non-optional fields
        keyExtractor_ = [memberPtr](const void* elem) -> std::optional<uint32_t> {
            const auto* typedElem = static_cast<const StructType*>(elem);
            return typedElem->*memberPtr;  // Implicitly wraps in optional
        };
    }

    /**
     * @brief PreValidation: Verify target is accumulation slot
     *
     * Group keys only make sense for accumulation slots where multiple
     * elements are collected and need to be partitioned.
     */
    [[nodiscard]] ConnectionResult PreValidation(ConnectionContext& ctx) override {
        // Verify target slot is an accumulation slot
        if (!ctx.targetSlot.isAccumulation) {
            return ConnectionResult::Error(
                "GroupKeyModifier requires an accumulation slot target. "
                "Target slot must have SlotFlags::Accumulation set."
            );
        }

        // Store extractor in context for runtime use
        ctx.metadata["groupKeyExtractor"] = keyExtractor_;
        ctx.metadata["groupKeyExtractsOptional"] = extractsOptional_;
        ctx.metadata["groupKeyFieldOffset"] = fieldOffset_;

        return ConnectionResult::Success();
    }

    /**
     * @brief PreResolve: No-op (metadata already stored in PreValidation)
     */
    ConnectionResult PreResolve(ConnectionContext& ctx) override {
        return ConnectionResult::Success();
    }

    /**
     * @brief Priority: Run after field extraction but before type validation
     *
     * Priority 60 ensures group key extraction happens after struct field
     * extraction (priority 75) but before connection validation (priority 50).
     */
    [[nodiscard]] uint32_t Priority() const override { return 60; }

    [[nodiscard]] std::string_view Name() const override { return "GroupKeyModifier"; }

    // Accessors for testing
    [[nodiscard]] size_t GetFieldOffset() const { return fieldOffset_; }
    [[nodiscard]] bool ExtractsOptional() const { return extractsOptional_; }

private:
    /**
     * @brief Calculate field offset from member pointer
     *
     * @tparam StructType The struct type
     * @tparam FieldType The field type (uint32_t or optional<uint32_t>)
     * @param memberPtr Pointer-to-member
     * @return Byte offset of field within struct
     */
    template<typename StructType, typename FieldType>
    static size_t CalculateOffset(FieldType StructType::* memberPtr) {
        return reinterpret_cast<size_t>(
            &(static_cast<StructType*>(nullptr)->*memberPtr));
    }

    // Function to extract group ID from element (stored in ConnectionContext metadata)
    std::function<std::optional<uint32_t>(const void*)> keyExtractor_;

    // Field offset for debugging
    size_t fieldOffset_;

    // Whether the extractor returns optional (true) or always valid (false)
    bool extractsOptional_;
};

// ============================================================================
// HELPER FUNCTION
// ============================================================================

/**
 * @brief Create GroupKeyModifier from member pointer
 *
 * Convenience function that deduces template types from member pointer.
 *
 * Usage:
 * @code
 * // For optional<uint32_t> field:
 * batch.Connect(src, SrcConfig::OUT, tgt, TgtConfig::IN,
 *               GroupKey(&MyStruct::groupId));
 *
 * // For plain uint32_t field:
 * batch.Connect(src, SrcConfig::OUT, tgt, TgtConfig::IN,
 *               GroupKey(&MyStruct::id));
 * @endcode
 *
 * @tparam StructType The struct type containing the field
 * @tparam FieldType The field type (uint32_t or optional<uint32_t>)
 * @param memberPtr Pointer-to-member for the group ID field
 * @return unique_ptr to configured GroupKeyModifier
 */
template<typename StructType, typename FieldType>
std::unique_ptr<GroupKeyModifier> GroupKey(FieldType StructType::* memberPtr) {
    return std::make_unique<GroupKeyModifier>(memberPtr);
}

} // namespace Vixen::RenderGraph
