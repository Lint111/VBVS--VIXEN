using Yeroket.Util.KernelFramework;
using Vixen.AppFlow.Reference;

namespace Vixen.ViewSchemas
{
    // One editor layer row: the static name/op text (already in editor.rml) plus the
    // mask-derived checked bool (Inc-A2's new model->view path) and the row's own click-target
    // element id (elementId lets editor.rml bind "id" off the row instead of re-deriving
    // "layer-<index>-toggle" via string concat in RML — the row carries its own identity, same
    // as HudFaction/HudEvent carry their own display fields).
    public struct EditorLayerRow {
        public string name;
        public string op;
        // Inc-Ovr (design §5a Projection proof): this attribute is consumed by the C++ view emitter
        // to generate ComputeEditorLayerRow_isChecked, which calls the transplanted
        // AppFlowCallables.bitAt. KEEP: removing it changes Generated/EditorLayers.g.h.
        [Projected(typeof(AppFlowCallables), nameof(AppFlowCallables.bitAt))]
        public bool   isChecked;
        public string elementId;
        public string moveUpId;
        public string moveDownId;
        public string deleteId;
        // E3: blend radius with its stepper ids, and the row's selection click target.
        public float blendRadius;
        public string blendUpId;
        public string blendDownId;
        public string selectId;
        public bool isSelected;
        // sn-rmlinput1: the typed blend-radius field's id, "layer-<i>-blend-{radius}" (the value slot is filled on commit).
        public string blendValueId;
        // R876 LM-V3: dependency marks are derived from the model's wire table. The strings are
        // display-only summaries; the exact names remain in the row title for hover/inspection.
        public string providesMark;
        public string readsMark;
        public string readByMark;
        public string dependencyTitle;
        public string provideId;
        public string soloId;
        public bool isSolo;
        public string warningMark;
        public string warningTitle;
        public string diagnostic;
        // R872.11: the material is the final row of each layer outline. The selector ids are
        // carried by the projection so the view never reconstructs action parameters.
        public string materialName;
        public string materialColor;
        public string materialClearId;
    }

    // E3: one placement field (position/rotation/scale axis) of the selected layer, with its stepper ids.
    public struct EditorTransformRow {
        public string label;
        public float value;
        public string upId;
        public string downId;
        // sn-rmlinput1: the typed field's id, "layer-<i>-placement-<key>-{<key>}".
        public string valueId;
    }

    // R872.1: one catalogue primitive of the picker ("primitive-<name>-add" is its AppFlow selector).
    // Rows come from the generated recipe-op catalogue (category Primitive), never from a hand list.
    public struct EditorPrimitiveRow {
        public string name;
        // Presentation of the opcode name (split at camel-case boundaries); ids keep the exact name.
        public string display;
        public string category;
        public string elementId;
    }

    // R872.1: one combine-op choice of the selected layer ("layer-<i>-op-<name>"), from the generated layer-op table.
    public struct EditorLayerOpRow {
        public string name;
        public string elementId;
        public bool isCurrent;
    }

    // R872.1: one component of the selected layer's primitive argument, with its declared range and
    // the ids of its nudge buttons ("layer-<i>-nudge-<argument>.<component>-<up|down>").
    public struct EditorShapeFieldRow {
        public string label;
        public float value;
        public bool hasRange;
        public float minimum;
        public float maximum;
        public string upId;
        public string downId;
        // sn-rmlinput1: the typed field's id, "layer-<i>-field-<argument>-<component>-{<component>}".
        public string valueId;
        // sn-editorpicker1: the catalogue default of this component and its reset button, which is the
        // SetLayerPrimitiveField selector with the default filled in ("layer-<i>-field-<argument>-<component>-<default>").
        public float defaultValue;
        public bool hasDefault;
        public bool isDefault;
        public string resetId;
    }

    public struct EditorParameterRow {
        public string name;
        public string unit;
        public string upId;
        public string downId;
        public float value;
        public float minimum;
        public float maximum;
        // sn-rmlinput1: the typed field's id, "parameter-<i>-set-{value}".
        public string setId;
    }

    // R765: one render-recipe document of the open pack, listed by content id. elementId is the row's
    // OpenRecipe click target ("recipe-<id>-open"); isOpen marks the document the editor is editing.
    public struct EditorRecipeRow {
        public string id;
        public string elementId;
        public bool isOpen;
    }

    // R872.6: one insertable recipe in the Add flyout's Pack recipes section. It is distinct from
    // the sidebar's open-recipe rows: its selector dispatches InsertRecipeReference, not OpenRecipe.
    public struct EditorPackRecipeRow {
        public string id;
        public string elementId;
    }

    // R876 LM-V1: the selected layer's generated graph outline. nodeIndex is the graph node index,
    // not an instruction walk owned by the view; all names and edit affordances are published by
    // the document model/application projection.
    public struct EditorNodeRow {
        public int nodeIndex;
        public string name;
        public string category;
        public string addOp;
        public string summary;
        public string elementId;
        public string addId;
        public string removeId;
        public string toggleId;
        public bool canRemove;
        public bool isExpanded;
        public string addReason;
        public string removeReason;
        public string removeTitle;
        public int indent;
        public string warning;
        public string materialName;
        public string materialColor;
    }

    public struct EditorNodeArgumentRow {
        public int nodeIndex;
        public string field;
        public string component;
        public string label;
        public float value;
        public bool hasRange;
        public float minimum;
        public float maximum;
        public float defaultValue;
        public bool hasDefault;
        public bool isDefault;
        public string valueId;
        public string upId;
        public string downId;
        public string resetId;
        public bool isPort;
        public string portType;
        public string portEmptyReason;
    }

    public struct EditorNodePortChoiceRow {
        public int nodeIndex;
        public string argument;
        public string label;
        public string elementId;
        public string removeId;
        public bool isSelected;
    }

    public struct EditorReadPortChoiceRow {
        public int nodeIndex;
        public string argument;
        public string label;
        public string elementId;
        public string removeId;
        public bool isSelected;
    }

    public struct EditorNodeWrapRow {
        public int firstNode;
        public int lastNode;
        public string op;
        public string label;
        public string elementId;
    }

    public struct EditorNodeChoiceRow {
        public string name;
        public string display;
        public string category;
        public string elementId;
    }

    // R872.10/R872.11: pack-owned material rows feed both the layer picker and the document
    // default control. elementId selects the current layer; defaultId selects the document default.
    public struct EditorMaterialRow {
        public string id;
        public string name;
        public string elementId;
        public string defaultId;
        public string color;
        public string coverage;
        public bool coverageStale;
    }

    // Provider-seam nouns for the editor face (seam M2a). A [View] noun source, NOT an RML
    // data-model: the layer bitmask the LayerControllerViewDataProvider reads/writes through
    // IViewDataProvider. Kept as its own struct (not a field on EditorLayers) so EditorLayers.g.h's
    // RmlDataModel binder is unaffected — nothing invokes --view on EditorNouns; only --view-noun-enum
    // reads its field name into ViewNounId::EditorNouns_layerMask.
    [View]
    public struct EditorNouns {
        public uint layerMask;
    }

    [View]
    public struct EditorLayers {
        [ViewSection(Layout = ViewLayout.Aos)] public EditorLayerRow[] layers;
        [ViewSection(Layout = ViewLayout.Aos)] public EditorParameterRow[] parameters;
        // E3: the selected layer's placement (nine fields) and which layer that is.
        [ViewSection(Layout = ViewLayout.Aos)] public EditorTransformRow[] transform;
        public string selectedLayerName;
        public int selectedLayer;
        // R872.1 (catalogue consumer): the primitive picker, the selected layer's combine-op choices
        // and its typed primitive fields. selectedShape is the primitive's name, or empty with
        // shapeNote saying why the fields are not editable.
        [ViewSection(Layout = ViewLayout.Aos)] public EditorPrimitiveRow[] primitives;
        [ViewSection(Layout = ViewLayout.Aos)] public EditorLayerOpRow[] opChoices;
        [ViewSection(Layout = ViewLayout.Aos)] public EditorShapeFieldRow[] shape;
        public string selectedShape;
        public string shapeNote;
        // sn-editorpicker1: the Add flyout's filter text. View state, not a document value: the picker's
        // text field writes it as the user types (the data binding is two-way) and the editor republishes
        // `primitives` filtered and ranked by it.
        public string pickerFilter;

        // Inc-Ovr (design §5b Override proof): the framework generates NO read/write/reconcile
        // logic for this field -- only a forward-declared hook,
        // BindEditorLayersModel_activeLayerCountOverride, that a consumer must define by hand
        // (Vixen::App::EditorLayersView.h) with the exact generated signature, `noexcept` included
        // (the emitter spells it; a mismatched definition fails to compile, MSVC C2382). A top-level scalar, separate from the row-level
        // isChecked Projection proof above, so the two proofs don't collide on the same binding.
        [Overridden]
        public int activeLayerCount;

        // R765: the open pack's render-recipe documents (empty when the editor runs on a loose file).
        [ViewSection(Layout = ViewLayout.Aos)] public EditorRecipeRow[] recipes;
        // R872.6: the Add flyout's second section; the editor filters this with the same picker text.
        [ViewSection(Layout = ViewLayout.Aos)] public EditorPackRecipeRow[] packRecipes;
        [ViewSection(Layout = ViewLayout.Aos)] public EditorMaterialRow[] materials;

        // R876 LM-V1: graph inspector. A raw program retains its located diagnostic and has no
        // node controls; valid programs expose generated nodes and catalogue-backed arguments.
        [ViewSection(Layout = ViewLayout.Aos)] public EditorNodeRow[] nodes;
        [ViewSection(Layout = ViewLayout.Aos)] public EditorNodeArgumentRow[] nodeArguments;
        [ViewSection(Layout = ViewLayout.Aos)] public EditorNodePortChoiceRow[] portChoices;
        [ViewSection(Layout = ViewLayout.Aos)] public EditorReadPortChoiceRow[] readPortChoices;
        [ViewSection(Layout = ViewLayout.Aos)] public EditorNodeWrapRow[] nodeWrapChoices;
        [ViewSection(Layout = ViewLayout.Aos)] public EditorNodeChoiceRow[] nodeChoices;
        public bool nodePickerActive;
        public string readPortEmptyReason;
        public bool rawProgram;
        public string rawProgramDiagnostic;

        // R766: the status-line panel (bottom region) binds these.
        public string statusDocument;
        public string statusMessage;
        public string flowState;
        public int undoDepth;
        public int redoDepth;
    }
}
