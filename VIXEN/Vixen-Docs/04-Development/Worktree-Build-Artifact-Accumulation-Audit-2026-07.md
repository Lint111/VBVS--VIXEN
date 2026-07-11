---
title: Worktree Build-Artifact Accumulation Audit
aliases: [Disk Space Audit, Worktree Build Bloat]
tags: [development, build, cmake, disk-space, worktrees]
created: 2026-07-11
related:
  - "[[Build-System]]"
  - "[[Known-Issues]]"
---

# Worktree Build-Artifact Accumulation Audit (2026-07)

## Trigger

Host `C:\` drive hit 100% full (852MB free of 931GB), stalling agent builds. Investigation
found 13 concurrent `.claude/worktrees/*` checkouts of VIXEN, several 10-64GB each, almost
entirely compiled build output rather than source. This doc records the root causes in the
CMake/build configuration that make worktree-based multi-agent development accumulate disk
space, ranked by impact, plus a fix plan to work through sequentially.

Session recovery already reclaimed ~72GB by deleting 8 worktrees whose branches were fully
merged to `main` (verified via `git rev-list main..<branch>` = 0 unmerged commits before
deletion). This doc is about preventing the recurrence, not that cleanup.

## Root causes (ranked by impact)

### 1. No shared `FETCHCONTENT_BASE_DIR` — largest multiplier

`dependencies/CMakeLists.txt` declares ~12 third-party libraries via `FetchContent_Declare`
(glm, Vulkan-Headers, gli, stb, hash-library, gaia-ecs, googletest, nlohmann_json, miniz,
VMA, glfw, freetype, RmlUi, oneTBB). Several build from source (TBB, RmlUi, freetype, glfw,
googletest). No `FETCHCONTENT_BASE_DIR` override exists anywhere in the repo, so CMake uses
its default: `${CMAKE_BINARY_DIR}/_deps`, i.e. **inside `build/<preset>/`, fully per-worktree
and per-preset.**

Effect: every worktree, times every preset built in that worktree, re-clones and rebuilds the
full dependency set independently. Confirmed live: worktree `tiered-esvo-inc2` had both
`build/ninja` (9.5GB) and `build/wsl` (3.8GB) populated, each with its own `_deps` (763MB +
608MB) for the *same* dependency set.

### 2. `binaries/` output hardcoded to the source tree, not the build tree

`CMakeLists.txt:13` — `set(VIXEN_ROOT ${CMAKE_CURRENT_SOURCE_DIR})`.

Three targets override `RUNTIME_OUTPUT_DIRECTORY*` (all 5 config variants) to
`${VIXEN_ROOT}/binaries`:
- `application/main/CMakeLists.txt:120-124` (target `VIXEN`)
- `application/editor/CMakeLists.txt:32-36` (target `vixen_editor`)
- `application/benchmark/CMakeLists.txt:117-121` (target `vixen_benchmark`)

`CMakeLists.txt:114` comment confirms intent: *"Keep final binaries in project directory...
RUNTIME_OUTPUT_DIRECTORY is overridden per-target below to use binaries/"* — deliberate, not
an oversight, likely so `binaries/` is a stable, predictable path for scripts/tooling
regardless of which preset/build-dir was used.

Effect: `binaries/` lands as a sibling to `build/`, inside the (git-ignored, but
source-tree-resident) worktree — invisible to `rm -rf build` cleanup. Confirmed: main
checkout `VIXEN/binaries/` = 18GB, including duplicate `_BEFORE`/`_AFTER` exe copies and a
stale `cache_bak_*` directory accumulated over time.

### 3. Embedded debug info produces large linked PDB/ILK per exe

`CMakePresets.json:14` — `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT: Embedded` (`/Z7`), because
separate-PDB `/Zi` is non-cacheable under sccache and causes `C1041` under parallel `cl.exe`
(documented in the preset's own `description` field — a correct, deliberate tradeoff for
cache-hit rate, not a misconfiguration to simply reverse).

Cost: all debug info gets pushed into the final link. Measured in the main checkout:
`VIXEN.pdb` 1.5GB, `vixen_benchmark.pdb` 1.4GB, `vixen_editor.pdb` 234MB, plus `.ilk`
incremental-link files (~350MB each ×3) because incremental linking is CMake's MSVC Debug
default and was never disabled. Three exes × this cost × every worktree/preset that got
built.

### 4. No worktree build-hygiene policy

The `multi-worktree-sync` skill governs git-ref collision avoidance only — nothing about
build-artifact placement, per-worktree size limits, or cleanup during a worktree's active
life. Its teardown step (`git worktree remove` after a merged work-stream) is opt-in/manual
and only runs at the very end — nothing warns or gates mid-flight, so worktrees accumulate
silently until someone notices `C:\` is full.

### 5. Multiple presets built in the same worktree double its footprint

Observed live: `tiered-esvo-inc2` had both `build/ninja` (Windows/MSVC) and `build/wsl`
(WSL/GCC) populated simultaneously — a workflow artifact of validating on both toolchains in
one worktree, not itself a CMake defect, but it compounds root causes #1 and #3 per-worktree.

### 6. Undertow triple-dips on the same dependency set

`undertow` (`/home/liory/Github/undertow`) embeds VIXEN as a separate git submodule at
`vixen/engine` (tracking `claude/wsl-build-portability`, a distinct clone from any
`VBVS--VIXEN` worktree). `vixen/CMakeLists.txt:21` — `add_subdirectory(engine/VIXEN)` — pulls
in VIXEN's own `dependencies/CMakeLists.txt`, so undertow's `vixen/build/_deps` (measured
2.4GB) is a **third independent rebuild** of the same TBB/RmlUi/freetype/glfw/gtest set, with
no cache-sharing back to the VIXEN-repo worktrees. Not itself misconfigured — undertow's
`.gitignore` correctly ignores `build/` — just structurally isolated.

## What's already working (don't touch)

- **sccache is correctly wired and shared.** `CMakeLists.txt:31-46` sets
  `CMAKE_C_COMPILER_LAUNCHER`/`CMAKE_CXX_COMPILER_LAUNCHER` when `sccache` is found;
  `build.bat` defaults `SCCACHE_DIR=C:\sccache` (confirmed on-disk, 0.74GB, genuinely shared
  across worktrees). This measurably shrinks recompile time and `.obj` churn. It does **not**
  help with FetchContent clone/first-build cost, the final link/PDB step, or (unverified) the
  WSL presets, where no launcher is confirmed wired.
- **Presets already share one `build/` parent.** `CMakePresets.json`'s own preset
  descriptions state intent: *"binaryDir lives under the consolidated `../build/` parent
  alongside every other preset's output"* — the multi-preset-per-worktree problem is about
  `_deps` duplication *within* that shared parent, not about the parent itself being wrong.
- `.gitignore` (`VIXEN/.gitignore:33-35,73-74`) correctly ignores `build/`, `binaries/`,
  `*.pdb`, `*.ilk` — these are intentionally untracked, the CMake output-path choices are the
  actual issue, not the gitignore.

## Fix plan (sequential)

Work through in this order — each is independently shippable and testable; later fixes
compound on earlier ones (e.g. shared FetchContent cache reduces the blast radius of #5).

- [x] **Fix 1 — Shared `FETCHCONTENT_BASE_DIR` across worktrees/presets.** DONE 2026-07-11.
      `CMakeLists.txt` now sets `FETCHCONTENT_BASE_DIR` to `C:/vixen-fetchcontent-cache`
      (override via `VIXEN_FETCHCONTENT_CACHE` env var), guarded to only apply when VIXEN is
      the top-level project (`CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR`) so a parent
      super-build (undertow) keeps control of its own setting. Verified live: fresh configure
      correctly cloned all ~12 deps (freetype, gaia, glfw, gli, glm, googletest, rmlui, tbb,
      etc.) into the shared path; full build compiled 427/429 objects clean — the only 2
      failures (`test_body_instance_raymarch_render.cpp` `setenv`/`unsetenv` MSVC-portability,
      `SdfRecipes.h:85` macro/parse ambiguity) are pre-existing bugs in already-committed files,
      unrelated to this change (confirmed via `git log` on both files). Concurrent-worktree
      write-race safety NOT yet stress-tested (two worktrees configuring simultaneously against
      the shared cache) — worth a follow-up if it becomes a problem in practice.
      **Amendment (found while verifying Fix 4):** Fix 1 exposed a latent bug — 16 call sites
      across `libraries/Profiler/CMakeLists.txt:38`,
      `libraries/RenderGraph/tests/test_critical_nodes.cmake` (9x), and
      `libraries/RenderGraph/tests/test_type_system.cmake` (6x) hardcoded
      `${CMAKE_BINARY_DIR}/_deps/<lib>-src` instead of using the FetchContent-provided
      `${<lib>_SOURCE_DIR}` variable. This "worked" only by coincidence before Fix 1 (when
      `_deps` happened to live under `CMAKE_BINARY_DIR` by default) and broke the moment
      `FETCHCONTENT_BASE_DIR` pointed elsewhere — surfaced as a `C1083: Cannot open include
      file: 'stb_image_write.h'` in `Profiler`. All 16 sites fixed to reference the correct
      `_SOURCE_DIR` variable.
- [x] **Fix 2 — Move `binaries/` under `CMAKE_BINARY_DIR`.** DONE 2026-07-11. Added
      `VIXEN_BINARIES_DIR` (= `${CMAKE_BINARY_DIR}/binaries`) in the root `CMakeLists.txt`;
      replaced all `${VIXEN_ROOT}/binaries` references with it across
      `application/{main,editor,benchmark}/CMakeLists.txt`, `cmake/BenchmarkPackage.cmake`,
      and the SDI-include fallback in `libraries/RenderGraph/CMakeLists.txt:459`. Root cause
      of why this was safe despite `binaries/generated/sdi/` being described as a "runtime"
      SDI directory in `Build-System.md`: everything written there (SDI headers, the
      `vulkan_app_log.txt` log) is written **CWD-relative** by the running exe, not to a
      hardcoded absolute path — so it automatically follows the exe wherever
      `RUNTIME_OUTPUT_DIRECTORY` places it. Measured before moving: the `generated/sdi`
      subfolder itself was ~0 bytes; the actual 18GB was loose `.pdb`/`.ilk`/`.exe` files
      sitting directly in `binaries/`, now correctly reclaimed by `rm -rf build`. Also updated
      `/mnt/c/cpp/_vixen_run_capture.bat` (external capture script, was pointing at a deleted
      worktree besides the stale `binaries\` path) and `Build-System.md` §7/§8.2. Verified
      live: `cmake --build build/ninja --target VIXEN` produced
      `build/ninja/binaries/VIXEN.exe` (44MB) + `.pdb` (228MB) + `.ilk` (348.5MB) + staged
      `assets/`, exit 0.
- [x] **Fix 3 — Stop accumulating duplicate/stale files inside `binaries/`.** DONE 2026-07-11.
      Root-caused: grepped the whole repo for anything producing `_BEFORE`/`_AFTER` or
      `cache_bak_*` naming — nothing does. `vixen_benchmark_BEFORE`/`_AFTER` (13.7MB each,
      timestamps 1 minute apart, 2026-07-02) was a one-off manual A/B perf-comparison snapshot;
      `cache_bak_1872681531/` (117MB, Vulkan-pipeline-cache-shaped `devices`/`global`
      structure, last modified 2025-11-01 — 8+ months stale) was similarly unowned orphan
      debris. Neither is a recurring workflow needing a code fix — both deleted. Then, since
      Fix 2 redirects all new output to `build/<preset>/binaries/`, the entire legacy
      `VIXEN/binaries/` (source-tree location, ~18GB of previous-generation
      `VIXEN.exe`/`.pdb`/`.ilk` + 7 accumulated `vixen_editor_debug*.log` files — the same
      stale-log-accumulation pattern) was superseded and deleted outright. Net reclaim this
      fix: ~18GB.
- [x] **Fix 4 — Disable incremental linking (`.ilk`) for Debug.** DONE 2026-07-11. Not
      load-bearing here: this is a Ninja + sccache workflow, not MSBuild/Visual-Studio-IDE
      incremental rebuild, so `/INCREMENTAL`'s only real benefit (fast re-link after touching
      one `.cpp` in an active IDE session) doesn't apply — the actual iteration-speed lever is
      sccache's object cache. Added an `/INCREMENTAL:NO` override to
      `CMAKE_{EXE,SHARED,MODULE}_LINKER_FLAGS_DEBUG` in `CMakeLists.txt`. **Gotcha that cost a
      debug cycle:** the first attempt placed this override in the `if(MSVC)` block *before*
      `project()`, alongside the pre-existing `/MP`/`/ignore:4099` flags — but CMake only sets
      its own `/INCREMENTAL` default into those variables *inside* `project()` itself, so the
      earlier `set()` was silently clobbered (confirmed via `build.ninja`'s actual
      `LINK_FLAGS`, which still showed bare `/INCREMENTAL`, no `:NO`, after a full reconfigure).
      Moved it into the existing post-`project()` MSVC block (which already carries a comment
      noting the `/FS` PDB fix "must be right after `project()`" — same category of ordering
      constraint). Verified live: full build produced **zero** `.ilk` files anywhere in
      `build/ninja` (was one ~350MB file per exe/test target, hundreds total), all 3 main
      executables linked clean, same 19 pre-existing `SdfRecipes.h`-cascade failures as
      before (no regressions).
      **Also fixed while verifying (unrelated to `.ilk`, found because a fresh full build
      surfaced it):** `libraries/Profiler/CMakeLists.txt:38` hardcoded
      `${CMAKE_BINARY_DIR}/_deps/stb-src` instead of properly consuming the `stb` INTERFACE
      target `dependencies/CMakeLists.txt:84` already defines — another instance of the Fix-1
      amendment's pattern, but this one wasn't fixable by swapping in `${stb_SOURCE_DIR}`
      (that variable is scoped to the `dependencies` subdirectory and isn't visible from
      `libraries/Profiler`'s sibling `add_subdirectory` scope — the working pattern used
      throughout the rest of the codebase is `target_link_libraries(... stb)` against the
      properly-scoped INTERFACE target). Fixed by adding `stb` to `Profiler`'s
      `target_link_libraries` and removing the raw include-dir line entirely.
      **Also fixed:** `build.bat`'s Fix-7 summary logic was intermittently printing garbled
      output (`'%b'` literal, `'else' is not recognized`, contradictory
      "all succeeded"-then-"N failed" messages) — root cause was the script files having
      LF-only line endings; cmd.exe's multi-line `if (...) else (...)` block parsing is
      fragile against LF and needs CRLF to reliably locate block boundaries, especially with a
      `for /f` containing a backtick-quoted command inside the `else` branch. Converted
      `build.bat` and the extracted `run_build_with_summary.bat` (see Fix 7) to CRLF; also
      switched the failure-count logic from a fragile `find /c` two-step temp-file dance to a
      single `findstr ... >nul` + `errorlevel` check, which is both simpler and more robust.
- [x] **Fix 5 — Add worktree build-hygiene guidance to `multi-worktree-sync` skill.** DONE
      2026-07-11. **Gotcha found mid-fix:** this skill is NOT a single canonical file —
      `~/.claude/skills/multi-worktree-sync/SKILL.md` and `~/.codex/skills/multi-worktree-sync`
      (the latter a symlink to `/mnt/c/Users/liory/.agents/skills/multi-worktree-sync/`) are two
      independently-diverged copies (different wording — "Claude/agent" vs "Codex/agent",
      `.claude/worktrees/` vs `.Codex/worktrees/`), not the same file. First pass edited only
      the `.agents`/Codex copy; caught the mistake by checking which file this session's Skill
      tool had actually loaded, then applied the identical edit to the real
      `~/.claude/skills/multi-worktree-sync/SKILL.md`. Both copies now carry the fix; they
      remain separately maintained files going forward, not a single source of truth — worth
      remembering if either is edited again.
      - Finale's teardown steps now include an explicit, verified "confirm the directory is
        actually gone" step (`test -d <path>`) rather than trusting `git worktree remove`'s
        exit code alone — this session directly observed the command report success and
        `git worktree list` show the entry gone while the directory (with a multi-GB `build/`)
        was still on disk minutes later on the `/mnt/c` cross-mount.
      - New guidance for **mid-flight** cleanup (not just teardown): a worktree going dormant
        for more than a day or two should have its `build/` deleted immediately (safe,
        reversible, source/commits untouched) rather than waiting for the finale — this is
        exactly how 100GB+ accumulated silently across 13 worktrees before this session's
        cleanup.
      - New guidance to check for genuine uncommitted work (ignoring build-artifact/cache/log
        paths) before treating an unmerged worktree's directory as disposable — this session
        found real A/B-comparison state and stale debug logs mixed into otherwise-disposable
        output dirs more than once.
      - New "Tooling gotchas" entry naming the root problem as a workflow-discipline gap (no
        build-system change alone fixes it) and recommending periodic `git worktree list` +
        size checks rather than only reacting after the disk is already full.
      - Updated the Quick Reference table to match the new teardown step and added a row for
        the dormant-worktree case.
- [x] **Fix 6 (cross-repo) — Point undertow's `vixen/build` FetchContent at the same shared
      cache as Fix 1.** DONE 2026-07-11. `undertow/vixen/CMakeLists.txt` (the super-build's
      own top-level `project(undertow_vixen ...)`) now sets `FETCHCONTENT_BASE_DIR`
      unconditionally before `add_subdirectory(engine/VIXEN)` — OS-appropriate default
      (`$HOME/.cache/vixen-fetchcontent-cache` on non-Windows, matching this machine's WSL
      undertow checkout at `/home/liory/Github/undertow`; `C:/vixen-fetchcontent-cache` on
      Windows to match VIXEN's own Fix 1 path), overridable via the same
      `VIXEN_FETCHCONTENT_CACHE` env var. VIXEN's own Fix-1 guard
      (`CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR`) correctly no-ops when nested here
      (`CMAKE_SOURCE_DIR` is `undertow/vixen`, not `engine/VIXEN`), so this super-build's own
      `CMakeLists.txt` has to own the setting — confirmed that division of responsibility
      works as designed. **Hit the identical bug Fix 1 hit on its first attempt:** guarding
      with `if(NOT FETCHCONTENT_BASE_DIR)` silently no-ops on any reconfigure after the first
      (a stale value is already cached from the previous run) — removed the guard, `set(...
      FORCE)` unconditionally instead, documented why inline so it isn't "fixed" back to a
      guard later. Verified live: full reconfigure resolved to
      `/home/liory/.cache/vixen-fetchcontent-cache`; a from-scratch `VixenApp` build correctly
      pulled `glfw`/`freetype`/`rmlui`/etc. from that shared path and linked
      `lib/libVixenApp.a` clean (including `Profiler`'s `stb` dependency, confirming the
      Fix-4-amendment fix generalizes correctly to the Linux/GCC toolchain too, not just
      MSVC). Deleted the now-orphaned old `vixen/build/_deps` (615MB reclaimed).
- [x] **Fix 7 — Non-fatal compile failures + build health summary.** DONE 2026-07-11.
      `build.bat`'s build action now invokes ninja with `-k 0` (keep going past failures
      instead of stopping at the first one) and prints a `BUILD SUMMARY` block listing every
      failed target by path, with a pointer to the full log for each failure's compiler error.
      Root problem this fixes: previously, ONE broken target (e.g. a known-broken test) masked
      whether everything else — including the 3 main executables — actually built, because
      ninja's default behavior stops at the first failure. Verified live: a build with 19
      known-broken `SVO`/`RenderGraph` test targets (all transitively hitting the pre-existing
      `SdfRecipes.h:85` parse bug) still correctly linked `VIXEN.exe`, `vixen_editor.exe`, and
      `vixen_benchmark.exe` — and the summary said so in ~20 lines instead of requiring a
      scrollback search through hundreds of ninja lines. Exit code still reflects overall
      health (non-zero if anything failed) so CI/scripted callers aren't fooled into thinking
      a partially-broken build is clean.
      **Amendment (found while verifying Fix 4):** the summary logic was originally an inline
      `:run_build` label `call`ed from `build.bat` — this intermittently produced garbled
      output (literal `%b`, `'else' is not recognized`) from a combination of cmd.exe's
      fragile `for /f` + `EnableDelayedExpansion` behavior inside called labels and the script
      file having LF-only line endings (cmd.exe's multi-line `if(...)else(...)` block parsing
      needs CRLF to reliably find block boundaries). Fixed by extracting the logic into a
      standalone `run_build_with_summary.bat`, invoked as a separate process from `build.bat`,
      and converting both files to CRLF. Also simplified the zero-vs-nonzero-failures check
      from a `find /c` count into `findstr ... >nul` + `errorlevel`, removing a fragile
      temp-file-count step entirely.
- [x] **Fix 8 — Periodic build-health report surfaced to the calling agent/user, not just the
      terminal.** DONE 2026-07-11. Replaced `run_build_with_summary.bat` (Fix 7) with
      `run_build_with_summary.ps1` — rewritten in PowerShell rather than extended in batch,
      because streaming a live log while updating a status file on a timer needs real
      background-job + polling primitives that kept fighting cmd.exe's parser (same class of
      fragility as Fix 7's CRLF/`for`-loop bugs, just worse once you add concurrency). Design:
      `cmake --build` runs inside a `Start-Job` background job, streaming output to a log file;
      the main script polls that log every 5s, regex-parses ninja's `[N/M] <target>` and
      `FAILED: [code=X] <target>` lines, and atomically rewrites (temp file + `Move-Item`, so a
      reader never sees a half-written file) a plain-text status file —
      `%TEMP%\vixen_build_status.txt` by default — with `VIXEN_BUILD_STATUS`
      (`RUNNING`/`DONE`), `elapsed_seconds`, `targets_total`, `targets_done`,
      `targets_failed`, `last_target`, and `failed_targets`. An agent or human can `cat`/
      `Get-Content` that file at any point mid-build for a clean, structured answer to "how's
      it going" instead of tailing raw ninja scrollback. The same regex parse also produces
      the Fix-7-style end-of-build summary, so nothing regressed there.
      **Two real bugs found during verification, both from `Start-Job`'s isolation:**
      (1) the background job does NOT inherit the caller's working directory — it runs in
      PowerShell's own profile default (this machine: `OneDrive\Documents`), so
      `cmake --build --preset` silently resolved `CMakePresets.json` from the wrong location
      and failed outright; fixed by capturing `$PWD.Path` before starting the job and
      `Set-Location`-ing to it first inside the job's scriptblock. (2) `Tee-Object -FilePath`
      writes UTF-16 by default in Windows PowerShell 5.1, which corrupted the regex parsing
      (every character interleaved with a null byte); switched to `Out-File -Encoding utf8`.
      Verified live end-to-end: status file correctly tracked `targets_done` climbing
      0→7→8→34→...→161 of 181 and `targets_failed` accumulating to 19 in real time during an
      actual (non-cached) rebuild, `last_target` reflected current activity, and the final
      state/summary matched the same 19 pre-existing `SdfRecipes.h:85`-cascade failures every
      other verification this session found — no regressions.
- [x] **Fix 9 — Build traffic control: one build at a time, agents queue instead of piling on.**
      DONE 2026-07-11. `run_build_with_summary.ps1` (Fix 8) now acquires a named, machine-wide
      `System.Threading.Mutex` (`Global\VixenBuildLock`) before starting `cmake --build`,
      releases it in a `finally` block (so a killed/crashed build can never wedge the lock —
      the OS releases a Mutex automatically if its owning process dies, unlike a lockfile that
      needs manual cleanup), and writes a `WAITING_FOR_LOCK` state to the Fix-8 status file
      while queued so a polling agent can distinguish "queued behind another build" from
      "building." Default wait is 30 minutes (`-LockTimeoutSeconds`, or
      `VIXEN_BUILD_LOCK_TIMEOUT` env var from `build.bat`); `-SkipLock`/
      `VIXEN_SKIP_BUILD_LOCK=1` bypasses entirely for a machine known to be otherwise idle.
      Added a small standalone `check_build_lock.ps1` (peek-only, `WaitOne(0)`, never blocks
      or acquires) so an agent can check "is a build running right now" before even deciding
      to dispatch one, without spinning up any build machinery to find out.
      **Design detour worth recording:** the first implementation split this into two scripts —
      a generic `acquire_build_lock.ps1` that would `Start-Process` an arbitrary inner
      command, with `build.bat` passing `powershell -File run_build_with_summary.ps1 ...` as
      that inner command via an `-InnerArgs` string. This hung indefinitely: three layers of
      nested process/argument quoting (`build.bat` → the lock script's `-InnerArgs` string →
      `Start-Process -ArgumentList`) silently failed to actually launch the real inner build,
      and `Start-Process -Wait` blocked forever on a child that never really started. Collapsed
      to a single design instead: the mutex acquire/release lives **in the same process** as
      the build itself (plain `try { ... } finally { $mutex.ReleaseMutex() }`), eliminating the
      cross-process argument-passing entirely. General lesson, consistent with Fix 7's
      cmd.exe-nesting bugs: prefer keeping stateful, sequential logic in ONE process over
      composing it from multiple scripts that pass commands-as-strings across process
      boundaries — each hop is a new place for quoting/escaping to silently break.
      **Also found and fixed (parser-level, unrelated to the above):** an em-dash character
      (`—`) inside a double-quoted string literal caused a `MissingArrayIndexExpression`
      parse error — misattributed by PowerShell's error reporting to an unrelated *later*
      line — because Windows PowerShell 5.1's `-File` invocation reads a script without a BOM
      using the system's legacy ANSI codepage, not UTF-8, so a raw multi-byte UTF-8 character
      inside a string can desync the parser's quote-matching. Fixed by adding a UTF-8 BOM to
      both `.ps1` files and replacing the one in-string em-dash with a plain ASCII hyphen
      (comment-only em-dashes elsewhere in both files were harmless, since comments don't need
      string-terminator balance) — worth remembering for any future `.ps1` invoked via `-File`
      rather than a BOM-aware editor/host.
      **Verified live**: two genuinely concurrent `build.bat build` invocations correctly
      serialized (`B` blocked on "Waiting for the machine-wide build lock..." with no
      "acquired" message while `A` held it; `B` acquired within ~1s of `A`'s release, logged
      as "acquired after waiting 148s"), both completed with correct/consistent results, lock
      confirmed `FREE` via `check_build_lock.ps1` after both finished.
      **Not yet done** — this covers builds dispatched through `build.bat` on THIS machine;
      it does not (and structurally cannot) coordinate across separate physical machines, and
      an agent invoking `cmake --build` directly (bypassing `build.bat`) skips the lock
      entirely. See the `multi-worktree-sync` skill amendment below for the
      agent-orchestration-policy half of this fix (when to check before dispatching, what to
      do while queued).
- [x] **Fix 10 — Throttle build parallelism to avoid resource-hogging the machine.** DONE
      2026-07-11. Two separate caps, because compile and link have very different resource
      profiles on this project:
      - **Overall ninja job count**: `run_build_with_summary.ps1` now passes `-j
        $MaxParallelJobs` to `cmake --build`, defaulting to 75% of logical cores
        (`[Environment]::ProcessorCount`) — 12 on this 16-core machine. Override via
        `-MaxParallelJobs` or `VIXEN_MAX_BUILD_JOBS` env var (documented in `build.bat`'s
        header comment alongside the Fix 9 lock env vars).
      - **Concurrent LINK jobs, capped lower and separately**: a CMake `JOB_POOLS` global
        property (`link_pool`, depth = `(logical_cores + 3) / 4` = 4 on this machine) assigned
        via `CMAKE_JOB_POOL_LINK`, in `CMakeLists.txt`. Link, not compile, was the actual
        RSS-heavy step this project's stutter came from — `link.exe` for these debug binaries
        (with `/Z7` embedded debug info, Fix 4) runs 400-500MB RSS each; running as many links
        in parallel as cores was the real problem, not raw compile-job count. Override via
        `VIXEN_MAX_PARALLEL_LINKS` env var.
      **Found and fixed a real, pre-existing, unrelated bug while implementing this:** the
      original "Parallel compilation (MSVC)" `if(MSVC)` block (source of the pre-existing
      `/MP` flag, third-party warning suppression, etc.) sat **before** `project()` in
      `CMakeLists.txt` — meaning `MSVC` was never actually `TRUE` at that point (CMake hasn't
      done compiler detection yet), so the entire block was silently dead code, always, not
      just for my new job-pool addition. Confirmed conclusively: a bare
      `message(STATUS "DEBUG_MARKER...")` placed at the very top of that block never printed,
      in a completely clean from-scratch configure, run directly via `cmake --preset` with no
      wrapping scripts involved at all. `/MP` still showed up in the generated `build.ninja`
      anyway — turned out to be CMake's own built-in default flags for the Ninja+MSVC
      combination, not this block — which is exactly why the bug went unnoticed: the visible
      symptom (parallel compiles happening) looked identical to the block "working." Fixed by
      moving the whole block's real content into the existing post-`project()` `if(MSVC)`
      block (the same one Fix 4's `/INCREMENTAL:NO` already had to live in for the identical
      structural reason — CMake only finalizes several MSVC-related defaults inside
      `project()` itself). This is now the second occurrence of this exact class of bug in
      this file (see Fix 4); worth a standing rule: **any `if(MSVC)`/compiler-conditional
      block in this file must go after `project()`, no exceptions** — consider a lint pass or
      a comment banner right before `project()` warning against adding one above it.
      **Verified live**: a full build showed `link.exe` process count holding steady at
      exactly 4 throughout the entire multi-minute linking phase (confirmed via repeated
      `tasklist` polling), `cl.exe` count around 11-12 during compilation (matching the 12-job
      cap), console printed "Max parallel jobs: 12 (of 16 logical cores...)" and "Capping
      concurrent link jobs at 4," and the build produced the same 19 pre-existing
      `SdfRecipes.h:85`-cascade failures as every other verification this session — no
      regressions from either the parallelism caps or the dead-code fix.

## Verification per fix

For each fix above: build clean in a fresh worktree before/after, compare `build/` +
`binaries/` size, and confirm no regression via the existing test suite
([[Testing]]) before merging. Use [[wsl-windows-side-scan]] (global skill,
`~/.claude/skills/wsl-windows-side-scan/`) to measure sizes quickly — `du`/`Get-ChildItem
-Recurse` over `/mnt/c` are too slow for this kind of before/after comparison.

## Sibling cleanup: benchmark runner + Profiler library removal (2026-07-11)

Not one of the numbered fixes above (not about build-artifact accumulation), but done in the
same session as a related "delete unused research-platform cruft" pass, and it touched the
build graph enough to be worth recording here.

**Trigger**: user identified `vixen_benchmark` (the headless compute benchmark exe) as an
unused research-platform artifact to delete outright.

**Investigation found it wasn't a clean single-directory delete.** `application/benchmark/`
itself was fully self-contained and safe to delete wholesale, but the exe's dependency,
`libraries/Profiler/`, turned out to be a single flat library mixing genuinely-generic
telemetry code with benchmark-orchestration code with no internal boundary between them —
confirmed by tracing: `TestConfiguration` (`FrameMetrics.h`, explicitly commented "Test
configuration for a single benchmark run") was threaded through `MetricsSanityChecker`,
`MetricsExporter`, `SceneInfo`, and `FrameMetrics` itself — the very files that looked
"generic" from their names. `ProfilerSystem` (the class `application/main` actually linked
`Profiler` for) turned out to expose a whole benchmark-shaped "Test Run"/"Test Suite" API
(`StartTestRun`/`EndTestRun`/`StartTestSuite`/`RunTestBatch`) that **zero real app code
called** — `application/main`/`application/editor` were confirmed to use exactly one thing
from the entire library: the standalone, zero-dependency `FrameCapture` (swapchain→PNG
readback for `CaptureFrameToPng`). Per user direction — delete unused/entangled code outright
rather than preserve or redesign it; keep only what's genuinely referenced, as a clean
self-contained piece — the resolution was:

- **Deleted wholesale**: `application/benchmark/` (exe, CLI, configs), `cmake/BenchmarkPackage.cmake`,
  the two `add_subdirectory`/`include` lines pulling them into the build, `libraries/Profiler/`
  in its entirety (`ProfilerSystem`, `MetricsCollector`, `MetricsExporter`,
  `MetricsSanityChecker`, `RollingStats`, `DeviceCapabilities`, `SceneInfo`, `FrameMetrics`,
  `NVMLWrapper`, `BenchmarkRunner`, `BenchmarkGraphFactory`, `BenchmarkConfig`,
  `VulkanIntegration`, `TesterPackage`, `ProfilerGraphAdapter`, `TestSuiteResults`,
  `ProfilerHeaders.h` PCH, `tests/test_profiler.cpp` — the whole library was already
  non-compiling mid-cleanup once the benchmark-only files were removed first, independently
  confirming nothing outside the benchmark machinery depended on the rest of it), the
  `Profiler` entries in `cmake/VixenInstall.cmake`'s export-target lists, the dead
  `application/benchmark/generated/sdi` include-path fallback in
  `libraries/RenderGraph/CMakeLists.txt`, the partially-git-tracked `VixenBenchmark/` output
  dir (52 files force-added despite `.gitignore`), the untracked `benchmark_results/` scratch
  dir, and a stale `pkill -f vixen_benchmark*` entry in `.claude/settings.json`.
- **Moved, not deleted**: `FrameCapture.h`/`.cpp` relocated from `libraries/Profiler/` directly
  into `application/main/{include,source}/` (its only real consumer) — folding a
  single-file-pair "library" into its sole consumer is cleaner than keeping a pointless
  one-file CMake target around. Kept the `Vixen::Profiler` namespace (meaningful prior art,
  not a leftover of the old path). `VixenApp` now links `stb` directly (for
  `FrameCapture.cpp`'s `stb_image_write.h`/`stb_image_resize2.h`) instead of the `Profiler`
  target.
- **Verified live**: full clean configure+build (`build/ninja` deleted and rebuilt from
  scratch) produced exactly the same 19 pre-existing `SdfRecipes.h:85`-cascade failures as
  every other verification this session — no new regressions — and exactly 2 executables in
  `binaries/` (`VIXEN.exe`, `vixen_editor.exe`; no `vixen_benchmark.exe`). `Profiler.lib` and
  every trace of the benchmark target are absent from `build.ninja` (`grep -c` = 0).
- **Left as-is (low priority, doc-only)**: prose comments in
  `libraries/RenderGraph/include/Core/{EngineConfig,EngineContext,RenderGraph}.h`,
  `Nodes/FrameSyncNode.h`, `tests/Nodes/TestVkValidation.h`,
  `libraries/CashSystem/include/MainCacher.h`, and
  `libraries/RenderGraph/tests/test_core_systems.cmake` reference `BenchmarkRunner` as a
  design example — harmless (not compiled code) but now stale; worth a follow-up comment pass,
  not a build blocker.

## Sibling addition: queue/notification layer + cross-repo discoverability (2026-07-11)

Not one of the numbered fixes (Fix 9 already covers the lock+throttle itself), but a direct
follow-on: Fix 9's lock is a raw blocking `WaitOne()` with zero visibility into position or
ETA — an agent that hits a held lock has no way to go do other work and come back only when
it's actually their turn. Added `VIXEN/scripts/build/build_queue.ps1`: a FIFO ticket queue
layered ABOVE the Fix-9 Mutex (which still does the real acquire/release for the build
itself). Register → get a `TicketId` + position instantly → go do other work → poll `-Status`
(via `ScheduleWakeup`, not a tight loop) → `YOUR_TURN` when position 1 AND the lock is free,
distinguished from `WAITING` (position 1 but lock still held by an in-flight build) so a
polling agent knows whether it's next-in-line-but-blocked vs. genuinely not next yet. Tickets
are files under `%TEMP%\vixen_build_queue\`, FIFO by an embedded creation timestamp (not
filesystem mtime); every queue command opportunistically reaps tickets older than 60 minutes
so a crashed/killed agent's ticket doesn't block the queue forever (unlike the Mutex, a
ticket file has no OS-level auto-release — callers must still `-Release` explicitly, the reap
is a backstop not the primary path). Verified live: 2-ticket FIFO ordering, release-and-
advance (releasing ticket A correctly promoted ticket B to `YOUR_TURN`), and the
position-1-but-lock-held distinction, all confirmed against a real concurrent `build.bat`
invocation.

**Moved all three build scripts** (`run_build_with_summary.ps1`, `check_build_lock.ps1`,
`build_queue.ps1`) from the outer repo root into `VIXEN/scripts/build/` — they'd been sitting
at `/mnt/c/cpp/VBVS--VIXEN/` (the outer worktree-container root) where only this specific
checkout could find them. Moving them inside `VIXEN/` means they travel with the engine
wherever it's embedded (e.g. undertow's `vixen/engine` submodule) instead of being
invisible/unreachable there. `build.bat` updated to reference the new path
(`%SRC_DIR%\scripts\build\...`); reconfigure+build re-verified working from the new location
(same 19 pre-existing failures, no regressions).

**Cross-repo scope, deliberately bounded**: investigated wiring undertow's own `vixen/` build
into this lock/queue and found a real platform wall — undertow's build runs directly via
`cmake --build vixen/build` on WSL/Linux, no `.bat` wrapper, while the lock is a
`System.Threading.Mutex` (a Win32 kernel object with no equivalent global-namespace semantics
on Linux) and the queue script is Windows PowerShell. Building a genuinely cross-platform
lock (e.g. a flock-based lockfile both OSes can reach) was considered and deferred — it only
matters if a WSL undertow build and a Windows VIXEN build are ever actually run concurrently
on the same physical machine, which wasn't confirmed as a real scenario, and the two
toolchains have different resource ceilings anyway (matches this project's existing
Windows-preferred convention for GPU/render work). Documented the boundary explicitly instead:
a note in `undertow/CLAUDE.md` (near the existing `Undertow.View`/VIXEN-render-integration
section) states plainly that `vixen/`'s build is NOT coordinated by any of this tooling — a
deliberate scope boundary, not an oversight to silently work around later.

**New skill**: `VIXEN/.claude/skills/vixen-build-policy/SKILL.md` — the policy doc for all of
the above (when to check the lock before dispatching, how to use the queue, why parallelism
is capped not maximized, the full env var table, explicit "what NOT to do" list including the
platform-scope boundary). Cross-references (doesn't duplicate) the `multi-worktree-sync`
skill's build-traffic-control section, which states the general cross-project principle this
skill implements concretely for VIXEN.

**Follow-on fix (2026-07-11): queue ticket auto-release + ccache-over-sccache for MSVC PCH.**
Two more gaps surfaced live while dispatching a real Inc-1 milestone build in a fresh worktree:

1. **Queue tickets were only ever released by the dispatching agent calling `-Release` after
   `build.bat` returned** — if that agent got killed, crashed, or had its context cleared
   mid-build, the ticket sat blocking the queue until the 60-minute stale reap. Fixed:
   `run_build_with_summary.ps1` now takes a `-QueueTicketId` param (wired through `build.bat`'s
   `VIXEN_QUEUE_TICKET_ID` env var) and releases it itself in the same `finally` block that
   already releases the build-lock Mutex — ticket lifetime is now tied to the build *process's*
   lifetime, not the dispatching agent's. Also writes the outcome (exit code, failed targets,
   log path) to `%TEMP%\vixen_build_queue_results\<TicketId>.log` so a different agent (or the
   same one resumed later) can read what happened without having stayed attached.
2. **sccache measured at only 38.9% cache-hit rate on a fresh worktree's clean build**, with
   `/Fp` (MSVC precompiled-header flag, 310 occurrences) as the single largest non-cacheable
   bucket. Root cause confirmed via sccache's own source (`src/compiler/msvc.rs`): `/Fp` and
   `/Yc` are unconditionally routed as `TooHardPath` — sccache cannot cache MSVC PCH
   compilation at all (open upstream issue `mozilla/sccache#978`, unresolved since 2021, no
   roadmap commitment). VIXEN's `target_precompile_headers()` is used across 7 core libraries
   (Core, EventBus, CashSystem, GaiaArchetypes, GaiaVoxelWorld, Logger, RenderGraphCore) — every
   fresh worktree on this multi-agent machine was paying full uncached PCH-compile cost for all
   of them, independent of the already-correct shared FetchContent/sccache-dir config (Fix 1).
   ccache added real MSVC `/Yc` PCH support in 2024 (refined through 2025 point releases) and is
   the documented fix other CMake projects (e.g. Qt) use for this exact gap. Fixed:
   `CMakeLists.txt`'s `USE_CCACHE` block now prefers ccache over sccache (falls back to sccache
   if ccache is genuinely unavailable, rather than failing configure). New
   `VIXEN/cmake/ProvisionCcache.cmake` self-provisions ccache on Windows (downloads the official
   prebuilt `ccache-4.13.6-windows-x86_64.zip`, ~4MB, into a gitignored `.ccache-deps/` cache —
   same precedent as `ProvisionVulkan.cmake`/`ProvisionGdb.cmake`: system tool wins, cache is
   project-local, no system-wide install) so no one has to hand-install ccache the way sccache
   apparently was. Verified end-to-end via `cmake -P` script-mode: real download, real
   extraction, provisioned `ccache.exe --version` runs standalone (no missing DLLs), and a
   second run correctly reused the cache with no re-download.

**Follow-on fix (2026-07-11): per-build BuildId for multi-worktree disambiguation.** Dispatching
builds from different worktrees in succession (or concurrently) made it hard to tell which
log/status/binary belonged to which request — the shared status file's `last_target` had already
been observed pointing at a sibling agent's build (see the "shared status file is machine-wide"
gotcha in `vixen-build-policy`). Fixed: `run_build_with_summary.ps1` now takes a `-BuildId`
(caller-supplied or auto-generated 8-char hex), printed as the FIRST line of console output,
embedded directly in the log filename (`%TEMP%\vixen_build_<BuildId>.log` — replacing the prior
anonymous random-GUID naming), written into the status file as a `build_id:` field, and repeated
in the BUILD SUMMARY footer. `build.bat` defaults it to the checkout's own directory name (a
worktree's builds self-identify with zero setup — `.claude\worktrees\graph-node-linkage-inc1\`
→ `BuildId=graph-node-linkage-inc1`), overridable via `VIXEN_BUILD_ID`. Verified the directory-
name derivation logic in isolation (worktree path → worktree folder name; main checkout →
`VBVS--VIXEN`) and the BuildId sanitize/auto-generate logic (empty → random hex, clean names
pass through, unsafe characters replaced) via standalone PowerShell snippets.

**Follow-on fix (2026-07-11): queue de-dup to stop repeated redundant registrations.**
`build_queue.ps1 -Register` had no protection against the same agent registering multiple
tickets for the same build request (retry, duplicate dispatch, a confused re-invocation),
which piles up redundant tickets and pushes back everyone else's queue position for no reason.
Fixed: `-Register` now takes `-Source <worktree-or-repo-path>` and `-BuildTarget <cmake-target>`
alongside the existing `-AgentId`, and de-dups on the exact combination of all three — an
existing non-stale ticket matching `(AgentId, Source, BuildTarget)` is returned as-is instead of
creating a duplicate. Deliberately NOT keyed on `AgentId` alone: the same agent building a
different target, or from a different worktree, is a distinct, legitimate request and correctly
gets its own new ticket, appended at the back of the queue behind everyone already
waiting — verified live against the real, active queue (3 genuine in-flight tickets from other
agents at the time): registering `(agent, worktree-A, VixenApp)` twice returned the identical
ticket both times at the same position; registering `(agent, worktree-A, test_node_self_registration)`
— same agent, different target — correctly created a new ticket at position 5 (behind all
existing entries, including the agent's own first ticket), not ahead of anyone. `-ListQueue`
output now also shows each ticket's `source=`/`target=` for visibility. Older tickets
(registered before this fix, with no `Source`/`BuildTarget` fields) coexist fine — they display
as `source=-  target=(full build)` and dedup correctly against a caller who also omits both.

**Follow-on fix (2026-07-11): liveness-based reaping to close a live queue-stall incident.**
User confirmed the lock/queue stack was holding up well under real 8+-worktree congestion, then
minutes later a real incident occurred: `inc1-m2-validator` (a validator agent) self-registered
a queue ticket for its own ad-hoc verification build — outside the `build.bat`-auto-release path
(that fix only covers tickets passed through `VIXEN_QUEUE_TICKET_ID` to an actual `build.bat`
dispatch) — then finished its work and shut down without ever calling `-Release`. Its abandoned
ticket sat ahead of `inc1-m4-linkage`'s real ticket in the queue; the lock was actually FREE, but
`-Status` still reported `WAITING` because the stale ticket was still "ahead" positionally, and
the old 60-minute stale-reap window meant it would have blocked for most of an hour before
auto-clearing. Manually released to unblock in the moment, but flagged as a structural gap: "the
queue working properly is a path-critical fix — anything that can stall it stalls all agent work
eventually."

Root fix: reaping is now based on **liveness** (`LastSeenUtc`, refreshed on every `-Register`
dedup-hit and every `-Status` call for that ticket), not just **age since registration**
(`CreatedUtc`). A genuinely active waiter polling `-Status` every ~20s (the standing active-
polling rule) never goes stale, however long it legitimately waits — while an abandoned ticket
(no check-in since registration) goes stale within the now-much-shorter default window (15
minutes, down from 60). `-Status` also opportunistically reaps its own blocking ticket **inline**
the instant it discovers it's stuck behind one, rather than waiting for the next periodic sweep
or the full stale window — verified live: registered a synthetic abandoned ticket + a synthetic
waiter behind it, backdated the abandoned ticket's `LastSeenUtc` past the threshold (simulating
real abandonment without waiting 15 real minutes), confirmed a `-Status` call from the waiter
correctly reaped the abandoned ticket and the waiter's position updated correctly; confirmed
`LastSeenUtc` genuinely refreshes on real `-Status` polls (proving an active waiter would never
have been reaped); ran against the REAL live queue mid-fix and it immediately reaped one
genuinely stale pre-existing ticket (`67a333d7ad9b`, unseen 20+ minutes) with zero disruption to
the one real active ticket. Old-format tickets (no `LastSeenUtc` field) fall back to `CreatedUtc`
so they still reap on a sane schedule rather than erroring or never expiring. `-Release` remains
the correct primary path — reaping is documented explicitly as the abandonment backstop, not a
substitute for releasing your own ticket.
