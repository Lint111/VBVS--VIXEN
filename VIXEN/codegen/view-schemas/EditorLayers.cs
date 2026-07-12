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
        // Inc-Ovr (design §5a Projection proof): re-derives EditorLayersView::PopulateFromMask's
        // hand-written `((mask >> i) & 1u) != 0u` through the schema-declared Projection mechanism
        // instead of a hand loop -- AppFlowCallables.bitAt is the [KernelCallable] transform body
        // (transplanted to Vixen::AppFlow::Generated::bitAt by --callable-cpp), this attribute only
        // marks WHICH field it computes.
        [Projected(typeof(AppFlowCallables), nameof(AppFlowCallables.bitAt))]
        public bool   isChecked;
        public string elementId;
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
