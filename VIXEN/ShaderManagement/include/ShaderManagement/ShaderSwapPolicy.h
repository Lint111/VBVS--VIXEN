#pragma once

#include <cstdint>

namespace ShaderManagement {

/**
 * @brief Shader swap policy
 *
 * Controls when newly compiled shaders are swapped into active use.
 * Allows fine-grained control over hot reload timing to minimize stutters.
 */
enum class ShaderSwapPolicy : uint8_t {
    /**
     * Swap immediately when compilation completes.
     * May cause frame stutter if pipeline recreation is expensive.
     */
    Immediate,

    /**
     * Swap at the beginning of the next frame.
     * Minimizes stutter by deferring swap to frame boundary.
     * Recommended for development hot reload.
     */
    NextFrame,

    /**
     * Swap only when application state changes (e.g., entering/exiting play mode).
     * Best for avoiding mid-gameplay disruption.
     * Requires explicit state transition notification.
     */
    OnStateChange,

    /**
     * Never swap automatically - user must call SwapProgram() explicitly.
     * Full manual control over shader updates.
     */
    Manual,
};

/**
 * @brief Shader swap request
 *
 * Tracks a pending shader swap with its timing policy.
 * Created when compilation completes, executed based on policy.
 */
struct ShaderSwapRequest {
    uint32_t programId;
    ShaderSwapPolicy policy;

    // Internal state
    bool isReady = false;         // Set to true when compilation completes
    bool canSwapNow = false;      // Set to true when policy allows swap
};

/**
 * @brief Application state for OnStateChange policy
 */
enum class ApplicationState : uint8_t {
    Editing,    // Editor mode, not running game logic
    Playing,    // Game running
    Paused,     // Game paused
};

} // namespace ShaderManagement