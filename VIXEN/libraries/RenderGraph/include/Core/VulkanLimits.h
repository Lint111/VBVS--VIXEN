#pragma once

#include <cstddef>

namespace Vixen::RenderGraph {

/**
 * @file VulkanLimits.h
 * @brief Compile-time constants for Vulkan resource limits
 *
 * These constants define maximum sizes for stack-allocated arrays,
 * replacing heap-allocated std::vector instances with std::array.
 *
 * Benefits:
 * - Zero heap allocations in hot paths
 * - Better cache locality
 * - Compile-time known sizes
 * - Reduced memory fragmentation
 *
 * Stack Usage Considerations:
 * - Total stack usage per frame should stay under 1-2 MB
 * - Use StackTracker (debug builds) to monitor actual usage
 * - Conservative estimates based on Vulkan spec limits
 */

// ============================================================================
// FRAME SYNCHRONIZATION
// ============================================================================

/** Maximum frames that can be in-flight simultaneously */
constexpr size_t MAX_FRAMES_IN_FLIGHT = 3;

/** Maximum swapchain images (spec minimum: 2, typical: 2-3, conservative: 4) */
constexpr size_t MAX_SWAPCHAIN_IMAGES = 4;

// ============================================================================
// SHADER PIPELINE LIMITS
// ============================================================================

/** Maximum shader stages in a graphics pipeline
 * (vertex, tessellation control, tessellation evaluation, geometry, fragment, task, mesh) */
constexpr size_t MAX_SHADER_STAGES = 8;

/** Maximum push constant ranges per pipeline (spec minimum: 32 bytes per range) */
constexpr size_t MAX_PUSH_CONSTANT_RANGES = 4;

/** Maximum descriptor bindings per set (conservative, spec varies by device) */
constexpr size_t MAX_DESCRIPTOR_BINDINGS = 32;

/** Maximum descriptor sets per pipeline layout */
constexpr size_t MAX_DESCRIPTOR_SETS = 4;

/** Maximum vertex input attributes (spec minimum: 16) */
constexpr size_t MAX_VERTEX_ATTRIBUTES = 16;

/** Maximum vertex input bindings (spec minimum: 16) */
constexpr size_t MAX_VERTEX_BINDINGS = 16;

// ============================================================================
// FRAMEBUFFER LIMITS
// ============================================================================

/** Maximum color attachments per framebuffer (spec minimum: 4, typical: 8) */
constexpr size_t MAX_FRAMEBUFFER_COLOR_ATTACHMENTS = 8;

/** Maximum total attachments per framebuffer (color + depth/stencil) */
constexpr size_t MAX_FRAMEBUFFER_ATTACHMENTS = 9;

// ============================================================================
// DEVICE LIMITS
// ============================================================================

/** Maximum physical devices (GPUs) to enumerate */
constexpr size_t MAX_PHYSICAL_DEVICES = 8;

/** Maximum device extensions to enable */
constexpr size_t MAX_DEVICE_EXTENSIONS = 64;

/** Maximum validation layers to enable */
constexpr size_t MAX_VALIDATION_LAYERS = 16;

/** Maximum queue families per device */
constexpr size_t MAX_QUEUE_FAMILIES = 8;

// ============================================================================
// EVENT SYSTEM LIMITS
// ============================================================================

/** Maximum window events to process per frame */
constexpr size_t MAX_WINDOW_EVENTS_PER_FRAME = 64;

/** Maximum event subscriptions per node */
constexpr size_t MAX_EVENT_SUBSCRIPTIONS = 16;

// ============================================================================
// COMMAND BUFFER LIMITS
// ============================================================================

/** Maximum command buffers per command pool (per frame) */
constexpr size_t MAX_COMMAND_BUFFERS_PER_FRAME = 16;

/** Maximum secondary command buffers for parallel recording */
constexpr size_t MAX_SECONDARY_COMMAND_BUFFERS = 8;

// ============================================================================
// DESCRIPTOR POOL LIMITS
// ============================================================================

/** Maximum descriptor pool sizes to specify */
constexpr size_t MAX_DESCRIPTOR_POOL_SIZES = 11; // One per VkDescriptorType

// ============================================================================
// STACK ALLOCATION SAFETY
// ============================================================================

/**
 * @brief Estimated maximum stack usage per frame (bytes)
 *
 * Conservative estimate based on MAX_* constants above:
 * - Frame sync: ~1 KB (frames, semaphores)
 * - Descriptor writes: ~4 KB (32 bindings × multiple info structs)
 * - Pipeline creation: ~2 KB (shader stages, vertex attributes)
 * - Command buffers: ~256 bytes
 * - Event processing: ~2 KB (64 events)
 * - Misc buffers: ~2 KB
 *
 * Total: ~11 KB per frame (well under 1 MB safe limit)
 *
 * Use StackTracker in debug builds to validate actual usage.
 */
constexpr size_t ESTIMATED_MAX_STACK_PER_FRAME = 11 * 1024;

/**
 * @brief Warning threshold for stack usage (bytes)
 *
 * Trigger warning when cumulative stack usage exceeds this threshold.
 * Default: 512 KB (conservative, typical stack size is 1-8 MB)
 */
constexpr size_t STACK_WARNING_THRESHOLD = 512 * 1024;

/**
 * @brief Critical threshold for stack usage (bytes)
 *
 * Trigger error when cumulative stack usage exceeds this threshold.
 * Default: 1 MB (absolute safety limit)
 */
constexpr size_t STACK_CRITICAL_THRESHOLD = 1024 * 1024;

} // namespace Vixen::RenderGraph
