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

TEST(VulkanResources_CapabilityGraph, OptionalPathUsesIndependentTwinWhenDisabledOrUnavailable) {
    Vixen::CapabilityGraph graph;
    graph.BuildStandardCapabilities();

    EXPECT_EQ(graph.ResolveOptionalPath("RayQueryLighting"),
              Vixen::CapabilityPath::CapabilityIndependent);

    graph.SetAvailableDeviceExtensions({
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_KHR_SPIRV_1_4_EXTENSION_NAME,
        VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
    });
    EXPECT_EQ(graph.ResolveOptionalPath("RayQueryLighting"),
              Vixen::CapabilityPath::CapabilityEnabled);
    EXPECT_EQ(graph.ResolveOptionalPath("RayQueryLighting", false),
              Vixen::CapabilityPath::CapabilityIndependent);

    // Unknown capabilities fail closed to the same capability-independent
    // twin as a known-but-unavailable optional capability.
    EXPECT_EQ(graph.ResolveOptionalPath("CapabilityThatDoesNotExist"),
              Vixen::CapabilityPath::CapabilityIndependent);
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
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_KHR_SPIRV_1_4_EXTENSION_NAME,
        VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
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

TEST(VulkanResources_CapabilityGraph, BackgroundGpuPrefersIntegratedAndLesserDevice) {
    using Vixen::CapabilityGraph;
    using Vixen::PhysicalDeviceClass;
    using Vixen::PhysicalDeviceInfo;

    EXPECT_EQ(CapabilityGraph::ClassifyPhysicalDevice(VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU),
              PhysicalDeviceClass::Integrated);
    EXPECT_EQ(CapabilityGraph::ClassifyPhysicalDevice(VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU),
              PhysicalDeviceClass::Discrete);

    const std::vector<PhysicalDeviceInfo> devices{
        {1u, VK_NULL_HANDLE, VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
         PhysicalDeviceClass::Discrete, 1ull << 30, "large discrete"},
        {2u, VK_NULL_HANDLE, VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,
         PhysicalDeviceClass::Integrated, 4ull << 30, "integrated A"},
        {3u, VK_NULL_HANDLE, VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,
         PhysicalDeviceClass::Integrated, 2ull << 30, "integrated B"},
    };
    const auto selection = CapabilityGraph::SelectBackgroundGpu(devices);
    ASSERT_TRUE(selection.has_value());
    EXPECT_EQ(selection->index, 3u);
    EXPECT_EQ(selection->classification, PhysicalDeviceClass::Integrated);
}

TEST(VulkanResources_CapabilityGraph, BackgroundGpuFailsClosedWithoutCandidate) {
    const std::vector<Vixen::PhysicalDeviceInfo> devices{{
        0u, VK_NULL_HANDLE, VK_PHYSICAL_DEVICE_TYPE_OTHER,
        Vixen::PhysicalDeviceClass::Unknown, 0u, "unknown"}};
    EXPECT_FALSE(Vixen::CapabilityGraph::SelectBackgroundGpu(devices).has_value());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
