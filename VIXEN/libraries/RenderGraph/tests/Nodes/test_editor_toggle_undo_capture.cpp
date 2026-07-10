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
 * R6 finding -- WHY the visual mask round-trip assertion was REMOVED (durable record)
 * ============================================================================================
 * An earlier version of this gate asserted a byte-exact VISUAL round-trip:
 *   capture_75.rgb == capture_5.rgb   (undo restores the render)
 *   capture_105.rgb == capture_45.rgb (redo re-applies it)
 * plus "toggle visibly changed the render at frame 45 vs 5".
 *
 * A max-effort GPU-boundary investigation (Inc-4 R6) proved those assertions tested an
 * INVISIBLE-BY-DESIGN quantity and were therefore removed:
 *   - The layer mask (toggling layer 2, a CSG cut) only changes the SDF field values in the
 *     shell channelPool (binding 11). Instrumentation proved that pool reaches the shader
 *     CORRECT and FRESH on every edit -- mapped+checksummed GPU memory shows the mask-A / mask-B /
 *     mask-A byte trail across toggle/undo/redo, and a shader-output tap reads it back A/B/A.
 *     So mask correctness is proven at the CPU, GPU-buffer, and shader-input layers.
 *   - BUT the editor's body renders via the mask-INVARIANT mip-fallback path (the drawn octree
 *     has OctreeConfig.brickResident == 0 at the editor's orbit camera -- confirmed by a shader
 *     tap), so the fine SDF surface (where the mask lives) is never marched. At that orbit camera
 *     the cut-layer silhouette delta is ~0px. Proof: two consecutive toggles (mask7->mask3->mask7,
 *     both post-residency) render BYTE-IDENTICAL. The only visible change in the whole run is the
 *     one-time non-resident -> resident transition the FIRST edit triggers (mip fallback ->
 *     resident), which is NOT the mask.
 *   => A byte-exact visual undo/redo round-trip is unreachable in the editor's current
 *      camera + LOD path. It never proved the mask round-trip; it accidentally tracked the
 *      residency transition. (The editor mip-fallback/residency behaviour is a separate
 *      content/LOD matter, not an AppFlow defect. See Vixen-Docs/01-Architecture for the note.)
 *
 * What this gate asserts INSTEAD -- only things PROVEN and editor-observable:
 *   1. ToggleUndoRedoStateTrailThroughWindowedRun -- parses the running editor's own
 *      "[EDITOR/state] <op> mask=.. undoDepth=.. redoDepth=.." lines (emitted by
 *      EditorApplication::PreTick on each scripted edit) and asserts the mask trail is exactly
 *      7 -> 3(toggle) -> 7(undo) -> 3(redo) with correct undo/redo depth movement. THIS is the real
 *      proof undo/redo work: from the running windowed editor, through the registry-dispatch path.
 *   2. FirstEditReachesRenderPipeline -- a smoke check that SOME visible change happened when the
 *      first edit hit the render pipeline (capture_5 != capture_45). This asserts the
 *      non-resident -> RESIDENT transition (i.e. "the edit reached the GPU and re-materialized"),
 *      explicitly NOT the mask. It cannot assert undo!=redo (residency latches after the first
 *      edit -- every later capture is byte-identical), so it is a one-shot smoke check only.
 *   3. BackButtonReachesReturnInRunningEditor -- back-button -> Return in the running editor
 *      (afterBack == FlowStateId::Editing). Proven + real; unchanged.
 *
 * Frame timeline (matches VIXEN_EDITOR_SCRIPT/VIXEN_EDITOR_CAPTURE_FRAMES in run_editor_script.bat):
 *   frame 5   -- baseline capture, all layers enabled, PRE first edit (mip-fallback render).
 *   frame 30  -- toggle:2 fires (also triggers the one-time residency transition).
 *   frame 45  -- capture (post-toggle, RESIDENT render -- differs from frame 5 by the residency
 *                transition, not the mask).
 *   frame 60  -- undo fires.   frame 75 -- capture (mask back to 7, render byte-identical to 45).
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

// Count pixels that differ by > 16 on any channel across the WHOLE image. Used only for the
// one-shot residency smoke check (FirstEditReachesRenderPipeline): the render is fully
// deterministic and noise-free (a genuinely unchanged frame diffs EXACTLY 0), so any positive
// count is real signal that the first edit reached and re-materialized the render pipeline.
int WholeImageDiffPixels(const Image& a, const Image& b) {
    if (a.rgb.empty() || b.rgb.empty()) return -1;
    if (a.width != b.width || a.height != b.height) return -1;
    int diff = 0;
    for (size_t i = 0; i + 2 < a.rgb.size(); i += 3) {
        const int dr = int(a.rgb[i + 0]) - int(b.rgb[i + 0]);
        const int dg = int(a.rgb[i + 1]) - int(b.rgb[i + 1]);
        const int db = int(a.rgb[i + 2]) - int(b.rgb[i + 2]);
        if (std::abs(dr) > 16 || std::abs(dg) > 16 || std::abs(db) > 16) ++diff;
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
    // R6 smoke check: the FIRST edit reaches the render pipeline and re-materializes it -- proven
    // by SOME visible change between the pre-first-edit baseline (frame 5) and the post-first-edit
    // capture (frame 45). This asserts the non-resident -> RESIDENT transition the first edit
    // triggers, NOT the mask (the mask is invisible in the editor's mip-fallback/orbit-camera path
    // -- see the file header). It is one-shot: residency latches, so later captures (75, 105) are
    // byte-identical to 45 and cannot be used to assert undo!=redo (that's what the state-trail
    // test above proves instead).
    const std::string dir = CaptureDir();
    const Image png5  = LoadPng(dir + "/editor_capture_5.png");
    const Image png45 = LoadPng(dir + "/editor_capture_45.png");

    ASSERT_FALSE(png5.rgb.empty())  << "missing " << dir << "/editor_capture_5.png -- run "
                                        "VIXEN/temp/run_editor_script.bat first";
    ASSERT_FALSE(png45.rgb.empty()) << "missing " << dir << "/editor_capture_45.png";
    ASSERT_EQ(png5.width, png45.width);
    ASSERT_EQ(png5.height, png45.height);

    const int diff = WholeImageDiffPixels(png5, png45);
    std::printf("[EDITOR/residency] wholeImageDiffPixels(png5,png45)=%d (%dx%d)\n",
                diff, png5.width, png5.height);
    // The residency transition repaints a large area (mip fallback -> resident SDF march), so this
    // is thousands of pixels, not the ~6px an on-axis mask cut would be. Any substantial positive
    // count proves the first edit reached the render pipeline; 0 would mean it never did.
    EXPECT_GT(diff, 0)
        << "the first edit produced NO visible render change (frame 45 == frame 5) -- the edit "
           "never reached the render pipeline (expected the non-resident->resident transition)";
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
