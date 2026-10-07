#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include "InputEvents.h"

namespace Vixen::RenderGraph {

/// One press or release edge, event-derived (input-rework slice 1, critique V4 loss-half): a
/// press+release inside a single frame is TWO entries here, not a collapsed non-edge the way a
/// single per-frame poll would see it. x/y are the cursor position AT that event (fold-order),
/// not the end-of-frame position — fixes click-position skew during fast motion for consumers
/// that hit-test against it.
struct ClickEvent {
    int button = 0;      // EventBus::MouseButton value (0=left, 1=right, 2=middle)
    bool pressed = false;  // true=press edge, false=release edge
    float x = 0.0f;
    float y = 0.0f;
};

/// Per-frame continuous input axes. Mouse-look remains available as pixel deltas in mouseDelta.
enum class InputAxis : uint8_t {
    MoveHorizontal,
    MoveForward,
    MoveUp,
    LookHorizontal,
    LookVertical,
    Count
};

/// One key contribution to a continuous axis. Keep default bindings as data so they can be
/// reviewed and changed without embedding key policy in InputNode's event-folding logic.
struct KeyAxisBinding {
    EventBus::KeyCode key;
    InputAxis axis;
    float value;
};

inline constexpr std::array<KeyAxisBinding, 10> kInputAxisBindings{{
    {EventBus::KeyCode::A,     InputAxis::MoveHorizontal, -1.0f},
    {EventBus::KeyCode::D,     InputAxis::MoveHorizontal,  1.0f},
    {EventBus::KeyCode::S,     InputAxis::MoveForward,    -1.0f},
    {EventBus::KeyCode::W,     InputAxis::MoveForward,     1.0f},
    {EventBus::KeyCode::Q,     InputAxis::MoveUp,         -1.0f},
    {EventBus::KeyCode::E,     InputAxis::MoveUp,          1.0f},
    {EventBus::KeyCode::Left,  InputAxis::LookHorizontal, -1.0f},
    {EventBus::KeyCode::Right, InputAxis::LookHorizontal,  1.0f},
    {EventBus::KeyCode::Down,  InputAxis::LookVertical,    -1.0f},
    {EventBus::KeyCode::Up,    InputAxis::LookVertical,     1.0f},
}};

/**
 * @brief Immediate-mode input state (polled once per frame)
 *
 * Modern input system following GLFW/SDL2 patterns:
 * - Poll hardware state once per frame
 * - No event flooding (hundreds of events → 1 poll)
 * - Predictable timing (always 1 sample per frame)
 * - Efficient (single Win32 API call, not hundreds)
 *
 * Producer is now GLFW callbacks -> a queue -> InputNode::ProcessPendingInput() (input-rework
 * slice 1); this struct stays the poll-shaped consumer contract, populated once per Execute from
 * that drained state so existing readers (CameraNode, the selection providers) are unaffected.
 */
struct InputState {
    // Mouse state (updated once per frame)
    glm::vec2 mouseDelta{0.0f};      // Raw pixel delta this frame (smooth, no jitter)
    glm::vec2 mousePosition{0.0f};   // Current position in window coordinates
    bool mouseButtons[3]{false};     // [0]=left, [1]=right, [2]=middle
    glm::vec2 wheelDelta{0.0f};      // Scroll offsets this frame (x=horizontal, y=vertical)
    std::vector<ClickEvent> clicksThisFrame;  // Every press+release edge since last frame, in order

    // InputConfig fields CameraNode needs (input-rework M4), mirrored in by InputNode::
    // PopulateInputState each frame. CameraNode has no InputNode reference — only this InputState
    // slot — so riding the existing data path here is cheaper than a new graph-slot/lookup type
    // for 4 read-only scalars a single other consumer wants.
    uint8_t orbitButton = 0;      // InputConfig::OrbitButton: 0=RightMouse, 1=LeftDrag, 2=Always
    float dragThresholdPx = 4.0f; // in-press motion below this stays a "click" (LeftDrag mode)
    bool wheelZoom = true;        // scroll drives orbit distance
    float wheelZoomSpeed = 2.0f;  // world units per wheel notch

    // Keyboard state (bitfield for fast queries)
    // Using unordered_map for sparse storage (only tracking keys we care about)
    std::unordered_map<EventBus::KeyCode, bool> keyDown;       // Currently held
    std::unordered_map<EventBus::KeyCode, bool> keyPressed;    // Just pressed this frame
    std::unordered_map<EventBus::KeyCode, bool> keyReleased;   // Just released this frame

    // Continuous key axes, recomputed from keyDown once per frame. Mouse look uses mouseDelta.
    std::array<float, static_cast<std::size_t>(InputAxis::Count)> axes{};

    // Debug visualization mode (0=normal, 1-9=debug modes)
    // Updated by pressing number keys 0-9
    int32_t debugMode = 0;

    // TEMP DEBUG: pixel of the most recent left-click press, for the ray-trace debug buffer
    // (TraceRecording.glsl's shouldCaptureDebug) to force-capture that exact ray's traversal —
    // NOT cleared per-frame like clicksThisFrame (persists until the next click) since the trace
    // export happens on a later frame's DebugBufferReaderNode::ExecuteImpl, after the click frame.
    // (-1,-1) = no click yet this session.
    glm::ivec2 lastClickPixel{-1, -1};

    // Frame timing (for framerate-independent input)
    float deltaTime = 0.0f;  // Seconds since last frame

    /**
     * @brief Clear per-frame state (key edges, axes, mouse delta, click/wheel edge data)
     * Call at the start of each frame before polling
     *
     * InputNode repopulates axes from held keys and mouseDelta from drained cursor events after
     * BeginFrame. Clearing both here also makes a disabled node output a zero-valued frame.
     *
     * clicksThisFrame and wheelDelta are also per-frame data and must be cleared here — otherwise
     * a disabled InputNode's early-return in ExecuteImpl (`!enabled_`) re-outputs stale click/wheel
     * values instead of an empty frame.
     */
    void BeginFrame() {
        keyPressed.clear();
        keyReleased.clear();
        axes.fill(0.0f);
        mouseDelta = glm::vec2(0.0f);
        clicksThisFrame.clear();
        wheelDelta = glm::vec2(0.0f);
    }

    /**
     * @brief Query if a key is currently held down
     */
    bool IsKeyDown(EventBus::KeyCode key) const {
        auto it = keyDown.find(key);
        return it != keyDown.end() && it->second;
    }

    /**
     * @brief Query if a key was just pressed this frame
     */
    bool IsKeyPressed(EventBus::KeyCode key) const {
        auto it = keyPressed.find(key);
        return it != keyPressed.end() && it->second;
    }

    /**
     * @brief Query if a key was just released this frame
     */
    bool IsKeyReleased(EventBus::KeyCode key) const {
        auto it = keyReleased.find(key);
        return it != keyReleased.end() && it->second;
    }

    /// Get a continuous axis value. Invalid axis values return zero.
    float GetAxis(InputAxis axis) const {
        const std::size_t index = static_cast<std::size_t>(axis);
        return index < axes.size() ? axes[index] : 0.0f;
    }

    /// Rebuild continuous axes from keyDown using the declarative binding table.
    void UpdateAxesFromKeyState() {
        axes.fill(0.0f);
        for (const KeyAxisBinding& binding : kInputAxisBindings) {
            if (IsKeyDown(binding.key)) {
                axes[static_cast<size_t>(binding.axis)] += binding.value;
            }
        }
        for (float& value : axes) {
            value = std::clamp(value, -1.0f, 1.0f);
        }
    }

    /**
     * @brief Get horizontal axis value (-1 = left/A, +1 = right/D)
     */
    float GetAxisHorizontal() const {
        return GetAxis(InputAxis::MoveHorizontal);
    }

    /**
     * @brief Get vertical axis value (-1 = backward/S, +1 = forward/W)
     */
    float GetAxisVertical() const {
        return GetAxis(InputAxis::MoveForward);
    }

    /**
     * @brief Get vertical movement axis (Q/E for up/down)
     */
    float GetAxisUpDown() const {
        return GetAxis(InputAxis::MoveUp);
    }

    /**
     * @brief Get look horizontal axis (Arrow Left/Right for yaw rotation)
     * Returns -1 = look left, +1 = look right
     */
    float GetAxisLookHorizontal() const {
        return GetAxis(InputAxis::LookHorizontal);
    }

    /**
     * @brief Get look vertical axis (Arrow Up/Down for pitch rotation)
     * Returns -1 = look down, +1 = look up
     */
    float GetAxisLookVertical() const {
        return GetAxis(InputAxis::LookVertical);
    }
};

// Pointer type for passing InputState through render graph
using InputStatePtr = InputState*;

} // namespace Vixen::RenderGraph
