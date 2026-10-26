using Yeroket.Util.KernelFramework;

namespace Vixen.ViewSchemas
{
    // R766: one tab stack of the dock — an edge-region split or a floating panel's frame. Geometry is in
    // window pixels, computed by Vixen::RenderGraph::PanelDock; the dock chrome document draws the frame
    // and its tab strip from these rows.
    public struct PanelLayoutStack {
        public string region;
        public int stack;
        public float left;
        public float top;
        public float width;
        public float height;
        public float headerHeight;
    }

    // R766: one declared panel's placement. The element ids are the AppFlow selectors of the chrome's
    // buttons (FocusPanel, ClosePanel, MovePanel), carried on the row like EditorLayerRow's ids.
    public struct PanelLayoutPanel {
        public string panel;
        public string title;
        public string region;
        public int stack;
        public bool visible;
        public bool active;
        public bool hasTab;
        public bool closable;
        public float tabLeft;
        public float tabTop;
        public float tabWidth;
        public float tabHeight;
        public string tabId;
        public string closeId;
        public string toLeftId;
        public string toRightId;
        public string toBottomId;
    }

    // R766: the dock's layout state — what the chrome document binds and what an agent reads back over
    // the app's AppFlow channel after open/close/move/dock/resize actions.
    [View]
    public struct PanelLayout {
        [ViewSection(Layout = ViewLayout.Aos)] public PanelLayoutStack[] stacks;
        [ViewSection(Layout = ViewLayout.Aos)] public PanelLayoutPanel[] panels;
        public float viewportLeft;
        public float viewportTop;
        public float viewportWidth;
        public float viewportHeight;
    }
}
