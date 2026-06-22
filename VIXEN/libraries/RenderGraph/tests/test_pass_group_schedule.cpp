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
