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
        // E3 (catalogue-free half): layer blend / name / placement, selection, and reopening the saved file.
        SetLayerBlend = 24, RenameLayer = 25, SetLayerTransform = 26, ReopenSaved = 27,
        SelectLayer = 28, NudgeLayerField = 29,
        // sn-editorinput1 block (R765 camera/input/typed-parameter actions). Camera actions are not document edits.
        OrbitCamera = 30, PanCamera = 31, ZoomCamera = 32, FrameAsset = 33, SetViewAngle = 34,
        SelectViewAngle0 = 35, SelectViewAngle1 = 36, SelectViewAngle2 = 37, SelectViewAngle3 = 38,
        SetParameter = 39, ResetParameter = 40, ScrubParameter = 41,
        // sn-editorshape2 block (R872.1 catalogue consumer): names are validated against the generated recipe-op catalogue.
        CreatePrimitiveLayer = 42, SetLayerOp = 43, SetLayerPrimitiveField = 44,
    }

    // Typed key vocabulary (design §3.1/§5.2) — KeyChord{KeyId,KeyMod} is the ONLY boundary;
    // the host-side glfw-keycode→KeyId map (M4) is the sole raw-string/int crossing.
    [FlowKeyEnum]
    public enum KeyId : ushort
    {
        None = 0, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z, Escape,
        // sn-editorinput1 block (append-only): every key the host maps from GLFW (KeyMap.h).
        Digit0, Digit1, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8, Digit9,
        F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
        Left, Right, Up, Down, Delete, Backspace, Tab, Space, Enter, Home, End, PageUp, PageDown
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

    // E3: typed layer edits. Blend radius and placement are document edits (one undo entry each);
    // the placement is position, Euler rotation in degrees and per-axis scale.
    [FlowActionParams(nameof(FlowAction.SetLayerBlend))]
    public static class SetLayerBlendParams
    {
        public const string Param0Name = "layerIndex";
        public const int Param0Type = (int)FlowParamType.Int;
        public const string Param1Name = "radius";
        public const int Param1Type = (int)FlowParamType.Float;
    }

    [FlowActionParams(nameof(FlowAction.RenameLayer))]
    public static class RenameLayerParams
    {
        public const string Param0Name = "layerIndex";
        public const int Param0Type = (int)FlowParamType.Int;
        public const string Param1Name = "name";
        public const int Param1Type = (int)FlowParamType.String;
    }

    // Any placement component the call does not name keeps the layer's current value.
    [FlowActionParams(nameof(FlowAction.SetLayerTransform))]
    public static class SetLayerTransformParams
    {
        public const string Param0Name = "layerIndex";
        public const int Param0Type = (int)FlowParamType.Int;
        public const string Param1Name = "px";
        public const int Param1Type = (int)FlowParamType.Float;
        public const string Param2Name = "py";
        public const int Param2Type = (int)FlowParamType.Float;
        public const string Param3Name = "pz";
        public const int Param3Type = (int)FlowParamType.Float;
        public const string Param4Name = "rx";
        public const int Param4Type = (int)FlowParamType.Float;
        public const string Param5Name = "ry";
        public const int Param5Type = (int)FlowParamType.Float;
        public const string Param6Name = "rz";
        public const int Param6Type = (int)FlowParamType.Float;
        public const string Param7Name = "sx";
        public const int Param7Type = (int)FlowParamType.Float;
        public const string Param8Name = "sy";
        public const int Param8Type = (int)FlowParamType.Float;
        public const string Param9Name = "sz";
        public const int Param9Type = (int)FlowParamType.Float;
    }

    // The layer whose placement the inspector shows. Selection is view state, not a document edit.
    [FlowActionParams(nameof(FlowAction.SelectLayer))]
    public static class SelectLayerParams
    {
        public const string Param0Name = "layerIndex";
        public const int Param0Type = (int)FlowParamType.Int;
    }

    // A stepper button: field is "<blend|px|py|pz|rx|ry|rz|sx|sy|sz>-<up|down>"; the step is one fixed
    // increment of that field. sn-editorshape2: "<argument>.<component>-<up|down>" (for example
    // "halfExtents.x-up") steps one component of the layer's primitive argument named in the
    // generated recipe-op catalogue.
    [FlowActionParams(nameof(FlowAction.NudgeLayerField))]
    public static class NudgeLayerFieldParams
    {
        public const string Param0Name = "layerIndex";
        public const int Param0Type = (int)FlowParamType.Int;
        public const string Param1Name = "field";
        public const int Param1Type = (int)FlowParamType.String;
    }

    // R872.1: append a layer holding one catalogue primitive at its declared defaults.
    [FlowActionParams(nameof(FlowAction.CreatePrimitiveLayer))]
    public static class CreatePrimitiveLayerParams
    {
        public const string Param0Name = "primitive";
        public const int Param0Type = (int)FlowParamType.String;
    }

    // R872.1: the layer's combine op, by its name in the generated layer-op table.
    [FlowActionParams(nameof(FlowAction.SetLayerOp))]
    public static class SetLayerOpParams
    {
        public const string Param0Name = "layerIndex";
        public const int Param0Type = (int)FlowParamType.Int;
        public const string Param1Name = "op";
        public const int Param1Type = (int)FlowParamType.String;
    }

    // R872.1: one InstructionData argument of the layer's primitive, by its catalogue name; the
    // components beyond the argument's width are ignored, and a component the call does not name keeps its current value.
    [FlowActionParams(nameof(FlowAction.SetLayerPrimitiveField))]
    public static class SetLayerPrimitiveFieldParams
    {
        public const string Param0Name = "layerIndex";
        public const int Param0Type = (int)FlowParamType.Int;
        public const string Param1Name = "field";
        public const int Param1Type = (int)FlowParamType.String;
        public const string Param2Name = "x";
        public const int Param2Type = (int)FlowParamType.Float;
        public const string Param3Name = "y";
        public const int Param3Type = (int)FlowParamType.Float;
        public const string Param4Name = "z";
        public const int Param4Type = (int)FlowParamType.Float;
        public const string Param5Name = "w";
        public const int Param5Type = (int)FlowParamType.Float;
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

    // sn-editorinput1 block: camera actions (radians / world units) and typed parameter entry.
    [FlowActionParams(nameof(FlowAction.OrbitCamera))]
    public static class OrbitCameraParams
    {
        public const string Param0Name = "dYaw";
        public const int Param0Type = (int)FlowParamType.Float;
        public const string Param1Name = "dPitch";
        public const int Param1Type = (int)FlowParamType.Float;
    }

    [FlowActionParams(nameof(FlowAction.PanCamera))]
    public static class PanCameraParams
    {
        public const string Param0Name = "dx";
        public const int Param0Type = (int)FlowParamType.Float;
        public const string Param1Name = "dy";
        public const int Param1Type = (int)FlowParamType.Float;
    }

    // Positive delta moves the camera toward the orbit centre.
    [FlowActionParams(nameof(FlowAction.ZoomCamera))]
    public static class ZoomCameraParams
    {
        public const string Param0Name = "delta";
        public const int Param0Type = (int)FlowParamType.Float;
    }

    [FlowActionParams(nameof(FlowAction.SetViewAngle))]
    public static class SetViewAngleParams
    {
        public const string Param0Name = "index";
        public const int Param0Type = (int)FlowParamType.Int;
    }

    // Clamped to the declared parameter range; one undo step.
    [FlowActionParams(nameof(FlowAction.SetParameter))]
    public static class SetParameterParams
    {
        public const string Param0Name = "parameterIndex";
        public const int Param0Type = (int)FlowParamType.Int;
        public const string Param1Name = "value";
        public const int Param1Type = (int)FlowParamType.Float;
    }

    [FlowActionParams(nameof(FlowAction.ResetParameter))]
    public static class ResetParameterParams
    {
        public const string Param0Name = "parameterIndex";
        public const int Param0Type = (int)FlowParamType.Int;
    }

    // Pressing the value label starts a horizontal drag-scrub of that parameter (one undo step per gesture).
    [FlowActionParams(nameof(FlowAction.ScrubParameter))]
    public static class ScrubParameterParams
    {
        public const string Param0Name = "parameterIndex";
        public const int Param0Type = (int)FlowParamType.Int;
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

    [FlowElementTrigger(nameof(FlowAction.ScrubParameter))]
    public static class ScrubParameterTrigger
    {
        public const string Element = "parameter-{index}-value";
        public const string ParamName = "parameterIndex";
        public const string On = "click";
    }

    [FlowElementTrigger(nameof(FlowAction.ResetParameter))]
    public static class ResetParameterTrigger
    {
        public const string Element = "parameter-{index}-reset";
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

    [FlowElementTrigger(nameof(FlowAction.SelectLayer))]
    public static class SelectLayerTrigger
    {
        public const string Element = "layer-{index}-select";
        public const string ParamName = "layerIndex";
        public const string On = "click";
    }

    // "layer-2-nudge-blend-up", "layer-2-nudge-px-down": the second placeholder binds the `field` param.
    [FlowElementTrigger(nameof(FlowAction.NudgeLayerField))]
    public static class NudgeLayerFieldTrigger
    {
        public const string Element = "layer-{index}-nudge-{field}";
        public const string ParamName = "layerIndex";
        public const string On = "click";
    }

    // "primitive-Sphere-add": the picker row's id carries the catalogue primitive name.
    [FlowElementTrigger(nameof(FlowAction.CreatePrimitiveLayer))]
    public static class CreatePrimitiveLayerTrigger
    {
        public const string Element = "primitive-{primitive}-add";
        public const string ParamName = "primitive";
        public const string On = "click";
    }

    // "layer-2-op-subtract": the second placeholder binds the `op` param.
    [FlowElementTrigger(nameof(FlowAction.SetLayerOp))]
    public static class SetLayerOpTrigger
    {
        public const string Element = "layer-{index}-op-{op}";
        public const string ParamName = "layerIndex";
        public const string On = "click";
    }

    // sn-rmlinput1 block: typed value entry. A text control's element id is one of these patterns with its
    // value slot left as the literal "{<param>}" ("parameter-0-set-{value}"); on commit (Enter, or blur after
    // an edit) the UI fills the slot with the typed text and dispatches the selector, so the last placeholder
    // binds the typed value to the action parameter it names. Nothing here is a new declaration form: they
    // are ordinary element triggers whose second placeholder names a parameter, like panel-{panelId}-to-{region}.
    [FlowElementTrigger(nameof(FlowAction.SetParameter))]
    public static class SetParameterTypedTrigger
    {
        public const string Element = "parameter-{index}-set-{value}";
        public const string ParamName = "parameterIndex";
        public const string On = "click";
    }

    [FlowElementTrigger(nameof(FlowAction.SetLayerBlend))]
    public static class SetLayerBlendTypedTrigger
    {
        public const string Element = "layer-{index}-blend-{radius}";
        public const string ParamName = "layerIndex";
        public const string On = "click";
    }

    [FlowElementTrigger(nameof(FlowAction.RenameLayer))]
    public static class RenameLayerTypedTrigger
    {
        public const string Element = "layer-{index}-name-{name}";
        public const string ParamName = "layerIndex";
        public const string On = "click";
    }

    // One component of a catalogue primitive argument; the other components keep their current values.
    [FlowElementTrigger(nameof(FlowAction.SetLayerPrimitiveField))]
    public static class SetLayerPrimitiveFieldXTrigger
    {
        public const string Element = "layer-{index}-field-{field}-x-{x}";
        public const string ParamName = "layerIndex";
        public const string On = "click";
    }

    [FlowElementTrigger(nameof(FlowAction.SetLayerPrimitiveField))]
    public static class SetLayerPrimitiveFieldYTrigger
    {
        public const string Element = "layer-{index}-field-{field}-y-{y}";
        public const string ParamName = "layerIndex";
        public const string On = "click";
    }

    [FlowElementTrigger(nameof(FlowAction.SetLayerPrimitiveField))]
    public static class SetLayerPrimitiveFieldZTrigger
    {
        public const string Element = "layer-{index}-field-{field}-z-{z}";
        public const string ParamName = "layerIndex";
        public const string On = "click";
    }

    [FlowElementTrigger(nameof(FlowAction.SetLayerPrimitiveField))]
    public static class SetLayerPrimitiveFieldWTrigger
    {
        public const string Element = "layer-{index}-field-{field}-w-{w}";
        public const string ParamName = "layerIndex";
        public const string On = "click";
    }

    // One placement component of a layer; the other eight keep their current values.
    [FlowElementTrigger(nameof(FlowAction.SetLayerTransform))]
    public static class SetLayerTransformPXTrigger
    {
        public const string Element = "layer-{index}-placement-px-{px}";
        public const string ParamName = "layerIndex";
        public const string On = "click";
    }

    [FlowElementTrigger(nameof(FlowAction.SetLayerTransform))]
    public static class SetLayerTransformPYTrigger
    {
        public const string Element = "layer-{index}-placement-py-{py}";
        public const string ParamName = "layerIndex";
        public const string On = "click";
    }

    [FlowElementTrigger(nameof(FlowAction.SetLayerTransform))]
    public static class SetLayerTransformPZTrigger
    {
        public const string Element = "layer-{index}-placement-pz-{pz}";
        public const string ParamName = "layerIndex";
        public const string On = "click";
    }

    [FlowElementTrigger(nameof(FlowAction.SetLayerTransform))]
    public static class SetLayerTransformRXTrigger
    {
        public const string Element = "layer-{index}-placement-rx-{rx}";
        public const string ParamName = "layerIndex";
        public const string On = "click";
    }

    [FlowElementTrigger(nameof(FlowAction.SetLayerTransform))]
    public static class SetLayerTransformRYTrigger
    {
        public const string Element = "layer-{index}-placement-ry-{ry}";
        public const string ParamName = "layerIndex";
        public const string On = "click";
    }

    [FlowElementTrigger(nameof(FlowAction.SetLayerTransform))]
    public static class SetLayerTransformRZTrigger
    {
        public const string Element = "layer-{index}-placement-rz-{rz}";
        public const string ParamName = "layerIndex";
        public const string On = "click";
    }

    [FlowElementTrigger(nameof(FlowAction.SetLayerTransform))]
    public static class SetLayerTransformSXTrigger
    {
        public const string Element = "layer-{index}-placement-sx-{sx}";
        public const string ParamName = "layerIndex";
        public const string On = "click";
    }

    [FlowElementTrigger(nameof(FlowAction.SetLayerTransform))]
    public static class SetLayerTransformSYTrigger
    {
        public const string Element = "layer-{index}-placement-sy-{sy}";
        public const string ParamName = "layerIndex";
        public const string On = "click";
    }

    [FlowElementTrigger(nameof(FlowAction.SetLayerTransform))]
    public static class SetLayerTransformSZTrigger
    {
        public const string Element = "layer-{index}-placement-sz-{sz}";
        public const string ParamName = "layerIndex";
        public const string On = "click";
    }

    [FlowElementTrigger(nameof(FlowAction.ReopenSaved))]
    public static class ReopenSavedButtonTrigger
    {
        public const string Element = "reopen-button";
        public const string ParamName = "";
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
    public static class SaveKey { public const KeyId Key = KeyId.S; public const KeyMod Mods = KeyMod.Ctrl; }

    // sn-editorinput1 block: camera key defaults (plain keys; the host skips them while a text field has focus).
    [FlowKeyDefault(nameof(FlowAction.FrameAsset), FlowScope.Global)]
    public static class FrameAssetKey { public const KeyId Key = KeyId.F; public const KeyMod Mods = KeyMod.None; }

    // A key default carries no action params, so the four look-dev angles bind through one param-less action each
    // (AppFlow gap: key defaults cannot carry constant params; see the sn-editorinput1 report).
    [FlowKeyDefault(nameof(FlowAction.SelectViewAngle0), FlowScope.Global)]
    public static class ViewAngle0Key { public const KeyId Key = KeyId.Digit1; public const KeyMod Mods = KeyMod.None; }

    [FlowKeyDefault(nameof(FlowAction.SelectViewAngle1), FlowScope.Global)]
    public static class ViewAngle1Key { public const KeyId Key = KeyId.Digit2; public const KeyMod Mods = KeyMod.None; }

    [FlowKeyDefault(nameof(FlowAction.SelectViewAngle2), FlowScope.Global)]
    public static class ViewAngle2Key { public const KeyId Key = KeyId.Digit3; public const KeyMod Mods = KeyMod.None; }

    [FlowKeyDefault(nameof(FlowAction.SelectViewAngle3), FlowScope.Global)]
    public static class ViewAngle3Key { public const KeyId Key = KeyId.Digit4; public const KeyMod Mods = KeyMod.None; }

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
