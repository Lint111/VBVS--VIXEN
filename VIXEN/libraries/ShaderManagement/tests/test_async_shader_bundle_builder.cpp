// Decision #6 regression: AsyncShaderBundleBuilder uses KernelDispatch's blocking lane.

#include <gtest/gtest.h>

#include "AsyncShaderBundleBuilder.h"

#include <atomic>
#include <chrono>

using namespace ShaderManagement;

namespace {

constexpr char kComputeSource[] = R"(#version 450
layout(local_size_x = 1) in;
void main() {}
 )";

} // namespace

TEST(AsyncShaderBundleBuilder, PreservesBuildReadinessAndCompletionEvents) {
    Vixen::EventBus::MessageBus bus;
    std::atomic<uint32_t> started{0};
    std::atomic<uint32_t> completed{0};
    std::atomic<uint32_t> failed{0};

    bus.Subscribe(ShaderCompilationStartedMessage::TYPE,
        [&](const Vixen::EventBus::BaseEventMessage&) {
            ++started;
            return true;
        });
    bus.Subscribe(ShaderCompilationCompletedMessage::TYPE,
        [&](const Vixen::EventBus::BaseEventMessage& message) {
            const auto& completedMessage = static_cast<const ShaderCompilationCompletedMessage&>(message);
            EXPECT_EQ(completedMessage.bundle.program.name, "AsyncLaneTest");
            ++completed;
            return true;
        });
    bus.Subscribe(ShaderCompilationFailedMessage::TYPE,
        [&](const Vixen::EventBus::BaseEventMessage&) {
            ++failed;
            return true;
        });

    AsyncShaderBundleBuilder builder(&bus, 1);
    const std::string uuid = builder.BuildAsync(7)
        .SetProgramName("AsyncLaneTest")
        .SetUuid("async-lane-test")
        .SetPipelineType(PipelineTypeConstraint::Compute)
        .AddStage(ShaderStage::Compute, kComputeSource)
        .EnableSdiGeneration(false)
        .Submit();

    EXPECT_TRUE(builder.WaitForBuild(uuid, std::chrono::seconds(5)));
    EXPECT_TRUE(builder.IsBuildComplete(uuid));

    // Worker publication is queued; readiness means the worker finished publishing. Main-thread
    // delivery remains the caller's explicit ProcessMessages safe point.
    EXPECT_EQ(started.load(), 0u);
    EXPECT_EQ(completed.load(), 0u);
    bus.ProcessMessages();
    EXPECT_EQ(started.load(), 1u);
    EXPECT_EQ(completed.load(), 1u);
    EXPECT_EQ(failed.load(), 0u);
}
