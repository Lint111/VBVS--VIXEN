#include "generated/AppFlow.g.h"
#include "KeyMap.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <span>
#include <vector>
#include "AppFlowRuntime.h"
using namespace Vixen::AppFlow;
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
    EXPECT_EQ(Vixen::Editor::GlfwToKeyId(-1), KeyId::None);
    EXPECT_EQ(Vixen::Editor::GlfwToKeyId(302), KeyId::None);  // GLFW_KEY_F13 -- not declared
    EXPECT_EQ(Vixen::Editor::GlfwToKeyId(340), KeyId::None);  // modifier keys are chord mods, never a KeyId
}

// E13: every declared KeyId except None is reachable from a GLFW code, and the map is a bijection, so a
// [FlowKeyDefault] on ANY declared key can bind without touching the host. Walks the generated enum's value
// range: the last declared id is PageDown, so growing KeyId without extending kMappedKeys fails here.
TEST(KeyChord, EveryDeclaredKeyIdIsMappedExactlyOnce) {
    constexpr uint16_t kLastDeclared = static_cast<uint16_t>(KeyId::PageDown);
    EXPECT_EQ(Vixen::Editor::kMappedKeys.size(), kLastDeclared);  // ids 1..PageDown, no gaps
    for (uint16_t raw = 1; raw <= kLastDeclared; ++raw) {
        const KeyId id = static_cast<KeyId>(raw);
        const int glfw = Vixen::Editor::KeyIdToGlfw(id);
        ASSERT_GE(glfw, 0) << "KeyId " << raw << " has no GLFW code in KeyMap.h";
        EXPECT_EQ(Vixen::Editor::GlfwToKeyId(glfw), id) << "round trip failed for KeyId " << raw;
    }
    EXPECT_EQ(Vixen::Editor::KeyIdToGlfw(KeyId::None), -1);
    // GLFW codes are unique.
    for (size_t i = 0; i < Vixen::Editor::kMappedKeys.size(); ++i)
        for (size_t j = i + 1; j < Vixen::Editor::kMappedKeys.size(); ++j)
            EXPECT_NE(Vixen::Editor::kMappedKeys[i].glfw, Vixen::Editor::kMappedKeys[j].glfw);
    EXPECT_EQ(Vixen::Editor::GlfwToKeyId(49), KeyId::Digit1);
    EXPECT_EQ(Vixen::Editor::GlfwToKeyId(294), KeyId::F5);
    EXPECT_EQ(Vixen::Editor::GlfwToKeyId(265), KeyId::Up);
    EXPECT_EQ(Vixen::Editor::GlfwToKeyId(32), KeyId::Space);
}

// Every generated key default and return edge names a mapped key, so the host's edge poll reaches it.
TEST(KeyChord, EveryGeneratedDefaultChordIsReachableFromAGlfwKey) {
    for (const auto& def : kKeyDefaults) EXPECT_GE(Vixen::Editor::KeyIdToGlfw(def.chord.key), 0) << def.id;
    for (const auto& edge : kReturnEdges) EXPECT_GE(Vixen::Editor::KeyIdToGlfw(edge.trigger.key), 0) << edge.id;
}

// The host edge detector: press fires once with the live modifiers, a held key never repeats, release re-arms.
TEST(KeyChord, EdgeTrackerFiresOncePerPressWithLiveMods) {
    Vixen::Editor::KeyEdgeTracker tracker;
    std::vector<int> down;
    const auto isDown = [&down](int key) { return std::find(down.begin(), down.end(), key) != down.end(); };
    std::vector<KeyChord> fired;
    const auto onPress = [&fired](KeyChord chord) { fired.push_back(chord); };

    tracker.Poll(isDown, onPress);
    EXPECT_TRUE(fired.empty());
    down = {Vixen::Editor::kGlfwKeyLeftControl, 83};  // Ctrl+S
    tracker.Poll(isDown, onPress);
    ASSERT_EQ(fired.size(), 1u);
    EXPECT_EQ(fired[0].key, KeyId::S);
    EXPECT_EQ(fired[0].mods, KeyMod::Ctrl);
    tracker.Poll(isDown, onPress);  // held: no repeat
    EXPECT_EQ(fired.size(), 1u);
    down.clear();
    tracker.Poll(isDown, onPress);
    down = {83};  // plain S after release: a fresh press, with NO modifiers
    tracker.Poll(isDown, onPress);
    ASSERT_EQ(fired.size(), 2u);
    EXPECT_EQ(fired[1].mods, KeyMod::None);
}

// E13 end to end, no host change: Save is Ctrl+S (plain S dispatches nothing), and a key default that exists
// ONLY in a test-built container (F7+Shift -> Redo) is bound by the runtime and reached through the same
// poll the editor host runs.
TEST(KeyChord, HostPollBindsDeclaredDefaultsAndANewTestDefaultWithNoHostChange) {
    static constexpr AppFlowKeyDefault kTestDefault{"TestF7Redo", FlowActionId::Redo,
                                                    {KeyId::F7, KeyMod::Shift}, FlowScope::Global, FlowStateId{}};
    std::vector<AppFlowKeyDefault> defaults(std::begin(kKeyDefaults), std::end(kKeyDefaults));
    defaults.push_back(kTestDefault);
    const AppFlowContainerView view(kActionDecls, kTransitions, kElementTriggers,
                                    std::span<const AppFlowKeyDefault>(defaults), kReturnEdges);
    AppFlowRuntime rt(nullptr, 1);
    ASSERT_EQ(rt.Load(&view), LoadResult::Ok);
    int saves = 0, redos = 0;
    rt.RegisterHandler(FlowActionId::Save, [&](const AppFlowRuntime::Params&) { ++saves; });
    rt.RegisterHandler(FlowActionId::Redo, [&](const AppFlowRuntime::Params&) { ++redos; });

    Vixen::Editor::KeyEdgeTracker tracker;
    std::vector<int> down;
    const auto isDown = [&down](int key) { return std::find(down.begin(), down.end(), key) != down.end(); };
    const auto onPress = [&rt](KeyChord chord) { (void)rt.DispatchByKey(chord); };
    const auto press = [&](std::vector<int> keys) {
        down = std::move(keys);
        tracker.Poll(isDown, onPress);
        down.clear();
        tracker.Poll(isDown, onPress);
    };

    press({83});                                         // plain S
    EXPECT_EQ(saves, 0);
    press({Vixen::Editor::kGlfwKeyRightControl, 83});    // Ctrl+S (right ctrl)
    EXPECT_EQ(saves, 1);
    press({Vixen::Editor::kGlfwKeyLeftShift, 296});      // Shift+F7 (GLFW_KEY_F7 = 296)
    EXPECT_EQ(redos, 1);
    press({296});                                        // F7 without Shift: unbound
    EXPECT_EQ(redos, 1);
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

// E-channel chord grammar: `appflow.list` emits a chord's mods with ChordModNames and
// `appflow.dispatch` reads them back with ParseChordMods, so every listed chord round-trips.
TEST(KeyChord, ListedModsRoundTripThroughDispatchGrammar) {
    for (const auto& binding : kKeyDefaults) {
        const std::vector<std::string> names = Vixen::Editor::ChordModNames(binding.chord.mods);
        KeyMod parsed = KeyMod::Alt;  // must be overwritten, including by None
        ASSERT_TRUE(Vixen::Editor::ParseChordMods(names, parsed));
        EXPECT_EQ(static_cast<uint8_t>(parsed), static_cast<uint8_t>(binding.chord.mods));
    }
}

TEST(KeyChord, ModNamesAreGeneratedNamesNotIntegers) {
    const auto ctrlShift = static_cast<KeyMod>(static_cast<uint8_t>(KeyMod::Ctrl) | static_cast<uint8_t>(KeyMod::Shift));
    EXPECT_EQ(Vixen::Editor::ChordModNames(KeyMod::None), std::vector<std::string>{});
    EXPECT_EQ(Vixen::Editor::ChordModNames(ctrlShift), (std::vector<std::string>{"Ctrl", "Shift"}));
    KeyMod parsed = KeyMod::None;
    EXPECT_TRUE(Vixen::Editor::ParseChordMods({"Shift", "Ctrl"}, parsed));
    EXPECT_EQ(static_cast<uint8_t>(parsed), static_cast<uint8_t>(ctrlShift));
    EXPECT_FALSE(Vixen::Editor::ParseChordMods({"Hyper"}, parsed));
    EXPECT_FALSE(Vixen::Editor::ParseChordMods({"1"}, parsed));
}

// sn-rmlinput1 focus rule: while a UI text control owns the keyboard the edge poll still advances but
// dispatches nothing, so typing "s" (or Ctrl+S inside a field) never acts as a binding, and a key held across the
// end of the edit does not fire late.
TEST(KeyChord, TypingSuppressesChordsAndAKeyHeldAcrossTheEditDoesNotFireLate) {
    Vixen::Editor::KeyEdgeTracker tracker;
    std::vector<int> down;
    const auto isDown = [&down](int key) { return std::find(down.begin(), down.end(), key) != down.end(); };
    std::vector<KeyChord> fired;
    const auto onPress = [&fired](KeyChord chord) { fired.push_back(chord); };

    down = {83};  // "s" typed into a focused field
    tracker.Poll(isDown, onPress, /*typing=*/true);
    EXPECT_TRUE(fired.empty());
    down = {Vixen::Editor::kGlfwKeyLeftControl, 83};  // Ctrl+S inside the field
    tracker.Poll(isDown, onPress, true);
    EXPECT_TRUE(fired.empty());
    tracker.Poll(isDown, onPress, /*typing=*/false);  // field blurred with S still held: not a new press
    EXPECT_TRUE(fired.empty());
    down.clear();
    tracker.Poll(isDown, onPress, false);
    down = {Vixen::Editor::kGlfwKeyLeftControl, 83};  // a real press with no field focused fires
    tracker.Poll(isDown, onPress, false);
    ASSERT_EQ(fired.size(), 1u);
    EXPECT_EQ(fired[0].key, KeyId::S);
    EXPECT_EQ(fired[0].mods, KeyMod::Ctrl);
}
