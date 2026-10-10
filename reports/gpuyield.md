# gpuyield run 4 — native overhead, estimator blind spot, Dozen correctness

Run 4 started from `11bdfb96` (Codex, 2026-10-10 20:20 UTC) and was taken over by Claude Fable 5.1 on 2026-10-11 after the Codex usage limit. Owner correction applied mid-run (2026-10-11): **Dozen timing does not represent the project's real state; performance conclusions come from native Windows only; Dozen counts for correctness (no crashes, pixel parity, pass/fail).** [Run 3](gpuyield-run3.md) and [run 2](gpuyield-run2.md) are preserved.

## LANDABLE NOW

**Tip `ee089f22`** on `lane-gpuyield` (VIXEN). Runtime commits since the wave merge: `cd21b127` … `5a13200f` (Codex, run 3 and 4) plus `ee089f22` (takeover). Evidence commit follows this report.

| Check | Evidence |
|---|---|
| Linux build at tip | PASS (`takeover/floor-fix-build.log`); no-op rebuild afterwards: `ninja: no work to do` (`takeover/generation-checks-and-noop.log`) |
| 22 generation checks | PASS, all `*_check` targets at tip (same log) |
| Targeted cost-model/submit tests | 15/15 PASS at tip (`takeover/floor-fix-targeted-tests.log`), including the two new `GpuChunkCost` cases |
| Full RenderGraph/SVO suite (Dozen) | 2144 tests, 2 failures: accepted opcode-94 `RecipeSimdParity` and the R6 Dozen timing gate (STOP 1). ShaderManagement green. `vixen_multiinstance_cache_gate` PASS (327 s). AppFlow toggle/undo PASS. (`takeover/verification/post-fix-suite-status.json`, `post-fix-suite.log.gz`) |
| Byte-identical captures | 9/9 fresh PNGs (all nine post-start mtimes) byte-identical to the pre-bounding references (`takeover/verification/png-comparison.txt`, `capture-provenance.json`) |
| Dozen 500-frame producer | exit 0, 500 frames, 71.5 s wall, no device loss (`takeover/dozen-takeover/`) |
| Native build at tip | PASS on the 8060S from the NTFS checkout (`takeover/native-floor-fix-build.log`), binary `AC430FAA…` |
| Native full producer matrix | 8 runs × 500 frames, all exit 0, under `--resource gpu --exclusive` (`takeover/native-paired-floor-fix-queue.log`, `driver-matrix.json`) |
| Native hardware rays | `RayQueryFeasibilityTest` 4/4 PASS; hardware-query producer exit 0, primary 0.368 ms mean |
| R412 canonical-shape greps on added lines | zero hits |

Binaries used by the Linux suite are the tip binaries (`takeover/verification/binaries.sha256` equals the post-no-op hashes). Codex's earlier run-4 witnesses at `5a13200f` remain valid for that commit: the rebuilt `5a13200f` binaries hash-matched `cache-gate-binaries.json` exactly.

### What `ee089f22` changes (and why)

**Estimator blind spot, found natively.** At 1920×1080 on the 8060S the chunk-cost anchor was set once by a slightly larger full tile; every later full tile at the settled launch size was smaller than the anchor and was filed as *fixed latency*. When the heavy tile ran over budget (2.54–2.63 ms, 18 of 488 frames at `5a13200f`) the model never shrank the launch. `GpuChunkCost::Observe` now updates throughput from a full tile at any launch scale. This is the same defect Codex's flight-ring replay found on Dozen (19,487 ignored hot full tiles), confirmed on native hardware before being kept.

**Floor at the cold launch.** Applied alone, that learning collapsed Dozen's launch 81 → 441 → 1024 → 3,969 commands within 77 frames, D3D12 removed the device, `vkEndCommandBuffer` failed, and the device-loss recovery path crashed (exit 139; `takeover/dozen-revertedfix-crash/`). Dozen's per-command floor is 0.7–1.0 ms, so splitting cannot lower a chunk's maximum there. `GpuChunkCost::Edge` never returns an edge below the cold launch (7); the submission gate still reports any shortfall at the floor. Natively the floor never engages (the settled edges are 63 and ≥ 97), so it is harmless where performance is measured. Codex's uncommitted cold-edge-5-for-CPU-class change was dropped: it tuned to Dozen.

**Native effect of the fix (exclusive, 1080p, traced run):** two over-budget chunks remain, at frames 52 (2.57 ms) and 78 (2.35 ms), both before the model adapted; from frame 85 to 500 no chunk exceeded 2 ms, and the heavy chunk settled at 0.87 ms with the other three at 0.38/0.43/0.18 ms. At `5a13200f` the same scene kept the heavy chunk at 1.68 ms with periodic 2.5–2.6 ms excursions for the whole run.

### Native overhead, scale 1.0, 500 frames, 10 warmup frames excluded, exclusive host

Unbounded control is the detached wave-plus-portability binary `2e7dde20` (`40333D59…`); bounded is the tip (`AC430FAA…`). GPU-clock pass spans in ms. `cpu_frame_time` of the bounded runs is the 16.67 ms pace target by design (R477.2); the control is unpaced.

| Resolution / pass | Unbounded | Bounded (tip) | Change | Bounded at `5a13200f` |
|---|---:|---:|---:|---:|
| 500×500 / esvo_traverse_shade | 0.3260 | 0.3493 | +7.1% | 0.3739 |
| 500×500 / spatial_reuse | 0.0542 | 0.0663 | +22.4% (0.012 ms) | 0.0925 |
| 500×500 / shadow_visibility_wave | 0.0488 | 0.0537 | +10.0% (0.005 ms) | 0.0587 |
| 500×500 / render_target_blit | 0.0063 | 0.0102 | +0.004 ms | 0.0098 |
| 500×500 / ui_composite_render | 0.0332 | 0.0446 | +0.011 ms | 0.0478 |
| 500×500 / whole_frame_gpu_span | 0.7160 | 1.5571 | +0.84 ms | 1.4835 |
| 500×500 / cpu_frame_time | 3.668 | 17.080 (paced) | — | 17.070 |
| 1920×1080 / esvo_traverse_shade | 1.7067 (second sample 1.7123) | 2.0413 | +19.6% | 2.1050 (+10.9% vs its own control 1.8987) |
| 1920×1080 / spatial_reuse | 0.7329 | 0.7382 | +0.7% | 0.7608 |
| 1920×1080 / shadow_visibility_wave | 0.7526 | 0.7292 | −3.1% | 0.7873 |
| 1920×1080 / render_target_blit | 0.0343 | 0.0350 | +2.0% | 0.0337 |
| 1920×1080 / ui_composite_render | 0.0368 | 0.0384 | +4.1% | 0.0418 |
| 1920×1080 / whole_frame_gpu_span | 3.6433 | 4.1042 | +0.46 ms | 4.4255 |
| 1920×1080 / cpu_frame_time | 4.435 | 17.057 (paced) | — | 17.048 |

Run-to-run spread of the unbounded 1080p primary between the two exclusive matrices is 1.71 vs 1.90 ms (10%), so the 1080p overhead is +11% to +20% depending on the control sample; the bounded tip (2.04) is 3% under the bounded `5a13200f` (2.10). The no-trace runs reproduce the submission counts exactly (1.688 and 7.37 per frame) and the paced frame time (17.04 / 17.13 ms), so the trace logging does not change the measured behaviour.

**Where the overhead goes (1080p, tip):**
- Primary pass +0.33 ms: four chunks instead of one launch. Steady state is 4 submissions/frame (mean 7.5 including the 400-chunk cold frames); 3,784 chunk samples, p99 1.69 ms, max 2.57 ms (frame 52, pre-adaptation).
- Whole-frame GPU span +0.46 ms at 1080p and +0.84 ms at 500×500 while per-pass costs barely move: these are **idle gaps between submissions**, not execution. The one-outstanding-batch retirement cap makes every pass boundary a host round trip (host `chunk-retire` 1.85 ms/frame and `frame-retire` 1.07 ms/frame at 1080p; 0.02 and 0.76 at 500×500). At 500×500 the primary pass is a single submission, so the whole +0.84 ms is the cap.
- Timestamp queries: four per chunk; outer-minus-inner interval 1–2 µs per chunk (run 3 attribution, unchanged).

**Native chunk timing by workload (tip, exclusive):**

| Workload | Chunks | p99 / max ms | Submissions/frame (steady) | Gate (2 ms max) |
|---|---:|---:|---:|---|
| 500×500 primary | 852 | 0.359 / 0.367 | 1 | PASS |
| 1920×1080 primary | 3,784 | 1.686 / 2.569 | 4 | 2 cold-adaptation chunks over (frames 52, 78); PASS from frame 85 |
| 500×500 hardware query | 852 | — / — (exit 0, primary 0.368 ms mean) | 1 | PASS |

**Frame pacing (brief item 1):** the high-resolution Windows waitable timer is verified on native: CPU frame time 17.05–17.13 ms mean, p99 17.4–17.6 ms, max 18.3–19.0 ms, against the 16.67 ms target (was 31.4 ms with the coarse sleep at `9e9550f2`).

### Native cliff check (owner question)

No latency-driven subdivision cliff occurs natively. Native fixed latency is tiny (clipped 120×15 tiles run 0.007–0.011 ms), the model grows to one launch at 500×500 and four at 1080p, and the cold floor never engages. The cliff is a Dozen artifact of its 0.7–1.0 ms per-command floor: at 441 chunks of 9 workgroups a Dozen chunk still took 1.2–1.3 ms mean (`takeover/dozen-revertedfix-crash/`). The native defect was the opposite one (stale throughput after a larger anchor), fixed above.

### Dozen, correctness only

- 500-frame producer at tip: exit 0, 81 submissions/frame steady at the floor, no device loss. Timing (chunk max 3.9 ms) is reported for completeness only.
- Editor capture producer (R6 fixture): exit 0, 65 s; HUD producer passed; both crash-free with the floor. With the unfloored ratchet the HUD producer segfaulted (same collapse), so the floor is what keeps Dozen crash-free.
- The run-3 exit-139 crash is reproduced and understood: command-count collapse → `D3D12: Removing Device` → `vkEndCommandBuffer` failure → `VK_ERROR_DEVICE_LOST` → recovery (`VulkanGraphApplication::Render` "attempting recovery") → SIGSEGV. The tip no longer triggers the collapse; the segfault inside device-loss recovery is a separate pre-existing defect (lane gpudevloss territory), reported in CONSOLIDATION ISSUES, not fixed here (no backtrace available).

### Contended data, excluded

The native matrix Codex queued at 00:05 UTC kept running on the Windows side after its WSL wrapper was killed and finished at 00:12 under three builds and a Dozen job (500×500 frame time 285 ms, primary 45 ms). Those runs are archived in `takeover/contended-native/` (status and CSV only) and are not used anywhere above.

## STOPs

1. **R6 `GpuSubmissionsRespectDeclaredBudget` on Dozen.** The gate asserts a 2 ms maximum over every live sample of the editor producer, which the full suite runs on Dozen. Dozen's floor and jitter put 39 of 140 frames at 2.01–3.99 ms with the launch already at the floor; subdividing further removes the device. Under the owner's correction this red carries no performance information. Options: (a) carry the device class in the existing `[GPUYIELD]` log line (`device_type=`) and have the test assert the budget only for non-CPU-class devices, keeping sample presence, crash-freedom and the `[GPUYIELD_UNBOUNDED]` absence on Dozen; (b) make `gpuSubmissionBudgetNs` a per-device-class default in `TimelineCapacityTracker::Config` (the brief's "measured default per device class"), which still fails on Dozen at its measured 3.9 ms maximum; (c) record the R6 Dozen red as an accepted base red like opcode-94 and witness the budget natively (500×500 producer-scale native max is 0.367 ms). **Recommendation: (a), with (c) as the landing witness until (a) lands.** Not implemented: it changes a gate contract.
2. **Remaining overhead needs a design choice.** Measured cost of the bound on native: +7% primary at 500×500, +11–20% at 1080p, and +0.46–0.84 ms of idle GPU gaps per frame from the one-outstanding-batch retirement cap. Options with their measured basis: (i) raise the outstanding cap to two batches (restores pipelining across pass boundaries; worst queued work becomes 2 × 2 ms, still inside R494's 4.17 ms desktop reserve); (ii) grow the 1080p launch to two chunks once the heavy chunk is learned (the steady heavy chunk is 0.87 ms, so a 2-chunk split would still fit 2 ms with the current content, but the cold anchor logic would have to allow regrowth, which the ratchet forbids today); (iii) accept the measured cost. **Recommendation: (i) first, re-measured exclusively; (ii) only after (i).**
3. **Cold calibration overshoots natively.** The ratchet learns only after a violation: at 1080p the first 85 frames contain two chunks at 2.35–2.57 ms. The gate as written applies to every frame. Options: persist the last calibration per device/extent through the existing capacity config, or declare the first N frames calibration in the gate. Owner choice; nothing invented here.

## SPT DISPOSITION

R477 — PARTIAL: bounded submissions, pacing, byte identity, generation checks, suites and cache gate are green at the tip; the R6 Dozen timing gate (STOP 1) and the near-zero-overhead goal (STOP 2) are open. The briefs supplied no SPT task identifier to close.

No kernel or Undertow declaration changes, no `Program.Legacy.cs` change, no new attribute, registry or id space. The two runtime changes are inside the existing `GpuChunkCost` envelope and its tests.

## CONSOLIDATION ISSUES

Proposed by Codex in this run (appended to `.spt-proposals/gpuyield.jsonl`, unedited): resolve native launchers before the queue; keep native witness outputs outside source paths; derive cold calibration defaults from the device budget contract (**superseded**: the cold-edge-5 change it describes was not landed, see above); bind witnesses to immutable build artifacts during relinks; carry queue provenance into capture comparisons.

Findings from the takeover, not proposed through the SPT CLI (the SPT working directory was removed from this session's allowed paths):
- Windows-side witness process trees survive a WSL queue kill and finish without provenance; the queue should own the Windows process (job object) or the native wrapper should check the WSL parent.
- Device-loss recovery segfaults after `D3D12: Removing Device` on Dozen (`takeover/dozen-revertedfix-crash/`); needs a backtrace-capable repro in lane gpudevloss.
- The one-outstanding-batch retirement cap costs 0.46–0.84 ms of idle GPU per frame (STOP 2).
- `analyze-matrix.py` skips `native-unbounded-1080-return`; the second unbounded sample (1.712 ms) was read directly.
