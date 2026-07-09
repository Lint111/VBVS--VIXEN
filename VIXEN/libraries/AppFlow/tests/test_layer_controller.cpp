#include <gtest/gtest.h>
#include "LayerController.h"
#include "generated/AppFlow.g.h"
using namespace Vixen::AppFlow;
using Vixen::AppFlow::Generated::LayerState;

TEST(LayerController, DefaultsAllEnabled) {
    LayerController lc;
    lc.SetLayerCount(3);
    EXPECT_EQ(lc.LayerCount(), 3u);
    EXPECT_TRUE(lc.IsEnabled(0));
    EXPECT_TRUE(lc.IsEnabled(1));
    EXPECT_TRUE(lc.IsEnabled(2));
    EXPECT_EQ(lc.Mask(), 0b111u);
}

TEST(LayerController, ToggleFlipsBit) {
    LayerController lc; lc.SetLayerCount(3);
    EXPECT_TRUE(lc.Toggle(2));
    EXPECT_FALSE(lc.IsEnabled(2));
    EXPECT_EQ(lc.Mask(), 0b011u);
    EXPECT_TRUE(lc.Toggle(2));
    EXPECT_TRUE(lc.IsEnabled(2));
    EXPECT_EQ(lc.Mask(), 0b111u);
}

TEST(LayerController, OutOfRangeIsNoOp) {
    LayerController lc; lc.SetLayerCount(3);
    EXPECT_FALSE(lc.Toggle(3));      // i >= count → no-op, false
    EXPECT_FALSE(lc.IsEnabled(9));   // out of range → false
    EXPECT_EQ(lc.Mask(), 0b111u);    // unchanged
}

TEST(LayerController, SnapshotRestoreRoundTrips) {
    LayerController lc; lc.SetLayerCount(3);
    lc.Toggle(1);                    // 0b101
    LayerState snap = lc.Snapshot();
    lc.Toggle(0); lc.Toggle(2);      // 0b000
    EXPECT_EQ(lc.Mask(), 0b000u);
    lc.Restore(snap);
    EXPECT_EQ(lc.Mask(), 0b101u);
}
