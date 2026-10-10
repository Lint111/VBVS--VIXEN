#include "EditorApplication.h"

// Include-order gotcha (see BuildRenderGraph.cpp's file header): BodyOctreeSceneNode.h /
// RecipeBaker.h transitively pull gaia.h (ShellOctree -> LaineKarrasOctree -> ISVOStructure),
// whose std::hash<> specialisations must be visible BEFORE RmlUi's bundled robin_hood.h wraps
// them, or robin_hood's Table<> instantiations fail with "std::hash<T> has no operator()".
#include "Recipe/RecipeRegistry.h"
#include "Recipe/RecipeBaker.h"
#include "Recipe/RecipeBounds.h"
#include "SdfRecipes.h"
#include "EditorDocumentFraming.h"
#include "ShellOctreeGpu.h"
#include "Nodes/UIRenderNode.h"               // AFTER the Recipe/gaia includes above
#include "Nodes/UISelectionProviderNode.h"
#include "Nodes/DeviceNode.h"                 // CaptureFrameToPng's live device lookup
#include "Nodes/BodyOctreeSceneNode.h"
#include "Nodes/CameraNode.h"
#include "Nodes/SwapChainNode.h"
#include "Data/Nodes/CameraNodeConfig.h"
#include "Core/RenderGraph.h"
#include "Debug/RenderTargetReadback.h"       // shared IRenderTarget -> PNG readback
#include "KeyMap.h"                           // Inc-4 R5a: GLFW keycode -> typed KeyId
#include "AppFlowBlobFile.h"                   // T1.2: external AppFlow watch/reload
#include "GaiaLayerViewDataProvider.h"        // Inc-B: view->model seam, Gaia-backed provider
#include "VixenHash.h"
#include <Logger.h>
#include <magic_enum.hpp>
#include <nlohmann/json.hpp>
#include <RmlUi/Core.h>
#include <stb_image_write.h>

#include <cstdlib>
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <sstream>
#include <string_view>
#include <thread>
#include <type_traits>

#define GLFW_INCLUDE_NONE   // don't pull in <GL/gl.h> (absent on headless/WSL builds)
#include <GLFW/glfw3.h>

#ifndef VIXEN_EDITOR_APPFLOW_PATH
#define VIXEN_EDITOR_APPFLOW_PATH "assets/AppFlow.appflow"
#endif

namespace {
// R463 capture instrumentation only; steady timestamps correlate with shared backend spans.
void TraceEditorLatency(const char* phase, const char* edge, long tick, int action = -1) {
    if (!std::getenv("VIXEN_EDITOR_LATENCY_TRACE")) return;
    const auto wallUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    std::fprintf(stderr, "[RECIPE/latency] phase=%s edge=%s tick=%ld action=%d wall_us=%lld\n",
        phase, edge, tick, action, static_cast<long long>(wallUs));
}

// Parses selector-equivalent editor operations such as
// "parameter_up:0@30,program_up:0@45,undo@60,redo@75,save@90,reopen@105".
// A malformed token is logged and skipped; it never aborts the editor.
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
        const bool needsIndex = actionName == "toggle" || actionName == "delete" ||
            actionName == "move_up" || actionName == "move_down" ||
            actionName == "program_up" || actionName == "program_down" ||
            actionName == "parameter_up" || actionName == "parameter_down";
        if (needsIndex) {
            if (colon == std::string::npos) {
                if (logger) logger->Warning("[EditorApplication] VIXEN_EDITOR_SCRIPT: action requires ':<index>': " + token);
                continue;
            }
            const std::string arg = actionPart.substr(colon + 1);
            if (arg.empty() || arg.find_first_not_of("0123456789") != std::string::npos) {
                if (logger) logger->Warning("[EditorApplication] VIXEN_EDITOR_SCRIPT: action index is not numeric: " + token);
                continue;
            }
            action.index = static_cast<uint32_t>(std::strtoul(arg.c_str(), nullptr, 10));
            if (actionName == "toggle") action.kind = EditorApplication::ScriptedAction::Kind::Toggle;
            else if (actionName == "delete") action.kind = EditorApplication::ScriptedAction::Kind::DeleteLayer;
            else if (actionName == "move_up") action.kind = EditorApplication::ScriptedAction::Kind::MoveLayerUp;
            else if (actionName == "move_down") action.kind = EditorApplication::ScriptedAction::Kind::MoveLayerDown;
            else if (actionName == "program_up") action.kind = EditorApplication::ScriptedAction::Kind::ProgramFieldUp;
            else if (actionName == "program_down") action.kind = EditorApplication::ScriptedAction::Kind::ProgramFieldDown;
            else if (actionName == "parameter_up") action.kind = EditorApplication::ScriptedAction::Kind::ParameterUp;
            else action.kind = EditorApplication::ScriptedAction::Kind::ParameterDown;
        } else if (colon != std::string::npos) {
            if (logger) logger->Warning("[EditorApplication] VIXEN_EDITOR_SCRIPT: action does not take an index: " + token);
            continue;
        } else if (actionName == "create") {
            action.kind = EditorApplication::ScriptedAction::Kind::CreateLayer;
        } else if (actionName == "undo") {
            action.kind = EditorApplication::ScriptedAction::Kind::Undo;
        } else if (actionName == "redo") {
            action.kind = EditorApplication::ScriptedAction::Kind::Redo;
        } else if (actionName == "save") {
            action.kind = EditorApplication::ScriptedAction::Kind::Save;
        } else if (actionName == "reopen") {
            action.kind = EditorApplication::ScriptedAction::Kind::Reopen;
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

constexpr uint32_t kEditorProceduralRecipeId = 0xE0D17001u;

std::array<float, 6> ParameterRangeMaxima(const Vixen::Editor::EditorDocumentModel& document) {
    std::array<float, 6> maxima{};
    for (uint32_t i = 0; i < document.ParameterCount(); ++i) {
        const uint32_t slot = document.ParameterSlot(i);
        if (slot < maxima.size()) maxima[slot] = document.ParameterMax(i);
    }
    return maxima;
}

template <typename Enum>
nlohmann::json EnumRecord(Enum value) {
    using Underlying = std::underlying_type_t<Enum>;
    return {{"id", static_cast<Underlying>(value)}, {"name", std::string(magic_enum::enum_name(value))}};
}

const char* ParamTypeName(Vixen::AppFlow::Generated::FlowParamType type) {
    using Vixen::AppFlow::Generated::FlowParamType;
    switch (type) {
        case FlowParamType::String: return "string";
        case FlowParamType::Int: return "integer";
        case FlowParamType::Float: return "number";
        case FlowParamType::EntityRef: return "entityRef";
    }
    return "unknown";
}

std::string RequestKey(const nlohmann::json& id) {
    return id.is_string() ? id.get<std::string>() : id.dump();
}

Vixen::AppFlow::Generated::FlowActionId ParseActionId(const nlohmann::json& value) {
    using Vixen::AppFlow::Generated::FlowActionId;
    if (value.is_number_unsigned() || value.is_number_integer()) {
        const auto raw = value.get<int64_t>();
        if (raw < 0 || raw > std::numeric_limits<uint16_t>::max()) {
            throw std::runtime_error("actionId is outside the generated action range");
        }
        const auto id = static_cast<FlowActionId>(raw);
        for (const auto& decl : Vixen::AppFlow::Generated::kActionDecls) {
            if (decl.id == id) return id;
        }
        throw std::runtime_error("actionId is not declared by the generated AppFlow schema");
    }
    if (value.is_string()) {
        const auto parsed = magic_enum::enum_cast<FlowActionId>(value.get<std::string>());
        if (parsed) {
            for (const auto& decl : Vixen::AppFlow::Generated::kActionDecls) {
                if (decl.id == *parsed) return *parsed;
            }
        }
    }
    throw std::runtime_error("actionId must name a generated action id");
}

std::string ParamValueAsString(const nlohmann::json& value,
                               Vixen::AppFlow::Generated::FlowParamType type) {
    using Vixen::AppFlow::Generated::FlowParamType;
    switch (type) {
        case FlowParamType::String:
            if (!value.is_string()) throw std::runtime_error("expected a string parameter");
            return value.get<std::string>();
        case FlowParamType::Int:
            if (!value.is_number_integer() || value.is_boolean()) {
                throw std::runtime_error("expected an integer parameter");
            }
            return value.dump();
        case FlowParamType::Float:
            if (!value.is_number() || value.is_boolean()) {
                throw std::runtime_error("expected a number parameter");
            }
            return value.dump();
        case FlowParamType::EntityRef:
            if (!value.is_object()) throw std::runtime_error("expected a typed entity reference object");
            if (!value.contains("type") || !value.at("type").is_string() ||
                !value.contains("id") || !(value.at("id").is_string() || value.at("id").is_number_integer())) {
                throw std::runtime_error("entityRef requires string type and string/integer id fields");
            }
            return value.dump();
    }
    throw std::runtime_error("unsupported generated parameter type");
}

std::string Base64Encode(const uint8_t* bytes, size_t size) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve(((size + 2) / 3) * 4);
    for (size_t i = 0; i < size; i += 3) {
        const uint32_t a = bytes[i];
        const uint32_t b = i + 1 < size ? bytes[i + 1] : 0;
        const uint32_t c = i + 2 < size ? bytes[i + 2] : 0;
        const uint32_t value = (a << 16) | (b << 8) | c;
        encoded.push_back(alphabet[(value >> 18) & 63]);
        encoded.push_back(alphabet[(value >> 12) & 63]);
        encoded.push_back(i + 1 < size ? alphabet[(value >> 6) & 63] : '=');
        encoded.push_back(i + 2 < size ? alphabet[value & 63] : '=');
    }
    return encoded;
}

void AppendPngBytes(void* context, void* data, int size) {
    if (!context || !data || size <= 0) return;
    auto* output = static_cast<std::vector<uint8_t>*>(context);
    const auto* bytes = static_cast<const uint8_t*>(data);
    output->insert(output->end(), bytes, bytes + size);
}

void ApplyEditorPreviewBounds(Vixen::SVO::RecipeRegistry::RecipeEntry& entry,
                              const Vixen::Editor::EditorDocumentModel& document) {
    const auto maxima = ParameterRangeMaxima(document);
    const auto derived = Vixen::SVO::Recipe::DeriveConservativeBounds(
        entry.bytecode.data(), static_cast<uint32_t>(entry.bytecode.size()), maxima);
    if (derived.ok && std::isfinite(derived.radius) && derived.radius > 0.0f) {
        entry.boundCenter = derived.center;
        entry.boundRadius = derived.radius;
    } else {
        // Non-whitelisted recipes use the finite editor bake domain as a conservative preview
        // envelope. The camera still frames the document rather than assuming a golden fixture.
        constexpr float kBakeResolution = 64.0f;
        entry.boundCenter = glm::vec3(0.0f);
        entry.boundRadius = kBakeResolution * 0.86602540378f;
    }
    entry.stepRelaxation = 0.9f;
}

bool SameProgram(const std::vector<Yeroket::Sdf::Generated::SdfInstruction>& a,
                 const std::vector<Yeroket::Sdf::Generated::SdfInstruction>& b) {
    return a.size() == b.size() && (a.empty() ||
        std::memcmp(a.data(), b.data(), a.size() * sizeof(a.front())) == 0);
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
    if (const char* overridePath = std::getenv("VIXEN_EDITOR_APPFLOW_PATH")) {
        appFlowPath_ = overridePath;
    } else {
        appFlowPath_ = VIXEN_EDITOR_APPFLOW_PATH;
    }
}

EditorApplication::~EditorApplication() {
    // Complete-type destroy through the bridge -- see EditorLayersViewBridge.h's file header and
    // ~EditorApplication()'s declaration comment in EditorApplication.h.
    Vixen::App::DestroyEditorLayersView(layersView_);
}

void EditorApplication::RefreshLayersView() {
    std::vector<Vixen::App::EditorLayerData> layers;
    layers.reserve(doc_.LayerCount());
    for (uint32_t i = 0; i < doc_.LayerCount(); ++i) {
        Vixen::App::EditorLayerData row;
        row.name = doc_.LayerName(i);
        row.op = Vixen::Editor::EditorDocumentModel::OpName(doc_.View().layers[i].header->op);
        row.enabled = ((doc_.EnabledMask() >> i) & 1u) != 0u;
        Vixen::Editor::DocumentDiagnostic diagnostic;
        doc_.GetLayerProgramField(i, 0u, 3u, row.programFieldValue, diagnostic);
        layers.push_back(std::move(row));
    }
    std::vector<Vixen::App::EditorParameterData> parameters;
    parameters.reserve(doc_.ParameterCount());
    for (uint32_t i = 0; i < doc_.ParameterCount(); ++i) {
        parameters.push_back(Vixen::App::EditorParameterData{
            doc_.ParameterName(i), doc_.ParameterUnit(i), doc_.ParameterValue(i),
            doc_.ParameterMin(i), doc_.ParameterMax(i)});
    }
    Vixen::App::RefreshEditorLayersView(*layersView_, doc_.EnabledMask(), layers, parameters);
}

void EditorApplication::SyncAfterDocumentMutation() {
    using Vixen::AppFlow::ViewNounKey;
    using Vixen::AppFlow::ViewNounId;
    const uint32_t mask = doc_.EnabledMask();
    layerProvider_.WriteU32(ViewNounKey{ViewNounId::EditorNouns_layerMask}, mask);
    rt_.Layers().SetLayerCount(doc_.LayerCount());
    rt_.Layers().SetMask(mask);
    dirty_ = true;
    RefreshLayersView();
}

void EditorApplication::ReconcileLayersView() {
    // Inc-B (design §4): the per-frame .changed<LayerMask>() reconcile. Returns nullopt on a
    // frame with no external change (the common case -- nothing to do); a value means
    // gaiaLayerEntity_'s LayerMask chunk was marked changed since the last call, and this IS the
    // current (already value-pushed) mask.
    const std::optional<uint32_t> reconciled = viewReconcile_.Reconcile(gaiaLayerEntity_);
    if (!reconciled.has_value()) return;

    // External mask writes are requests to the document model, which owns the layer state and
    // validates every accepted mutation. Restore the projection if the request contains bits
    // outside this document's admitted layer range.
    Vixen::Editor::DocumentDiagnostic diagnostic;
    const uint32_t previousMask = doc_.EnabledMask();
    if (!doc_.SetEnabledMask(*reconciled, diagnostic)) {
        logger_->Warning("[EditorApplication] ReconcileLayersView rejected external layer mask: " +
                         diagnostic.message);
        using Vixen::AppFlow::ViewNounKey;
        using Vixen::AppFlow::ViewNounId;
        layerProvider_.WriteU32(ViewNounKey{ViewNounId::EditorNouns_layerMask}, previousMask);
        rt_.Layers().SetMask(previousMask);
        RefreshLayersView();
        return;
    }

    rt_.Layers().SetMask(doc_.EnabledMask());
    if (doc_.EnabledMask() != previousMask) {
        dirty_ = true;
    }
    // VALUE-PUSH into the bound view (design §4) -- RefreshEditorLayersView's own DirtyVariable
    // call (EditorLayersView::PopulateFromDocument) is the "forward into RmlUi's own dirty tracking"
    // half of the loop. Harmless if this races the same-frame echo above (idempotent re-push,
    // DirtyVariable coalesces) -- see ViewReconcileNode.h's file header.
    RefreshLayersView();
}

bool EditorApplication::LoadDocument(const std::string& path) {
    Vixen::Editor::EditorDocumentModel candidate;
    Vixen::Editor::DocumentDiagnostic diagnostic;
    if (!candidate.Load(path, diagnostic)) {
        lastEditorError_ = diagnostic.message;
        return false;
    }
    if (rt_.Load(nullptr, &layerProvider_) != Vixen::AppFlow::LoadResult::Ok) {
        lastEditorError_ = "AppFlowRuntime initial load failed";
        return false;
    }
    rt_.Layers().SetLayerCount(candidate.LayerCount());
    rt_.Layers().SetMask(candidate.EnabledMask());

    // Keep the Gaia view projection and AppFlow undo snapshot mask synchronized with the
    // candidate document's authored enabled state before committing that document.
    {
        using Vixen::AppFlow::ViewNounKey;
        using Vixen::AppFlow::ViewNounId;
        layerProvider_.WriteU32(ViewNounKey{ViewNounId::EditorNouns_layerMask}, candidate.EnabledMask());
    }

    doc_ = std::move(candidate);
    documentPath_ = path;
    lastEditorError_.clear();

    // Inc-A2: initial model->view population -- the editor layer view's bound "layers" array now
    // reflects the ACTUAL mask/names/ops of the freshly loaded document (was static markup with 3
    // hand-authored rows; see editor.rml). A reload re-populates from scratch, same as the
    // SetLayerCount resync above.
    RefreshLayersView();

    RegisterAppFlowHandlers();
    std::error_code appFlowEc;
    appFlowMtime_ = std::filesystem::last_write_time(appFlowPath_, appFlowEc);
    appFlowMtimeInitialized_ = !appFlowEc;
    return true;
}

void EditorApplication::RegisterAppFlowHandlers() {
    // All editor mutations are dispatched through AppFlow, but the document model remains the
    // only authority: every forward and inverse callback calls its validated headless operation.
    using Vixen::AppFlow::Generated::FlowActionId;
    rt_.RegisterHandler(FlowActionId::ToggleLayer, [this](const Vixen::AppFlow::AppFlowRuntime::Params& p) {
        const uint32_t idx = ParseParam(p, "layerIndex");
        if (idx >= doc_.LayerCount()) {
            logger_->Warning("[EditorApplication] ToggleLayer ignored invalid layerIndex=" +
                             std::to_string(idx));
            return;
        }
        const bool wasEnabled = ((doc_.EnabledMask() >> idx) & 1u) != 0u;
        const bool nextEnabled = !wasEnabled;
        rt_.Stack().Dispatch(FlowActionId::ToggleLayer,
                             [this, idx, wasEnabled, nextEnabled](bool forward) {
                Vixen::Editor::DocumentDiagnostic diagnostic;
                if (!doc_.SetLayerEnabled(idx, forward ? nextEnabled : wasEnabled, diagnostic)) {
                    lastEditorError_ = diagnostic.message;
                    logger_->Error("[EditorApplication] ToggleLayer operation failed: " +
                                   diagnostic.message);
                    return;
                }
                SyncAfterDocumentMutation();
            });
    });

    rt_.RegisterHandler(FlowActionId::CreateLayer, [this](const Vixen::AppFlow::AppFlowRuntime::Params&) {
        if (doc_.LayerCount() >= Vixen::Editor::EditorDocumentModel::kMaximumLayerCount) {
            logger_->Warning("[EditorApplication] CreateLayer ignored: document layer limit reached");
            return;
        }
        Vixen::Editor::EditableDocumentLayer layer;
        layer.name = "Rule layer " + std::to_string(doc_.LayerCount() + 1u);
        layer.op = 0u;
        Yeroket::Sdf::Generated::SdfInstruction sphere{};
        sphere.opCode = static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::Sphere);
        sphere.data[3] = 1.0f;
        layer.program.push_back(sphere);
        const uint32_t index = doc_.LayerCount();
        rt_.Stack().Dispatch(FlowActionId::CreateLayer, [this, layer, index](bool forward) {
            Vixen::Editor::DocumentDiagnostic diagnostic;
            const bool ok = forward
                ? doc_.InsertLayer(index, layer, diagnostic)
                : doc_.DeleteLayer(index, nullptr, diagnostic);
            if (!ok) {
                lastEditorError_ = diagnostic.message;
                logger_->Error("[EditorApplication] CreateLayer operation failed: " + diagnostic.message);
                return;
            }
            SyncAfterDocumentMutation();
        });
    });

    rt_.RegisterHandler(FlowActionId::DeleteLayer, [this](const Vixen::AppFlow::AppFlowRuntime::Params& p) {
        const uint32_t index = ParseParam(p, "layerIndex");
        if (doc_.LayerCount() <= 1u) {
            logger_->Warning("[EditorApplication] DeleteLayer ignored: a document must retain one rule layer");
            return;
        }
        Vixen::Editor::EditableDocumentLayer removed;
        Vixen::Editor::DocumentDiagnostic diagnostic;
        if (!doc_.GetLayer(index, removed, diagnostic)) {
            logger_->Warning("[EditorApplication] DeleteLayer ignored: " + diagnostic.message);
            return;
        }
        rt_.Stack().Dispatch(FlowActionId::DeleteLayer, [this, index, removed](bool forward) {
            Vixen::Editor::DocumentDiagnostic opDiagnostic;
            const bool ok = forward
                ? doc_.DeleteLayer(index, nullptr, opDiagnostic)
                : doc_.InsertLayer(index, removed, opDiagnostic);
            if (!ok) {
                lastEditorError_ = opDiagnostic.message;
                logger_->Error("[EditorApplication] DeleteLayer operation failed: " + opDiagnostic.message);
                return;
            }
            SyncAfterDocumentMutation();
        });
    });

    auto registerMove = [this](FlowActionId actionId, bool up) {
        rt_.RegisterHandler(actionId, [this, actionId, up](const Vixen::AppFlow::AppFlowRuntime::Params& p) {
            const uint32_t from = ParseParam(p, "layerIndex");
            if (from >= doc_.LayerCount() || (up && from == 0u) ||
                (!up && from + 1u >= doc_.LayerCount())) return;
            const uint32_t to = up ? from - 1u : from + 1u;
            rt_.Stack().Dispatch(actionId, [this, from, to](bool forward) {
                Vixen::Editor::DocumentDiagnostic diagnostic;
                if (!doc_.MoveLayer(forward ? from : to, forward ? to : from, diagnostic)) {
                    lastEditorError_ = diagnostic.message;
                    logger_->Error("[EditorApplication] MoveLayer operation failed: " + diagnostic.message);
                    return;
                }
                SyncAfterDocumentMutation();
            });
        });
    };
    registerMove(FlowActionId::MoveLayerUp, true);
    registerMove(FlowActionId::MoveLayerDown, false);

    auto registerProgramFieldEdit = [this](FlowActionId actionId, bool increase) {
        rt_.RegisterHandler(actionId, [this, actionId, increase](const Vixen::AppFlow::AppFlowRuntime::Params& p) {
            const uint32_t layerIndex = ParseParam(p, "layerIndex");
            constexpr uint32_t kInstruction = 0u;
            constexpr uint32_t kData0 = 3u;
            float before = 0.0f;
            Vixen::Editor::DocumentDiagnostic diagnostic;
            if (!doc_.GetLayerProgramField(layerIndex, kInstruction, kData0, before, diagnostic)) {
                logger_->Warning("[EditorApplication] EditProgramField ignored: " + diagnostic.message);
                return;
            }
            const float after = before + (increase ? 0.25f : -0.25f);
            if (!std::isfinite(after) || after == before) return;
            rt_.Stack().Dispatch(actionId, [this, layerIndex, before, after,
                                            kInstruction, kData0](bool forward) {
                Vixen::Editor::DocumentDiagnostic opDiagnostic;
                if (!doc_.SetLayerProgramField(layerIndex, kInstruction, kData0,
                                               forward ? after : before, opDiagnostic)) {
                    lastEditorError_ = opDiagnostic.message;
                    logger_->Error("[EditorApplication] EditProgramField operation failed: " + opDiagnostic.message);
                    return;
                }
                SyncAfterDocumentMutation();
            });
        });
    };
    registerProgramFieldEdit(FlowActionId::EditProgramFieldUp, true);
    registerProgramFieldEdit(FlowActionId::EditProgramFieldDown, false);

    auto registerParameterEdit = [this](FlowActionId actionId, bool increase) {
        rt_.RegisterHandler(actionId, [this, actionId, increase](const Vixen::AppFlow::AppFlowRuntime::Params& p) {
            const uint32_t index = ParseParam(p, "parameterIndex");
            if (index >= doc_.ParameterCount()) return;
            const float before = doc_.ParameterValue(index);
            const float span = doc_.ParameterMax(index) - doc_.ParameterMin(index);
            const float step = span > 0.0f ? span / 8.0f : 0.1f;
            const float after = std::clamp(before + (increase ? step : -step),
                                           doc_.ParameterMin(index), doc_.ParameterMax(index));
            if (after == before) return;
            rt_.Stack().Dispatch(actionId, [this, index, before, after](bool forward) {
                Vixen::Editor::DocumentDiagnostic diagnostic;
                if (!doc_.SetParameterValue(index, forward ? after : before, diagnostic)) {
                    lastEditorError_ = diagnostic.message;
                    logger_->Error("[EditorApplication] AdjustParameter operation failed: " + diagnostic.message);
                    return;
                }
                SyncAfterDocumentMutation();
            });
        });
    };
    registerParameterEdit(FlowActionId::AdjustParameterUp, true);
    registerParameterEdit(FlowActionId::AdjustParameterDown, false);

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

void EditorApplication::PollAppFlowFile() {
    constexpr long kPollIntervalTicks = 15;
    if ((updateTick_ % kPollIntervalTicks) != 0) return;

    std::error_code ec;
    const auto mtime = std::filesystem::last_write_time(appFlowPath_, ec);
    if (ec) {
        if (appFlowMtimeInitialized_) {
            logger_->Warning("[EditorApplication] AppFlow watch: cannot stat '" + appFlowPath_ + "'");
        }
        return;
    }
    if (!appFlowMtimeInitialized_) {
        appFlowMtime_ = mtime;
        appFlowMtimeInitialized_ = true;
        return;
    }
    if (mtime == appFlowMtime_) return;

    // Consume this mtime once even when the candidate is rejected. This preserves the running
    // state on parse failure and prevents a broken save from retry-spamming every poll; the next
    // editor save produces a new mtime and a fresh candidate.
    appFlowMtime_ = mtime;

    const auto declaredShape = Vixen::AppFlow::AppFlowBlobFile::ReadDeclaredShapeHash(appFlowPath_);
    if (declaredShape.has_value() && *declaredShape != Vixen::AppFlow::Generated::kAppFlowShapeHash) {
        logger_->Error("[EditorApplication] facade change alters the interface/graph → rebuild required; "
                       "keeping the running compiled-in state");
        return;
    }

    auto blob = Vixen::AppFlow::AppFlowBlobFile::Load(appFlowPath_);
    if (!blob.has_value()) {
        logger_->Error("[EditorApplication] AppFlow reload rejected: parse failure; keeping the running state");
        return;
    }
    if (blob->ShapeHash() != Vixen::AppFlow::Generated::kAppFlowShapeHash) {
        // Defensive gate in addition to AppFlowBlobFile::Parse()'s fail-closed validation.
        logger_->Error("[EditorApplication] facade change alters the interface/graph → rebuild required; "
                       "keeping the running compiled-in state");
        return;
    }

    // AppFlowRuntime::Load() validates and builds fresh primitives before committing them. The
    // parsed blob is a local candidate and may be destroyed immediately after this call: runtime
    // primitives deep-own the pointer-bearing generated fields they retain.
    if (rt_.Load(&blob->View(), &layerProvider_) != Vixen::AppFlow::LoadResult::Ok) {
        logger_->Error("[EditorApplication] AppFlow reload rejected: validation failure; keeping the running state");
        return;
    }
    RegisterAppFlowHandlers();
    logger_->Info("[EditorApplication] AppFlow reloaded from '" + appFlowPath_ + "'");
}

void EditorApplication::BuildRenderGraph() {
    // Capture runs can use the production graph without creating a window or swapchain. This is
    // deliberately opt-in so interactive editor sessions retain their normal presentation path.
    if (const char* offscreenCapture = std::getenv("VIXEN_EDITOR_OFFSCREEN_CAPTURE");
        offscreenCapture && std::string(offscreenCapture) == "1") {
        SetPresentationTarget(PresentationTarget::Offscreen);
    }

    // The base graph's shader builder reads the procedural registry at first Compile(). Register
    // the document's field before building it so the editor uses the same shared virtual-recipe
    // path as other VIXEN consumers.
    {
        const auto snapshot = doc_.CaptureBakeSnapshot();
        Vixen::SVO::RecipeRegistry::RecipeEntry entry;
        Vixen::Editor::DocumentDiagnostic diagnostic;
        if (doc_.FlattenToRecipeEntry(entry, snapshot, diagnostic)) {
            ApplyEditorPreviewBounds(entry, doc_);
            const auto result = RegisterProceduralRecipe(kEditorProceduralRecipeId, entry);
            if (result == Vixen::SVO::RecipeRegistry::RegisterResult::Ok) {
                proceduralPreviewRegistered_ = true;
                lastProceduralProgram_ = entry.bytecode;
            } else {
                lastEditorError_ = "RegisterProceduralRecipe failed (code " +
                    std::to_string(static_cast<int>(result)) + ")";
                logger_->Error("[EditorApplication] " + lastEditorError_);
            }
        } else {
            lastEditorError_ = diagnostic.message;
            logger_->Error("[EditorApplication] BuildRenderGraph could not flatten the preview: " + lastEditorError_);
        }
    }

    // Build the full standard graph unmodified (window, body-octree scene, UI composite HUD),
    // then re-point the UI node at the editor's own document and replace the 3 default demo
    // bodies with the loaded VoxelDocument's single flattened recipe.
    //
    // No capture-specific node is needed. The script harness reads the existing compute target
    // after Render(), before the HUD composite overlays the viewport. The readback only runs on
    // requested capture ticks.
    VulkanGraphApplication::BuildRenderGraph();

    if (auto* ui = GetUiRenderNode()) {
        // Inc-A2: wire the editor's IView instead of the raw RML_DOCUMENT_PATH param -- SetView
        // gives editor.rml a real data-model (CompileImpl calls CreateDataModel(ModelName()) +
        // Register() before LoadDocument), and view_->DocumentPath() ("assets/ui/editor.rml") now
        // supplies the doc path, same convention BuildRenderGraph.cpp uses for hud.rml/HudView.
        // Through the bridge (WireEditorLayersView), not a direct ui->SetView(layersView_) call --
        // this TU never sees UIRenderNode's real definition/IView, only the forward declaration
        // (see EditorLayersViewBridge.h). ResolveUiAsset (UIRenderNode.cpp) strips the "assets/"
        // prefix when resolving against VIXEN_UI_SOURCE_DIR/VIXEN_UI_ASSET_SOURCE_DIR, and checks
        // the literal path relative to CWD otherwise; a bare "editor.rml" (no prefix) fails both
        // and RmlUi logs "Unable to open file editor.rml" (found via the windowed smoke test).
        Vixen::App::WireEditorLayersView(*ui, *layersView_);
    }

    if (auto* cameraInst = GetRenderGraph() ? GetRenderGraph()->GetInstanceByName("raymarch_camera") : nullptr) {
        using CC = Vixen::RenderGraph::CameraNodeConfig;
        // The capture test asks for a top-down view so the layer toggle changes visible pixels.
        // Leave the editor's default side view untouched for interactive sessions.
        if (const char* captureCamera = std::getenv("VIXEN_EDITOR_TEST_CAMERA");
            captureCamera && std::string(captureCamera) == "top-down") {
            cameraInst->SetParameter(CC::PARAM_YAW, 0.0f);
            cameraInst->SetParameter(CC::PARAM_PITCH, 1.45f);
        }
    }

    if (!ApplyDocumentToScene(/*reframeCamera=*/true)) {
        logger_->Error("[EditorApplication] BuildRenderGraph: ApplyDocumentToScene failed: " +
                       lastEditorError_);
    }
}

bool EditorApplication::ApplyDocumentToScene(bool reframeCamera) {
    TraceEditorLatency("flatten", "begin", updateTick_);
    Vixen::SVO::RecipeRegistry::RecipeEntry entry;
    Vixen::Editor::DocumentDiagnostic diagnostic;
    const Vixen::Editor::DocumentBakeSnapshot snapshot = doc_.CaptureBakeSnapshot();
    if (!doc_.FlattenToRecipeEntry(entry, snapshot, diagnostic)) {
        lastEditorError_ = diagnostic.message;
        return false;
    }
    ApplyEditorPreviewBounds(entry, doc_);
    TraceEditorLatency("flatten", "end", updateTick_);
    TraceEditorLatency("registry", "begin", updateTick_);

    // Runtime parameter values remain per-instance data. Only a changed canonical bytecode
    // program replaces the registry entry and triggers a shader splice rebuild.
    if (!proceduralPreviewRegistered_) {
        const auto result = RegisterProceduralRecipe(kEditorProceduralRecipeId, entry);
        if (result != Vixen::SVO::RecipeRegistry::RegisterResult::Ok) {
            lastEditorError_ = "RegisterProceduralRecipe failed (code " +
                std::to_string(static_cast<int>(result)) + ")";
            return false;
        }
        proceduralPreviewRegistered_ = true;
        lastProceduralProgram_ = entry.bytecode;
        TraceEditorLatency("shader_request", "begin", updateTick_);
        RecompileProceduralShader();
        TraceEditorLatency("shader_request", "end", updateTick_);
    } else if (!SameProgram(entry.bytecode, lastProceduralProgram_)) {
        const auto result = ReplaceProceduralRecipe(kEditorProceduralRecipeId, entry);
        if (result != Vixen::SVO::RecipeRegistry::RegisterResult::Ok) {
            lastEditorError_ = "ReplaceProceduralRecipe failed (code " +
                std::to_string(static_cast<int>(result)) + ")";
            return false;
        }
        lastProceduralProgram_ = entry.bytecode;
        TraceEditorLatency("shader_request", "begin", updateTick_);
        RecompileProceduralShader();
        TraceEditorLatency("shader_request", "end", updateTick_);
    }

    TraceEditorLatency("registry", "end", updateTick_);
    TraceEditorLatency("bake", "begin", updateTick_);
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

    TraceEditorLatency("bake", "end", updateTick_);
    TraceEditorLatency("publish", "begin", updateTick_);
    lastBakeSnapshot_ = snapshot;
    for (size_t i = 0; i < lastPreviewParameterValues_.size(); ++i) {
        lastPreviewParameterValues_[i] = snapshot.parameterValues[i];
    }

    SetRecipePool(std::move(bakeResult.pool));

    // One virtual provider body evaluates the exact same registered bytecode used by the CPU
    // bake. Its six runtime values are copied from that bake snapshot; the CPU octree remains
    // available as the explicit baked result, while the editor's visible body uses the virtual
    // recipe path for immediate parameter feedback.
    Vixen::SVO::BodyInstanceGpu inst{};
    Vixen::SVO::SetInstanceTranslationScale(inst, glm::vec3(0.0f), 1.0f);
    inst.material.color[0] = 1.0f; inst.material.color[1] = 1.0f; inst.material.color[2] = 1.0f;
    inst.material.octreeIndex = 0u;
    inst.material.providerKind = Vixen::SVO::PROVIDER_PROCEDURAL;
    inst.material.recipeId = kEditorProceduralRecipeId;
    for (size_t i = 0; i < snapshot.parameterValues.size(); ++i)
        inst.material.recipeParams[i] = snapshot.parameterValues[i];
    SetBodyInstances({inst});
    TraceEditorLatency("publish", "end", updateTick_);

    if (entry.parameterValues.size() != snapshot.parameterValues.size() ||
        !std::equal(snapshot.parameterValues.begin(), snapshot.parameterValues.end(),
                    entry.parameterValues.begin())) {
        lastEditorError_ = "CPU bake and virtual preview parameter snapshots diverged";
        return false;
    }

    std::ostringstream values;
    values << "[EDITOR/snapshot] revision=" << snapshot.revision << " cpu=[";
    for (size_t i = 0; i < snapshot.parameterValues.size(); ++i) {
        if (i) values << ',';
        values << snapshot.parameterValues[i];
    }
    values << "] preview=[";
    for (size_t i = 0; i < lastPreviewParameterValues_.size(); ++i) {
        if (i) values << ',';
        values << lastPreviewParameterValues_[i];
    }
    values << "] programInstructions=" << entry.bytecode.size();
    logger_->Info(values.str());

    if (reframeCamera) {
        if (auto* cameraInst = GetRenderGraph()
                ? GetRenderGraph()->GetInstanceByName("raymarch_camera") : nullptr) {
            using CC = Vixen::RenderGraph::CameraNodeConfig;
            // Virtual recipe coordinates are world units. The helper's one-voxel-to-world
            // factor is normalized to 1.0 here so arbitrary recipe bounds map directly.
            const auto frame = Vixen::Editor::FitEditorCameraToBounds(
                entry.boundCenter, entry.boundRadius, glm::vec3(0.0f), 64,
                /*worldGridSize=*/10.0f, /*renderScale=*/6.4f,
                /*verticalFovDegrees=*/45.0f, /*padding=*/1.15f);
            cameraInst->SetParameter(CC::PARAM_ORBIT_CENTER_X, frame.center.x);
            cameraInst->SetParameter(CC::PARAM_ORBIT_CENTER_Y, frame.center.y);
            cameraInst->SetParameter(CC::PARAM_ORBIT_CENTER_Z, frame.center.z);
            cameraInst->SetParameter(CC::PARAM_ORBIT_DISTANCE, frame.distance);
        }
    }

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
    // Live lookups every call -- never cache a node pointer across graph recompiles.
    auto* graph = GetRenderGraph();
    if (!graph) {
        err = "CaptureFrameToPng: no render graph";
        return false;
    }

    // Read the pre-composite scene image; the main_swapchain capture includes editor panels that
    // can cover the document body and create a UI-only pixel delta.
    static constexpr const char* kCaptureTargetName = "compute_render_target";
    auto* targetInst = graph->GetInstanceByName(kCaptureTargetName);
    if (!targetInst) {
        err = std::string("CaptureFrameToPng: instance '") + kCaptureTargetName + "' not found";
        return false;
    }
    Resource* targetOutput = targetInst->GetOutput(0, 0);
    if (!targetOutput) {
        err = "CaptureFrameToPng: RENDER_TARGET output is unavailable (graph not compiled?)";
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

    const auto snapshot = doc_.CaptureBakeSnapshot();
    Vixen::SVO::RecipeRegistry::RecipeEntry canonical;
    Vixen::Editor::DocumentDiagnostic diagnostic;
    if (!doc_.FlattenToRecipeEntry(canonical, snapshot, diagnostic)) {
        lastEditorError_ = diagnostic.message;
        return false;
    }
    if (!doc_.Save(outPath, diagnostic)) {
        lastEditorError_ = diagnostic.message;
        return false;
    }
    lastSavedPath_ = outPath;
    lastSavedProgram_ = std::move(canonical.bytecode);
    lastSavedParameters_ = snapshot.parameterValues;
    lastSavedMask_ = doc_.EnabledMask();
    lastEditorError_.clear();
    logger_->Info("[EditorApplication] Saved document to " + outPath);
    std::ostringstream state;
    state << "[EDITOR/state] save revision=" << snapshot.revision << " mask=" << lastSavedMask_
          << " parameterCount=" << doc_.ParameterCount();
    logger_->Info(state.str());
    return true;
}

bool EditorApplication::ReopenSavedDocument() {
    if (lastSavedPath_.empty()) {
        lastEditorError_ = "ReopenSavedDocument requires a successful save first";
        return false;
    }
    Vixen::Editor::EditorDocumentModel candidate;
    Vixen::Editor::DocumentDiagnostic diagnostic;
    if (!candidate.Load(lastSavedPath_, diagnostic)) {
        lastEditorError_ = diagnostic.message;
        return false;
    }

    const auto snapshot = candidate.CaptureBakeSnapshot();
    Vixen::SVO::RecipeRegistry::RecipeEntry canonical;
    if (!candidate.FlattenToRecipeEntry(canonical, snapshot, diagnostic)) {
        lastEditorError_ = diagnostic.message;
        return false;
    }
    const bool programMatch = SameProgram(canonical.bytecode, lastSavedProgram_);
    const bool parametersMatch = std::equal(snapshot.parameterValues.begin(), snapshot.parameterValues.end(),
                                            lastSavedParameters_.begin());
    const bool maskMatch = candidate.EnabledMask() == lastSavedMask_;
    if (!programMatch || !parametersMatch || !maskMatch) {
        lastEditorError_ = "reopened document differs from the saved canonical program, parameters, or layer mask";
        logger_->Error("[EditorApplication] " + lastEditorError_);
        return false;
    }

    auto blob = Vixen::AppFlow::AppFlowBlobFile::Load(appFlowPath_);
    const Vixen::AppFlow::Generated::AppFlowContainerView* view = blob ? &blob->View() : nullptr;
    if (rt_.Load(view, &layerProvider_) != Vixen::AppFlow::LoadResult::Ok) {
        lastEditorError_ = "AppFlowRuntime reload failed while reopening the saved document";
        return false;
    }
    doc_ = std::move(candidate);
    rt_.Layers().SetLayerCount(doc_.LayerCount());
    rt_.Layers().SetMask(doc_.EnabledMask());
    RegisterAppFlowHandlers();
    SyncAfterDocumentMutation();
    lastEditorError_.clear();
    logger_->Info("[EDITOR/state] reopen revision=" + std::to_string(doc_.Revision()) +
                  " programMatch=1 parametersMatch=1 maskMatch=1");
    return true;
}

void EditorApplication::PreTick() {
    TraceEditorLatency("frame", "begin", updateTick_);
    if (channelPaused_ && channelStepBudget_ == 0) return;
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
        TraceEditorLatency("dispatch", "begin", updateTick_, static_cast<int>(action.kind));
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
                const auto r = rt_.DispatchBySelector("layer-" + std::to_string(action.index) + "-toggle");
                logger_->Info("[EDITOR/state] toggle mask=" + std::to_string(rt_.Layers().Mask()) +
                               " undoDepth=" + std::to_string(rt_.Stack().UndoDepth()) +
                               " redoDepth=" + std::to_string(rt_.Stack().RedoDepth()) +
                               " result=" + std::to_string(static_cast<int>(r)));
                break;
            }
            case ScriptedAction::Kind::CreateLayer: {
                const auto r = rt_.DispatchBySelector("add-layer");
                logger_->Info("[EDITOR/state] create layerCount=" + std::to_string(doc_.LayerCount()) +
                               " undoDepth=" + std::to_string(rt_.Stack().UndoDepth()) +
                               " result=" + std::to_string(static_cast<int>(r)));
                break;
            }
            case ScriptedAction::Kind::DeleteLayer:
            case ScriptedAction::Kind::MoveLayerUp:
            case ScriptedAction::Kind::MoveLayerDown:
            case ScriptedAction::Kind::ProgramFieldUp:
            case ScriptedAction::Kind::ProgramFieldDown: {
                const std::string index = std::to_string(action.index);
                const char* suffix = "";
                const char* label = "";
                switch (action.kind) {
                    case ScriptedAction::Kind::DeleteLayer: suffix = "-delete"; label = "delete"; break;
                    case ScriptedAction::Kind::MoveLayerUp: suffix = "-up"; label = "move_up"; break;
                    case ScriptedAction::Kind::MoveLayerDown: suffix = "-down"; label = "move_down"; break;
                    case ScriptedAction::Kind::ProgramFieldUp: suffix = "-program-up"; label = "program_up"; break;
                    case ScriptedAction::Kind::ProgramFieldDown: suffix = "-program-down"; label = "program_down"; break;
                    default: break;
                }
                const auto r = rt_.DispatchBySelector("layer-" + index + suffix);
                logger_->Info("[EDITOR/state] " + std::string(label) + " layerCount=" +
                               std::to_string(doc_.LayerCount()) + " revision=" +
                               std::to_string(doc_.Revision()) + " undoDepth=" +
                               std::to_string(rt_.Stack().UndoDepth()) + " result=" +
                               std::to_string(static_cast<int>(r)));
                break;
            }
            case ScriptedAction::Kind::ParameterUp:
            case ScriptedAction::Kind::ParameterDown: {
                const char* suffix = action.kind == ScriptedAction::Kind::ParameterUp ? "-up" : "-down";
                const char* label = action.kind == ScriptedAction::Kind::ParameterUp ? "parameter_up" : "parameter_down";
                const auto r = rt_.DispatchBySelector("parameter-" + std::to_string(action.index) + suffix);
                const float value = doc_.ParameterValue(action.index);
                logger_->Info("[EDITOR/state] " + std::string(label) + " parameterIndex=" +
                               std::to_string(action.index) + " value=" + std::to_string(value) +
                               " revision=" + std::to_string(doc_.Revision()) + " result=" +
                               std::to_string(static_cast<int>(r)));
                break;
            }
            case ScriptedAction::Kind::Undo: {
                const auto r = rt_.DispatchBySelector("undo-button");
                logger_->Info("[EDITOR/state] undo mask=" + std::to_string(rt_.Layers().Mask()) +
                               " undoDepth=" + std::to_string(rt_.Stack().UndoDepth()) +
                               " redoDepth=" + std::to_string(rt_.Stack().RedoDepth()) +
                               " result=" + std::to_string(static_cast<int>(r)));
                break;
            }
            case ScriptedAction::Kind::Redo: {
                const auto r = rt_.DispatchBySelector("redo-button");
                logger_->Info("[EDITOR/state] redo mask=" + std::to_string(rt_.Layers().Mask()) +
                               " undoDepth=" + std::to_string(rt_.Stack().UndoDepth()) +
                               " redoDepth=" + std::to_string(rt_.Stack().RedoDepth()) +
                               " result=" + std::to_string(static_cast<int>(r)));
                break;
            }
            case ScriptedAction::Kind::Save: {
                const auto r = rt_.DispatchBySelector("save-button");
                logger_->Info("[EDITOR/state] save dispatch=" +
                               std::to_string(static_cast<int>(r)) + " path=" + lastSavedPath_);
                break;
            }
            case ScriptedAction::Kind::Reopen:
                if (!ReopenSavedDocument()) {
                    logger_->Error("[EditorApplication] scripted reopen failed: " + lastEditorError_);
                }
                break;
            case ScriptedAction::Kind::Settings:
                // This scripted system request names the generated declaration and its cause.
                rt_.NavTo(Vixen::AppFlow::Generated::FlowEdgeId::ToSettings,
                          {Vixen::AppFlow::FlowTriggerKind::System, "EditorApplication"});
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
        TraceEditorLatency("dispatch", "end", updateTick_, static_cast<int>(action.kind));
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
    PumpAppFlowChannel();
    StartPendingReadbacks();
    if (channelPaused_ && channelStepBudget_ == 0) return;
    VulkanGraphApplication::Update();

    // Inc-2b M3 (carried over from the M2 validator): the base VulkanGraphApplication::Update's
    // try/catch (VulkanGraphApplication.cpp) is scoped to that method's OWN body -- it returns
    // before control reaches here, so nothing below is actually covered by it. A prior version of
    // this comment claimed otherwise; wrap this override's own body in its own guard (mirroring
    // the base method's catch shape) so the no-throw-across-the-tick contract (design §5) really
    // holds for the toggle/undo/capture/script code added in Inc-2b, not just by assertion.
    try {
    // T1.2: check the external facade before this tick's input dispatch. A successful compatible
    // reload therefore supplies the new trigger/key tables to the running editor immediately;
    // a rejected candidate leaves the already-loaded runtime untouched.
    PollAppFlowFile();
    // Drain UI clicks (S4 pattern) and dispatch by selector -- carries no behavior itself; the
    // registered ToggleLayer/Return handlers decide what the click means (design §4.3). A
    // selector with no binding (a non-editor UI hit) resolves to RejectedByState and is ignored.
    if (auto* selection = GetUiSelectionProviderNode()) {
        const std::string clickedId = selection->DrainClickedElementId();
        if (!clickedId.empty()) {
            TraceEditorLatency("dispatch", "begin", updateTick_);
            rt_.DispatchBySelector(clickedId);
            TraceEditorLatency("dispatch", "end", updateTick_);
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

    // Inc-B (design §4a): the per-frame .changed<LayerMask>() reconcile, placed AFTER all input
    // dispatch above (UI click + keybindings) so an EXTERNAL write picked up this frame lands in
    // the same dirty_ re-flatten tail below as an input-driven toggle would. The input path's own
    // same-frame echo (inside the ToggleLayer handler) already covers the self case; this call is
    // what makes a non-input Gaia write reach the view at all.
    ReconcileLayersView();
    if (doc_.EnabledMask() != channelObservedMask_) {
        channelObservedMask_ = doc_.EnabledMask();
        RecordAppFlowEvent("view.reconcile");
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

    // Inc-2b Task 4: schedule a capture after this tick's dirty_ re-flatten tail. PostTick performs
    // the readback after Render(), so the offscreen scene target contains the post-toggle image
    // and UI overlays cannot create a false-positive pixel difference.
    for (const long captureFrame : captureFrames_) {
        if (captureFrame != updateTick_) continue;
        const std::string path = captureDir_ + "/editor_capture_" + std::to_string(updateTick_) + ".png";
        // Canonical parseable line: correlates each capture PNG to the mask it was taken at, so the
        // R6 gate's residency smoke-check knows which frame is pre- vs post-first-edit.
        logger_->Info("[EDITOR/state] capture tick=" + std::to_string(updateTick_) +
                       " mask=" + std::to_string(rt_.Layers().Mask()));
        pendingCapturePath_ = path;
    }

    // Advanced AFTER this tick's script/capture checks above compare against it, so updateTick_
    // is 0 on the very first Update() call rather than 1 (the pre-existing ++ prefix here made a
    // scripted "@0"/capture-frame-0 entry permanently un-hittable -- found live via the M3
    // windowed gate: editor_capture_0.png never appeared even though captureFrames_ contained 0).
    // Note frame 0 is still not a useful CAPTURE frame regardless of this fix -- it is the first
    // Render() call and the document graph has not drawn a completed frame before it.
    // Scripted ACTIONS (toggle/undo/redo) at frame 0 are unaffected by that -- they mutate the
    // mask/ActionStack regardless of what's on screen yet.
    ++updateTick_;
    if (channelPaused_ && channelStepBudget_ > 0) --channelStepBudget_;
    } catch (const std::exception& e) {
        lastEditorError_ = std::string("Update failed: ") + e.what();
        logger_->Error("[EditorApplication] Update: " + lastEditorError_);
    } catch (...) {
        lastEditorError_ = "Update failed: unknown (non-std) exception";
        logger_->Error("[EditorApplication] Update: " + lastEditorError_);
    }
}

void EditorApplication::PostTick() {
    TraceEditorLatency("frame", "end", updateTick_ - 1);
    // VulkanApplicationBase::Tick calls this only after Render(), when compute_render_target
    // contains the completed scene image for the Update tick that scheduled the capture.
    if (!pendingCapturePath_.empty()) {
        const std::string path = std::move(pendingCapturePath_);
        pendingCapturePath_.clear();
        std::string captureErr;
        TraceEditorLatency("readback", "begin", updateTick_ - 1);
        if (!CaptureFrameToPng(path, captureErr)) {
            logger_->Error("[EditorApplication] CaptureFrameToPng failed for " + path + ": " + captureErr);
        }
        TraceEditorLatency("readback", "end", updateTick_ - 1);
    }
    VulkanGraphApplication::PostTick();
    ++channelPresentedFrame_;
    if (appFlowChannel_) {
        appFlowChannel_->SetPresentedFrame(channelPresentedFrame_);
        StartPendingReadbacks();
        PumpAppFlowChannel();
    }
}

void EditorApplication::PumpAppFlowChannel() {
    if (!appFlowChannel_) {
        appFlowChannel_ = std::make_unique<AppFlowChannel>();
        std::string error;
        if (!appFlowChannel_->Start(error)) {
            logger_->Error("[AppFlow/channel] start failed: " + error);
            appFlowChannel_.reset();
            return;
        }
        channelObservedMask_ = doc_.EnabledMask();
        logger_->Info("[AppFlow/channel] descriptor=" + appFlowChannel_->DescriptorPath());
    }
    appFlowChannel_->Poll([this](const std::string& request) {
        return HandleAppFlowRequest(request);
    });
}

void EditorApplication::StartPendingReadbacks() {
    if (channelReadbacks_.empty()) return;
    using namespace Vixen::RenderGraph;
    using Vixen::Vulkan::Resources::IRenderTarget;

    auto* graph = GetRenderGraph();
    auto* deviceNode = graph
        ? static_cast<DeviceNode*>(graph->GetInstanceByName("main_device")) : nullptr;
    auto* device = deviceNode ? deviceNode->GetVulkanDevice() : nullptr;
    if (!graph || !device) {
        for (auto it = channelReadbacks_.begin(); it != channelReadbacks_.end();) {
            channelReadbackResults_[it->first] = nlohmann::json{{"error", "render graph/device is unavailable"}}.dump();
            it = channelReadbacks_.erase(it);
        }
        return;
    }

    auto* target = static_cast<IRenderTarget*>(nullptr);
    if (auto* swapInstance = graph->GetInstanceByName("main_swapchain")) {
        target = static_cast<SwapChainNode*>(swapInstance)->GetSwapchainPublic();
    }
    if (!target) {
        auto* renderTargetInstance = graph->GetInstanceByName("compute_render_target");
        if (renderTargetInstance) {
            Resource* output = renderTargetInstance->GetOutput(0, 0);
            if (output) target = output->GetHandle<IRenderTarget*>();
        }
    }
    if (!target) {
        for (auto it = channelReadbacks_.begin(); it != channelReadbacks_.end();) {
            channelReadbackResults_[it->first] = nlohmann::json{{"error", "native render target is unavailable"}}.dump();
            it = channelReadbacks_.erase(it);
        }
        return;
    }

    const VkExtent2D extent = target->GetExtent();
    const auto asU32 = [](const nlohmann::json& value, const char* name) -> uint32_t {
        if ((!value.is_number_unsigned() && !value.is_number_integer()) || value.is_boolean()) {
            throw std::runtime_error(std::string(name) + " must be an integer");
        }
        const int64_t raw = value.get<int64_t>();
        if (raw < 0 || static_cast<uint64_t>(raw) > std::numeric_limits<uint32_t>::max()) {
            throw std::runtime_error(std::string(name) + " is outside the valid pixel range");
        }
        return static_cast<uint32_t>(raw);
    };
    const auto clampRect = [&](double minX, double minY, double maxX, double maxY,
                               uint32_t& x, uint32_t& y, uint32_t& width, uint32_t& height) {
        const int64_t left = std::clamp<int64_t>(static_cast<int64_t>(std::floor(minX)), 0, extent.width);
        const int64_t top = std::clamp<int64_t>(static_cast<int64_t>(std::floor(minY)), 0, extent.height);
        const int64_t right = std::clamp<int64_t>(static_cast<int64_t>(std::ceil(maxX)), 0, extent.width);
        const int64_t bottom = std::clamp<int64_t>(static_cast<int64_t>(std::ceil(maxY)), 0, extent.height);
        if (right <= left || bottom <= top) throw std::runtime_error("selection has no visible pixels in the native frame");
        x = static_cast<uint32_t>(left); y = static_cast<uint32_t>(top);
        width = static_cast<uint32_t>(right - left); height = static_cast<uint32_t>(bottom - top);
    };

    for (auto it = channelReadbacks_.begin(); it != channelReadbacks_.end();) {
        PendingReadback& pending = it->second;
        if (pending.enqueued || pending.frame > channelPresentedFrame_) { ++it; continue; }
        const std::string key = it->first;
        try {
            if (pending.frame < channelPresentedFrame_) {
                throw std::runtime_error("selected frame passed before its readback was staged");
            }
            const nlohmann::json spec = nlohmann::json::parse(pending.spec);
            const nlohmann::json selection = spec.contains("selection")
                ? spec.at("selection") : spec;
            const std::string format = spec.value("format", std::string("png"));
            if (format != "png" && format != "raw") throw std::runtime_error("format must be png or raw");
            if (!selection.is_object() || !selection.contains("type") || !selection.at("type").is_string()) {
                throw std::runtime_error("selection requires a type");
            }
            const std::string type = selection.at("type").get<std::string>();
            uint32_t x = 0, y = 0, width = extent.width, height = extent.height;
            if (type == "fullFrame") {
                // Native target extent is the full-frame contract.
            } else if (type == "screenRect") {
                x = asU32(selection.at("x"), "x");
                y = asU32(selection.at("y"), "y");
                width = asU32(selection.at("width"), "width");
                height = asU32(selection.at("height"), "height");
            } else if (type == "selector") {
                if (!selection.contains("selector") || !selection.at("selector").is_string()) {
                    throw std::runtime_error("selector selection requires a selector string");
                }
                auto* uiNode = GetUiRenderNode();
                Rml::Context* context = uiNode ? uiNode->GetUiContext() : nullptr;
                if (!context) throw std::runtime_error("UI context is unavailable in this session");
                std::string selector = selection.at("selector").get<std::string>();
                if (!selector.empty() && selector.front() == '#') selector.erase(selector.begin());
                Rml::Element* root = context->GetRootElement();
                Rml::Element* element = root ? root->GetElementById(selector) : nullptr;
                if (!element) throw std::runtime_error("UI selector did not resolve to an element id");
                const auto offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);
                const auto size = element->GetBox().GetSize(Rml::BoxArea::Border);
                clampRect(offset.x, offset.y, offset.x + size.x, offset.y + size.y, x, y, width, height);
            } else if (type == "sceneInstance") {
                if (!selection.contains("reference") || !selection.at("reference").is_object()) {
                    throw std::runtime_error("sceneInstance requires a typed reference object");
                }
                const auto& reference = selection.at("reference");
                if (reference.value("type", std::string{}) != "bodyInstance" || !reference.contains("index")) {
                    throw std::runtime_error("scene instance reference must use type bodyInstance and an index");
                }
                const uint32_t index = asU32(reference.at("index"), "reference.index");
                auto* scene = static_cast<BodyOctreeSceneNode*>(graph->GetInstanceByName("body_octree_scene"));
                auto* camera = static_cast<CameraNode*>(graph->GetInstanceByName("raymarch_camera"));
                if (!scene || !camera || index >= scene->GetInstances().size()) {
                    throw std::runtime_error("scene instance or camera reference is unavailable");
                }
                Vixen::SVO::RecipeRegistry::RecipeEntry recipe;
                Vixen::Editor::DocumentDiagnostic diagnostic;
                if (!doc_.FlattenToRecipeEntry(recipe, doc_.CaptureBakeSnapshot(), diagnostic)) {
                    throw std::runtime_error("could not derive scene instance bounds: " + diagnostic.message);
                }
                ApplyEditorPreviewBounds(recipe, doc_);
                const glm::mat4 localToWorld = Vixen::SVO::ToMat4(scene->GetInstances()[index].transform.localToWorld);
                const glm::mat4 viewProjection = camera->GetCurrentViewProj();
                double minX = std::numeric_limits<double>::infinity();
                double minY = std::numeric_limits<double>::infinity();
                double maxX = -std::numeric_limits<double>::infinity();
                double maxY = -std::numeric_limits<double>::infinity();
                const glm::vec3 center = recipe.boundCenter;
                const glm::vec3 radius(recipe.boundRadius);
                for (uint32_t corner = 0; corner < 8; ++corner) {
                    const glm::vec3 local = center + glm::vec3(
                        (corner & 1u) ? radius.x : -radius.x,
                        (corner & 2u) ? radius.y : -radius.y,
                        (corner & 4u) ? radius.z : -radius.z);
                    const glm::vec4 clip = viewProjection * localToWorld * glm::vec4(local, 1.0f);
                    if (clip.w <= 1e-5f) continue;
                    const double ndcX = clip.x / clip.w;
                    const double ndcY = clip.y / clip.w;
                    const double pixelX = (ndcX * 0.5 + 0.5) * extent.width;
                    const double pixelY = (0.5 - ndcY * 0.5) * extent.height;
                    minX = std::min(minX, pixelX); maxX = std::max(maxX, pixelX);
                    minY = std::min(minY, pixelY); maxY = std::max(maxY, pixelY);
                }
                if (!std::isfinite(minX) || !std::isfinite(minY)) {
                    throw std::runtime_error("scene instance bounds are behind the camera");
                }
                clampRect(minX, minY, maxX, maxY, x, y, width, height);
            } else {
                throw std::runtime_error("selection type must be fullFrame, screenRect, selector, or sceneInstance");
            }

            if (!appFlowReadback_) appFlowReadback_ = std::make_unique<AppFlowReadbackRing>();
            std::string error;
            if (!appFlowReadback_->Enqueue(key, pending.frame, device, device->queue,
                    device->graphicsQueueIndex, target, x, y, width, height, error)) {
                throw std::runtime_error(error);
            }
            pending.enqueued = true;
        } catch (const std::exception& exception) {
            channelReadbackResults_[key] = nlohmann::json{{"error", exception.what()}}.dump();
            it = channelReadbacks_.erase(it);
            continue;
        }
        ++it;
    }

    if (!appFlowReadback_) return;
    for (auto& completed : appFlowReadback_->Poll()) {
        auto pendingIt = channelReadbacks_.find(completed.key);
        if (pendingIt == channelReadbacks_.end()) continue;
        nlohmann::json response;
        if (!completed.error.empty()) {
            response = {{"error", completed.error}};
        } else {
            const auto responseStarted = std::chrono::steady_clock::now();
            const nlohmann::json spec = nlohmann::json::parse(pendingIt->second.spec);
            const std::string format = spec.value("format", std::string("png"));
            std::vector<uint8_t> pixels = completed.pixels;
            const bool bgra = completed.format == VK_FORMAT_B8G8R8A8_UNORM ||
                              completed.format == VK_FORMAT_B8G8R8A8_SRGB;
            std::string encodedFormat = format;
            if (format == "png") {
                if (bgra) {
                    for (size_t p = 0; p + 3 < pixels.size(); p += 4) std::swap(pixels[p], pixels[p + 2]);
                }
                std::vector<uint8_t> png;
                const int pngOk = stbi_write_png_to_func(
                    &AppendPngBytes, &png, static_cast<int>(completed.width),
                    static_cast<int>(completed.height), 4, pixels.data(),
                    static_cast<int>(completed.width * 4));
                if (!pngOk || png.empty()) {
                    response = {{"error", "PNG encoding failed"}};
                } else {
                    encodedFormat = "png";
                    response = {
                        {"format", encodedFormat}, {"mimeType", "image/png"},
                        {"pixelsBase64", Base64Encode(png.data(), png.size())},
                        {"hash", Vixen::Hash::ComputeHash64(completed.pixels)},
                        {"pixelFormat", bgra ? "BGRA8" : "RGBA8"},
                        {"frame", completed.frame}, {"x", completed.x}, {"y", completed.y},
                        {"width", completed.width}, {"height", completed.height},
                        {"nativeWidth", extent.width}, {"nativeHeight", extent.height},
                        {"transferBytes", completed.transferBytes},
                        {"fullImageTransferFallback", completed.fullImageTransferFallback},
                        {"submitCpuUs", completed.enqueueCpuUs},
                        {"readbackRoundTripUs", std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - pendingIt->second.requestedAt).count()}
                    };
                }
            } else {
                response = {
                    {"format", "raw"}, {"mimeType", bgra ? "application/x-bgra8" : "application/x-rgba8"},
                    {"pixelsBase64", Base64Encode(completed.pixels.data(), completed.pixels.size())},
                    {"hash", Vixen::Hash::ComputeHash64(completed.pixels)},
                    {"pixelFormat", bgra ? "BGRA8" : "RGBA8"},
                    {"frame", completed.frame}, {"x", completed.x}, {"y", completed.y},
                    {"width", completed.width}, {"height", completed.height},
                    {"nativeWidth", extent.width}, {"nativeHeight", extent.height},
                    {"transferBytes", completed.transferBytes},
                    {"fullImageTransferFallback", completed.fullImageTransferFallback},
                    {"submitCpuUs", completed.enqueueCpuUs},
                    {"readbackRoundTripUs", std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - pendingIt->second.requestedAt).count()}
                };
            }
            if (!response.contains("error")) {
                const uint64_t encodeHashCpuUs = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - responseStarted).count());
                response["mapCropCpuUs"] = completed.completionCpuUs;
                response["encodeHashCpuUs"] = encodeHashCpuUs;
                response["frameTimeImpactUs"] = completed.enqueueCpuUs + completed.completionCpuUs + encodeHashCpuUs;
            }
        }
        channelReadbackResults_[completed.key] = response.dump();
        channelReadbacks_.erase(pendingIt);
    }
}

void EditorApplication::RecordAppFlowEvent(const std::string& type) {
    if (!appFlowChannel_) return;
    using namespace Vixen::AppFlow;
    using namespace Vixen::AppFlow::Generated;
    nlohmann::json event{
        {"sequence", ++channelEventSequence_},
        {"type", type},
        {"frame", channelPresentedFrame_},
        {"flowState", EnumRecord(rt_.Current())},
        {"undoDepth", rt_.Stack().UndoDepth()},
        {"redoDepth", rt_.Stack().RedoDepth()},
        {"layerMask", doc_.EnabledMask()}
    };
    channelEvents_.push_back(event.dump());
    if (channelEvents_.size() > 256) channelEvents_.erase(channelEvents_.begin());
}

std::optional<std::string> EditorApplication::HandleAppFlowRequest(const std::string& rawRequest) {
    using namespace Vixen::AppFlow;
    using namespace Vixen::AppFlow::Generated;

    const nlohmann::json request = nlohmann::json::parse(rawRequest);
    if (!request.is_object() || !request.contains("method") || !request.at("method").is_string()) {
        throw std::runtime_error("request requires a method string");
    }
    const std::string method = request.at("method").get<std::string>();
    const nlohmann::json params = request.value("params", nlohmann::json::object());
    if (!params.is_object()) throw std::runtime_error("params must be an object");

    if (method == "appflow.list") {
        nlohmann::json actions = nlohmann::json::array();
        for (const auto& declaration : kActionDecls) {
            nlohmann::json paramList = nlohmann::json::array();
            for (uint32_t i = 0; i < declaration.paramCount; ++i) {
                paramList.push_back({{"name", declaration.params[i].name},
                                     {"type", ParamTypeName(declaration.params[i].type)}});
            }
            actions.push_back({{"id", static_cast<uint16_t>(declaration.id)},
                               {"name", std::string(magic_enum::enum_name(declaration.id))},
                               {"params", std::move(paramList)}});
        }
        nlohmann::json selectors = nlohmann::json::array();
        for (const auto& trigger : kElementTriggers) {
            selectors.push_back({{"id", trigger.id}, {"pattern", trigger.elementPattern},
                                 {"actionId", static_cast<uint16_t>(trigger.action)},
                                 {"param", trigger.paramName}, {"on", trigger.on}});
        }
        nlohmann::json keys = nlohmann::json::array();
        for (const auto& key : kKeyDefaults) {
            keys.push_back({{"id", key.id}, {"actionId", static_cast<uint16_t>(key.action)},
                            {"key", std::string(magic_enum::enum_name(key.chord.key))},
                            {"mods", static_cast<uint8_t>(key.chord.mods)},
                            {"scope", EnumRecord(key.scope)}, {"state", EnumRecord(key.state)}});
        }
        nlohmann::json transitions = nlohmann::json::array();
        for (const auto& edge : kTransitions) {
            transitions.push_back({{"id", edge.id}, {"from", EnumRecord(edge.from)},
                                   {"to", EnumRecord(edge.to)},
                                   {"guard", std::string(magic_enum::enum_name(edge.guard))}});
        }
        nlohmann::json nouns = nlohmann::json::array();
        for (const auto noun : magic_enum::enum_values<ViewNounId>()) nouns.push_back(EnumRecord(noun));
        nlohmann::json dataTargets = nlohmann::json::array();
        for (const auto& target : kDataTargets) {
            dataTargets.push_back({{"actionId", static_cast<uint16_t>(target.action)},
                                   {"viewNoun", EnumRecord(target.viewNoun)}});
        }
        nlohmann::json states = nlohmann::json::array();
        for (const auto state : kStateIds) states.push_back(EnumRecord(state));
        return nlohmann::json{
            {"schema", {"shapeHash", kAppFlowShapeHash}},
            {"actions", std::move(actions)},
            {"selectors", std::move(selectors)},
            {"chords", std::move(keys)},
            {"transitions", std::move(transitions)},
            {"viewNouns", std::move(nouns)},
            {"dataTargets", std::move(dataTargets)},
            {"flow", {{"states", std::move(states)}, {"initial", EnumRecord(kInitialState)},
                       {"current", EnumRecord(rt_.Current())}}},
            {"undo", {{"depth", rt_.Stack().UndoDepth()}, {"redoDepth", rt_.Stack().RedoDepth()}}},
            {"frame", channelPresentedFrame_}
        }.dump();
    }

    if (method == "appflow.dispatch") {
        const nlohmann::json actionRequestId = request.value("id", nlohmann::json(nullptr));
        const std::string actionKey = RequestKey(actionRequestId);
        if (const auto deferred = channelDeferredDispatchResults_.find(actionKey);
            deferred != channelDeferredDispatchResults_.end()) {
            const auto pixels = channelReadbackResults_.find(actionKey);
            if (pixels == channelReadbackResults_.end()) return std::nullopt;
            const std::string deferredJson = deferred->second;
            const nlohmann::json image = nlohmann::json::parse(pixels->second);
            channelReadbackResults_.erase(pixels);
            channelDeferredDispatchResults_.erase(deferred);
            if (image.contains("error")) throw std::runtime_error(image.at("error").get<std::string>());
            nlohmann::json response = nlohmann::json::parse(deferredJson);
            response["readback"] = image;
            return response.dump();
        }
        if (params.contains("readback")) {
            const auto& visual = params.at("readback");
            if (!visual.is_object() || !visual.contains("selection") || !visual.at("selection").is_object()) {
                throw std::runtime_error("dispatch readback requires a selection object");
            }
        }
        const bool hasId = params.contains("actionId");
        const bool hasSelector = params.contains("selector");
        const bool hasChord = params.contains("chord");
        if (static_cast<int>(hasId) + static_cast<int>(hasSelector) + static_cast<int>(hasChord) != 1) {
            throw std::runtime_error("dispatch requires exactly one of actionId, selector, or chord");
        }
        DispatchResult result = DispatchResult::RejectedByState;
        FlowActionId actionId{};
        if (hasId) {
            actionId = ParseActionId(params.at("actionId"));
            const AppFlowActionDecl* declaration = nullptr;
            for (const auto& candidate : kActionDecls) if (candidate.id == actionId) declaration = &candidate;
            if (!declaration) throw std::runtime_error("actionId is not in the generated catalog");
            const nlohmann::json supplied = params.value("params", nlohmann::json::object());
            if (!supplied.is_object()) throw std::runtime_error("action params must be an object");
            if (supplied.size() != declaration->paramCount) {
                throw std::runtime_error("action params do not match the generated schema");
            }
            AppFlowRuntime::Params typedParams;
            for (uint32_t i = 0; i < declaration->paramCount; ++i) {
                const auto& schema = declaration->params[i];
                if (!supplied.contains(schema.name)) {
                    throw std::runtime_error(std::string("missing generated parameter: ") + schema.name);
                }
                typedParams.emplace_back(schema.name, ParamValueAsString(supplied.at(schema.name), schema.type));
            }
            result = rt_.DispatchById(actionId, typedParams);
        } else if (hasSelector) {
            if (!params.at("selector").is_string()) throw std::runtime_error("selector must be a string");
            result = rt_.DispatchBySelector(params.at("selector").get<std::string>());
        } else {
            const auto& chord = params.at("chord");
            if (!chord.is_object() || !chord.contains("key") || !chord.at("key").is_string()) {
                throw std::runtime_error("chord requires a generated key name");
            }
            const auto key = magic_enum::enum_cast<KeyId>(chord.at("key").get<std::string>());
            if (!key || *key == KeyId::None) throw std::runtime_error("unknown generated chord key");
            KeyMod mods = KeyMod::None;
            if (chord.contains("mods")) {
                if (!chord.at("mods").is_array()) throw std::runtime_error("chord mods must be an array");
                for (const auto& modValue : chord.at("mods")) {
                    if (!modValue.is_string()) throw std::runtime_error("each chord mod must be a string");
                    const auto mod = magic_enum::enum_cast<KeyMod>(modValue.get<std::string>());
                    if (!mod) throw std::runtime_error("unknown generated chord modifier");
                    mods = static_cast<KeyMod>(static_cast<uint8_t>(mods) | static_cast<uint8_t>(*mod));
                }
            }
            result = rt_.DispatchByKey({*key, mods});
        }
        channelActionFrames_[actionKey] = channelPresentedFrame_ + 1;
        RecordAppFlowEvent("dispatch");
        const nlohmann::json response{
            {"dispatchResult", {{"code", static_cast<int>(result)},
                                 {"name", std::string(magic_enum::enum_name(result))}}},
            {"actionId", actionRequestId},
            {"frame", channelPresentedFrame_ + 1},
            {"flowState", EnumRecord(rt_.Current())},
            {"undoDepth", rt_.Stack().UndoDepth()},
            {"redoDepth", rt_.Stack().RedoDepth()}
        };
        if (params.contains("readback")) {
            const auto& visual = params.at("readback");
            channelReadbacks_[actionKey] = PendingReadback{
                visual.dump(), channelPresentedFrame_ + 1, std::chrono::steady_clock::now(), false};
            channelDeferredDispatchResults_[actionKey] = response.dump();
            return std::nullopt;
        }
        return response.dump();
    }

    if (method == "appflow.read") {
        if (params.contains("noun")) {
            ViewNounId noun{};
            const auto& input = params.at("noun");
            if (input.is_string()) {
                const auto parsed = magic_enum::enum_cast<ViewNounId>(input.get<std::string>());
                if (!parsed) throw std::runtime_error("noun name is not generated");
                noun = *parsed;
            } else if (input.is_number_integer()) {
                const auto numeric = input.get<int64_t>();
                bool found = false;
                for (const auto candidate : magic_enum::enum_values<ViewNounId>()) {
                    if (static_cast<uint32_t>(candidate) == numeric) { noun = candidate; found = true; break; }
                }
                if (!found) throw std::runtime_error("noun id is not generated");
            } else {
                throw std::runtime_error("noun must be a generated name or integer id");
            }
            for (const auto& target : kDataTargets) {
                if (target.viewNoun != noun) continue;
                uint32_t value = 0;
                if (!rt_.ReadData(target.action, value)) throw std::runtime_error("view noun is not readable in this editor session");
                return nlohmann::json{{"noun", EnumRecord(noun)}, {"value", value}, {"frame", channelPresentedFrame_}}.dump();
            }
            throw std::runtime_error("generated view noun has no readable Data action in this session");
        }

        nlohmann::json views = nlohmann::json::array();
        for (const auto& target : kDataTargets) {
            uint32_t value = 0;
            if (rt_.ReadData(target.action, value)) {
                views.push_back({{"noun", EnumRecord(target.viewNoun)}, {"value", value}});
            }
        }
        return nlohmann::json{
            {"flowState", EnumRecord(rt_.Current())},
            {"undoDepth", rt_.Stack().UndoDepth()},
            {"redoDepth", rt_.Stack().RedoDepth()},
            {"views", std::move(views)},
            {"frame", channelPresentedFrame_}
        }.dump();
    }

    if (method == "appflow.subscribe") {
        const uint64_t after = params.value("after", uint64_t{0});
        nlohmann::json events = nlohmann::json::array();
        for (const auto& encoded : channelEvents_) {
            const auto event = nlohmann::json::parse(encoded);
            if (event.at("sequence").get<uint64_t>() > after) events.push_back(event);
        }
        return nlohmann::json{{"events", std::move(events)}, {"latestSequence", channelEventSequence_}}.dump();
    }

    if (method == "appflow.await_frame") {
        uint64_t frame = 0;
        if (params.contains("actionId")) {
            const std::string actionKey = RequestKey(params.at("actionId"));
            const auto found = channelActionFrames_.find(actionKey);
            if (found == channelActionFrames_.end()) throw std::runtime_error("unknown actionId; dispatch it first");
            frame = found->second;
        } else if (params.contains("afterFrame") && params.at("afterFrame").is_number_integer()) {
            frame = params.at("afterFrame").get<uint64_t>() + 1;
        } else {
            throw std::runtime_error("await_frame requires actionId or afterFrame");
        }
        if (channelPresentedFrame_ < frame) return std::nullopt;
        return nlohmann::json{{"presented", true}, {"frame", channelPresentedFrame_}, {"targetFrame", frame}}.dump();
    }

    if (method == "appflow.pause") {
        channelPaused_ = true;
        channelStepBudget_ = 0;
        return nlohmann::json{{"paused", true}, {"frame", channelPresentedFrame_}}.dump();
    }
    if (method == "appflow.run") {
        channelPaused_ = false;
        channelStepBudget_ = 0;
        return nlohmann::json{{"paused", false}, {"frame", channelPresentedFrame_}}.dump();
    }
    if (method == "appflow.step") {
        const uint32_t count = params.value("ticks", uint32_t{1});
        if (count == 0 || count > 10000) throw std::runtime_error("ticks must be between 1 and 10000");
        channelPaused_ = true;
        channelStepBudget_ = count;
        return nlohmann::json{{"paused", true}, {"ticks", count},
                              {"targetFrame", channelPresentedFrame_ + count}}.dump();
    }
    if (method == "appflow.readback") {
        const std::string requestKey = RequestKey(request.value("id", nlohmann::json(nullptr)));
        if (const auto ready = channelReadbackResults_.find(requestKey);
            ready != channelReadbackResults_.end()) {
            const std::string response = ready->second;
            channelReadbackResults_.erase(ready);
            const nlohmann::json decoded = nlohmann::json::parse(response);
            if (decoded.contains("error")) throw std::runtime_error(decoded.at("error").get<std::string>());
            return response;
        }
        if (channelReadbacks_.contains(requestKey)) return std::nullopt;
        if (!params.contains("selection") || !params.at("selection").is_object()) {
            throw std::runtime_error("readback requires a typed selection object");
        }
        uint64_t targetFrame = channelPresentedFrame_ + 1;
        if (params.contains("actionId")) {
            const std::string actionKey = RequestKey(params.at("actionId"));
            const auto actionFrame = channelActionFrames_.find(actionKey);
            if (actionFrame == channelActionFrames_.end()) throw std::runtime_error("unknown actionId for readback");
            targetFrame = actionFrame->second;
        } else if (params.contains("frame")) {
            if (!params.at("frame").is_number_integer()) throw std::runtime_error("frame must be an integer");
            targetFrame = params.at("frame").get<uint64_t>();
        }
        if (targetFrame < channelPresentedFrame_) {
            throw std::runtime_error("requested frame has already passed; attach readback to dispatch or request the next frame");
        }
        channelReadbacks_[requestKey] = PendingReadback{
            params.dump(), targetFrame, std::chrono::steady_clock::now(), false};
        return std::nullopt;
    }

    throw std::runtime_error("unknown AppFlow method: " + method);
}
