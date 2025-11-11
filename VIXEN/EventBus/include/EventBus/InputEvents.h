#pragma once

#include "Message.h"
#include <cstdint>

namespace Vixen::EventBus {

/**
 * @brief Key codes (using Win32 virtual key codes)
 *
 * Common keys defined here. For complete list see:
 * https://docs.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes
 */
enum class KeyCode : uint32_t {
    // Movement keys
    W = 0x57,  // 'W'
    A = 0x41,  // 'A'
    S = 0x53,  // 'S'
    D = 0x44,  // 'D'
    Q = 0x51,  // 'Q'
    E = 0x45,  // 'E'

    // Special keys
    Space = 0x20,
    Shift = 0x10,
    Ctrl = 0x11,
    Alt = 0x12,
    Escape = 0x1B,

    // Arrow keys
    Left = 0x25,
    Up = 0x26,
    Right = 0x27,
    Down = 0x28,

    // F-keys
    F1 = 0x70,
    F2 = 0x71,
    F3 = 0x72,
    // ... extend as needed
};

/**
 * @brief Mouse button codes
 */
enum class MouseButton : uint8_t {
    Left = 0,
    Right = 1,
    Middle = 2,
    X1 = 3,
    X2 = 4
};

/**
 * @brief Key event types
 */
enum class KeyEventType : uint8_t {
    Pressed,   // Key just went down this frame
    Held,      // Key is down (includes first frame)
    Released,  // Key just went up this frame
    Clicked    // Key was pressed and released within same frame
};

/**
 * @brief Keyboard input event
 *
 * Published by InputNode for each key state change.
 * Duration is time key was held (useful for Held events).
 */
struct KeyEvent : public BaseEventMessage {
    static constexpr MessageType TYPE = AUTO_MESSAGE_TYPE();
    static constexpr EventCategory CATEGORY = EventCategory::ApplicationState;

    KeyCode key;
    KeyEventType eventType;
    float duration;  // Time held in seconds (for Held/Released events)

    // Modifier state
    bool shiftPressed;
    bool ctrlPressed;
    bool altPressed;

    KeyEvent(
        SenderID sender,
        KeyCode keyCode,
        KeyEventType type,
        float durationSeconds = 0.0f,
        bool shift = false,
        bool ctrl = false,
        bool alt = false
    )
        : BaseEventMessage(CATEGORY, TYPE, sender)
        , key(keyCode)
        , eventType(type)
        , duration(durationSeconds)
        , shiftPressed(shift)
        , ctrlPressed(ctrl)
        , altPressed(alt)
    {}
};

/**
 * @brief Mouse movement event
 *
 * Published by InputNode when mouse moves.
 * Contains both absolute position and delta movement.
 */
struct MouseMoveEvent : public BaseEventMessage {
    static constexpr MessageType TYPE = AUTO_MESSAGE_TYPE();
    static constexpr EventCategory CATEGORY = EventCategory::ApplicationState;

    int32_t x;          // Absolute position in window
    int32_t y;
    float deltaX;       // Movement since last frame
    float deltaY;

    MouseMoveEvent(
        SenderID sender,
        int32_t posX,
        int32_t posY,
        float dx,
        float dy
    )
        : BaseEventMessage(CATEGORY, TYPE, sender)
        , x(posX)
        , y(posY)
        , deltaX(dx)
        , deltaY(dy)
    {}
};

/**
 * @brief Mouse button event
 *
 * Published by InputNode for mouse button state changes.
 */
struct MouseButtonEvent : public BaseEventMessage {
    static constexpr MessageType TYPE = AUTO_MESSAGE_TYPE();
    static constexpr EventCategory CATEGORY = EventCategory::ApplicationState;

    MouseButton button;
    KeyEventType eventType;  // Reuse key event types (Pressed/Held/Released/Clicked)
    int32_t x;  // Mouse position when event occurred
    int32_t y;

    MouseButtonEvent(
        SenderID sender,
        MouseButton btn,
        KeyEventType type,
        int32_t posX,
        int32_t posY
    )
        : BaseEventMessage(CATEGORY, TYPE, sender)
        , button(btn)
        , eventType(type)
        , x(posX)
        , y(posY)
    {}
};

/**
 * @brief Mouse scroll wheel event
 */
struct MouseScrollEvent : public BaseEventMessage {
    static constexpr MessageType TYPE = AUTO_MESSAGE_TYPE();
    static constexpr EventCategory CATEGORY = EventCategory::ApplicationState;

    float deltaVertical;    // Scroll up (positive) or down (negative)
    float deltaHorizontal;  // For horizontal scroll wheels

    MouseScrollEvent(
        SenderID sender,
        float vertical,
        float horizontal = 0.0f
    )
        : BaseEventMessage(CATEGORY, TYPE, sender)
        , deltaVertical(vertical)
        , deltaHorizontal(horizontal)
    {}
};

} // namespace Vixen::EventBus
