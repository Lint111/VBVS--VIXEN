// Recipe edit-to-visible latency per edit class (op-recipeedit1, R864).
//
// Runs the look-dev scene (optionally tiled, optionally with extra registered recipes) and fires
// one edit at a time from a fixed schedule, waiting for each to become visible before the next:
//
//   param      a runtime recipe parameter (the Lamp's recipeParams[3]) through SetBodyInstances
//   transform  the Hull instances moved                     (SetBodyInstances)
//   material   the Hull instances recoloured                (SetBodyInstances)
//   literal    one literal operand of the Hull program      (ReplaceProceduralRecipe + recompile)
//   structural one primitive + union appended to the Hull   (ReplaceProceduralRecipe + recompile)
//   revert     the Hull program back to its original bytes  (recompile of an already-seen source)
//   newrecipe  a new recipe id registered, Hull instances moved onto it at the swap
//
// edits.csv has one row per edit with steady-clock nanoseconds (the FrameTrace clock):
//   t_call       before the edit call;           t_mutated  after Replace/Register (CPU derivations)
//   t_call_end   after RecompileProceduralShader returned (source emission on the calling thread)
//   frame_swap   the frame whose Update applied the swap (data edits: the call frame)
//   t_swap_begin / t_swap_end  that Update (device drain + graph recompile of the procedural wave)
//   t_submit     end of the Render() of frame_swap: the first submit that carries the edit
// frames.csv has one row per frame (update and render ms, swap-pending flag) for the frame-thread cost
// while a compile runs. The per-phase split (emit, SPIR-V build per library, per-node recompile) is in
// the frame trace (benchmark build, VIXEN_FRAME_TRACE), categories "recipe-edit" and "compile".
//
// Usage: vixen_recipe_edit_bench --out DIR [--tier 0..3] [--extra-recipes N] [--repeats N]
//        (defaults from RECIPE_EDIT_TIER / RECIPE_EDIT_EXTRA_RECIPES / RECIPE_EDIT_REPEATS)

#include "VulkanGraphApplication.h"
#include "FrameTrace.h"
#include "graph/LookdevSceneDefinition.h"
#include "Nodes/CameraNode.h"
#include "Nodes/LightingConfigNode.h"
#include "Data/Nodes/CameraNodeConfig.h"
#include "Core/NodeRegistration.h"
#include "Core/RenderGraph.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace {

namespace Lookdev = Vixen::App::Lookdev;
using Instance = Vixen::SVO::BodyInstanceGpu;
using Vixen::SVO::RecipeRegistry;

constexpr float kFovDegrees = 45.0f;
constexpr float kCopySpacing = 12.0f;
constexpr glm::vec3 kSceneCenter{5.0f, 5.0f, 5.0f};
constexpr std::array<int, 4> kTierSide{{1, 3, 7, 13}};
constexpr uint32_t kExtraRecipeBase = 0x45580000u;   // "EX": filler recipes for registry-size scaling
constexpr uint32_t kNewRecipeBase = 0x4e450000u;     // "NE": recipes added by the newrecipe edit
constexpr uint32_t kWarmFrames = 90;
constexpr uint32_t kSettleFrames = 12;               // frames between a visible edit and the next one
constexpr uint32_t kFrameCap = 20000;                // safety cap; the schedule ends the run

uint64_t Now() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

enum class EditClass : uint8_t { Param, Transform, Material, Literal, Revert, Structural, NewRecipe };

const char* Name(EditClass c) {
    switch (c) {
        case EditClass::Param: return "param";
        case EditClass::Transform: return "transform";
        case EditClass::Material: return "material";
        case EditClass::Literal: return "literal";
        case EditClass::Revert: return "revert";
        case EditClass::Structural: return "structural";
        case EditClass::NewRecipe: return "newrecipe";
    }
    return "?";
}

bool Compiles(EditClass c) {
    return c == EditClass::Literal || c == EditClass::Revert || c == EditClass::Structural ||
           c == EditClass::NewRecipe;
}

// One cycle; each compile edit is followed by a revert so the next edit starts from the base source.
constexpr std::array<EditClass, 10> kCycle{{
    EditClass::Param, EditClass::Transform, EditClass::Material,
    EditClass::Literal, EditClass::Revert,
    EditClass::Structural, EditClass::Revert,
    EditClass::NewRecipe,
    EditClass::Literal, EditClass::Revert}};

struct EditRecord {
    uint32_t index = 0;
    EditClass cls{};
    uint32_t frameCall = 0;
    uint64_t tCall = 0, tMutated = 0, tCallEnd = 0;
    uint32_t frameSwap = 0;
    uint64_t tSwapBegin = 0, tSwapEnd = 0, tSubmit = 0;
};

class RecipeEditBenchApplication final : public VulkanGraphApplication {
public:
    RecipeEditBenchApplication(std::filesystem::path out, int tier, uint32_t extraRecipes, uint32_t repeats)
        : out_(std::move(out)), tier_(tier), extraRecipes_(extraRecipes) {
        SetPresentationTarget(PresentationTarget::Offscreen);
        auto config = GetRenderPathConfig();
        config.samplingPath = Vixen::RenderGraph::ReconstructionSamplingPath::CanonicalCapture;
        SetRenderPathConfig(config);
        for (uint32_t r = 0; r < repeats; ++r)
            for (EditClass c : kCycle) schedule_.push_back(c);
        edits_.open(out_ / "edits.csv");
        edits_ << "edit,class,frame_call,t_call,t_mutated,t_call_end,frame_swap,t_swap_begin,t_swap_end,t_submit,"
                  "tier,instances,registered_recipes\n";
        frames_.open(out_ / "frames.csv");
        frames_ << "frame,update_ms,render_ms,swap_pending,edit_in_flight\n";
    }

    void BuildRenderGraph() override {
        const Lookdev::LightingPreset& first = Lookdev::LightingPresets().front();
        recipes_ = Lookdev::BuildMaterialRecipes(first);
        for (const auto& recipe : recipes_) {
            if (RegisterProceduralRecipe(recipe.recipeId, recipe.entry) != RecipeRegistry::RegisterResult::Ok) {
                error_ = "could not register look-dev recipe " + std::to_string(recipe.recipeId);
                return;
            }
            if (recipe.material == Lookdev::Material::Hull) {
                hullOriginal_ = recipe.entry;
                hullBase_ = recipe.entry;
                hullId_ = recipe.recipeId;
            }
        }
        if (hullOriginal_.bytecode.empty()) { error_ = "no Hull recipe"; return; }
        // Registry-size scaling: filler copies of the Hull with one distinct literal each, never
        // instanced; each one is one more field function in every procedural source.
        for (uint32_t i = 0; i < extraRecipes_; ++i) {
            auto entry = hullOriginal_;
            entry.bytecode.front().data[0] *= 1.0f + 0.001f * static_cast<float>(i + 1);
            if (RegisterProceduralRecipe(kExtraRecipeBase + i, std::move(entry)) != RecipeRegistry::RegisterResult::Ok) {
                error_ = "could not register filler recipe " + std::to_string(i);
                return;
            }
        }
        registered_ = static_cast<uint32_t>(recipes_.size()) + extraRecipes_;
        VulkanGraphApplication::BuildRenderGraph();
        auto* graph = GetRenderGraph();
        camera_ = graph ? dynamic_cast<Vixen::RenderGraph::CameraNode*>(graph->GetInstanceByName("raymarch_camera")) : nullptr;
        auto* lighting = graph ? dynamic_cast<Vixen::RenderGraph::LightingConfigNode*>(graph->GetInstanceByName("lighting_config")) : nullptr;
        if (!camera_ || !lighting) { error_ = "production graph is missing raymarch_camera or lighting_config"; return; }
        camera_->SetParameter(Vixen::RenderGraph::CameraNodeConfig::PARAM_FOV, kFovDegrees);
        Vixen::App::SetSceneLightsLayer(*lighting, Vixen::RenderGraph::LookLightSet::From(
            Lookdev::Lights(first), first.ambient, Lookdev::CelSpillPurposeScales(first)));
        auto look = Lookdev::SceneLookDeclaration();
        look.exposure.compensationEV = first.exposureCompensationEV;
        Vixen::App::ApplySceneLook(*lighting,
            dynamic_cast<Vixen::RenderGraph::SkySphereNode*>(graph->GetInstanceByName("sky_sphere")), look);
        baseInstances_ = Lookdev::BodyInstances(recipes_, first);
        PushInstances();
    }

    void Update() override {
        const bool pendingBefore = IsProceduralShaderSwapPending();
        const uint64_t t0 = Now();
        VulkanGraphApplication::Update();
        const uint64_t t1 = Now();
        updateMs_ = static_cast<double>(t1 - t0) / 1e6;
        // The swap is applied inside Update (PollProceduralShaderSwap + RecompileDirtyNodes).
        if (inFlight_ && Compiles(inFlight_->cls) && pendingBefore && !IsProceduralShaderSwapPending()) {
            inFlight_->frameSwap = frame_;
            inFlight_->tSwapBegin = t0;
            inFlight_->tSwapEnd = t1;
            swapThisFrame_ = true;
        }
    }

    bool Render() override {
        if (!error_.empty()) return false;
        if (swapThisFrame_ && inFlight_->cls == EditClass::NewRecipe) {
            hullId_ = pendingNewId_;   // the new id is in the pipelines now: move the instances onto it
            hullBase_ = pendingNewBase_;
            PushInstances();
        }
        if (!inFlight_ && frame_ >= nextEditFrame_ && next_ < schedule_.size()) {
            if (!FireEdit(schedule_[next_])) return false;
        }
        // Fixed orbit pose (the load scenario's orbit at its first frame), set every frame.
        const float half = 0.5f * static_cast<float>(kTierSide[static_cast<size_t>(tier_)]) * kCopySpacing;
        const float radius = std::max(14.0f, half * 1.1f);
        const glm::vec3 dir{std::cos(0.4f) * std::cos(0.45f), std::sin(0.45f), std::sin(0.4f) * std::cos(0.45f)};
        camera_->SetPositionForTest(kSceneCenter + dir * radius);
        camera_->SetLookTargetNoOrbitForTest(kSceneCenter);
        const uint64_t r0 = Now();
        if (!VulkanGraphApplication::Render()) return false;
        const uint64_t r1 = Now();
        if (inFlight_ && (swapThisFrame_ || !Compiles(inFlight_->cls))) {
            inFlight_->tSubmit = r1;
            if (!Compiles(inFlight_->cls)) { inFlight_->frameSwap = frame_; inFlight_->tSwapBegin = inFlight_->tSwapEnd = 0; }
            WriteEdit(*inFlight_);
            inFlight_.reset();
            nextEditFrame_ = frame_ + kSettleFrames;
            if (next_ >= schedule_.size()) doneFrame_ = frame_ + kSettleFrames;
        }
        frames_ << frame_ << ',' << updateMs_ << ',' << static_cast<double>(r1 - r0) / 1e6 << ','
                << (IsProceduralShaderSwapPending() ? 1 : 0) << ',' << (inFlight_ ? 1 : 0) << '\n';
        swapThisFrame_ = false;
        ++frame_;
        if (doneFrame_ != 0 && frame_ >= doneFrame_) done_ = true;
        if (frame_ >= kFrameCap) { error_ = "frame cap reached before the schedule finished"; return false; }
        return !done_;
    }

    bool IsShutdownRequested() const override { return done_ || VulkanGraphApplication::IsShutdownRequested(); }
    const std::string& Error() const { return error_; }
    bool Done() const { return done_; }
    uint32_t EditsWritten() const { return written_; }
    size_t EditsScheduled() const { return schedule_.size(); }

private:
    void PushInstances() {
        const int side = kTierSide[static_cast<size_t>(tier_)];
        std::vector<Instance> instances;
        instances.reserve(baseInstances_.size() * static_cast<size_t>(side * side));
        for (int i = 0; i < side; ++i) {
            for (int j = 0; j < side; ++j) {
                const glm::vec3 offset{(static_cast<float>(i) - 0.5f * static_cast<float>(side - 1)) * kCopySpacing, 0.0f,
                                       (static_cast<float>(j) - 0.5f * static_cast<float>(side - 1)) * kCopySpacing};
                for (Instance instance : baseInstances_) {
                    const bool hull = instance.material.recipeId == Lookdev::RecipeId(Lookdev::Material::Hull);
                    const bool lamp = instance.material.recipeId == Lookdev::RecipeId(Lookdev::Material::Lamp);
                    glm::vec3 move = offset;
                    if (hull) {
                        move.y += hullLift_;
                        instance.material.color[1] *= hullTint_;
                        instance.material.recipeId = hullId_;
                    }
                    if (lamp) instance.material.recipeParams[3] *= lampGain_;
                    for (int row = 0; row < 3; ++row) {
                        instance.transform.localToWorld.rows[row][3] += move[row];
                        instance.transform.worldToLocal.rows[row][3] -= move[row];
                    }
                    instances.push_back(instance);
                }
            }
        }
        instanceCount_ = instances.size();
        SetBodyInstances(std::move(instances));
    }

    bool FireEdit(EditClass cls) {
        EditRecord rec;
        rec.index = next_++;
        rec.cls = cls;
        rec.frameCall = frame_;
        rec.tCall = Now();
        const float k = static_cast<float>(rec.index + 1);
        bool recompile = false;
        RecipeRegistry::RegisterResult result = RecipeRegistry::RegisterResult::Ok;
        switch (cls) {
            case EditClass::Param:     lampGain_ = 1.0f + 0.05f * std::fmod(k, 7.0f); PushInstances(); break;
            case EditClass::Transform: hullLift_ = 0.05f * std::fmod(k, 5.0f);       PushInstances(); break;
            case EditClass::Material:  hullTint_ = 0.8f + 0.03f * std::fmod(k, 6.0f); PushInstances(); break;
            case EditClass::Literal: {
                // A never-seen value each time, so the source is new (no shader-cache hit).
                auto entry = hullBase_;
                for (int axis = 0; axis < 3; ++axis) entry.bytecode.front().data[axis] *= 1.0f + 0.0007f * k;
                result = ReplaceProceduralRecipe(hullId_, std::move(entry));
                recompile = true;
                break;
            }
            case EditClass::Structural: {
                // One more primitive (a rounded box above the hull, never-seen size) and a union.
                auto entry = hullBase_;
                Vixen::SVO::Recipe::SdfInstruction box = entry.bytecode.front();
                box.data[0] *= 0.25f + 0.001f * k;
                box.data[2] *= 0.25f;
                box.data[5] += box.data[1] * 1.1f;
                Vixen::SVO::Recipe::SdfInstruction combine{};
                combine.opCode = static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::Union);
                entry.bytecode.push_back(box);
                entry.bytecode.push_back(combine);
                result = ReplaceProceduralRecipe(hullId_, std::move(entry));
                recompile = true;
                break;
            }
            case EditClass::Revert:
                // Back to the bytes this id was registered with: the whole source was compiled
                // before, so this measures the content-keyed SPIR-V cache-hit path.
                result = ReplaceProceduralRecipe(hullId_, hullBase_);
                recompile = true;
                break;
            case EditClass::NewRecipe: {
                auto entry = hullOriginal_;
                entry.bytecode.front().data[1] *= 1.0f + 0.0005f * k;
                pendingNewId_ = kNewRecipeBase + rec.index;
                pendingNewBase_ = entry;
                result = RegisterProceduralRecipe(pendingNewId_, std::move(entry));
                ++registered_;
                recompile = true;
                break;
            }
        }
        rec.tMutated = Now();
        if (result != RecipeRegistry::RegisterResult::Ok) {
            error_ = std::string("edit ") + Name(cls) + " was rejected by the registry";
            return false;
        }
        if (recompile) RecompileProceduralShader();
        rec.tCallEnd = Now();
        inFlight_ = rec;
        return true;
    }

    void WriteEdit(const EditRecord& r) {
        edits_ << r.index << ',' << Name(r.cls) << ',' << r.frameCall << ',' << r.tCall << ',' << r.tMutated << ','
               << r.tCallEnd << ',' << r.frameSwap << ',' << r.tSwapBegin << ',' << r.tSwapEnd << ',' << r.tSubmit << ','
               << tier_ << ',' << instanceCount_ << ',' << registered_ << '\n';
        edits_.flush();
        ++written_;
    }

    std::filesystem::path out_;
    int tier_ = 0;
    uint32_t extraRecipes_ = 0;
    std::ofstream edits_;
    std::ofstream frames_;
    std::vector<Lookdev::MaterialRecipe> recipes_;
    std::vector<Instance> baseInstances_;
    RecipeRegistry::RecipeEntry hullOriginal_{};   // the look-dev Hull as authored
    RecipeRegistry::RecipeEntry hullBase_{};       // the bytes registered under hullId_
    RecipeRegistry::RecipeEntry pendingNewBase_{};
    uint32_t hullId_ = 0;
    uint32_t pendingNewId_ = 0;
    uint32_t registered_ = 0;
    size_t instanceCount_ = 0;
    float hullLift_ = 0.0f;
    float hullTint_ = 1.0f;
    float lampGain_ = 1.0f;
    Vixen::RenderGraph::CameraNode* camera_ = nullptr;
    std::vector<EditClass> schedule_;
    size_t next_ = 0;
    std::optional<EditRecord> inFlight_;
    uint32_t frame_ = 0;
    uint32_t nextEditFrame_ = kWarmFrames;
    uint32_t doneFrame_ = 0;
    bool swapThisFrame_ = false;
    bool done_ = false;
    double updateMs_ = 0.0;
    uint32_t written_ = 0;
    std::string error_;
};

}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path out;
    // Defaults may come from the harness environment (the native .bat runner passes KEY:VALUE pairs as
    // environment variables and cannot carry spaced arguments); explicit flags win.
    const auto envInt = [](const char* name, int fallback) {
        const char* v = std::getenv(name);
        return v && *v ? std::atoi(v) : fallback;
    };
    int tier = envInt("RECIPE_EDIT_TIER", 0);
    uint32_t extra = static_cast<uint32_t>(envInt("RECIPE_EDIT_EXTRA_RECIPES", 0));
    uint32_t repeats = static_cast<uint32_t>(envInt("RECIPE_EDIT_REPEATS", 5));
    for (int i = 1; i + 1 < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--out") out = argv[++i];
        else if (arg == "--tier") tier = std::atoi(argv[++i]);
        else if (arg == "--extra-recipes") extra = static_cast<uint32_t>(std::atoi(argv[++i]));
        else if (arg == "--repeats") repeats = static_cast<uint32_t>(std::atoi(argv[++i]));
    }
    if (out.empty() || tier < 0 || tier > 3 || repeats == 0) {
        std::fprintf(stderr, "usage: vixen_recipe_edit_bench --out DIR [--tier 0..3] [--extra-recipes N] [--repeats N]\n");
        return 2;
    }
    std::error_code ec;
    std::filesystem::create_directories(out, ec);
    if (ec) {
        std::fprintf(stderr, "could not create %s: %s\n", out.string().c_str(), ec.message().c_str());
        return 2;
    }
    Vixen::Log::Logger::ApplyLevelFromEnvironment(Vixen::Log::LogLevel::LOG_INFO);
    RecipeEditBenchApplication app(out, tier, extra, repeats);
    const int result = app.Run(RunOptions{.exitAfterFrames = kFrameCap + 2u});
    if (result != 0 || !app.Error().empty() || !app.Done() || app.EditsWritten() != app.EditsScheduled()) {
        std::fprintf(stderr, "recipe edit bench failed: result=%d done=%d edits=%u/%zu error=%s\n", result,
                     app.Done() ? 1 : 0, app.EditsWritten(), app.EditsScheduled(), app.Error().c_str());
        return 1;
    }
    std::printf("[RECIPE_EDIT_BENCH] edits=%u ok\n", app.EditsWritten());
    return 0;
}
