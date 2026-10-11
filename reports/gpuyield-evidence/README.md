# R477 evidence

Source and executable provenance is in `provenance.json`. The kernel pin did not change.

- `baseline/`: fresh executables built from the original lane base before implementation, then saved under `.baseline` names. Seven windowed captures, nine offscreen fixture captures, the unchanged editor GPU oracle, HUD GPU profile, unloaded second-client probe and 500-frame concurrent-editor probe.
- `after/`: the completed candidate witness before the final main-wave merge. Retained so that its measurements are not silently replaced.
- `after-merged/`: merged-source full-suite fixtures (two cores) and completed four-core capture/profile/probe run.
- `after-merged-matched/`: repeated two-core windowed captures and paired cost; the concurrent editor timed out after 240 seconds and logged a budget violation. `failed-partial-summary.json` is explicitly a failed whole-peer trace, not a successful producer-overlap summary.
- `offscreen-recovery/`: original-base/candidate diagnostic replays. Both 500-frame producers exceeded their 120-second bound. The batch exit is a collection status, not producer success; see `status.txt`.
- `verification/`: build, test, identity and canonical-shape logs. Every build and GPU run used the global box queue with agent `gpuyield`.

`vixen-perf.csv` contains existing whole-dispatch timestamp columns. Their after values span the first tile through the last and include inter-submission gaps; they are not sums of tile GPU execution time. `durations.json` discards the first eight rows for allocation-ring warm-up. Percentiles use nearest rank.

`[GPUYIELD]` producer log rows report the sum of tile timestamp intervals and the maximum interval for each frame, including final flight-ring samples collected during cleanup. `measurements.json` summarizes these **per-frame maxima**, not every tile's individual distribution. The procedural test prints a separate `[GPUYIELD_TILES]` distribution over its 64 individual submissions and compares pixels, hit records and SDF traces against an untiled dispatch in the same process.

The independent probe measures a second Vulkan compute client's completion cadence. It performs real GPU work and waits for its fence. It does not present windows, measure DWM, or measure Windows input latency. `probe.csv` has Linux monotonic timestamps; `summary.json` selects only intervals overlapping the observed editor render loop and discards the first overlap interval. Render-marker polling has 50 ms resolution. The original baseline helper did not retain terminal process lifetime when the editor outlived the probe; the app's start/exit log timestamps supply that duration.

All standard capture PNGs are compared by encoded bytes and decoded pixels. Matching brick hashes are supplementary data evidence; they are not presented as serialized bake-file byte comparisons.
