#include "Nodes/InputNode.h"
#include "Core/NodeLogging.h"
#include <iostream>

namespace Vixen::RenderGraph {

// ====== InputNodeType ======

std::unique_ptr<NodeInstance> InputNodeType::CreateInstance(const std::string& instanceName) const {
    return std::make_unique<InputNode>(instanceName, const_cast<InputNodeType*>(this));
}

// ====== InputNode ======

InputNode::InputNode(
    const std::string& instanceName,
    NodeType* nodeType
)
    : TypedNode<InputNodeConfig>(instanceName, nodeType)
{
    // Initialize key states for keys we care about
    using EventBus::KeyCode;
    keyStates[KeyCode::W] = KeyState{};
    keyStates[KeyCode::A] = KeyState{};
    keyStates[KeyCode::S] = KeyState{};
    keyStates[KeyCode::D] = KeyState{};
    keyStates[KeyCode::Q] = KeyState{};
    keyStates[KeyCode::E] = KeyState{};
    keyStates[KeyCode::Space] = KeyState{};
    keyStates[KeyCode::Shift] = KeyState{};
    keyStates[KeyCode::Ctrl] = KeyState{};
    keyStates[KeyCode::Alt] = KeyState{};
    keyStates[KeyCode::Escape] = KeyState{};
}

void InputNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_INFO("[InputNode] Setup");
    lastFrameTime = std::chrono::steady_clock::now();
}

void InputNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("[InputNode] Compile");

    // Get HWND from input slot
    hwnd = ctx.In(InputNodeConfig::HWND_IN);
    if (hwnd == nullptr) {
        throw std::runtime_error("[InputNode] HWND input is null");
    }

    NODE_LOG_INFO("[InputNode] HWND received successfully");
}

void InputNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Calculate delta time
    auto now = std::chrono::steady_clock::now();
    deltaTime = std::chrono::duration<float>(now - lastFrameTime).count();
    lastFrameTime = now;

    // Poll input state
    PollKeyboard();
    PollMouse();

    // Publish events based on state changes
    PublishKeyEvents();
    PublishMouseEvents();
}

void InputNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("[InputNode] Cleanup");
    keyStates.clear();
}

// ====== Input Polling ======

bool InputNode::IsKeyDown(EventBus::KeyCode key) const {
    // GetAsyncKeyState returns high-order bit set if key is down
    SHORT state = GetAsyncKeyState(static_cast<int>(key));
    return (state & 0x8000) != 0;
}

bool InputNode::IsShiftPressed() const {
    return IsKeyDown(EventBus::KeyCode::Shift);
}

bool InputNode::IsCtrlPressed() const {
    return IsKeyDown(EventBus::KeyCode::Ctrl);
}

bool InputNode::IsAltPressed() const {
    return IsKeyDown(EventBus::KeyCode::Alt);
}

void InputNode::PollKeyboard() {
    // Update all tracked keys
    for (auto& [key, state] : keyStates) {
        state.wasDown = state.isDown;
        state.isDown = IsKeyDown(key);

        // Track press time for duration calculation
        if (!state.wasDown && state.isDown) {
            state.pressTime = std::chrono::steady_clock::now();
        }
    }
}

void InputNode::PollMouse() {
    // Get mouse position in client coordinates
    POINT cursorPos;
    if (GetCursorPos(&cursorPos)) {
        // Convert to client coordinates
        if (ScreenToClient(hwnd, &cursorPos)) {
            if (firstMousePoll) {
                // First frame: initialize without delta
                lastMouseX = cursorPos.x;
                lastMouseY = cursorPos.y;
                firstMousePoll = false;
            }
            // Don't update lastMouse here - let PublishMouseEvents do it after calculating delta
        }
    }
}

// ====== Event Publishing ======

void InputNode::PublishKeyEvents() {
    if (!GetMessageBus()) {
        return;  // No message bus available
    }

    bool shift = IsShiftPressed();
    bool ctrl = IsCtrlPressed();
    bool alt = IsAltPressed();

    for (const auto& [key, state] : keyStates) {
        // Skip modifier keys to avoid self-reporting
        if (key == EventBus::KeyCode::Shift ||
            key == EventBus::KeyCode::Ctrl ||
            key == EventBus::KeyCode::Alt) {
            continue;
        }

        // KeyPressed: Key just went down
        if (!state.wasDown && state.isDown) {
            auto event = std::make_unique<EventBus::KeyEvent>(
                instanceId,
                key,
                EventBus::KeyEventType::Pressed,
                0.0f,  // Duration 0 for pressed
                shift, ctrl, alt
            );
            GetMessageBus()->Publish(std::move(event));
        }
        // KeyReleased: Key just went up
        else if (state.wasDown && !state.isDown) {
            // Calculate duration held
            auto now = std::chrono::steady_clock::now();
            float duration = std::chrono::duration<float>(now - state.pressTime).count();

            auto event = std::make_unique<EventBus::KeyEvent>(
                instanceId,
                key,
                EventBus::KeyEventType::Released,
                duration,
                shift, ctrl, alt
            );
            GetMessageBus()->Publish(std::move(event));
        }
        // KeyHeld: Key is currently down (every frame it's down)
        else if (state.isDown) {
            // Calculate duration held
            auto now = std::chrono::steady_clock::now();
            float duration = std::chrono::duration<float>(now - state.pressTime).count();

            auto event = std::make_unique<EventBus::KeyEvent>(
                instanceId,
                key,
                EventBus::KeyEventType::Held,
                duration,
                shift, ctrl, alt
            );
            GetMessageBus()->Publish(std::move(event));
        }
    }
}

void InputNode::PublishMouseEvents() {
    if (!GetMessageBus()) {
        return;
    }

    // Get current mouse position
    POINT cursorPos;
    if (!GetCursorPos(&cursorPos) || !ScreenToClient(hwnd, &cursorPos)) {
        return;
    }

    // Calculate delta
    float deltaX = static_cast<float>(cursorPos.x - lastMouseX);
    float deltaY = static_cast<float>(cursorPos.y - lastMouseY);

    // Only publish if mouse moved (avoid spam)
    if (deltaX != 0.0f || deltaY != 0.0f) {
        auto event = std::make_unique<EventBus::MouseMoveEvent>(
            instanceId,
            cursorPos.x,
            cursorPos.y,
            deltaX,
            deltaY
        );
        GetMessageBus()->Publish(std::move(event));

        // Update last position
        lastMouseX = cursorPos.x;
        lastMouseY = cursorPos.y;
    }
}

} // namespace Vixen::RenderGraph
