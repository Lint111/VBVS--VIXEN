/**
 * @file test_editor_toggle_undo_capture.cpp
 * @brief AppFlow Inc-2b / Inc-4 R6 -- the authoritative WINDOWED live proof that toggle/undo/redo
 * work end-to-end in the RUNNING editor, through the real click-equivalent -> registry dispatch ->
 * ActionStack -> re-flatten path (EditorApplication's registered handlers, via
 * DispatchBySelector/DispatchByKey), not a shortcut. Reads the artifacts of an unattended
 * vixen_editor run (VIXEN/temp/run_editor_script.bat: script "toggle:2@30,undo@60,redo@90,
 * settings@100,back@110", capture frames "5,45,75,105"). Pure file I/O -- no Vulkan/GPU here; the
 * GPU work already happened when the .bat produced the PNGs + log. Registered OUTSIDE the
 * glslc-gated GPU-render test group (test_critical_nodes.cmake) so it builds+runs Windows-side too,
 * matching where the windowed editor itself runs.
 *
 * ============================================================================================
 * R6 finding history -- byte-exact visual round-trip removed, then RESTORED (durable record)
 * ============================================================================================
 * An earlier version of this gate asserted a byte-exact VISUAL round-trip
 * (capture_75==capture_5, capture_105==capture_45) plus "toggle visibly changed the render at
 * frame 45 vs 5". A max-effort GPU-boundary investigation (Inc-4 R6) proved those assertions
 * tested an INVISIBLE-BY-DESIGN quantity: the editor's document body never requested brick
 * residency, so it rendered the mask-INVARIANT mip-fallback path (OctreeConfig.brickResident==0)
 * -- the fine SDF march where the layer mask lives never ran. The gate was reworked to assert
 * only the state trail + a residency-transition smoke check (FirstEditReachesRenderPipeline).
 * See Vixen-Docs/01-Architecture/R6-Editor-Render-Mask-Invisible-Finding-2026-07.md.
 *
 * Editor-Brick-Residency-Fix-Plan-2026-07 (RESOLVED): EditorApplication::ApplyDocumentToScene
 * now grants RequestBrickResidency(true) unconditionally for the document body, and overrides
 * VulkanGraphApplication::SkipResidencyHeuristic() so the main app's camera-driven heuristic
 * (which a static editor session never satisfies) cannot stomp the grant back to false. Live
 * WSL/Dozen verification confirmed brick-level traversal (BRICK_ENTER/BRICK_EXIT) now occurs
 * where only mip-fallback ran before, and the mask IS now visible -- but the editor's document
 * body occupies a small on-screen footprint (a ~32x32px silhouette in the 500x500 capture at the
 * framed orbit camera; see EditorApplication::ApplyDocumentToScene's comment on the object's
 * ~2-unit local extent), so the cut-layer delta is dozens of pixels with small (<10) per-channel
 * magnitude, NOT the thousands-of-pixels/>16-per-channel delta a larger or bore-aligned camera
 * would show (that larger delta IS what test_appflow_editor_toggle_render.cpp's headless,
 * bore-aligned-camera gate asserts against a fresh BodyOctreeSceneNode -- a different, bigger
 * signal by design). The R6-finding doc's own investigation predicted exactly this ("the cut
 * delta is ~6px at best" at the editor's general orbit camera) -- this measurement (a real,
 * deterministic, mask-confined delta strictly inside the object's bounding box) confirms it.
 * The round-trip below IS byte-exact (proven on a real WSL/Dozen run), so the visual assertions
 * are restored with a threshold calibrated to the ACTUAL measured delta, not a guessed one.
 *
 * What this gate asserts:
 *   1. ToggleUndoRedoStateTrailThroughWindowedRun -- parses the running editor's own
 *      "[EDITOR/state] <op> mask=.. undoDepth=.. redoDepth=.." trail (emitted by
 *      EditorApplication::PreTick on each scripted edit) and asserts the mask trail is exactly
 *      7 -> 3(toggle) -> 7(undo) -> 3(redo) with correct undo/redo depth movement. THIS is the real
 *      proof undo/redo work: from the running windowed editor, through the registry-dispatch path.
 *   2. FirstEditReachesRenderPipeline -- asserts capture_5 != capture_45: the first edit produced
 *      a real, mask-confined visual delta (residency is now requested unconditionally, so this is
 *      no longer a one-shot residency-transition smoke check -- it is the mask itself; see
 *      kMinMaskDiffPixels below for the calibrated lower bound).
 *   3. UndoRedoRestoresRenderByteExact -- capture_75.rgb == capture_5.rgb (undo restores the
 *      render) and capture_105.rgb == capture_45.rgb (redo re-applies it), byte-for-byte. This is
 *      the real visual round-trip the residency fix makes reachable again.
 *   4. BackButtonReachesReturnInRunningEditor -- back-button -> Return in the running editor
 *      (afterBack == FlowStateId::Editing). Proven + real; unchanged.
 *
 * Frame timeline (matches VIXEN_EDITOR_SCRIPT/VIXEN_EDITOR_CAPTURE_FRAMES in run_editor_script.bat):
 *   frame 5   -- baseline capture, all layers enabled, PRE first edit.
 *   frame 30  -- toggle:2 fires (mask cut becomes visible -- residency is now unconditional, so
 *                this is not confounded with a mip->resident transition).
 *   frame 45  -- capture (post-toggle; differs from frame 5 by the mask cut).
 *   frame 60  -- undo fires.   frame 75 -- capture (mask back to 7, render byte-identical to 5).
 *   frame 90  -- redo fires.   frame 105 -- capture (mask 3 again, byte-identical to 45).
 *   frame 100 -- settings (NavTo). frame 110 -- back-button (DispatchBySelector("back-button")).
 */

#include <gtest/gtest.h>

#include <stb_image.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

// One RGB8 image loaded via stb_image (matches CaptureRenderTargetToPng's RGB8 PNG output --
// RenderTargetReadback.h always writes 3 channels regardless of the source image's alpha).
struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgb;  // width*height*3, empty if load failed
};

// VIXEN_EDITOR_CAPTURE_DIR defaults to "temp" in both run_editor_script.bat and
// EditorApplication's own default (captureDir_ = "temp"); allow an override via the same env
// name so this test can point at a different capture dir without touching the .bat.
std::string CaptureDir() {
    if (const char* env = std::getenv("VIXEN_EDITOR_CAPTURE_DIR")) return env;
    return "temp";
}

Image LoadPng(const std::string& path) {
    Image img;
    int channels = 0;
    // Force 3 channels (RGB) -- the capture writer always emits RGB8, but force_channels makes
    // the comparison robust even if a PNG decoder reports a different channel count.
    unsigned char* data = stbi_load(path.c_str(), &img.width, &img.height, &channels, 3);
    if (!data) return img;
    img.rgb.assign(data, data + (size_t(img.width) * img.height * 3));
    stbi_image_free(data);
    return img;
}

// Count pixels that differ AT ALL (any channel != 0) across the WHOLE image. The render is fully
// deterministic and noise-free (a genuinely unchanged frame diffs EXACTLY 0 -- proven live: two
// consecutive mask=3 renders round-trip byte-identical), so any positive count is real signal.
// Editor-Brick-Residency-Fix-Plan-2026-07: NOT a >16-per-channel threshold (that was calibrated
// for test_appflow_editor_toggle_render.cpp's bore-aligned camera against a much bigger on-screen
// silhouette) -- the editor's document body is small on screen (see the file header), so its cut-
// layer delta is real but sub-16-per-channel. A live WSL/Dozen run measured 62 differing pixels
// (max per-channel delta 9), all strictly inside the object's own bounding box -- see
// kMinMaskDiffPixels below for the calibrated lower bound this feeds.
int WholeImageDiffPixels(const Image& a, const Image& b) {
    if (a.rgb.empty() || b.rgb.empty()) return -1;
    if (a.width != b.width || a.height != b.height) return -1;
    int diff = 0;
    for (size_t i = 0; i + 2 < a.rgb.size(); i += 3) {
        if (a.rgb[i + 0] != b.rgb[i + 0] || a.rgb[i + 1] != b.rgb[i + 1] || a.rgb[i + 2] != b.rgb[i + 2]) {
            ++diff;
        }
    }
    return diff;
}

// Log dir mirrors CaptureDir()'s VIXEN_EDITOR_CAPTURE_DIR override -- run_editor_script.bat
// writes the log at "temp\run_editor_script.log" (relative to the VIXEN dir it cd's into),
// same directory the .bat points VIXEN_EDITOR_CAPTURE_DIR at by default.
std::string LogPath() { return CaptureDir() + "/run_editor_script.log"; }

// Parses "key=<int>" out of one log line (positional-by-key, whitespace-delimited). Returns
// nullopt if the key isn't present. Tolerant of trailing tokens after the value.
std::optional<int> ParseIntField(const std::string& line, const std::string& key) {
    const std::string needle = key + "=";
    const size_t pos = line.find(needle);
    if (pos == std::string::npos) return std::nullopt;
    std::istringstream iss(line.substr(pos + needle.size()));
    int value = 0;
    if (iss >> value) return value;
    return std::nullopt;
}

// One parsed "[EDITOR/state] <op> mask=.. undoDepth=.. redoDepth=.." edit line.
struct EditState {
    std::string op;      // "toggle" | "undo" | "redo"
    int mask = -1;
    int undoDepth = -1;
    int redoDepth = -1;
};

// Scans run_editor_script.log for the ordered "[EDITOR/state] <op> ..." edit lines (toggle/undo/
// redo), in emission order. Ignores the "[EDITOR/state] capture ..." and "afterBack=" lines --
// those have no <op>/mask=/depth triple. Returns them in the order the editor logged them.
std::vector<EditState> ReadEditStates(const std::string& logPath) {
    std::vector<EditState> out;
    std::ifstream in(logPath);
    if (!in) return out;
    const std::string marker = "[EDITOR/state] ";
    std::string line;
    while (std::getline(in, line)) {
        const size_t pos = line.find(marker);
        if (pos == std::string::npos) continue;
        std::istringstream iss(line.substr(pos + marker.size()));
        std::string op;
        iss >> op;  // first token after the marker
        if (op != "toggle" && op != "undo" && op != "redo") continue;  // skip capture/afterBack
        const auto mask = ParseIntField(line, "mask");
        const auto ud   = ParseIntField(line, "undoDepth");
        const auto rd   = ParseIntField(line, "redoDepth");
        if (!mask || !ud || !rd) continue;  // malformed -- skip, the size check below will fail
        out.push_back(EditState{op, *mask, *ud, *rd});
    }
    return out;
}

// Scans for the LAST "[EDITOR/state] afterBack=<int>" line (last, not first, in case a future
// script schedules more than one Back). Returns nullopt if absent.
std::optional<int> ReadAfterBackState(const std::string& logPath) {
    std::ifstream in(logPath);
    if (!in) return std::nullopt;
    const std::string marker = "[EDITOR/state] afterBack=";
    std::optional<int> found;
    std::string line;
    while (std::getline(in, line)) {
        const size_t pos = line.find(marker);
        if (pos == std::string::npos) continue;
        std::istringstream iss(line.substr(pos + marker.size()));
        int value = -1;
        if (iss >> value) found = value;
    }
    return found;
}

// Editor-Brick-Residency-Fix-Plan-2026-07 Task 3: calibrated lower bound for the mask-cut delta
// between capture_5 (mask=7) and capture_45 (mask=3), measured directly off a real WSL/Dozen run
// (62 differing pixels, all inside the object's ~32x32px bounding box). Set well below that
// measurement (not at it) so the gate has headroom for minor cross-run/driver variance while still
// failing hard if residency regresses back to mip-only (which renders capture_45 byte-IDENTICAL to
// capture_5, i.e. 0 diff pixels -- see the R6-finding doc's own "two consecutive toggles render
// byte-identical" proof of the pre-fix behaviour).
constexpr int kMinMaskDiffPixels = 20;

}  // namespace

TEST(EditorToggleUndoCapture, ToggleUndoRedoStateTrailThroughWindowedRun) {
    // R6 (replaces the removed byte-exact VISUAL round-trip -- see the file header for why that was
    // invisible-by-design). Asserts undo/redo actually work in the RUNNING editor by parsing the
    // state trail it logs on each scripted edit. mask=7 (all layers) -> 3 (layer 2 cut off) -> 7
    // (undo) -> 3 (redo), with the ActionStack depths moving correctly. This exercises the full
    // click-equivalent -> registry -> handler -> ActionStack -> re-flatten path, windowed.
    const std::string logPath = LogPath();
    const std::vector<EditState> edits = ReadEditStates(logPath);

    ASSERT_EQ(edits.size(), 3u)
        << "expected exactly 3 [EDITOR/state] edit lines (toggle,undo,redo) in " << logPath
        << " -- run VIXEN/temp/run_editor_script.bat first (found " << edits.size() << ")";

    // toggle:2@30 -- mask 7 -> 3; the edit pushes one undo entry, clears redo.
    EXPECT_EQ(edits[0].op, "toggle");
    EXPECT_EQ(edits[0].mask, 3)       << "toggle:2 did not flip mask 7->3 in the running editor";
    EXPECT_EQ(edits[0].undoDepth, 1)  << "toggle did not push an undo entry";
    EXPECT_EQ(edits[0].redoDepth, 0)  << "toggle did not clear the redo stack";

    // undo@60 -- mask 3 -> 7; pops the undo entry onto the redo stack.
    EXPECT_EQ(edits[1].op, "undo");
    EXPECT_EQ(edits[1].mask, 7)       << "undo did not restore mask 3->7 in the running editor";
    EXPECT_EQ(edits[1].undoDepth, 0)  << "undo did not pop the undo entry";
    EXPECT_EQ(edits[1].redoDepth, 1)  << "undo did not push a redo entry";

    // redo@90 -- mask 7 -> 3; re-applies from the redo stack.
    EXPECT_EQ(edits[2].op, "redo");
    EXPECT_EQ(edits[2].mask, 3)       << "redo did not re-apply mask 7->3 in the running editor";
    EXPECT_EQ(edits[2].undoDepth, 1)  << "redo did not push the undo entry back";
    EXPECT_EQ(edits[2].redoDepth, 0)  << "redo did not consume the redo entry";
}

TEST(EditorToggleUndoCapture, FirstEditReachesRenderPipeline) {
    // Editor-Brick-Residency-Fix-Plan-2026-07: residency is now requested unconditionally (see the
    // file header), so this asserts the MASK delta itself, not a one-shot residency-transition
    // smoke check -- capture_5 (mask=7) must differ from capture_45 (mask=3) by at least
    // kMinMaskDiffPixels, calibrated to a real WSL/Dozen measurement (62 pixels, all inside the
    // object's bounding box).
    const std::string dir = CaptureDir();
    const Image png5  = LoadPng(dir + "/editor_capture_5.png");
    const Image png45 = LoadPng(dir + "/editor_capture_45.png");

    ASSERT_FALSE(png5.rgb.empty())  << "missing " << dir << "/editor_capture_5.png -- run "
                                        "VIXEN/temp/run_editor_script.bat first";
    ASSERT_FALSE(png45.rgb.empty()) << "missing " << dir << "/editor_capture_45.png";
    ASSERT_EQ(png5.width, png45.width);
    ASSERT_EQ(png5.height, png45.height);

    const int diff = WholeImageDiffPixels(png5, png45);
    std::printf("[EDITOR/mask] wholeImageDiffPixels(png5,png45)=%d (%dx%d)\n",
                diff, png5.width, png5.height);
    EXPECT_GT(diff, kMinMaskDiffPixels)
        << "the first edit produced too small a render change (frame 45 vs frame 5, diff=" << diff
        << ") -- either the mask cut isn't reaching the render, or brick residency regressed back "
           "to mip-only fallback (see EditorApplication::SkipResidencyHeuristic)";
}

TEST(EditorToggleUndoCapture, UndoRedoRestoresRenderByteExact) {
    // Editor-Brick-Residency-Fix-Plan-2026-07 Task 3: the REAL visual round-trip, restored now that
    // brick residency (and therefore the mask) is reachable in the editor's render path. Byte-exact,
    // not "close" -- a live WSL/Dozen run proved capture_75==capture_5 and capture_105==capture_45
    // to the byte.
    const std::string dir = CaptureDir();
    const Image png5   = LoadPng(dir + "/editor_capture_5.png");
    const Image png45  = LoadPng(dir + "/editor_capture_45.png");
    const Image png75  = LoadPng(dir + "/editor_capture_75.png");
    const Image png105 = LoadPng(dir + "/editor_capture_105.png");

    ASSERT_FALSE(png5.rgb.empty())   << "missing " << dir << "/editor_capture_5.png -- run "
                                         "VIXEN/temp/run_editor_script.bat first";
    ASSERT_FALSE(png45.rgb.empty())  << "missing " << dir << "/editor_capture_45.png";
    ASSERT_FALSE(png75.rgb.empty())  << "missing " << dir << "/editor_capture_75.png";
    ASSERT_FALSE(png105.rgb.empty()) << "missing " << dir << "/editor_capture_105.png";

    // Undo (frame 60) restores mask=7 -- frame 75's render must byte-match frame 5's baseline.
    ASSERT_EQ(png75.width, png5.width);
    ASSERT_EQ(png75.height, png5.height);
    EXPECT_EQ(png75.rgb, png5.rgb)
        << "undo did not restore the render byte-for-byte (frame 75 vs frame 5)";

    // Redo (frame 90) re-applies mask=3 -- frame 105's render must byte-match frame 45's.
    ASSERT_EQ(png105.width, png45.width);
    ASSERT_EQ(png105.height, png45.height);
    EXPECT_EQ(png105.rgb, png45.rgb)
        << "redo did not restore the render byte-for-byte (frame 105 vs frame 45)";
}

TEST(EditorToggleUndoCapture, BackButtonReachesReturnInRunningEditor) {
    // Inc-4 R6a: settings@100 (NavTo(Settings)) then back@110 (DispatchBySelector("back-button"))
    // -- proves the SAME edge test_flow_return.cpp's FSM unit test proves, but through the real,
    // wired, windowed dispatch path (registry -> BindingStore -> Return handler -> NavPop),
    // exactly as design D15/R6 requires ("prove Esc AND back-button reach Return in the RUNNING
    // editor, not just the unit test").
    const std::string logPath = LogPath();
    const std::optional<int> afterBack = ReadAfterBackState(logPath);
    ASSERT_TRUE(afterBack.has_value())
        << "missing \"[EDITOR/state] afterBack=\" in " << logPath << " -- run "
           "VIXEN/temp/run_editor_script.bat first (script must include settings@.../back@...)";

    // FlowStateId::Editing == 0 (AppFlow.g.h) -- Settings is only ever entered from Editing in
    // this script, and back-button (bound to FlowActionId::Return in Settings, kSelectorBindings)
    // resolves through the Return handler's rt_.NavPop(), which restores the FSM's entry history
    // to whatever NavTo(Settings) pushed from.
    constexpr int kFlowStateIdEditing = 0;
    EXPECT_EQ(*afterBack, kFlowStateIdEditing)
        << "back-button did not pop the FSM back to Editing (afterBack=" << *afterBack << ") -- "
           "the running editor's back-button->Return wiring is broken";
}
