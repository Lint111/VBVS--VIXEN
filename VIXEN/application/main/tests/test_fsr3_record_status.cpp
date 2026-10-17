#include "graph/Fsr3Upscaler.h"
#include "Nodes/ComputeStageNode.h"

#include <gtest/gtest.h>

#include <string>

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
