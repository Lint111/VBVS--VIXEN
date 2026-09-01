#include <gtest/gtest.h>

#include "CapabilityGraph.h"

TEST(VulkanResources_CapabilityGraph, FragmentStoresAndAtomicsUsesDeviceFeatureSet) {
    Vixen::CapabilityGraph graph;
    graph.BuildStandardCapabilities();

    ASSERT_NE(graph.GetCapability("DeviceFeature:fragmentStoresAndAtomics"), nullptr);
    EXPECT_FALSE(graph.IsCapabilityAvailable(
        "DeviceFeature:fragmentStoresAndAtomics"));

    graph.SetAvailableDeviceFeatures({"fragmentStoresAndAtomics"});
    graph.InvalidateAll();
    EXPECT_TRUE(graph.IsCapabilityAvailable(
        "DeviceFeature:fragmentStoresAndAtomics"));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
