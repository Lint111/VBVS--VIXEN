// Offline (no Vulkan/GPU) unit test for VulkanApplicationBase::Tick() status classification and
// the PreTick->Update->Render->PostTick hook order. Uses a stub subclass whose virtuals return
// canned outcomes so the loop logic is provable without a device.
#include <gtest/gtest.h>
#include "VulkanApplicationBase.h"
#include <vector>
#include <string>

namespace {

// Minimal stub: overrides every pure virtual with a canned outcome + records the call order.
class StubApp : public VulkanApplicationBase {
public:
    // Canned control knobs the tests set:
    bool  renderReturns    = true;    // what Render() returns this tick
    bool  shutdown         = false;   // what IsShutdownRequested() reports
    bool  deviceLost       = false;   // what IsDeviceLostState() reports
    std::vector<std::string> calls;   // records hook/method order

    void Prepare() override { calls.push_back("Prepare"); }
    void Update()  override { calls.push_back("Update"); }
    bool Render()  override { calls.push_back("Render"); return renderReturns; }
    void PreTick()  override { calls.push_back("PreTick"); }
    void PostTick() override { calls.push_back("PostTick"); }
protected:
    bool IsShutdownRequested() const override { return shutdown; }
    bool IsDeviceLostState()   const override { return deviceLost; }
public:
    using VulkanApplicationBase::Tick;   // expose for the test
    void SetExitAfterFrames(uint64_t n) { SetExitAfterFramesForTest(n); }
};

TEST(AppRunTick, HookOrderIsPreUpdateRenderPost) {
    StubApp app;
    app.Tick();
    ASSERT_EQ(app.calls, (std::vector<std::string>{"PreTick", "Update", "Render", "PostTick"}));
}

TEST(AppRunTick, RenderTrueUnderLimitReturnsRunning) {
    StubApp app;
    app.renderReturns = true;
    EXPECT_EQ(app.Tick(), TickStatus::Running);
}

TEST(AppRunTick, FrameLimitReachedWhenCounterHitsExitAfterFrames) {
    StubApp app;
    app.renderReturns = true;
    app.SetExitAfterFrames(1);       // limit of 1 frame
    EXPECT_EQ(app.Tick(), TickStatus::FrameLimitReached);  // after this single tick, counter==1>=1
}

TEST(AppRunTick, RenderFalseWithShutdownIsWindowClosed) {
    StubApp app;
    app.renderReturns = false;
    app.shutdown = true;
    EXPECT_EQ(app.Tick(), TickStatus::WindowClosed);
}

TEST(AppRunTick, RenderFalseWithDeviceLostIsDeviceLostUnrecoverable) {
    StubApp app;
    app.renderReturns = false;
    app.shutdown = false;
    app.deviceLost = true;
    EXPECT_EQ(app.Tick(), TickStatus::DeviceLostUnrecoverable);
}

TEST(AppRunTick, RenderFalseWithNeitherIsRenderError) {
    StubApp app;
    app.renderReturns = false;
    app.shutdown = false;
    app.deviceLost = false;
    EXPECT_EQ(app.Tick(), TickStatus::RenderError);
}

}  // namespace
