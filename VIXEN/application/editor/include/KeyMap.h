#pragma once
// Inc-4 reframe R5a: plain-int GLFW keycode -> typed AppFlow KeyId, with NO GLFW header
// dependency (keeps this header includable from AppFlow-side tests that don't link GLFW).
// Values below are copied from <GLFW/glfw3.h> (verified against the SDK's glfw3.h) rather
// than including it: GLFW_KEY_S=83, GLFW_KEY_Y=89, GLFW_KEY_Z=90, GLFW_KEY_ESCAPE=256 --
// exactly the four codes EditorApplication.cpp's input block resolves (Save / Redo / Undo /
// Return). Unmapped codes resolve to KeyId::None (caught by AppFlowRuntime::DispatchByKey's
// RejectedByState path, never a silent wrong-key match).
#include "generated/AppFlow.g.h"

#include <magic_enum.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Vixen::Editor {

using Vixen::AppFlow::Generated::KeyId;
using Vixen::AppFlow::Generated::KeyMod;

inline KeyId GlfwToKeyId(int glfwKey) {
    switch (glfwKey) {
        case 90:  return KeyId::Z;       // GLFW_KEY_Z
        case 89:  return KeyId::Y;       // GLFW_KEY_Y
        case 83:  return KeyId::S;       // GLFW_KEY_S
        case 82:  return KeyId::R;       // GLFW_KEY_R (R766 ResetLayout)
        case 256: return KeyId::Escape;  // GLFW_KEY_ESCAPE
        default:  return KeyId::None;
    }
}

inline KeyMod ReadMods(bool ctrl, bool shift, bool alt, bool super) {
    uint8_t m = 0;
    if (ctrl) m |= uint8_t(KeyMod::Ctrl);
    if (shift) m |= uint8_t(KeyMod::Shift);
    if (alt) m |= uint8_t(KeyMod::Alt);
    if (super) m |= uint8_t(KeyMod::Super);
    return static_cast<KeyMod>(m);
}

// One chord-modifier grammar for the agent channel: `appflow.list` reports a chord's mods and
// `appflow.dispatch` accepts them as an array of generated KeyMod names ("Ctrl", "Shift", ...), so a
// listed chord can be dispatched as listed.
inline std::vector<std::string> ChordModNames(KeyMod mods) {
    std::vector<std::string> names;
    for (const KeyMod mod : magic_enum::enum_values<KeyMod>()) {
        if (mod != KeyMod::None && (uint8_t(mods) & uint8_t(mod)) != 0) {
            names.emplace_back(magic_enum::enum_name(mod));
        }
    }
    return names;
}

// False when a name is not a generated KeyMod.
inline bool ParseChordMods(const std::vector<std::string>& names, KeyMod& out) {
    uint8_t bits = 0;
    for (const std::string& name : names) {
        const auto mod = magic_enum::enum_cast<KeyMod>(name);
        if (!mod) return false;
        bits |= uint8_t(*mod);
    }
    out = static_cast<KeyMod>(bits);
    return true;
}

}  // namespace Vixen::Editor
