#include "Nodes/InputNode.h"
#include "Core/NodeRegistration.h"
#include "Core/NodeLogging.h"
#include "NodeHelpers/ValidationHelpers.h"
#include "Message.h"
#include <iostream>
#include <unordered_map>

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
        case KeyCode::F: return GLFW_KEY_F;
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
        case KeyCode::Tab:    return GLFW_KEY_TAB;
        // Arrow keys
        case KeyCode::Left:   return GLFW_KEY_LEFT;
        case KeyCode::Right:  return GLFW_KEY_RIGHT;
        case KeyCode::Up:     return GLFW_KEY_UP;
        case KeyCode::Down:   return GLFW_KEY_DOWN;
        default: return GLFW_KEY_UNKNOWN;
    }
}

// Inverse of KeyCodeToGlfw: the callback receives a raw GLFW key code and keyStates is keyed by
// EventBus::KeyCode, so folding a Key InputEvent needs this direction too. Built once, lazily, by
// running every KeyCode this node tracks (InputNode's ctor list) through the forward map — that
// keeps the two maps mechanically in sync (one edit site) instead of hand-duplicating the switch.
const std::unordered_map<int, EventBus::KeyCode>& GlfwToKeyCodeMap() {
    static const std::unordered_map<int, EventBus::KeyCode> map = [] {
        using EventBus::KeyCode;
        constexpr KeyCode kTracked[] = {
            KeyCode::W, KeyCode::A, KeyCode::S, KeyCode::D, KeyCode::Q, KeyCode::E, KeyCode::C, KeyCode::F,
            KeyCode::Space, KeyCode::Shift, KeyCode::Ctrl, KeyCode::Alt, KeyCode::Escape, KeyCode::Tab,
            KeyCode::Left, KeyCode::Right, KeyCode::Up, KeyCode::Down,
            KeyCode::Key0, KeyCode::Key1, KeyCode::Key2, KeyCode::Key3, KeyCode::Key4,
            KeyCode::Key5, KeyCode::Key6, KeyCode::Key7, KeyCode::Key8, KeyCode::Key9,
        };
        std::unordered_map<int, KeyCode> m;
        for (KeyCode kc : kTracked) {
            const int glfwKey = KeyCodeToGlfw(kc);
            if (glfwKey != GLFW_KEY_UNKNOWN) m[glfwKey] = kc;
        }
        return m;
    }();
    return map;
}

// --- GLFW-window -> InputNode registry (coexistence with WindowNode's user pointer) -----------
// WindowNode already calls glfwSetWindowUserPointer(window, this) for its own 4 callbacks
// (WindowNode.cpp:82-87) and there is no dispatch-struct precedent for sharing that pointer.
// Rather than touch WindowNode (out of scope for this slice, and risks its resize/close/focus/
// iconify wiring) InputNode keeps a SEPARATE static map keyed by GLFWwindow* — GLFW callback
// registration is per-callback-slot (mouse-button/cursor-pos/scroll/key are each their own GLFW
// slot, distinct from WindowNode's framebuffer-size/close/focus/iconify slots), so there is no
// collision: both nodes register their own callbacks on the same window with zero shared state.
// One process only ever has a handful of live windows, so a linear/hash lookup here is not a
// hot path (fires once per raw OS input event, not per render frame).
std::unordered_map<GLFWwindow*, Vixen::RenderGraph::InputNode*>& WindowToInputNodeRegistry() {
    static std::unordered_map<GLFWwindow*, Vixen::RenderGraph::InputNode*> registry;
    return registry;
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
    keyStates[KeyCode::Tab] = KeyState{};
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
    // Orbit -> Free-fly transition key
    keyStates[KeyCode::F] = KeyState{};
}

void InputNode::SetupImpl(TypedSetupContext& ctx) {
    NODE_LOG_INFO("[InputNode] Setup");
    lastFrameTime = std::chrono::steady_clock::now();
    mouseCaptured = false;

    enabled_ = GetParameterValue<bool>(InputNodeConfig::PARAM_ENABLED, true);
    SyncConfigFromParams();

    // Subscribe to focus-loss ONCE (SetupImpl re-runs on every recompile — see SwapChainNode's
    // resizeSubscribed_ for the same guard idiom). WindowNode already publishes
    // WindowStateChangeEvent(Focused/Unfocused) from its GLFW focus callback; no new callback
    // slot needed (GLFW focus callbacks are WindowNode's single owned slot).
    if (GetMessageBus() && !focusSubscribed_) {
        focusSubscribed_ = true;
        SubscribeToMessage(
            EventBus::WindowStateChangeEvent::TYPE,
            [this](const EventBus::BaseEventMessage& msg) -> bool {
                const auto& stateMsg = static_cast<const EventBus::WindowStateChangeEvent&>(msg);
                if (stateMsg.newState == EventBus::WindowStateChangeEvent::State::Unfocused) {
                    ClearInputOnFocusLoss();
                }
                return true;
            }
        );
    }

    NODE_LOG_INFO("[InputNode] enabled=" + std::to_string(enabled_) +
                  ", cursor_mode=" + std::to_string(static_cast<int>(config_.cursorMode)));
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
    // map to Normal. An UNSET param means "no opinion" — config_ (which can hold Hidden, a mode with
    // no legacy wire value) is left untouched; only an explicit param write folds in.
    if (GetParameter(InputNodeConfig::PARAM_MOUSE_CAPTURE_MODE) != nullptr) {
        const int captureMode = GetParameterValue<int>(InputNodeConfig::PARAM_MOUSE_CAPTURE_MODE, 0);
        const auto newCursorMode = static_cast<MouseCaptureMode>(captureMode) == MouseCaptureMode::CenterLock
            ? InputConfig::CursorMode::CenterLock
            : InputConfig::CursorMode::Normal;
        if (newCursorMode != config_.cursorMode) {
            config_.cursorMode = newCursorMode;
            ApplyCursorMode();
        }
    }
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
    GLFWwindow* newWindow = ValidateInput<GLFWwindow*>(ctx, "WINDOW", InputNodeConfig::WINDOW);

    // A recompile can hand back the SAME window (WindowNode's window/surface persist across
    // recompiles by design — see WindowNode.h) or, in principle, a different one if the graph
    // were rebuilt against a new WindowNode instance. Either way RegisterCallbacks() must run
    // whenever the window identity changes, so registry-and-window updates stay atomic: drop this
    // node's old registry entry (if any) BEFORE swapping `window`, so no stale entry can dispatch
    // a late in-flight callback to a node that no longer owns that window.
    if (newWindow != window) {
        if (window) WindowToInputNodeRegistry().erase(window);
        window = newWindow;
    }

    // config_ may have been set via SetInputConfig before the window existed (graph-build order
    // is app-defined); ApplyCursorMode no-oped then, so re-apply now that window is live. Normal/
    // CenterLock also get a chance via SyncConfigFromParams each frame, but Hidden has no param
    // path, so this is its only application point besides a direct SetInputConfig post-Compile.
    ApplyCursorMode();
    RegisterCallbacks();  // idempotent — GLFW replaces same-slot callbacks, registry entry re-pointed at `this`

    NODE_LOG_INFO("[InputNode] Window received successfully");
}

void InputNode::RegisterCallbacks() {
    if (!window) return;

    // Own registry entry: FoldEvent/the trampolines dispatch through this map (see the anonymous-
    // namespace comment above WindowToInputNodeRegistry for why this is separate from WindowNode's
    // glfwSetWindowUserPointer). Re-assigning on every CompileImpl keeps a recompiled/rebuilt graph
    // correct even if a NEW InputNode instance ends up owning the same GLFWwindow*.
    WindowToInputNodeRegistry()[window] = this;

    // Seed the canonical cursor position NOW, before any CursorPos callback has fired. Without this,
    // firstCursorEvent_ stays true and cursorPos_ stays (0,0) until the first mouse-move event — so a
    // click landing before the cursor ever moves (e.g. immediately after window focus) would carry
    // (0,0) as its ClickEvent position instead of the real cursor location. This does not set
    // firstCursorEvent_ = false: the first real CursorPos event still seeds without a spurious delta,
    // it just seeds from (correctly) the same position instead of (0,0).
    double seedX = 0.0, seedY = 0.0;
    glfwGetCursorPos(window, &seedX, &seedY);
    cursorPos_ = glm::vec2(static_cast<float>(seedX), static_cast<float>(seedY));

    glfwSetMouseButtonCallback(window, &InputNode::OnMouseButton);
    glfwSetCursorPosCallback(window, &InputNode::OnCursorPos);
    glfwSetScrollCallback(window, &InputNode::OnScroll);
    glfwSetKeyCallback(window, &InputNode::OnKey);

    // Belt-and-braces backstop (spec 2026-07-02-input-rework-slice1-design.md §2): if a press+
    // release both land inside one glfwPollEvents() call in a way that would otherwise skip the
    // callback edge (shouldn't happen with callbacks, but costs nothing), GLFW's sticky flags
    // still latch the edge for the next glfwGetMouseButton/glfwGetKey poll. Nothing in this file
    // polls anymore, but the flag is a global per-window one-liner, so setting it here is free
    // insurance requested explicitly by the plan, not a live code path this node depends on.
    glfwSetInputMode(window, GLFW_STICKY_MOUSE_BUTTONS, GLFW_TRUE);
    glfwSetInputMode(window, GLFW_STICKY_KEYS, GLFW_TRUE);
}

// ====== GLFW callback trampolines ======
// Static (GLFW's C API requires free-function pointers); each resolves the originating InputNode
// via the registry then forwards to QueueEvent. Kept minimal — all fold logic lives in FoldEvent,
// run later from ProcessPendingInput, never from inside a GLFW callback (glfwPollEvents() calls
// these synchronously on the main thread; deferring the fold keeps the mutex-guarded queue as the
// ONLY thing callbacks touch, matching WindowNode's pendingEvents idiom).

void InputNode::OnMouseButton(GLFWwindow* w, int button, int action, int /*mods*/) {
    auto it = WindowToInputNodeRegistry().find(w);
    if (it == WindowToInputNodeRegistry().end()) return;
    it->second->QueueEvent({InputEvent::Type::MouseButton, button, action, 0.0, 0.0});
}

void InputNode::OnCursorPos(GLFWwindow* w, double x, double y) {
    auto it = WindowToInputNodeRegistry().find(w);
    if (it == WindowToInputNodeRegistry().end()) return;
    it->second->QueueEvent({InputEvent::Type::CursorPos, 0, 0, x, y});
}

void InputNode::OnScroll(GLFWwindow* w, double xoffset, double yoffset) {
    auto it = WindowToInputNodeRegistry().find(w);
    if (it == WindowToInputNodeRegistry().end()) return;
    it->second->QueueEvent({InputEvent::Type::Scroll, 0, 0, xoffset, yoffset});
}

void InputNode::OnKey(GLFWwindow* w, int key, int /*scancode*/, int action, int /*mods*/) {
    auto it = WindowToInputNodeRegistry().find(w);
    if (it == WindowToInputNodeRegistry().end()) return;
    it->second->QueueEvent({InputEvent::Type::Key, key, action, 0.0, 0.0});
}

void InputNode::QueueEvent(const InputEvent& event) {
    std::lock_guard<std::mutex> lock(eventMutex_);
    pendingInput_.push_back(event);
}

void InputNode::ExecuteImpl(TypedExecuteContext& ctx) {
    // Calculate delta time
    UpdateDeltaTime();

    // If disabled, output empty state and skip polling. NOTE: pendingInput_ keeps draining via
    // ProcessPendingInput() regardless of `enabled_` (that call is unconditional from Update(),
    // not gated here) — disabling input only stops the OUTPUT from reflecting it, matching the
    // pre-rework contract (ExecuteImpl was the sole gate before too).
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

    // NOTE: no GLFW polling here anymore (critique V4: the old glfwGetKey/glfwGetMouseButton/
    // glfwGetCursorPos block collapsed a same-frame press+release into an invisible non-edge).
    // Canonical state is entirely event-derived now, drained unconditionally by
    // ProcessPendingInput() from VulkanGraphApplication::Update() (called once per Update, NOT
    // gated by whether this Execute runs — see the header's ProcessPendingInput doc comment).
    // ExecuteImpl's only remaining job is to COPY that already-current canonical state into the
    // frame's InputState output and then retire the per-frame accumulators.

    // GLFW's GLFW_CURSOR_DISABLED mode provides virtual unbounded cursor movement and recenters
    // internally, so no manual recentering is needed (unlike the old Win32 SetCursorPos approach).

    PopulateInputState();  // copy canonical -> inputState_ (accumulators cleared AFTER, inside here)
    ctx.Out(InputNodeConfig::INPUT_STATE, &inputState_);
}

void InputNode::ProcessPendingInput() {
    std::vector<InputEvent> eventsToProcess;
    {
        std::lock_guard<std::mutex> lock(eventMutex_);
        eventsToProcess.swap(pendingInput_);
    }
    for (const auto& event : eventsToProcess) {
        FoldEvent(event);
    }
}

void InputNode::FoldEvent(const InputEvent& event) {
    switch (event.type) {
        case InputEvent::Type::CursorPos: {
            const glm::vec2 newPos(static_cast<float>(event.x), static_cast<float>(event.y));
            if (firstCursorEvent_) {
                // Seed without a delta (mirrors the old firstMousePoll behavior): the very first
                // cursor sample has no "previous frame" to diff against.
                cursorPos_ = newPos;
                firstCursorEvent_ = false;
            } else {
                pendingDelta_ += (newPos - cursorPos_);
                cursorPos_ = newPos;
            }
            break;
        }
        case InputEvent::Type::MouseButton: {
            // action is GLFW_PRESS(1) or GLFW_RELEASE(0) (GLFW_REPEAT doesn't apply to buttons).
            if (event.buttonOrKey < 0 || event.buttonOrKey > 2) break;  // X1/X2 unmapped — InputState only carries L/R/M
            const bool pressed = (event.action == GLFW_PRESS);
            buttonDown_[event.buttonOrKey] = pressed;
            // clicksThisFrame position is THIS event's cursor position (fold-order), not the
            // end-of-frame position — see ClickEvent's doc comment for why that matters to
            // consumers hit-testing fast motion.
            pendingClicks_.push_back({event.buttonOrKey, pressed, cursorPos_.x, cursorPos_.y});

            // Publish the bus event with a REAL payload (was dead: zero subscribers before this
            // slice — spec calls this out as the seam future consumers, e.g. RmlUi hover, use).
            if (GetMessageBus()) {
                static constexpr EventBus::MouseButton kVixenButtons[3] = {
                    EventBus::MouseButton::Left, EventBus::MouseButton::Right, EventBus::MouseButton::Middle
                };
                const auto type = pressed ? EventBus::KeyEventType::Pressed : EventBus::KeyEventType::Released;
                GetMessageBus()->Publish(std::make_unique<EventBus::MouseButtonEvent>(
                    instanceId, kVixenButtons[event.buttonOrKey], type,
                    static_cast<int32_t>(cursorPos_.x), static_cast<int32_t>(cursorPos_.y)));
            }
            break;
        }
        case InputEvent::Type::Scroll:
            pendingScroll_ += glm::vec2(static_cast<float>(event.x), static_cast<float>(event.y));
            if (GetMessageBus()) {
                GetMessageBus()->Publish(std::make_unique<EventBus::MouseScrollEvent>(
                    instanceId, static_cast<float>(event.y), static_cast<float>(event.x)));
            }
            break;
        case InputEvent::Type::Key: {
            // GLFW_REPEAT is a held-key auto-repeat notification, not a state transition — folding
            // it into keyStates would falsely re-trigger the press edge every OS repeat interval.
            if (event.action == GLFW_REPEAT) break;
            const auto it = GlfwToKeyCodeMap().find(event.buttonOrKey);
            if (it == GlfwToKeyCodeMap().end()) break;  // untracked key (not in InputNode's ctor list)
            auto stateIt = keyStates.find(it->second);
            if (stateIt == keyStates.end()) break;
            stateIt->second.isDown = (event.action == GLFW_PRESS);
            if (stateIt->second.isDown) {
                stateIt->second.pressTime = std::chrono::steady_clock::now();
            }
            break;
        }
    }
}

void InputNode::PopulateInputState() {
    // Clear per-frame state (pressed/released flags, but NOT mouseDelta)
    inputState_.BeginFrame();

    // Update frame timing
    inputState_.deltaTime = deltaTime;

    // Copy keyboard state (KeyState.isDown/wasDown edge-detection is unchanged from the old poll
    // era; only the PRODUCER of isDown changed — FoldEvent's Key case now sets it instead of
    // glfwGetKey). wasDown<-isDown here mirrors the old PollKeyboard's per-frame edge shift,
    // relocated to sit right where it's consumed (PollKeyboard is gone — no GLFW polling remains).
    for (auto& [key, state] : keyStates) {
        inputState_.keyDown[key] = state.isDown;
        if (state.isDown && !state.wasDown) {
            inputState_.keyPressed[key] = true;
        }
        if (!state.isDown && state.wasDown) {
            inputState_.keyReleased[key] = true;
        }
        state.wasDown = state.isDown;
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

    // ESC -> graceful-shutdown WindowCloseEvent (was PublishKeyEvents' only real job; folded in
    // here since it's a one-shot check against the same keyStates map this loop already walked).
    if (inputState_.IsKeyPressed(KeyCode::Escape) && GetMessageBus()) {
        GetMessageBus()->Publish(std::make_unique<EventBus::WindowCloseEvent>(instanceId));
        NODE_LOG_INFO("[InputNode] ESC pressed - published WindowCloseEvent for graceful shutdown");
    }

    // Mouse: canonical state + this frame's accumulators, all event-derived (no GLFW query here —
    // cursorPos_/buttonDown_ are already current from FoldEvent via ProcessPendingInput, called
    // unconditionally from Update() before this Execute runs).
    inputState_.mouseDelta = pendingDelta_;
    inputState_.mousePosition = cursorPos_;
    inputState_.mouseButtons[0] = buttonDown_[0];
    inputState_.mouseButtons[1] = buttonDown_[1];
    inputState_.mouseButtons[2] = buttonDown_[2];
    inputState_.wheelDelta = pendingScroll_;
    inputState_.clicksThisFrame = pendingClicks_;

    // Mirror the config fields CameraNode needs (M4) — see InputState.h's doc comment for why
    // this rides the existing slot instead of a new connection.
    inputState_.wheelZoom = config_.wheelZoom;
    inputState_.wheelZoomSpeed = config_.wheelZoomSpeed;

    // RETENTION RULE: clear the per-frame accumulators only AFTER the copy above, not before/
    // during. If this Execute is skipped (render graph paused/recompiling — RenderFrame() skips
    // ALL node Execute() while paused, same gate WindowNode's ProcessPendingEvents doc explains),
    // ProcessPendingInput() still runs every Update() and keeps folding into pendingDelta_/
    // pendingScroll_/pendingClicks_ — clearing only here means those events accumulate across
    // however many gated Updates happen and land intact on the NEXT Execute that actually runs,
    // instead of being silently dropped by a clear nobody's Execute reached.
    pendingDelta_ = glm::vec2(0.0f);
    pendingScroll_ = glm::vec2(0.0f);
    pendingClicks_.clear();
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

    // Final teardown only (recompile keeps window/callbacks alive, same as WindowNode's own
    // window/surface persistence — see WindowNode::CleanupImpl). Drop this node's registry entry
    // so a stray in-flight callback after this node is gone can't dereference a dangling `this`;
    // WindowNode may still own + eventually destroy the same GLFWwindow*, so the registry (not
    // the window) is what must be cleaned up here.
    if (ctx.reason == CleanupReason::FinalTeardown && window) {
        WindowToInputNodeRegistry().erase(window);
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

    // Seed the canonical cursor position from the current cursor so the first folded delta is
    // ~0 (mirrors the old lastMouseX/Y seed; firstCursorEvent_ handles the very-first-ever
    // CursorPos event, this handles switching INTO CenterLock later, where GLFW_CURSOR_DISABLED's
    // virtual coordinate space starts fresh and a stale cursorPos_ from Normal-mode screen
    // coordinates would otherwise produce one large spurious delta on the mode-switch frame).
    double cursorX = 0.0, cursorY = 0.0;
    glfwGetCursorPos(window, &cursorX, &cursorY);
    cursorPos_ = glm::vec2(static_cast<float>(cursorX), static_cast<float>(cursorY));
    pendingDelta_ = glm::vec2(0.0f);

    mouseCaptured = true;
    NODE_LOG_INFO("[InputNode] Mouse captured for game mode (GLFW_CURSOR_DISABLED)");
}

void InputNode::RecenterMouse() {
    // No-op under GLFW: GLFW_CURSOR_DISABLED recenters internally and reports virtual unbounded
    // motion, so the old Win32 manual SetCursorPos recentering is unnecessary.
}

void InputNode::ClearInputOnFocusLoss() {
    for (auto& [key, state] : keyStates) {
        state.isDown = false;
    }
    buttonDown_[0] = buttonDown_[1] = buttonDown_[2] = false;
    NODE_LOG_INFO("[InputNode] Focus lost - cleared latched key/button state");
}

} // namespace Vixen::RenderGraph

// Self-registration (M3): registrar kept in this TU; RenderGraphNodes is whole-archived so it is not stripped.
VIXEN_REGISTER_NODE(Vixen::RenderGraph::InputNodeType);
