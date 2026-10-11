#include "VulkanGraphApplication.h"
#include "graph/LookdevSceneDefinition.h"
#include "Nodes/CameraNode.h"
#include "Nodes/LightingConfigNode.h"
#include "Data/Nodes/CameraNodeConfig.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {
VulkanGraphApplication::RenderPathConfig gRenderPathConfig{};
}

namespace Vixen::RenderGraph {

class LightingConfigNodeTestAccess {
public:
    static uint32_t FrameCount(const LightingConfigNode& node) {
        return node.perFrame_.GetFrameCount();
    }

    static bool ReadMappedBytes(const LightingConfigNode& node, uint32_t frameIndex,
                                std::array<std::byte, sizeof(Vixen::Gpu::LightingConfig)>& bytes) {
        if (frameIndex >= node.perFrame_.GetFrameCount()) return false;
        const void* mapped = node.perFrame_.GetUniformBufferMapped(frameIndex);
        if (!mapped) return false;
        std::memcpy(bytes.data(), mapped, bytes.size());
        return true;
    }
};

} // namespace Vixen::RenderGraph

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
    const bool slowPan = std::getenv("VIXEN_LOOKDEV_CAPTURE_PAN") != nullptr;
    constexpr size_t kPanAngles = 24;
    scenarios.reserve(presets.size() * (slowPan ? kPanAngles : yaws.size()));
    for (const auto& preset : presets) {
        if (requestedState && preset.name != std::string(requestedState)) continue;
        const size_t angleCount = slowPan ? kPanAngles : yaws.size();
        for (size_t angle = 0; angle < angleCount; ++angle) {
            const float yaw = slowPan
                ? 6.28318530718f * static_cast<float>(angle) / static_cast<float>(kPanAngles)
                : yaws[angle];
            std::ostringstream filename;
            filename << preset.name << (slowPan ? "-pan-" : "-angle-")
                     << std::setfill('0') << std::setw(slowPan ? 2 : 0) << angle << ".png";
            scenarios.push_back({preset, yaw, filename.str()});
        }
    }
    return scenarios;
}

class LookdevCaptureApplication final : public VulkanGraphApplication {
public:
    LookdevCaptureApplication(std::filesystem::path outputDir,
                              std::vector<CaptureScenario> scenarios)
        : outputDir_(std::move(outputDir)), scenarios_(std::move(scenarios)) {
        SetPresentationTarget(PresentationTarget::Offscreen);
        SetRenderPathConfig(gRenderPathConfig);
    }

    void BuildRenderGraph() override {
        const auto recipes = Vixen::App::Lookdev::BuildMaterialRecipes(scenarios_.front().lighting);
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

        // The ordinary look-dev capture below is procedural-only, so it cannot
        // exercise VIXEN_BRICKMAP_TRAVERSAL. When the existing stored-SDF scene
        // is requested, keep its six FORMAT_STORED_SDF instances and orbit that
        // scene with the same four yaw angles and lighting presets.
        const bool storedSdfScene = std::getenv("VIXEN_BRICKMAP_SCENE") != nullptr;
        const float orbitCenterX = storedSdfScene ? 64.0f : 5.0f;
        const float orbitCenterY = storedSdfScene ? 64.0f : 5.0f;
        const float orbitCenterZ = storedSdfScene ? 0.0f : 5.0f;
        const float orbitDistance = storedSdfScene ? 300.0f : 14.0f;
        camera_->SetParameter(Vixen::RenderGraph::CameraNodeConfig::PARAM_ORBIT_CENTER_X, orbitCenterX);
        camera_->SetParameter(Vixen::RenderGraph::CameraNodeConfig::PARAM_ORBIT_CENTER_Y, orbitCenterY);
        camera_->SetParameter(Vixen::RenderGraph::CameraNodeConfig::PARAM_ORBIT_CENTER_Z, orbitCenterZ);
        camera_->SetParameter(Vixen::RenderGraph::CameraNodeConfig::PARAM_ORBIT_DISTANCE, orbitDistance);
        camera_->SetParameter(Vixen::RenderGraph::CameraNodeConfig::PARAM_FOV, 45.0f);
        camera_->SetParameter(Vixen::RenderGraph::CameraNodeConfig::PARAM_YAW, scenarios_.front().yawRadians);
        camera_->SetParameter(Vixen::RenderGraph::CameraNodeConfig::PARAM_PITCH, kPitchRadians);

        lighting_->SetLights(Vixen::App::Lookdev::Lights(scenarios_.front().lighting),
                             scenarios_.front().lighting.ambient,
                             Vixen::App::Lookdev::CelSpillPurposeScales(scenarios_.front().lighting));
        lighting_->SetParameter(Vixen::RenderGraph::LightingConfigNodeConfig::PARAM_EXPOSURE_COMPENSATION_EV,
                                scenarios_.front().lighting.exposureCompensationEV);
        if (!storedSdfScene) {
            SetBodyInstances(Vixen::App::Lookdev::BodyInstances(
                recipes, scenarios_.front().lighting));
        }
    }

    bool Render() override {
        if (!captureError_.empty() || !VulkanGraphApplication::Render()) return false;
        ++framesInScenario_;
        if (framesInScenario_ < kFramesPerScenario) return true;

        const auto& scenario = scenarios_[scenarioIndex_];
        if (scenarioIndex_ == 0 && !VerifyUploadedExposure(scenario)) return false;
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
                                 next.lighting.ambient,
                                 Vixen::App::Lookdev::CelSpillPurposeScales(next.lighting));
            lighting_->SetParameter(Vixen::RenderGraph::LightingConfigNodeConfig::PARAM_EXPOSURE_COMPENSATION_EV,
                                    next.lighting.exposureCompensationEV);
        }
        return true;
    }

    size_t CapturedCount() const { return capturedCount_; }
    const std::string& CaptureError() const { return captureError_; }

private:
    bool VerifyUploadedExposure(const CaptureScenario& scenario) {
        const std::size_t exposureOffset = offsetof(Vixen::Gpu::LightingConfig, exposureCompensationEV);
        std::ofstream readback(outputDir_ / "lighting-config-readback.txt", std::ios::trunc);
        if (!readback) {
            captureError_ = "could not create LightingConfig readback report";
            return false;
        }

        for (uint32_t frameIndex = 0;
             frameIndex < Vixen::RenderGraph::LightingConfigNodeTestAccess::FrameCount(*lighting_);
             ++frameIndex) {
            std::array<std::byte, sizeof(Vixen::Gpu::LightingConfig)> bytes{};
            if (!Vixen::RenderGraph::LightingConfigNodeTestAccess::ReadMappedBytes(
                    *lighting_, frameIndex, bytes)) {
                captureError_ = "could not read mapped LightingConfig ring slot " +
                    std::to_string(frameIndex);
                return false;
            }

            float uploadedExposureEV = 0.0f;
            std::memcpy(&uploadedExposureEV, bytes.data() + exposureOffset, sizeof(uploadedExposureEV));

            std::ostringstream line;
            line << "frame_slot=" << frameIndex
                 << " offset=" << exposureOffset
                 << " bytes=" << std::hex << std::setfill('0')
                 << std::setw(2) << std::to_integer<unsigned>(bytes[exposureOffset]) << ' '
                 << std::setw(2) << std::to_integer<unsigned>(bytes[exposureOffset + 1]) << ' '
                 << std::setw(2) << std::to_integer<unsigned>(bytes[exposureOffset + 2]) << ' '
                 << std::setw(2) << std::to_integer<unsigned>(bytes[exposureOffset + 3])
                 << std::dec << std::setprecision(9)
                 << " value=" << uploadedExposureEV
                 << " expected=" << scenario.lighting.exposureCompensationEV;
            readback << line.str() << '\n';

            if (uploadedExposureEV != scenario.lighting.exposureCompensationEV) {
                captureError_ = "LightingConfig SSBO exposure readback mismatch: " + line.str();
                return false;
            }
        }
        return true;
    }

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
    const bool slowPan = std::getenv("VIXEN_LOOKDEV_CAPTURE_PAN") != nullptr;
    ASSERT_EQ(scenarios.size(), slowPan ? 24u : 4u)
        << "CTest must select one fixed-lighting four-angle set or 24-angle pan";
    std::string error;
    ASSERT_TRUE(CaptureSequence(outputDir, scenarios, error)) << error;
}

} // namespace

int main(int argc, char** argv) {
    int forwardedArgc = 1;
    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        if (argument.starts_with("--render-scale=")) {
            gRenderPathConfig.renderScale = std::stof(argument.substr(15));
        } else if (argument == "--upscaler=none") {
            gRenderPathConfig.upscaler = VulkanGraphApplication::UpscalerKind::None;
        } else if (argument == "--upscaler=linear") {
            gRenderPathConfig.upscaler = VulkanGraphApplication::UpscalerKind::Linear;
        } else if (argument == "--upscaler=fsr1") {
            gRenderPathConfig.upscaler = VulkanGraphApplication::UpscalerKind::Fsr1;
        } else {
            argv[forwardedArgc++] = argv[index];
        }
    }
    argc = forwardedArgc;
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
