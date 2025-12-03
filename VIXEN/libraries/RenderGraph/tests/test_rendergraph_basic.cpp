#include <gtest/gtest.h>
#include <vector>
#include <vulkan/vulkan.h>

// Use centralized Vulkan global names to avoid duplicate strong symbols
#include <VulkanGlobalNames.h>

TEST(RenderGraph_Basic, Placeholder) {
    // Placeholder test - replace with real tests.
    EXPECT_TRUE(true);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
