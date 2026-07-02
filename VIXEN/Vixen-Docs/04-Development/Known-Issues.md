---
title: Known Issues / Bugs To Fix
status: living log
created: 2026-07-02
tags: [known-issues, bugs, tech-debt]
---

# Known Issues / Bugs To Fix

Living log of confirmed-but-unfixed issues. Each entry: symptom, root cause, impact, fix options, severity. Add new issues at the top; move fixed ones to a `## Resolved` section with the fixing commit.

---

## KI-001 — 3 RenderGraph tests fail to build: missing `xcb/xcb.h` (WSL env)

**Discovered:** 2026-07-02 (during the config-struct codegen epic full-build gate; pre-existing, not caused by that work).

**Symptom:** a full `cmake --build build-wsl -- -k 0` fails to compile 3 test TUs:
- `libraries/RenderGraph/tests/test_array_type_validation.cpp`
- `libraries/RenderGraph/tests/test_field_extraction.cpp`
- `libraries/RenderGraph/tests/test_resource_gatherer.cpp`

Error (all three, identical): `.vulkan-sdk/1.4.350.1/x86_64/Include/vulkan/vulkan.h:52:10: fatal error: xcb/xcb.h: No such file or directory`.

**Root cause:** `vulkan.h` includes `<xcb/xcb.h>` when `VK_USE_PLATFORM_XCB_KHR` is defined; the WSL build environment has no XCB development headers installed (`libxcb1-dev` / `libxcb-*-dev`). These three tests pull the full Vulkan platform header (transitively) rather than a headless subset.

**Impact:** medium (local/WSL). These 3 tests cannot build here. The offscreen/lavapipe render tests and SPIR-V-reflection tests are UNAFFECTED (they don't require the XCB surface platform) — e.g. `test_body_instance_raymarch_render` and `test_octree_config_sdi_parity` build + pass. A CI/host with the xcb dev package would not hit this.

**Fix options (pick one):**
1. Install the XCB dev headers in the WSL env: `sudo apt-get install libxcb1-dev libxcb-*-dev` (fastest; unblocks locally, but every dev must do it).
2. Do not define `VK_USE_PLATFORM_XCB_KHR` for headless test targets — they need no window surface. Scope the platform define to the app/windowing targets only.
3. CMake-gate these 3 tests on XCB header availability (`check_include_file`), skipping them with a `message(STATUS ...)` when absent (mirrors the existing dotnet-gated / SDK-gated patterns).

**Recommended:** option 2 (root-cause: headless tests shouldn't require a window-surface platform), with option 1 as the immediate unblock.

**Severity:** Medium · **Status:** OPEN

---

## KI-002 — `test_shell_octree_gpu.ConcatRejectsMoreThanThree` fails (stale test vs removed cap)

**Discovered:** 2026-07-02 (config-struct codegen C1 gate; pre-existing, unrelated to that byte-identical struct alias).

**Symptom:** `test_shell_octree_gpu` is 8/9 — `ShellOctreeGpu.ConcatRejectsMoreThanThree` (`libraries/SVO/tests/test_shell_octree_gpu.cpp:179`) fails. The test builds 4 shell octrees and asserts `EXPECT_THROW(Concatenate(four), std::length_error)`.

**Root cause:** the `kMaxOctrees = 3` cap was intentionally removed in the earlier recipe-authoring epic (the octree pool became memory-budgeted / count-unbounded — see the `recipe-authoring-pipeline-shipped` work). `Concatenate` no longer throws for >3 octrees, so the test's `EXPECT_THROW` fails. The test was not updated when the cap was removed.

**Impact:** low. This is a STALE TEST, not a functional regression — the unbounded-concat behavior is the intended design. But it leaves the SVO suite permanently red (1 test), which erodes the "green suite" signal.

**Fix options:**
1. If concat is now genuinely unbounded: replace the test with one asserting `Concatenate(four)` SUCCEEDS and produces a valid combined octree (rename e.g. `ConcatAcceptsMoreThanThree`).
2. If a new upper bound exists (e.g. a memory-budget or `kMaxChannels`-style limit): assert rejection at that real boundary instead of 3.

**Recommended:** confirm the intended concat contract (unbounded vs new-limit), then option 1 or 2 accordingly.

**Severity:** Low · **Status:** OPEN
