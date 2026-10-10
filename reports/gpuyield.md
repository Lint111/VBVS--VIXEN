# gpuyield run 5 — R503: two outstanding batches, persisted calibration, native-only timing gate

Continues [run 4](gpuyield-run4.md) (`f4a18b08`, Claude Fable 5.1 takeover) under the owner's R503 rulings of 2026-10-11. All timing below is native Windows on the 8060S under `--resource gpu --exclusive` admission; Dozen appears only for correctness.

## LANDABLE NOW

**Tip `9a79ae99`** on `lane-gpuyield` (VIXEN), building on `ee089f22` unchanged:
- `630a016d` feat(rendergraph): two outstanding batches per queue, chunk calibration persisted per device and extent through the existing CalibrationStore, `device_class=` on every `[GPUYIELD]` line, R6 asserts the budget only on native devices.
- `9a79ae99` fix(rendergraph): the class is derived from the Vulkan driver id the capability graph already records (Dozen reports the adapter as an integrated GPU; it now reports `translated`).
- Evidence commit follows this report (`reports/gpuyield-run5/`).

| Check | Evidence at `9a79ae99` |
|---|---|
| Linux build; no-op rebuild; 22 generation checks | PASS; `ninja: no work to do`; all `*_check` targets (`run5/generation-checks-and-noop.log`) |
| Targeted tests | 41/41: `GpuChunkCost`, `SubmitRecord` (3 updated, 1 new cross-record cap test), `SimpleTaskProfileTest` (new chunk-calibration round trip), `MultiDispatchIntegration.ActualNode`, timestamp validation, recipe tiles, CalibrationStore (`run4/takeover/run5-targeted-tests-2.log`) |
| Full RenderGraph/SVO suite (Dozen) | 2147 tests, **1 failure: the accepted opcode-94 `RecipeSimdParity`**. R6 `GpuSubmissionsRespectDeclaredBudget` PASS on Dozen as a correctness check (`device_class=translated`, 140 live samples). ShaderManagement green. Cache gate PASS 267 s (`run5/verification/`) |
| Byte-identical captures | 9/9 fresh PNGs byte-identical to the pre-bounding references (`run5/verification/png-comparison.txt`) |
| Dozen 500-frame producer | exit 0, warm start restored from the Linux calibration file (`run5/dozen-run5/`) |
| Native MSVC build | PASS from the NTFS checkout at `9a79ae99`, binary `F489B784…` (`run5/native-build.log`) |
| Native matrix | 10 runs × 500 frames, all exit 0, exclusive, cold/warm pairs with the calibration file cleared before each cold run (`run5/native-matrix-queue.log`, `run5/native/`) |
| Native hardware rays | `RayQueryFeasibilityTest` 4/4; hardware-query producer exit 0, 1 submission/frame, max 0.457 ms |
| R412 greps on added lines | zero hits |

Linux binaries used by the suite: `e8f847a5…` / `072a748f…` (`run5/verification/binaries.sha256`), unchanged by the no-op rebuild.

### 1. Outstanding-batch cap raised to two (R503.1)

`SubmitRecord::Submit` retires the queue only when two batches are already outstanding; the drain owns a per-queue counter across records, so a pass boundary no longer costs a host round trip. The guarded direct path (channel inactive) still assumes a full queue and retires first. Worst queued work: two submission budgets, 4 ms.

Per-pass GPU-clock means, scale 1.0, 500 frames, first 10 excluded. Control is the detached unbounded binary `2e7dde20` (`40333D59…`). "Cold" starts with no calibration file; "warm" is the next run with the persisted envelope.

| 500×500 | Unbounded | Bounded cold | Bounded warm | Warm, no trace | Run 4 bounded (one-batch cap) |
|---|---:|---:|---:|---:|---:|
| esvo_traverse_shade | 0.3391 | 0.3871 | 0.3780 | 0.3852 | 0.3493 |
| whole_frame_gpu_span, mean | 0.7747 | 0.9134 | 1.0041 | 0.8775 | 1.5571 |
| whole_frame_gpu_span, p90 | 0.783 | 1.007 | 0.988 | 0.959 | — |
| spatial_reuse | 0.0866 | 0.0910 | 0.0988 | 0.1101 | 0.0663 |
| shadow_visibility_wave | 0.0561 | 0.0594 | 0.0624 | 0.0647 | 0.0537 |
| ui_composite_render | 0.0326 | 0.0381 | 0.0385 | 0.0385 | 0.0446 |
| cpu_frame_time mean / p99 | 3.41 / 4.24 | 17.05 / 17.35 | 17.07 / 17.33 | 17.05 / 17.37 | 17.08 |
| submissions/frame (steady) | 1 launch | 1 | 1 | 1 | 1 |

| 1920×1080 (viewport dispatch 136×135 workgroups) | Unbounded | Unbounded (return) | Bounded cold | Bounded warm | Warm, no trace | Run 4 bounded |
|---|---:|---:|---:|---:|---:|---:|
| esvo_traverse_shade | 1.8662 | 1.8690 | 2.0141 | 2.0118 | 1.9990 | 2.0413 |
| whole_frame_gpu_span, mean | 3.9052 | 3.9454 | 4.0529 | 4.1808 | 4.1507 | 4.1042 |
| whole_frame_gpu_span, p90 | 3.957 | — | 4.152 | 4.143 | 4.132 | — |
| spatial_reuse | 0.7595 | 0.7584 | 0.7459 | 0.7493 | 0.7554 | 0.7382 |
| shadow_visibility_wave | 0.8317 | 0.8305 | 0.8100 | 0.8210 | 0.8229 | 0.7292 |
| cpu_frame_time mean / p99 | 4.61 / 5.06 | 4.61 / 5.52 | 17.06 / 17.35 | 17.06 / 17.34 | 17.06 / 17.33 | 17.06 |
| submissions/frame (steady) | 1 launch | 1 | 4 | 4 | 4 | 4 |

Reading: the whole-frame span, the only honest total, is now +0.10 to +0.23 ms at 500×500 (was +0.84 ms with the one-batch cap) and +0.15 to +0.28 ms at 1080p (was +0.46 ms); at p90 the 1080p frame is 4.13–4.15 ms bounded against 3.96 unbounded (+4–5%). The primary pass reads +7–8% at 1080p (was +20%) and +11–14% at 500×500; with two batches in flight a pass's own timestamp interval now includes queue overlap with the previous pass, so per-pass spans overstate cost and the whole-frame span is the number to use. The host-side `chunk-retire` wait at 1080p fell from 1.85 to 1.36 ms/frame (warm). The whole-frame p99 (5.8–9.1 ms) comes entirely from frames 11–22, a startup tail present in every run including the cold one; from frame 23 no frame exceeds 5 ms.

**Frame pacing:** every bounded run paces at 17.05–17.07 ms mean, p99 17.33–17.37 ms, against the 16.67 ms target. Unbounded controls are unpaced (3.4–4.6 ms).

**Chunk timing (native, tip):**

| Run | Chunks | p99 / max ms | Frames over 2 ms |
|---|---:|---:|---|
| 500×500 cold | 852 | 0.396 / 0.407 | 0 |
| 500×500 warm | 500 | 0.389 / 0.416 | 0 |
| 1080p cold | 3,784 | 1.703 / 2.576 | 2 (frames 72, 287) |
| 1080p warm | 2,000 | 0.967 / 1.895 | **0** |
| 1080p warm, no trace | — | 0.970 / 1.703 (per-frame maxima) | **0** |
| hardware query 500×500 | 500 | 0.416 / 0.457 | 0 |

### 2. Calibration persisted per device and extent (R503.2)

Existing location used: the `CalibrationStore` (`{runtime-cache}/calibration/{gpu}.json`, per GPU with vendor/device id and driver-version check), which already persists every node's `SimpleTaskProfile`. `ComputeDispatchNode` publishes its learned `GpuChunkCost` envelope to its profile keyed by the dispatch extent in workgroups; the store saves it at shutdown and loads it on `DeviceMetadataEvent`. No new store, file or registry. A `[GPUYIELD_CALIBRATION]` line records each entry:

- 1080p cold: `extent=136x135 start=cold` → 400 → 49 → 9 → 4 chunks over 12 frames, two over-budget chunks later while the envelope settled.
- 1080p warm: `start=warm anchor_units=16384 unit_ns=208.87 fixed_ns=1319196` → 4 chunks from frame 1, no chunk over 2 ms in 500 frames (max 1.895 ms). The overshoot is gone.
- 500×500 cold: 81 → 9 → 1 over 8 frames; warm: 1 launch from frame 1.
- Persisted file after the matrix: `AMD_Radeon_TM__8060S_Graphics_4098_5510.json` with `chunkCalibration` for `136x135` and `63x63` (copied beside each run under `run5/native/*/`).
- Linux/Dozen: the editor producer and the 500-frame producer both logged `start=warm` from `runtime-cache/calibration/Microsoft_Direct3D12__AMD…json`.

Note on geometry: the 1080p editor viewport dispatch is 136×135 workgroups (the side panel takes the rest of 1920), so the four "chunks" at edge 128 are one 128×128 tile plus three slivers (8×128, 128×7, 8×7). Run 4's reading of the heavy first chunk as content concentration was this geometry.

### 3. R6 timing gate asserts only on native devices (R503.3, R394.1)

Every `[GPUYIELD]` line (ComputeDispatch, TraceRays, MultiDispatch, ShellRevalidate) carries `device_class=`: `translated` when the observed driver id is Mesa Dozen, Venus, llvmpipe or SwiftShader; otherwise `integrated`/`discrete`/`virtual`/`cpu`/`other` from the device type. The test asserts `max_submission_ms ≤ budget_ms` only for `integrated`/`discrete` and on other classes still requires live samples, `submissions ≥ 1` and no `[GPUYIELD_UNBOUNDED]` marker, printing `[GPUYIELD_GATE] device=non-native …`. First attempt used the device type alone and Dozen reported `integrated` (`run5/first-pass-integrated-class/`), hence the driver-id fix.

### Dozen, correctness only

500-frame producer exit 0; editor and HUD capture producers exit 0; full suite as above; PNG byte identity as above. No device loss in any Dozen run at the tip.

## STOPs

None blocking. Two findings for the owner's attention:
1. **Per-pass spans overstate cost under pipelining.** With two batches outstanding a pass's timestamp interval includes overlap with the previous submission, so the per-pass table is no longer a clean cost metric; the whole-frame span and the frame p90 are. If a per-pass oracle is wanted, the start timestamp should move from `TOP_OF_PIPE` to the first compute stage.
2. **Startup tail frames 11–22** reach 7–12 ms whole-frame span in every run (unbounded included: one 17 ms frame at 500×500). It is shader warm-up, not bounding; the gate excludes nothing today because chunk maxima stay under budget there.

## SPT DISPOSITION

R477 — PARTIAL, close to CLOSE: bounded submissions, pacing, byte identity, suites, cache gate, generation checks, native witnesses and the R503 follow-ups are green at the tip. Open: the accepted opcode-94 red (not this lane's) and the two findings above. The briefs supplied no SPT task identifier.

No kernel or Undertow changes, no `Program.Legacy.cs` change, no new attribute, registry, id space or store. `SubmitRecord::kOutstandingBatchCap` is the R503 value in the existing record; calibration rides on the existing `SimpleTaskProfile`/`CalibrationStore`; the device class is a log field read from the existing capability build fact.

## CONSOLIDATION ISSUES

- Native fast-forward of the witness checkout aborted on untracked `reports/gpuyield-run4/` raw outputs (same shape as Codex's earlier proposal); moved to `.tmp/run5-preserved-native-run4`. A native witness should write outside tracked report paths.
- `analyze-matrix.py` from run 4 is superseded by the run-5 inline analysis (`run5/native-analysis.txt`); it still skips the `-return` unbounded sample.
- SPT CLI remained outside this session's allowed paths; findings are listed here, not proposed.
