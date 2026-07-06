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
#include <Logger.h>

#include <cstdlib>
#include <sstream>

#define GLFW_INCLUDE_NONE   // don't pull in <GL/gl.h> (absent on headless/WSL builds)
#include <GLFW/glfw3.h>

namespace {
// Parses "layer-<N>-toggle" -> N. Returns -1 on any other id (including non-editor UI hits).
int ParseLayerToggleId(const std::string& id) {
    static constexpr char kPrefix[] = "layer-";
    static constexpr char kSuffix[] = "-toggle";
    const size_t prefixLen = sizeof(kPrefix) - 1;
    const size_t suffixLen = sizeof(kSuffix) - 1;
    if (id.size() <= prefixLen + suffixLen) return -1;
    if (id.compare(0, prefixLen, kPrefix) != 0) return -1;
    if (id.compare(id.size() - suffixLen, suffixLen, kSuffix) != 0) return -1;
    const std::string digits = id.substr(prefixLen, id.size() - prefixLen - suffixLen);
    if (digits.empty()) return -1;
    for (char c : digits) if (c < '0' || c > '9') return -1;
    return std::stoi(digits);
}

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

EditorApplication::EditorApplication(std::string documentPath)
    : documentPath_(std::move(documentPath)) {}

bool EditorApplication::LoadDocument(const std::string& path) {
    if (!doc_.Load(path, lastEditorError_)) {
        return false;
    }
    documentPath_ = path;
    rt_.Load();  // load the AppFlow reference vocab (state/action tables)
    rt_.Layers().SetLayerCount(doc_.LayerCount());  // (re)sync the mask to the freshly loaded doc
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

void EditorApplication::ToggleLayer(uint32_t layerIndex) {
    // Route through the ActionStack so the toggle is undoable; onChanged fires on BOTH the
    // forward apply AND on rt_.Undo()'s inverse, so a later Ctrl+Z re-flattens too.
    rt_.ToggleLayer(layerIndex, [this]{ dirty_ = true; });
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

void EditorApplication::Update() {
    VulkanGraphApplication::Update();

    // Inc-2b M3 (carried over from the M2 validator): the base VulkanGraphApplication::Update's
    // try/catch (VulkanGraphApplication.cpp) is scoped to that method's OWN body -- it returns
    // before control reaches here, so nothing below is actually covered by it. A prior version of
    // this comment claimed otherwise; wrap this override's own body in its own guard (mirroring
    // the base method's catch shape) so the no-throw-across-the-tick contract (design §5) really
    // holds for the toggle/undo/capture/script code added in Inc-2b, not just by assertion.
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

    // Inject any scripted action due this tick through the SAME methods the interactive input
    // path calls (ToggleLayer / rt_.Undo / rt_.Redo) -- so the harness exercises the real
    // click-equivalent -> ActionStack -> re-flatten -> undo dispatch, not a shortcut. Placed
    // BEFORE the dirty_ re-flatten tail below so the re-flatten happens the same tick.
    for (const auto& action : scriptedActions_) {
        if (action.frame != updateTick_) continue;
        switch (action.kind) {
            case ScriptedAction::Kind::Toggle: ToggleLayer(action.layerIndex); break;
            case ScriptedAction::Kind::Undo:   rt_.Undo(); break;
            case ScriptedAction::Kind::Redo:   rt_.Redo(); break;
        }
    }

    // Drain UI clicks (S4 pattern) and toggle the matching layer's enabled override.
    if (auto* selection = GetUiSelectionProviderNode()) {
        const std::string clickedId = selection->DrainClickedElementId();
        if (!clickedId.empty()) {
            const int layerIndex = ParseLayerToggleId(clickedId);
            if (layerIndex >= 0) {
                ToggleLayer(static_cast<uint32_t>(layerIndex));
            }
        }
    }

    // Save keybinding: 'S', edge-detected (press-only, not held-repeat).
    if (GLFWwindow* window = GetWindowHandle()) {
        const bool sKeyDown = glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS;
        if (sKeyDown && !sKeyWasDown_) {
            if (!SaveDocument()) {
                logger_->Error("[EditorApplication] SaveDocument failed: " + lastEditorError_);
            }
        }
        sKeyWasDown_ = sKeyDown;

        // Undo/redo keybindings: Ctrl+Z / Ctrl+Y, edge-detected (press-only). rt_.Undo()/Redo()
        // re-run the stored apply lambda (set inside ToggleLayer), which sets dirty_ — the
        // dirty_ re-flatten tail below reuses the same path a toggle uses; no new re-flatten call.
        const bool ctrl = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS
                       || glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
        const bool zDown = ctrl && glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS;
        const bool yDown = ctrl && glfwGetKey(window, GLFW_KEY_Y) == GLFW_PRESS;
        if (zDown && !ctrlZWasDown_) rt_.Undo();
        if (yDown && !ctrlYWasDown_) rt_.Redo();
        ctrlZWasDown_ = zDown;
        ctrlYWasDown_ = yDown;
    }

    // Re-flatten + re-upload on the next tick after a toggle (dirty-flag pattern — no
    // MarkNeedsRecompile; SetRecipePool inside ApplyDocumentToScene already sets the
    // BodyOctreeSceneNode's own recipeDirty_ flag for the in-Execute re-materialize).
    // Inc-2: dirty_ is now owned here (the model no longer tracks edit state).
    if (dirty_) {
        dirty_ = false;
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
    // BEFORE the render loop's first Render() call (main.cpp: `Update(); Render();` per
    // iteration), so a tick-0 capture still reads compute_render_target before anything has ever
    // been drawn into it (an all-black PNG). Scripted ACTIONS (toggle/undo/redo) at frame 0 are
    // unaffected by that -- they mutate the mask/ActionStack regardless of what's on screen yet.
    ++updateTick_;
    } catch (const std::exception& e) {
        lastEditorError_ = std::string("Update failed: ") + e.what();
        logger_->Error("[EditorApplication] Update: " + lastEditorError_);
    } catch (...) {
        lastEditorError_ = "Update failed: unknown (non-std) exception";
        logger_->Error("[EditorApplication] Update: " + lastEditorError_);
    }
}
