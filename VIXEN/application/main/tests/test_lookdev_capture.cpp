#include "VulkanGraphApplication.h"
#include "graph/LookdevSceneDefinition.h"
#include "Nodes/CameraNode.h"
#include "Nodes/LightingConfigNode.h"
#include "Data/Nodes/CameraNodeConfig.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr uint32_t kFramesPerScenario = 64; // Two full 32-frame reservoir-history caps.
constexpr float kPitchRadians = 0.45f;

struct CaptureScenario {
    Vixen::App::Lookdev::LightingPreset lighting;
    float yawRadians = 0.0f;
    std::string filename;
};

std::vector<CaptureScenario> BuildScenarios() {
    constexpr std::array<float, 4> yaws{{0.0f, 1.57079632679f, 3.14159265359f, 4.71238898038f}};
    const auto& presets = Vixen::App::Lookdev::LightingPresets();
    const char* requestedState = std::getenv("VIXEN_LOOKDEV_CAPTURE_STATE");
    std::vector<CaptureScenario> scenarios;
    scenarios.reserve(presets.size() * yaws.size());
    for (const auto& preset : presets) {
        if (requestedState && preset.name != std::string(requestedState)) continue;
        for (size_t angle = 0; angle < yaws.size(); ++angle) {
            scenarios.push_back({preset, yaws[angle],
                std::string(preset.name) + "-angle-" + std::to_string(angle) + ".png"});
        }
    }
    return scenarios;
}

bool SetEnvironmentVariable(const char* name, const std::string& value) {
#ifdef _WIN32
    return _putenv_s(name, value.c_str()) == 0;
#else
    return setenv(name, value.c_str(), 1) == 0;
#endif
}

class LookdevCaptureApplication final : public VulkanGraphApplication {
public:
    LookdevCaptureApplication(std::filesystem::path outputDir,
                              std::vector<CaptureScenario> scenarios)
        : outputDir_(std::move(outputDir)), scenarios_(std::move(scenarios)) {
        SetPresentationTarget(PresentationTarget::Offscreen);
    }

    void BuildRenderGraph() override {
        const auto recipes = Vixen::App::Lookdev::BuildMaterialRecipes();
        for (const auto& recipe : recipes) {
            const auto result = RegisterProceduralRecipe(recipe.recipeId, recipe.entry);
            if (result != Vixen::SVO::RecipeRegistry::RegisterResult::Ok) {
                captureError_ = "could not register look-dev material recipe " +
                    std::to_string(recipe.recipeId) + " (status " +
                    std::to_string(static_cast<int>(result)) + ")";
                return;
            }
        }

        VulkanGraphApplication::BuildRenderGraph();
        auto* graph = GetRenderGraph();
        camera_ = graph ? dynamic_cast<Vixen::RenderGraph::CameraNode*>(
            graph->GetInstanceByName("raymarch_camera")) : nullptr;
        lighting_ = graph ? dynamic_cast<Vixen::RenderGraph::LightingConfigNode*>(
            graph->GetInstanceByName("lighting_config")) : nullptr;
        if (!camera_ || !lighting_) {
            captureError_ = "production graph is missing raymarch_camera or lighting_config";
            return;
        }

        camera_->SetParameter(Vixen::RenderGraph::CameraNodeConfig::PARAM_ORBIT_CENTER_X, 5.0f);
        camera_->SetParameter(Vixen::RenderGraph::CameraNodeConfig::PARAM_ORBIT_CENTER_Y, 5.0f);
        camera_->SetParameter(Vixen::RenderGraph::CameraNodeConfig::PARAM_ORBIT_CENTER_Z, 5.0f);
        camera_->SetParameter(Vixen::RenderGraph::CameraNodeConfig::PARAM_ORBIT_DISTANCE, 14.0f);
        camera_->SetParameter(Vixen::RenderGraph::CameraNodeConfig::PARAM_FOV, 45.0f);
        camera_->SetParameter(Vixen::RenderGraph::CameraNodeConfig::PARAM_YAW, scenarios_.front().yawRadians);
        camera_->SetParameter(Vixen::RenderGraph::CameraNodeConfig::PARAM_PITCH, kPitchRadians);

        lighting_->SetLights(Vixen::App::Lookdev::Lights(scenarios_.front().lighting),
                             scenarios_.front().lighting.ambient, {1.0f, 1.0f});
        SetBodyInstances(Vixen::App::Lookdev::BodyInstances(recipes));
    }

    bool Render() override {
        if (!captureError_.empty() || !VulkanGraphApplication::Render()) return false;
        ++framesInScenario_;
        if (framesInScenario_ < kFramesPerScenario) return true;

        const auto& scenario = scenarios_[scenarioIndex_];
        const std::filesystem::path output = outputDir_ / scenario.filename;
        if (!CaptureOffscreenFrameToPng(output.string(), captureError_)) return false;
        ++capturedCount_;

        if (capturedCount_ < scenarios_.size()) {
            scenarioIndex_ = capturedCount_;
            framesInScenario_ = 0;
            const auto& next = scenarios_[scenarioIndex_];
            camera_->SetYawForTest(next.yawRadians);
            camera_->SetPitchForTest(kPitchRadians);
            lighting_->SetLights(Vixen::App::Lookdev::Lights(next.lighting),
                                 next.lighting.ambient, {1.0f, 1.0f});
        }
        return true;
    }

    size_t CapturedCount() const { return capturedCount_; }
    const std::string& CaptureError() const { return captureError_; }

private:
    std::filesystem::path outputDir_;
    std::vector<CaptureScenario> scenarios_;
    Vixen::RenderGraph::CameraNode* camera_ = nullptr;
    Vixen::RenderGraph::LightingConfigNode* lighting_ = nullptr;
    size_t scenarioIndex_ = 0;
    size_t capturedCount_ = 0;
    uint32_t framesInScenario_ = 0;
    std::string captureError_;
};

bool CaptureSequence(const std::filesystem::path& outputDir,
                     const std::vector<CaptureScenario>& scenarios,
                     std::string& error) {
    std::error_code ec;
    // The directory is supplied by the CTest runner. Create it if needed, but
    // do not recursively delete caller-owned paths when the test is invoked
    // directly with a custom VIXEN_LOOKDEV_CAPTURE_DIR.
    std::filesystem::create_directories(outputDir, ec);
    if (ec) {
        error = "could not create capture directory: " + outputDir.string() + ": " + ec.message();
        return false;
    }

    LookdevCaptureApplication app(outputDir, scenarios);
    const uint32_t frameCount = kFramesPerScenario * static_cast<uint32_t>(scenarios.size());
    const int result = app.Run(RunOptions{.exitAfterFrames = frameCount});
    if (result != 0 || app.CapturedCount() != scenarios.size()) {
        error = app.CaptureError().empty()
            ? "application exited before all look-dev captures were written"
            : app.CaptureError();
        return false;
    }
    for (const auto& scenario : scenarios) {
        const auto path = outputDir / scenario.filename;
        if (!std::filesystem::exists(path) || std::filesystem::file_size(path, ec) == 0) {
            error = "capture file was not written: " + path.string();
            return false;
        }
    }
    return true;
}

TEST(LookdevCapture, RendersFourStatesAtFourAnglesDeterministically) {
    const char* outputEnv = std::getenv("VIXEN_LOOKDEV_CAPTURE_DIR");
    ASSERT_NE(outputEnv, nullptr) << "CTest must configure the look-dev capture directory";
    const std::filesystem::path outputDir(outputEnv);
    const std::vector<CaptureScenario> scenarios = BuildScenarios();
    ASSERT_EQ(scenarios.size(), 4u)
        << "CTest must select one four-angle look-dev lighting preset";
    ASSERT_TRUE(SetEnvironmentVariable("VIXEN_HDR_EXPOSURE_COMPENSATION_EV",
                                       std::to_string(scenarios.front().lighting.exposureCompensationEV)));
    std::string error;
    ASSERT_TRUE(CaptureSequence(outputDir, scenarios, error)) << error;
}

} // namespace
