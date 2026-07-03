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
    return true;
}

void EditorApplication::BuildRenderGraph() {
    // Build the full standard graph unmodified (window, body-octree scene, UI composite HUD),
    // then re-point the UI node at the editor's own document and replace the 3 default demo
    // bodies with the loaded VoxelDocument's single flattened recipe.
    VulkanGraphApplication::BuildRenderGraph();

    if (auto* ui = GetUiRenderNode()) {
        ui->SetParameter(Vixen::RenderGraph::UIRenderNodeConfig::RML_DOCUMENT_PATH,
                          std::string("editor.rml"));
    }

    if (!ApplyDocumentToScene()) {
        logger_->Error("[EditorApplication] BuildRenderGraph: ApplyDocumentToScene failed: " +
                       lastEditorError_);
    }
}

bool EditorApplication::ApplyDocumentToScene() {
    Vixen::SVO::RecipeRegistry::RecipeEntry entry;
    if (!doc_.FlattenToRecipeEntry(entry, lastEditorError_)) {
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
    // renderScale=5.0: the golden-style document authoring convention centres geometry at
    // grid-space origin with a small (~2-unit) extent (see VoxelDocumentFlattener.h / the M3
    // flatten test's [-2,2]^3 parity sweep) — unlike other render-gate recipes, which are
    // authored with large positive-octant coordinates. BakeRecipeInstructionsToSdfWorld samples
    // at raw grid indices [0,64), and the shader's base-octree world frame spans a fixed
    // [0,10] world units (BodyInstanceRayMarch.comp / ShellOctreeGpu.h's kWorldGridSize=10), so
    // a grid-space extent of ~2 voxels maps to only ~0.3 world units before renderScale — too
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
    if (layerIndex >= doc_.LayerCount()) return;
    doc_.ToggleLayer(layerIndex);
}

bool EditorApplication::SaveDocument() {
    const size_t dot = documentPath_.find_last_of('.');
    const std::string base = (dot == std::string::npos) ? documentPath_ : documentPath_.substr(0, dot);
    const std::string outPath = base + ".edited.vxd";

    if (!doc_.Save(outPath, lastEditorError_)) {
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
    }

    // Re-flatten + re-upload on the next tick after a toggle (dirty-flag pattern — no
    // MarkNeedsRecompile; SetRecipePool inside ApplyDocumentToScene already sets the
    // BodyOctreeSceneNode's own recipeDirty_ flag for the in-Execute re-materialize).
    if (doc_.ConsumeDirty()) {
        if (!ApplyDocumentToScene()) {
            logger_->Error("[EditorApplication] toggle re-apply failed: " + lastEditorError_);
        }
    }
}
