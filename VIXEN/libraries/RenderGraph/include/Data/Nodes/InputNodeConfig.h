#pragma once

#include "Data/Core/ResourceConfig.h"
#include "Data/InputState.h"

struct GLFWwindow;  // cross-platform window handle (GLFW); concrete type only needed in the .cpp

namespace Vixen::RenderGraph {

/**
 * @brief Mouse capture modes for InputNode
 * Controls how the mouse cursor behaves during input polling.
 */
enum class MouseCaptureMode {
    CenterLock,  ///< Mouse locked to window center (FPS-style camera control)
    Free,        ///< Mouse moves freely (GUI/editor mode)
    Disabled     ///< No mouse capture at all (benchmark/headless mode)
};

// Compile-time slot counts
namespace InputNodeCounts {
    static constexpr size_t INPUTS = 1;   // WINDOW
    static constexpr size_t OUTPUTS = 1;  // InputState pointer (modern polling interface)
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Pure constexpr resource configuration for InputNode
 *
 * Modern polling-based input system (GLFW/SDL2 style):
 * - Polls GLFW state once per frame (no event flooding)
 * - Outputs InputState* for immediate-mode queries
 * - Still publishes legacy events for compatibility
 *
 * Inputs: 1
 *   - WINDOW (GLFWwindow*) - cross-platform window handle for input polling
 * Outputs: 1
 *   - INPUT_STATE (InputStatePtr) - Polling interface for camera/gameplay
 * Parameters:
 *   - enabled (bool): Enable/disable input polling (default: true)
 *   - mouse_capture_mode (int): MouseCaptureMode enum value (default: CenterLock)
 */
CONSTEXPR_NODE_CONFIG(InputNodeConfig,
                      InputNodeCounts::INPUTS,
                      InputNodeCounts::OUTPUTS,
                      InputNodeCounts::ARRAY_MODE) {
    // Input: GLFWwindow* for cross-platform input polling
    INPUT_SLOT(WINDOW, GLFWwindow*, 0,
        SlotNullability::Required,
        SlotRole::Execute,  // Need the window every frame for polling
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // Output: InputState pointer for polling interface
    OUTPUT_SLOT(INPUT_STATE, InputStatePtr, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // ===== PARAMETERS =====
    static constexpr const char* PARAM_ENABLED = "enabled";           // bool: Enable input polling
    static constexpr const char* PARAM_MOUSE_CAPTURE_MODE = "mouse_capture_mode";  // int: MouseCaptureMode enum

    // Constructor for runtime descriptor initialization
    InputNodeConfig() {
        // Cross-platform window handle input
        HandleDescriptor windowDesc{"GLFWwindow"};
        INIT_INPUT_DESC(WINDOW, "window", ResourceLifetime::Persistent, windowDesc);

        // InputState pointer output (Persistent: pointer is stable, internal state changes each frame)
        // Using Persistent because member field extraction requires stable memory addresses
        HandleDescriptor inputStateDesc{"InputState*"};
        INIT_OUTPUT_DESC(INPUT_STATE, "input_state", ResourceLifetime::Persistent, inputStateDesc);
    }

    // Compile-time validation
    static_assert(WINDOW_Slot::index == 0, "WINDOW must be at index 0");
    static_assert(!WINDOW_Slot::nullable, "WINDOW must not be nullable");
    static_assert(std::is_same_v<WINDOW_Slot::Type, GLFWwindow*>, "WINDOW must be GLFWwindow*");

    static_assert(INPUT_STATE_Slot::index == 0, "INPUT_STATE must be at index 0");
    static_assert(!INPUT_STATE_Slot::nullable, "INPUT_STATE must not be nullable");
    static_assert(std::is_same_v<INPUT_STATE_Slot::Type, InputStatePtr>, "INPUT_STATE must be InputStatePtr");

    // Automated config validation
    VALIDATE_NODE_CONFIG(InputNodeConfig, InputNodeCounts);
};

} // namespace Vixen::RenderGraph
