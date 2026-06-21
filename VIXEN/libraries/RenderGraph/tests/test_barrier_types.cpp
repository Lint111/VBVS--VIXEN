// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#include <gtest/gtest.h>
#include "Core/BarrierTypes.h"

using namespace Vixen::RenderGraph;

TEST(BarrierTypes, ComputeStorageWriteMapsToGeneral) {
    const AccessInfo i = ResolveAccess(AccessKind::ComputeStorageWrite);
    EXPECT_EQ(i.stage,  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    EXPECT_EQ(i.access, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    EXPECT_EQ(i.layout, VK_IMAGE_LAYOUT_GENERAL);
}

TEST(BarrierTypes, FragmentSampledReadMapsToReadOnlyOptimal) {
    const AccessInfo i = ResolveAccess(AccessKind::FragmentSampledRead);
    EXPECT_EQ(i.stage,  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
    EXPECT_EQ(i.access, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    EXPECT_EQ(i.layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

TEST(BarrierTypes, ColorAttachmentWriteMapsToColorOptimal) {
    const AccessInfo i = ResolveAccess(AccessKind::ColorAttachmentWrite);
    EXPECT_EQ(i.stage,  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    EXPECT_EQ(i.access, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    EXPECT_EQ(i.layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}

TEST(BarrierTypes, PresentSrcHasNoAccessAndPresentLayout) {
    const AccessInfo i = ResolveAccess(AccessKind::PresentSrc);
    EXPECT_EQ(i.access, VK_ACCESS_2_NONE);
    EXPECT_EQ(i.layout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}

TEST(BarrierTypes, ReadWriteClassification) {
    EXPECT_TRUE (AccessWrites(AccessKind::ComputeStorageWrite));
    EXPECT_FALSE(AccessWrites(AccessKind::FragmentSampledRead));
    EXPECT_TRUE (AccessReads (AccessKind::ComputeStorageReadWrite));
    EXPECT_TRUE (AccessWrites(AccessKind::ComputeStorageReadWrite));
    EXPECT_FALSE(AccessReads (AccessKind::ColorAttachmentWrite));
}
