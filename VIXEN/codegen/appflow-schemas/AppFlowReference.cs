// Codegen-tool decision (Inc1 Task 1, outcome 2 of 3): the existing Yeroket
// `CodegenTool~` (VIXEN/codegen/CMakeLists.txt's `octreeconfig_check`/`_regen`)
// only loads `[GpuStruct]`-marked structs and emits a C++/GLSL struct BODY
// (see CompilationLoader.LoadGpuStructs + GpuStructCppEmitter/GpuStructGlslEmitter
// in Yeroket's SourceGenerator~/Transpiler) — it has no enum, table, or reader
// emitter, and no `--help`/multi-artifact mode. It cannot produce AppFlow.g.h's
// FlowStateId/FlowActionId enums, kActionDecls/kTransitions tables, or
// AppFlowContainerView. Extending it to do so is a Yeroket-repo change and is
// CROSS-TREE / out of scope for this milestone (see implementer-prompt-m1.md's
// cross-tree caveat). Per the plan's outcome-2 path: this file is the canonical,
// documented reference vocabulary (states/guards/actions/param-signature/
// transition, matching design §4.1) but `AppFlow.g.h` is HAND-AUTHORED to match
// it exactly for Inc 1. TODO(appflow-codegen): a follow-up increment should
// extend the Yeroket tool (or add a sibling emitter) to generate AppFlow.g.h
// from this declaration, the way SdfOpCodes.g.h is generated from SDFOpCode.
//
// AppFlow Inc-1 reference vocabulary. VIXEN ships this minimal module (design §9 option a).
using Vixen.AppFlow.Reference;

namespace Vixen.AppFlow.Reference
{
    // States — members become FlowStateId (pinned, append-only).
    [FlowStateEnum]
    public enum FlowState { Editing = 0, Simulating = 1, Paused = 2 }

    // Guards — declared predicate opcodes.
    [FlowGuardEnum]
    public enum FlowGuard { DocumentValid = 0 }

    // Actions — members become FlowActionId (pinned, append-only).
    [FlowActionEnum]
    public enum FlowAction { ToggleLayer = 0 }

    // Param wire types — mirror undertow's UiParamType (String/Int/Float/EntityRef),
    // the generalized UI-action param contract (design §7c).
    [FlowParamTypeEnum]
    public enum FlowParamType { String = 0, Int = 1, Float = 2, EntityRef = 3 }

    // Action param signature — ToggleLayer takes one Int param `layerIndex`. Declared
    // so the typed-param path (validate + carry) is proven non-vacuously in Inc 1.
    // Mirrors UiActionRegistry's UiParamSchema[] signature.
    [FlowActionParams(nameof(FlowAction.ToggleLayer))]
    public static class ToggleLayerParams
    {
        // name → type; the generator emits a param-schema table entry per action.
        public const string Param0Name = "layerIndex";
        public const int    Param0Type = (int)FlowParamType.Int;
    }

    // Footprint struct for ToggleLayer — a GpuStruct-style serializable blob so the
    // runtime can snapshot it generically (Inc 2 uses this; Inc 1 emits it only).
    [FlowStateStruct]
    public struct LayerState { public uint enabledMask; }

    // One transition table entry, declared as data.
    [FlowTransition] // from=Editing to=Simulating guard=DocumentValid
    public static class Transitions
    {
        public const int From = (int)FlowState.Editing;
        public const int To = (int)FlowState.Simulating;
        public const int Guard = (int)FlowGuard.DocumentValid;
    }
}
