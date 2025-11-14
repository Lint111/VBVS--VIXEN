/**
 * @file test_migration_compat.cpp
 * @brief Backward compatibility tests for migration to new type system
 */

#include <gtest/gtest.h>
#include "../include/Data/Core/ResourceVariantV2Integration.h"
#include "../include/Data/Core/ResourceVariant.h"

using namespace Vixen::RenderGraph;

TEST(MigrationTest, OldAPIWorks) {
    auto res = Resource::Create<VkImage>(ImageDescriptor{1920, 1080, VK_FORMAT_R8G8B8A8_UNORM});
    VkImage img = reinterpret_cast<VkImage>(0x1234);
    res.SetHandle<VkImage>(img);
    EXPECT_EQ(res.GetHandle<VkImage>(), img);
}

TEST(MigrationTest, NewAPIWorks) {
    auto res = ResourceV2::Create<VkImage>(ImageDescriptor{1920, 1080, VK_FORMAT_R8G8B8A8_UNORM});
    VkImage img = reinterpret_cast<VkImage>(0x1234);
    res.SetHandle(img);
    EXPECT_EQ(res.GetHandle<VkImage>(), img);
}

TEST(MigrationTest, CachedValidation) {
    EXPECT_TRUE(CachedTypeRegistry::Instance().IsTypeAcceptable<VkImage>());
    EXPECT_TRUE(CachedTypeRegistry::Instance().IsTypeAcceptable<VkBuffer>());
}