#include "Nodes/InputNode.h"
#include "Core/NodeRegistration.h"
#include "Core/NodeLogging.h"
#include "NodeHelpers/ValidationHelpers.h"
#include <iostream>

#define GLFW_INCLUDE_NONE   // don't pull in <GL/gl.h> (absent on headless/WSL); Vulkan-only below
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

using namespace RenderGraph::NodeHelpers;

namespace Vixen::RenderGraph {

namespace {
// Map VIXEN's KeyCode (which uses Win32 virtual-key values) to GLFW key codes.
// Letters and digits already coincide (GLFW uses ASCII uppercase: GLFW_KEY_A==0x41, GLFW_KEY_0==0x30),
// but modifiers, escape and the arrow keys differ, so they need an explicit translation.
int KeyCodeToGlfw(EventBus::KeyCode key) {
    using EventBus::KeyCode;
    switch (key) {
        // Letters (ASCII-aligned, but list explicitly for clarity)
        case KeyCode::W: return GLFW_KEY_W;
        case KeyCode::A: return GLFW_KEY_A;
        case KeyCode::S: return GLFW_KEY_S;
        case KeyCode::D: return GLFW_KEY_D;
        case KeyCode::Q: return GLFW_KEY_Q;
        case KeyCode::E: return GLFW_KEY_E;
        case KeyCode::C: return GLFW_KEY_C;
        // Digits
        case KeyCode::Key0: return GLFW_KEY_0;
        case KeyCode::Key1: return GLFW_KEY_1;
        case KeyCode::Key2: return GLFW_KEY_2;
        case KeyCode::Key3: return GLFW_KEY_3;
        case KeyCode::Key4: return GLFW_KEY_4;
        case KeyCode::Key5: return GLFW_KEY_5;
        case KeyCode::Key6: return GLFW_KEY_6;
        case KeyCode::Key7: return GLFW_KEY_7;
        case KeyCode::Key8: return GLFW_KEY_8;
        case KeyCode::Key9: return GLFW_KEY_9;
        // Special keys (differ from Win32 VK codes)
        case KeyCode::Space:  return GLFW_KEY_SPACE;
        case KeyCode::Shift:  return GLFW_KEY_LEFT_SHIFT;
        case KeyCode::Ctrl:   return GLFW_KEY_LEFT_CONTROL;
        case KeyCode::Alt:    return GLFW_KEY_LEFT_ALT;
        case KeyCode::Escape: return GLFW_KEY_ESCAPE;
        // Arrow keys
        case KeyCode::Left:   return GLFW_KEY_LEFT;
        case KeyCode::Right:  return GLFW_KEY_RIGHT;
        case KeyCode::Up:     return GLFW_KEY_UP;
        case KeyCode::Down:   return GLFW_KEY_DOWN;
        default: return GLFW_KEY_UNKNOWN;
    }
}
}  // namespace

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
    // Arrow keys for look rotation
    keyStates[KeyCode::Left] = KeyState{};
    keyStates[KeyCode::Right] = KeyState{};
    keyStates[KeyCode::Up] = KeyState{};
    keyStates[KeyCode::Down] = KeyState{};
    // Number keys for debug mode switching
    keyStates[KeyCode::Key0] = KeyState{};
    keyStates[KeyCode::Key1] = KeyState{};
    keyStates[KeyCode::Key2] = KeyState{};
    keyStates[KeyCode::Key3] = KeyState{};
    keyStates[KeyCode::Key4] = KeyState{};
    keyStates[KeyCode::Key5] = KeyState{};
    keyStates[KeyCode::Key6] = KeyState{};
    keyStates[KeyCode::Key7] = KeyState{};
    keyStates[KeyCode::Key8] = KeyState{};
    keyStates[KeyCode::Key9] = KeyState{};
    // Frame capture key
    keyStates[KeyCode::C] = KeyState{};
}

void InputNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_INFO("[InputNode] Setup");
    lastFrameTime = std::chrono::steady_clock::now();
    mouseCaptured = false;

    enabled_ = GetParameterValue<bool>(InputNodeConfig::PARAM_ENABLED, true);
    SyncConfigFromParams();

    NODE_LOG_INFO("[InputNode] enabled=" + std::to_string(enabled_) +
                  ", cursor_mode=" + std::to_string(static_cast<int>(config_.cursorMode)) +
                  ", orbit_button=" + std::to_string(static_cast<int>(config_.orbitButton)));
}

void InputNode::SetInputConfig(const InputConfig& config) {
    config_ = config;
    ApplyCursorMode();
}

void InputNode::SyncConfigFromParams() {
    // mouse_capture_mode keeps its legacy MouseCaptureMode encoding on the wire (existing callers
    // like BenchmarkGraphFactory pass MouseCaptureMode values) but is folded into config_.cursorMode,
    // the single source of truth ApplyCursorMode/InitializeMouseCapture now read. Free and Disabled
    // both meant "don't capture" in the old gate (ExecuteImpl only special-cased CenterLock), so both
    // map to Normal.
    const int legacyDefault = config_.cursorMode == InputConfig::CursorMode::CenterLock
        ? static_cast<int>(MouseCaptureMode::CenterLock)
        : static_cast<int>(MouseCaptureMode::Free);
    const int captureMode = GetParameterValue<int>(InputNodeConfig::PARAM_MOUSE_CAPTURE_MODE, legacyDefault);
    const auto newCursorMode = static_cast<MouseCaptureMode>(captureMode) == MouseCaptureMode::CenterLock
        ? InputConfig::CursorMode::CenterLock
        : InputConfig::CursorMode::Normal;

    const int orbitButton = GetParameterValue<int>(InputNodeConfig::PARAM_ORBIT_BUTTON,
                                                     static_cast<int>(config_.orbitButton));
    const auto newOrbitButton = static_cast<InputConfig::OrbitButton>(orbitButton);

    if (newCursorMode != config_.cursorMode) {
        config_.cursorMode = newCursorMode;
        ApplyCursorMode();
    }
    config_.orbitButton = newOrbitButton;
}

void InputNode::ApplyCursorMode() {
    if (!window) return;
    switch (config_.cursorMode) {
        case InputConfig::CursorMode::Normal:
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            mouseCaptured = false;
            break;
        case InputConfig::CursorMode::Hidden:
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
            mouseCaptured = false;
            break;
        case InputConfig::CursorMode::CenterLock:
            // Deferred to InitializeMouseCapture (ExecuteImpl) so the last-mouse-position seed
            // stays colocated with the GLFW_CURSOR_DISABLED call; just clear the latch here so
            // that call fires again on the next ExecuteImpl.
            mouseCaptured = false;
            break;
    }
}

void InputNode::CompileImpl(TypedCompileContext& ctx) {
    NODE_LOG_INFO("[InputNode] Compile");

    // Validate WINDOW input using helper
    window = ValidateInput<GLFWwindow*>(ctx, "WINDOW", InputNodeConfig::WINDOW);

    // config_ may have been set via SetInputConfig before the window existed (graph-build order
    // is app-defined); ApplyCursorMode no-oped then, so re-apply now that window is live. Normal/
    // CenterLock also get a chance via SyncConfigFromParams each frame, but Hidden has no param
    // path, so this is its only application point besides a direct SetInputConfig post-Compile.
    ApplyCursorMode();

    NODE_LOG_INFO("[InputNode] Window received successfully");
}

void InputNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Calculate delta time
    UpdateDeltaTime();

    // If disabled, output empty state and skip polling
    if (!enabled_) {
        inputState_.BeginFrame();
        inputState_.deltaTime = deltaTime;
        ctx.Out(InputNodeConfig::INPUT_STATE, &inputState_);
        return;
    }

    // Live param re-apply (no callback exists on SetParameter — see SyncConfigFromParams doc).
    SyncConfigFromParams();

    // Initialize mouse capture based on capture mode
    if (!mouseCaptured && window && config_.cursorMode == InputConfig::CursorMode::CenterLock) {
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
    // Publish MouseButtonEvent on press/release transitions (for picking/selection).
    // Mouse-move and continuous input still flow via InputState polling.
    PublishMouseEvents();

    // GLFW's GLFW_CURSOR_DISABLED mode provides virtual unbounded cursor movement and recenters
    // internally, so no manual recentering is needed (unlike the old Win32 SetCursorPos approach).

    // Modern polling interface: Populate InputState and output it
    PopulateInputState();
    ctx.Out(InputNodeConfig::INPUT_STATE, &inputState_);
}

void InputNode::PopulateInputState() {
    // Clear per-frame state (pressed/released flags, but NOT mouseDelta)
    inputState_.BeginFrame();

    // Update frame timing
    inputState_.deltaTime = deltaTime;

    // Copy keyboard state
    for (const auto& [key, state] : keyStates) {
        inputState_.keyDown[key] = state.isDown;

        // Just pressed: down this frame, but not last frame
        if (state.isDown && !state.wasDown) {
            inputState_.keyPressed[key] = true;
        }

        // Just released: up this frame, but was down last frame
        if (!state.isDown && state.wasDown) {
            inputState_.keyReleased[key] = true;
        }
    }

    // Update debug mode based on number key presses (0-9)
    // debugMode persists until another number is pressed
    using EventBus::KeyCode;
    if (inputState_.IsKeyPressed(KeyCode::Key0)) inputState_.debugMode = 0;
    else if (inputState_.IsKeyPressed(KeyCode::Key1)) inputState_.debugMode = 1;
    else if (inputState_.IsKeyPressed(KeyCode::Key2)) inputState_.debugMode = 2;
    else if (inputState_.IsKeyPressed(KeyCode::Key3)) inputState_.debugMode = 3;
    else if (inputState_.IsKeyPressed(KeyCode::Key4)) inputState_.debugMode = 4;
    else if (inputState_.IsKeyPressed(KeyCode::Key5)) inputState_.debugMode = 5;
    else if (inputState_.IsKeyPressed(KeyCode::Key6)) inputState_.debugMode = 6;
    else if (inputState_.IsKeyPressed(KeyCode::Key7)) inputState_.debugMode = 7;
    else if (inputState_.IsKeyPressed(KeyCode::Key8)) inputState_.debugMode = 8;
    else if (inputState_.IsKeyPressed(KeyCode::Key9)) inputState_.debugMode = 9;

    // Get current mouse position and calculate delta (GLFW reports cursor in window coordinates;
    // in GLFW_CURSOR_DISABLED mode this is a virtual unbounded position ideal for camera deltas).
    if (window) {
        double cursorX = 0.0, cursorY = 0.0;
        glfwGetCursorPos(window, &cursorX, &cursorY);
        int32_t ix = static_cast<int32_t>(cursorX);
        int32_t iy = static_cast<int32_t>(cursorY);

        // Calculate this frame's mouse movement delta
        float deltaX = static_cast<float>(ix - lastMouseX);
        float deltaY = static_cast<float>(iy - lastMouseY);
        inputState_.mouseDelta = glm::vec2(deltaX, deltaY);

        // Store position for next frame's delta calculation
        lastMouseX = ix;
        lastMouseY = iy;

        // Update current position in input state
        inputState_.mousePosition = glm::vec2(static_cast<float>(ix), static_cast<float>(iy));

        // Mouse buttons (query current state)
        inputState_.mouseButtons[0] = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        inputState_.mouseButtons[1] = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        inputState_.mouseButtons[2] = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
    }
}

void InputNode::CleanupImpl(TypedCleanupContext& ctx) {
    NODE_LOG_INFO("[InputNode] Cleanup");

    // Release mouse capture (restore normal cursor)
    if (mouseCaptured) {
        if (window) {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
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
    if (!window) return;

    // GLFW_CURSOR_DISABLED hides the cursor and provides virtual unbounded motion (the
    // cross-platform equivalent of Win32 SetCapture + manual recentering for FPS camera control).
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // Seed the last-mouse position from the current cursor so the first frame's delta is ~0.
    double cursorX = 0.0, cursorY = 0.0;
    glfwGetCursorPos(window, &cursorX, &cursorY);
    lastMouseX = static_cast<int32_t>(cursorX);
    lastMouseY = static_cast<int32_t>(cursorY);

    mouseCaptured = true;
    NODE_LOG_INFO("[InputNode] Mouse captured for game mode (GLFW_CURSOR_DISABLED)");
}

void InputNode::RecenterMouse() {
    // No-op under GLFW: GLFW_CURSOR_DISABLED recenters internally and reports virtual unbounded
    // motion, so the old Win32 manual SetCursorPos recentering is unnecessary.
}

// ====== Input Polling ======

bool InputNode::IsKeyDown(EventBus::KeyCode key) const {
    if (!window) return false;
    int glfwKey = KeyCodeToGlfw(key);
    if (glfwKey == GLFW_KEY_UNKNOWN) return false;
    return glfwGetKey(window, glfwKey) == GLFW_PRESS;
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
    // Get mouse position in window coordinates (GLFW)
    if (window) {
        double cursorX = 0.0, cursorY = 0.0;
        glfwGetCursorPos(window, &cursorX, &cursorY);
        if (firstMousePoll) {
            // First frame: initialize without delta
            lastMouseX = static_cast<int32_t>(cursorX);
            lastMouseY = static_cast<int32_t>(cursorY);
            firstMousePoll = false;
        }
        // Don't update lastMouse here - let PopulateInputState do it after calculating delta
    }
}

// ====== Event Publishing ======

void InputNode::PublishKeyEvents() {
    // Modern input system: ESC publishes WindowCloseEvent for graceful shutdown
    // All other keyboard input is polled via InputState.keyDown/keyPressed/keyReleased
    // No continuous KeyEvent publishing to avoid event flooding and render stalls

    // Check ESC specifically - publish WindowCloseEvent instead of PostQuitMessage
    // This allows BenchmarkRunner and other higher-level systems to handle shutdown gracefully
    auto escIt = keyStates.find(EventBus::KeyCode::Escape);
    if (escIt != keyStates.end()) {
        const auto& escState = escIt->second;
        // ESC just pressed: request graceful exit via event
        if (!escState.wasDown && escState.isDown) {
            if (GetMessageBus()) {
                auto closeEvent = std::make_unique<EventBus::WindowCloseEvent>(instanceId);
                GetMessageBus()->Publish(std::move(closeEvent));
                NODE_LOG_INFO("[InputNode] ESC pressed - published WindowCloseEvent for graceful shutdown");
            }
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
    if (!window) {
        return;
    }
    double rawCursorX = 0.0, rawCursorY = 0.0;
    glfwGetCursorPos(window, &rawCursorX, &rawCursorY);
    int32_t cursorPosX = static_cast<int32_t>(rawCursorX);
    int32_t cursorPosY = static_cast<int32_t>(rawCursorY);

    // --- Mouse button transitions -> MouseButtonEvent ----------------------
    // Edge-detect each of the 3 buttons against last frame's state and publish
    // a Pressed event on the down-edge and a Released event on the up-edge,
    // carrying the button and current cursor pixel coords. Pull-based InputState
    // polling is unaffected (both coexist).
    {
        const int glfwButtons[3] = {
            GLFW_MOUSE_BUTTON_LEFT,
            GLFW_MOUSE_BUTTON_RIGHT,
            GLFW_MOUSE_BUTTON_MIDDLE
        };
        const EventBus::MouseButton vixenButtons[3] = {
            EventBus::MouseButton::Left,
            EventBus::MouseButton::Right,
            EventBus::MouseButton::Middle
        };

        for (int i = 0; i < 3; ++i) {
            const bool nowDown = glfwGetMouseButton(window, glfwButtons[i]) == GLFW_PRESS;
            const bool wasDown = lastMouseButtonState_[i];

            if (nowDown != wasDown) {
                const EventBus::KeyEventType type = nowDown
                    ? EventBus::KeyEventType::Pressed
                    : EventBus::KeyEventType::Released;

                auto event = std::make_unique<EventBus::MouseButtonEvent>(
                    instanceId,
                    vixenButtons[i],
                    type,
                    cursorPosX,
                    cursorPosY
                );
                GetMessageBus()->Publish(std::move(event));
            }

            lastMouseButtonState_[i] = nowDown;
        }
    }

    // Use delta from inputState (calculated in PopulateInputState)
    float deltaX = inputState_.mouseDelta.x;
    float deltaY = inputState_.mouseDelta.y;
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
            cursorPosX,
            cursorPosY,
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
            cursorPosX,
            cursorPosY,
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

// Self-registration (M3): registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::InputNodeType);
