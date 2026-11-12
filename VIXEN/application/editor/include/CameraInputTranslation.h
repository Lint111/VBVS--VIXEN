#pragma once
// R765 camera input -> AppFlow camera actions. InputProfile binds chords, not axes or drags (AppFlow gap), so the
// host turns the raw per-frame InputState into OrbitCamera/PanCamera/ZoomCamera dispatches here. Pure (no graph, no
// runtime): the editor dispatches the result, a test feeds it synthetic InputState.
#include "Data/InputState.h"
#include "generated/AppFlow.g.h"
#include <algorithm>
#include <cstddef>
#include <vector>

namespace Vixen::Editor {

struct CameraDispatch {
    Vixen::AppFlow::Generated::FlowActionId action;
    float a = 0.0f;  // OrbitCamera dYaw | PanCamera dx | ZoomCamera delta
    float b = 0.0f;  // OrbitCamera dPitch | PanCamera dy
};

// World units per second of a full forward axis (W/S zoom, as they always did) and pixel-equivalents per second of
// a full arrow-key axis.
inline constexpr float kCameraKeyZoomSpeed = 100.0f;
inline constexpr float kCameraArrowLookSpeed = 120.0f;

// sensitivity: radians per pixel; moveSpeed: world units per second. ctrlHeld suppresses the held-key axes so the
// Ctrl chords (Ctrl+S/Z/Y/R) never also move the camera.
inline std::vector<CameraDispatch> CameraDispatchesFromInput(const Vixen::RenderGraph::InputState& state,
                                                             bool ctrlHeld, float sensitivity, float moveSpeed) {
    using Vixen::AppFlow::Generated::FlowActionId;
    using Vixen::RenderGraph::InputAxis;
    std::vector<CameraDispatch> out;
    const float dt = std::min(state.deltaTime, 0.1f);

    // Right-mouse orbit: pixels become radians; screen-down is pitch-down.
    if (state.mouseButtons[1] && (state.mouseDelta.x != 0.0f || state.mouseDelta.y != 0.0f)) {
        out.push_back({FlowActionId::OrbitCamera, state.mouseDelta.x * sensitivity, -state.mouseDelta.y * sensitivity});
    }
    if (state.wheelZoom && state.wheelDelta.y != 0.0f) {
        out.push_back({FlowActionId::ZoomCamera, state.wheelDelta.y * state.wheelZoomSpeed, 0.0f});
    }
    if (ctrlHeld) return out;

    const auto axis = [&state](InputAxis a) { return state.axes[static_cast<std::size_t>(a)]; };
    if (const float forward = axis(InputAxis::MoveForward); forward != 0.0f) {
        out.push_back({FlowActionId::ZoomCamera, forward * kCameraKeyZoomSpeed * dt, 0.0f});
    }
    const float horizontal = axis(InputAxis::MoveHorizontal);
    const float vertical = axis(InputAxis::MoveUp);
    if (horizontal != 0.0f || vertical != 0.0f) {
        out.push_back({FlowActionId::PanCamera, horizontal * moveSpeed * dt, vertical * moveSpeed * dt});
    }
    const float lookH = axis(InputAxis::LookHorizontal);
    const float lookV = axis(InputAxis::LookVertical);
    if (lookH != 0.0f || lookV != 0.0f) {
        out.push_back({FlowActionId::OrbitCamera, lookH * kCameraArrowLookSpeed * dt * sensitivity,
                       lookV * kCameraArrowLookSpeed * dt * sensitivity});
    }
    return out;
}

}  // namespace Vixen::Editor
