// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#include <gtest/gtest.h>
#include "Core/FrameSyncSchedule.h"
using namespace Vixen::RenderGraph;

TEST(FrameSyncScheduleTypes, DefaultScheduleIsEmptyAndInvalid) {
    FrameSyncSchedule s;
    EXPECT_FALSE(s.valid);
    EXPECT_TRUE(s.groups.empty());
    EXPECT_TRUE(s.edges.empty());
    EXPECT_EQ(s.timelineValuesPerFrame, 0u);
}

TEST(FrameSyncScheduleTypes, GroupBarrierDistinguishesImageVsBuffer) {
    GroupBarrier b{};
    b.isImage = true;
    b.src = ResolveAccess(AccessKind::ComputeStorageWrite);
    b.dst = ResolveAccess(AccessKind::FragmentSampledRead);
    EXPECT_EQ(b.src.layout, VK_IMAGE_LAYOUT_GENERAL);
    EXPECT_EQ(b.dst.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}
