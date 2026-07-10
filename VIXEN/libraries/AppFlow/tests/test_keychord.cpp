#include "generated/AppFlow.g.h"
#include "KeyMap.h"
#include <gtest/gtest.h>
using namespace Vixen::AppFlow::Generated;

// Inc-4 reframe R5a: completeness guard for KeyMap.h's GLFW-keycode -> KeyId map. Every code
// EditorApplication.cpp actually resolves a key off of must map non-None; anything else must
// map to None (caught by AppFlowRuntime::DispatchByKey's RejectedByState, never a silent
// wrong-key match). Codes copied from <GLFW/glfw3.h> per KeyMap.h's header comment.
TEST(KeyChord, GlfwToKeyIdCoversEditorUsage) {
    EXPECT_EQ(Vixen::Editor::GlfwToKeyId(90),  KeyId::Z);       // GLFW_KEY_Z
    EXPECT_EQ(Vixen::Editor::GlfwToKeyId(89),  KeyId::Y);       // GLFW_KEY_Y
    EXPECT_EQ(Vixen::Editor::GlfwToKeyId(83),  KeyId::S);       // GLFW_KEY_S
    EXPECT_EQ(Vixen::Editor::GlfwToKeyId(256), KeyId::Escape);  // GLFW_KEY_ESCAPE
}

TEST(KeyChord, GlfwToKeyIdUnknownMapsToNone) {
    EXPECT_EQ(Vixen::Editor::GlfwToKeyId(0), KeyId::None);
    EXPECT_EQ(Vixen::Editor::GlfwToKeyId(65), KeyId::None);   // GLFW_KEY_A -- unused by the editor today
    EXPECT_EQ(Vixen::Editor::GlfwToKeyId(-1), KeyId::None);
}

TEST(KeyChord, EqualityAndMods) {
    KeyChord a{KeyId::Z, KeyMod::Ctrl};
    KeyChord b{KeyId::Z, KeyMod::Ctrl};
    KeyChord c{KeyId::Z, KeyMod::None};
    EXPECT_TRUE(a.key == b.key && a.mods == b.mods);
    EXPECT_FALSE(a.mods == c.mods);
    // Bitmask composition is order-independent (Ctrl|Shift == Shift|Ctrl).
    auto ctrlShift = static_cast<KeyMod>(static_cast<uint8_t>(KeyMod::Ctrl) | static_cast<uint8_t>(KeyMod::Shift));
    auto shiftCtrl = static_cast<KeyMod>(static_cast<uint8_t>(KeyMod::Shift) | static_cast<uint8_t>(KeyMod::Ctrl));
    EXPECT_EQ(static_cast<uint8_t>(ctrlShift), static_cast<uint8_t>(shiftCtrl));
}
