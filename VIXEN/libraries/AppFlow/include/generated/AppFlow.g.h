#pragma once
// <provenance: generated from AppFlowReference — do not edit by hand>
#include <cstdint>
#include <span>

namespace Vixen::AppFlow::Generated {

enum class FlowStateId : uint16_t { Editing=0, Simulating=1, Paused=2 };

enum class FlowGuardId : uint16_t { DocumentValid=0 };

enum class FlowActionId : uint16_t { ToggleLayer=0 };

// Mirrors undertow's UiParamType (String/Int/Float/EntityRef) — design §7c.
enum class FlowParamType : uint8_t { String=0, Int=1, Float=2, EntityRef=3 };

struct LayerState { uint32_t enabledMask; };

// Mirrors undertow's UiParamSchema.
struct FlowParamSchema { const char* name; FlowParamType type; };

struct AppFlowActionDecl {
    FlowActionId id;
    uint32_t footprintBytes;
    bool hasInvert;
    const FlowParamSchema* params;
    uint32_t paramCount;
};

struct AppFlowTransition { FlowStateId from; FlowStateId to; FlowGuardId guard; };

inline constexpr FlowParamSchema kToggleLayerParams[] = { {"layerIndex", FlowParamType::Int} };

inline constexpr AppFlowActionDecl kActionDecls[] = {
    { FlowActionId::ToggleLayer, sizeof(LayerState), true, kToggleLayerParams, 1 }
};

inline constexpr AppFlowTransition kTransitions[] = {
    { FlowStateId::Editing, FlowStateId::Simulating, FlowGuardId::DocumentValid }
};

// Mirrors RecipeContainerView's role: the reader later tasks parse the artifact through.
struct AppFlowContainerView {
    static constexpr std::span<const AppFlowActionDecl> actions() { return kActionDecls; }
    static constexpr std::span<const AppFlowTransition> transitions() { return kTransitions; }
};

} // namespace Vixen::AppFlow::Generated
