// test_frame_sync_node_timeline.cpp — auto-sync P5a M1
// Pure CPU test of the FrameSyncNode timeline-base arithmetic.
// No Vulkan device required (NextFrameBase is a constexpr free function).

#include <gtest/gtest.h>
#include <cstdint>
#include <set>

// Helper under test: NextFrameBase is a constexpr free function in Vixen::RenderGraph namespace,
// declared in FrameSyncNode.h alongside the FrameSyncNode class.
#include "Nodes/FrameSyncNode.h"

using Vixen::RenderGraph::NextFrameBase;

// ---------------------------------------------------------------------------
// BaseAdvancesByStrideAndNeverCollidesWithin4Frames
//
// Simulates 8 consecutive frames with stride=3 (3 submit-groups/frame).
// Each frame allocates timeline values [base, base+stride).
// Asserts:
//   1. base advances by stride every frame (monotonic).
//   2. No (base + offset) value is ever reused across the 8 frames
//      (no ring collision: the values form a strictly monotonic sequence).
// ---------------------------------------------------------------------------
TEST(FrameSyncTimeline, BaseAdvancesByStrideAndNeverCollidesWithin4Frames) {
    const uint64_t stride = 3;  // e.g. 3 submit groups per frame
    uint64_t base = 0;
    std::set<uint64_t> seen;

    for (int f = 0; f < 8; ++f) {
        for (uint64_t o = 0; o < stride; ++o) {
            // Every (base + offset) used in this frame must be globally unique.
            EXPECT_TRUE(seen.insert(base + o).second)
                << "Collision at frame=" << f << " offset=" << o
                << " value=" << (base + o);
        }
        base = NextFrameBase(base, stride);
    }

    // After 8 frames, base should equal stride * 8.
    EXPECT_EQ(base, stride * 8);
}

// ---------------------------------------------------------------------------
// ZeroStrideHoldsBase
//
// When no edges are baked yet, timelineValuesPerFrame == 0 and the base
// must remain unchanged (no-op advance, no collision possible).
// ---------------------------------------------------------------------------
TEST(FrameSyncTimeline, ZeroStrideHoldsBase) {
    EXPECT_EQ(NextFrameBase(5, 0), 5u);   // zero stride -> base unchanged
    EXPECT_EQ(NextFrameBase(0, 0), 0u);   // starts at zero, stays at zero
}
