using Yeroket.Util.KernelFramework;

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
        public bool   isChecked;
        public string elementId;
    }

    [View]
    public struct EditorLayers {
        [ViewSection(Layout = ViewLayout.Aos)] public EditorLayerRow[] layers;
    }
}
