# swvulkan1 — 0ew software Vulkan opt-in report

Date: 2026-09-02
Branch: `lane-swvulkan`

## Delivered in the worktree

- `VIXEN/libraries/VulkanResources/include/VulkanGlobalNames.h`
  - `VixenSelectWslGpuIcd()` now honors any explicit `VK_ICD_FILENAMES` first.
  - On WSL (`/dev/dxg`), it selects an existing Dozen manifest, otherwise throws by default.
  - The throw names `VK_ICD_FILENAMES`, the expected manifest path, and
    `VIXEN_ALLOW_SOFTWARE_VULKAN=1`.
  - The opt-out permits the old software fallback and emits one loud lavapipe/llvmpipe line per
    process.
  - If provisioning did not define `VIXEN_WSL_DZN_ICD`, the expected path is reconstructed from
    `XDG_CACHE_HOME`/`HOME`, so failed or disabled provisioning remains strict.
- The five intentional software render-smoke setups opt in in-process before selector use:
  - `test_editor_document_render.cpp:235`
  - `test_recipe_pool_render.cpp:188`
  - `test_procedural_recipe_render.cpp:182`
  - `test_recipe_authoring_gate.cpp:204`
  - `test_appflow_editor_toggle_render.cpp:198`
  Each comment cites 0ew and the real-GPU `IsRealGpu` precedent in
  `test_recipe_glsl_numerical_parity.cpp`.
- Added `test_vulkan_global_names_policy`, covering strict throw, software opt-in, and explicit ICD
  honoring with a deliberately missing manifest path.
- Updated WSL provisioning/README/consumer-feedback documentation for the strict runtime policy.

## Software-landing census

- Explicit opt-in sites: the five render-smoke `SetUp()` sites listed above.
- Honored-env path: `VIXEN/libraries/VulkanResources/include/VulkanGlobalNames.h`; any caller that
  supplies `VK_ICD_FILENAMES` is accepted without replacement or throw.
- All other selector callers remain strict by default on WSL; they can only land on software when
  their caller/environment makes that choice explicit.

## Verification

- `git diff --check`: passed.
- First fresh configure through the queue: reached CMake, then failed before generation because the
  shared FetchContent `glm` sub-build used Ninja while the fresh worktree configure used Unix
  Makefiles. The failed worktree-only `build/` directory was removed with `cmake -E remove_directory`.
- Second fresh configure through the queue with `-G Ninja`: remained queued for approximately
  17.5 minutes behind active build/test jobs and was cancelled when the queue remained blocked.
- Five render-smoke binaries: not run; configure/build was not admitted.
- Negative witness: not run; configure/build was not admitted.
- Full build/full test suite: not run; configure/build was not admitted.

## Not delivered vs brief

- No commit was created. The repository clean-commit gate forbids committing without a fresh full
  build and full test-suite summary, and the queued configure was environmentally blocked.
- No per-binary pass counts or negative-witness runtime evidence are available yet.
