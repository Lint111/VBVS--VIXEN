#include "EditorApplication.h"

// Include-order gotcha (see BuildRenderGraph.cpp's file header): BodyOctreeSceneNode.h /
// RecipeBaker.h transitively pull gaia.h (ShellOctree -> LaineKarrasOctree -> ISVOStructure),
// whose std::hash<> specialisations must be visible BEFORE RmlUi's bundled robin_hood.h wraps
// them, or robin_hood's Table<> instantiations fail with "std::hash<T> has no operator()".
#include "Recipe/RecipeRegistry.h"
#include "Recipe/RecipeBaker.h"
#include "ShellOctreeGpu.h"
#include "Nodes/UIRenderNode.h"               // AFTER the Recipe/gaia includes above
#include "Nodes/UISelectionProviderNode.h"
#include "Nodes/DeviceNode.h"                 // Task 3: VulkanDevice* + queue for CaptureFrameToPng
#include "Data/Nodes/UIRenderNodeConfig.h"
#include "Nodes/CameraNode.h"
#include "Data/Nodes/CameraNodeConfig.h"
#include "Core/RenderGraph.h"
#include "Debug/RenderTargetReadback.h"       // Task 3: shared IRenderTarget -> PNG readback
#include "KeyMap.h"                           // Inc-4 R5a: GLFW keycode -> typed KeyId
#include "generated/AppFlowCallables.g.hpp"   // Inc-4 R5c: transplanted applyToggle(mask,index)
#include <Logger.h>

#include <cstdlib>
#include <sstream>

#define GLFW_INCLUDE_NONE   // don't pull in <GL/gl.h> (absent on headless/WSL builds)
#include <GLFW/glfw3.h>

namespace {
// Inc-2b Task 4: parses "toggle:2@30,undo@60,redo@90" into ScriptedAction entries. A malformed
// token (bad action name, missing '@frame', non-numeric frame/arg) is logged and SKIPPED --
// never aborts the app (Global Constraint: VIXEN_EDITOR_SCRIPT malformed -> warn + continue).
// `logger` may be null (never in practice here, but keeps this a free function testable in
// isolation without a Logger instance).
std::vector<EditorApplication::ScriptedAction> ParseEditorScript(const std::string& spec,
                                                                  Vixen::Log::Logger* logger) {
    std::vector<EditorApplication::ScriptedAction> actions;
    std::stringstream ss(spec);
    std::string token;
    while (std::getline(ss, token, ',')) {
        if (token.empty()) continue;
        const size_t at = token.find('@');
        if (at == std::string::npos) {
            if (logger) logger->Warning("[EditorApplication] VIXEN_EDITOR_SCRIPT: skipping token missing '@frame': " + token);
            continue;
        }
        const std::string actionPart = token.substr(0, at);
        const std::string framePart  = token.substr(at + 1);
        if (framePart.empty() || framePart.find_first_not_of("0123456789") != std::string::npos) {
            if (logger) logger->Warning("[EditorApplication] VIXEN_EDITOR_SCRIPT: skipping token with non-numeric frame: " + token);
            continue;
        }
        const long frame = std::strtol(framePart.c_str(), nullptr, 10);

        const size_t colon = actionPart.find(':');
        const std::string actionName = (colon == std::string::npos) ? actionPart : actionPart.substr(0, colon);

        EditorApplication::ScriptedAction action;
        action.frame = frame;
        if (actionName == "toggle") {
            if (colon == std::string::npos) {
                if (logger) logger->Warning("[EditorApplication] VIXEN_EDITOR_SCRIPT: 'toggle' missing ':<layerIndex>': " + token);
                continue;
            }
            const std::string arg = actionPart.substr(colon + 1);
            if (arg.empty() || arg.find_first_not_of("0123456789") != std::string::npos) {
                if (logger) logger->Warning("[EditorApplication] VIXEN_EDITOR_SCRIPT: 'toggle' has non-numeric layer index: " + token);
                continue;
            }
            action.kind = EditorApplication::ScriptedAction::Kind::Toggle;
            action.layerIndex = static_cast<uint32_t>(std::strtoul(arg.c_str(), nullptr, 10));
        } else if (actionName == "undo") {
            action.kind = EditorApplication::ScriptedAction::Kind::Undo;
        } else if (actionName == "redo") {
            action.kind = EditorApplication::ScriptedAction::Kind::Redo;
        } else if (actionName == "settings") {
            action.kind = EditorApplication::ScriptedAction::Kind::Settings;
        } else if (actionName == "back") {
            action.kind = EditorApplication::ScriptedAction::Kind::Back;
        } else {
            if (logger) logger->Warning("[EditorApplication] VIXEN_EDITOR_SCRIPT: unknown action, skipping: " + token);
            continue;
        }
        actions.push_back(action);
    }
    return actions;
}

// Parses "0,45,75,105" into frame numbers. Malformed entries are skipped with a warning (same
// never-abort contract as ParseEditorScript).
std::vector<long> ParseCaptureFrames(const std::string& spec, Vixen::Log::Logger* logger) {
    std::vector<long> frames;
    std::stringstream ss(spec);
    std::string token;
    while (std::getline(ss, token, ',')) {
        if (token.empty()) continue;
        if (token.find_first_not_of("0123456789") != std::string::npos) {
            if (logger) logger->Warning("[EditorApplication] VIXEN_EDITOR_CAPTURE_FRAMES: skipping non-numeric entry: " + token);
            continue;
        }
        frames.push_back(std::strtol(token.c_str(), nullptr, 10));
    }
    return frames;
}
}  // namespace

namespace {
// Reads a named param out of a resolved BoundAction's {name,value} vector (BindingStore.h:21).
// Returns 0 if absent or non-numeric -- ToggleLayer's only param is "layerIndex", extracted
// as a string by BindingStore's pattern match (see AppFlowElementTrigger's paramName), so the
// handler is the one place that parses it back to a uint32_t.
uint32_t ParseParam(const Vixen::AppFlow::AppFlowRuntime::Params& params, const char* name) {
    for (const auto& [n, v] : params) {
        if (n == name && !v.empty() && v.find_first_not_of("0123456789") == std::string::npos) {
            return static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 10));
        }
    }
    return 0;
}
}  // namespace

EditorApplication::EditorApplication(std::string documentPath)
    : documentPath_(std::move(documentPath)) {
    // R6a fix-loop finding: logger_ was constructed enabled=true but terminalOutput_ defaults
    // false (Logger.h) and nothing ever called SetTerminalOutput on it (only main.cpp's
    // SEPARATE mainLogger got that call) -- every [EditorApplication] Info/Error line, including
    // the pre-existing "Saved document to ..." one, was silently buffered into logEntries and
    // never reached stdout/the redirected run_editor_script.log. Mirrors main.cpp:56.
    logger_->SetTerminalOutput(true);
}

bool EditorApplication::LoadDocument(const std::string& path) {
    if (!doc_.Load(path, lastEditorError_)) {
        return false;
    }
    documentPath_ = path;
    rt_.Load();  // load the AppFlow reference vocab (state/action tables)
    rt_.Layers().SetLayerCount(doc_.LayerCount());  // (re)sync the mask to the freshly loaded doc

    // Inc-4 reframe (design §4.3, R5b): register the 5 self-contained handlers exactly once.
    // Each handler decides for itself which primitive it needs (Stack() for undoable actions,
    // NavPop() for navigation, a bare side effect for Save) -- the framework/registry knows
    // none of this. Guarded so a document reload doesn't double-register.
    if (!handlersRegistered_) {
        handlersRegistered_ = true;
        using Vixen::AppFlow::Generated::FlowActionId;
        rt_.RegisterHandler(FlowActionId::ToggleLayer, [this](const Vixen::AppFlow::AppFlowRuntime::Params& p) {
            const uint32_t idx = ParseParam(p, "layerIndex");
            rt_.Stack().Dispatch(FlowActionId::ToggleLayer, [this, idx](bool /*forward*/) {
                // THE TRANSPLANTED BODY, LIVE (R5c): applyToggle is kernel-generated C++ from
                // the same C# body the design's D12 walking skeleton proves transplants
                // identically. Self-inverse (mask ^ (1<<idx)) -- byte-identical to the old
                // LayerController::Toggle(idx) for any idx within the valid layer range.
                rt_.Layers().SetMask(Vixen::AppFlow::Generated::applyToggle(rt_.Layers().Mask(), idx));
                dirty_ = true;
            });
        });
        rt_.RegisterHandler(FlowActionId::Undo, [this](const Vixen::AppFlow::AppFlowRuntime::Params&) {
            rt_.Stack().Undo();
        });
        rt_.RegisterHandler(FlowActionId::Redo, [this](const Vixen::AppFlow::AppFlowRuntime::Params&) {
            rt_.Stack().Redo();
        });
        rt_.RegisterHandler(FlowActionId::Save, [this](const Vixen::AppFlow::AppFlowRuntime::Params&) {
            if (!SaveDocument()) {
                logger_->Error("[EditorApplication] SaveDocument failed: " + lastEditorError_);
            }
        });
        rt_.RegisterHandler(FlowActionId::Return, [this](const Vixen::AppFlow::AppFlowRuntime::Params&) {
            rt_.NavPop();
        });
    }
    return true;
}

void EditorApplication::BuildRenderGraph() {
    // Build the full standard graph unmodified (window, body-octree scene, UI composite HUD),
    // then re-point the UI node at the editor's own document and replace the 3 default demo
    // bodies with the loaded VoxelDocument's single flattened recipe.
    //
    // Inc-2b Task 3 (capture-target decision): NO capture-specific node is added here. The
    // standard graph built above already contains a "compute_render_target" RenderTargetNode
    // (application/main/source/graph/BuildRenderGraph.cpp's M4 render-scale-decoupling target) --
    // the offscreen target the compute voxel-raymarch dispatch writes the scene into every frame,
    // BEFORE the UI composite blits it up to the swapchain. It is created with
    // VK_IMAGE_USAGE_TRANSFER_SRC_BIT (for that same blit) and follows the swapchain extent 1:1
    // by default, so it reliably holds the full rendered body (a toggle/undo/redo is visible
    // there exactly as it is on screen) with zero new wiring and zero overhead when
    // VIXEN_EDITOR_CAPTURE_FRAMES is unset (the node exists either way -- capture only adds a
    // read-back, never a build-time cost). See CaptureFrameToPng (below) and EditorApplication.h.
    VulkanGraphApplication::BuildRenderGraph();

    if (auto* ui = GetUiRenderNode()) {
        // Matches the "assets/ui/<doc>.rml" convention BuildRenderGraph.cpp uses for hud.rml —
        // ResolveUiAsset (UIRenderNode.cpp) strips the "assets/" prefix when resolving against
        // VIXEN_UI_SOURCE_DIR/VIXEN_UI_ASSET_SOURCE_DIR, and checks the literal path relative to
        // CWD otherwise; a bare "editor.rml" (no prefix) fails both and RmlUi logs "Unable to
        // open file editor.rml" (found via the windowed smoke test).
        ui->SetParameter(Vixen::RenderGraph::UIRenderNodeConfig::RML_DOCUMENT_PATH,
                          std::string("assets/ui/editor.rml"));
    }

    // Frame the loaded document by default: the interactive graph's CameraNode otherwise keeps
    // its main-app orbit defaults (center=(5,5,5), distance=30 — tuned for the Cornell-box demo
    // scene), which has no relationship to where the editor's object-centered geometry actually
    // bakes. Ray-march world-position formula: p_world = p_base*renderScale + worldPos (see
    // BodyInstanceRayMarch.comp); ApplyDocumentToScene uses worldPos=(0,0,0), renderScale=5.0,
    // and RecipeBakeConfig's default center=(32,32,32) at resolution n=64, so grid-to-world =
    // (kWorldGridSize/n)*renderScale = (10/64)*5 = 0.78125 and the baked center sits at world
    // (25,25,25) — same target test_editor_document_render.cpp uses.
    //
    // Distance: the golden document's base layer is Box(1,1,1) (halfExtents=1 in grid/local
    // space -- see that test's file header), so in world space (x*renderScale) its half-extent
    // is 5 units, half-diagonal ~8.66. At the raymarch camera's 45-deg FOV, a distance camera
    // ends up INSIDE the box below ~21 units (tan(22.5deg)*dist >= half-diagonal). The test's own
    // eye offset (|(1.6,1.3,1.6)| ~= 2.6) only worked there because RenderPool's harness frames a
    // single octree slot directly, bypassing this scale entirely -- copying it into the
    // interactive app put the camera inside the box (verified: a flat, edgeless green fill with
    // no visible faces, flickering against black -- the near/far-plane and self-intersection
    // symptom of a camera embedded in solid geometry). 30 world units (matching the main app's
    // original default, which was tuned for objects at this same ~10-unit scale) comfortably
    // clears the half-diagonal with margin.
    if (auto* cameraInst = GetRenderGraph() ? GetRenderGraph()->GetInstanceByName("raymarch_camera") : nullptr) {
        using CC = Vixen::RenderGraph::CameraNodeConfig;
        constexpr float kGridToWorld = (10.0f / 64.0f) * 5.0f;  // (kWorldGridSize/n) * renderScale
        constexpr float kBakeCenterGrid = 32.0f;
        cameraInst->SetParameter(CC::PARAM_ORBIT_CENTER_X, kBakeCenterGrid * kGridToWorld);
        cameraInst->SetParameter(CC::PARAM_ORBIT_CENTER_Y, kBakeCenterGrid * kGridToWorld);
        cameraInst->SetParameter(CC::PARAM_ORBIT_CENTER_Z, kBakeCenterGrid * kGridToWorld);
        cameraInst->SetParameter(CC::PARAM_ORBIT_DISTANCE, 30.0f);
    }

    if (!ApplyDocumentToScene()) {
        logger_->Error("[EditorApplication] BuildRenderGraph: ApplyDocumentToScene failed: " +
                       lastEditorError_);
    }
}

bool EditorApplication::ApplyDocumentToScene() {
    Vixen::SVO::RecipeRegistry::RecipeEntry entry;
    if (!doc_.FlattenToRecipeEntry(rt_.Layers().Mask(), entry, lastEditorError_)) {
        return false;
    }

    Vixen::SVO::RecipeRegistry reg;
    static constexpr uint32_t kRecipeId = 1u;
    const auto result = reg.Register(kRecipeId, entry);
    if (result != Vixen::SVO::RecipeRegistry::RegisterResult::Ok) {
        lastEditorError_ = "RecipeRegistry::Register failed (code " +
                            std::to_string(static_cast<int>(result)) + ")";
        return false;
    }

    Vixen::SVO::RecipeBakeConfig bakeCfg{};  // defaults: n=64, band=2.5, depth=3
    auto bakeResult = Vixen::SVO::BakeRegistryToPool(reg, bakeCfg);
    if (!bakeResult.ok) {
        lastEditorError_ = "BakeRegistryToPool failed: " + bakeResult.err;
        return false;
    }

    SetRecipePool(std::move(bakeResult.pool));

    // Single body instance selecting the pool's only slot (octreeIndex=0, providerKind
    // defaults to 0/Stored) — mirrors test_recipe_pool_render.cpp's per-slot instance pattern.
    //
    // renderScale=5.0: the golden-style document authoring convention is object-centered —
    // geometry is authored near local origin with a small (~2-unit) extent (see
    // VoxelDocumentFlattener.h / the M3 flatten test's [-2,2]^3 parity sweep) — unlike other
    // render-gate recipes, which are authored with large positive-octant coordinates.
    // BakeRecipeInstructionsToSdfWorld now applies `center` (Inc2a fix: `p - center` at eval),
    // so this object-centered geometry bakes AT RecipeBakeConfig::center's default grid
    // position (32,32,32), not raw grid origin. The shader's base-octree world frame spans a
    // fixed [0,10] world units (BodyInstanceRayMarch.comp / ShellOctreeGpu.h's kWorldGridSize=10),
    // so a grid-space extent of ~2 voxels maps to only ~0.3 world units before renderScale — too
    // small to frame usefully. renderScale=5 brings that up to a comfortable ~1.5 world units.
    Vixen::SVO::BodyInstanceGpu inst{};
    inst.worldPos[0] = 0.0f; inst.worldPos[1] = 0.0f; inst.worldPos[2] = 0.0f;
    inst.renderScale = 5.0f;
    inst.color[0] = 1.0f; inst.color[1] = 1.0f; inst.color[2] = 1.0f;
    inst.octreeIndex = 0u;
    SetBodyInstances({inst});

    // Editor Brick-Residency Fix: the document body is the ONE object being directly edited and
    // is always in view — grant brick residency unconditionally rather than relying on the main
    // app's camera-motion/frustum heuristic (SkipResidencyHeuristic() above opts this body out of
    // that heuristic entirely, so this is the sole residency driver). RequestBrickResidency only
    // stashes a dirty-flag (BodyOctreeSceneNode::RequestBrickResidency) — safe to call here even
    // though SetRecipePool just above already marked the node's recipe dirty for re-materialize;
    // ExecuteImpl re-lands both on the next Execute. Called every ApplyDocumentToScene, i.e. after
    // every edit (see Update()'s dirty_ tail), so residency re-lands after each Rematerialize too.
    RequestBodyBrickResidency(true);

    return true;
}

bool EditorApplication::CaptureFrameToPng(const std::string& path, std::string& err) {
    // Live lookups every call -- never cache a node pointer (mirrors GetWindowHandle's rule;
    // both the render target and the device node persist across recompile, but re-resolving by
    // name is the established pattern for host-facing lookups in this app).
    auto* graph = GetRenderGraph();
    if (!graph) {
        err = "CaptureFrameToPng: no render graph";
        return false;
    }

    // See EditorApplication.h's captureFrames_/updateTick_ comment for why this reuses the
    // standard graph's existing "compute_render_target" instance instead of adding a new one.
    static constexpr const char* kCaptureTargetName = "compute_render_target";
    auto* targetInst = graph->GetInstanceByName(kCaptureTargetName);
    if (!targetInst) {
        err = std::string("CaptureFrameToPng: instance '") + kCaptureTargetName + "' not found";
        return false;
    }
    // RENDER_TARGET is RenderTargetNodeConfig output slot 0 (IRenderTarget*); read directly off
    // the node's bundle rather than pulling in the typed config here for one slot index.
    Resource* targetOutput = targetInst->GetOutput(0, 0);
    if (!targetOutput) {
        err = "CaptureFrameToPng: capture target has no RENDER_TARGET output yet (graph not compiled?)";
        return false;
    }
    auto* renderTarget = targetOutput->GetHandle<Vixen::Vulkan::Resources::IRenderTarget*>();
    if (!renderTarget) {
        err = "CaptureFrameToPng: RENDER_TARGET output handle is null";
        return false;
    }

    auto* deviceInst = static_cast<DeviceNode*>(graph->GetInstanceByName("main_device"));
    if (!deviceInst || !deviceInst->GetVulkanDevice()) {
        err = "CaptureFrameToPng: 'main_device' not found or has no VulkanDevice";
        return false;
    }
    auto* device = deviceInst->GetVulkanDevice();

    return Vixen::RenderGraph::Debug::CaptureRenderTargetToPng(
        device, renderTarget, device->queue, device->graphicsQueueIndex, path, err);
}

bool EditorApplication::SaveDocument() {
    const size_t dot = documentPath_.find_last_of('.');
    const std::string base = (dot == std::string::npos) ? documentPath_ : documentPath_.substr(0, dot);
    const std::string outPath = base + ".edited.vxd";

    if (!doc_.Save(rt_.Layers().Mask(), outPath, lastEditorError_)) {
        return false;
    }
    lastSavedPath_ = outPath;
    logger_->Info("[EditorApplication] Saved document to " + outPath);
    return true;
}

void EditorApplication::PreTick() {
    // graph.Run() consolidation: the scripted-action injector runs in PreTick() (before Update())
    // instead of at the top of Update(). Behavior-identical: PreTick() is called by
    // VulkanApplicationBase::Tick() immediately before Update(), and updateTick_ only advances at
    // the END of Update() -- so the injector sees the same updateTick_ it saw when it lived inside
    // Update(), and the dirty_ it sets is re-flattened by Update()'s existing dirty tail the same
    // tick. Own try/catch (mirrors Update()'s) so a malformed script never throws across the tick.
    try {
    // Inc-2b Task 4: parse VIXEN_EDITOR_SCRIPT / VIXEN_EDITOR_CAPTURE_FRAMES /
    // VIXEN_EDITOR_CAPTURE_DIR exactly once (mirrors VulkanGraphApplication.cpp's
    // VIXEN_RESIZE_AT_FRAME static-init-on-first-use pattern, adapted to a per-instance flag
    // since these are member vectors, not process-wide statics). Unset envs parse to empty
    // vectors, so every check below is a no-op -- zero behaviour change for the interactive editor.
    if (!scriptParsed_) {
        scriptParsed_ = true;
        if (const char* scriptEnv = std::getenv("VIXEN_EDITOR_SCRIPT")) {
            scriptedActions_ = ParseEditorScript(scriptEnv, logger_.get());
        }
        if (const char* captureEnv = std::getenv("VIXEN_EDITOR_CAPTURE_FRAMES")) {
            captureFrames_ = ParseCaptureFrames(captureEnv, logger_.get());
        }
        if (const char* dirEnv = std::getenv("VIXEN_EDITOR_CAPTURE_DIR")) {
            captureDir_ = dirEnv;
        }
    }

    // Inject any scripted action due this tick through the SAME dispatch path the interactive
    // input drives (design §4.3: the editor is a pure consumer, it names zero actions in code
    // beyond a selector/key) -- exercising the real click-equivalent/key-equivalent ->
    // registry -> handler -> ActionStack -> re-flatten dispatch, not a shortcut.
    for (const auto& action : scriptedActions_) {
        if (action.frame != updateTick_) continue;
        switch (action.kind) {
            // The three edit kinds emit one canonical, parseable "[EDITOR/state] <op> ..." line
            // each (mask + undo/redo depths + dispatch result). This state-dump is the R6 gate's
            // real proof that undo/redo work: it asserts the mask trail (7->3->7->3) and depth
            // movement come out of the RUNNING editor through the registry-dispatch path. It is NOT
            // debug noise -- it is the observable contract the windowed gate parses (a windowed
            // PIXEL round-trip is unreachable here: the editor body renders the mask-INVARIANT
            // mip-fallback path at an orbit camera, so the layer mask has ~0 visible effect --
            // proven via GPU-memory checksums + a shader-output tap; see the R6 finding note in
            // Vixen-Docs/01-Architecture and test_editor_toggle_undo_capture.cpp's header). Keep the
            // key=value shape stable: ReadEditStates() in that gate parses it positionally by key.
            case ScriptedAction::Kind::Toggle: {
                const auto r = rt_.DispatchBySelector("layer-" + std::to_string(action.layerIndex) + "-toggle");
                logger_->Info("[EDITOR/state] toggle mask=" + std::to_string(rt_.Layers().Mask()) +
                               " undoDepth=" + std::to_string(rt_.Stack().UndoDepth()) +
                               " redoDepth=" + std::to_string(rt_.Stack().RedoDepth()) +
                               " result=" + std::to_string(static_cast<int>(r)));
                break;
            }
            case ScriptedAction::Kind::Undo: {
                const auto r = rt_.DispatchByKey({Vixen::AppFlow::Generated::KeyId::Z, Vixen::AppFlow::Generated::KeyMod::Ctrl});
                logger_->Info("[EDITOR/state] undo mask=" + std::to_string(rt_.Layers().Mask()) +
                               " undoDepth=" + std::to_string(rt_.Stack().UndoDepth()) +
                               " redoDepth=" + std::to_string(rt_.Stack().RedoDepth()) +
                               " result=" + std::to_string(static_cast<int>(r)));
                break;
            }
            case ScriptedAction::Kind::Redo: {
                const auto r = rt_.DispatchByKey({Vixen::AppFlow::Generated::KeyId::Y, Vixen::AppFlow::Generated::KeyMod::Ctrl});
                logger_->Info("[EDITOR/state] redo mask=" + std::to_string(rt_.Layers().Mask()) +
                               " undoDepth=" + std::to_string(rt_.Stack().UndoDepth()) +
                               " redoDepth=" + std::to_string(rt_.Stack().RedoDepth()) +
                               " result=" + std::to_string(static_cast<int>(r)));
                break;
            }
            case ScriptedAction::Kind::Settings:
                // NavTo is a plain service call (AppFlowRuntime.h:46), not a dispatchable verb --
                // there's no selector/key for "open settings" yet (design D15's registry only
                // covers actions, not raw navigation), so this is the one script action that
                // calls a runtime service directly rather than Dispatch*. Sets up the state the
                // very next Back action needs (Settings is the only state back-button/Esc resolve
                // Return from -- kReturnEdges, AppFlow.g.h:82).
                rt_.NavTo(Vixen::AppFlow::Generated::FlowStateId::Settings);
                break;
            case ScriptedAction::Kind::Back: {
                // The real dispatch path a back-button UI click takes: DispatchBySelector, exactly
                // like Update()'s click-drain call site -- not a shortcut to NavPop(). Emits a
                // parseable state-dump line so run_editor_script.bat's log can be asserted against
                // by the windowed gtest (a windowed capture can't cheaply show a state pop; this is
                // the state-dump-hook option the plan calls out, R6 Step 3).
                rt_.DispatchBySelector("back-button");
                logger_->Info("[EDITOR/state] afterBack=" +
                               std::to_string(static_cast<int>(rt_.Current())));
                break;
            }
        }
    }
    } catch (const std::exception& e) {
        lastEditorError_ = std::string("PreTick: ") + e.what();
        logger_->Error("[EditorApplication] PreTick exception: " + lastEditorError_);
    } catch (...) {
        lastEditorError_ = "PreTick: unknown exception";
        logger_->Error("[EditorApplication] PreTick unknown exception");
    }
}

void EditorApplication::Update() {
    VulkanGraphApplication::Update();

    // Inc-2b M3 (carried over from the M2 validator): the base VulkanGraphApplication::Update's
    // try/catch (VulkanGraphApplication.cpp) is scoped to that method's OWN body -- it returns
    // before control reaches here, so nothing below is actually covered by it. A prior version of
    // this comment claimed otherwise; wrap this override's own body in its own guard (mirroring
    // the base method's catch shape) so the no-throw-across-the-tick contract (design §5) really
    // holds for the toggle/undo/capture/script code added in Inc-2b, not just by assertion.
    try {
    // Drain UI clicks (S4 pattern) and dispatch by selector -- carries no behavior itself; the
    // registered ToggleLayer/Return handlers decide what the click means (design §4.3). A
    // selector with no binding (a non-editor UI hit) resolves to RejectedByState and is ignored.
    if (auto* selection = GetUiSelectionProviderNode()) {
        const std::string clickedId = selection->DrainClickedElementId();
        if (!clickedId.empty()) {
            rt_.DispatchBySelector(clickedId);
        }
    }

    // Keybindings: dispatch by chord, edge-detected (press-only, not held-repeat) via KeyMap.h's
    // GLFW-keycode -> KeyId map. Carries no behavior itself -- DispatchByKey resolves the chord
    // through the InputProfile to whichever action is bound (Save/Undo/Redo/Return), then routes
    // it through the same registered-handler path a UI click uses. An unbound chord (or Esc
    // outside Settings) resolves to RejectedByState and is ignored, never a crash.
    if (GLFWwindow* window = GetWindowHandle()) {
        using Vixen::Editor::GlfwToKeyId;
        using Vixen::Editor::ReadMods;
        using Vixen::AppFlow::Generated::KeyId;
        using Vixen::AppFlow::Generated::KeyMod;

        const bool ctrl = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS
                       || glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
        const KeyMod ctrlMods = ReadMods(ctrl, /*shift*/false, /*alt*/false, /*super*/false);

        // Save is bound plain (kKeyDefaults: {S, KeyMod::None}) -- dispatch with a fixed
        // KeyMod::None chord (not the live ctrl state) so Ctrl+S still resolves (matches the
        // pre-Inc-4 behavior, which fired Save on 'S' regardless of ctrl).
        const bool sKeyDown = glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS;
        if (sKeyDown && !sKeyWasDown_) rt_.DispatchByKey({GlfwToKeyId(GLFW_KEY_S), KeyMod::None});
        sKeyWasDown_ = sKeyDown;

        const bool zDown = ctrl && glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS;
        if (zDown && !ctrlZWasDown_) rt_.DispatchByKey({GlfwToKeyId(GLFW_KEY_Z), ctrlMods});
        ctrlZWasDown_ = zDown;

        const bool yDown = ctrl && glfwGetKey(window, GLFW_KEY_Y) == GLFW_PRESS;
        if (yDown && !ctrlYWasDown_) rt_.DispatchByKey({GlfwToKeyId(GLFW_KEY_Y), ctrlMods});
        ctrlYWasDown_ = yDown;

        // Escape: NEW this increment (design §4.3 -- a back-button selector reaches Return
        // identically to Esc). Resolves to Return via R2's seeded return-edge in Settings;
        // outside Settings it resolves to nothing (RejectedByState, ignored).
        const bool escDown = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
        if (escDown && !escWasDown_) rt_.DispatchByKey({KeyId::Escape, KeyMod::None});
        escWasDown_ = escDown;
    }

    // Re-flatten + re-upload on the next tick after a toggle (dirty-flag pattern — no
    // MarkNeedsRecompile; SetRecipePool inside ApplyDocumentToScene already sets the
    // BodyOctreeSceneNode's own recipeDirty_ flag for the in-Execute re-materialize).
    // Inc-2: dirty_ is now owned here (the model no longer tracks edit state).
    if (dirty_) {
        dirty_ = false;
        logger_->Debug("[EDITOR/diag] re-flatten tick=" + std::to_string(updateTick_) +
                       " mask=" + std::to_string(rt_.Layers().Mask()));
        if (!ApplyDocumentToScene()) {
            logger_->Error("[EditorApplication] toggle re-apply failed: " + lastEditorError_);
        }
    }

    // Inc-2b Task 4: dump a capture PNG if this tick is scripted for one. Placed AFTER the
    // dirty_ re-flatten tail so a capture on the same tick as a scripted toggle reflects the
    // post-toggle scene. A capture failure is logged, never thrown -- CaptureFrameToPng already
    // never crashes the frame loop, and this call site preserves that; the try/catch around this
    // whole override body is the actual backstop (see the M3 comment above), not a claim about
    // the base method's guard.
    for (const long captureFrame : captureFrames_) {
        if (captureFrame != updateTick_) continue;
        const std::string path = captureDir_ + "/editor_capture_" + std::to_string(updateTick_) + ".png";
        // Canonical parseable line: correlates each capture PNG to the mask it was taken at, so the
        // R6 gate's residency smoke-check knows which frame is pre- vs post-first-edit.
        logger_->Info("[EDITOR/state] capture tick=" + std::to_string(updateTick_) +
                       " mask=" + std::to_string(rt_.Layers().Mask()));
        std::string captureErr;
        if (!CaptureFrameToPng(path, captureErr)) {
            logger_->Error("[EditorApplication] CaptureFrameToPng failed for " + path + ": " + captureErr);
        }
    }

    // Advanced AFTER this tick's script/capture checks above compare against it, so updateTick_
    // is 0 on the very first Update() call rather than 1 (the pre-existing ++ prefix here made a
    // scripted "@0"/capture-frame-0 entry permanently un-hittable -- found live via the M3
    // windowed gate: editor_capture_0.png never appeared even though captureFrames_ contained 0).
    // Note frame 0 is still not a useful CAPTURE frame regardless of this fix -- Update() ticks
    // BEFORE the render loop's first Render() call (VulkanApplicationBase::Tick(): PreTick() ->
    // Update() -> Render() -> PostTick(), per iteration), so a tick-0 capture still reads
    // compute_render_target before anything has ever been drawn into it (an all-black PNG).
    // Scripted ACTIONS (toggle/undo/redo) at frame 0 are unaffected by that -- they mutate the
    // mask/ActionStack regardless of what's on screen yet.
    ++updateTick_;
    } catch (const std::exception& e) {
        lastEditorError_ = std::string("Update failed: ") + e.what();
        logger_->Error("[EditorApplication] Update: " + lastEditorError_);
    } catch (...) {
        lastEditorError_ = "Update failed: unknown (non-std) exception";
        logger_->Error("[EditorApplication] Update: " + lastEditorError_);
    }
}
