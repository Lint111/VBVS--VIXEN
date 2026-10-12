# Paired native measurement stop

Implementation under measurement: `66cec822` (lane/report tip before this record: `e6738c47`). No product source was changed during this measurement attempt.

## LANDABLE NOW

- The fixed-LOD 500×500 run manifest, GPU telemetry availability note, native A-before CSV/readback/stills, and the failing Linear B-candidate log are preserved under `paired-stop/`.
- The native empty-slot A-before control completed the filtered GTest successfully in 11.792 seconds and wrote four stills plus `perf.csv`.
- Adapter discovery reported AMD Radeon(TM) 8060S Graphics, driver `32.0.22018.5`. Windows performance counters exposed no GPU clock or power paths. `amd-smi.exe` was present, but its earlier clock/power query timed out; no clock/power samples were recorded.
- No complete A/B/A round exists. No paired timing table, paired sheet, or gate result can be reported for 500×500 or 1080p.

| Extent and mode | Valid paired rounds | Gate status |
|---|---:|---|
| 500×500 fixed LOD | 0/3 | Not evaluated |
| 500×500 coupled LOD | 0/3 | Not run |
| 1080p fixed LOD | 0/3 | Not run |

## STOPs

The first clean fixed-LOD 500×500 A/B/A attempt failed at its first Linear candidate (`scale=1`, `upscaler=linear`). Native control A-before passed. The candidate failed during render-graph preparation, before the render loop:

```text
[VulkanGraphApplication::Prepare] Prepare failed: Unbound sub-graph input port 1 in render_path/upscaler
[Run] Prepare failed: Prepare failed: Unbound sub-graph input port 1 in render_path/upscaler - aborting before render loop
LookdevCapture.RendersFourStatesAtFourAnglesDeterministically: FAILED (321 ms)
application exited before all look-dev captures were written
```

The failed candidate has no rendered stills and only a header-only performance CSV. Its stdout is preserved at `paired-stop/500x500/round-1/linear-scale1/B-candidate/stdout.log`. The native A-before files are controls only and are not used as paired results.

The measurement sequence stopped because this is a render-graph failure at the implementation tip, and this continuation was explicitly limited to measurement with no further source changes. The remaining work is to resolve the selected upscaler slot’s unbound input, then rerun all three fixed-LOD 500×500 rounds, the three separate coupled-LOD 500×500 rounds, and the three fixed-LOD 1080p rounds. Only then can adjacent-control p50/p95 spread, the gate, and paired sheets be evaluated.

Invocation findings: the first queued command lost Windows path separators in a nested `bash -lc`; direct PowerShell argument passing corrected that. The committed runner then saw a blank `Start-Process` exit code despite a passing GTest control. A temporary runner copy refreshed the process and required the exact single-test GTest pass marker when the exit code remained unavailable. The copy is not part of the committed source.
