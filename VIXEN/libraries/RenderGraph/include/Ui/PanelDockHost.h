#pragma once
// R766: binds one app's PanelDock to its UI node. Attach() mounts the dock chrome (dock.rml, bound to
// the generated PanelLayout View) and every declared panel document through IUiCompositionHost — the
// existing HUD/fragment mount path — so per-app mounting is exactly the app's declarations. Update()
// re-places the documents when the layout or the window size changed. The layout persists as settings
// rows (PanelDock::RowsJson) at LayoutPath().
//
// RmlUi-free header: gaia-touching app TUs include this; PanelDockHost.cpp holds the RmlUi side.
#include "Ui/IView.h"
#include "Ui/PanelDock.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Vixen::RenderGraph {

class PanelLayoutView;

class PanelDockHost {
public:
    PanelDockHost();
    ~PanelDockHost();
    PanelDockHost(const PanelDockHost&) = delete;
    PanelDockHost& operator=(const PanelDockHost&) = delete;

    PanelDock& Dock() { return dock_; }
    const PanelDock& Dock() const { return dock_; }

    // Persisted layout location. Load applies saved rows over the declared defaults (a missing file is
    // not an error); Save writes the current rows atomically.
    void SetLayoutPath(std::filesystem::path path) { layoutPath_ = std::move(path); }
    const std::filesystem::path& LayoutPath() const { return layoutPath_; }
    bool LoadLayout(std::string& error);
    bool SaveLayout(std::string& error) const;
    // Saves only when the persisted layout changed since the last save: a verb on a transient panel (a flyout
    // opening, being dragged, closing) writes nothing. Returns false only on a write error.
    bool SaveLayoutIfChanged(std::string& error);

    // Opens a floating panel beside the element `anchorElementId` (the control that opened it): its rect is read from
    // the live layout, the dock places the panel next to it inside the window, away from the viewport centre. With no
    // anchor, no such element, or a non-floating panel this is Open.
    DockResult OpenAnchored(std::string_view panelId, const std::string& anchorElementId);

    // One pointer sample per frame (window pixels, left button state): drags a floating panel by its header strip
    // and an edge region by its splitter. The live gesture is applied to the dock without a save; the returned
    // step's `commit` is the gesture to emit as its verb (FloatPanel / ResizeRegion) on release, and `captured`
    // tells the app the pointer belongs to the dock.
    DockDragStep StepDrag(float pointerX, float pointerY, bool leftDown);

    // Mounts the chrome and every declared panel on `host`. Call once, after the declarations.
    void Attach(IUiCompositionHost& host);
    bool Attached() const { return host_ != nullptr; }

    // Re-places the documents when the dock revision or the host extent changed. Returns true when it
    // placed (the layout View was refreshed).
    bool Update();

    // Mount handle of a declared panel (kPrimaryDocument for a Primary panel, 0 if not mounted).
    IUiCompositionHost::MountHandle HandleOf(std::string_view panelId) const;

    // The last computed frame and the layout View's rows as JSON (what agents read back):
    // {"viewport":{...},"stacks":[...],"panels":[...],"rows":{...persisted rows...}}.
    const DockFrame& Frame() const { return frame_; }
    std::string LayoutJson() const;

private:
    PanelDock dock_;
    std::unique_ptr<PanelLayoutView> chromeView_;
    IUiCompositionHost* host_ = nullptr;
    IUiCompositionHost::MountHandle chromeHandle_ = 0;
    std::unordered_map<std::string, IUiCompositionHost::MountHandle> handles_;
    std::filesystem::path layoutPath_;
    DockFrame frame_;
    PanelDockDrag drag_;
    uint64_t savedPersistedRevision_ = ~uint64_t{0};
    uint64_t placedRevision_ = ~uint64_t{0};
    float placedWidth_ = -1.0f;
    float placedHeight_ = -1.0f;
};

}  // namespace Vixen::RenderGraph
