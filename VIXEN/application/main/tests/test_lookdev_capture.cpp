#include "VulkanGraphApplication.h"
#include "graph/LookdevSceneDefinition.h"
#include "Nodes/CameraNode.h"
#include "Nodes/LightingConfigNode.h"
#include "Nodes/DeviceNode.h"
#include "Nodes/StorageBufferNode.h"
#include "Nodes/ComputeStageNode.h"
#include "Nodes/BodyOctreeSceneNode.h"
#include "Data/Nodes/CameraNodeConfig.h"

#include <gtest/gtest.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
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

TEST(LookdevLighting, EmittersHaveDistinctDeclaredSources) {
    using namespace Vixen::App::Lookdev;
    for (const auto& preset : LightingPresets()) {
        const auto lights = Lights(preset);
        EXPECT_EQ(lights.size(), CelSpillPurposeScales(preset).size());
        const auto recipes = BuildMaterialRecipes(preset);
        const auto instances = BodyInstances(recipes, preset);
        size_t emitterCount = 0;
        for (size_t i = 0; i < recipes.size(); ++i) {
            if (!IsServiceEmitter(recipes[i].material)) continue;
            ++emitterCount;
            EXPECT_EQ(recipes[i].entry.bytecode.size(), 1u);
            const auto position = Vixen::SVO::TransformPoint(instances[i].transform.localToWorld, glm::vec3(0.0f));
            size_t matched = 0;
            for (const auto& light : lights) {
                if (light.kind != 1u) continue;
                const glm::vec3 source(light.direction_or_positionX,
                    light.direction_or_positionY, light.direction_or_positionZ);
                if (glm::distance(position, source) > 1e-5f) continue;
                ++matched;
                EXPECT_GT(light.range, 0.0f);
                if (ServiceLightsEnabled(preset))
                    EXPECT_GT(light.radianceX + light.radianceY + light.radianceZ, 0.0f);
                else EXPECT_EQ(light.radianceX + light.radianceY + light.radianceZ, 0.0f);
            }
            EXPECT_EQ(matched, 1u) << "emitter instance " << i;
        }
        EXPECT_EQ(lights.size(), emitterCount + 1u); // One shared directional key.
        for (size_t i = 1; i < lights.size(); ++i)
            for (size_t j = i + 1; j < lights.size(); ++j) {
                const glm::vec3 a(lights[i].direction_or_positionX, lights[i].direction_or_positionY,
                    lights[i].direction_or_positionZ);
                const glm::vec3 b(lights[j].direction_or_positionX, lights[j].direction_or_positionY,
                    lights[j].direction_or_positionZ);
                EXPECT_GT(glm::distance(a, b), 1e-5f);
            }
    }
}

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

class LookdevCaptureApplication final : public VulkanGraphApplication {
public:
    LookdevCaptureApplication(std::filesystem::path outputDir,
                              std::vector<CaptureScenario> scenarios)
        : outputDir_(std::move(outputDir)), scenarios_(std::move(scenarios)) {
        SetPresentationTarget(PresentationTarget::Offscreen);
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

        camera_->SetParameter(Vixen::RenderGraph::CameraNodeConfig::PARAM_ORBIT_CENTER_X, 5.0f);
        camera_->SetParameter(Vixen::RenderGraph::CameraNodeConfig::PARAM_ORBIT_CENTER_Y, 5.0f);
        camera_->SetParameter(Vixen::RenderGraph::CameraNodeConfig::PARAM_ORBIT_CENTER_Z, 5.0f);
        camera_->SetParameter(Vixen::RenderGraph::CameraNodeConfig::PARAM_ORBIT_DISTANCE, 14.0f);
        camera_->SetParameter(Vixen::RenderGraph::CameraNodeConfig::PARAM_FOV, 45.0f);
        camera_->SetParameter(Vixen::RenderGraph::CameraNodeConfig::PARAM_YAW, scenarios_.front().yawRadians);
        camera_->SetParameter(Vixen::RenderGraph::CameraNodeConfig::PARAM_PITCH, kPitchRadians);

        lighting_->SetLights(Vixen::App::Lookdev::Lights(scenarios_.front().lighting),
                             scenarios_.front().lighting.ambient,
                             Vixen::App::Lookdev::CelSpillPurposeScales(scenarios_.front().lighting));
        lighting_->SetParameter(Vixen::RenderGraph::LightingConfigNodeConfig::PARAM_EXPOSURE_COMPENSATION_EV,
                                scenarios_.front().lighting.exposureCompensationEV);
        SetBodyInstances(Vixen::App::Lookdev::BodyInstances(
            recipes, scenarios_.front().lighting));
        if (std::getenv("VIXEN_LOOKDEV_READBACK")) {
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
                }, "lookdev shadow snapshot cleanup", wave);
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
        if (std::getenv("VIXEN_LOOKDEV_READBACK") && !ExportLightingReadback(scenario)) return false;
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
    bool ExportLightingReadback(const CaptureScenario& scenario) {
        auto* graph = GetRenderGraph();
        auto* deviceNode = dynamic_cast<Vixen::RenderGraph::DeviceNode*>(
            graph->GetInstanceByName("main_device"));
        auto* records = dynamic_cast<Vixen::RenderGraph::StorageBufferNode*>(
            graph->GetInstanceByName("hit_record_buffer"));
        auto* device = deviceNode ? deviceNode->GetVulkanDevice() : nullptr;
        if (!device || !records || vkDeviceWaitIdle(device->device) != VK_SUCCESS) {
            captureError_ = "could not synchronize look-dev shadow readback";
            return false;
        }
        const auto stem = outputDir_ / std::filesystem::path(scenario.filename).stem();
        void* mapped = nullptr;
        if (!snapshotMemory_ || vkMapMemory(device->device, snapshotMemory_, 0, snapshotSize_, 0, &mapped) != VK_SUCCESS) {
            captureError_ = "could not map look-dev HitRecord buffer";
            return false;
        }
        std::ofstream hits(stem.string() + ".hits.bin", std::ios::binary);
        hits.write(static_cast<const char*>(mapped), static_cast<std::streamsize>(snapshotSize_));
        vkUnmapMemory(device->device, snapshotMemory_);
        std::array<std::byte, sizeof(Vixen::Gpu::LightingConfig)> bytes{};
        if (!hits || !Vixen::RenderGraph::LightingConfigNodeTestAccess::ReadMappedBytes(*lighting_, 0, bytes)) {
            captureError_ = "could not export look-dev lighting buffers";
            return false;
        }
        std::ofstream config(stem.string() + ".lighting.bin", std::ios::binary);
        config.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        if (!config) {
            captureError_ = "could not export look-dev LightingConfig";
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
        std::ofstream scene(stem.string() + ".scene.json");
        const auto recipes = Vixen::App::Lookdev::BuildMaterialRecipes(scenario.lighting);
        scene << std::setprecision(9) << "{\"lightCapacity\":"
              << std::size(Vixen::Gpu::LightingConfig{}.lights)
              << ",\"exposureOffset\":" << offsetof(Vixen::Gpu::LightingConfig, exposureCompensationEV)
              << ",\"instances\":[";
        const auto* bodyScene = dynamic_cast<Vixen::RenderGraph::BodyOctreeSceneNode*>(
            graph->GetInstanceByName("body_octree_scene"));
        if (!bodyScene) {
            captureError_ = "missing runtime body order for shadow snapshot";
            return false;
        }
        const auto& runtimeInstances = bodyScene->GetInstances();
        for (size_t i = 0; i < runtimeInstances.size(); ++i) {
            if (i) scene << ',';
            const auto recipe = std::find_if(recipes.begin(), recipes.end(), [&](const auto& value) {
                return value.recipeId == runtimeInstances[i].material.recipeId;
            });
            if (recipe == recipes.end()) throw std::runtime_error("unknown runtime recipe in capture");
            scene << "{\"index\":" << i << ",\"material\":" << static_cast<unsigned>(recipe->material)
                  << ",\"recipeId\":" << recipe->recipeId << '}';
        }
        scene << "],\"primitives\":[";
        bool first = true;
        auto primitive = [&](const Vixen::App::Lookdev::Primitive& authored) {
            if (std::none_of(recipes.begin(), recipes.end(), [&](const auto& recipe) {
                    return recipe.material == authored.material;
                })) return;
            const auto value = Vixen::App::Lookdev::PrimitiveForLighting(authored, scenario.lighting);
            if (!first) scene << ',';
            first = false;
            const auto center = Vixen::App::Lookdev::GridToWorld(value.center);
            const auto extent = Vixen::App::Lookdev::GridToWorld(value.extent);
            scene << "{\"group\":" << static_cast<unsigned>(value.group)
                  << ",\"material\":" << static_cast<unsigned>(value.material)
                  << ",\"center\":[" << center.x << ',' << center.y << ',' << center.z
                  << "],\"extent\":[" << extent.x << ',' << extent.y << ',' << extent.z << "]}";
        };
        for (const auto& value : Vixen::App::Lookdev::Primitives()) primitive(value);
        for (const auto& value : Vixen::App::Lookdev::FigureAppendages()) primitive(value);
        scene << "]}\n";
        if (!scene) {
            captureError_ = "could not export look-dev scene metadata";
            return false;
        }
        return true;
    }

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
    VkDevice snapshotDevice_ = VK_NULL_HANDLE;
    VkBuffer snapshotBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory snapshotMemory_ = VK_NULL_HANDLE;
    VkDeviceSize snapshotSize_ = 0;
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
    std::string error;
    ASSERT_TRUE(CaptureSequence(outputDir, scenarios, error)) << error;
}

} // namespace
