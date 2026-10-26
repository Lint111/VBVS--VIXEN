#pragma once
// R766: the one docking-panel model for every VIXEN app (editor and game). A panel is DECLARED (id,
// bound IView, its own RmlUi document, default region and size rules) and the dock decides where it
// sits: edge regions holding split tab stacks, an overlay that fills the viewport (the game HUD), and
// floating panels. The model is pure data — no RmlUi, no Vulkan — so the layout rules are testable
// headless; PanelDockHost applies a computed DockFrame to an IUiCompositionHost.
//
// Layout persistence is settings data: one row per region (its size) and one row per panel (region,
// stack, order, visible, active tab, floating rect). Rows naming an undeclared panel are dropped on
// load; a declared panel missing from the rows takes its declared defaults.
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Vixen::RenderGraph {

class IView;

// Where a panel lives. Left/Right/Bottom are edge regions around the viewport; Overlay draws over the
// whole viewport rect without chrome (the game HUD); Floating panels carry their own rect.
enum class DockRegion : uint8_t { Left = 0, Right = 1, Bottom = 2, Overlay = 3, Floating = 4 };

std::string_view DockRegionName(DockRegion region);
std::optional<DockRegion> ParseDockRegion(std::string_view name);

struct PanelRect {
    float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
    bool operator==(const PanelRect&) const = default;
};

// How the panel's document joins the UI node: a Fragment is a mounted second document (the normal
// case); Primary binds the node's primary document (the HUD, which owns the HUD blob hot reload).
enum class PanelMount : uint8_t { Fragment = 0, Primary = 1 };

// What a panel declares. The app owns the bound view; several panels may share one view (one data
// model, several documents) — each panel names its own document.
struct PanelDecl {
    std::string id;           // stable id: AppFlow params, selectors ("panel-<id>-close") and layout rows
    std::string title;        // tab label
    IView* view = nullptr;    // the generated View binding (borrowed; outlives the dock)
    std::string document;     // "assets/ui/<panel>.rml" — content and style only, never page layout
    DockRegion region = DockRegion::Left;
    uint32_t stack = 0;       // default split inside the region (panels sharing a stack become tabs)
    float size = 320.0f;     // preferred region extent: width for Left/Right, height for Bottom
    float minSize = 120.0f;   // the region never shrinks below the largest visible panel minimum
    bool closable = true;
    bool header = true;       // false: a stack holding only this panel draws no tab strip (status line)
    PanelMount mount = PanelMount::Fragment;
    PanelRect floatRect{80.0f, 80.0f, 360.0f, 420.0f};
};

// One persisted panel row.
struct PanelRow {
    std::string panel;
    DockRegion region = DockRegion::Left;
    uint32_t stack = 0;       // split index inside the region (0 = first along the region's long axis)
    uint32_t order = 0;       // tab order inside the stack
    bool visible = true;
    bool active = false;      // the visible tab of its stack
    PanelRect floatRect{};
    uint32_t focus = 0;       // floating z order (higher draws on top)
    bool operator==(const PanelRow&) const = default;
};

struct RegionRow {
    DockRegion region = DockRegion::Left;
    float size = 0.0f;
    bool operator==(const RegionRow&) const = default;
};

struct DockLayoutRows {
    std::vector<RegionRow> regions;
    std::vector<PanelRow> panels;
};

// A computed frame for one window size.
struct DockTabFrame {
    std::string panel;
    std::string title;
    PanelRect tab;            // the tab button inside its stack header
    bool active = false;
    bool closable = true;
};

struct DockStackFrame {
    DockRegion region = DockRegion::Left;
    uint32_t stack = 0;
    PanelRect frame;          // header + content
    float headerHeight = 0.0f;
    std::vector<DockTabFrame> tabs;
};

struct DockPanelFrame {
    std::string panel;
    PanelRect content;        // where the panel's document body goes
    bool visible = false;     // closed panels and inactive tabs are hidden
    int z = 0;                // draw order among panel documents
};

struct DockFrame {
    PanelRect viewport;       // the area no docked panel covers
    std::vector<DockStackFrame> stacks;
    std::vector<DockPanelFrame> panels;
};

enum class DockResult : uint8_t { Ok = 0, UnknownPanel, UnknownRegion, NotClosable, InvalidSize };
std::string_view DockResultName(DockResult result);

class PanelDock {
public:
    static constexpr float kHeaderHeight = 26.0f;
    static constexpr float kTabWidth = 118.0f;
    static constexpr float kMinViewport = 160.0f;

    // Declares a panel (first-win on a duplicate id). Declaration order is the default tab order.
    bool Declare(PanelDecl decl);
    const std::vector<PanelDecl>& Declarations() const { return decls_; }
    const PanelDecl* Find(std::string_view panelId) const;

    // Resets every panel and region to its declared default.
    void ResetToDefaults();

    // Layout verbs (the AppFlow panel actions call these). Each returns Ok or why it refused; a refused
    // verb leaves the layout unchanged.
    DockResult Open(std::string_view panelId);                 // show it and make it its stack's active tab
    DockResult Close(std::string_view panelId);
    DockResult Focus(std::string_view panelId);                // activate the tab / raise the floating panel
    DockResult Move(std::string_view panelId, DockRegion region);   // dock as a tab of the region's last stack
    DockResult Split(std::string_view panelId, DockRegion region);  // dock into a new stack of the region
    DockResult Float(std::string_view panelId, PanelRect rect);
    DockResult Resize(DockRegion region, float size);

    // Settings rows (persisted layout). Apply keeps declared defaults for panels the rows don't name.
    DockLayoutRows Rows() const;
    void ApplyRows(const DockLayoutRows& rows);

    // JSON form of the rows: {"version":1,"regions":[...],"panels":[...]}.
    std::string RowsJson() const;
    bool ApplyRowsJson(std::string_view json, std::string& error);

    DockFrame Compute(float windowWidth, float windowHeight) const;

    // Bumped by every successful verb / reset / apply, so a host re-places only on change.
    uint64_t Revision() const { return revision_; }

private:
    PanelRow* RowOf(std::string_view panelId);
    const PanelRow* RowOf(std::string_view panelId) const;
    RegionRow* RegionOf(DockRegion region);
    float RegionSize(DockRegion region) const;
    uint32_t NextStack(DockRegion region) const;
    uint32_t NextOrder(DockRegion region, uint32_t stack) const;
    void ActivateOnly(PanelRow& row);
    void Normalize();   // compact stack indices, keep exactly one active visible tab per stack

    std::vector<PanelDecl> decls_;
    std::vector<PanelRow> rows_;      // parallel to decls_
    std::vector<RegionRow> regions_;  // Left, Right, Bottom
    uint32_t focusCounter_ = 0;
    uint64_t revision_ = 0;
};

}  // namespace Vixen::RenderGraph
