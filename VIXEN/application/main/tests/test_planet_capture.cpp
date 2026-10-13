#include "VulkanGraphApplication.h"
#include "graph/PlanetSceneDefinition.h"
#include "Nodes/CameraNode.h"
#include "Nodes/LightingConfigNode.h"
#include "Nodes/ShadowConfigNode.h"
#include "Nodes/DeviceNode.h"
#include "Nodes/StorageBufferNode.h"
#include "Nodes/ComputeStageNode.h"
#include "Nodes/BodyOctreeSceneNode.h"
#include "Nodes/ReservoirConfigNode.h"
#include "Data/Nodes/CameraNodeConfig.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Vixen::RenderGraph {

class LightingConfigNodeTestAccess {
public:
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

// Frames per state are derived, not chosen (R536, R565 C2): every per-frame ring (LightingConfig,
// camera, previous camera, shadow config) holds kRingSlotCount slots and each must be rewritten for
// the state, so two full rings give a warmed, verified steady state. The reservoir path carries
// temporalCap=32 samples of history; when it is enabled the capture keeps two full caps (64).
// The frame-count sweep that proves 8 == 64 per state is tools/planet-capture-frame-sweep.py.
constexpr uint32_t kRingSlotCount = Vixen::RenderGraph::FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT;
constexpr uint32_t kReservoirHistoryCap = 32;
constexpr uint32_t kMaxFramesPerState = 2 * kReservoirHistoryCap;
constexpr uint32_t kDefaultFramesPerState = 2 * kRingSlotCount;
constexpr double kDefaultProcessBudgetMs = 300000.0; // Cold-driver dzn compile is ~100 s; see reports/capturespeed.md.

using CaptureClock = std::chrono::steady_clock;

uint32_t DefaultFramesPerState() {
    return Vixen::RenderGraph::ResolveReservoirEnabled() ? kMaxFramesPerState : kDefaultFramesPerState;
}

uint32_t CaptureFramesPerState(std::string& error) {
    const char* requested = std::getenv("VIXEN_PLANET_CAPTURE_FRAMES");
    if (!requested || *requested == '\0') return DefaultFramesPerState();

    char* end = nullptr;
    const unsigned long value = std::strtoul(requested, &end, 10);
    if (end == requested || *end != '\0' || value == 0 || value > kMaxFramesPerState) {
        error = "VIXEN_PLANET_CAPTURE_FRAMES must be an integer from 1 through " +
            std::to_string(kMaxFramesPerState);
        return 0;
    }
    return static_cast<uint32_t>(value);
}

double ProcessBudgetMs() {
    const char* requested = std::getenv("VIXEN_PLANET_CAPTURE_BUDGET_MS");
    if (!requested || *requested == '\0') return kDefaultProcessBudgetMs;
    char* end = nullptr;
    const double value = std::strtod(requested, &end);
    return (end == requested || *end != '\0' || value <= 0.0) ? kDefaultProcessBudgetMs : value;
}

void SetEnvironment(const char* key, const std::string& value) {
#ifdef _WIN32
    _putenv_s(key, value.c_str());
#else
    setenv(key, value.c_str(), 1);
#endif
}

class PlanetCaptureApplication final : public VulkanGraphApplication {
public:
    PlanetCaptureApplication(std::filesystem::path outputDir, Vixen::App::Planet::ResolvedState state,
                             uint32_t framesPerState)
        : outputDir_(std::move(outputDir)), state_(std::move(state)), framesPerState_(framesPerState) {
        SetPresentationTarget(PresentationTarget::Offscreen);
    }

    void BuildRenderGraph() override {
        namespace Planet = Vixen::App::Planet;
        if (state_.bodies.size() > Planet::kMaxStateInstances) {
            captureError_ = "state exceeds the body instance cap";
            return;
        }
        for (const auto& recipe : Planet::BuildRecipes()) {
            const auto result = RegisterProceduralRecipe(static_cast<uint32_t>(recipe.slot), recipe.entry);
            if (result != Vixen::SVO::RecipeRegistry::RegisterResult::Ok) {
                captureError_ = std::string("could not register planet recipe ") + recipe.name +
                    " (status " + std::to_string(static_cast<int>(result)) + ")";
                return;
            }
        }

        VulkanGraphApplication::BuildRenderGraph();
        auto* graph = GetRenderGraph();
        camera_ = graph ? dynamic_cast<Vixen::RenderGraph::CameraNode*>(
            graph->GetInstanceByName("raymarch_camera")) : nullptr;
        lighting_ = graph ? dynamic_cast<Vixen::RenderGraph::LightingConfigNode*>(
            graph->GetInstanceByName("lighting_config")) : nullptr;
        auto* shadow = graph ? dynamic_cast<Vixen::RenderGraph::ShadowConfigNode*>(
            graph->GetInstanceByName("shadow_config")) : nullptr;
        if (!camera_ || !lighting_ || !shadow) {
            captureError_ = "production graph is missing raymarch_camera, lighting_config or shadow_config";
            return;
        }

        // The camera is the render-space origin (camera-relative upload); only its aim is state data.
        // The production graph latches orbit mode, which recomputes the position every frame; a zero-radius
        // orbit about the origin pins the camera there so the look target alone aims it.
        using Vixen::RenderGraph::CameraNodeConfig;
        camera_->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_X, 0.0f);
        camera_->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Y, 0.0f);
        camera_->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Z, 0.0f);
        camera_->SetParameter(CameraNodeConfig::PARAM_ORBIT_DISTANCE, 0.0f);
        camera_->SetParameter(Vixen::RenderGraph::CameraNodeConfig::PARAM_FOV, state_.verticalFovDegrees);
        camera_->SetPositionForTest(glm::vec3(0.0f));
        camera_->SetLookTargetNoOrbitForTest(glm::vec3(state_.forward));
        shadow->SetReachAndBias(state_.shadowMaxDistance, state_.shadowBias);

        lighting_->SetLights(Planet::Lights(state_), Planet::kAmbient, Planet::CelSpillPurposeScales());
        lighting_->SetParameter(Vixen::RenderGraph::LightingConfigNodeConfig::PARAM_EXPOSURE_COMPENSATION_EV,
                                state_.exposureCompensationEV);
        Vixen::App::ApplySceneLook(
            *lighting_, dynamic_cast<Vixen::RenderGraph::SkySphereNode*>(graph->GetInstanceByName("sky_sphere")),
            Planet::SceneLookDeclaration());
        const char* shading = std::getenv("VIXEN_PLANET_SHADING");
        shadingLambert_ = shading && std::string(shading) == "lambert";
        lighting_->SetParameter(Vixen::RenderGraph::LightingConfigNodeConfig::PARAM_SHADING_MODE,
                                shadingLambert_ ? Vixen::RenderGraph::LightingConfigNodeConfig::SHADING_MODE_LAMBERT_GGX
                                                : Vixen::RenderGraph::LightingConfigNodeConfig::SHADING_MODE_CEL);
        SetBodyInstances(Planet::BodyInstances(state_));
        if (std::getenv("VIXEN_PLANET_READBACK")) {
            auto* records = dynamic_cast<Vixen::RenderGraph::StorageBufferNode*>(
                graph->GetInstanceByName("hit_record_buffer"));
            if (!records) throw std::runtime_error("missing shadow snapshot source");
            records->SetParameter(Vixen::RenderGraph::StorageBufferNodeConfig::PARAM_EXTRA_USAGE_FLAGS,
                                  static_cast<uint32_t>(VK_BUFFER_USAGE_TRANSFER_SRC_BIT));
            auto* wave = dynamic_cast<Vixen::RenderGraph::ComputeStageNode*>(
                graph->GetInstanceByName("shadow_visibility_wave"));
            if (!wave) throw std::runtime_error("missing shadow snapshot stage");
            wave->SetPostDispatchRecorderForTest([this, records](VkCommandBuffer cmd) {
                auto* deviceNode = dynamic_cast<Vixen::RenderGraph::DeviceNode*>(
                    GetRenderGraph()->GetInstanceByName("main_device"));
                auto* device = deviceNode ? deviceNode->GetVulkanDevice() : nullptr;
                if (!device) throw std::runtime_error("missing shadow snapshot device");
                if (!snapshotBuffer_) {
                    snapshotSize_ = records->GetSizeBytes();
                    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
                    info.size = snapshotSize_;
                    info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
                    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                    if (vkCreateBuffer(device->device, &info, nullptr, &snapshotBuffer_) != VK_SUCCESS)
                        throw std::runtime_error("shadow snapshot buffer allocation failed");
                    snapshotDevice_ = device->device;
                    VkMemoryRequirements requirements{};
                    vkGetBufferMemoryRequirements(device->device, snapshotBuffer_, &requirements);
                    VkPhysicalDeviceMemoryProperties properties{};
                    vkGetPhysicalDeviceMemoryProperties(*device->gpu, &properties);
                    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                    allocation.allocationSize = requirements.size;
                    allocation.memoryTypeIndex = properties.memoryTypeCount;
                    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
                        if ((requirements.memoryTypeBits & (1u << i)) &&
                            (properties.memoryTypes[i].propertyFlags &
                             (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                             (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                            allocation.memoryTypeIndex = i;
                            break;
                        }
                    if (allocation.memoryTypeIndex == properties.memoryTypeCount ||
                        vkAllocateMemory(device->device, &allocation, nullptr, &snapshotMemory_) != VK_SUCCESS ||
                        vkBindBufferMemory(device->device, snapshotBuffer_, snapshotMemory_, 0) != VK_SUCCESS)
                        throw std::runtime_error("shadow snapshot host memory allocation failed");
                }
                VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     0, 1, &barrier, 0, nullptr, 0, nullptr);
                const VkBufferCopy copy{0, 0, snapshotSize_};
                vkCmdCopyBuffer(cmd, records->GetBufferHandle(), snapshotBuffer_, 1, &copy);
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_HOST_READ_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
                                     0, 1, &barrier, 0, nullptr, 0, nullptr);
            });
            graph->GetLifecycleHooks().RegisterNodeHook(Vixen::RenderGraph::NodeLifecyclePhase::PreCleanup,
                [this](Vixen::RenderGraph::NodeInstance*) {
                    if (!snapshotDevice_) return;
                    vkDeviceWaitIdle(snapshotDevice_);
                    vkDestroyBuffer(snapshotDevice_, snapshotBuffer_, nullptr);
                    vkFreeMemory(snapshotDevice_, snapshotMemory_, nullptr);
                    snapshotBuffer_ = VK_NULL_HANDLE;
                    snapshotMemory_ = VK_NULL_HANDLE;
                    snapshotDevice_ = VK_NULL_HANDLE;
                }, "planet shadow snapshot cleanup", wave);
        }
    }


    bool Render() override {
        if (!captureError_.empty() || !VulkanGraphApplication::Render()) return false;
        if (++framesInState_ < framesPerState_) return true;
        const std::filesystem::path output = outputDir_ / (state_.name + ".png");
        if (!CaptureOffscreenFrameToPng(output.string(), captureError_)) return false;
        if (std::getenv("VIXEN_PLANET_READBACK") && !ExportReadback()) return false;
        captured_ = true;
        return true;
    }

    bool Captured() const { return captured_; }
    const std::string& CaptureError() const { return captureError_; }

private:
    static void WriteVec(std::ostream& out, const glm::dvec3& v) {
        out << '[' << v.x << ',' << v.y << ',' << v.z << ']';
    }

    bool ExportReadback() {
        namespace Planet = Vixen::App::Planet;
        auto* graph = GetRenderGraph();
        auto* deviceNode = dynamic_cast<Vixen::RenderGraph::DeviceNode*>(
            graph->GetInstanceByName("main_device"));
        auto* device = deviceNode ? deviceNode->GetVulkanDevice() : nullptr;
        if (!device || vkDeviceWaitIdle(device->device) != VK_SUCCESS) {
            captureError_ = "could not synchronize planet readback";
            return false;
        }
        const auto stem = outputDir_ / state_.name;
        void* mapped = nullptr;
        if (!snapshotMemory_ || vkMapMemory(device->device, snapshotMemory_, 0, snapshotSize_, 0, &mapped) != VK_SUCCESS) {
            captureError_ = "could not map planet HitRecord buffer";
            return false;
        }
        std::ofstream hits(stem.string() + ".hits.bin", std::ios::binary);
        hits.write(static_cast<const char*>(mapped), static_cast<std::streamsize>(snapshotSize_));
        vkUnmapMemory(device->device, snapshotMemory_);
        std::array<std::byte, sizeof(Vixen::Gpu::LightingConfig)> bytes{};
        if (!hits || !Vixen::RenderGraph::LightingConfigNodeTestAccess::ReadMappedBytes(*lighting_, 0, bytes)) {
            captureError_ = "could not export planet lighting buffers";
            return false;
        }
        std::ofstream config(stem.string() + ".lighting.bin", std::ios::binary);
        config.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        if (!config) {
            captureError_ = "could not export planet LightingConfig";
            return false;
        }
        auto* meterBuffer = dynamic_cast<Vixen::RenderGraph::StorageBufferNode*>(
            graph->GetInstanceByName("exposure_meter_buffer"));
        void* mappedMeter = meterBuffer ? meterBuffer->MapForReadback(device) : nullptr;
        if (!mappedMeter) {
            captureError_ = "could not export exposure meter";
            return false;
        }
        std::ofstream meter(stem.string() + ".meter.bin", std::ios::binary);
        meter.write(static_cast<const char*>(mappedMeter), sizeof(float));
        meterBuffer->UnmapReadback(device);
        if (!meter) {
            captureError_ = "could not write exposure meter";
            return false;
        }

        const auto* bodyScene = dynamic_cast<Vixen::RenderGraph::BodyOctreeSceneNode*>(
            graph->GetInstanceByName("body_octree_scene"));
        if (!bodyScene) {
            captureError_ = "missing runtime body order for planet snapshot";
            return false;
        }
        const auto& runtime = bodyScene->GetInstances();
        std::ofstream scene(stem.string() + ".scene.json");
        scene << std::setprecision(17);
        scene << "{\"state\":\"" << state_.name << "\",\"specState\":" << (Planet::IsSpecState(state_.name) ? "true" : "false")
              << ",\"width\":" << state_.width << ",\"height\":" << state_.height
              << ",\"verticalFovDegrees\":" << state_.verticalFovDegrees
              << ",\"shading\":\"" << (shadingLambert_ ? "lambert" : "cel") << "\"" << ",\"exposureCompensationEV\":" << state_.exposureCompensationEV
              << ",\"lightCapacity\":" << std::size(Vixen::Gpu::LightingConfig{}.lights)
              << ",\"exposureOffset\":" << offsetof(Vixen::Gpu::LightingConfig, exposureCompensationEV)
              << ",\"focus\":\"" << state_.focus << "\",\"sunDirection\":";
        WriteVec(scene, state_.sunDirection);
        scene << ",\"sunDistance\":" << state_.sunDistance
              << ",\"forward\":";
        WriteVec(scene, state_.forward);
        scene << ",\"shadowBias\":" << state_.shadowBias
              << ",\"shadowMaxDistance\":" << state_.shadowMaxDistance
              << ",\"timeSeconds\":" << state_.timeSeconds
              << ",\"stationPhaseDegrees\":" << state_.stationPhaseDegrees
              << ",\"moonPhaseDegrees\":" << state_.moonPhaseDegrees
              << ",\"stationVelocity\":";
        WriteVec(scene, state_.stationVelocity);
        scene << ",\"stationNormal\":";
        WriteVec(scene, state_.stationNormal);
        scene << ",\"stationUp\":";
        WriteVec(scene, state_.stationUp);
        scene << ",\"bodies\":[";
        for (size_t i = 0; i < state_.bodies.size(); ++i) {
            const auto& body = state_.bodies[i];
            // Match the sim body to its runtime instance by recipe, scale and camera-relative position.
            const glm::dvec3 relative = body.position - state_.camera;
            int runtimeIndex = -1;
            for (size_t r = 0; r < runtime.size(); ++r) {
                const auto& t = runtime[r].transform.localToWorld;
                const double sx = std::sqrt(double(t.rows[0][0]) * t.rows[0][0] + double(t.rows[1][0]) * t.rows[1][0] +
                                            double(t.rows[2][0]) * t.rows[2][0]);
                const glm::dvec3 translation{t.rows[0][3], t.rows[1][3], t.rows[2][3]};
                if (runtime[r].material.recipeId == static_cast<uint32_t>(body.recipe) &&
                    std::abs(sx - body.scale) <= 1e-4 * body.scale &&
                    glm::length(translation - relative) <= 1e-6 * glm::length(relative) + 1e-3) {
                    runtimeIndex = static_cast<int>(r);
                    break;
                }
            }
            if (runtimeIndex < 0) {
                captureError_ = std::string("body has no runtime instance: ") + body.name;
                return false;
            }
            if (i) scene << ',';
            scene << "{\"name\":\"" << body.name << "\",\"recipeId\":" << static_cast<uint32_t>(body.recipe)
                  << ",\"runtimeIndex\":" << runtimeIndex << ",\"scale\":" << body.scale
                  << ",\"emission\":" << body.emission << ",\"relative\":";
            WriteVec(scene, relative);
            scene << ",\"simPosition\":";
            WriteVec(scene, body.position);
            scene << ",\"color\":[" << body.color.r << ',' << body.color.g << ',' << body.color.b << "]}";
        }
        scene << "]}\n";
        if (!scene) {
            captureError_ = "could not export planet scene metadata";
            return false;
        }
        return true;
    }

    std::filesystem::path outputDir_;
    Vixen::App::Planet::ResolvedState state_;
    Vixen::RenderGraph::CameraNode* camera_ = nullptr;
    Vixen::RenderGraph::LightingConfigNode* lighting_ = nullptr;
    uint32_t framesPerState_ = kDefaultFramesPerState;
    uint32_t framesInState_ = 0;
    bool shadingLambert_ = false;
    bool captured_ = false;
    std::string captureError_;
    VkDevice snapshotDevice_ = VK_NULL_HANDLE;
    VkBuffer snapshotBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory snapshotMemory_ = VK_NULL_HANDLE;
    VkDeviceSize snapshotSize_ = 0;
};

TEST(PlanetCapture, RendersStateDeterministically) {
    const char* outputEnv = std::getenv("VIXEN_PLANET_CAPTURE_DIR");
    const char* stateEnv = std::getenv("VIXEN_PLANET_CAPTURE_STATE");
    ASSERT_NE(outputEnv, nullptr) << "CTest must configure the planet capture directory";
    ASSERT_NE(stateEnv, nullptr) << "CTest must select one planet capture state";
    auto state = Vixen::App::Planet::ResolveState(stateEnv);
    ASSERT_TRUE(state.has_value()) << "unknown planet capture state: " << stateEnv;

    const std::filesystem::path outputDir(outputEnv);
    std::error_code ec;
    std::filesystem::create_directories(outputDir, ec);
    ASSERT_FALSE(ec) << "could not create capture directory: " << outputDir.string();

    std::string framesError;
    const uint32_t framesPerState = CaptureFramesPerState(framesError);
    ASSERT_TRUE(framesError.empty()) << framesError;

    SetEnvironment("VIXEN_WINDOW_WIDTH", std::to_string(state->width));
    SetEnvironment("VIXEN_WINDOW_HEIGHT", std::to_string(state->height));
    const auto processStart = CaptureClock::now();
    PlanetCaptureApplication app(outputDir, *state, framesPerState);
    const int result = app.Run(RunOptions{.exitAfterFrames = framesPerState});
    const double processMs =
        std::chrono::duration<double, std::milli>(CaptureClock::now() - processStart).count();
    ASSERT_TRUE(result == 0 && app.Captured())
        << (app.CaptureError().empty() ? "application exited before the planet capture was written"
                                       : app.CaptureError());
    const auto path = outputDir / (state->name + ".png");
    ASSERT_TRUE(std::filesystem::exists(path) && std::filesystem::file_size(path, ec) > 0)
        << "capture file was not written: " << path.string();

    // R536: every capture states its runtime budget and reports its wall time, so the runner and
    // the queue see how long one state costs (per-frame cost is in [GPUYIELD]/VIXEN_PHASE_TIMING).
    const double budgetMs = ProcessBudgetMs();
    std::printf("[PLANET_CAPTURE_TIMING] state=%s frames=%u wall_ms=%.0f budget_ms=%.0f\n",
                state->name.c_str(), framesPerState, processMs, budgetMs);
    std::fflush(stdout);
    ASSERT_LE(processMs, budgetMs) << "planet capture exceeded its runtime budget: " << processMs
                                   << " ms > " << budgetMs << " ms (VIXEN_PLANET_CAPTURE_BUDGET_MS)";
}

} // namespace
