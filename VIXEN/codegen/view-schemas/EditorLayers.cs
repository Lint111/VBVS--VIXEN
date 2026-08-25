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

        // Inc-Ovr (design §5b Override proof): the framework generates NO read/write/reconcile
        // logic for this field -- only a forward-declared hook,
        // BindEditorLayersModel_activeLayerCountOverride, that a consumer must define by hand
        // (Vixen::App::EditorLayersView.h). A top-level scalar, separate from the row-level
        // isChecked Projection proof above, so the two proofs don't collide on the same binding.
        [Overridden]
        public int activeLayerCount;
    }
}
