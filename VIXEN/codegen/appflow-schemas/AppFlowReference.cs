// AppFlow reference vocabulary — the single-source schema `AppFlowEmitter` reflects to
// generate `AppFlow.g.h` (Inc-4 M1 retired the hand-authored mirror). VIXEN ships this
// module (design §9 option a); states/guards/actions/param-signature/transition match
// design §4.1. Inc-4 M2 extends it with the typed key vocabulary + element/key/return-edge
// declarations (design §3.1/§5.2).
using Yeroket.Util.KernelFramework;

namespace Vixen.AppFlow.Reference
{
    // States — members become FlowStateId (pinned, append-only).
    [FlowStateEnum]
    public enum FlowState { Editing = 0, Simulating = 1, Paused = 2, Settings = 3 }

    // Guards — declared predicate opcodes.
    [FlowGuardEnum]
    public enum FlowGuard { DocumentValid = 0 }

    // Actions — members become FlowActionId (pinned, append-only).
    [FlowActionEnum]
    public enum FlowAction { ToggleLayer = 0, Undo = 1, Redo = 2, Save = 3, UndoSettingChange = 4 }

    // Typed key vocabulary (design §3.1/§5.2) — KeyChord{KeyId,KeyMod} is the ONLY boundary;
    // the host-side glfw-keycode→KeyId map (M4) is the sole raw-string/int crossing.
    [FlowKeyEnum]
    public enum KeyId : ushort
    {
        None = 0, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z, Escape
    }

    [FlowModEnum]
    public enum KeyMod : byte { None = 0, Ctrl = 1, Shift = 2, Alt = 4, Super = 8 }

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

    // One transition table entry, declared as data. Index 0 — [FlowEdgeEffect(0)] below targets it.
    [FlowTransition] // from=Editing to=Simulating guard=DocumentValid
    public static class Transitions
    {
        public const int From = (int)FlowState.Editing;
        public const int To = (int)FlowState.Simulating;
        public const int Guard = (int)FlowGuard.DocumentValid;
    }

    // Effect-ref on transition 0 (Editing->Simulating). DESIGNED not built: nothing consumes
    // this yet — the column exists so an edge-effect runtime has a place to read from (design §D7).
    [FlowEdgeEffect(0)]
    public static class T0Effect { public const string Effect = "none"; }

    // Element click → ToggleLayer. The element identity stays a dynamic-read string this
    // increment (design §D3/§5.2); only the extracted {index} param is typed.
    [FlowElementTrigger(nameof(FlowAction.ToggleLayer))]
    public static class ToggleLayerTrigger
    {
        public const string Element = "layer-{index}-toggle";
        public const string ParamName = "layerIndex";
        public const string On = "click";
    }

    // Scoped key defaults — tightest-wins resolution (global -> flow-state -> context).
    [FlowKeyDefault(nameof(FlowAction.Undo), FlowScope.Global)]
    public static class UndoKey { public const KeyId Key = KeyId.Z; public const KeyMod Mods = KeyMod.Ctrl; }

    [FlowKeyDefault(nameof(FlowAction.Redo), FlowScope.Global)]
    public static class RedoKey { public const KeyId Key = KeyId.Y; public const KeyMod Mods = KeyMod.Ctrl; }

    [FlowKeyDefault(nameof(FlowAction.Save), FlowScope.Global)]
    public static class SaveKey { public const KeyId Key = KeyId.S; public const KeyMod Mods = KeyMod.None; }

    // Settings sub-state — the scoped-override + return-edge live demonstration's home (design §6.3).
    // Reachable via transition index 1 (Editing -> Settings).
    [FlowTransition] // from=Editing to=Settings guard=DocumentValid
    public static class ToSettings
    {
        public const int From = (int)FlowState.Editing;
        public const int To = (int)FlowState.Settings;
        public const int Guard = (int)FlowGuard.DocumentValid;
    }

    // Ctrl+Z means UndoSettingChange while in Settings — overrides the global Undo binding
    // without re-declaring Ctrl+Z anywhere else (tightest-wins, design §D3/§4.1).
    [FlowKeyDefault(nameof(FlowAction.UndoSettingChange), FlowScope.State, nameof(FlowState.Settings))]
    public static class SettingsUndoOverride { public const KeyId Key = KeyId.Z; public const KeyMod Mods = KeyMod.Ctrl; }

    // Escape pops out of Settings back to the prior state (FSM entry-history, not ActionStack undo).
    [FlowReturnEdge(nameof(FlowState.Settings))]
    public static class SettingsReturn { public const KeyId Key = KeyId.Escape; public const KeyMod Mods = KeyMod.None; }
}
