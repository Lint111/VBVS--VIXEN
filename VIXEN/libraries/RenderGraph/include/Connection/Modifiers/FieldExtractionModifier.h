#pragma once

/**
 * @file FieldExtractionModifier.h
 * @brief Modifier for extracting fields from struct outputs
 */

#include "Connection/ConnectionModifier.h"

namespace Vixen::RenderGraph {

/**
 * @brief Modifier for extracting fields from struct outputs
 *
 * Enables connecting a specific field from a struct output to a slot
 * that expects that field's type. For example:
 *
 * ```cpp
 * // Source outputs SwapChainPublicVariables struct
 * // Target expects VkImageView
 * // Extract the colorBuffers field
 * pipeline.AddModifier(std::make_unique<FieldExtractionModifier>(
 *     offsetof(SwapChainPublicVariables, colorBuffers),
 *     sizeof(VkImageView),
 *     ResourceType::ImageView
 * ));
 * ```
 *
 * Lifecycle:
 * - PreValidation: Verifies source has Persistent lifetime (stable address)
 * - PreResolve: Sets effective resource type in context for type checking
 * - PostResolve: No-op (field offset already in SlotInfo)
 */
class FieldExtractionModifier : public ConnectionModifier {
public:
    /**
     * @brief Construct field extraction modifier
     *
     * @param fieldOffset Offset of field within source struct (from offsetof)
     * @param fieldSize Size of the extracted field
     * @param fieldType Resource type of the extracted field
     */
    FieldExtractionModifier(size_t fieldOffset, size_t fieldSize, ResourceType fieldType)
        : fieldOffset_(fieldOffset)
        , fieldSize_(fieldSize)
        , fieldType_(fieldType)
    {}

    /**
     * @brief PreValidation: Check source has Persistent lifetime
     *
     * Field extraction requires a stable memory address. Transient resources
     * may be reallocated, invalidating the extracted pointer.
     */
    [[nodiscard]] ConnectionResult PreValidation(const ConnectionContext& ctx) override {
        if (!ctx.IsPersistentSource()) {
            return ConnectionResult::Error(
                "Field extraction requires Persistent lifetime source. "
                "Transient resources may be reallocated between frames.");
        }
        return ConnectionResult::Success();
    }

    /**
     * @brief PreResolve: Set effective resource type for type checking
     *
     * The rule should use the field's type, not the source struct's type,
     * when validating compatibility with the target slot.
     *
     * Also enforces that field extraction metadata on the source slot is
     * effectively write-once: multiple FieldExtractionModifiers on the same
     * connection must agree on offset/size or the connection fails.
     */
    ConnectionResult PreResolve(ConnectionContext& ctx) override {
        // Set the effective type to the extracted field's type
        ctx.SetEffectiveResourceType(fieldType_);

        // Update source slot info with extraction details.
        // If another modifier already set extraction, ensure it is consistent.
        auto& sourceSlot = ctx.sourceSlot;
        if (sourceSlot.hasFieldExtraction) {
            const bool sameOffset = (sourceSlot.fieldOffset == fieldOffset_);
            const bool sameSize = (sourceSlot.fieldSize == fieldSize_);
            if (!sameOffset || !sameSize) {
                return ConnectionResult::Error(
                    "Multiple FieldExtractionModifiers with conflicting field "
                    "offset/size applied to the same connection.");
            }
            // Idempotent: same configuration is allowed; nothing more to do.
            return ConnectionResult::Success();
        }

        sourceSlot.fieldOffset = fieldOffset_;
        sourceSlot.fieldSize = fieldSize_;
        sourceSlot.hasFieldExtraction = true;
        return ConnectionResult::Success();
    }

    [[nodiscard]] uint32_t Priority() const override { return 75; }
    [[nodiscard]] std::string_view Name() const override { return "FieldExtractionModifier"; }

    // Accessors for testing
    [[nodiscard]] size_t GetFieldOffset() const { return fieldOffset_; }
    [[nodiscard]] size_t GetFieldSize() const { return fieldSize_; }
    [[nodiscard]] ResourceType GetFieldType() const { return fieldType_; }

private:
    size_t fieldOffset_;
    size_t fieldSize_;
    ResourceType fieldType_;
};

// ============================================================================
// HELPER FUNCTION
// ============================================================================

/**
 * @brief Create FieldExtractionModifier from member pointer
 *
 * Convenience function that calculates offset and type from member pointer.
 *
 * Example:
 * @code
 * batch.Connect(swapchain, SwapChainConfig::PUBLIC,
 *               gatherer, Shader::output,
 *               ConnectionMeta{}.With(ExtractField(&SwapChainVars::colorBuffer)));
 * @endcode
 *
 * @tparam StructType The struct type containing the field
 * @tparam FieldType The type of the field being extracted
 * @param memberPtr Pointer-to-member for the field
 * @return unique_ptr to configured FieldExtractionModifier
 */
template<typename StructType, typename FieldType>
std::unique_ptr<FieldExtractionModifier> ExtractField(FieldType StructType::* memberPtr) {
    size_t offset = reinterpret_cast<size_t>(
        &(static_cast<StructType*>(nullptr)->*memberPtr));
    return std::make_unique<FieldExtractionModifier>(
        offset,
        sizeof(FieldType),
        ResourceTypeTraits<std::remove_reference_t<FieldType>>::resourceType);
}

} // namespace Vixen::RenderGraph
