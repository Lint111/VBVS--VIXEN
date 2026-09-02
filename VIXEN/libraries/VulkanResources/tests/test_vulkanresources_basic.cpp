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

TEST(VulkanResources_CapabilityGraph, RayQueryAndSubgroupCompositesUseAvailabilitySets) {
    Vixen::CapabilityGraph graph;
    graph.BuildStandardCapabilities();

    ASSERT_NE(graph.GetCapability("RayQueryLighting"), nullptr);
    ASSERT_NE(graph.GetCapability("SubgroupCoopTraversal"), nullptr);
    EXPECT_FALSE(graph.IsCapabilityAvailable("RayQueryLighting"));
    EXPECT_FALSE(graph.IsCapabilityAvailable("SubgroupCoopTraversal"));

    graph.SetAvailableDeviceExtensions({
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
    });
    graph.SetAvailableDeviceFeatures({
        "subgroupComputeBallot",
        "subgroupComputeArithmetic",
        "subgroupComputeShuffle",
    });
    graph.InvalidateAll();

    EXPECT_TRUE(graph.IsCapabilityAvailable("RayQueryLighting"));
    EXPECT_TRUE(graph.IsCapabilityAvailable("SubgroupCoopTraversal"));

    graph.SetAvailableDeviceFeatures({"subgroupComputeBallot", "subgroupComputeShuffle"});
    graph.InvalidateAll();
    EXPECT_FALSE(graph.IsCapabilityAvailable("SubgroupCoopTraversal"));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
