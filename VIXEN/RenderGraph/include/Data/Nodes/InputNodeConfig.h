#pragma once

#include "Data/Core/ResourceConfig.h"

namespace Vixen::RenderGraph {

// Compile-time slot counts
namespace InputNodeCounts {
    static constexpr size_t INPUTS = 1;   // HWND
    static constexpr size_t OUTPUTS = 0;  // No outputs (publishes to EventBus)
    static constexpr SlotArrayMode ARRAY_MODE = SlotArrayMode::Single;
}

/**
 * @brief Pure constexpr resource configuration for InputNode
 *
 * Polls Win32 keyboard/mouse input and publishes events to EventBus.
 * Uses per-frame state tracking (not sub-frame).
 *
 * Inputs: 1
 *   - HWND (::HWND) - Windows window handle for input polling
 * Outputs: None (publishes to EventBus)
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

    // Constructor for runtime descriptor initialization
    InputNodeConfig() {
        // HWND handle input
        HandleDescriptor hwndDesc{"HWND"};
        INIT_INPUT_DESC(HWND_IN, "hwnd", ResourceLifetime::Persistent, hwndDesc);
    }

    // Compile-time validation
    static_assert(HWND_IN_Slot::index == 0, "HWND_IN must be at index 0");
    static_assert(!HWND_IN_Slot::nullable, "HWND_IN must not be nullable");
    static_assert(std::is_same_v<HWND_IN_Slot::Type, ::HWND>, "HWND_IN must be ::HWND");

    static_assert(INPUT_COUNT == InputNodeCounts::INPUTS, "Input count mismatch");
    static_assert(OUTPUT_COUNT == InputNodeCounts::OUTPUTS, "Output count mismatch");
    static_assert(ARRAY_MODE == InputNodeCounts::ARRAY_MODE, "Array mode mismatch");
};

// Compile-time verification
static_assert(InputNodeConfig::INPUT_COUNT == InputNodeCounts::INPUTS,
              "InputNode input count validation");
static_assert(InputNodeConfig::OUTPUT_COUNT == InputNodeCounts::OUTPUTS,
              "InputNode output count validation");

} // namespace Vixen::RenderGraph
