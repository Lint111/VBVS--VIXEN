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
#include "Data/Nodes/UIRenderNodeConfig.h"
#include "Nodes/CameraNode.h"
#include "Data/Nodes/CameraNodeConfig.h"
#include "Core/RenderGraph.h"
#include <Logger.h>

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
}
