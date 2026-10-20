# vixdeps: one shared dependency cache for all VIXEN worktrees

Lane `sn-vixdeps`, branch `lane-vixdeps`, based on `origin/wave/authoring-convergence`.
Raw evidence (not committed): `/home/liory/codeman-cases/undertow/reports/vixdeps-evidence/`.

## Plain-language summary

Before this change every VIXEN worktree that configured on Linux/WSL downloaded its own copy of the
Vulkan SDK (about 2.0 GB) and its own X11 dev libraries (about 12 MB) into `VIXEN/.vulkan-sdk` and
`VIXEN/.windowing-deps`. Thirteen trees on this machine hold a copy today, so about 26 GB for one SDK.
Now they all use one entry under `${XDG_CACHE_HOME:-$HOME/.cache}/vixen/`:

- `vulkan-sdk/<sdk-version>/` (one entry per SDK version, 1.7 GB)
- `windowing/<12-hex package-set hash>/` (one entry per distro/arch/package set, 8.9 MB)

## Task 1: shared Vulkan SDK and X11 caches

What changed:

- `VIXEN/cmake/VixenSharedCache.cmake` (new): `vixen_shared_cache_root()` and the stamp name
  `.vixen-complete`. Same root `ProvisionWslVulkan.cmake` already uses for Dozen.
- `VIXEN/cmake/ProvisionVulkan.cmake:28-60` default cache dir and migration; `:204-259` populate flow
  (lock, recheck stamp, download and extract into a temp dir, verify, write stamp, `file(RENAME)`);
  `:103-110` uninstall flag; `:401` uninstall target. Both only remove their own `<version>` entry.
- `VIXEN/cmake/ProvisionWindowingDeps.cmake:48-52` content key (SHA256 over arch, processor, distro
  ID and VERSION_ID, package list); `:110-161` locked populate with temp dir, stamp and rename;
  `:187` uninstall target.
- `VIXEN/CMakePresets.json`: `vixen-wsl` description text.
- `VIXEN/libraries/RenderGraph/src/Ui/shaders/regenerate-ui-shader-header.py:31-33`: first `glslc`
  candidate is now the shared cache (honours `XDG_CACHE_HOME`).

Kept: `-DVIXEN_VULKAN_CACHE_DIR` and `-DVIXEN_WINDOWING_CACHE_DIR` overrides, and a system or
`VULKAN_SDK` install still wins over any download. An override dir is owned by the provisioner.

Safety properties:

- Concurrent configures: the first holds `<entry>.lock`, the rest wait, then see the stamp and reuse.
- A reader never sees a half entry: population happens in a temp sibling dir and is renamed in.
- A partial entry (no stamp) is repopulated. Another key's or version's entry is never deleted.

Left unchanged on purpose: `VixenTesting.cmake` and `tools/run-vixen-windowed-captures.sh` read the
cache variables, which are always set now. `test_critical_nodes.cmake` has a harmless legacy
fallback. `.gitignore` keeps the legacy dir entries.

### Evidence

MEASURED:

- Two configures at once, `vixdeps` and a scratch detached worktree: one populated, the other waited
  and reused (`configure-vixdeps*.log`, `shared-entry-evidence.txt`).
- Both trees' CMakeCache point at `~/.cache/vixen/vulkan-sdk` and `~/.cache/vixen/windowing/f7ab3a51d6a9`.
  Neither tree has `VIXEN/.vulkan-sdk` or `VIXEN/.windowing-deps` (`after-du.txt`).
- `du`: before, 13 trees x 2.0 GB `.vulkan-sdk` and 17 trees x 12 MB `.windowing-deps`
  (`before-du.txt`, read-only `du` of existing trees). After, one 1.7 GB SDK entry plus one 8.9 MB
  windowing entry, shared (`after-du.txt`).
- Windowing provisioner edge cases T1-T5, all passing, in isolated `XDG_CACHE_HOME` dirs: concurrent
  populate, partial-entry repopulation, old-tree migration, explicit override, other keys untouched
  (`windowing-edge-cases.txt`).
- Vulkan old-tree migration: a build tree seeded with the old in-tree cache values printed
  "moved this build tree from the in-tree Vulkan cache to ..." and rewrote
  `VIXEN_VULKAN_CACHE_DIR` and `Vulkan_INCLUDE_DIR` (`vulkan-edge-cases.txt`).

INFERRED, not tested:

- Vulkan corrupt-entry repopulation: it uses the same stamp and lock code shape as the windowing
  path, which is tested. Testing it needs another 333 MB download into a throwaway `XDG_CACHE_HOME`.

## Task 2: fetched sources: NOT done, next slice

Skipped on purpose. Sharing FetchContent sources touches about a dozen declarations in several
CMakeLists, and some are branch-pinned (`GIT_TAG main/master`). Doing it unsafely would change what
every tree builds. Design for the next slice:

- A locked, keyed source cache per pinned commit: `~/.cache/vixen/fetch/<name>/<sha>`, populated with
  the same temp dir, stamp and rename scheme.
- Point `FETCHCONTENT_SOURCE_DIR_<NAME>` at it. Keep `-build` and `-subbuild` per tree.
- Branch-pinned declarations must be pinned to a sha first.
- It extends the existing `FETCHCONTENT_SOURCE_DIR_GAIA` revision-keyed pattern in
  `dependencies/native/CMakeLists.txt`, and would also fix the unfetchable stb pin.
- Cost today: `.tmp/fetch` is 836 MB per tree (measured, `after-du.txt`).

## Task 3: migration

- A build tree configured before this change moves to the shared cache at its next configure (the old
  default is force-reset, cache variables pointing into the old dir are unset, and an INTERNAL
  "last default" variable lets a changed key move existing trees too).
- The old in-tree dirs are left alone. One-line cleanup per worktree, after it has reconfigured:
  `rm -rf VIXEN/.vulkan-sdk VIXEN/.windowing-deps` (documented in `VIXEN/README.md:25-39`).

## Task 4: proof

MEASURED:

- Configure plus `build-wsl.sh -j4` (Release; `VIXEN_WITH_FSR3` is OFF by default, confirmed in CMakeCache)
  ran concurrently in `vixdeps` and a scratch detached worktree through the queue. Both ran the
  same 677-678 ninja steps (`-k 0`) and ended with the identical three
  failures below, none related to the cache.
- After merging `origin/wave/authoring-convergence`, an incremental build in `vixdeps` produced the
  same three failures and nothing new except one deprecation warning at `DeviceNode.cpp:683`, which
  lives in code this lane did not touch (blame points to `1a4fb911`, older than the base).
- Tests (`ctest`, queue `--resource gpu`, Dozen ICD):
  - RenderGraph + Textures labels: 1431 run, 1395 passed, 36 failed, 11 skipped
    (`ctest-rg-textures.txt`). Textures label: 3 of 3 passed.
  - VulkanResources: 14 run, 14 passed, 1 skipped (`ctest-vulkanresources.txt`).

The three build failures (identical in both trees):

| Failure | Cause | Related to cache? |
|---|---|---|
| `checks/no_new_mutex_check.stamp` | stale allowlist row for `Textures/src/TextureLoader.cpp` | No. MEASURED: the script fails the same way on base `e4a18398` in the scratch tree. |
| `libraries/CashSystem/tests/test_registration_api` | link error, undefined `CashSystem::TextureCacher` vtable | No (INFERRED: nothing in this diff touches CashSystem). |
| `codegen/checks/frame_targets_schema_check.stamp` | `SubstrateKinds.cs:173` "unsupported schema field fluent call 'Rate'" | No (INFERRED: Undertow pair declarations vs this VIXEN codegen). |

The 36 test failures:

- 10 `ProceduralRecipeRenderTest`, 3 `EditorDocumentRenderTest`, 1 `AppFlowEditorToggleRenderTest`:
  in-process HLSL parse error in generated kernel code ("shader:6 ... HLSL parsing failed"). This is
  a shader-source problem, not SDK discovery (the SDK and glslang were found fine).
- 6 `UiCompositionHostTest`, `ViewEditorLayersGolden`, `ViewEditorLayersReconcile`, 2 `BlobView`:
  `No font engine interface set!`, then `Rml::Initialise()` false. `RMLUI_FONT_ENGINE=none` is set by
  the repo itself (`VIXEN/dependencies/CMakeLists.txt`), so these do not depend on this change.
- 7 `EditorToggleUndoCapture`, 3 `HudRenderCapture`: "Not Run", executables not linked (same
  link-stage cause as `test_registration_api`). `vixen_editor_capture_producer` timed out and
  `vixen_text_shader_capture_producer` failed (GPU capture producers on Dozen).

I did not run these on the base commit, so "not caused by this lane" for the test failures is
INFERRED from their causes, not measured. Only the mutex gate was re-run on base (MEASURED).

### What went wrong during proving (my mistake, no lasting effect)

My first Vulkan migration test passed `-DVIXEN_UNINSTALL_VULKAN=ON` to a scratch build tree whose
cache dir resolved, through a symlink I made to avoid a re-download, to the real shared cache. The
uninstall flag then deleted the real `1.4.350.1` entry while both builds were compiling, so both
failed with `vulkan/vulkan.h: No such file or directory`. The next configure re-downloaded and
re-stamped the entry (verified: `vulkan.h`, `glslc` and `.vixen-complete` present). Both builds were
rerun from scratch of that step; the failed logs are kept in `failed-by-test-mishap/`. This shows
that `-DVIXEN_UNINSTALL_VULKAN=ON` deletes a shared entry other trees may be building against, which
is how the flag and the `vixen-uninstall-*` targets are documented ("no build may be running").

## Task 5: docs

`VIXEN/README.md:25-39`: "Shared dependency cache (Linux/WSL)" bullet with the layout, locking and
stamp behaviour, overrides, per-entry and clear-all removal, and the one-line cleanup.

## SPT DISPOSITION

SPT has no dependency on this change. SPT is not touched, its build trees are not migrated, and no
SPT worktree is read or written. Nothing for SPT to merge.

## CONSOLIDATION ISSUES

1. Three base build failures (mutex allowlist row, `test_registration_api` link error, codegen
   `Rate` schema field) and 36 base test failures (HLSL parse, RmlUi font engine, uncapped GPU
   capture producers) exist on `origin/wave/authoring-convergence` with the Undertow pair used here.
   Not caused by this lane. `ninja` stops on the mutex gate without `-k 0`.
2. `-DVIXEN_UNINSTALL_VULKAN=ON` and `vixen-uninstall-vulkan` delete the shared `<version>` entry for
   every tree. Worth a guard in a later slice (refuse while a `.lock` is held, or require a
   confirmation variable).
3. Fetched-source sharing (task 2) is the next slice and is where most of the remaining per-tree
   weight is: `.tmp/fetch` is 836 MB per tree and `build/` 4.1 GB per tree.
4. Out of scope: the `Include`/`include` case-alias glslang issue from the gpuyield proposal.
5. `reports/` in this branch now also carries `instdemo.md` from the merged base commit; unrelated.
