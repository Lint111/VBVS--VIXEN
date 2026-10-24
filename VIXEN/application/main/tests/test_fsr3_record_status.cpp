#include "graph/Fsr3Upscaler.h"
#include "Nodes/ComputeStageNode.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

using namespace Vixen::RenderGraph;

TEST(Fsr3RecordStatus, ReturnsFailureTextInsteadOfThrowingBeforeInitialization) {
    ComputeStageNodeType type;
    const auto instance = type.CreateInstance("fsr3_record_status");
    auto* node = static_cast<ComputeStageNode*>(instance.get());
    ComputeStageNode::Context context(node, 0);
    const Fsr3UpscalerHooks hooks = CreateFsr3UpscalerHooks(
        {1280, 720}, {1920, 1080}, ReconstructionDepthConvention::ReversedZeroToOne, 0.1f, 1000.0f);

    std::string failure;
    EXPECT_NO_THROW(failure = hooks.record(context, VK_NULL_HANDLE, 0, 0));
    EXPECT_NE(failure.find("before context initialization"), std::string::npos) << failure;
}

TEST(Fsr3RecordStatus, ReportsRecordFailureToTheHostSoItCanDemoteTheProvider) {
    ComputeStageNodeType type;
    const auto instance = type.CreateInstance("fsr3_record_report");
    auto* node = static_cast<ComputeStageNode*>(instance.get());
    ComputeStageNode::Context context(node, 0);
    std::vector<RenderModuleProviderInitializationResult> reports;
    const Fsr3UpscalerHooks hooks = CreateFsr3UpscalerHooks(
        {1280, 720}, {1920, 1080}, ReconstructionDepthConvention::ReversedZeroToOne, 0.1f, 1000.0f,
        [&reports](RenderModuleProviderInitializationResult result) { reports.push_back(std::move(result)); });

    const std::string failure = hooks.record(context, VK_NULL_HANDLE, 0, 0);
    ASSERT_FALSE(failure.empty());
    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(reports[0].providerId, "fsr3");
    EXPECT_FALSE(reports[0].initialized);
    EXPECT_EQ(reports[0].message, failure);

    // The reported provider joins the failure list Prepare's fallback loop consults, once.
    std::vector<std::string> failedProviders;
    EXPECT_TRUE(RecordRenderModuleProviderInitializationFailure(failedProviders, reports[0].providerId));
    EXPECT_FALSE(RecordRenderModuleProviderInitializationFailure(failedProviders, reports[0].providerId));
}
