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
        [ViewSection(Layout = ViewLayout.Aos)] public HudFaction[] factions;
        [ViewSection(Layout = ViewLayout.Aos)] public HudEvent[]   events;
    }
}
