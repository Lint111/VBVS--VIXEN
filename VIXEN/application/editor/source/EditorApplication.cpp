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
#include "Nodes/InputNode.h"                  // camera input translation reads the live InputState
#include "Nodes/SwapChainNode.h"
#include "Data/Nodes/CameraNodeConfig.h"
#include "Core/RenderGraph.h"
#include "Debug/RenderTargetReadback.h"       // shared IRenderTarget -> PNG readback
#include "KeyMap.h"                           // Inc-4 R5a: GLFW keycode -> typed KeyId
#include "CameraInputTranslation.h"             // R765: raw mouse/axis input -> camera actions
#include "AppFlowBlobFile.h"                   // T1.2: external AppFlow watch/reload
#include "GaiaLayerViewDataProvider.h"        // Inc-B: view->model seam, Gaia-backed provider
#include "VixenHash.h"
#include <Logger.h>
#include <magic_enum.hpp>
#include <nlohmann/json.hpp>
#include <RmlUi/Core.h>
#include <stb_image_write.h>

#include <cstdlib>
#include <charconv>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string_view>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
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
#if UNDERSET_NATIVE_BENCHMARK
    if (!std::getenv("VIXEN_EDITOR_LATENCY_TRACE")) return;
    const auto wallUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    std::fprintf(stderr, "[RECIPE/latency] phase=%s edge=%s tick=%ld action=%d wall_us=%lld\n",
        phase, edge, tick, action, static_cast<long long>(wallUs));
#else
    (void)phase;
    (void)edge;
    (void)tick;
    (void)action;
#endif
}

// Parses "0,45,75,105" into frame numbers. Malformed entries are skipped with a warning (same
// never-abort contract as the rest of the capture controls).
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

// Float twin of ParseParam; a missing, malformed or non-finite value is rejected (nullopt), never defaulted.
std::optional<float> ParseFloatParam(const Vixen::AppFlow::AppFlowRuntime::Params& params, const char* name) {
    for (const auto& [n, v] : params) {
        if (n != name || v.empty()) continue;
        char* end = nullptr;
        const float parsed = std::strtof(v.c_str(), &end);
        if (end != v.c_str() + v.size() || !std::isfinite(parsed)) return std::nullopt;
        return parsed;
    }
    return std::nullopt;
}

// Formats a float for an AppFlow param string with round-trip precision.
std::string FloatParamText(float value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(value));
    return buffer;
}

// Typed-param readers for the E3 layer edits. Unlike ParseParam they report absence/garbage, so a
// malformed dispatch is refused instead of becoming "layer 0" or "0.0".
bool ParseIndexParam(const Vixen::AppFlow::AppFlowRuntime::Params& params, const char* name, uint32_t& out) {
    for (const auto& [n, v] : params) {
        if (n != name) continue;
        const auto parsed = std::from_chars(v.data(), v.data() + v.size(), out);
        return !v.empty() && parsed.ec == std::errc{} && parsed.ptr == v.data() + v.size();
    }
    return false;
}

bool ParseFloatParam(const Vixen::AppFlow::AppFlowRuntime::Params& params, const char* name, float& out) {
    for (const auto& [n, v] : params) {
        if (n != name) continue;
        const auto parsed = std::from_chars(v.data(), v.data() + v.size(), out);
        return !v.empty() && parsed.ec == std::errc{} && parsed.ptr == v.data() + v.size();
    }
    return false;
}

bool ParseTextParam(const Vixen::AppFlow::AppFlowRuntime::Params& params, const char* name, std::string& out) {
    for (const auto& [n, v] : params) {
        if (n != name) continue;
        out = v;
        return true;
    }
    return false;
}

// One stepper click of the E3 layer fields (the same fixed-step shape as the program-field buttons).
constexpr float kBlendStep = 0.25f;
constexpr float kPrimitiveStep = 0.25f;
constexpr float kPositionStep = 0.25f;
constexpr float kRotationStepDegrees = 15.0f;
constexpr float kScaleStep = 0.25f;
constexpr float kMinimumScale = 0.25f;

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

void EditorApplication::DeInitialize() {
    // The readback ring owns Vulkan objects of the device that the base teardown destroys; freeing
    // them afterwards (member destruction) hit an invalid device on every clean channel session.
    appFlowReadback_.reset();
    VulkanGraphApplication::DeInitialize();
}

void EditorApplication::RefreshLayersView() {
    std::vector<Vixen::App::EditorLayerData> layers;
    layers.reserve(doc_.LayerCount());
    for (uint32_t i = 0; i < doc_.LayerCount(); ++i) {
        Vixen::App::EditorLayerData row;
        row.name = doc_.LayerName(i);
        row.op = Vixen::Editor::EditorDocumentModel::OpName(doc_.View().layers[i].header->op);
        row.enabled = ((doc_.EnabledMask() >> i) & 1u) != 0u;
        row.blendRadius = doc_.View().layers[i].header->blendRadius;
        layers.push_back(std::move(row));
    }
    std::vector<Vixen::App::EditorParameterData> parameters;
    parameters.reserve(doc_.ParameterCount());
    for (uint32_t i = 0; i < doc_.ParameterCount(); ++i) {
        parameters.push_back(Vixen::App::EditorParameterData{
            doc_.ParameterName(i), doc_.ParameterUnit(i), doc_.ParameterValue(i),
            doc_.ParameterMin(i), doc_.ParameterMax(i)});
    }
    Vixen::App::EditorPlacementData placement;
    if (doc_.LayerCount() != 0u) {
        selectedLayer_ = std::min(selectedLayer_, doc_.LayerCount() - 1u);
        placement.selectedLayer = static_cast<int>(selectedLayer_);
        placement.selectedName = doc_.LayerName(selectedLayer_);
        Vixen::Editor::LayerTransform transform;
        Vixen::Editor::DocumentDiagnostic placementDiagnostic;
        doc_.GetLayerTransform(selectedLayer_, transform, placementDiagnostic);
        for (size_t axis = 0; axis < 3; ++axis) {
            placement.values[axis] = transform.position[axis];
            placement.values[3 + axis] = transform.rotationDegrees[axis];
            placement.values[6 + axis] = transform.scale[axis];
        }
    }
    // R872.1: the selected layer's combine-op choices and typed primitive fields, read from the generated
    // recipe-op catalogue through the document model.
    Vixen::App::EditorShapeData shape;
    if (doc_.LayerCount() != 0u) {
        const uint8_t currentOp = doc_.View().layers[selectedLayer_].header->op;
        for (const auto& choice : Vixen::SVO::Recipe::SdfLayerOpTable) {
            shape.opChoices.push_back({choice.name, choice.layerOp == currentOp});
        }
        std::vector<Vixen::Editor::PrimitiveFieldInfo> fields;
        Vixen::Editor::DocumentDiagnostic shapeDiagnostic;
        Vixen::SVO::Recipe::SdfOpCode primitive{};
        if (doc_.GetLayerPrimitive(selectedLayer_, primitive, shapeDiagnostic) &&
            doc_.GetLayerPrimitiveFields(selectedLayer_, fields, shapeDiagnostic)) {
            shape.primitive = Vixen::SVO::Recipe::SdfOpCodeName(primitive);
            static constexpr const char* kComponents[4] = {"x", "y", "z", "w"};
            for (const auto& field : fields) {
                for (uint32_t c = 0; c < field.width && c < 4u; ++c) {
                    shape.fields.push_back({std::string(field.name) + "." + kComponents[c],
                                            field.width > 1u ? std::string(field.name) + " " + kComponents[c]
                                                             : std::string(field.name),
                                            field.value[c], field.hasRange, field.minimum, field.maximum});
                }
            }
        } else {
            shape.note = shapeDiagnostic.message;
        }
    }
    Vixen::App::RefreshEditorLayersView(*layersView_, doc_.EnabledMask(), layers, parameters, placement, shape);
    if (!primitivesPublished_) {
        // The picker rows are the catalogue's Primitive rows, sorted by name; the table never changes at run time.
        std::vector<Vixen::App::EditorPrimitiveData> primitives;
        static constexpr const char* kCategories[] = {"Control", "Domain", "Vector", "Primitive",
                                                      "Combine", "Modifier", "Input", "Expression"};
        for (const auto& info : Vixen::SVO::Recipe::SdfOpInfoTable) {
            if (info.category != Vixen::SVO::Recipe::SdfOpCategory::Primitive ||
                !Vixen::Editor::EditorDocumentModel::CanMakePrimitive(static_cast<Vixen::SVO::Recipe::SdfOpCode>(info.opcode))) continue;
            primitives.push_back({info.name, kCategories[static_cast<size_t>(info.category)]});
        }
        std::sort(primitives.begin(), primitives.end(),
                  [](const auto& a, const auto& b) { return a.name < b.name; });
        Vixen::App::RefreshEditorPrimitivesView(*layersView_, primitives);
        primitivesPublished_ = true;
    }
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
        ReportEditorDiagnostic(diagnostic);
        return false;
    }
    if (rt_.Load(nullptr, &layerProvider_) != Vixen::AppFlow::LoadResult::Ok) {
        ReportEditorError("AppFlowRuntime initial load failed");
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
    ClearEditorError();

    // Inc-A2: initial model->view population -- the editor layer view's bound "layers" array now
    // reflects the ACTUAL mask/names/ops of the freshly loaded document (was static markup with 3
    // hand-authored rows; see editor_layers.rml). A reload re-populates from scratch, same as the
    // SetLayerCount resync above.
    RefreshLayersView();

    channelGroupOpen_ = false;
    channelGroupLabel_.clear();
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
            ReportEditorDiagnostic({Vixen::Editor::DocumentDiagnosticCode::InvalidLayerIndex,
                "ToggleLayer: layerIndex " + std::to_string(idx) + " is outside the " +
                std::to_string(doc_.LayerCount()) + " document layers"});
            return;
        }
        const bool wasEnabled = ((doc_.EnabledMask() >> idx) & 1u) != 0u;
        const bool nextEnabled = !wasEnabled;
        rt_.Stack().Dispatch(FlowActionId::ToggleLayer,
                             [this, idx, wasEnabled, nextEnabled](bool forward) {
                Vixen::Editor::DocumentDiagnostic diagnostic;
                if (!doc_.SetLayerEnabled(idx, forward ? nextEnabled : wasEnabled, diagnostic)) {
                    ReportEditorDiagnostic(diagnostic);
                    logger_->Error("[EditorApplication] ToggleLayer operation failed: " +
                                   diagnostic.message);
                    return;
                }
                SyncAfterDocumentMutation();
            });
    });

    // CreateLayer and CreatePrimitiveLayer append one catalogue primitive at its declared defaults
    // (CreateLayer is the fixed Sphere shortcut; there is no hand-built layer body any more).
    const auto createPrimitiveLayer = [this](FlowActionId actionId, Vixen::SVO::Recipe::SdfOpCode primitive) {
        if (doc_.LayerCount() >= Vixen::Editor::EditorDocumentModel::kMaximumLayerCount) {
            ReportEditorDiagnostic({Vixen::Editor::DocumentDiagnosticCode::TooManyLayers,
                std::string(magic_enum::enum_name(actionId)) + ": the document already holds the maximum of " +
                std::to_string(Vixen::Editor::EditorDocumentModel::kMaximumLayerCount) + " layers"});
            return;
        }
        Vixen::Editor::EditableDocumentLayer layer;
        Vixen::Editor::DocumentDiagnostic makeDiagnostic;
        if (!Vixen::Editor::EditorDocumentModel::MakePrimitiveLayer(
                primitive, std::string(Vixen::SVO::Recipe::SdfOpCodeName(primitive)) + " " +
                               std::to_string(doc_.LayerCount() + 1u), layer, makeDiagnostic)) {
            ReportEditorDiagnostic(makeDiagnostic);
            return;
        }
        const uint32_t index = doc_.LayerCount();
        rt_.Stack().Dispatch(actionId, [this, layer, index](bool forward) {
            Vixen::Editor::DocumentDiagnostic diagnostic;
            const bool ok = forward
                ? doc_.InsertLayer(index, layer, diagnostic)
                : doc_.DeleteLayer(index, nullptr, diagnostic);
            if (!ok) {
                ReportEditorDiagnostic(diagnostic);
                logger_->Error("[EditorApplication] CreateLayer operation failed: " + diagnostic.message);
                return;
            }
            SyncAfterDocumentMutation();
        });
    };
    rt_.RegisterHandler(FlowActionId::CreateLayer, [createPrimitiveLayer](const Vixen::AppFlow::AppFlowRuntime::Params&) {
        createPrimitiveLayer(FlowActionId::CreateLayer, Vixen::SVO::Recipe::SdfOpCode::Sphere);
    });
    rt_.RegisterHandler(FlowActionId::CreatePrimitiveLayer, [this, createPrimitiveLayer](const Vixen::AppFlow::AppFlowRuntime::Params& p) {
        std::string name;
        Vixen::SVO::Recipe::SdfOpCode primitive{};
        if (!ParseTextParam(p, "primitive", name) || !Vixen::SVO::Recipe::FindSdfOpCode(name, primitive)) {
            ReportEditorDiagnostic({Vixen::Editor::DocumentDiagnosticCode::InvalidPrimitive,
                "CreatePrimitiveLayer: '" + name + "' is not an opcode name in the recipe op catalogue"});
            return;
        }
        createPrimitiveLayer(FlowActionId::CreatePrimitiveLayer, primitive);
    });

    rt_.RegisterHandler(FlowActionId::DeleteLayer, [this](const Vixen::AppFlow::AppFlowRuntime::Params& p) {
        const uint32_t index = ParseParam(p, "layerIndex");
        if (doc_.LayerCount() <= 1u) {
            ReportEditorDiagnostic({Vixen::Editor::DocumentDiagnosticCode::InvalidLayerIndex,
                "DeleteLayer: a document must retain one rule layer"});
            return;
        }
        Vixen::Editor::EditableDocumentLayer removed;
        Vixen::Editor::DocumentDiagnostic diagnostic;
        if (!doc_.GetLayer(index, removed, diagnostic)) {
            ReportEditorDiagnostic(diagnostic);
            return;
        }
        rt_.Stack().Dispatch(FlowActionId::DeleteLayer, [this, index, removed](bool forward) {
            Vixen::Editor::DocumentDiagnostic opDiagnostic;
            const bool ok = forward
                ? doc_.DeleteLayer(index, nullptr, opDiagnostic)
                : doc_.InsertLayer(index, removed, opDiagnostic);
            if (!ok) {
                ReportEditorDiagnostic(opDiagnostic);
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
                (!up && from + 1u >= doc_.LayerCount())) {
                ReportEditorDiagnostic({Vixen::Editor::DocumentDiagnosticCode::InvalidLayerIndex,
                    "MoveLayer: layer " + std::to_string(from) + " cannot move " + (up ? "up" : "down") +
                    " in a document of " + std::to_string(doc_.LayerCount()) + " layers"});
                return;
            }
            const uint32_t to = up ? from - 1u : from + 1u;
            rt_.Stack().Dispatch(actionId, [this, from, to](bool forward) {
                Vixen::Editor::DocumentDiagnostic diagnostic;
                if (!doc_.MoveLayer(forward ? from : to, forward ? to : from, diagnostic)) {
                    ReportEditorDiagnostic(diagnostic);
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
                ReportEditorDiagnostic(diagnostic);
                return;
            }
            const float after = before + (increase ? 0.25f : -0.25f);
            if (!std::isfinite(after) || after == before) return;
            rt_.Stack().Dispatch(actionId, [this, layerIndex, before, after,
                                            kInstruction, kData0](bool forward) {
                Vixen::Editor::DocumentDiagnostic opDiagnostic;
                if (!doc_.SetLayerProgramField(layerIndex, kInstruction, kData0,
                                               forward ? after : before, opDiagnostic)) {
                    ReportEditorDiagnostic(opDiagnostic);
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
            if (index >= doc_.ParameterCount()) {
                ReportEditorDiagnostic({Vixen::Editor::DocumentDiagnosticCode::InvalidParameterValue,
                    "AdjustParameter: parameterIndex " + std::to_string(index) + " is outside the " +
                    std::to_string(doc_.ParameterCount()) + " document parameters"});
                return;
            }
            const float span = doc_.ParameterMax(index) - doc_.ParameterMin(index);
            const float step = span > 0.0f ? span / 8.0f : 0.1f;
            CommitParameter(actionId, index, doc_.ParameterValue(index) + (increase ? step : -step));
        });
    };
    registerParameterEdit(FlowActionId::AdjustParameterUp, true);
    registerParameterEdit(FlowActionId::AdjustParameterDown, false);

    // Typed entry: the value lands on the same CommitParameter path the +/- buttons use (clamped to the
    // declared range, one ActionStack entry, nothing recorded when the value does not change).
    rt_.RegisterHandler(FlowActionId::SetParameter, [this](const Vixen::AppFlow::AppFlowRuntime::Params& p) {
        const uint32_t index = ParseParam(p, "parameterIndex");
        const auto value = ParseFloatParam(p, "value");
        if (index >= doc_.ParameterCount() || !value) {
            ReportEditorDiagnostic({Vixen::Editor::DocumentDiagnosticCode::InvalidParameterValue,
                "SetParameter: needs a parameterIndex inside the " + std::to_string(doc_.ParameterCount()) +
                " document parameters and a finite numeric value"});
            return;
        }
        CommitParameter(FlowActionId::SetParameter, index, *value);
    });
    rt_.RegisterHandler(FlowActionId::ResetParameter, [this](const Vixen::AppFlow::AppFlowRuntime::Params& p) {
        const uint32_t index = ParseParam(p, "parameterIndex");
        if (index >= doc_.ParameterCount()) return;
        CommitParameter(FlowActionId::ResetParameter, index, doc_.ParameterDefault(index));
    });
    // Pressing a parameter's value label starts a drag-scrub; StepParameterScrub (Update) turns the held
    // drag into SetParameter dispatches inside one undo group.
    rt_.RegisterHandler(FlowActionId::ScrubParameter, [this](const Vixen::AppFlow::AppFlowRuntime::Params& p) {
        const uint32_t index = ParseParam(p, "parameterIndex");
        if (index >= doc_.ParameterCount() || scrub_.active) return;
        scrub_ = ParameterScrub{true, index, doc_.ParameterValue(index), -1.0f, false};
    });

    // Camera actions are not document edits: no ActionStack entry, the node's live orbit state changes.
    rt_.RegisterHandler(FlowActionId::OrbitCamera, [this](const Vixen::AppFlow::AppFlowRuntime::Params& p) {
        const auto dYaw = ParseFloatParam(p, "dYaw");
        const auto dPitch = ParseFloatParam(p, "dPitch");
        if (auto* camera = EditorCamera(); camera && dYaw && dPitch) camera->OrbitBy(*dYaw, *dPitch);
    });
    rt_.RegisterHandler(FlowActionId::PanCamera, [this](const Vixen::AppFlow::AppFlowRuntime::Params& p) {
        const auto dx = ParseFloatParam(p, "dx");
        const auto dy = ParseFloatParam(p, "dy");
        if (auto* camera = EditorCamera(); camera && dx && dy) camera->PanBy(*dx, *dy);
    });
    rt_.RegisterHandler(FlowActionId::ZoomCamera, [this](const Vixen::AppFlow::AppFlowRuntime::Params& p) {
        const auto delta = ParseFloatParam(p, "delta");
        if (auto* camera = EditorCamera(); camera && delta) camera->ZoomBy(*delta);
    });
    rt_.RegisterHandler(FlowActionId::FrameAsset, [this](const Vixen::AppFlow::AppFlowRuntime::Params&) {
        ApplyCameraFrame();
    });
    rt_.RegisterHandler(FlowActionId::SetViewAngle, [this](const Vixen::AppFlow::AppFlowRuntime::Params& p) {
        SetViewAngle(ParseParam(p, "index"));
    });
    rt_.RegisterHandler(FlowActionId::SelectViewAngle0, [this](const Vixen::AppFlow::AppFlowRuntime::Params&) { SetViewAngle(0); });
    rt_.RegisterHandler(FlowActionId::SelectViewAngle1, [this](const Vixen::AppFlow::AppFlowRuntime::Params&) { SetViewAngle(1); });
    rt_.RegisterHandler(FlowActionId::SelectViewAngle2, [this](const Vixen::AppFlow::AppFlowRuntime::Params&) { SetViewAngle(2); });
    rt_.RegisterHandler(FlowActionId::SelectViewAngle3, [this](const Vixen::AppFlow::AppFlowRuntime::Params&) { SetViewAngle(3); });

    // E3: typed layer edits. Each accepted edit is one ActionStack entry whose inverse restores the exact
    // prior value (the prior program for a placement, so even a hand-authored Transform comes back
    // byte-for-byte). A refused value records nothing.
    using EditFn = std::function<bool(Vixen::Editor::DocumentDiagnostic&)>;
    const auto recordEdit = [this](FlowActionId actionId, const char* label, EditFn forward, EditFn inverse) {
        rt_.Stack().Dispatch(actionId, [this, label, forward, inverse](bool isForward) {
            Vixen::Editor::DocumentDiagnostic opDiagnostic;
            if (!(isForward ? forward : inverse)(opDiagnostic)) {
                ReportEditorDiagnostic(opDiagnostic);
                logger_->Error(std::string("[EditorApplication] ") + label + " operation failed: " + opDiagnostic.message);
                return;
            }
            SyncAfterDocumentMutation();
        });
    };
    const auto refuse = [this](const char* label, const std::string& why) {
        ReportEditorError(std::string(label) + ": " + why, "RefusedEdit");
    };
    const auto setBlend = [this, recordEdit, refuse](FlowActionId actionId, uint32_t index, float radius) {
        Vixen::Editor::DocumentDiagnostic diagnostic;
        if (index >= doc_.LayerCount()) { refuse("SetLayerBlend", "layer index " + std::to_string(index) + " is outside the document"); return; }
        if (!Vixen::Editor::EditorDocumentModel::ValidateBlendRadius(radius, diagnostic)) { refuse("SetLayerBlend", diagnostic.message); return; }
        const float before = doc_.View().layers[index].header->blendRadius;
        if (before == radius) return;
        recordEdit(actionId, "SetLayerBlend",
                   [this, index, radius](Vixen::Editor::DocumentDiagnostic& d) { return doc_.SetLayerBlendRadius(index, radius, d); },
                   [this, index, before](Vixen::Editor::DocumentDiagnostic& d) { return doc_.SetLayerBlendRadius(index, before, d); });
        lastEditorError_.clear();
    };
    const auto setTransform = [this, recordEdit, refuse](FlowActionId actionId, uint32_t index,
                                                         const Vixen::Editor::LayerTransform& next) {
        Vixen::Editor::DocumentDiagnostic diagnostic;
        Vixen::Editor::EditableDocumentLayer layerBefore;
        if (!doc_.GetLayer(index, layerBefore, diagnostic)) { refuse("SetLayerTransform", diagnostic.message); return; }
        if (!Vixen::Editor::EditorDocumentModel::ValidateLayerTransform(next, diagnostic)) { refuse("SetLayerTransform", diagnostic.message); return; }
        Vixen::Editor::LayerTransform current;
        doc_.GetLayerTransform(index, current, diagnostic);
        if (current.position == next.position && current.rotationDegrees == next.rotationDegrees &&
            current.scale == next.scale) return;
        recordEdit(actionId, "SetLayerTransform",
                   [this, index, next](Vixen::Editor::DocumentDiagnostic& d) { return doc_.SetLayerTransform(index, next, d); },
                   [this, index, program = std::move(layerBefore.program)](Vixen::Editor::DocumentDiagnostic& d) {
                       return doc_.SetLayerProgram(index, program, d); });
        lastEditorError_.clear();
    };

    // R872.1: one catalogue argument of the layer's primitive. The inverse is the exact program snapshot.
    const auto setPrimitiveField = [this, recordEdit, refuse](FlowActionId actionId, uint32_t index,
                                                              const std::string& field,
                                                              const std::array<float, 4>& value) {
        Vixen::Editor::DocumentDiagnostic diagnostic;
        Vixen::Editor::EditableDocumentLayer layerBefore;
        if (!doc_.GetLayer(index, layerBefore, diagnostic)) { refuse("SetLayerPrimitiveField", diagnostic.message); return; }
        std::array<float, 4> current{};
        if (!doc_.GetLayerPrimitiveField(index, field, current, diagnostic)) { refuse("SetLayerPrimitiveField", diagnostic.message); return; }
        // Apply the model's own value rules first so a refused value records nothing.
        std::vector<Vixen::Editor::PrimitiveFieldInfo> fields;
        doc_.GetLayerPrimitiveFields(index, fields, diagnostic);
        for (const auto& candidate : fields) {
            if (field == candidate.name &&
                !Vixen::Editor::EditorDocumentModel::ValidatePrimitiveField(candidate, value, diagnostic)) {
                refuse("SetLayerPrimitiveField", diagnostic.message);
                return;
            }
        }
        if (current == value) return;
        recordEdit(actionId, "SetLayerPrimitiveField",
                   [this, index, field, value](Vixen::Editor::DocumentDiagnostic& d) { return doc_.SetLayerPrimitiveField(index, field, value, d); },
                   [this, index, program = std::move(layerBefore.program)](Vixen::Editor::DocumentDiagnostic& d) {
                       return doc_.SetLayerProgram(index, program, d); });
        lastEditorError_.clear();
    };
    rt_.RegisterHandler(FlowActionId::SetLayerBlend, [this, setBlend, refuse](const Vixen::AppFlow::AppFlowRuntime::Params& p) {
        uint32_t index = 0;
        float radius = 0.0f;
        if (!ParseIndexParam(p, "layerIndex", index) || !ParseFloatParam(p, "radius", radius)) {
            refuse("SetLayerBlend", "needs an integer layerIndex and a numeric radius");
            return;
        }
        setBlend(FlowActionId::SetLayerBlend, index, radius);
    });

    rt_.RegisterHandler(FlowActionId::RenameLayer, [this, recordEdit, refuse](const Vixen::AppFlow::AppFlowRuntime::Params& p) {
        uint32_t index = 0;
        std::string name;
        if (!ParseIndexParam(p, "layerIndex", index) || !ParseTextParam(p, "name", name)) {
            refuse("RenameLayer", "needs an integer layerIndex and a name");
            return;
        }
        Vixen::Editor::DocumentDiagnostic diagnostic;
        if (index >= doc_.LayerCount()) { refuse("RenameLayer", "layer index " + std::to_string(index) + " is outside the document"); return; }
        if (!Vixen::Editor::EditorDocumentModel::ValidateLayerName(name, diagnostic)) { refuse("RenameLayer", diagnostic.message); return; }
        const std::string before = doc_.LayerName(index);
        if (before == name) return;
        recordEdit(FlowActionId::RenameLayer, "RenameLayer",
                   [this, index, name](Vixen::Editor::DocumentDiagnostic& d) { return doc_.SetLayerName(index, name, d); },
                   [this, index, before](Vixen::Editor::DocumentDiagnostic& d) { return doc_.SetLayerName(index, before, d); });
        lastEditorError_.clear();
    });

    rt_.RegisterHandler(FlowActionId::SetLayerTransform, [this, setTransform, refuse](const Vixen::AppFlow::AppFlowRuntime::Params& p) {
        static constexpr const char* kFields[9] = {"px", "py", "pz", "rx", "ry", "rz", "sx", "sy", "sz"};
        uint32_t index = 0;
        if (!ParseIndexParam(p, "layerIndex", index)) {
            refuse("SetLayerTransform", "needs an integer layerIndex");
            return;
        }
        // A component the call does not name keeps the layer's current value, so one typed field can set one
        // component; a named component must be a number.
        Vixen::Editor::DocumentDiagnostic diagnostic;
        Vixen::Editor::LayerTransform next;
        if (!doc_.GetLayerTransform(index, next, diagnostic)) { refuse("SetLayerTransform", diagnostic.message); return; }
        std::array<float, 9> values{next.position[0], next.position[1], next.position[2],
                                    next.rotationDegrees[0], next.rotationDegrees[1], next.rotationDegrees[2],
                                    next.scale[0], next.scale[1], next.scale[2]};
        for (size_t f = 0; f < values.size(); ++f) {
            bool named = false;
            for (const auto& [n, v] : p) if (n == kFields[f]) named = true;
            if (named && !ParseFloatParam(p, kFields[f], values[f])) {
                refuse("SetLayerTransform", std::string(kFields[f]) + " is not a finite number");
                return;
            }
        }
        next.position = {values[0], values[1], values[2]};
        next.rotationDegrees = {values[3], values[4], values[5]};
        next.scale = {values[6], values[7], values[8]};
        setTransform(FlowActionId::SetLayerTransform, index, next);
    });

    rt_.RegisterHandler(FlowActionId::NudgeLayerField, [this, setBlend, setTransform, setPrimitiveField, refuse](const Vixen::AppFlow::AppFlowRuntime::Params& p) {
        uint32_t index = 0;
        std::string field;
        if (!ParseIndexParam(p, "layerIndex", index) || !ParseTextParam(p, "field", field)) {
            refuse("NudgeLayerField", "needs an integer layerIndex and a field");
            return;
        }
        const size_t dash = field.rfind('-');
        const std::string key = dash == std::string::npos ? field : field.substr(0, dash);
        const std::string direction = dash == std::string::npos ? std::string{} : field.substr(dash + 1);
        if (direction != "up" && direction != "down") {
            refuse("NudgeLayerField", "field '" + field + "' must end in -up or -down");
            return;
        }
        const float sign = direction == "up" ? 1.0f : -1.0f;
        if (key == "blend") {
            if (index >= doc_.LayerCount()) { refuse("NudgeLayerField", "layer index " + std::to_string(index) + " is outside the document"); return; }
            const float before = doc_.View().layers[index].header->blendRadius;
            setBlend(FlowActionId::NudgeLayerField, index, std::max(0.0f, before + sign * kBlendStep));
            return;
        }
        // "<argument>.<component>": one component of the layer's catalogue primitive argument.
        if (const size_t dot = key.find('.'); dot != std::string::npos) {
            const std::string argument = key.substr(0, dot);
            const std::string component = key.substr(dot + 1);
            static constexpr const char* kComponentKeys[4] = {"x", "y", "z", "w"};
            size_t slotComponent = 4;
            for (size_t c = 0; c < 4; ++c) if (component == kComponentKeys[c]) slotComponent = c;
            Vixen::Editor::DocumentDiagnostic diagnostic;
            std::vector<Vixen::Editor::PrimitiveFieldInfo> fields;
            if (!doc_.GetLayerPrimitiveFields(index, fields, diagnostic)) { refuse("NudgeLayerField", diagnostic.message); return; }
            for (const auto& candidate : fields) {
                if (argument != candidate.name) continue;
                if (slotComponent >= candidate.width) { refuse("NudgeLayerField", "field '" + key + "' has no such component"); return; }
                std::array<float, 4> next = candidate.value;
                float stepped = next[slotComponent] + sign * kPrimitiveStep;
                if (candidate.hasRange) stepped = std::clamp(stepped, candidate.minimum, candidate.maximum);
                if (stepped == next[slotComponent]) return;
                next[slotComponent] = stepped;
                setPrimitiveField(FlowActionId::NudgeLayerField, index, argument, next);
                return;
            }
            refuse("NudgeLayerField", "the layer's primitive has no field '" + argument + "'");
            return;
        }
        static constexpr const char* kKeys[9] = {"px", "py", "pz", "rx", "ry", "rz", "sx", "sy", "sz"};
        size_t slot = 9;
        for (size_t f = 0; f < 9; ++f) if (key == kKeys[f]) slot = f;
        if (slot == 9) { refuse("NudgeLayerField", "unknown field '" + key + "'"); return; }
        Vixen::Editor::DocumentDiagnostic diagnostic;
        Vixen::Editor::LayerTransform next;
        if (!doc_.GetLayerTransform(index, next, diagnostic)) { refuse("NudgeLayerField", diagnostic.message); return; }
        const size_t axis = slot % 3u;
        if (slot < 3u) next.position[axis] += sign * kPositionStep;
        else if (slot < 6u) next.rotationDegrees[axis] += sign * kRotationStepDegrees;
        else next.scale[axis] = std::max(kMinimumScale, next.scale[axis] + sign * kScaleStep);
        setTransform(FlowActionId::NudgeLayerField, index, next);
    });

    // R872.1: the layer's combine op by its generated layer-op table name. The inverse restores the whole
    // program and op through the exact snapshot of the layer.
    rt_.RegisterHandler(FlowActionId::SetLayerOp, [this, recordEdit, refuse](const Vixen::AppFlow::AppFlowRuntime::Params& p) {
        uint32_t index = 0;
        std::string name;
        if (!ParseIndexParam(p, "layerIndex", index) || !ParseTextParam(p, "op", name)) {
            refuse("SetLayerOp", "needs an integer layerIndex and an op name");
            return;
        }
        if (index >= doc_.LayerCount()) { refuse("SetLayerOp", "layer index " + std::to_string(index) + " is outside the document"); return; }
        const Vixen::SVO::Recipe::SdfLayerOp* entry = nullptr;
        for (const auto& candidate : Vixen::SVO::Recipe::SdfLayerOpTable) {
            // The table names are canonical ("Subtract"); input is matched ignoring case.
            const std::string_view known = candidate.name;
            if (known.size() == name.size() &&
                std::equal(known.begin(), known.end(), name.begin(), [](char a, char b) {
                    return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); })) {
                entry = &candidate;
            }
        }
        if (entry == nullptr) { refuse("SetLayerOp", "'" + name + "' is not in the layer-op table"); return; }
        const uint8_t before = doc_.View().layers[index].header->op;
        const uint8_t after = entry->layerOp;
        if (before == after) return;
        recordEdit(FlowActionId::SetLayerOp, "SetLayerOp",
                   [this, index, after](Vixen::Editor::DocumentDiagnostic& d) { return doc_.SetLayerOp(index, after, d); },
                   [this, index, before](Vixen::Editor::DocumentDiagnostic& d) { return doc_.SetLayerOp(index, before, d); });
        lastEditorError_.clear();
    });

    rt_.RegisterHandler(FlowActionId::SetLayerPrimitiveField, [this, setPrimitiveField, refuse](const Vixen::AppFlow::AppFlowRuntime::Params& p) {
        uint32_t index = 0;
        std::string field;
        if (!ParseIndexParam(p, "layerIndex", index) || !ParseTextParam(p, "field", field)) {
            refuse("SetLayerPrimitiveField", "needs an integer layerIndex and a field");
            return;
        }
        std::array<float, 4> value{};
        std::array<bool, 4> present{};
        static constexpr const char* kComponents[4] = {"x", "y", "z", "w"};
        for (size_t c = 0; c < 4; ++c) {
            bool hasParam = false;
            for (const auto& [n, v] : p) if (n == kComponents[c]) hasParam = true;
            if (!hasParam) continue;
            if (!ParseFloatParam(p, kComponents[c], value[c])) {
                refuse("SetLayerPrimitiveField", std::string("component ") + kComponents[c] + " is not a number");
                return;
            }
            present[c] = true;
        }
        std::vector<Vixen::Editor::PrimitiveFieldInfo> fields;
        Vixen::Editor::DocumentDiagnostic diagnostic;
        if (!doc_.GetLayerPrimitiveFields(index, fields, diagnostic)) { refuse("SetLayerPrimitiveField", diagnostic.message); return; }
        for (const auto& candidate : fields) {
            if (field != candidate.name) continue;
            // A component the call does not name keeps the field's current value, so one typed field can set
            // one component of a vector argument.
            for (uint32_t c = 0; c < candidate.width && c < 4u; ++c) {
                if (!present[c]) value[c] = candidate.value[c];
            }
        }
        setPrimitiveField(FlowActionId::SetLayerPrimitiveField, index, field, value);
    });

    rt_.RegisterHandler(FlowActionId::SelectLayer, [this, refuse](const Vixen::AppFlow::AppFlowRuntime::Params& p) {
        uint32_t index = 0;
        if (!ParseIndexParam(p, "layerIndex", index) || index >= doc_.LayerCount()) {
            refuse("SelectLayer", "needs a layerIndex inside the document");
            return;
        }
        selectedLayer_ = index;
        RefreshLayersView();
    });

    // The runtime reload (fresh undo history) must not happen inside the runtime's own dispatch, so the
    // request is applied by Update(), as OpenRecipe's is.
    rt_.RegisterHandler(FlowActionId::ReopenSaved, [this, refuse](const Vixen::AppFlow::AppFlowRuntime::Params&) {
        if (lastSavedPath_.empty()) { refuse("ReopenSaved", "requires a successful save first"); return; }
        pendingReopenSaved_ = true;
    });

    // R765: open a pack recipe by content id. Opening replaces the document and reloads the AppFlow
    // runtime (fresh undo history), which must not happen inside the runtime's own dispatch, so the
    // request is validated here and applied by Update().
    rt_.RegisterHandler(FlowActionId::OpenRecipe, [this](const Vixen::AppFlow::AppFlowRuntime::Params& p) {
        std::string recipeId;
        for (const auto& [name, value] : p) if (name == "recipeId") recipeId = value;
        pack_.Refresh();
        std::string path;
        if (!pack_.TryResolve(recipeId, path)) {
            ReportEditorError("OpenRecipe: the open pack has no recipe '" + recipeId + "'");
            logger_->Warning("[EditorApplication] " + lastEditorError_);
            return;
        }
        pendingOpenRecipe_ = recipeId;
    });

    rt_.RegisterHandler(FlowActionId::Undo, [this](const Vixen::AppFlow::AppFlowRuntime::Params&) {
        if (channelGroupOpen_) {
            ReportEditorError("Undo: refused while the undo group '" + channelGroupLabel_ + "' is open", "GroupOpen");
            return;
        }
        rt_.Stack().Undo();
    });
    rt_.RegisterHandler(FlowActionId::Redo, [this](const Vixen::AppFlow::AppFlowRuntime::Params&) {
        if (channelGroupOpen_) {
            ReportEditorError("Redo: refused while the undo group '" + channelGroupLabel_ + "' is open", "GroupOpen");
            return;
        }
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

    // R766: panel layout verbs. They change the dock, not the document: no ActionStack entry.
    using Params = Vixen::AppFlow::AppFlowRuntime::Params;
    using Vixen::RenderGraph::DockResult;
    const auto param = [](const Params& p, std::string_view name) {
        for (const auto& [key, value] : p) if (key == name) return value;
        return std::string{};
    };
    const auto intParam = [param](const Params& p, std::string_view name, int& out) {
        const std::string text = param(p, name);
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), out);
        return !text.empty() && parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
    };
    rt_.RegisterHandler(FlowActionId::OpenPanel, [this, param](const Params& p) {
        AfterPanelVerb("open", Panels().Dock().Open(param(p, "panelId")));
    });
    rt_.RegisterHandler(FlowActionId::ClosePanel, [this, param](const Params& p) {
        AfterPanelVerb("close", Panels().Dock().Close(param(p, "panelId")));
    });
    rt_.RegisterHandler(FlowActionId::FocusPanel, [this, param](const Params& p) {
        AfterPanelVerb("focus", Panels().Dock().Focus(param(p, "panelId")));
    });
    rt_.RegisterHandler(FlowActionId::MovePanel, [this, param](const Params& p) {
        const auto region = Vixen::RenderGraph::ParseDockRegion(param(p, "region"));
        AfterPanelVerb("move", region ? Panels().Dock().Move(param(p, "panelId"), *region) : DockResult::UnknownRegion);
    });
    rt_.RegisterHandler(FlowActionId::SplitPanel, [this, param](const Params& p) {
        const auto region = Vixen::RenderGraph::ParseDockRegion(param(p, "region"));
        AfterPanelVerb("split", region ? Panels().Dock().Split(param(p, "panelId"), *region) : DockResult::UnknownRegion);
    });
    rt_.RegisterHandler(FlowActionId::FloatPanel, [this, param, intParam](const Params& p) {
        int x = 0, y = 0, w = 0, h = 0;
        if (!intParam(p, "x", x) || !intParam(p, "y", y) || !intParam(p, "width", w) || !intParam(p, "height", h)) {
            AfterPanelVerb("float", DockResult::InvalidSize);
            return;
        }
        AfterPanelVerb("float", Panels().Dock().Float(param(p, "panelId"),
            {static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h)}));
    });
    rt_.RegisterHandler(FlowActionId::ResizeRegion, [this, param, intParam](const Params& p) {
        const auto region = Vixen::RenderGraph::ParseDockRegion(param(p, "region"));
        int size = 0;
        if (!region || !intParam(p, "size", size)) {
            AfterPanelVerb("resize", region ? DockResult::InvalidSize : DockResult::UnknownRegion);
            return;
        }
        AfterPanelVerb("resize", Panels().Dock().Resize(*region, static_cast<float>(size)));
    });
    rt_.RegisterHandler(FlowActionId::ResetLayout, [this](const Params&) {
        Panels().Dock().ResetToDefaults();
        AfterPanelVerb("reset", DockResult::Ok);
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
                ReportEditorError("RegisterProceduralRecipe failed (code " +
                    std::to_string(static_cast<int>(result)) + ")");
                logger_->Error("[EditorApplication] " + lastEditorError_);
            }
        } else {
            ReportEditorDiagnostic(diagnostic);
            logger_->Error("[EditorApplication] BuildRenderGraph could not flatten the preview: " + lastEditorError_);
        }
    }

    // Build the full standard graph (window, body-octree scene, UI composite node with this app's
    // declared panels), then replace the 3 default demo bodies with the loaded VoxelDocument's single
    // flattened recipe.
    //
    // No capture-specific node is needed. The script harness reads the existing compute target
    // after Render(), before the HUD composite overlays the viewport. The readback only runs on
    // requested capture ticks.
    VulkanGraphApplication::BuildRenderGraph();

    // R766: the editor's panels (DeclarePanels) were mounted on the UI node by the base graph build's
    // panel dock; there is no hard-wired editor document to re-point any more.

    if (auto* cameraInst = GetRenderGraph() ? GetRenderGraph()->GetInstanceByName("raymarch_camera") : nullptr) {
        using CC = Vixen::RenderGraph::CameraNodeConfig;
        // Camera control goes through the AppFlow camera actions (TranslateCameraInput), not InputState.
        cameraInst->SetParameter(CC::PARAM_INPUT_DRIVEN, false);
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

Vixen::RenderGraph::CameraNode* EditorApplication::EditorCamera() {
    auto* graph = GetRenderGraph();
    return graph ? static_cast<Vixen::RenderGraph::CameraNode*>(graph->GetInstanceByName("raymarch_camera"))
                 : nullptr;
}

void EditorApplication::CommitParameter(Vixen::AppFlow::Generated::FlowActionId actionId, uint32_t index,
                                        float requested) {
    if (index >= doc_.ParameterCount() || !std::isfinite(requested)) return;
    const float before = doc_.ParameterValue(index);
    const float after = std::clamp(requested, doc_.ParameterMin(index), doc_.ParameterMax(index));
    if (after == before) return;
    rt_.Stack().Dispatch(actionId, [this, index, before, after](bool forward) {
        Vixen::Editor::DocumentDiagnostic diagnostic;
        if (!doc_.SetParameterValue(index, forward ? after : before, diagnostic)) {
            ReportEditorDiagnostic(diagnostic);
            logger_->Error("[EditorApplication] SetParameter operation failed: " + diagnostic.message);
            return;
        }
        SyncAfterDocumentMutation();
    });
}

void EditorApplication::ApplyCameraFrame() {
    if (!hasFrame_) return;
    if (auto* camera = EditorCamera()) camera->SetOrbit(framedCenter_, framedDistance_);
}

void EditorApplication::SetViewAngle(uint32_t index) {
    if (index >= Vixen::Editor::kEditorViewAngleYaws.size()) {
        logger_->Warning("[EditorApplication] SetViewAngle ignored: index " + std::to_string(index) +
                         " is outside the " + std::to_string(Vixen::Editor::kEditorViewAngleYaws.size()) +
                         " look-dev angles");
        return;
    }
    if (auto* camera = EditorCamera()) camera->SetOrbitYaw(Vixen::Editor::kEditorViewAngleYaws[index]);
}

void EditorApplication::TranslateCameraInput() {
    auto* graph = GetRenderGraph();
    auto* input = graph ? static_cast<Vixen::RenderGraph::InputNode*>(graph->GetInstanceByName("input_handler"))
                        : nullptr;
    auto* camera = EditorCamera();
    if (!input || !camera) return;
    using Vixen::AppFlow::Generated::FlowActionId;
    const bool ctrl = input->IsRawKeyHeld(GLFW_KEY_LEFT_CONTROL) || input->IsRawKeyHeld(GLFW_KEY_RIGHT_CONTROL);
    const auto* ui = GetUiRenderNode();
    const Vixen::Editor::UiInputCapture capture{ui && ui->WantsKeyboard(), ui && ui->WantsPointer()};
    for (const auto& dispatch : Vixen::Editor::CameraDispatchesFromInput(
             input->GetInputState(), ctrl, camera->GetMouseSensitivity(), camera->GetMoveSpeed(), capture)) {
        switch (dispatch.action) {
            case FlowActionId::OrbitCamera:
                rt_.DispatchById(dispatch.action, {{"dYaw", FloatParamText(dispatch.a)}, {"dPitch", FloatParamText(dispatch.b)}});
                break;
            case FlowActionId::PanCamera:
                rt_.DispatchById(dispatch.action, {{"dx", FloatParamText(dispatch.a)}, {"dy", FloatParamText(dispatch.b)}});
                break;
            default:
                rt_.DispatchById(dispatch.action, {{"delta", FloatParamText(dispatch.a)}});
                break;
        }
    }
}

void EditorApplication::StepParameterScrub() {
    if (!scrub_.active) return;
    auto* graph = GetRenderGraph();
    auto* input = graph ? static_cast<Vixen::RenderGraph::InputNode*>(graph->GetInstanceByName("input_handler"))
                        : nullptr;
    const Vixen::RenderGraph::InputState* state = input ? &input->GetInputState() : nullptr;
    const bool held = state && state->mouseButtons[0];
    if (held && scrub_.index < doc_.ParameterCount()) {
        if (scrub_.startCursorX < 0.0f) scrub_.startCursorX = state->mousePosition.x;
        // One full declared range per kScrubPixelsPerRange pixels of horizontal drag.
        constexpr float kScrubPixelsPerRange = 200.0f;
        const float span = doc_.ParameterMax(scrub_.index) - doc_.ParameterMin(scrub_.index);
        const float target = scrub_.startValue +
                             (state->mousePosition.x - scrub_.startCursorX) * (span / kScrubPixelsPerRange);
        const float clamped = std::clamp(target, doc_.ParameterMin(scrub_.index), doc_.ParameterMax(scrub_.index));
        if (clamped != doc_.ParameterValue(scrub_.index)) {
            if (!scrub_.grouped) {  // the group opens on the first real change so a plain click leaves no empty undo step
                rt_.Stack().BeginGroup(nextUndoGroup_++);
                scrub_.grouped = true;
            }
            rt_.DispatchById(Vixen::AppFlow::Generated::FlowActionId::SetParameter,
                             {{"parameterIndex", std::to_string(scrub_.index)}, {"value", FloatParamText(clamped)}});
        }
        return;
    }
    if (scrub_.grouped) rt_.Stack().EndGroup();
    scrub_ = ParameterScrub{};
}

bool EditorApplication::ApplyDocumentToScene(bool reframeCamera) {
    TraceEditorLatency("flatten", "begin", updateTick_);
    Vixen::SVO::RecipeRegistry::RecipeEntry entry;
    Vixen::Editor::DocumentDiagnostic diagnostic;
    const Vixen::Editor::DocumentBakeSnapshot snapshot = doc_.CaptureBakeSnapshot();
    if (!doc_.FlattenToRecipeEntry(entry, snapshot, diagnostic)) {
        ReportEditorDiagnostic(diagnostic);
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
            ReportEditorError("RegisterProceduralRecipe failed (code " +
                std::to_string(static_cast<int>(result)) + ")");
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
            ReportEditorError("ReplaceProceduralRecipe failed (code " +
                std::to_string(static_cast<int>(result)) + ")");
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
        ReportEditorError("RecipeRegistry::Register failed (code " +
                          std::to_string(static_cast<int>(result)) + ")");
        return false;
    }

    Vixen::SVO::RecipeBakeConfig bakeCfg{};  // defaults: n=64, band=2.5, depth=3
    auto bakeRegistry = std::make_shared<Vixen::SVO::RecipeRegistry>(std::move(reg));
    TraceEditorLatency("publish", "begin", updateTick_);
    lastBakeSnapshot_ = snapshot;
    for (size_t i = 0; i < lastPreviewParameterValues_.size(); ++i) {
        lastPreviewParameterValues_[i] = snapshot.parameterValues[i];
    }

    SetRecipePoolProducer([bakeRegistry, bakeCfg]() mutable {
        auto bakeResult = Vixen::SVO::BakeRegistryToPool(*bakeRegistry, bakeCfg);
        if (!bakeResult.ok) {
            throw std::runtime_error("BakeRegistryToPool failed: " + bakeResult.err);
        }
        return std::move(bakeResult.pool);
    });
    TraceEditorLatency("bake", "end", updateTick_);

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
        ReportEditorError("CPU bake and virtual preview parameter snapshots diverged");
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
            framedCenter_ = frame.center;
            framedDistance_ = frame.distance;
            hasFrame_ = true;
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
    if (pendingCapture_) {
        if (pendingCapturePath_ != path) {
            err = "CaptureFrameToPng: another capture is pending";
            return false;
        }
        if (!pendingCapture_->Poll(err)) return false;
        pendingCapture_.reset();
        pendingCapturePath_.clear();
        return err.empty();
    }
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
    pendingCapture_ = Vixen::RenderGraph::Debug::BeginCaptureRenderTargetToPng(
        device, renderTarget, device->queue, device->graphicsQueueIndex, path, err);
    if (!pendingCapture_) return false;
    pendingCapturePath_ = path;
    err.clear();
    return true;
}

bool EditorApplication::SaveDocument() {
    const auto snapshot = doc_.CaptureBakeSnapshot();
    Vixen::SVO::RecipeRegistry::RecipeEntry canonical;
    Vixen::Editor::DocumentDiagnostic diagnostic;
    if (!doc_.FlattenToRecipeEntry(canonical, snapshot, diagnostic)) {
        ReportEditorDiagnostic(diagnostic);
        return false;
    }

    std::string outPath;
    if (!openRecipeId_.empty()) {
        // R765: a pack recipe saves its layered document back into the pack's authored form; the pack
        // build flattens it. No side file and no flattened .vrc is written.
        if (!pack_.TryResolve(openRecipeId_, outPath)) {
            ReportEditorError("the open pack no longer lists recipe '" + openRecipeId_ + "'");
            return false;
        }
        std::vector<uint8_t> bytes;
        if (!doc_.Serialize(bytes, diagnostic)) {
            ReportEditorDiagnostic(diagnostic);
            return false;
        }
        std::string writeError;
        if (!Vixen::Editor::PackRecipeSource::WriteAtomically(outPath, bytes, writeError)) {
            ReportEditorError(writeError);
            return false;
        }
    } else {
        const size_t dot = documentPath_.find_last_of('.');
        const std::string base = (dot == std::string::npos) ? documentPath_ : documentPath_.substr(0, dot);
        outPath = base + ".edited.vxd";
        if (!doc_.Save(outPath, diagnostic)) {
            ReportEditorDiagnostic(diagnostic);
            return false;
        }
    }
    lastSavedPath_ = outPath;
    lastSavedProgram_ = std::move(canonical.bytecode);
    lastSavedParameters_ = snapshot.parameterValues;
    lastSavedMask_ = doc_.EnabledMask();
    ClearEditorError();
    logger_->Info("[EditorApplication] Saved document to " + outPath);
    std::ostringstream state;
    state << "[EDITOR/state] save revision=" << snapshot.revision << " mask=" << lastSavedMask_
          << " parameterCount=" << doc_.ParameterCount();
    if (!openRecipeId_.empty()) state << " recipe=" << openRecipeId_ << " path=" << outPath;
    logger_->Info(state.str());
    return true;
}

bool EditorApplication::ReopenSavedDocument() {
    if (lastSavedPath_.empty()) {
        ReportEditorError("ReopenSavedDocument requires a successful save first");
        return false;
    }
    Vixen::Editor::EditorDocumentModel candidate;
    Vixen::Editor::DocumentDiagnostic diagnostic;
    if (!candidate.Load(lastSavedPath_, diagnostic)) {
        ReportEditorDiagnostic(diagnostic);
        return false;
    }

    const auto snapshot = candidate.CaptureBakeSnapshot();
    Vixen::SVO::RecipeRegistry::RecipeEntry canonical;
    if (!candidate.FlattenToRecipeEntry(canonical, snapshot, diagnostic)) {
        ReportEditorDiagnostic(diagnostic);
        return false;
    }
    const bool programMatch = SameProgram(canonical.bytecode, lastSavedProgram_);
    const bool parametersMatch = std::equal(snapshot.parameterValues.begin(), snapshot.parameterValues.end(),
                                            lastSavedParameters_.begin());
    const bool maskMatch = candidate.EnabledMask() == lastSavedMask_;
    if (!programMatch || !parametersMatch || !maskMatch) {
        ReportEditorError("reopened document differs from the saved canonical program, parameters, or layer mask");
        logger_->Error("[EditorApplication] " + lastEditorError_);
        return false;
    }

    if (!AdoptDocument(std::move(candidate))) return false;
    logger_->Info("[EDITOR/state] reopen revision=" + std::to_string(doc_.Revision()) +
                  " programMatch=1 parametersMatch=1 maskMatch=1");
    return true;
}

bool EditorApplication::AdoptDocument(Vixen::Editor::EditorDocumentModel candidate) {
    auto blob = Vixen::AppFlow::AppFlowBlobFile::Load(appFlowPath_);
    const Vixen::AppFlow::Generated::AppFlowContainerView* view = blob ? &blob->View() : nullptr;
    if (rt_.Load(view, &layerProvider_) != Vixen::AppFlow::LoadResult::Ok) {
        ReportEditorError("AppFlowRuntime reload failed while adopting a document");
        return false;
    }
    channelGroupOpen_ = false;  // the reloaded runtime starts with an empty undo history
    channelGroupLabel_.clear();
    doc_ = std::move(candidate);
    rt_.Layers().SetLayerCount(doc_.LayerCount());
    rt_.Layers().SetMask(doc_.EnabledMask());
    RegisterAppFlowHandlers();
    SyncAfterDocumentMutation();
    ClearEditorError();
    return true;
}

void EditorApplication::RefreshRecipesView() {
    std::vector<Vixen::App::EditorRecipeData> recipes;
    recipes.reserve(pack_.Recipes().size());
    for (const auto& recipe : pack_.Recipes()) recipes.push_back({recipe.id, recipe.id == openRecipeId_});
    Vixen::App::RefreshEditorRecipesView(*layersView_, recipes);
}

void EditorApplication::RefreshStatusView() {
    Vixen::App::EditorStatusData status;
    status.document = !openRecipeId_.empty() ? openRecipeId_
        : std::filesystem::path(documentPath_).filename().string();
    status.message = !lastEditorError_.empty()
        ? "[" + lastEditorDiagnosticCode_ + "] " + lastEditorError_ : statusMessage_;
    status.flowState = std::string(magic_enum::enum_name(rt_.Current()));
    status.undoDepth = static_cast<uint32_t>(rt_.Stack().UndoDepth());
    status.redoDepth = static_cast<uint32_t>(rt_.Stack().RedoDepth());
    Vixen::App::RefreshEditorStatusView(*layersView_, status);
}

void EditorApplication::DeclarePanels(Vixen::RenderGraph::PanelDock& dock) {
    using Vixen::RenderGraph::DockRegion;
    using Vixen::RenderGraph::PanelDecl;
    Vixen::RenderGraph::IView& view = Vixen::App::EditorLayersAsView(*layersView_);
    const auto declare = [&](std::string id, std::string title, std::string document, DockRegion region,
                             uint32_t stack, float size, float minSize, bool closable, bool header) {
        PanelDecl decl;
        decl.id = std::move(id);
        decl.title = std::move(title);
        decl.view = &view;
        decl.document = std::move(document);
        decl.region = region;
        decl.stack = stack;
        decl.size = size;
        decl.minSize = minSize;
        decl.closable = closable;
        decl.header = header;
        dock.Declare(std::move(decl));
    };
    // R765 three regions: layers | viewport | inspector, plus the status line. The viewport is what
    // the dock leaves uncovered.
    declare("outliner", "Recipes", "assets/ui/editor_outliner.rml", DockRegion::Left, 0, 340.0f, 220.0f, true, true);
    declare("layers", "Layers", "assets/ui/editor_layers.rml", DockRegion::Left, 1, 340.0f, 220.0f, true, true);
    declare("inspector", "Inspector", "assets/ui/editor_inspector.rml", DockRegion::Right, 0, 320.0f, 220.0f, true, true);
    declare("status", "Status", "assets/ui/editor_status.rml", DockRegion::Bottom, 0, 28.0f, 24.0f, false, false);
}

void EditorApplication::AfterPanelVerb(const char* verb, Vixen::RenderGraph::DockResult result) {
    const std::string resultName(Vixen::RenderGraph::DockResultName(result));
    if (result == Vixen::RenderGraph::DockResult::Ok) {
        std::string error;
        if (!Panels().SaveLayout(error)) {
            logger_->Warning("[EDITOR/panels] layout save failed: " + error);
        }
        statusMessage_ = std::string("layout: ") + verb;
    } else {
        statusMessage_ = std::string("layout ") + verb + " refused: " + resultName;
    }
    logger_->Info(std::string("[EDITOR/panels] verb=") + verb + " result=" + resultName +
                  " revision=" + std::to_string(Panels().Dock().Revision()));
    // A refused verb still dispatches Ok (the handler ran); the event names the refusal for agents.
    RecordAppFlowEvent(std::string("panel.") + verb +
                       (result == Vixen::RenderGraph::DockResult::Ok ? "" : "." + resultName));
}

bool EditorApplication::ConfigurePack(const std::string& packRoot, const std::string& schemaPath) {
    std::string error;
    if (!pack_.Configure(packRoot, schemaPath, error)) {
        ReportEditorError(error);
        return false;
    }
    logger_->Info("[EDITOR/state] pack root=" + pack_.PackRoot() + " id=" + pack_.PackId() +
                  " assets=" + pack_.AssetDirectory() + "/*" + pack_.AssetExtension() +
                  " recipes=" + std::to_string(pack_.Recipes().size()));
    RefreshRecipesView();
    return true;
}

bool EditorApplication::OpenPackRecipe(const std::string& recipeId) {
    std::string path;
    if (!pack_.TryResolve(recipeId, path)) {
        ReportEditorError("the open pack has no recipe '" + recipeId + "'");
        return false;
    }
    if (GetRenderGraph() == nullptr) {
        // First document, before Prepare(): the ordinary initial load.
        if (!LoadDocument(path)) return false;
    } else {
        Vixen::Editor::EditorDocumentModel candidate;
        Vixen::Editor::DocumentDiagnostic diagnostic;
        if (!candidate.Load(path, diagnostic)) {
            ReportEditorDiagnostic(diagnostic);
            return false;
        }
        if (!AdoptDocument(std::move(candidate))) return false;
        documentPath_ = path;
        reframePending_ = true;
    }
    openRecipeId_ = recipeId;
    RefreshRecipesView();
    std::ostringstream state;
    state << "[EDITOR/state] open recipe=" << recipeId << " path=" << path
          << " layers=" << doc_.LayerCount() << " mask=" << doc_.EnabledMask() << " parameters=[";
    for (uint32_t i = 0; i < doc_.ParameterCount(); ++i) {
        if (i) state << ',';
        state << doc_.ParameterName(i) << '=' << doc_.ParameterValue(i);
    }
    state << "] names=[";
    for (uint32_t i = 0; i < doc_.LayerCount(); ++i) {
        if (i) state << ',';
        state << doc_.LayerName(i);
    }
    state << ']';
    logger_->Info(state.str());
    return true;
}

void EditorApplication::PreTick() {
    TraceEditorLatency("frame", "begin", updateTick_);
    AttachAppFlowEventBus();
    if (channelPaused_ && channelStepBudget_ == 0) return;
    // Capture-fixture controls (VIXEN_EDITOR_CAPTURE_FRAMES / VIXEN_EDITOR_CAPTURE_DIR), parsed once.
    // Unset envs leave them empty, so the interactive editor is unchanged. Edits are never injected
    // here: an agent or test drives them through the AppFlow channel (R468.4).
    if (!captureEnvParsed_) {
        captureEnvParsed_ = true;
        if (const char* captureEnv = std::getenv("VIXEN_EDITOR_CAPTURE_FRAMES")) {
            captureFrames_ = ParseCaptureFrames(captureEnv, logger_.get());
        }
        if (const char* dirEnv = std::getenv("VIXEN_EDITOR_CAPTURE_DIR")) {
            captureDir_ = dirEnv;
        }
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
        // A text control's id is a selector pattern with a value slot, not a selector: clicking it focuses it
        // (the Rml context took the press); nothing dispatches until a value is committed.
        if (!clickedId.empty() && !Vixen::RenderGraph::HasValueSlot(clickedId)) {
            TraceEditorLatency("dispatch", "begin", updateTick_);
            rt_.DispatchBySelector(clickedId);
            TraceEditorLatency("dispatch", "end", updateTick_);
        }
    }
    // Typed values committed by text controls (Enter / blur after an edit): the control's id is the selector
    // with its value slot filled, so a commit is one DispatchBySelector like a click. The runtime refuses a
    // bad value through the handler's normal diagnostic path; UIRenderNode re-reads the model afterwards so
    // the field shows the accepted (or unchanged) value.
    if (auto* ui = GetUiRenderNode()) {
        for (const auto& commit : ui->DrainCommittedInputs()) {
            TraceEditorLatency("dispatch", "begin", updateTick_);
            const auto result = rt_.DispatchBySelector(commit.selector);
            if (result == Vixen::AppFlow::DispatchResult::RejectedByState) {
                ReportEditorError(commit.text.empty()
                                      ? "the field '" + commit.elementId + "' needs a value"
                                      : "no action is bound to the typed field '" + commit.elementId + "' (value '" + commit.text + "')",
                                  "RefusedEdit");
            }
            TraceEditorLatency("dispatch", "end", updateTick_);
        }
    }

    // Keybindings: dispatch by chord, edge-detected (press-only, not held-repeat) via KeyMap.h's
    // GLFW-keycode -> KeyId map. Carries no behavior itself -- DispatchByKey resolves the chord
    // through the InputProfile to whichever action is bound (Save/Undo/Redo/Return), then routes
    // it through the same registered-handler path a UI click uses. An unbound chord (or Esc
    // outside Settings) resolves to RejectedByState and is ignored, never a crash.
    auto* keyboard = [&]() -> Vixen::RenderGraph::InputNode* {
        auto* graph = GetRenderGraph();
        return graph ? static_cast<Vixen::RenderGraph::InputNode*>(graph->GetInstanceByName("input_handler")) : nullptr;
    }();
    if (keyboard) {
        // Key state comes from InputNode's event-derived held set (the callback queue every input takes,
        // ProcessPendingInput having already folded this frame's events), not from polling the window: a
        // scripted appflow.input key and a person's key are the same thing here.
        // Every mapped key is edge-detected; each press resolves its chord (key + live modifiers) through
        // the runtime's InputProfile, so a [FlowKeyDefault] on any declared key binds with no change here.
        // An unbound chord (or Esc outside Settings) resolves to RejectedByState and is ignored.
        // A focused text control owns the keyboard: the edge state still advances, the chord is dropped, so typing
        // "s" into a field never saves and a key held across a blur does not fire late.
        const auto* ui = GetUiRenderNode();
        const bool typing = ui && ui->WantsKeyboard();
        keyEdges_.Poll([keyboard](int key) { return keyboard->IsRawKeyHeld(key); },
                       [this](Vixen::AppFlow::Generated::KeyChord chord) { (void)rt_.DispatchByKey(chord); },
                       typing);
    }
    TranslateCameraInput();
    StepParameterScrub();

    // Inc-B (design §4a): the per-frame .changed<LayerMask>() reconcile, placed AFTER all input
    // dispatch above (UI click + keybindings) so an EXTERNAL write picked up this frame lands in
    // the same dirty_ re-flatten tail below as an input-driven toggle would. The input path's own
    // same-frame echo (inside the ToggleLayer handler) already covers the self case; this call is
    // what makes a non-input Gaia write reach the view at all.
    if (pendingReopenSaved_) {
        pendingReopenSaved_ = false;
        if (!ReopenSavedDocument()) {
            logger_->Error("[EditorApplication] ReopenSaved failed: " + lastEditorError_);
        }
    }
    if (pendingOpenRecipe_.has_value()) {
        const std::string recipeId = std::move(*pendingOpenRecipe_);
        pendingOpenRecipe_.reset();
        if (!OpenPackRecipe(recipeId)) {
            logger_->Error("[EditorApplication] OpenRecipe failed: " + lastEditorError_);
        }
    }

    ReconcileLayersView();
    RefreshStatusView();
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
        const bool reframe = reframePending_;
        reframePending_ = false;
        if (!ApplyDocumentToScene(reframe)) {
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
        ReportEditorError(std::string("Update failed: ") + e.what());
        logger_->Error("[EditorApplication] Update: " + lastEditorError_);
    } catch (...) {
        ReportEditorError("Update failed: unknown (non-std) exception");
        logger_->Error("[EditorApplication] Update: " + lastEditorError_);
    }
    ++channelUpdateSerial_;  // E6b: this Update ran to its end; the next presented frame reflects it
}

void EditorApplication::PostTick() {
    TraceEditorLatency("frame", "end", updateTick_ - 1);
    // VulkanApplicationBase::Tick calls this only after Render(), when compute_render_target
    // contains the completed scene image for the Update tick that scheduled the capture.
    if (!pendingCapturePath_.empty()) {
        const std::string path = pendingCapturePath_;
        std::string captureErr;
        TraceEditorLatency("readback", "begin", updateTick_ - 1);
        if (!CaptureFrameToPng(path, captureErr) && pendingCapture_ == nullptr) {
            logger_->Error("[EditorApplication] CaptureFrameToPng failed for " + path + ": " + captureErr);
        }
        TraceEditorLatency("readback", "end", updateTick_ - 1);
    }
    VulkanGraphApplication::PostTick();
    ++channelPresentedFrame_;
    for (auto pending = channelActionPending_.begin(); pending != channelActionPending_.end();) {
        if (channelUpdateSerial_ >= pending->second) {
            channelActionFrames_[pending->first] = channelPresentedFrame_;
            pending = channelActionPending_.erase(pending);
        } else {
            ++pending;
        }
    }
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
    // The swapchain's IRenderTarget::GetCurrentIndex() is never advanced after an acquire (it stays 0),
    // so the image just presented comes from the swapchain node itself (E6b).
    uint32_t imageIndex = 0;
    bool haveImageIndex = false;
    if (auto* swapInstance = graph->GetInstanceByName("main_swapchain")) {
        auto* swapNode = static_cast<SwapChainNode*>(swapInstance);
        target = swapNode->GetSwapchainPublic();
        imageIndex = swapNode->GetCurrentImageIndex();
        haveImageIndex = true;
    }
    if (!target) {
        auto* renderTargetInstance = graph->GetInstanceByName("compute_render_target");
        if (renderTargetInstance) {
            Resource* output = renderTargetInstance->GetOutput(0, 0);
            if (output) target = output->GetHandle<IRenderTarget*>();
        }
    }
    if (target && !haveImageIndex) imageIndex = target->GetCurrentIndex();
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
        // E6b: a dispatch-attached readback waits for the first frame presented after an Update that
        // reflected its action (View reconcile + scene re-apply), then reads that frame.
        const bool actionBound = pending.requiredSerial != 0;
        if (pending.enqueued) { ++it; continue; }
        if (actionBound) {
            if (channelUpdateSerial_ < pending.requiredSerial) { ++it; continue; }
            pending.frame = channelPresentedFrame_;
        } else if (pending.frame > channelPresentedFrame_) { ++it; continue; }
        const std::string key = it->first;
        try {
            if (!actionBound && pending.frame < channelPresentedFrame_) {
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
                    device->graphicsQueueIndex, target, imageIndex, x, y, width, height, error)) {
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

void EditorApplication::ReportEditorDiagnostic(const Vixen::Editor::DocumentDiagnostic& diagnostic) {
    ReportEditorError(diagnostic.message, std::string(magic_enum::enum_name(diagnostic.code)));
}

void EditorApplication::ReportEditorError(std::string message, std::string code) {
    lastEditorError_ = std::move(message);
    lastEditorDiagnosticCode_ = std::move(code);
    logger_->Warning("[EditorApplication] diagnostic " + lastEditorDiagnosticCode_ + ": " + lastEditorError_);
    RecordAppFlowEvent("diagnostic", {{"code", lastEditorDiagnosticCode_}, {"message", lastEditorError_}});
}

void EditorApplication::ClearEditorError() {
    lastEditorError_.clear();
    lastEditorDiagnosticCode_.clear();
}

void EditorApplication::AttachAppFlowEventBus() {
    using namespace Vixen::AppFlow;
    auto* graph = GetRenderGraph();
    auto* bus = graph ? graph->GetMessageBus() : nullptr;
    if (!bus || bus == appFlowBus_) return;
    appFlowBus_ = bus;
    rt_.SetEventBus(bus, /*sender*/ 1);
    // The runtime publishes with PublishImmediate on this thread; the channel log mirrors what it says.
    bus->Subscribe(AppFlowEdgeEvent::TYPE, [this](const Vixen::EventBus::BaseEventMessage& message) {
        const auto& edge = static_cast<const AppFlowEdgeEvent&>(message);
        RecordAppFlowEvent("flow.edge", {{"edge", edge.edgeId}, {"cause", edge.cause.identity},
                                         {"from", std::string(magic_enum::enum_name(edge.from))},
                                         {"to", std::string(magic_enum::enum_name(edge.to))}});
        return false;
    });
    bus->Subscribe(AppFlowDiagnosticEvent::TYPE, [this](const Vixen::EventBus::BaseEventMessage& message) {
        const auto& diagnostic = static_cast<const AppFlowDiagnosticEvent&>(message);
        RecordAppFlowEvent("flow.diagnostic", {{"message", diagnostic.message}});
        return false;
    });
    bus->Subscribe(AppFlowChangedEvent::TYPE, [this](const Vixen::EventBus::BaseEventMessage& message) {
        const auto& changed = static_cast<const AppFlowChangedEvent&>(message);
        static constexpr const char* kKind[] = {"flow.state", "flow.action", "flow.undone", "flow.redone"};
        RecordAppFlowEvent(kKind[static_cast<int>(changed.kind)],
                           {{"actionId", static_cast<uint16_t>(changed.action)}, {"group", changed.group}});
        return false;
    });
}

void EditorApplication::RecordAppFlowEvent(const std::string& type) {
    RecordAppFlowEvent(type, nlohmann::json::object());
}

void EditorApplication::RecordAppFlowEvent(const std::string& type, nlohmann::json detail) {
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
    if (!detail.empty()) event["detail"] = std::move(detail);
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
                            {"mods", Vixen::Editor::ChordModNames(key.chord.mods)},
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
                std::vector<std::string> names;
                for (const auto& modValue : chord.at("mods")) {
                    if (!modValue.is_string()) throw std::runtime_error("each chord mod must be a string");
                    names.push_back(modValue.get<std::string>());
                }
                if (!Vixen::Editor::ParseChordMods(names, mods)) {
                    throw std::runtime_error("unknown generated chord modifier");
                }
            }
            result = rt_.DispatchByKey({*key, mods});
        }
        const uint64_t requiredSerial = channelUpdateSerial_ + 1;
        channelActionFrames_.erase(actionKey);
        channelActionPending_[actionKey] = requiredSerial;
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
                visual.dump(), channelPresentedFrame_ + 1, requiredSerial,
                std::chrono::steady_clock::now(), false};
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
            // R766: the PanelLayout View's nouns read straight from the dock's layout rows.
            if (const std::string_view nounName = magic_enum::enum_name(noun); nounName.starts_with("PanelLayout_")) {
                const nlohmann::json layout = nlohmann::json::parse(Panels().LayoutJson());
                const std::string field(nounName.substr(std::string_view("PanelLayout_").size()));
                nlohmann::json value;
                if (field == "stacks" || field == "panels") value = layout.at(field);
                else if (field == "viewportLeft") value = layout.at("viewport").at("left");
                else if (field == "viewportTop") value = layout.at("viewport").at("top");
                else if (field == "viewportWidth") value = layout.at("viewport").at("width");
                else if (field == "viewportHeight") value = layout.at("viewport").at("height");
                else throw std::runtime_error("PanelLayout noun has no layout field");
                return nlohmann::json{{"noun", EnumRecord(noun)}, {"value", std::move(value)},
                                      {"frame", channelPresentedFrame_}}.dump();
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
        nlohmann::json recipes = nlohmann::json::array();
        for (const auto& recipe : pack_.Recipes()) {
            recipes.push_back({{"id", recipe.id}, {"open", recipe.id == openRecipeId_}});
        }
        return nlohmann::json{
            {"flowState", EnumRecord(rt_.Current())},
            {"undoDepth", rt_.Stack().UndoDepth()},
            {"redoDepth", rt_.Stack().RedoDepth()},
            {"views", std::move(views)},
            {"pack", {{"root", pack_.PackRoot()}, {"id", pack_.PackId()}, {"recipes", std::move(recipes)},
                      {"open", openRecipeId_}, {"lastSaved", lastSavedPath_}}},
            {"panelLayout", nlohmann::json::parse(Panels().LayoutJson())},
            {"diagnostic", {{"code", lastEditorDiagnosticCode_}, {"message", lastEditorError_}}},
            {"group", {{"open", channelGroupOpen_}, {"label", channelGroupLabel_}}},
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
            if (channelActionPending_.contains(actionKey)) return std::nullopt;  // not reflected yet
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

    if (method == "appflow.begin_group" || method == "appflow.end_group") {
        const bool begin = method == "appflow.begin_group";
        std::string outcome = "Ok";
        if (begin) {
            const std::string label = params.value("label", std::string{});
            if (label.empty()) throw std::runtime_error("begin_group requires a non-empty label");
            if (channelGroupOpen_) {
                outcome = "NestedGroupRefused";
            } else {
                channelGroupOpen_ = true;
                channelGroupLabel_ = label;
                rt_.Stack().BeginGroup(++channelGroupSerial_);
                RecordAppFlowEvent("group.begin", {{"label", label}, {"group", channelGroupSerial_}});
            }
        } else if (!channelGroupOpen_) {
            outcome = "NoOpenGroupRefused";
        } else {
            rt_.Stack().EndGroup();
            channelGroupOpen_ = false;
            RecordAppFlowEvent("group.end", {{"label", channelGroupLabel_}, {"group", channelGroupSerial_}});
        }
        return nlohmann::json{{"group", {{"result", outcome}, {"open", channelGroupOpen_},
                                          {"label", channelGroupLabel_}}},
                              {"undoDepth", rt_.Stack().UndoDepth()},
                              {"redoDepth", rt_.Stack().RedoDepth()}}.dump();
    }

    if (method == "appflow.navigate") {
        // A flow transition named by its generated edge id (the ids appflow.list reports under
        // "transitions"); the runtime applies the edge's guard and records the cause.
        if (!params.contains("edge") || !params.at("edge").is_string()) {
            throw std::runtime_error("navigate requires a generated transition edge id");
        }
        const std::string edge = params.at("edge").get<std::string>();
        bool declared = false;
        for (const auto& candidate : kTransitions) if (edge == candidate.id) declared = true;
        if (!declared) throw std::runtime_error("edge is not in the generated transition catalog");
        const DispatchResult result = rt_.NavTo(edge, {FlowTriggerKind::System, "AppFlowChannel"});
        RecordAppFlowEvent("navigate", {{"edge", edge}, {"result", std::string(magic_enum::enum_name(result))}});
        return nlohmann::json{{"dispatchResult", {{"code", static_cast<int>(result)},
                                                   {"name", std::string(magic_enum::enum_name(result))}}},
                              {"flowState", EnumRecord(rt_.Current())}}.dump();
    }

    if (method == "appflow.input") {
        // Scripted input on the app's REAL input path (R468: the same path a person's input takes). Events are
        // queued exactly where the GLFW callbacks queue theirs (InputNode's ring), so they are folded into the
        // next frame's InputState and reach every consumer (the UI's text controls, the camera translation, the
        // selection providers) the way typed or pointed input does; nothing here dispatches an action directly.
        //   {"kind":"text","text":"1.5"}
        //   {"kind":"key","key":"Enter","action":"press|release|repeat|tap","mods":["Ctrl"]}   (key = a generated KeyId name)
        //   {"kind":"pointer","x":120,"y":80,"action":"move|press|release|click","button":"left|right|middle"}
        auto* graph = GetRenderGraph();
        auto* input = graph ? static_cast<Vixen::RenderGraph::InputNode*>(graph->GetInstanceByName("input_handler"))
                            : nullptr;
        if (!input) throw std::runtime_error("the running graph has no input node");
        const std::string kind = params.value("kind", std::string{});
        uint32_t queued = 0;
        if (kind == "text") {
            if (!params.contains("text") || !params.at("text").is_string()) throw std::runtime_error("input text requires a text string");
            const std::string text = params.at("text").get<std::string>();
            for (size_t i = 0; i < text.size();) {  // UTF-8 -> code points
                const unsigned char lead = static_cast<unsigned char>(text[i]);
                const size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 0;
                if (length == 0 || i + length > text.size()) throw std::runtime_error("input text is not valid UTF-8");
                char32_t cp = length == 1 ? lead : static_cast<char32_t>(lead & (0xFF >> (length + 1)));
                for (size_t k = 1; k < length; ++k) cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
                input->InjectChar(cp);
                ++queued;
                i += length;
            }
        } else if (kind == "key") {
            const auto id = magic_enum::enum_cast<Vixen::AppFlow::Generated::KeyId>(params.value("key", std::string{}));
            const int glfwKey = id ? Vixen::Editor::KeyIdToGlfw(*id) : -1;
            if (glfwKey < 0) throw std::runtime_error("input key must be a generated KeyId name (see appflow.list chords)");
            int mods = 0;
            for (const auto& name : params.value("mods", std::vector<std::string>{})) {
                if (name == "Shift") mods |= GLFW_MOD_SHIFT;
                else if (name == "Ctrl") mods |= GLFW_MOD_CONTROL;
                else if (name == "Alt") mods |= GLFW_MOD_ALT;
                else if (name == "Super") mods |= GLFW_MOD_SUPER;
                else throw std::runtime_error("input mods are Shift, Ctrl, Alt, Super");
            }
            const std::string action = params.value("action", std::string("tap"));
            if (action == "press" || action == "tap") { input->InjectRawKey(glfwKey, GLFW_PRESS, mods); ++queued; }
            if (action == "repeat") { input->InjectRawKey(glfwKey, GLFW_REPEAT, mods); ++queued; }
            if (action == "release" || action == "tap") { input->InjectRawKey(glfwKey, GLFW_RELEASE, mods); ++queued; }
            if (queued == 0) throw std::runtime_error("input key action is press, release, repeat or tap");
        } else if (kind == "pointer") {
            if (!params.contains("x") || !params.contains("y") || !params.at("x").is_number() || !params.at("y").is_number()) {
                throw std::runtime_error("input pointer requires numeric x and y (window pixels)");
            }
            input->InjectCursorPos(params.at("x").get<double>(), params.at("y").get<double>());
            ++queued;
            const std::string action = params.value("action", std::string("move"));
            const std::string buttonName = params.value("button", std::string("left"));
            const int button = buttonName == "left" ? GLFW_MOUSE_BUTTON_LEFT
                             : buttonName == "right" ? GLFW_MOUSE_BUTTON_RIGHT
                             : buttonName == "middle" ? GLFW_MOUSE_BUTTON_MIDDLE : -1;
            if (action != "move" && button < 0) throw std::runtime_error("input pointer button is left, right or middle");
            if (action == "press" || action == "click") { input->InjectMouseButton(button, GLFW_PRESS); ++queued; }
            if (action == "release" || action == "click") { input->InjectMouseButton(button, GLFW_RELEASE); ++queued; }
            if (action != "move" && action != "press" && action != "release" && action != "click") {
                throw std::runtime_error("input pointer action is move, press, release or click");
            }
        } else {
            throw std::runtime_error("input kind is text, key or pointer");
        }
        RecordAppFlowEvent("input", {{"kind", kind}, {"queued", queued}});
        return nlohmann::json{{"queued", queued}, {"kind", kind}, {"frame", channelPresentedFrame_}}.dump();
    }

    if (method == "appflow.quit") {
        // Orderly shutdown through the same path a closed window takes (system acknowledgements, log and
        // cache extraction), so a finished agent or test session does not end as a killed process.
        RecordAppFlowEvent("quit");
        HandleShutdownRequest();
        return nlohmann::json{{"quitting", true}, {"frame", channelPresentedFrame_}}.dump();
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
            if (const auto reflecting = channelActionPending_.find(actionKey);
                reflecting != channelActionPending_.end()) {
                channelReadbacks_[requestKey] = PendingReadback{
                    params.dump(), 0, reflecting->second, std::chrono::steady_clock::now(), false};
                return std::nullopt;
            }
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
            params.dump(), targetFrame, 0, std::chrono::steady_clock::now(), false};
        return std::nullopt;
    }

    throw std::runtime_error("unknown AppFlow method: " + method);
}
