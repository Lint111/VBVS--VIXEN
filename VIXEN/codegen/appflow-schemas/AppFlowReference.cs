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
    public enum FlowState { [FlowInitialState] Editing = 0, Simulating = 1, Paused = 2, Settings = 3 }

    // Guards — declared predicate opcodes.
    [FlowGuardEnum]
    public enum FlowGuard { DocumentValid = 0 }

    // Actions — members become FlowActionId (pinned, append-only).
    [FlowActionEnum]
    public enum FlowAction
    {
        ToggleLayer = 0, Undo = 1, Redo = 2, Save = 3, UndoSettingChange = 4, Return = 5, Data = 6,
        CreateLayer = 7, DeleteLayer = 8, MoveLayerUp = 9, MoveLayerDown = 10,
        EditProgramFieldUp = 11, EditProgramFieldDown = 12,
        AdjustParameterUp = 13, AdjustParameterDown = 14,
        OpenRecipe = 15,
        // R766: panel layout verbs on the app's channel (PanelDock). Not undoable document edits.
        OpenPanel = 16, ClosePanel = 17, FocusPanel = 18, MovePanel = 19, SplitPanel = 20,
        FloatPanel = 21, ResizeRegion = 22, ResetLayout = 23,
    }

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

    [FlowActionParams(nameof(FlowAction.DeleteLayer))]
    public static class DeleteLayerParams
    {
        public const string Param0Name = "layerIndex";
        public const int Param0Type = (int)FlowParamType.Int;
    }

    [FlowActionParams(nameof(FlowAction.MoveLayerUp))]
    public static class MoveLayerUpParams
    {
        public const string Param0Name = "layerIndex";
        public const int Param0Type = (int)FlowParamType.Int;
    }

    [FlowActionParams(nameof(FlowAction.MoveLayerDown))]
    public static class MoveLayerDownParams
    {
        public const string Param0Name = "layerIndex";
        public const int Param0Type = (int)FlowParamType.Int;
    }

    [FlowActionParams(nameof(FlowAction.EditProgramFieldUp))]
    public static class EditProgramFieldUpParams
    {
        public const string Param0Name = "layerIndex";
        public const int Param0Type = (int)FlowParamType.Int;
    }

    [FlowActionParams(nameof(FlowAction.EditProgramFieldDown))]
    public static class EditProgramFieldDownParams
    {
        public const string Param0Name = "layerIndex";
        public const int Param0Type = (int)FlowParamType.Int;
    }

    [FlowActionParams(nameof(FlowAction.AdjustParameterUp))]
    public static class AdjustParameterUpParams
    {
        public const string Param0Name = "parameterIndex";
        public const int Param0Type = (int)FlowParamType.Int;
    }

    [FlowActionParams(nameof(FlowAction.AdjustParameterDown))]
    public static class AdjustParameterDownParams
    {
        public const string Param0Name = "parameterIndex";
        public const int Param0Type = (int)FlowParamType.Int;
    }

    // R765: open a pack render recipe by its content id (`<pack id>:<document stem>`, the id a render_recipe
    // row's `blob:` names). The editor lists the pack's layered documents; Save writes the open one back
    // into the pack.
    [FlowActionParams(nameof(FlowAction.OpenRecipe))]
    public static class OpenRecipeParams
    {
        public const string Param0Name = "recipeId";
        public const int Param0Type = (int)FlowParamType.String;
    }

    // R766: panel ids are the declared PanelDecl ids; regions are left | right | bottom | overlay | floating.
    [FlowActionParams(nameof(FlowAction.OpenPanel))]
    public static class OpenPanelParams
    {
        public const string Param0Name = "panelId";
        public const int Param0Type = (int)FlowParamType.String;
    }

    [FlowActionParams(nameof(FlowAction.ClosePanel))]
    public static class ClosePanelParams
    {
        public const string Param0Name = "panelId";
        public const int Param0Type = (int)FlowParamType.String;
    }

    [FlowActionParams(nameof(FlowAction.FocusPanel))]
    public static class FocusPanelParams
    {
        public const string Param0Name = "panelId";
        public const int Param0Type = (int)FlowParamType.String;
    }

    // Dock as a tab of the region's last stack.
    [FlowActionParams(nameof(FlowAction.MovePanel))]
    public static class MovePanelParams
    {
        public const string Param0Name = "panelId";
        public const int Param0Type = (int)FlowParamType.String;
        public const string Param1Name = "region";
        public const int Param1Type = (int)FlowParamType.String;
    }

    // Dock into a new split stack of the region.
    [FlowActionParams(nameof(FlowAction.SplitPanel))]
    public static class SplitPanelParams
    {
        public const string Param0Name = "panelId";
        public const int Param0Type = (int)FlowParamType.String;
        public const string Param1Name = "region";
        public const int Param1Type = (int)FlowParamType.String;
    }

    [FlowActionParams(nameof(FlowAction.FloatPanel))]
    public static class FloatPanelParams
    {
        public const string Param0Name = "panelId";
        public const int Param0Type = (int)FlowParamType.String;
        public const string Param1Name = "x";
        public const int Param1Type = (int)FlowParamType.Int;
        public const string Param2Name = "y";
        public const int Param2Type = (int)FlowParamType.Int;
        public const string Param3Name = "width";
        public const int Param3Type = (int)FlowParamType.Int;
        public const string Param4Name = "height";
        public const int Param4Type = (int)FlowParamType.Int;
    }

    // Width of a left/right region, height of the bottom region, in pixels.
    [FlowActionParams(nameof(FlowAction.ResizeRegion))]
    public static class ResizeRegionParams
    {
        public const string Param0Name = "region";
        public const int Param0Type = (int)FlowParamType.String;
        public const string Param1Name = "size";
        public const int Param1Type = (int)FlowParamType.Int;
    }

    // Footprint struct for ToggleLayer — a GpuStruct-style serializable blob so the
    // runtime can snapshot it generically (Inc 2 uses this; Inc 1 emits it only).
    [FlowStateStruct]
    public struct LayerState { public uint enabledMask; }

    // Declared edges are the only way to change AppFlow state.
    [FlowTransition] // from=Editing to=Simulating guard=DocumentValid
    public static class Transitions
    {
        public const int From = (int)FlowState.Editing;
        public const int To = (int)FlowState.Simulating;
        public const int Guard = (int)FlowGuard.DocumentValid;
    }

    // Element click → ToggleLayer. The element identity stays a dynamic-read string this
    // increment (design §D3/§5.2); only the extracted {index} param is typed.
    [FlowElementTrigger(nameof(FlowAction.ToggleLayer))]
    public static class ToggleLayerTrigger
    {
        public const string Element = "layer-{index}-toggle";
        public const string ParamName = "layerIndex";
        public const string On = "click";
    }

    [FlowElementTrigger(nameof(FlowAction.CreateLayer))]
    public static class CreateLayerTrigger
    {
        public const string Element = "add-layer";
        public const string ParamName = "";
        public const string On = "click";
    }

    [FlowElementTrigger(nameof(FlowAction.DeleteLayer))]
    public static class DeleteLayerTrigger
    {
        public const string Element = "layer-{index}-delete";
        public const string ParamName = "layerIndex";
        public const string On = "click";
    }

    [FlowElementTrigger(nameof(FlowAction.MoveLayerUp))]
    public static class MoveLayerUpTrigger
    {
        public const string Element = "layer-{index}-up";
        public const string ParamName = "layerIndex";
        public const string On = "click";
    }

    [FlowElementTrigger(nameof(FlowAction.MoveLayerDown))]
    public static class MoveLayerDownTrigger
    {
        public const string Element = "layer-{index}-down";
        public const string ParamName = "layerIndex";
        public const string On = "click";
    }

    [FlowElementTrigger(nameof(FlowAction.EditProgramFieldUp))]
    public static class EditProgramFieldUpTrigger
    {
        public const string Element = "layer-{index}-program-up";
        public const string ParamName = "layerIndex";
        public const string On = "click";
    }

    [FlowElementTrigger(nameof(FlowAction.EditProgramFieldDown))]
    public static class EditProgramFieldDownTrigger
    {
        public const string Element = "layer-{index}-program-down";
        public const string ParamName = "layerIndex";
        public const string On = "click";
    }

    [FlowElementTrigger(nameof(FlowAction.AdjustParameterUp))]
    public static class AdjustParameterUpTrigger
    {
        public const string Element = "parameter-{index}-up";
        public const string ParamName = "parameterIndex";
        public const string On = "click";
    }

    [FlowElementTrigger(nameof(FlowAction.AdjustParameterDown))]
    public static class AdjustParameterDownTrigger
    {
        public const string Element = "parameter-{index}-down";
        public const string ParamName = "parameterIndex";
        public const string On = "click";
    }

    // A listed recipe row's button carries its content id in the element id ("recipe-core:asteroid-open").
    [FlowElementTrigger(nameof(FlowAction.OpenRecipe))]
    public static class OpenRecipeTrigger
    {
        public const string Element = "recipe-{recipeId}-open";
        public const string ParamName = "recipeId";
        public const string On = "click";
    }

    [FlowElementTrigger(nameof(FlowAction.Undo))]
    public static class UndoButtonTrigger
    {
        public const string Element = "undo-button";
        public const string ParamName = "";
        public const string On = "click";
    }

    [FlowElementTrigger(nameof(FlowAction.Redo))]
    public static class RedoButtonTrigger
    {
        public const string Element = "redo-button";
        public const string ParamName = "";
        public const string On = "click";
    }

    [FlowElementTrigger(nameof(FlowAction.Save))]
    public static class SaveButtonTrigger
    {
        public const string Element = "save-button";
        public const string ParamName = "";
        public const string On = "click";
    }

    // R766: the dock chrome's buttons. Each tab, close and dock-to button carries its panel id in the element id.
    [FlowElementTrigger(nameof(FlowAction.FocusPanel))]
    public static class FocusPanelTrigger
    {
        public const string Element = "panel-{panelId}-tab";
        public const string ParamName = "panelId";
        public const string On = "click";
    }

    [FlowElementTrigger(nameof(FlowAction.ClosePanel))]
    public static class ClosePanelTrigger
    {
        public const string Element = "panel-{panelId}-close";
        public const string ParamName = "panelId";
        public const string On = "click";
    }

    // Two placeholders: the panel and the target region ("panel-inspector-to-left").
    [FlowElementTrigger(nameof(FlowAction.MovePanel))]
    public static class MovePanelTrigger
    {
        public const string Element = "panel-{panelId}-to-{region}";
        public const string ParamName = "panelId";
        public const string On = "click";
    }

    [FlowKeyDefault(nameof(FlowAction.ResetLayout), FlowScope.Global)]
    public static class ResetLayoutKey { public const KeyId Key = KeyId.R; public const KeyMod Mods = KeyMod.Ctrl; }

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

    // History returns from Settings to Editing through this declared edge.
    [FlowTransition] // from=Settings to=Editing guard=DocumentValid
    public static class SettingsEditingEdge
    {
        public const int From = (int)FlowState.Settings;
        public const int To = (int)FlowState.Editing;
        public const int Guard = (int)FlowGuard.DocumentValid;
    }

    // Ctrl+Z means UndoSettingChange while in Settings — overrides the global Undo binding
    // without re-declaring Ctrl+Z anywhere else (tightest-wins, design §D3/§4.1).
    [FlowKeyDefault(nameof(FlowAction.UndoSettingChange), FlowScope.State, nameof(FlowState.Settings))]
    public static class SettingsUndoOverride { public const KeyId Key = KeyId.Z; public const KeyMod Mods = KeyMod.Ctrl; }

    // Escape pops out of Settings back to the prior state (FSM entry-history, not ActionStack undo).
    [FlowReturnEdge(nameof(FlowState.Settings))]
    public static class SettingsReturn { public const KeyId Key = KeyId.Escape; public const KeyMod Mods = KeyMod.None; }

    // A UI back-button reaches the same Return action as Escape (design §4.3 — "a BUTTON
    // reaches Return identically to Esc"); no element-index param.
    [FlowElementTrigger(nameof(FlowAction.Return))]
    public static class BackButtonTrigger
    {
        public const string Element = "back-button";
        public const string ParamName = "";
        public const string On = "click";
    }

    // Data names the [View] noun it mutates (design D16) — the View (Inc-1->3) is the single
    // source for data flowing engine<->consumer; a Data verb operates on a declared View noun,
    // not an opaque blob. Compile-checked against the [View] schema via --view-schema, resolving to
    // the typed ViewNounId::EditorNouns_layerMask member (seam M2b/M2c). Targets the editor layer
    // mask — the noun LayerControllerViewDataProvider serves — so AppFlowLoader wires the Data
    // action end-to-end through IViewDataProvider (M2c). EditorNouns_layerMask is unambiguous, so
    // the bare "layerMask" would resolve too; kept qualified for clarity.
    [FlowDataTarget(nameof(FlowAction.Data), "EditorNouns.layerMask")]
    public static class DataToLayerMask { }
}
