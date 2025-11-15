/**
 * @file test_resourcev3_basic.cpp
 * @brief Basic test to verify ResourceV3.h drop-in replacement works
 */

#include <gtest/gtest.h>
#include "../include/Data/Core/CompileTimeResourceSystem.h"

using namespace Vixen::RenderGraph;

TEST(ResourceV3Test, BasicCompilation) {
    // Just verify it compiles and basic API works
    Resource res = Resource::Create<VkImage>(ImageDescriptor{1920, 1080, VK_FORMAT_R8G8B8A8_UNORM});

    VkImage img = reinterpret_cast<VkImage>(0x12345678);
    res.SetHandle(img);

    VkImage retrieved = res.GetHandle<VkImage>();
    EXPECT_EQ(retrieved, img);
}

TEST(ResourceV3Test, ReferenceSemantics) {
    // Test reference storage (new capability)
    struct TestData {
        float value = 42.0f;
    };

    REGISTER_COMPILE_TIME_TYPE(TestData);

    TestData data;
    Resource res = Resource::Create<TestData>(HandleDescriptor{});

    res.SetHandle(data);  // Store by reference
    TestData& ref = res.GetHandle<TestData&>();

    EXPECT_EQ(&ref, &data);
    ref.value = 100.0f;
    EXPECT_FLOAT_EQ(data.value, 100.0f);
}

TEST(ResourceV3Test, BackwardCompatibility) {
    // Verify same API as old Resource class
    auto res = Resource::Create<VkBuffer>(BufferDescriptor{1024, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT});

    res.SetLifetime(ResourceLifetime::Persistent);
    EXPECT_EQ(res.GetLifetime(), ResourceLifetime::Persistent);

    VkBuffer buffer = reinterpret_cast<VkBuffer>(0xABCDEF);
    res.SetHandle(buffer);
    EXPECT_TRUE(res.IsValid());

    VkBuffer retrieved = res.GetHandle<VkBuffer>();
    EXPECT_EQ(retrieved, buffer);
}