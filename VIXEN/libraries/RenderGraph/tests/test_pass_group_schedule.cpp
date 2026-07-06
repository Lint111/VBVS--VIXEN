// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#include <gtest/gtest.h>
#include "Core/PassGroupSchedule.h"
#include "Data/PassStep.h"

using namespace Vixen::RenderGraph;

// BuildScheduleFromTimelines only pointer-compares Resource*, never dereferences it,
// so sentinel pointers are valid test fixtures.
static const Resource* kBufA = reinterpret_cast<const Resource*>(0x1);
static const Resource* kBufB = reinterpret_cast<const Resource*>(0x2);

static ComputePassStep Comp(std::vector<PassResourceAccess> a) {
    ComputePassStep s; s.accesses = std::move(a); return s;
}

TEST(PassGroupSchedule, ComputeWriteThenComputeReadEmitsOneBufferBarrier) {
    std::vector<PassStep> passes = {
        Comp({{kBufA, AccessKind::ComputeStorageWrite, false}}),
        Comp({{kBufA, AccessKind::ComputeStorageRead,  false}}),
    };
    FrameSyncSchedule s = BuildPassGroupSchedule(passes);
    ASSERT_TRUE(s.valid);
    ASSERT_EQ(s.groups.size(), 2u);
    EXPECT_TRUE(s.groups[0].entryBarriers.empty());
    ASSERT_EQ(s.groups[1].entryBarriers.size(), 1u);
    EXPECT_EQ(s.groups[1].entryBarriers[0].src.access, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    EXPECT_EQ(s.groups[1].entryBarriers[0].dst.access, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    EXPECT_FALSE(s.groups[1].entryBarriers[0].isImage);
}

TEST(PassGroupSchedule, ComputeToFragmentReadAcrossRenderStep) {
    RenderPassStep r; r.accesses = {{kBufA, AccessKind::FragmentStorageRead, false}};
    std::vector<PassStep> passes = {
        Comp({{kBufA, AccessKind::ComputeStorageWrite, false}}),
        r,
    };
    FrameSyncSchedule s = BuildPassGroupSchedule(passes);
    ASSERT_EQ(s.groups.size(), 2u);
    ASSERT_EQ(s.groups[1].entryBarriers.size(), 1u);
    EXPECT_EQ(s.groups[1].entryBarriers[0].dst.stage, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
}

TEST(PassGroupSchedule, IndependentResourcesNoBarrier) {
    std::vector<PassStep> passes = {
        Comp({{kBufA, AccessKind::ComputeStorageWrite, false}}),
        Comp({{kBufB, AccessKind::ComputeStorageWrite, false}}),
    };
    FrameSyncSchedule s = BuildPassGroupSchedule(passes);
    ASSERT_EQ(s.groups.size(), 2u);
    EXPECT_TRUE(s.groups[1].entryBarriers.empty());
}

// ---------------------------------------------------------------------------
// Surface-Shell ESVO cache — the double-buffer no-false-edge guarantee.
//
// Models the exact live scenario: the ray-march RENDER reads the CURRENT shell
// slot N (shellData[0], grid remap lookup[0]); the ShellRevalidate COMPUTE writes
// the NEXT slot N+1 (shellData[1]) while READING the shared full-interior SOURCE
// pool. Because slot N and slot N+1 are DISTINCT Resource objects, and the render
// never touches the source pool (it reads only the compact shell), there is ZERO
// barrier between the render pass and the revalidate pass — they schedule
// concurrently (no false hazard edge). This is the parallelism claim, proven.
// ---------------------------------------------------------------------------
TEST(PassGroupSchedule, ShellDoubleBufferRenderAndRevalidateNoBarrier) {
    static const Resource* kShellSlotN   = reinterpret_cast<const Resource*>(0x10); // read by render
    static const Resource* kShellLookupN = reinterpret_cast<const Resource*>(0x11); // read by render
    static const Resource* kShellSlotN1  = reinterpret_cast<const Resource*>(0x20); // written by revalidate
    static const Resource* kSourcePool   = reinterpret_cast<const Resource*>(0x30); // read by revalidate only

    RenderPassStep render;
    render.accesses = {
        { kShellSlotN,   AccessKind::ComputeStorageRead, false },
        { kShellLookupN, AccessKind::ComputeStorageRead, false },
    };
    ComputePassStep revalidate = Comp({
        { kSourcePool,  AccessKind::ComputeStorageRead,  false },
        { kShellSlotN1, AccessKind::ComputeStorageWrite, false },
    });

    // Order render-first then revalidate (as the live frame would record them).
    std::vector<PassStep> passes = { render, revalidate };
    FrameSyncSchedule s = BuildPassGroupSchedule(passes);
    ASSERT_TRUE(s.valid);
    ASSERT_EQ(s.groups.size(), 2u);

    // The revalidate group (index 1) must have NO entry barrier: it shares no
    // resource with the render (render's slot-N buffers are distinct objects from
    // the revalidate's slot-N+1 write and its source read).
    EXPECT_TRUE(s.groups[1].entryBarriers.empty())
        << "false barrier between render (slot N) and revalidate (slot N+1); "
           "double-buffer parallelism broken";
    EXPECT_TRUE(s.groups[0].entryBarriers.empty());

    // Reverse order (revalidate first) must ALSO produce zero barriers — the
    // guarantee is order-independent because the resource sets are disjoint.
    std::vector<PassStep> reversed = { revalidate, render };
    FrameSyncSchedule s2 = BuildPassGroupSchedule(reversed);
    ASSERT_EQ(s2.groups.size(), 2u);
    EXPECT_TRUE(s2.groups[1].entryBarriers.empty());
}
