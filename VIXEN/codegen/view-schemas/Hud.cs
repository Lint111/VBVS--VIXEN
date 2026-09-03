using Yeroket.Util.KernelFramework;

namespace Vixen.ViewSchemas
{
    public struct HudFaction {
        public string name;
        public float  grievance;
        public bool   focused;
        public bool   known;
        public bool   inLens;
        public bool   recentChanged;
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
    }
}
