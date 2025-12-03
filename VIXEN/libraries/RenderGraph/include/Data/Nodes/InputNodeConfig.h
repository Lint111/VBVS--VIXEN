#pragma once

#include "Data/Core/ResourceConfig.h"
#include "Data/InputState.h"

namespace Vixen::RenderGraph {

// Compile-time slot counts
namespace InputNodeCounts {
    static constexpr size_t INPUTS = 1;   // HWND
    static constexpr size_t OUTPUTS = 1;  // InputState pointer (modern polling interface)
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Pure constexpr resource configuration for InputNode
 *
 * Modern polling-based input system (GLFW/SDL2 style):
 * - Polls Win32 state once per frame (no event flooding)
 * - Outputs InputState* for immediate-mode queries
 * - Still publishes legacy events for compatibility
 *
 * Inputs: 1
 *   - HWND (::HWND) - Windows window handle for input polling
 * Outputs: 1
 *   - INPUT_STATE (InputStatePtr) - Polling interface for camera/gameplay
 * Parameters: None
 */
CONSTEXPR_NODE_CONFIG(InputNodeConfig,
                      InputNodeCounts::INPUTS,
                      InputNodeCounts::OUTPUTS,
                      InputNodeCounts::ARRAY_MODE) {
    // Input: HWND for Win32 input polling
    INPUT_SLOT(HWND_IN, ::HWND, 0,
        SlotNullability::Required,
        SlotRole::Execute,  // Need HWND every frame for polling
        SlotMutability::ReadOnly,
        SlotScope::NodeLevel);

    // Output: InputState pointer for polling interface
    OUTPUT_SLOT(INPUT_STATE, InputStatePtr, 0,
        SlotNullability::Required,
        SlotMutability::WriteOnly);

    // Constructor for runtime descriptor initialization
    InputNodeConfig() {
        // HWND handle input
        HandleDescriptor hwndDesc{"HWND"};
        INIT_INPUT_DESC(HWND_IN, "hwnd", ResourceLifetime::Persistent, hwndDesc);

        // InputState pointer output (Persistent: pointer is stable, internal state changes each frame)
        // Using Persistent because member field extraction requires stable memory addresses
        HandleDescriptor inputStateDesc{"InputState*"};
        INIT_OUTPUT_DESC(INPUT_STATE, "input_state", ResourceLifetime::Persistent, inputStateDesc);
    }

    // Compile-time validation
    static_assert(HWND_IN_Slot::index == 0, "HWND_IN must be at index 0");
    static_assert(!HWND_IN_Slot::nullable, "HWND_IN must not be nullable");
    static_assert(std::is_same_v<HWND_IN_Slot::Type, ::HWND>, "HWND_IN must be ::HWND");

    static_assert(INPUT_STATE_Slot::index == 0, "INPUT_STATE must be at index 0");
    static_assert(!INPUT_STATE_Slot::nullable, "INPUT_STATE must not be nullable");
    static_assert(std::is_same_v<INPUT_STATE_Slot::Type, InputStatePtr>, "INPUT_STATE must be InputStatePtr");

    // Automated config validation
    VALIDATE_NODE_CONFIG(InputNodeConfig, InputNodeCounts);
};

} // namespace Vixen::RenderGraph
