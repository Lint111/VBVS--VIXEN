#pragma once
// Plain-int GLFW keycode <-> typed AppFlow KeyId, with NO GLFW header dependency (keeps this header
// includable from AppFlow-side tests that don't link GLFW). The codes below are copied from
// <GLFW/glfw3.h>. kMappedKeys lists EVERY declared KeyId except None (a test pins that), so a
// [FlowKeyDefault] on any declared key binds with no host change: the host edge-detects every mapped
// key and resolves the chord through the runtime's InputProfile (DispatchByKey), which is also what a
// hot-reloaded or rebound profile feeds. Unmapped codes resolve to KeyId::None (caught by
// AppFlowRuntime::DispatchByKey's RejectedByState path, never a silent wrong-key match).
#include <array>
#include <cstddef>
#include <cstdint>
#include "generated/AppFlow.g.h"

namespace Vixen::Editor {

using Vixen::AppFlow::Generated::KeyId;
using Vixen::AppFlow::Generated::KeyMod;
using Vixen::AppFlow::Generated::KeyChord;

struct MappedKey {
    int glfw;
    KeyId id;
};

inline constexpr std::array<MappedKey, 62> kMappedKeys = {{
    {65, KeyId::A}, {66, KeyId::B}, {67, KeyId::C}, {68, KeyId::D}, {69, KeyId::E}, {70, KeyId::F},
    {71, KeyId::G}, {72, KeyId::H}, {73, KeyId::I}, {74, KeyId::J}, {75, KeyId::K}, {76, KeyId::L},
    {77, KeyId::M}, {78, KeyId::N}, {79, KeyId::O}, {80, KeyId::P}, {81, KeyId::Q}, {82, KeyId::R},
    {83, KeyId::S}, {84, KeyId::T}, {85, KeyId::U}, {86, KeyId::V}, {87, KeyId::W}, {88, KeyId::X},
    {89, KeyId::Y}, {90, KeyId::Z},
    {256, KeyId::Escape},
    {48, KeyId::Digit0}, {49, KeyId::Digit1}, {50, KeyId::Digit2}, {51, KeyId::Digit3}, {52, KeyId::Digit4},
    {53, KeyId::Digit5}, {54, KeyId::Digit6}, {55, KeyId::Digit7}, {56, KeyId::Digit8}, {57, KeyId::Digit9},
    {290, KeyId::F1}, {291, KeyId::F2}, {292, KeyId::F3}, {293, KeyId::F4}, {294, KeyId::F5}, {295, KeyId::F6},
    {296, KeyId::F7}, {297, KeyId::F8}, {298, KeyId::F9}, {299, KeyId::F10}, {300, KeyId::F11}, {301, KeyId::F12},
    {263, KeyId::Left}, {262, KeyId::Right}, {265, KeyId::Up}, {264, KeyId::Down},
    {261, KeyId::Delete}, {259, KeyId::Backspace}, {258, KeyId::Tab}, {32, KeyId::Space}, {257, KeyId::Enter},
    {268, KeyId::Home}, {269, KeyId::End}, {266, KeyId::PageUp}, {267, KeyId::PageDown},
}};

inline constexpr int kGlfwKeyLeftShift = 340;
inline constexpr int kGlfwKeyLeftControl = 341;
inline constexpr int kGlfwKeyLeftAlt = 342;
inline constexpr int kGlfwKeyLeftSuper = 343;
inline constexpr int kGlfwKeyRightShift = 344;
inline constexpr int kGlfwKeyRightControl = 345;
inline constexpr int kGlfwKeyRightAlt = 346;
inline constexpr int kGlfwKeyRightSuper = 347;

inline KeyId GlfwToKeyId(int glfwKey) {
    for (const auto& mapped : kMappedKeys) {
        if (mapped.glfw == glfwKey) return mapped.id;
    }
    return KeyId::None;
}

// Inverse of GlfwToKeyId; -1 for KeyId::None and for any id the table does not carry.
inline int KeyIdToGlfw(KeyId id) {
    for (const auto& mapped : kMappedKeys) {
        if (mapped.id == id) return mapped.glfw;
    }
    return -1;
}

inline KeyMod ReadMods(bool ctrl, bool shift, bool alt, bool super) {
    uint8_t m = 0;
    if (ctrl) m |= uint8_t(KeyMod::Ctrl);
    if (shift) m |= uint8_t(KeyMod::Shift);
    if (alt) m |= uint8_t(KeyMod::Alt);
    if (super) m |= uint8_t(KeyMod::Super);
    return static_cast<KeyMod>(m);
}

// Live modifier state from any "is this GLFW key down" predicate.
template <class IsDown>
KeyMod ReadLiveMods(IsDown&& isDown) {
    return ReadMods(isDown(kGlfwKeyLeftControl) || isDown(kGlfwKeyRightControl),
                    isDown(kGlfwKeyLeftShift) || isDown(kGlfwKeyRightShift),
                    isDown(kGlfwKeyLeftAlt) || isDown(kGlfwKeyRightAlt),
                    isDown(kGlfwKeyLeftSuper) || isDown(kGlfwKeyRightSuper));
}

// Rising-edge detector over every mapped key (press only, never held-repeat). Poll() calls
// onPress(KeyChord{key, liveMods}) once per key that went down since the previous Poll; the caller
// routes the chord through the runtime's InputProfile, so which chords do anything is declaration
// data, not host code.
class KeyEdgeTracker {
public:
    template <class IsDown, class OnPress>
    void Poll(IsDown&& isDown, OnPress&& onPress) {
        const KeyMod mods = ReadLiveMods(isDown);
        for (size_t i = 0; i < kMappedKeys.size(); ++i) {
            const bool down = isDown(kMappedKeys[i].glfw);
            if (down && !wasDown_[i]) onPress(KeyChord{kMappedKeys[i].id, mods});
            wasDown_[i] = down;
        }
    }

private:
    std::array<bool, kMappedKeys.size()> wasDown_{};
};

}  // namespace Vixen::Editor
