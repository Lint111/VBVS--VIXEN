#include "Nodes/InputNode.h"
#include "Core/NodeLogging.h"
#include "NodeHelpers/ValidationHelpers.h"
#include <iostream>

using namespace RenderGraph::NodeHelpers;

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
    mouseCaptured = false;
}

void InputNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("[InputNode] Compile");

    // Validate HWND input using helper
    hwnd = ValidateInput<HWND>(ctx, "HWND", InputNodeConfig::HWND_IN);

    NODE_LOG_INFO("[InputNode] HWND received successfully");
}

void InputNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Calculate delta time
    UpdateDeltaTime();

    // Enable mouse capture on first frame
    if (!mouseCaptured && hwnd) {
        InitializeMouseCapture();
    }

    // Poll input state
    PollKeyboard();
    PollMouse();

    // Modern polling-based input: No event publishing needed
    // - Mouse movement is handled via InputState.mouseDelta (no MouseMoveEvent)
    // - Keyboard input is polled directly from InputState (no KeyEvent)
    // - ESC is handled inline in PublishKeyEvents for app exit

    // Still call PublishKeyEvents for ESC handling only
    PublishKeyEvents();
    // PublishMouseEvents() disabled - all input via InputState polling

    // Re-center mouse for continuous movement
    if (mouseCaptured && hwnd) {
        RecenterMouse();
    }

    // Modern polling interface: Populate InputState and output it
    PopulateInputState();
    ctx.Out(InputNodeConfig::INPUT_STATE, &inputState);
}

void InputNode::PopulateInputState() {
    // Clear per-frame state (pressed/released flags, but NOT mouseDelta)
    inputState.BeginFrame();

    // Update frame timing
    inputState.deltaTime = deltaTime;

    // Copy keyboard state
    for (const auto& [key, state] : keyStates) {
        inputState.keyDown[key] = state.isDown;

        // Just pressed: down this frame, but not last frame
        if (state.isDown && !state.wasDown) {
            inputState.keyPressed[key] = true;
        }

        // Just released: up this frame, but was down last frame
        if (!state.isDown && state.wasDown) {
            inputState.keyReleased[key] = true;
        }
    }

    // Get current mouse position and calculate delta
    POINT cursorPos;
    if (GetCursorPos(&cursorPos) && ScreenToClient(hwnd, &cursorPos)) {
        // Calculate this frame's mouse movement delta
        float deltaX = static_cast<float>(cursorPos.x - lastMouseX);
        float deltaY = static_cast<float>(cursorPos.y - lastMouseY);
        inputState.mouseDelta = glm::vec2(deltaX, deltaY);

        // Store position for next frame's delta calculation
        lastMouseX = cursorPos.x;
        lastMouseY = cursorPos.y;

        // Update current position in input state
        inputState.mousePosition = glm::vec2(cursorPos.x, cursorPos.y);
    }

    // Mouse buttons (query current state)
    inputState.mouseButtons[0] = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    inputState.mouseButtons[1] = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    inputState.mouseButtons[2] = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
}

void InputNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("[InputNode] Cleanup");

    // Release mouse capture
    if (mouseCaptured) {
        ReleaseCapture();
        mouseCaptured = false;
    }

    keyStates.clear();
}

// ====== Helper Methods ======

void InputNode::UpdateDeltaTime() {
    auto now = std::chrono::steady_clock::now();
    deltaTime = std::chrono::duration<float>(now - lastFrameTime).count();
    lastFrameTime = now;
}

void InputNode::InitializeMouseCapture() {
    // Get window center for re-centering
    RECT rect;
    if (GetClientRect(hwnd, &rect)) {
        int centerX = (rect.right - rect.left) / 2;
        int centerY = (rect.bottom - rect.top) / 2;
        POINT center = {centerX, centerY};
        ClientToScreen(hwnd, &center);
        SetCursorPos(center.x, center.y);

        lastMouseX = centerX;
        lastMouseY = centerY;
    }

    // Capture mouse to window
    SetCapture(hwnd);
    mouseCaptured = true;
    NODE_LOG_INFO("[InputNode] Mouse captured for game mode");
}

void InputNode::RecenterMouse() {
    RECT rect;
    GetClientRect(hwnd, &rect);
    int centerX = (rect.right - rect.left) / 2;
    int centerY = (rect.bottom - rect.top) / 2;
    POINT center = {centerX, centerY};
    ClientToScreen(hwnd, &center);
    SetCursorPos(center.x, center.y);

    // Update last position to center (avoid accumulated drift)
    lastMouseX = centerX;
    lastMouseY = centerY;
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
    // Modern input system: Only handle ESC for application exit
    // All other keyboard input is polled via InputState.keyDown/keyPressed/keyReleased
    // No continuous KeyEvent publishing to avoid event flooding and render stalls

    // Check ESC specifically
    auto escIt = keyStates.find(EventBus::KeyCode::Escape);
    if (escIt != keyStates.end()) {
        const auto& escState = escIt->second;
        // ESC just pressed: exit application
        if (!escState.wasDown && escState.isDown) {
            PostQuitMessage(0);
            return;
        }
    }

    // No other events published (input via polling instead)
}

void InputNode::PublishMouseEvents() {
    if (!GetMessageBus()) {
        return;
    }

    // Get current mouse position and delta (already calculated in PopulateInputState)
    POINT cursorPos;
    if (!GetCursorPos(&cursorPos) || !ScreenToClient(hwnd, &cursorPos)) {
        return;
    }

    // Use delta from inputState (calculated in PopulateInputState)
    float deltaX = inputState.mouseDelta.x;
    float deltaY = inputState.mouseDelta.y;
    float deltaMagnitude = std::sqrt(deltaX * deltaX + deltaY * deltaY);

    // DISABLED: Continuous MouseMoveEvent causes event flooding and stuttering
    // Camera now queries mouse state once per frame instead of processing hundreds of events
    // State-based MouseMoveStartEvent below is kept for UI/debug feedback
    // if (deltaMagnitude > 0.01f) {
    //     auto event = std::make_unique<EventBus::MouseMoveEvent>(
    //         instanceId,
    //         cursorPos.x,
    //         cursorPos.y,
    //         deltaX,
    //         deltaY
    //     );
    //     GetMessageBus()->Publish(std::move(event));
    // }

    // State-based event system: Start/End events for UI feedback
    const float START_THRESHOLD = 0.5f;  // Start movement session
    const float END_THRESHOLD = 0.1f;    // End movement session

    static bool mouseMoving = false;
    static int32_t moveStartX = 0, moveStartY = 0;
    static float totalDeltaX = 0.0f, totalDeltaY = 0.0f;
    static auto moveStartTime = std::chrono::steady_clock::now();

    if (!mouseMoving && deltaMagnitude >= START_THRESHOLD) {
        // Start movement session
        mouseMoving = true;
        moveStartX = lastMouseX;
        moveStartY = lastMouseY;
        totalDeltaX = deltaX;
        totalDeltaY = deltaY;
        moveStartTime = std::chrono::steady_clock::now();

        auto event = std::make_unique<EventBus::MouseMoveStartEvent>(
            instanceId,
            cursorPos.x,
            cursorPos.y,
            deltaX,
            deltaY
        );
        GetMessageBus()->Publish(std::move(event));
    }
    else if (mouseMoving && deltaMagnitude < END_THRESHOLD) {
        // End movement session
        mouseMoving = false;
        auto moveEndTime = std::chrono::steady_clock::now();
        float duration = std::chrono::duration<float>(moveEndTime - moveStartTime).count();

        auto event = std::make_unique<EventBus::MouseMoveEndEvent>(
            instanceId,
            cursorPos.x,
            cursorPos.y,
            totalDeltaX,
            totalDeltaY,
            duration
        );
        GetMessageBus()->Publish(std::move(event));

        totalDeltaX = 0.0f;
        totalDeltaY = 0.0f;
    }
    else if (mouseMoving) {
        // Accumulate deltas during session
        totalDeltaX += deltaX;
        totalDeltaY += deltaY;
    }
}

} // namespace Vixen::RenderGraph
