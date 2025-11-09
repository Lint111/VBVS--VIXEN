#include <gtest/gtest.h>
#include <vector>
#include <vulkan/vulkan.h>

// Define globals required by DeviceNode
std::vector<const char*> deviceExtensionNames;
std::vector<const char*> layerNames;

TEST(RenderGraph_Basic, Placeholder) {
    // Placeholder test - replace with real tests.
    EXPECT_TRUE(true);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
