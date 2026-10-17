#include <gtest/gtest.h>

#include "Nodes/CameraNode.h"
#include "IRenderTarget.h"

using namespace Vixen::RenderGraph;

namespace {
// Exercise the production callbacks and reference-valued graph outputs without a GPU.
class CameraProbe final : public CameraNode {
public:
    explicit CameraProbe(NodeType* type) : CameraNode("camera_history_probe", type) {}

    void CompileCamera() {
        TypedSetupContext setup(this, 0);
        TypedSetupImpl(setup);
        TypedCompileContext compile(this, 0);
        TypedCompileImpl(compile);
    }
    void ExecuteCamera() {
        TypedExecuteContext execute(this, 0);
        TypedExecuteImpl(execute);
    }
    glm::mat4 Previous() const {
        return GetOutput(CameraNodeConfig::PREV_VIEW_PROJ.index)->GetHandle<const glm::mat4&>();
    }
};

void ExpectMatrixEqual(const glm::mat4& actual, const glm::mat4& expected) {
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            EXPECT_FLOAT_EQ(actual[column][row], expected[column][row]);
}
} // namespace

TEST(CameraHistory, PreviousOutputSurvivesCurrentCameraUpdate) {
    CameraNodeType type;
    CameraProbe camera(&type);
    Vixen::Vulkan::Resources::RenderTargetData target;
    target.extent = {500, 500};
    auto targetResource = Resource::Create<Vixen::Vulkan::Resources::IRenderTarget*>(
        HandleDescriptor{"IRenderTarget*"});
    targetResource.SetHandle<Vixen::Vulkan::Resources::IRenderTarget*>(&target);
    camera.SetInput(CameraNodeConfig::SWAPCHAIN_PUBLIC.index, 0, &targetResource);
    camera.CompileCamera();
    const glm::mat4 initial = camera.GetCurrentViewProj();
    ExpectMatrixEqual(camera.Previous(), initial);

    camera.SetPositionForTest({4.0f, 2.0f, 8.0f});
    camera.ExecuteCamera();
    const glm::mat4 translated = camera.GetCurrentViewProj();
    EXPECT_NE(translated, initial);
    // Read AFTER Execute, exactly when PrevCameraConfigNode consumes the reference.
    ExpectMatrixEqual(camera.Previous(), initial);

    camera.SetPositionForTest({-2.0f, 1.0f, 6.0f});
    camera.ExecuteCamera();
    ExpectMatrixEqual(camera.Previous(), translated);

    // A projection/extent recompile has no valid prior pose; seed both outputs together.
    target.extent = {640, 360};
    camera.CompileCamera();
    ExpectMatrixEqual(camera.Previous(), camera.GetCurrentViewProj());
}

TEST(CameraHistory, ContinuousPoseUpdatesKeepEpochAndExplicitCutsAdvanceIt) {
    CameraNodeType type;
    CameraProbe camera(&type);
    Vixen::Vulkan::Resources::RenderTargetData target;
    target.extent = {501, 333};
    auto resource = Resource::Create<Vixen::Vulkan::Resources::IRenderTarget*>(HandleDescriptor{"IRenderTarget*"});
    resource.SetHandle<Vixen::Vulkan::Resources::IRenderTarget*>(&target);
    camera.SetInput(CameraNodeConfig::SWAPCHAIN_PUBLIC.index, 0, &resource);
    camera.CompileCamera();
    auto epoch = [&] { return camera.GetOutput(CameraNodeConfig::HISTORY_EPOCH.index)->GetHandle<uint32_t>(); };
    const auto first = epoch();
    camera.SetPositionForTest({4, 2, 8});
    camera.ExecuteCamera();
    EXPECT_EQ(epoch(), first);
    camera.NotifyCameraCut();
    camera.ExecuteCamera();
    EXPECT_EQ(epoch(), first + 1u);
}
