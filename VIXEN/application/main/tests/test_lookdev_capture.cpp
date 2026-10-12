#include "VulkanGraphApplication.h"
#include "graph/LookdevSceneDefinition.h"
#include "Nodes/CameraNode.h"
#include "Nodes/LightingConfigNode.h"
#include "Nodes/DeviceNode.h"
#include "Nodes/StorageBufferNode.h"
#include "Nodes/ComputeStageNode.h"
#include "Nodes/PushConstantGathererNode.h"
#include "Data/Nodes/PushConstantGathererNodeConfig.h"
#include "Nodes/BodyOctreeSceneNode.h"
#include "Data/Nodes/CameraNodeConfig.h"
#include "Data/Nodes/FrameSyncNodeConfig.h"
#include "Nodes/ReservoirConfigNode.h"
#include "Core/NodeRegistration.h"
#include "Core/RenderGraph.h"

#include <gtest/gtest.h>

#include <array>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Vixen::RenderGraph {

CONSTEXPR_NODE_CONFIG(TestIndirectIrradianceNodeConfig, 0, 1, SlotArrayMode::Single) {
    OUTPUT_SLOT(INDIRECT_IRRADIANCE, glm::vec3, 0,
                SlotNullability::Required, SlotMutability::WriteOnly);

    TestIndirectIrradianceNodeConfig() {
        HandleDescriptor irradianceDesc{"glm::vec3"};
        INIT_OUTPUT_DESC(INDIRECT_IRRADIANCE, "indirect_irradiance",
                         ResourceLifetime::Transient, irradianceDesc);
    }
};

class TestIndirectIrradianceNode final : public TypedNode<TestIndirectIrradianceNodeConfig> {
public:
    TestIndirectIrradianceNode(const std::string& name, NodeType* nodeType)
        : TypedNode(name, nodeType) {}

protected:
    void TypedExecuteImpl(TypedExecuteContext& ctx) override {
        ctx.Out(TestIndirectIrradianceNodeConfig::INDIRECT_IRRADIANCE,
                glm::vec3(2.0f, 0.18f, 0.04f));
    }
};

class TestIndirectIrradianceNodeType final
    : public TypedNodeType<TestIndirectIrradianceNodeConfig> {
public:
    TestIndirectIrradianceNodeType() : TypedNodeType("TestIndirectIrradiance") {}

    std::unique_ptr<NodeInstance> CreateInstance(const std::string& name) const override {
        return std::make_unique<TestIndirectIrradianceNode>(
            name, const_cast<TestIndirectIrradianceNodeType*>(this));
    }
};

CONSTEXPR_NODE_CONFIG(TestIndirectLightingPorts, 0, 1, SlotArrayMode::Single) {
    OUTPUT_SLOT(INDIRECT_LIGHTING, glm::vec3, 0,
                SlotNullability::Required, SlotMutability::WriteOnly);

    TestIndirectLightingPorts() {
        HandleDescriptor irradianceDesc{"glm::vec3"};
        INIT_OUTPUT_DESC(INDIRECT_LIGHTING, "moduleIndirectIrradiance",
                         ResourceLifetime::Transient, irradianceDesc);
    }
};

class TestIndirectLightingModule final : public SubGraphType<TestIndirectLightingModule> {
public:
    static constexpr std::string_view TYPE_NAME = "TestIndirectLightingModule";
    static constexpr std::string_view CAPABILITY_NAME = "Test:IndirectLighting";
    struct Params {};
    using PortConfig = TestIndirectLightingPorts;

    void Build(GraphScope& scope, const Params&) {
        const NodeHandle source = scope.AddNode<TestIndirectIrradianceNodeType>("irradiance");
        scope.BindOutput(TestIndirectLightingPorts::INDIRECT_LIGHTING, source,
                         TestIndirectIrradianceNodeConfig::INDIRECT_IRRADIANCE);
    }
};

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

VIXEN_REGISTER_NODE(Vixen::RenderGraph::TestIndirectIrradianceNodeType);
VIXEN_REGISTER_NODE(Vixen::RenderGraph::TestIndirectLightingModule);

namespace {

// Frames per angle are derived, not chosen: every per-frame ring (LightingConfig, camera,
// previous camera) holds kRingSlotCount slots and each must be rewritten for the new angle, so
// two full rings give a warmed, verified steady state. The reservoir path carries
// temporalCap=32 samples of history; when it is enabled the capture needs two full caps (64).
constexpr uint32_t kRingSlotCount = Vixen::RenderGraph::FrameSyncNodeConfig::MAX_FRAMES_IN_FLIGHT;
constexpr uint32_t kReservoirHistoryCap = 32;
constexpr uint32_t kMaxFramesPerScenario = 2 * kReservoirHistoryCap;
constexpr uint32_t kDefaultFramesPerScenario = 2 * kRingSlotCount;
constexpr double kDefaultProcessBudgetMs = 300000.0; // Cold-driver dzn compile is ~100 s; see reports/capturespeed.md.
constexpr float kPitchRadians = 0.45f;

using CaptureClock = std::chrono::steady_clock;

double MsBetween(CaptureClock::time_point from, CaptureClock::time_point to) {
    return std::chrono::duration<double, std::milli>(to - from).count();
}

double Percentile(std::vector<double> samples, double fraction) {
    if (samples.empty()) return 0.0;
    std::sort(samples.begin(), samples.end());
    const auto index = static_cast<size_t>(fraction * static_cast<double>(samples.size() - 1) + 0.5);
    return samples[std::min(index, samples.size() - 1)];
}

uint32_t DefaultFramesPerScenario() {
    return Vixen::RenderGraph::ResolveReservoirEnabled() ? kMaxFramesPerScenario
                                                          : kDefaultFramesPerScenario;
}

uint32_t CaptureFramesPerScenario(std::string& error) {
    const char* requested = std::getenv("VIXEN_LOOKDEV_CAPTURE_FRAMES");
    if (!requested || *requested == '\0') return DefaultFramesPerScenario();

    char* end = nullptr;
    const unsigned long value = std::strtoul(requested, &end, 10);
    if (end == requested || *end != '\0' || value == 0 || value > kMaxFramesPerScenario) {
        error = "VIXEN_LOOKDEV_CAPTURE_FRAMES must be an integer from 1 through " +
            std::to_string(kMaxFramesPerScenario);
        return 0;
    }
    return static_cast<uint32_t>(value);
}

double ProcessBudgetMs() {
    const char* requested = std::getenv("VIXEN_LOOKDEV_CAPTURE_BUDGET_MS");
    if (!requested || *requested == '\0') return kDefaultProcessBudgetMs;
    char* end = nullptr;
    const double value = std::strtod(requested, &end);
    return (end == requested || *end != '\0' || value <= 0.0) ? kDefaultProcessBudgetMs : value;
}

struct CaptureScenario {
    Vixen::App::Lookdev::LightingPreset lighting;
    float yawRadians = 0.0f;
    std::string filename;
};

std::vector<CaptureScenario> BuildScenarios() {
    constexpr std::array<float, 4> yaws{{0.0f, 1.57079632679f, 3.14159265359f, 4.71238898038f}};
    const auto& presets = Vixen::App::Lookdev::LightingPresets();
    const char* requestedState = std::getenv("VIXEN_LOOKDEV_CAPTURE_STATE");
    const char* requestedAngleText = std::getenv("VIXEN_LOOKDEV_CAPTURE_ANGLE");
    int requestedAngle = -1;
    if (requestedAngleText && *requestedAngleText != '\0') {
        char* end = nullptr;
        const long parsed = std::strtol(requestedAngleText, &end, 10);
        if (end != requestedAngleText && *end == '\0' && parsed >= 0 && parsed < 4) {
            requestedAngle = static_cast<int>(parsed);
        }
    }
    std::vector<CaptureScenario> scenarios;
    scenarios.reserve(presets.size() * yaws.size());
    for (const auto& preset : presets) {
        if (requestedState && preset.name != std::string(requestedState)) continue;
        for (size_t angle = 0; angle < yaws.size(); ++angle) {
            if (requestedAngle >= 0 && static_cast<int>(angle) != requestedAngle) continue;
            scenarios.push_back({preset, yaws[angle],
                std::string(preset.name) + "-angle-" + std::to_string(angle) + ".png"});
        }
    }
    return scenarios;
}

class LookdevCaptureApplication final : public VulkanGraphApplication {
public:
    LookdevCaptureApplication(std::filesystem::path outputDir,
                              std::vector<CaptureScenario> scenarios,
                              uint32_t framesPerScenario)
        : outputDir_(std::move(outputDir)), scenarios_(std::move(scenarios)),
          framesPerScenario_(framesPerScenario) {
        SetPresentationTarget(PresentationTarget::Offscreen);
    }

    void SetProcessStart(CaptureClock::time_point start) { processStart_ = start; }

    void BuildRenderGraph() override {
        const auto buildStart = CaptureClock::now();
        buildGraphBeginMs_ = MsBetween(processStart_, buildStart);
        BuildRenderGraphTimed();
        buildGraphEndMs_ = MsBetween(processStart_, CaptureClock::now());
    }

    void PrintTimingSummary() const {
        const double total = frameWallMs_.empty() ? 0.0
            : std::accumulate(frameWallMs_.begin(), frameWallMs_.end(), 0.0);
        std::printf("[CAPTURE_TIMING] startup_to_build_graph_ms=%.1f build_graph_ms=%.1f "
                    "build_graph_end_to_first_frame_ms=%.1f first_frame_ms=%.1f startup_total_ms=%.1f\n",
            buildGraphBeginMs_, buildGraphEndMs_ - buildGraphBeginMs_,
            firstFrameBeginMs_ - buildGraphEndMs_,
            frameWallMs_.empty() ? 0.0 : frameWallMs_.front(), firstFrameBeginMs_);
        std::printf("[CAPTURE_TIMING] frames=%zu frame_total_ms=%.1f frame_p50_ms=%.2f frame_p95_ms=%.2f "
                    "frame_max_ms=%.2f png_capture_total_ms=%.1f\n",
            frameWallMs_.size(), total, Percentile(frameWallMs_, 0.50), Percentile(frameWallMs_, 0.95),
            Percentile(frameWallMs_, 1.0), captureWallMs_);
        std::fflush(stdout);
    }

    void BuildRenderGraphTimed() {
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
        // prove the composed DDA fallback. When the existing stored-SDF scene is
        // requested, keep its six FORMAT_STORED_SDF instances and orbit that
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
        Vixen::App::ApplySceneLook(
            *lighting_,
            graph ? dynamic_cast<Vixen::RenderGraph::SkySphereNode*>(graph->GetInstanceByName("sky_sphere")) : nullptr,
            Vixen::App::Lookdev::SceneLookDeclaration());
        if (!storedSdfScene) {
            SetBodyInstances(Vixen::App::Lookdev::BodyInstances(
                recipes, scenarios_.front().lighting));
        }
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
        const auto frameStart = CaptureClock::now();
        if (frameWallMs_.empty()) firstFrameBeginMs_ = MsBetween(processStart_, frameStart);
        if (!captureError_.empty() || !VulkanGraphApplication::Render()) return false;
        frameWallMs_.push_back(MsBetween(frameStart, CaptureClock::now()));
        if (!modulePushChecked_ && std::getenv("VIXEN_LOOKDEV_TEST_INDIRECT_LIGHTING")) {
            modulePushChecked_ = true;
            auto* graph = GetRenderGraph();
            auto* gatherer = graph ? graph->GetInstanceByName("spatial_reuse_push_gatherer") : nullptr;
            auto* packedResource = gatherer
                ? gatherer->GetOutput(PushConstantGathererNodeConfig::PUSH_CONSTANT_DATA.index)
                : nullptr;
            if (!packedResource || !packedResource->IsValid()) {
                captureError_ = "indirect-light test module did not produce push-constant data";
                return false;
            }
            const auto packed = packedResource->GetHandle<std::vector<uint8_t>>();
            constexpr size_t kModuleIrradianceOffset = 96;
            constexpr glm::vec3 kExpectedModuleIrradiance{2.0f, 0.18f, 0.04f};
            if (packed.size() < kModuleIrradianceOffset + sizeof(kExpectedModuleIrradiance)) {
                captureError_ = "indirect-light push-constant block is too short: " +
                    std::to_string(packed.size()) + " bytes";
                return false;
            }
            glm::vec3 packedIrradiance{};
            std::memcpy(&packedIrradiance, packed.data() + kModuleIrradianceOffset,
                        sizeof(packedIrradiance));
            if (std::memcmp(&packedIrradiance, &kExpectedModuleIrradiance,
                            sizeof(packedIrradiance)) != 0) {
                std::ostringstream message;
                message << "indirect-light module push value mismatch: got ("
                        << packedIrradiance.x << ", " << packedIrradiance.y << ", "
                        << packedIrradiance.z << "), packedBytes=" << packed.size();
                auto* variadic = dynamic_cast<IVariadicNode*>(gatherer);
                const auto* slot = variadic ? variadic->GetVariadicSlotInfo(14) : nullptr;
                message << ", variadicSlot14=" << (slot ? "present" : "missing");
                if (slot) {
                    message << ", sourceHandle=" << slot->sourceNode.index
                            << ", sourceOutput=" << slot->sourceOutput
                            << ", resource=" << (slot->resource ? "present" : "null");
                }
                auto* sourceNode = graph->GetInstanceByName(
                    "capture_test_indirect_lighting/irradiance");
                auto* sourceOutput = sourceNode
                    ? sourceNode->GetOutput(TestIndirectIrradianceNodeConfig::INDIRECT_IRRADIANCE.index)
                    : nullptr;
                message << ", moduleOutput=" << (sourceOutput && sourceOutput->IsValid()
                    ? "valid" : "missing");
                if (sourceOutput && sourceOutput->IsValid()) {
                    const glm::vec3 sourceValue = sourceOutput->GetHandle<glm::vec3>();
                    message << " (" << sourceValue.x << ", " << sourceValue.y
                            << ", " << sourceValue.z << ")";
                }
                captureError_ = message.str();
                return false;
            }
        }
        ++framesInScenario_;
        if (framesInScenario_ < framesPerScenario_) return true;

        const auto& scenario = scenarios_[scenarioIndex_];
        // Verifying every LightingConfig ring slot needs each slot rewritten at least once;
        // a capture shorter than the ring only exercises rendering.
        if (scenarioIndex_ == 0 && framesPerScenario_ >= kRingSlotCount &&
            !VerifyUploadedExposure(scenario)) {
            return false;
        }
        const std::filesystem::path output = outputDir_ / scenario.filename;
        const auto captureStart = CaptureClock::now();
        if (!CaptureOffscreenFrameToPng(output.string(), captureError_)) return false;
        captureWallMs_ += MsBetween(captureStart, CaptureClock::now());
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
    uint32_t framesPerScenario_ = kDefaultFramesPerScenario;
    CaptureClock::time_point processStart_ = CaptureClock::now();
    double buildGraphBeginMs_ = 0.0;
    double buildGraphEndMs_ = 0.0;
    double firstFrameBeginMs_ = 0.0;
    double captureWallMs_ = 0.0;
    std::vector<double> frameWallMs_;
    Vixen::RenderGraph::CameraNode* camera_ = nullptr;
    Vixen::RenderGraph::LightingConfigNode* lighting_ = nullptr;
    size_t scenarioIndex_ = 0;
    size_t capturedCount_ = 0;
    uint32_t framesInScenario_ = 0;
    bool modulePushChecked_ = false;
    std::string captureError_;
    VkDevice snapshotDevice_ = VK_NULL_HANDLE;
    VkBuffer snapshotBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory snapshotMemory_ = VK_NULL_HANDLE;
    VkDeviceSize snapshotSize_ = 0;
};

bool CaptureSequence(const std::filesystem::path& outputDir,
                     const std::vector<CaptureScenario>& scenarios,
                     uint32_t framesPerScenario,
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

    LookdevCaptureApplication app(outputDir, scenarios, framesPerScenario);
    const auto processStart = CaptureClock::now();
    app.SetProcessStart(processStart);
    const uint32_t frameCount = framesPerScenario * static_cast<uint32_t>(scenarios.size());
    const int result = app.Run(RunOptions{.exitAfterFrames = frameCount});
    const double processMs = MsBetween(processStart, CaptureClock::now());
    const double budgetMs = ProcessBudgetMs();
    app.PrintTimingSummary();
    std::printf("[CAPTURE_TIMING] frames_per_angle=%u process_total_ms=%.1f budget_ms=%.0f\n",
        framesPerScenario, processMs, budgetMs);
    std::fflush(stdout);
    if (result != 0 || app.CapturedCount() != scenarios.size()) {
        error = app.CaptureError().empty()
            ? "application exited before all look-dev captures were written"
            : app.CaptureError();
        return false;
    }
    if (processMs > budgetMs) {
        error = "look-dev capture exceeded its runtime budget: " + std::to_string(processMs) +
            " ms > " + std::to_string(budgetMs) + " ms (VIXEN_LOOKDEV_CAPTURE_BUDGET_MS)";
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
    const bool oneAngleRequested = std::getenv("VIXEN_LOOKDEV_CAPTURE_ANGLE") != nullptr;
    ASSERT_EQ(scenarios.size(), oneAngleRequested ? 1u : 4u)
        << "CTest must select one supported look-dev lighting preset";
    std::string error;
    const uint32_t framesPerScenario = CaptureFramesPerScenario(error);
    ASSERT_NE(framesPerScenario, 0u) << error;
    ASSERT_TRUE(CaptureSequence(outputDir, scenarios, framesPerScenario, error)) << error;
}

} // namespace
