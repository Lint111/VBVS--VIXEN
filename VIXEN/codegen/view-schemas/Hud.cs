using Yeroket.Util.KernelFramework;

namespace Vixen.ViewSchemas
{
    public struct HudFaction {
        public string name;
        public float  grievance;
        public bool   focused;
        public bool   known;
        public bool   inLens;
        public bool   recentChanged;    // derived pulse flag (recentEventAge < K) — kept for the .changed CSS
        public int    recentEventAge;   // RESTORED: raw ticks-since-most-recent-event, capped 255 (was demoted to recentChanged)
    }
    public struct HudEvent {
        public string kind;
        public int    tick;
    }

    [View]
    public struct Hud {
        public int    tick;
        public int    bodyCount;
        public string activeLensName;
        public int    activeLensCount;
        [ViewSection(Layout = ViewLayout.Soa)] public HudFaction[] factions;
        [ViewSection(Layout = ViewLayout.Soa)] public HudEvent[]   events;
        // T1 inspect panel: the selected-body/entity detail. inspectSelected gates the panel's
        // data-if in hud.rml; the rest are meaningless when inspectSelected == 0.
        public int    inspectSelected;   // 1 = a body/entity is selected (bound as a bool via data-if)
        public string inspectName;       // selected entity's display name
        public string inspectCause;      // causal "because" sentence (empty when none)
        // RESTORED inspect detail (composed from the HudInspect section by the view-query layer):
        // faction diplomacy readout. Meaningful only when inspectSelected != 0. (Building telemetry
        // is NOT a HUD field — it feeds the dedicated BuildingInspectorView via the same query
        // layer; adding it here would duplicate that existing mounted view.)
        public float  inspectMaxGrievance;  // RESTORED
        public float  inspectStrength;      // RESTORED
        public string inspectTopRelName;    // RESTORED
        public float  inspectTopRelSig;     // RESTORED
    }
}
