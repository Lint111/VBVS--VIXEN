#pragma once

#include <glm/glm.hpp>
#include <unordered_map>
#include "InputEvents.h"

namespace Vixen::RenderGraph {

/**
 * @brief Immediate-mode input state (polled once per frame)
 *
 * Modern input system following GLFW/SDL2 patterns:
 * - Poll hardware state once per frame
 * - No event flooding (hundreds of events → 1 poll)
 * - Predictable timing (always 1 sample per frame)
 * - Efficient (single Win32 API call, not hundreds)
 */
struct InputState {
    // Mouse state (updated once per frame)
    glm::vec2 mouseDelta{0.0f};      // Pixel delta this frame (smooth, no jitter)
    glm::vec2 mousePosition{0.0f};   // Current position in window coordinates
    bool mouseButtons[3]{false};     // [0]=left, [1]=right, [2]=middle

    // Keyboard state (bitfield for fast queries)
    // Using unordered_map for sparse storage (only tracking keys we care about)
    std::unordered_map<EventBus::KeyCode, bool> keyDown;       // Currently held
    std::unordered_map<EventBus::KeyCode, bool> keyPressed;    // Just pressed this frame
    std::unordered_map<EventBus::KeyCode, bool> keyReleased;   // Just released this frame

    // Frame timing (for framerate-independent input)
    float deltaTime = 0.0f;  // Seconds since last frame

    /**
     * @brief Clear per-frame state (pressed/released flags)
     * Call at the start of each frame before polling
     *
     * NOTE: mouseDelta is NOT cleared here because it's calculated by PollMouse()
     * after polling but before BeginFrame() is called. Clearing would lose the frame's delta.
     * The delta is used by consumers (CameraNode) and should persist until the next frame.
     */
    void BeginFrame() {
        keyPressed.clear();
        keyReleased.clear();
        // NOTE: mouseDelta is preserved (calculated in PollMouse, not cleared here)
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

    /**
     * @brief Get horizontal axis value (-1 = left/A, +1 = right/D)
     */
    float GetAxisHorizontal() const {
        float value = 0.0f;
        if (IsKeyDown(EventBus::KeyCode::A)) value -= 1.0f;
        if (IsKeyDown(EventBus::KeyCode::D)) value += 1.0f;
        return value;
    }

    /**
     * @brief Get vertical axis value (-1 = backward/S, +1 = forward/W)
     */
    float GetAxisVertical() const {
        float value = 0.0f;
        if (IsKeyDown(EventBus::KeyCode::S)) value -= 1.0f;
        if (IsKeyDown(EventBus::KeyCode::W)) value += 1.0f;
        return value;
    }

    /**
     * @brief Get vertical movement axis (Q/E for up/down)
     */
    float GetAxisUpDown() const {
        float value = 0.0f;
        if (IsKeyDown(EventBus::KeyCode::Q)) value -= 1.0f;
        if (IsKeyDown(EventBus::KeyCode::E)) value += 1.0f;
        return value;
    }

    /**
     * @brief Get look horizontal axis (Arrow Left/Right for yaw rotation)
     * Returns -1 = look left, +1 = look right
     */
    float GetAxisLookHorizontal() const {
        float value = 0.0f;
        if (IsKeyDown(EventBus::KeyCode::Left)) value -= 1.0f;
        if (IsKeyDown(EventBus::KeyCode::Right)) value += 1.0f;
        return value;
    }

    /**
     * @brief Get look vertical axis (Arrow Up/Down for pitch rotation)
     * Returns -1 = look down, +1 = look up
     */
    float GetAxisLookVertical() const {
        float value = 0.0f;
        if (IsKeyDown(EventBus::KeyCode::Down)) value -= 1.0f;
        if (IsKeyDown(EventBus::KeyCode::Up)) value += 1.0f;
        return value;
    }
};

// Pointer type for passing InputState through render graph
using InputStatePtr = InputState*;

} // namespace Vixen::RenderGraph
