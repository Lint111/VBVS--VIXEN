#pragma once

#include <cstdint>

namespace ShaderManagement {

/**
 * @brief Dirty flags for tracking shader data changes
 *
 * Used during hot-reload to determine what operations are needed.
 * Enables smart reload decisions:
 * - SPIRV only → Safe hot-swap (just update shader module)
 * - Descriptor layout → May need pipeline rebuild
 * - Vertex inputs → Definitely needs pipeline rebuild
 */
enum class ShaderDirtyFlags : uint32_t {
    None                = 0,

    // SPIRV bytecode changed (but interface identical)
    Spirv               = 1 << 0,   // Safe: Just swap VkShaderModule

    // Descriptor layout changed
    DescriptorSets      = 1 << 1,   // Risky: May need new descriptor sets
    DescriptorBindings  = 1 << 2,   // Risky: Binding numbers/types changed
    DescriptorTypes     = 1 << 3,   // Risky: Descriptor types changed

    // Push constants changed
    PushConstants       = 1 << 4,   // Risky: Push constant layout changed

    // Vertex attributes changed
    VertexInputs        = 1 << 5,   // Critical: Must rebuild pipeline

    // Stage I/O changed
    StageOutputs        = 1 << 6,   // Medium: May affect next stage

    // Specialization constants changed
    SpecConstants       = 1 << 7,   // Medium: May need recompile with new values

    // Struct layouts changed (UBO/SSBO internal layout)
    StructLayouts       = 1 << 8,   // Critical: CPU data no longer matches

    // Metadata only (names, comments, non-functional)
    MetadataOnly        = 1 << 9,   // Safe: No functional changes

    // Convenience combinations
    InterfaceChanged    = DescriptorSets | DescriptorBindings | DescriptorTypes |
                          PushConstants | VertexInputs | StructLayouts,

    SafeHotReload       = Spirv | MetadataOnly,

    RequiresPipelineRebuild = VertexInputs | DescriptorBindings | PushConstants,

    RequiresDataUpdate  = StructLayouts,

    All                 = 0xFFFFFFFF
};

// Bitwise operators
inline constexpr ShaderDirtyFlags operator|(ShaderDirtyFlags a, ShaderDirtyFlags b) {
    return static_cast<ShaderDirtyFlags>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b)
    );
}

inline constexpr ShaderDirtyFlags operator&(ShaderDirtyFlags a, ShaderDirtyFlags b) {
    return static_cast<ShaderDirtyFlags>(
        static_cast<uint32_t>(a) & static_cast<uint32_t>(b)
    );
}

inline constexpr ShaderDirtyFlags operator~(ShaderDirtyFlags a) {
    return static_cast<ShaderDirtyFlags>(~static_cast<uint32_t>(a));
}

inline ShaderDirtyFlags& operator|=(ShaderDirtyFlags& a, ShaderDirtyFlags b) {
    return a = a | b;
}

inline ShaderDirtyFlags& operator&=(ShaderDirtyFlags& a, ShaderDirtyFlags b) {
    return a = a & b;
}

inline constexpr bool HasFlag(ShaderDirtyFlags flags, ShaderDirtyFlags test) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(test)) != 0;
}

/**
 * @brief Hot-reload compatibility level
 */
enum class HotReloadCompatibility {
    FullyCompatible,        // Can hot-swap safely (SPIRV only changed)
    LayoutCompatible,       // Descriptors same, data update may be needed
    PipelineRebuild,        // Must rebuild pipeline (but data compatible)
    Incompatible,           // Breaking changes (full reload required)
    IdenticalInterface      // Nothing changed (no action needed)
};

/**
 * @brief Determine hot-reload compatibility from dirty flags
 */
inline HotReloadCompatibility GetHotReloadCompatibility(ShaderDirtyFlags flags) {
    if (flags == ShaderDirtyFlags::None) {
        return HotReloadCompatibility::IdenticalInterface;
    }

    // Check for breaking changes
    if (HasFlag(flags, ShaderDirtyFlags::StructLayouts) ||
        HasFlag(flags, ShaderDirtyFlags::DescriptorTypes)) {
        return HotReloadCompatibility::Incompatible;
    }

    // Check if pipeline rebuild needed
    if (HasFlag(flags, ShaderDirtyFlags::RequiresPipelineRebuild)) {
        return HotReloadCompatibility::PipelineRebuild;
    }

    // Check if just descriptor layout changed
    if (HasFlag(flags, ShaderDirtyFlags::DescriptorSets) ||
        HasFlag(flags, ShaderDirtyFlags::DescriptorBindings)) {
        return HotReloadCompatibility::LayoutCompatible;
    }

    // Only safe changes (SPIRV, metadata)
    return HotReloadCompatibility::FullyCompatible;
}

/**
 * @brief Get human-readable compatibility description
 */
inline const char* HotReloadCompatibilityName(HotReloadCompatibility compat) {
    switch (compat) {
        case HotReloadCompatibility::FullyCompatible:
            return "FullyCompatible (safe hot-swap)";
        case HotReloadCompatibility::LayoutCompatible:
            return "LayoutCompatible (may need descriptor update)";
        case HotReloadCompatibility::PipelineRebuild:
            return "PipelineRebuild (must rebuild graphics pipeline)";
        case HotReloadCompatibility::Incompatible:
            return "Incompatible (breaking changes - full reload)";
        case HotReloadCompatibility::IdenticalInterface:
            return "Identical (no changes)";
        default:
            return "Unknown";
    }
}

} // namespace ShaderManagement
