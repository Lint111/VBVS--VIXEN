# cx-readyfence2

STATUS: COMPLETE

## CAUSE

The ready fence stalled at published serial 6 because `VulkanGraphApplication::Update()` captured
`updateEditSerial` before `EditorApplication::Update()` ran its deferred document dirty tail. A
single program edit then advanced the shared ledger twice: `ReplaceProceduralRecipe()` advanced it
for the shader replacement and `SetBodyInstances()` advanced it again for the body-instance row
(`VIXEN/application/editor/source/EditorApplication.cpp:1350-1354, 798-879`). The settled path used
the stale snapshot (`VIXEN/application/main/source/VulkanGraphApplication.cpp:2643-2648`), so the
next settled frame could be associated with 6 while the channel waited for serial 8.

The prior stalled run records `publishedSerial: 6`, `serial: 8`, and `ready: false` in
`/home/liory/codeman-cases/undertow/reports/cx-readyfence1.md` and the preserved editor trace at
`/home/liory/codeman-cases/undertow/reports/cx-readyfence2-evidence/gpu-normal-10.editor.log`.

## CHANGE

- `VIXEN/application/main/source/VulkanGraphApplication.cpp:2648` now carries the live highest
  serial with `std::max(updateEditSerial, recipeEditSerial_)` after a settled recompile.
- `VIXEN/application/main/tests/test_recipe_preview_fence.cpp:48-62` adds the exact coalesced-row
  sequence: serial 6 is already published, the edit rows advance through 7 and 8, and publication
  of 8 satisfies waits and success checks for both rows.
- No experimental render-pause or pipeline-warmer changes remain in the final diff.

## WITNESS

- Final Release editor build: PASS, zero warning lines in the target output. Queue log:
  `/home/liory/.local/state/undertow/undertow-box-logs/1794596990-build-cx-readyfence2:final-editor-release-zero-warning.log`.
- Focused `RecipePreviewFence.*`: PASS, 4/4. Queue log:
  `/home/liory/.local/state/undertow/undertow-box-logs/1794596999-test-cx-readyfence2:final-fence-unit.log`.
- Forced-failure GPU witness: PASS, `ready=true`, `ok=false`, `serial=8`,
  `publishedSerial=8`. Transcript:
  `/home/liory/codeman-cases/undertow/reports/cx-readyfence2-evidence/gpu-forced-failure-final.json`;
  queue log:
  `/home/liory/.local/state/undertow/undertow-box-logs/1794596881-gpu-cx-readyfence2:gpu-forced-failure-final.log`.
- Normal GPU witness: PASS, 10/10 serial-tied edits and readbacks. The ready-fence samples were
  serials 8, 10, 12, 14, 16, 18, 20, 22, 24, 26; every readback hash changed. Transcript:
  `/home/liory/codeman-cases/undertow/reports/cx-readyfence2-evidence/gpu-normal-10-cache-retry.json`;
  queue log:
  `/home/liory/.local/state/undertow/undertow-box-logs/1794596610-gpu-cx-readyfence2:gpu-normal-10-cache-retry.log`.

## RECOVERY / REDS

- The first post-restart build invocation used the repository parent instead of this assigned
  worktree and therefore reported an unrelated `unknown target 'test_recipe_preview_fence'`.
  Re-running the same queued command from this worktree passed; no source change was needed.
- One normal GPU attempt reached serial 18 with `publishedSerial=16` and timed out while warming a
  fresh Vulkan pipeline cache. The unchanged queued retry reused the warmed runtime cache and
  passed all 10 edits. This was recovered and is filed as a consolidation issue, not classified as
  a product regression.
- The earlier full 672-step Release retry also passed but emitted pre-existing VMA/deprecation
  warnings; the final changed-target Release build above emitted none. No new warning is present
  in the changed translation unit.
- CodeGraph was attempted, but this checkout has no usable index and its installed CLI rejected the
  requested `--query` form; source tracing and preserved GPU logs supplied the evidence.

## STOPs

- None. The requested implementation, unit sequence, normal 10/10 GPU witness, forced-failure GPU
  witness, and final Release editor build are complete.

## LANDABLE NOW

- VIXEN: the implementation and test in this report, with the targeted build, unit, and GPU
  witnesses above.

## SPT DISPOSITION

- No SPT task id was dispatched for this lane.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Queued VIXEN editor build can terminate without a diagnostic
- proposed: Recipe preview GPU witness needs an automatic pipeline-cache prewarm
