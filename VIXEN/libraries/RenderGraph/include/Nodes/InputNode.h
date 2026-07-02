#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Data/Nodes/InputNodeConfig.h"
#include "InputEvents.h"
#include <unordered_map>
#include <vector>
#include <mutex>
#include <chrono>

struct GLFWwindow;  // GLFW/glfw3.h is included in the .cpp; the header only needs the handle type.

namespace Vixen::RenderGraph {

/// Application-facing input policy (spec 2026-07-02-input-rework-slice1). Owned by InputNode;
/// set at graph build via SetInputConfig, key fields mirrored as node params for live tuning
/// (the runtime console reaches them via setparam input_handler ...).
struct InputConfig {
    enum class CursorMode  : uint8_t { Normal = 0, Hidden = 1, CenterLock = 2 };
    enum class OrbitButton : uint8_t { RightMouse = 0, LeftDrag = 1, Always = 2 /*legacy*/ };
    CursorMode  cursorMode      = CursorMode::Normal;      // V1 fix: visible OS cursor by default
    OrbitButton orbitButton     = OrbitButton::RightMouse; // V2 fix (consumed by CameraNode in M4)
    float       dragThresholdPx = 4.0f;   // in-press motion below this stays a "click"
    bool        wheelZoom       = true;   // scroll drives orbit distance (M4)
    float       wheelZoomSpeed  = 2.0f;   // world units per notch
};

/**
 * @brief Node type for input handling
 */
class InputNodeType : public TypedNodeType<InputNodeConfig> {
public:
    InputNodeType(const std::string& typeName = "Input")
        : TypedNodeType<InputNodeConfig>(typeName) {}
    virtual ~InputNodeType() = default;

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& instanceName) const override;
};

/**
 * @brief Event-based input node (input-rework slice 1): GLFW callbacks -> a mutex-guarded queue
 * -> ProcessPendingInput() drains + folds into canonical state, called unconditionally from
 * VulkanGraphApplication::Update() every frame regardless of render-graph pause/recompile state.
 * ExecuteImpl only copies that already-current canonical state into the frame's InputState
 * output (poll-shaped for existing consumers) and retires the per-frame accumulators.
 *
 * Publishes per-frame bus events with real payloads: MouseButtonEvent (press/release),
 * MouseScrollEvent, WindowCloseEvent (ESC). InputState.clicksThisFrame carries every press+
 * release edge since the last Execute (a same-frame press+release is two entries, not a
 * collapsed non-edge — the fix for the old single-poll's click-loss failure mode).
 */
class InputNode : public TypedNode<InputNodeConfig> {
public:
    InputNode(
        const std::string& instanceName,
        NodeType* nodeType
    );
    ~InputNode() override = default;

    /// Get the current input state (updated each frame)
    const InputState& GetInputState() const { return inputState_; }

    /// Set the application-facing input policy (graph-build time). Re-applies the cursor mode
    /// immediately if the window already exists (SetInputConfig can run after CompileImpl too).
    void SetInputConfig(const InputConfig& config);
    const InputConfig& Config() const { return config_; }

    /// Drain the GLFW-callback event queue and fold it into canonical state (cursor/buttons/wheel/
    /// keys) + clicksThisFrame, independent of node Execute(). Mirrors WindowNode::
    /// ProcessPendingEvents() (VIXEN's shipped "input never rides the render graph's gates"
    /// idiom): called unconditionally from VulkanGraphApplication::Update(), so events still
    /// accumulate while the render graph is paused/recompiling instead of being lost. ExecuteImpl
    /// copies the drained state into the frame's InputState output and clears the per-frame
    /// accumulators AFTER the copy (see ExecuteImpl's retention-rule comment) — so calling this
    /// with no pending events, or calling it more than once before the next Execute, is safe:
    /// events keep accumulating into the same canonical state either way.
    void ProcessPendingInput();

protected:
    void SetupImpl(TypedSetupContext& ctx) override;
    void CompileImpl(TypedCompileContext& ctx) override;
    void ExecuteImpl(TypedExecuteContext& ctx) override;
    void CleanupImpl(TypedCleanupContext& ctx) override;

private:
    // Per-key state for tracking held duration
    struct KeyState {
        bool wasDown = false;  // State last frame
        bool isDown = false;   // State this frame
        std::chrono::steady_clock::time_point pressTime;  // When key was first pressed
    };

    // One raw GLFW callback observation, queued for the drain (ProcessPendingInput). Scroll uses
    // x/y as offsets; cursor-pos uses x/y as the new absolute position; button/key use
    // buttonOrKey+action (GLFW_PRESS/GLFW_RELEASE) and leave x/y at 0.
    struct InputEvent {
        enum class Type : uint8_t { MouseButton, CursorPos, Scroll, Key };
        Type type;
        int buttonOrKey = 0;
        int action = 0;
        double x = 0.0;
        double y = 0.0;
    };

    // Helper methods
    void UpdateDeltaTime();
    void InitializeMouseCapture();
    void RecenterMouse();

    // Re-applies config_.cursorMode via glfwSetInputMode (idempotent; no-op if window is null).
    void ApplyCursorMode();
    // Per-frame live-param sync: mouse_capture_mode/orbit_button can change between frames via
    // SetParameter (no callback exists on NodeParameterManager — see InputConfig doc comment),
    // so ExecuteImpl re-reads them and folds any change into config_ before polling.
    void SyncConfigFromParams();

    // Registers the 4 GLFW input callbacks + GLFW_STICKY_MOUSE_BUTTONS/GLFW_STICKY_KEYS on
    // `window`. Called once per CompileImpl (idempotent: GLFW replaces a callback set twice, and
    // the static registry entry below is re-pointed at `this` on every call — safe across
    // recompiles, including window re-entry after a graph rebuild targeting a new instance).
    void RegisterCallbacks();

    // Static GLFW trampolines (GLFW callbacks are free functions/static; WindowNode already owns
    // glfwSetWindowUserPointer for its own 4 callbacks — see the .cpp's WindowToInputNodeRegistry
    // for why these route through a SEPARATE map instead of sharing that user pointer).
    static void OnMouseButton(GLFWwindow* w, int button, int action, int mods);
    static void OnCursorPos(GLFWwindow* w, double x, double y);
    static void OnScroll(GLFWwindow* w, double xoffset, double yoffset);
    static void OnKey(GLFWwindow* w, int key, int scancode, int action, int mods);

    // Push one event onto pendingInput_ under eventMutex_ (shared body for the 4 trampolines).
    void QueueEvent(const InputEvent& event);

    // Fold one drained InputEvent into canonical state (cursor/buttons/wheel/keys) + append to
    // pendingClicks_ / publish the bus event, if applicable. Called by ProcessPendingInput() once
    // per drained event, in fold (== fire) order, so a click's recorded position is the cursor
    // position AT that press, not the end-of-frame position.
    void FoldEvent(const InputEvent& event);

    // Copies the canonical drained state (+ pendingClicks_) into inputState_ and publishes the
    // real-payload bus events for ANY edges since the last Execute (ExecuteImpl's ProcessedThisFrame
    // step — see .cpp). Kept out of ExecuteImpl's body only so the "what gets copied" list has one
    // definition site next to FoldEvent.
    void PopulateInputState();

    // Window handle for input context (cross-platform GLFW handle)
    GLFWwindow* window = nullptr;

    // Key state tracking (only track keys we care about)
    std::unordered_map<EventBus::KeyCode, KeyState> keyStates;

    // Configuration parameters
    bool enabled_ = true;   // Enable/disable input polling
    InputConfig config_;    // cursorMode/orbitButton mirrored live from mouse_capture_mode/orbit_button params

    // --- Event queue (GLFW callback thread... in practice GLFW callbacks fire synchronously
    // inside glfwPollEvents() on the main thread, but the mutex is cheap insurance + matches the
    // WindowNode::pendingEvents idiom this mirrors) ---
    std::vector<InputEvent> pendingInput_;
    std::mutex eventMutex_;

    // --- Canonical state, event-derived (updated by FoldEvent via ProcessPendingInput) ---
    glm::vec2 cursorPos_{0.0f};           // Last known absolute cursor position
    bool firstCursorEvent_ = true;        // First CursorPos event seeds cursorPos_ without a delta
    bool buttonDown_[3] = {false, false, false};  // [0]=left,[1]=right,[2]=middle — canonical down state
    bool mouseCaptured = false;  // Track if mouse is captured for game mode (CenterLock)

    // --- Per-frame accumulators: filled by FoldEvent, copied into inputState_ by
    // PopulateInputState, THEN CLEARED — see ExecuteImpl's retention-rule comment for why the
    // clear happens after the copy (so events landing during a gated/unexecuted frame are not
    // lost, they just ride into the next Execute's copy instead of vanishing). ---
    glm::vec2 pendingDelta_{0.0f};
    glm::vec2 pendingScroll_{0.0f};
    std::vector<ClickEvent> pendingClicks_;

    // Delta time for held duration calculation
    std::chrono::steady_clock::time_point lastFrameTime;
    float deltaTime = 0.0f;

    // Modern polling interface (GLFW/SDL2 style)
    InputState inputState_;  // Updated once per frame, output to consumers
};

} // namespace Vixen::RenderGraph
