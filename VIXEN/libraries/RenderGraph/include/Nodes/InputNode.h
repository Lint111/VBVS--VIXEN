#pragma once

#include "Core/TypedNodeInstance.h"
#include "Core/NodeType.h"
#include "Data/Nodes/InputNodeConfig.h"
#include "InputEvents.h"
#include <Windows.h>
#include <unordered_map>
#include <chrono>

namespace Vixen::RenderGraph {

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

    // Poll Win32 input state
    void PollKeyboard();
    void PollMouse();

    // Publish events for state changes
    void PublishKeyEvents();
    void PublishMouseEvents();

    // Modern polling interface
    void PopulateInputState();

    // Check if key is currently down (Win32 GetAsyncKeyState)
    bool IsKeyDown(EventBus::KeyCode key) const;

    // Get modifier state
    bool IsShiftPressed() const;
    bool IsCtrlPressed() const;
    bool IsAltPressed() const;

    // Window handle for input context
    HWND hwnd = nullptr;

    // Key state tracking (only track keys we care about)
    std::unordered_map<EventBus::KeyCode, KeyState> keyStates;

    // Mouse state
    int32_t lastMouseX = 0;
    int32_t lastMouseY = 0;
    bool firstMousePoll = true;
    bool mouseCaptured = false;  // Track if mouse is captured for game mode

    // Delta time for held duration calculation
    std::chrono::steady_clock::time_point lastFrameTime;
    float deltaTime = 0.0f;

    // Modern polling interface (GLFW/SDL2 style)
    InputState inputState_;  // Updated once per frame, output to consumers
};

} // namespace Vixen::RenderGraph
