#include "generated/AppFlow.g.h"
#include <gtest/gtest.h>
using namespace Vixen::AppFlow::Generated;

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
