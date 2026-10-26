// R766: the panel dock model — declarations, the region/stack/tab/floating layout rules, the layout
// verbs the AppFlow panel actions call, and the persisted settings rows. Pure CPU (no RmlUi/Vulkan).
#include "Ui/PanelDock.h"
#include "Ui/PanelDockHost.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>

using namespace Vixen::RenderGraph;

namespace {

PanelDecl Decl(std::string id, DockRegion region, uint32_t stack = 0, float size = 300.0f) {
    PanelDecl decl;
    decl.id = id;
    decl.title = id;
    decl.document = "assets/ui/" + id + ".rml";
    decl.region = region;
    decl.stack = stack;
    decl.size = size;
    decl.minSize = 200.0f;
    return decl;
}

// The editor's declared layout (EditorApplication::DeclarePanels).
PanelDock EditorDock() {
    PanelDock dock;
    dock.Declare(Decl("outliner", DockRegion::Left, 0, 340.0f));
    dock.Declare(Decl("layers", DockRegion::Left, 1, 340.0f));
    dock.Declare(Decl("inspector", DockRegion::Right, 0, 320.0f));
    PanelDecl status = Decl("status", DockRegion::Bottom, 0, 28.0f);
    status.minSize = 24.0f;
    status.header = false;
    status.closable = false;
    dock.Declare(status);
    return dock;
}

const DockPanelFrame& PanelIn(const DockFrame& frame, std::string_view id) {
    for (const auto& panel : frame.panels) if (panel.panel == id) return panel;
    ADD_FAILURE() << "no frame for panel " << id;
    static DockPanelFrame none;
    return none;
}

bool Overlaps(const PanelRect& a, const PanelRect& b) {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}

}  // namespace

TEST(PanelDock, EditorDefaultLayoutHasThreeRegionsAndNeverCoversTheViewport) {
    const PanelDock dock = EditorDock();
    const DockFrame frame = dock.Compute(1280.0f, 720.0f);

    EXPECT_EQ(frame.viewport, (PanelRect{340.0f, 0.0f, 1280.0f - 340.0f - 320.0f, 720.0f - 28.0f}));
    ASSERT_EQ(frame.stacks.size(), 4u);  // outliner, layers (left split), inspector, status
    for (const auto& panel : frame.panels) {
        EXPECT_TRUE(panel.visible) << panel.panel;
        EXPECT_FALSE(Overlaps(panel.content, frame.viewport)) << panel.panel << " covers the viewport";
    }
    // The left region splits vertically between its two stacks; each has a tab strip.
    const auto& outliner = PanelIn(frame, "outliner");
    const auto& layers = PanelIn(frame, "layers");
    EXPECT_FLOAT_EQ(outliner.content.y, PanelDock::kHeaderHeight);
    EXPECT_FLOAT_EQ(layers.content.y, (720.0f - 28.0f) / 2.0f + PanelDock::kHeaderHeight);
    // The status line spans the full width with no tab strip.
    const auto& status = PanelIn(frame, "status");
    EXPECT_EQ(status.content, (PanelRect{0.0f, 692.0f, 1280.0f, 28.0f}));
}

TEST(PanelDock, MoveTabsIntoTheRegionsLastStackAndOnlyTheActiveTabShows) {
    PanelDock dock = EditorDock();
    ASSERT_EQ(dock.Move("inspector", DockRegion::Left), DockResult::Ok);
    const DockFrame frame = dock.Compute(1280.0f, 720.0f);

    // The right region emptied, so the viewport grows to the window's right edge.
    EXPECT_FLOAT_EQ(frame.viewport.x + frame.viewport.width, 1280.0f);
    // inspector joined the layers stack (left stack 1) as the active tab; layers is hidden behind it.
    const auto rows = dock.Rows();
    for (const auto& row : rows.panels) {
        if (row.panel == "inspector") { EXPECT_EQ(row.region, DockRegion::Left); EXPECT_EQ(row.stack, 1u); EXPECT_TRUE(row.active); }
        if (row.panel == "layers") EXPECT_FALSE(row.active);
    }
    EXPECT_TRUE(PanelIn(frame, "inspector").visible);
    EXPECT_FALSE(PanelIn(frame, "layers").visible);
    EXPECT_EQ(PanelIn(frame, "inspector").content, PanelIn(frame, "layers").content);

    // Focusing the hidden tab swaps which document shows.
    ASSERT_EQ(dock.Focus("layers"), DockResult::Ok);
    const DockFrame refocused = dock.Compute(1280.0f, 720.0f);
    EXPECT_TRUE(PanelIn(refocused, "layers").visible);
    EXPECT_FALSE(PanelIn(refocused, "inspector").visible);
}

TEST(PanelDock, SplitCloseOpenFloatAndResize) {
    PanelDock dock = EditorDock();
    ASSERT_EQ(dock.Split("outliner", DockRegion::Right), DockResult::Ok);
    DockFrame frame = dock.Compute(1280.0f, 720.0f);
    // Right region now holds two stacks, one above the other.
    EXPECT_LT(PanelIn(frame, "inspector").content.y, PanelIn(frame, "outliner").content.y);
    EXPECT_FLOAT_EQ(PanelIn(frame, "outliner").content.x, PanelIn(frame, "inspector").content.x);

    ASSERT_EQ(dock.Close("inspector"), DockResult::Ok);
    EXPECT_FALSE(PanelIn(dock.Compute(1280.0f, 720.0f), "inspector").visible);
    EXPECT_EQ(dock.Close("status"), DockResult::NotClosable);
    ASSERT_EQ(dock.Open("inspector"), DockResult::Ok);
    EXPECT_TRUE(PanelIn(dock.Compute(1280.0f, 720.0f), "inspector").visible);

    ASSERT_EQ(dock.Float("layers", {500.0f, 100.0f, 300.0f, 260.0f}), DockResult::Ok);
    frame = dock.Compute(1280.0f, 720.0f);
    EXPECT_EQ(PanelIn(frame, "layers").content, (PanelRect{500.0f, 100.0f + PanelDock::kHeaderHeight, 300.0f,
                                                            260.0f - PanelDock::kHeaderHeight}));
    EXPECT_GT(PanelIn(frame, "layers").z, PanelIn(frame, "inspector").z);  // floating draws on top
    EXPECT_EQ(dock.Float("layers", {0.0f, 0.0f, 0.0f, 10.0f}), DockResult::InvalidSize);

    ASSERT_EQ(dock.Resize(DockRegion::Right, 400.0f), DockResult::Ok);
    EXPECT_FLOAT_EQ(dock.Compute(1280.0f, 720.0f).viewport.width, 1280.0f - 400.0f);  // left region emptied
    EXPECT_EQ(dock.Resize(DockRegion::Overlay, 10.0f), DockResult::UnknownRegion);
    EXPECT_EQ(dock.Resize(DockRegion::Left, -1.0f), DockResult::InvalidSize);
    EXPECT_EQ(dock.Open("missing"), DockResult::UnknownPanel);
}

TEST(PanelDock, RegionsShrinkToKeepTheMinimumViewport) {
    PanelDock dock = EditorDock();
    ASSERT_EQ(dock.Resize(DockRegion::Left, 900.0f), DockResult::Ok);
    ASSERT_EQ(dock.Resize(DockRegion::Right, 900.0f), DockResult::Ok);
    const DockFrame frame = dock.Compute(1280.0f, 720.0f);
    EXPECT_FLOAT_EQ(frame.viewport.width, PanelDock::kMinViewport);
}

TEST(PanelDock, RowsRoundTripThroughJsonAndIgnoreUndeclaredPanels) {
    PanelDock edited = EditorDock();
    edited.Move("inspector", DockRegion::Bottom);
    edited.Float("outliner", {40.0f, 50.0f, 320.0f, 300.0f});
    edited.Close("layers");
    edited.Resize(DockRegion::Bottom, 180.0f);
    const std::string json = edited.RowsJson();

    PanelDock restored = EditorDock();
    std::string error;
    ASSERT_TRUE(restored.ApplyRowsJson(json, error)) << error;
    EXPECT_EQ(restored.RowsJson(), json);
    EXPECT_EQ(restored.Compute(1280.0f, 720.0f).viewport, edited.Compute(1280.0f, 720.0f).viewport);

    // A row for a panel this app does not declare is dropped; the declared panels keep their rows.
    const std::string foreign = R"({"version":1,"regions":[],"panels":[{"panel":"hud","region":"overlay"}]})";
    PanelDock other = EditorDock();
    ASSERT_TRUE(other.ApplyRowsJson(foreign, error));
    EXPECT_EQ(other.Rows().panels.size(), 4u);
    EXPECT_FALSE(other.ApplyRowsJson("not json", error));
    EXPECT_FALSE(other.ApplyRowsJson(R"({"version":2})", error));
}

TEST(PanelDock, OverlayPanelsFillTheViewportWithoutChrome) {
    PanelDock dock;
    PanelDecl hud = Decl("hud", DockRegion::Overlay);
    hud.header = false;
    hud.mount = PanelMount::Primary;
    dock.Declare(hud);
    dock.Declare(Decl("building", DockRegion::Overlay));
    const DockFrame frame = dock.Compute(1920.0f, 1080.0f);
    EXPECT_TRUE(frame.stacks.empty());
    EXPECT_EQ(PanelIn(frame, "hud").content, (PanelRect{0.0f, 0.0f, 1920.0f, 1080.0f}));
    EXPECT_EQ(PanelIn(frame, "building").content, (PanelRect{0.0f, 0.0f, 1920.0f, 1080.0f}));
    EXPECT_FALSE(dock.Declare(Decl("hud", DockRegion::Left)));  // first-win on a duplicate id
}

namespace {

// Records what the dock asks of the UI node, with no RmlUi behind it.
class RecordingHost final : public IUiCompositionHost {
public:
    MountHandle Mount(IView&) override { return ++mounts; }
    MountHandle Mount(IView&, const char*) override { return ++mounts; }
    void BindPrimary(IView&) override { ++primaries; }
    void Place(MountHandle, const PanelRect&, bool, int) override { ++places; }
    bool Extent(float& width, float& height) const override { width = 1280.0f; height = 720.0f; return true; }
    void Unmount(MountHandle) override {}
    void MarkMountedDirty(MountHandle, const char*) override {}
    bool IsMounted(MountHandle) const override { return true; }
    MountHandle mounts = 0;
    int primaries = 0;
    int places = 0;
};

// Any non-null view: the recording host never touches it.
class NullView final : public IView {
public:
    const char* ModelName() const override { return "null"; }
    void Register(Rml::DataModelConstructor&) override {}
    const char* DocumentPath() const override { return ""; }
};

}  // namespace

// R761: the dock's per-frame path. A steady frame (no verb, same window size) places nothing; only a
// layout change re-places the chrome and every panel. Prints the measured per-frame cost.
TEST(PanelDockHost, SteadyFramesPlaceNothingAndOnlyChangesReplace) {
    NullView view;
    PanelDockHost host;
    for (const char* id : {"outliner", "layers", "inspector", "status"}) {
        PanelDecl decl = Decl(id, std::string_view(id) == "status" ? DockRegion::Bottom
                                  : std::string_view(id) == "inspector" ? DockRegion::Right : DockRegion::Left,
                              std::string_view(id) == "layers" ? 1u : 0u);
        decl.view = &view;
        host.Dock().Declare(decl);
    }
    RecordingHost node;
    host.Attach(node);
    EXPECT_EQ(node.mounts, 5u);  // chrome + four panels
    ASSERT_TRUE(host.Update());
    EXPECT_EQ(node.places, 5);

    constexpr int kSteadyFrames = 200000;
    const auto steadyStart = std::chrono::steady_clock::now();
    for (int i = 0; i < kSteadyFrames; ++i) ASSERT_FALSE(host.Update());
    const double steadyNs = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - steadyStart).count() / kSteadyFrames;
    EXPECT_EQ(node.places, 5) << "a steady frame must not re-place documents";

    constexpr int kChanges = 2000;
    const auto changeStart = std::chrono::steady_clock::now();
    for (int i = 0; i < kChanges; ++i) {
        host.Dock().Resize(DockRegion::Left, 300.0f + static_cast<float>(i % 50));
        ASSERT_TRUE(host.Update());
    }
    const double changeUs = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - changeStart).count() / kChanges;
    EXPECT_EQ(node.places, 5 + 5 * kChanges);
    std::printf("[panel-dock-cost] steady frame %.1f ns; layout change (verb + compute + view rows + 5 places) %.2f us\n",
                steadyNs, changeUs);
}
