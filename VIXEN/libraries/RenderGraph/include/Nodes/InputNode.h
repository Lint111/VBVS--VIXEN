#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Data/Nodes/InputNodeConfig.h"
#include "InputEvents.h"
#include <unordered_map>
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
 * @brief Input polling node that publishes keyboard/mouse events to EventBus
 *
 * Polls Win32 input state in ExecuteImpl and publishes per-frame events:
 * - KeyPressed: Key went down this frame
 * - KeyHeld: Key is down (includes duration)
 * - KeyReleased: Key went up this frame
 * - KeyClicked: Key was pressed and released within same frame (future)
 * - MouseMoveEvent: Mouse moved since last frame
 * - MouseButtonEvent: Mouse button state changed
 *
 * Uses per-frame state tracking (quantized to frame boundaries).
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

    // Poll GLFW input state
    void PollKeyboard();
    void PollMouse();

    // Publish events for state changes
    void PublishKeyEvents();
    void PublishMouseEvents();

    // Modern polling interface
    void PopulateInputState();

    // Check if key is currently down (GLFW glfwGetKey)
    bool IsKeyDown(EventBus::KeyCode key) const;

    // Get modifier state
    bool IsShiftPressed() const;
    bool IsCtrlPressed() const;
    bool IsAltPressed() const;

    // Window handle for input context (cross-platform GLFW handle)
    GLFWwindow* window = nullptr;

    // Key state tracking (only track keys we care about)
    std::unordered_map<EventBus::KeyCode, KeyState> keyStates;

    // Configuration parameters
    bool enabled_ = true;   // Enable/disable input polling
    InputConfig config_;    // cursorMode/orbitButton mirrored live from mouse_capture_mode/orbit_button params

    // Mouse state
    int32_t lastMouseX = 0;
    int32_t lastMouseY = 0;
    bool firstMousePoll = true;
    bool mouseCaptured = false;  // Track if mouse is captured for game mode

    // Previous-frame button state for edge detection in PublishMouseEvents()
    // ([0]=left, [1]=right, [2]=middle — matches InputState::mouseButtons).
    bool lastMouseButtonState_[3] = {false, false, false};

    // Delta time for held duration calculation
    std::chrono::steady_clock::time_point lastFrameTime;
    float deltaTime = 0.0f;

    // Modern polling interface (GLFW/SDL2 style)
    InputState inputState_;  // Updated once per frame, output to consumers
};

} // namespace Vixen::RenderGraph
