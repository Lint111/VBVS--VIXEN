#include <gtest/gtest.h>

#include "CapabilityGraph.h"
#include "ConfiguredOptionalPath.h"

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

TEST(VulkanResources_CapabilityGraph, BindingTimeAndTypedBuildFactsAreStoredOnGraphNodes) {
    using namespace Vixen;
    auto runtimeNode = std::make_shared<CompositeCapability>("RuntimeNode");
    EXPECT_EQ(runtimeNode->GetBindingTime(), CapabilityBindingTime::Runtime);
    runtimeNode->SetBindingTime(CapabilityBindingTime::BuildTime);
    EXPECT_EQ(runtimeNode->GetBindingTime(), CapabilityBindingTime::BuildTime);

    CapabilityGraph graph;
    graph.RegisterBuildFact("BuildFact:Platform",
        PlatformBuildFact{"Linux", "x86_64", "GNU", "15.2"});
    const auto factNode = std::dynamic_pointer_cast<BuildFactCapability>(
        graph.GetCapability("BuildFact:Platform"));
    ASSERT_NE(factNode, nullptr);
    ASSERT_TRUE(std::holds_alternative<PlatformBuildFact>(factNode->GetValue()));
    EXPECT_EQ(factNode->GetBindingTime(), CapabilityBindingTime::BuildTime);
}

TEST(VulkanResources_CapabilityGraph, ConfiguredOptionalPathUsesBuildModeBinding) {
    using namespace Vixen;
    CapabilityGraph graph;
    graph.RegisterCapability(std::make_shared<CompositeCapability>("RayQueryLighting"));

    EXPECT_EQ(BuildCapabilityTraits<BuildCapabilityId::RayQueryLighting>::buildTime,
              BuildCapabilities::kPersonalizedMode);
    const auto path = ResolveConfiguredOptionalPath<BuildCapabilityId::RayQueryLighting>(graph);
    if constexpr (BuildCapabilities::kPersonalizedMode &&
                  BuildCapabilityTraits<BuildCapabilityId::RayQueryLighting>::buildTime) {
        EXPECT_EQ(path == CapabilityPath::CapabilityEnabled,
                  BuildCapabilities::kCapability_RayQueryLighting);
    } else {
        EXPECT_EQ(path, CapabilityPath::CapabilityEnabled)
            << "full mode must preserve runtime CapabilityGraph resolution";
    }
}

TEST(VulkanResources_CapabilityGraph, ConfiguredPathInvokesOnlyTheSelectedBody) {
    using namespace Vixen;
    CapabilityGraph graph;
    graph.RegisterCapability(std::make_shared<CompositeCapability>("RayQueryLighting"));

    bool enabledInvoked = false;
    bool independentInvoked = false;
    WithConfiguredOptionalPath<BuildCapabilityId::RayQueryLighting>(
        graph, true,
        [&] { enabledInvoked = true; },
        [&] { independentInvoked = true; });

    if constexpr (BuildCapabilities::kPersonalizedMode &&
                  BuildCapabilityTraits<BuildCapabilityId::RayQueryLighting>::buildTime) {
        EXPECT_EQ(enabledInvoked, BuildCapabilities::kCapability_RayQueryLighting);
        EXPECT_EQ(independentInvoked, !BuildCapabilities::kCapability_RayQueryLighting);
    } else {
        EXPECT_TRUE(enabledInvoked);
        EXPECT_FALSE(independentInvoked);
    }
}

TEST(VulkanResources_CapabilityGraph, ConfiguredDeviceIdentityRejectsChangedDeviceOrDriver) {
    using namespace Vixen;
    if constexpr (!BuildCapabilities::kPersonalizedMode || !BuildCapabilities::kHasDeviceIdentity) {
        GTEST_SKIP() << "full mode has no device identity bound at configure";
    } else {
        DeviceDriverIdentityBuildFact identity{};
        identity.deviceName = BuildCapabilities::kDeviceName;
        identity.driverName = BuildCapabilities::kDriverName;
        identity.driverInfo = BuildCapabilities::kDriverInfo;
        identity.vendorId = BuildCapabilities::kVendorId;
        identity.deviceId = BuildCapabilities::kDeviceId;
        identity.driverVersion = BuildCapabilities::kDriverVersion;
        identity.driverId = BuildCapabilities::kDriverId;
        identity.deviceUuid = BuildCapabilities::kDeviceUuid;
        identity.driverUuid = BuildCapabilities::kDriverUuid;
        EXPECT_TRUE(CapabilityGraph::MatchesConfiguredDeviceIdentity(identity));

        identity.driverVersion ^= 1u;
        std::string refusal;
        EXPECT_FALSE(CapabilityGraph::MatchesConfiguredDeviceIdentity(identity, &refusal));
        EXPECT_NE(refusal.find("expected"), std::string::npos);
        EXPECT_NE(refusal.find(std::string(BuildCapabilities::kDeviceName)), std::string::npos);
    }
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

TEST(VulkanResources_CapabilityGraph, VersionRequirementsSelectIndependentPathsByToolchainAndApi) {
    using Vixen::CapabilityGraph;
    using Vixen::CapabilityPath;
    using Vixen::CapabilityVersionSource;
    using Vixen::CapabilityVersionValue;

    CapabilityGraph graph;
    graph.BuildStandardCapabilities();

    ASSERT_NE(graph.GetCapability("Version:VulkanApi"), nullptr);
    ASSERT_NE(graph.GetCapability("Version:VulkanSdk"), nullptr);
    ASSERT_NE(graph.GetCapability("Version:Glslang"), nullptr);
    ASSERT_NE(graph.GetCapability("Version:SpirvTarget"), nullptr);
    ASSERT_NE(graph.GetCapability("VulkanApi:RequiredFloor"), nullptr);
    ASSERT_NE(graph.GetCapability("SpirvTarget:RayQueryFloor"), nullptr);
    EXPECT_TRUE(graph.IsCapabilityAvailable("Version:VulkanSdk"));
    EXPECT_TRUE(graph.IsCapabilityAvailable("Version:Glslang"));
    EXPECT_TRUE(graph.IsCapabilityAvailable("Version:SpirvTarget"));

    const std::vector<std::string> rayQueryExtensions{
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_KHR_SPIRV_1_4_EXTENSION_NAME,
        VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
    };
    auto rayQueryExtensionsWithSync2 = rayQueryExtensions;
    rayQueryExtensionsWithSync2.emplace_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    graph.SetAvailableDeviceExtensions(rayQueryExtensionsWithSync2);
    graph.SetAvailableDeviceFeatures({"synchronization2"});

    const auto oldApi = CapabilityVersionValue::Parse("1.2.0");
    const auto belowApiFloor = CapabilityVersionValue::Parse("1.1.0");
    const auto oldSpirv = CapabilityVersionValue::Parse("1.3.0");
    ASSERT_TRUE(oldApi.has_value());
    ASSERT_TRUE(belowApiFloor.has_value());
    ASSERT_TRUE(oldSpirv.has_value());
    graph.SetVersion(CapabilityVersionSource::VulkanApi, *belowApiFloor);
    graph.SetVersion(CapabilityVersionSource::SpirvTarget, *oldSpirv);

    EXPECT_FALSE(graph.IsCapabilityAvailable("VulkanApi:RequiredFloor"));
    EXPECT_FALSE(graph.IsCapabilityAvailable("DeviceFeature:synchronization2"));
    EXPECT_FALSE(graph.IsCapabilityAvailable("SpirvTarget:RayQueryFloor"));
    EXPECT_EQ(graph.ResolveOptionalPath("RayQueryLighting"),
              CapabilityPath::CapabilityIndependent);

    // Vulkan 1.2 uses the KHR promotion path: the extension plus its feature bit satisfies
    // synchronization2, while the old SPIR-V target still selects the independent ray-query twin.
    graph.SetVersion(CapabilityVersionSource::VulkanApi, *oldApi);
    EXPECT_TRUE(graph.IsCapabilityAvailable("VulkanApi:RequiredFloor"));
    graph.SetAvailableDeviceExtensions(rayQueryExtensions);
    EXPECT_FALSE(graph.IsCapabilityAvailable("DeviceFeature:synchronization2"));
    graph.SetAvailableDeviceExtensions(rayQueryExtensionsWithSync2);
    EXPECT_TRUE(graph.IsCapabilityAvailable("DeviceFeature:synchronization2"));
    EXPECT_EQ(graph.ResolveOptionalPath("RayQueryLighting"),
              CapabilityPath::CapabilityIndependent);

    // Vulkan 1.3 exposes synchronization2 as core; no extension advertisement is needed.
    graph.SetAvailableDeviceExtensions(rayQueryExtensions);
    const auto newApi = CapabilityVersionValue::Parse("1.3.0");
    const auto newSpirv = CapabilityVersionValue::Parse("1.6.0");
    ASSERT_TRUE(newApi.has_value());
    ASSERT_TRUE(newSpirv.has_value());
    graph.SetVersion(CapabilityVersionSource::VulkanApi, *newApi);
    graph.SetVersion(CapabilityVersionSource::SpirvTarget, *newSpirv);

    EXPECT_TRUE(graph.IsCapabilityAvailable("VulkanApi:RequiredFloor"));
    EXPECT_TRUE(graph.IsCapabilityAvailable("DeviceFeature:synchronization2"));
    EXPECT_TRUE(graph.IsCapabilityAvailable("SpirvTarget:RayQueryFloor"));
    EXPECT_EQ(graph.ResolveOptionalPath("RayQueryLighting"),
              CapabilityPath::CapabilityEnabled);
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
