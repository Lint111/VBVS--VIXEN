// Deterministic heavy dynamic load scenario (sn-loadscene1, R853).
//
// One scripted timeline over the look-dev scene: instance-count tiers, live recipe edits and
// swaps, lighting/view switches, per-frame material changes and camera paths (fly-through, fast
// pan, close-up). Every input is a pure function of the frame index (no wall clock, no random
// source), so the same build produces the same frames; hashes of the captured frames at fixed
// timeline points are written to hashes.csv. Per-frame timings come from the existing perf CSV
// (VIXEN_PERF_CSV, benchmark build); this executable adds timeline.csv (frame -> leg, tier,
// instance count, event) and vram.csv (device memory budget and usage at each leg end).
//
// Reuses only existing seams: RegisterProceduralRecipe / ReplaceProceduralRecipe /
// RecompileProceduralShader, SetBodyInstances, the camera test setters and the scene lights and
// look layers. Resolution comes from the existing VIXEN_WINDOW_WIDTH/HEIGHT; illumination
// switches are the existing feature switches (see reports/sn-loadscene1.md).
//
// Usage: vixen_load_scenario --out DIR

#include "VulkanGraphApplication.h"
#include "FrameTrace.h"
#include "graph/LookdevSceneDefinition.h"
#include "Nodes/CameraNode.h"
#include "Nodes/DeviceNode.h"
#include "Nodes/LightingConfigNode.h"
#include "Data/Nodes/CameraNodeConfig.h"
#include "Core/NodeRegistration.h"
#include "Core/RenderGraph.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace Lookdev = Vixen::App::Lookdev;
using Instance = Vixen::SVO::BodyInstanceGpu;

constexpr float kFovDegrees = 45.0f;
constexpr float kCopySpacing = 12.0f;                       // world units between look-dev copies
constexpr glm::vec3 kSceneCenter{5.0f, 5.0f, 5.0f};         // the look-dev scene's own center
constexpr uint32_t kExtraRecipeId = 0x4c4f4144u;            // LOAD: a recipe swapped in mid-run

// Copies per side for each instance tier: 1x1, 3x3, 7x7, 13x13 copies of the 13-instance scene.
constexpr std::array<int, 4> kTierSide{{1, 3, 7, 13}};

enum class Path : uint8_t { Orbit, Flythrough, FastPan, CloseUp };

struct Leg {
    const char* name;
    uint32_t frames;
    int tier;          // instance tier from the start of the leg
    Path path;
    // Events: every `editEvery` frames of the leg (0 = never).
    uint32_t recipeEditEvery;   // ReplaceProceduralRecipe + RecompileProceduralShader
    uint32_t materialEvery;     // per-frame-style material colour/param change pushed via SetBodyInstances
    uint32_t viewEvery;         // lighting preset (view) switch
    bool recipeSwap;            // register a new recipe and move instances onto it at the leg start
    bool capture;               // hash a frame at the end of the leg
};

// The timeline. 840 frames: about 14 s of the 60 fps budget.
constexpr std::array<Leg, 9> kLegs{{
    {"warm-orbit-t0",        60, 0, Path::Orbit,      0,  0,  0, false, true },
    {"flythrough-t0",       120, 0, Path::Flythrough, 0,  0,  0, false, true },
    {"fastpan-t1",           60, 1, Path::FastPan,    0,  0,  0, false, true },
    {"recipe-edits-t1",      60, 1, Path::Orbit,     20,  5,  0, false, true },
    {"view-switches-t1",     60, 1, Path::Orbit,      0,  0, 15, false, true },
    {"closeup-t2",          120, 2, Path::CloseUp,    0,  0,  0, false, true },
    {"flythrough-t3",       120, 3, Path::Flythrough, 0,  0,  0, false, true },
    {"mixed-t3",            120, 3, Path::CloseUp,   40,  1, 30, true,  true },
    {"scale-down-t1",       120, 1, Path::FastPan,    0,  0,  0, false, true },
}};

uint32_t TotalFrames() {
    uint32_t total = 0;
    for (const Leg& leg : kLegs) total += leg.frames;
    return total;
}

uint64_t Fnv1a(const std::string& bytes) {
    uint64_t hash = 1469598103934665603ull;
    for (const unsigned char c : bytes) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

float Smooth(float t) { return t * t * (3.0f - 2.0f * t); }

struct Pose {
    glm::vec3 eye;
    glm::vec3 target;
};

// Half extent of the instance field for a tier, in world units around kSceneCenter.
float FieldHalfExtent(int tier) {
    return 0.5f * static_cast<float>(kTierSide[static_cast<size_t>(tier)] - 1) * kCopySpacing + 6.0f;
}

Pose PoseFor(Path path, int tier, uint32_t frameInLeg, uint32_t legFrames) {
    const float t = legFrames > 1 ? static_cast<float>(frameInLeg) / static_cast<float>(legFrames - 1) : 0.0f;
    const float half = FieldHalfExtent(tier);
    switch (path) {
    case Path::Orbit: {
        const float yaw = 0.02f * static_cast<float>(frameInLeg) + 0.4f;
        const float pitch = 0.45f;
        const float radius = std::max(14.0f, half * 1.1f);
        const glm::vec3 offset{std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch)};
        return {kSceneCenter + offset * radius, kSceneCenter};
    }
    case Path::Flythrough: {
        // Low pass across the whole field, diagonal, rising slightly, looking along the path.
        const auto at = [&](float s) {
            const float u = std::clamp(s, 0.0f, 1.0f);
            return glm::vec3{kSceneCenter.x - half + 2.0f * half * u,
                             2.5f + 3.5f * std::sin(u * 3.14159265f),
                             kSceneCenter.z - half * 0.8f + 1.6f * half * Smooth(u)};
        };
        return {at(t), at(t + 0.06f) + glm::vec3(0.0f, -0.4f, 0.0f)};
    }
    case Path::FastPan: {
        const glm::vec3 eye{kSceneCenter.x, 3.5f, kSceneCenter.z + std::max(8.0f, half * 0.5f)};
        const float angle = 6.28318530718f * t;   // one full turn over the leg
        return {eye, eye + glm::vec3{std::cos(angle), -0.12f, std::sin(angle)} * 10.0f};
    }
    case Path::CloseUp: {
        // Glide in to a fixed surface point of the main structure, then back out.
        const glm::vec3 focus{5.0f, 3.0f, 5.0f};
        const float depth = 0.5f - 0.5f * std::cos(6.28318530718f * t);   // 0 -> 1 -> 0
        const float distance = 16.0f + (1.6f - 16.0f) * Smooth(depth);
        const glm::vec3 dir = glm::normalize(glm::vec3{0.5f, 0.35f, 1.0f});
        return {focus + dir * distance, focus};
    }
    }
    return {kSceneCenter, kSceneCenter};
}

// Tiles the look-dev instances (one per material recipe) over side x side copies centered on the
// original scene. `recipeOverride` != 0 moves the Hull instances onto that recipe id (recipe swap).
std::vector<Instance> BuildTier(const std::vector<Instance>& base, int tier, uint32_t recipeOverride,
                                float colourPhase) {
    const int side = kTierSide[static_cast<size_t>(tier)];
    std::vector<Instance> instances;
    instances.reserve(base.size() * static_cast<size_t>(side) * static_cast<size_t>(side));
    for (int i = 0; i < side; ++i) {
        for (int j = 0; j < side; ++j) {
            const glm::vec3 offset{(static_cast<float>(i) - 0.5f * static_cast<float>(side - 1)) * kCopySpacing,
                                   0.0f,
                                   (static_cast<float>(j) - 0.5f * static_cast<float>(side - 1)) * kCopySpacing};
            for (Instance instance : base) {
                // Base transforms are pure translations (unit scale, no rotation), so the copy
                // offset adds to the local-to-world translation and subtracts from its inverse.
                for (int row = 0; row < 3; ++row) {
                    instance.transform.localToWorld.rows[row][3] += offset[row];
                    instance.transform.worldToLocal.rows[row][3] -= offset[row];
                }
                // Deterministic per-copy colour variation; the phase animates it across frames.
                const float v = 0.5f + 0.5f * std::sin(colourPhase + 0.7f * static_cast<float>(i) +
                                                       1.3f * static_cast<float>(j));
                instance.material.color[0] *= 0.75f + 0.25f * v;
                instance.material.color[2] *= 1.0f - 0.25f * v;
                if (recipeOverride != 0 && instance.material.recipeId == Lookdev::RecipeId(Lookdev::Material::Hull))
                    instance.material.recipeId = recipeOverride;
                instances.push_back(instance);
            }
        }
    }
    return instances;
}

class LoadScenarioApplication final : public VulkanGraphApplication {
public:
    explicit LoadScenarioApplication(std::filesystem::path outputDir) : outputDir_(std::move(outputDir)) {
        SetPresentationTarget(PresentationTarget::Offscreen);
        auto config = GetRenderPathConfig();
        config.samplingPath = Vixen::RenderGraph::ReconstructionSamplingPath::CanonicalCapture;
        SetRenderPathConfig(config);
        timeline_.open(outputDir_ / "timeline.csv");
        timeline_ << "frame,leg,tier,instances,event,render_ms,eye_x,eye_y,eye_z\n";
        hashes_.open(outputDir_ / "hashes.csv");
        hashes_ << "frame,leg,file,fnv1a64\n";
        vram_.open(outputDir_ / "vram.csv");
        vram_ << "frame,leg,heap,budget_bytes,usage_bytes\n";
    }

    void BuildRenderGraph() override {
        const Lookdev::LightingPreset& first = Lookdev::LightingPresets().front();
        recipes_ = Lookdev::BuildMaterialRecipes(first);
        for (const auto& recipe : recipes_) {
            if (RegisterProceduralRecipe(recipe.recipeId, recipe.entry) !=
                Vixen::SVO::RecipeRegistry::RegisterResult::Ok) {
                error_ = "could not register look-dev recipe " + std::to_string(recipe.recipeId);
                return;
            }
        }
        VulkanGraphApplication::BuildRenderGraph();
        auto* graph = GetRenderGraph();
        camera_ = graph ? dynamic_cast<Vixen::RenderGraph::CameraNode*>(graph->GetInstanceByName("raymarch_camera")) : nullptr;
        lighting_ = graph ? dynamic_cast<Vixen::RenderGraph::LightingConfigNode*>(graph->GetInstanceByName("lighting_config")) : nullptr;
        if (!camera_ || !lighting_) {
            error_ = "production graph is missing raymarch_camera or lighting_config";
            return;
        }
        camera_->SetParameter(Vixen::RenderGraph::CameraNodeConfig::PARAM_FOV, kFovDegrees);
        baseInstances_ = Lookdev::BodyInstances(recipes_, first);
        ApplyLighting(0);
        sceneLook_ = Lookdev::SceneLookDeclaration();
        sceneLook_.exposure.compensationEV = first.exposureCompensationEV;
        Vixen::App::ApplySceneLook(*lighting_,
            dynamic_cast<Vixen::RenderGraph::SkySphereNode*>(graph->GetInstanceByName("sky_sphere")), sceneLook_);
        SetBodyInstances(BuildTier(baseInstances_, 0, 0, 0.0f));
        currentCount_ = baseInstances_.size();
    }

    bool Render() override {
        if (!error_.empty()) return false;
        if (frame_ >= TotalFrames()) return VulkanGraphApplication::Render();   // drain ticks
        const Leg* leg = &kLegs[0];
        uint32_t legStart = 0;
        size_t legIndex = 0;
        for (size_t i = 0; i < kLegs.size(); ++i) {
            if (frame_ < legStart + kLegs[i].frames) { leg = &kLegs[i]; legIndex = i; break; }
            legStart += kLegs[i].frames;
        }
        const uint32_t inLeg = frame_ - legStart;
        std::string event;
        if (inLeg == 0) {
            if (legIndex != 0) RecordVram(legIndex - 1);
            event += "leg-start;";
            if (leg->tier != tier_ || leg->recipeSwap) {
                if (leg->recipeSwap && !SwapInRecipe()) return false;
                tier_ = leg->tier;
                SetBodyInstances(BuildTier(baseInstances_, tier_, swapped_ ? kExtraRecipeId : 0u, colourPhase_));
                currentCount_ = baseInstances_.size() * static_cast<size_t>(kTierSide[static_cast<size_t>(tier_)]) *
                                static_cast<size_t>(kTierSide[static_cast<size_t>(tier_)]);
                event += leg->recipeSwap ? "tier+recipe-swap;" : "tier;";
            }
        }
        if (leg->recipeEditEvery && inLeg % leg->recipeEditEvery == 0 && inLeg != 0) {
            if (!EditRecipe(inLeg / leg->recipeEditEvery)) return false;
            event += "recipe-edit;";
        }
        if (leg->viewEvery && inLeg % leg->viewEvery == 0 && inLeg != 0) {
            ApplyLighting(++viewIndex_);
            event += "view-switch;";
        }
        if (leg->materialEvery && inLeg % leg->materialEvery == 0) {
            colourPhase_ += 0.15f;
            SetBodyInstances(BuildTier(baseInstances_, tier_, swapped_ ? kExtraRecipeId : 0u, colourPhase_));
            event += "material;";
        }
        const Pose pose = PoseFor(leg->path, tier_, inLeg, leg->frames);
        camera_->SetPositionForTest(pose.eye);
        camera_->SetLookTargetNoOrbitForTest(pose.target);

        const auto start = std::chrono::steady_clock::now();
        if (!VulkanGraphApplication::Render()) return false;
        const double renderMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

        const bool lastOfLeg = inLeg + 1 == leg->frames;
        if (lastOfLeg && leg->capture) {
            const std::string file = std::string("frame-") + std::to_string(frame_) + ".png";
            const std::filesystem::path path = outputDir_ / file;
            if (!CaptureOffscreenFrameToPng(path.string(), error_)) return false;
            std::ifstream in(path, std::ios::binary);
            const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            hashes_ << frame_ << ',' << leg->name << ',' << file << ',' << std::hex << Fnv1a(bytes) << std::dec << '\n';
            event += "capture;";
        }
        timeline_ << frame_ << ',' << leg->name << ',' << tier_ << ',' << currentCount_ << ',' << event << ','
                  << renderMs << ',' << pose.eye.x << ',' << pose.eye.y << ',' << pose.eye.z << '\n';
        if (frame_ + 1 == TotalFrames()) RecordVram(kLegs.size() - 1);
        ++frame_;
        return true;
    }

    const std::string& Error() const { return error_; }
    uint32_t FramesRun() const { return frame_; }

private:
    void ApplyLighting(uint32_t viewIndex) {
        const auto& presets = Lookdev::LightingPresets();
        const auto& preset = presets[viewIndex % presets.size()];
        Vixen::App::SetSceneLightsLayer(*lighting_, Vixen::RenderGraph::LookLightSet::From(
            Lookdev::Lights(preset), preset.ambient, Lookdev::CelSpillPurposeScales(preset)));
        sceneLook_.exposure.compensationEV = preset.exposureCompensationEV;
        Vixen::App::SetSceneLookLayer(*lighting_, sceneLook_);
    }

    // Live edit of the Hull recipe: scales its first primitive's half extents, then recompiles the
    // procedural shader (the existing recipe live-edit seam).
    bool EditRecipe(uint32_t editIndex) {
        for (auto& recipe : recipes_) {
            if (recipe.material != Lookdev::Material::Hull) continue;
            Vixen::SVO::RecipeRegistry::RecipeEntry entry = recipe.entry;
            const float scale = 1.0f + 0.03f * static_cast<float>(editIndex % 4);
            for (int axis = 0; axis < 3; ++axis) entry.bytecode.front().data[axis] *= scale;
            const uint32_t id = swapped_ ? kExtraRecipeId : recipe.recipeId;
            if (ReplaceProceduralRecipe(id, std::move(entry)) != Vixen::SVO::RecipeRegistry::RegisterResult::Ok) {
                error_ = "recipe replace failed for " + std::to_string(id);
                return false;
            }
            RecompileProceduralShader();
            return true;
        }
        error_ = "no Hull recipe to edit";
        return false;
    }

    // Registers a second Hull-shaped recipe under a new id and (via the next SetBodyInstances) moves
    // the Hull instances onto it: a recipe swap with a shader recompile.
    bool SwapInRecipe() {
        for (const auto& recipe : recipes_) {
            if (recipe.material != Lookdev::Material::Hull) continue;
            Vixen::SVO::RecipeRegistry::RecipeEntry entry = recipe.entry;
            for (int axis = 0; axis < 3; ++axis) entry.bytecode.front().data[axis] *= 0.85f;
            if (RegisterProceduralRecipe(kExtraRecipeId, std::move(entry)) != Vixen::SVO::RecipeRegistry::RegisterResult::Ok) {
                error_ = "recipe swap registration failed";
                return false;
            }
            RecompileProceduralShader();
            swapped_ = true;
            return true;
        }
        error_ = "no Hull recipe to swap";
        return false;
    }

    // Device memory heaps: budget and usage (VK_EXT_memory_budget). Rows are skipped, not faked,
    // when the extension is not available on the device.
    void RecordVram(size_t legIndex) {
        auto* graph = GetRenderGraph();
        auto* deviceNode = graph ? dynamic_cast<Vixen::RenderGraph::DeviceNode*>(graph->GetInstanceByName("main_device")) : nullptr;
        auto* device = deviceNode ? deviceNode->GetVulkanDevice() : nullptr;
        if (!device || !device->gpu) return;
        VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT};
        VkPhysicalDeviceMemoryProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2};
        properties.pNext = &budget;
        vkGetPhysicalDeviceMemoryProperties2(*device->gpu, &properties);
        for (uint32_t heap = 0; heap < properties.memoryProperties.memoryHeapCount; ++heap) {
            if (budget.heapBudget[heap] == 0) continue;
            vram_ << frame_ << ',' << kLegs[legIndex].name << ',' << heap << ',' << budget.heapBudget[heap] << ','
                  << budget.heapUsage[heap] << '\n';
        }
        vram_.flush();
    }

    std::filesystem::path outputDir_;
    std::ofstream timeline_;
    std::ofstream hashes_;
    std::ofstream vram_;
    std::vector<Lookdev::MaterialRecipe> recipes_;
    std::vector<Instance> baseInstances_;
    Vixen::RenderGraph::CameraNode* camera_ = nullptr;
    Vixen::RenderGraph::LightingConfigNode* lighting_ = nullptr;
    Vixen::App::SceneLook sceneLook_;
    uint32_t frame_ = 0;
    int tier_ = 0;
    uint32_t viewIndex_ = 0;
    float colourPhase_ = 0.0f;
    bool swapped_ = false;
    size_t currentCount_ = 0;
    std::string error_;
};

}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path out;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--out") out = argv[i + 1];
    if (out.empty()) {
        std::fprintf(stderr, "usage: vixen_load_scenario --out DIR\n");
        return 2;
    }
    std::error_code ec;
    std::filesystem::create_directories(out, ec);
    if (ec) {
        std::fprintf(stderr, "could not create %s: %s\n", out.string().c_str(), ec.message().c_str());
        return 2;
    }
    // INFO like the player: the DEBUG node logging is a harness artefact (sn-framecpu1).
    Vixen::Log::Logger::ApplyLevelFromEnvironment(Vixen::Log::LogLevel::LOG_INFO);
    LoadScenarioApplication app(out);
    const int result = app.Run(RunOptions{.exitAfterFrames = TotalFrames() + 2u});
    if (result != 0 || !app.Error().empty() || app.FramesRun() != TotalFrames()) {
        std::fprintf(stderr, "load scenario failed: result=%d frames=%u/%u error=%s\n", result, app.FramesRun(),
                     TotalFrames(), app.Error().c_str());
        return 1;
    }
    std::printf("[LOAD_SCENARIO] frames=%u legs=%zu ok\n", TotalFrames(), kLegs.size());
    return 0;
}
