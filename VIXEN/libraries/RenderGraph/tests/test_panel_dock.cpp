// R766: the panel dock model — declarations, the region/stack/tab/floating layout rules, the layout
// verbs the AppFlow panel actions call, and the persisted settings rows. Pure CPU (no RmlUi/Vulkan).
#include "Ui/PanelDock.h"
#include "Ui/PanelDockHost.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

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

// ---- sn-editordock2: transient / anchored / draggable / dismissible panels ---------------------------------------

namespace {

PanelDock DockWithFlyout() {
    PanelDock dock = EditorDock();
    PanelDecl flyout = Decl("picker", DockRegion::Floating);
    flyout.floatRect = {348.0f, 56.0f, 300.0f, 360.0f};
    flyout.transient = true;
    flyout.dismissible = true;
    dock.Declare(flyout);
    return dock;
}

bool Visible(const PanelDock& dock, std::string_view id) {
    for (const auto& row : dock.Rows().panels) if (row.panel == id) return row.visible;
    return false;
}

}  // namespace

TEST(PanelDockTransient, StartsClosedIsNeverPersistedAndNeverRestored) {
    PanelDock dock = DockWithFlyout();
    EXPECT_FALSE(Visible(dock, "picker")) << "a transient panel starts closed";
    ASSERT_EQ(dock.Open("picker"), DockResult::Ok);
    ASSERT_TRUE(Visible(dock, "picker"));
    const std::string json = dock.RowsJson();
    EXPECT_EQ(json.find("picker"), std::string::npos) << "not part of the persisted layout";
    EXPECT_NE(json.find("outliner"), std::string::npos);

    // A saved layout that does name it (an older file) cannot reopen it.
    PanelDock restored = DockWithFlyout();
    std::string error;
    const std::string withRow =
        R"({"version":1,"regions":[],"panels":[{"panel":"picker","region":"floating","visible":true,"active":true,"float":[1,2,300,360]}]})";
    ASSERT_TRUE(restored.ApplyRowsJson(withRow, error)) << error;
    EXPECT_FALSE(Visible(restored, "picker"));
}

TEST(PanelDockTransient, OpeningMovingAndClosingDoNotTouchTheSavedLayout) {
    PanelDock dock = DockWithFlyout();
    const uint64_t before = dock.PersistedRevision();
    const uint64_t revision = dock.Revision();
    ASSERT_EQ(dock.Open("picker"), DockResult::Ok);
    ASSERT_EQ(dock.Float("picker", {500.0f, 100.0f, 300.0f, 360.0f}), DockResult::Ok);
    ASSERT_EQ(dock.Close("picker"), DockResult::Ok);
    EXPECT_GT(dock.Revision(), revision) << "the host still re-places";
    EXPECT_EQ(dock.PersistedRevision(), before) << "nothing a layout file contains changed";
    ASSERT_EQ(dock.Resize(DockRegion::Left, 300.0f), DockResult::Ok);
    EXPECT_GT(dock.PersistedRevision(), before) << "a splitter change does";
    const uint64_t afterResize = dock.PersistedRevision();
    ASSERT_EQ(dock.Close("layers"), DockResult::Ok);
    EXPECT_GT(dock.PersistedRevision(), afterResize) << "an ordinary panel verb does";
}

TEST(PanelDockHost, SaveLayoutIfChangedWritesOnlyWhenThePersistedLayoutChanged) {
    NullView view;
    PanelDockHost host;
    for (PanelDecl decl : {Decl("left", DockRegion::Left), Decl("picker", DockRegion::Floating)}) {
        decl.view = &view;
        decl.transient = decl.id == "picker";
        host.Dock().Declare(decl);
    }
    const auto path = std::filesystem::temp_directory_path() / "editordock2-layout-test" / "layout.json";
    std::filesystem::remove_all(path.parent_path());
    host.SetLayoutPath(path);
    std::string error;
    ASSERT_TRUE(host.SaveLayoutIfChanged(error)) << error;   // the first call writes the baseline
    ASSERT_TRUE(std::filesystem::exists(path));
    const auto stamp = std::filesystem::last_write_time(path);
    std::filesystem::remove(path);
    ASSERT_EQ(host.Dock().Open("picker"), DockResult::Ok);
    ASSERT_EQ(host.Dock().Close("picker"), DockResult::Ok);
    ASSERT_TRUE(host.SaveLayoutIfChanged(error)) << error;
    EXPECT_FALSE(std::filesystem::exists(path)) << "opening and closing a flyout writes no layout";
    ASSERT_EQ(host.Dock().Resize(DockRegion::Left, 280.0f), DockResult::Ok);
    ASSERT_TRUE(host.SaveLayoutIfChanged(error)) << error;
    EXPECT_TRUE(std::filesystem::exists(path)) << "a splitter move does";
    (void)stamp;
    std::filesystem::remove_all(path.parent_path());
}

TEST(PanelDockAnchored, SitsBesideTheAnchorClampedInsideTheWindowAndAwayFromTheViewportCentre) {
    PanelDock dock = DockWithFlyout();
    // The Add button of the Layers toolbar, in the left column.
    const PanelRect add{12.0f, 380.0f, 40.0f, 22.0f};
    ASSERT_EQ(dock.OpenAnchored("picker", add, 1280.0f, 720.0f), DockResult::Ok);
    PanelRow row;
    for (const auto& candidate : dock.Rows().panels) if (candidate.panel == "picker") row = candidate;
    EXPECT_TRUE(row.visible);
    EXPECT_EQ(row.region, DockRegion::Floating);
    // On the viewport side of the Layers panel (its right edge, 340), top aligned with the button, clamped so the
    // flyout stays above the status region (the viewport's bottom edge, 692).
    const DockFrame homeFrame = dock.Compute(1280.0f, 720.0f);
    EXPECT_FLOAT_EQ(row.floatRect.x, 340.0f + 4.0f) << "beside the panel, not over its rows";
    EXPECT_FLOAT_EQ(row.floatRect.y, homeFrame.viewport.y + homeFrame.viewport.height - 360.0f);
    EXPECT_LE(row.floatRect.y + row.floatRect.height, homeFrame.viewport.y + homeFrame.viewport.height)
        << "never over the status line";
    EXPECT_GE(row.floatRect.y, 0.0f);
    // An anchor higher up keeps its top aligned with the button.
    ASSERT_EQ(dock.OpenAnchored("picker", {12.0f, 100.0f, 40.0f, 22.0f}, 1280.0f, 720.0f), DockResult::Ok);
    for (const auto& candidate : dock.Rows().panels) if (candidate.panel == "picker") row = candidate;
    EXPECT_FLOAT_EQ(row.floatRect.y, 100.0f);
    const DockFrame frame = dock.Compute(1280.0f, 720.0f);
    const float centreX = frame.viewport.x + frame.viewport.width * 0.5f;
    const float centreY = frame.viewport.y + frame.viewport.height * 0.5f;
    EXPECT_FALSE(centreX >= row.floatRect.x && centreX <= row.floatRect.x + row.floatRect.width &&
                 centreY >= row.floatRect.y && centreY <= row.floatRect.y + row.floatRect.height);
}

TEST(PanelDockAnchored, PlaceBesideFallsThroughCandidatesThatCoverTheCentreAndClampsEveryEdge) {
    // An anchor whose right-hand candidate would cover the centre: the next candidate (below) is used.
    const PanelRect anchor{500.0f, 100.0f, 80.0f, 20.0f};
    const PanelRect rect = PanelDock::PlaceBeside(anchor, 200.0f, 150.0f, 1280.0f, 720.0f, 640.0f, 120.0f);
    EXPECT_FALSE(640.0f >= rect.x && 640.0f <= rect.x + rect.width && 120.0f >= rect.y && 120.0f <= rect.y + rect.height);
    EXPECT_FLOAT_EQ(rect.x, anchor.x) << "the below candidate keeps the anchor's left edge";
    // Corners clamp inside the window.
    const PanelRect corner = PanelDock::PlaceBeside({1260.0f, 700.0f, 20.0f, 20.0f}, 300.0f, 360.0f, 1280.0f, 720.0f, 0.0f, 0.0f);
    EXPECT_GE(corner.x, 0.0f);
    EXPECT_GE(corner.y, 0.0f);
    EXPECT_LE(corner.x + corner.width, 1280.0f);
    EXPECT_LE(corner.y + corner.height, 720.0f);
    // A panel larger than the window shrinks to it.
    const PanelRect huge = PanelDock::PlaceBeside(anchor, 4000.0f, 4000.0f, 1280.0f, 720.0f, 0.0f, 0.0f);
    EXPECT_LE(huge.width, 1280.0f);
    EXPECT_LE(huge.height, 720.0f);
}

TEST(PanelDockAnchored, MissingAnchorOrNonFloatingPanelOpensWhereItIs) {
    PanelDock dock = DockWithFlyout();
    const PanelRect rectBefore = dock.Rows().panels.back().floatRect;
    ASSERT_EQ(dock.OpenAnchored("picker", {}, 1280.0f, 720.0f), DockResult::Ok);
    EXPECT_TRUE(Visible(dock, "picker"));
    EXPECT_EQ(dock.Rows().panels.back().floatRect, rectBefore) << "no anchor: the declared rect";
    EXPECT_EQ(dock.OpenAnchored("nope", {1, 1, 10, 10}, 1280.0f, 720.0f), DockResult::UnknownPanel);
    ASSERT_EQ(dock.Close("layers"), DockResult::Ok);
    ASSERT_EQ(dock.OpenAnchored("layers", {1, 1, 10, 10}, 1280.0f, 720.0f), DockResult::Ok);
    for (const auto& row : dock.Rows().panels) if (row.panel == "layers") EXPECT_EQ(row.region, DockRegion::Left);
}

TEST(PanelDockDismissible, TheTopmostOpenDismissiblePanelIsTheEscapeTarget) {
    PanelDock dock = DockWithFlyout();
    EXPECT_TRUE(dock.TopDismissible().empty()) << "closed: Escape means nothing to the dock";
    EXPECT_FALSE(dock.HasOpenDismissible());
    ASSERT_EQ(dock.Open("picker"), DockResult::Ok);
    EXPECT_EQ(dock.TopDismissible(), "picker");
    EXPECT_TRUE(dock.HasOpenDismissible());
    PanelDecl second = Decl("second", DockRegion::Floating);
    second.dismissible = true;
    dock.Declare(second);
    ASSERT_EQ(dock.Open("second"), DockResult::Ok);
    EXPECT_EQ(dock.TopDismissible(), "second") << "the most recently raised floating panel";
    ASSERT_EQ(dock.Open("picker"), DockResult::Ok);
    EXPECT_EQ(dock.TopDismissible(), "picker");
    // A panel that does not declare itself dismissible never is one.
    EXPECT_NE(dock.TopDismissible(), "layers");
}

// ---- pointer drag ------------------------------------------------------------------------------------------------

TEST(PanelDockDrag, DraggingAFloatingPanelsHeaderMovesItAndEmitsOneCommitOnRelease) {
    PanelDock dock = DockWithFlyout();
    ASSERT_EQ(dock.Open("picker"), DockResult::Ok);
    ASSERT_EQ(dock.Float("picker", {400.0f, 100.0f, 300.0f, 360.0f}), DockResult::Ok);
    PanelDockDrag drag;
    DockFrame frame = dock.Compute(1280.0f, 720.0f);
    // Press on the header strip (y 100..126), far from any splitter.
    auto step = drag.Step(dock, frame, 1280.0f, 720.0f, 600.0f, 110.0f, true);
    EXPECT_TRUE(step.captured);
    EXPECT_FALSE(step.preview) << "a press alone moves nothing";
    step = drag.Step(dock, frame, 1280.0f, 720.0f, 602.0f, 111.0f, true);
    EXPECT_TRUE(step.captured);
    EXPECT_FALSE(step.preview) << "under the drag threshold";
    step = drag.Step(dock, frame, 1280.0f, 720.0f, 640.0f, 150.0f, true);
    ASSERT_EQ(step.preview.kind, DockGesture::Kind::MovePanel);
    EXPECT_EQ(step.preview.panel, "picker");
    EXPECT_FLOAT_EQ(step.preview.rect.x, 440.0f);
    EXPECT_FLOAT_EQ(step.preview.rect.y, 140.0f);
    EXPECT_FLOAT_EQ(step.preview.rect.width, 300.0f);
    EXPECT_FALSE(step.commit);
    step = drag.Step(dock, frame, 1280.0f, 720.0f, 700.0f, 200.0f, false);
    EXPECT_FALSE(step.captured);
    ASSERT_EQ(step.commit.kind, DockGesture::Kind::MovePanel);
    EXPECT_FLOAT_EQ(step.commit.rect.x, 500.0f);
    EXPECT_FLOAT_EQ(step.commit.rect.y, 190.0f);
    EXPECT_FALSE(drag.Active());
    // Nothing more after release.
    step = drag.Step(dock, frame, 1280.0f, 720.0f, 701.0f, 200.0f, false);
    EXPECT_FALSE(step.captured);
    EXPECT_FALSE(step.commit);
}

TEST(PanelDockDrag, TheMovedRectStaysInsideTheWindow) {
    PanelDock dock = DockWithFlyout();
    ASSERT_EQ(dock.Float("picker", {400.0f, 100.0f, 300.0f, 360.0f}), DockResult::Ok);
    PanelDockDrag drag;
    const DockFrame frame = dock.Compute(1280.0f, 720.0f);
    drag.Step(dock, frame, 1280.0f, 720.0f, 600.0f, 110.0f, true);
    auto step = drag.Step(dock, frame, 1280.0f, 720.0f, 5000.0f, 5000.0f, true);
    ASSERT_EQ(step.preview.kind, DockGesture::Kind::MovePanel);
    EXPECT_FLOAT_EQ(step.preview.rect.x, 1280.0f - 300.0f);
    EXPECT_FLOAT_EQ(step.preview.rect.y, 720.0f - 360.0f);
    step = drag.Step(dock, frame, 1280.0f, 720.0f, -5000.0f, -5000.0f, true);
    EXPECT_FLOAT_EQ(step.preview.rect.x, 0.0f);
    EXPECT_FLOAT_EQ(step.preview.rect.y, 0.0f);
}

TEST(PanelDockDrag, DraggingASplitterResizesItsRegionAndANoMovePressEmitsNothing) {
    PanelDock dock = DockWithFlyout();
    PanelDockDrag drag;
    DockFrame frame = dock.Compute(1280.0f, 720.0f);
    const float leftEdge = frame.viewport.x;   // 340
    auto step = drag.Step(dock, frame, 1280.0f, 720.0f, leftEdge + 2.0f, 300.0f, true);
    EXPECT_TRUE(step.captured) << "the press began on the splitter";
    step = drag.Step(dock, frame, 1280.0f, 720.0f, 420.0f, 300.0f, true);
    ASSERT_EQ(step.preview.kind, DockGesture::Kind::ResizeRegion);
    EXPECT_EQ(step.preview.region, DockRegion::Left);
    EXPECT_FLOAT_EQ(step.preview.size, 420.0f);
    step = drag.Step(dock, frame, 1280.0f, 720.0f, 421.0f, 300.0f, false);
    ASSERT_EQ(step.commit.kind, DockGesture::Kind::ResizeRegion);
    EXPECT_FLOAT_EQ(step.commit.size, 421.0f);

    // The right splitter measures from the window's right edge.
    const float rightEdge = frame.viewport.x + frame.viewport.width;   // 960
    drag.Step(dock, frame, 1280.0f, 720.0f, rightEdge - 2.0f, 300.0f, true);
    step = drag.Step(dock, frame, 1280.0f, 720.0f, 900.0f, 300.0f, true);
    ASSERT_EQ(step.preview.kind, DockGesture::Kind::ResizeRegion);
    EXPECT_EQ(step.preview.region, DockRegion::Right);
    EXPECT_FLOAT_EQ(step.preview.size, 380.0f);
    drag.Step(dock, frame, 1280.0f, 720.0f, 900.0f, 300.0f, false);

    // A press on the splitter released without moving past the threshold is a click: no gesture at all.
    drag.Step(dock, frame, 1280.0f, 720.0f, leftEdge + 1.0f, 300.0f, true);
    step = drag.Step(dock, frame, 1280.0f, 720.0f, leftEdge + 2.0f, 300.0f, false);
    EXPECT_FALSE(step.commit);
    EXPECT_FALSE(step.preview);

    // A press in the middle of the viewport is the world's.
    step = drag.Step(dock, frame, 1280.0f, 720.0f, 640.0f, 300.0f, true);
    EXPECT_FALSE(step.captured);
    drag.Step(dock, frame, 1280.0f, 720.0f, 640.0f, 300.0f, false);
}

TEST(PanelDockHostDrag, PreviewMovesTheDockLiveWithoutSavingAndCommitIsTheVerbToEmit) {
    NullView view;
    PanelDockHost host;
    for (PanelDecl decl : {Decl("left", DockRegion::Left), Decl("picker", DockRegion::Floating)}) {
        decl.view = &view;
        decl.transient = decl.id == "picker";
        host.Dock().Declare(decl);
    }
    RecordingHost node;
    host.Attach(node);
    ASSERT_TRUE(host.Update());
    ASSERT_EQ(host.Dock().Float("picker", {600.0f, 100.0f, 300.0f, 360.0f}), DockResult::Ok);
    ASSERT_TRUE(host.Update());
    const uint64_t persisted = host.Dock().PersistedRevision();
    host.StepDrag(700.0f, 110.0f, true);
    auto step = host.StepDrag(760.0f, 150.0f, true);
    ASSERT_EQ(step.preview.kind, DockGesture::Kind::MovePanel);
    PanelRow row;
    for (const auto& candidate : host.Dock().Rows().panels) if (candidate.panel == "picker") row = candidate;
    EXPECT_FLOAT_EQ(row.floatRect.x, 660.0f) << "applied live";
    EXPECT_EQ(host.Dock().PersistedRevision(), persisted) << "a transient panel's drag never dirties the layout";
    step = host.StepDrag(760.0f, 150.0f, false);
    EXPECT_EQ(step.commit.kind, DockGesture::Kind::MovePanel);
}
