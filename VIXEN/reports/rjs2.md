# rjs2 — render-job S2 (tracked kernel pin dc6c87b7)

## LANDABLE NOW

- Branch: lane-rjs2. The authoring-convergence wave was merged at 213d54f2 and updated again at 49dd9502. The generated-binding graph aliases are committed in f0306a82. The tracked YEROKET_KERNEL_SHA remains dc6c87b7262c6791c745f37e583ca12f2f8706fe; no per-build override remains.
- Tone-map outputs were regenerated from that tracked pin. The generated shader and binding header content did not change. WSL and native Release all-check builds passed, including every default *_check target; the WSL shader golden check passed. glslangValidator is absent on WSL, so native Vulkan build is the shader compile witness.
- Those full builds completed before merge 49dd9502. That merge only added case-tolerant glslang include lookup in VIXEN/CMakeLists.txt and removed the runtime-written cache manifest. Per instruction, the full builds were not repeated.
- The 16-pair native parity sheet was recaptured at dc6c87b7. Every hand-written reference/generated-system pair is byte-identical at 500×500: zero changed pixels and zero channel delta. The sheet now records the tracked kernel SHA, PNG SHA-256 values, and each queued capture log in VIXEN/reports/rjs2-parity.csv.
- The midday look-dev A/B passed for all four angles: byte-identical PNGs and zero changed pixels at 500×500. I opened all four reference and all four generated images. The output images are under /mnt/c/vixen-native/rjs2/build/native/runtime-captures/lookdev-ab-dc6-midday/.
- The focused editor capture producer passed. The full GPU split also completed all 172 unique selected tests, with 155 passes, 17 expected skips, and no unresolved failures.

### Full GPU split and capperf1 comparison

The six disjoint shards were derived from the runner-default GPU selection (native and nightly excluded) and submitted as separate queued GPU jobs. The merged-wave capperf1 inventory selected 140 GPU tests and had no failing names. The current inventory selected 172 unique names. Per-label counts are not additive: two tests each have two correctness labels, so the row totals are 142/174 against 140/172 unique selections.

| Correctness label | capperf1 GPU | rjs2 GPU |
| --- | ---: | ---: |
| AppFlow | 0 | 0 |
| Application | 25 | 57 |
| CashSystem | 0 | 0 |
| GaiaArchetypes | 0 | 0 |
| GaiaVoxelWorld | 0 | 0 |
| RenderGraph | 98 | 98 |
| ResourceManagement | 1 | 1 |
| SVO | 15 | 15 |
| ShaderManagement | 0 | 0 |
| VIXENCapture | 1 | 1 |
| VoxelComponents | 0 | 0 |
| VoxelData | 0 | 0 |
| VoxelDocument | 0 | 0 |
| VulkanResources | 1 | 1 |
| WSL | 1 | 1 |
| logger | 0 | 0 |
| **Unique selected tests** | **140** | **172** |

Failure-name comparison: capperf1 had no GPU failures; the completed rjs2 aggregation has no failing names. One transient observed name was vixen_hud_capture_producer, which timed out at 180.36 seconds in shard 3 while shared GPU shards were active. It passed in other shards and in the isolated retry; the two fixture-dependent tests also passed on retry. The same producer took 148.84 seconds in shard 4, 69.87 seconds in shard 5, and 32.37 seconds on retry, consistent with a load-sensitive timeout rather than a repeatable render failure.

Queue jobs: shards 1–6 are logs 1791947742, 1791947749, 1791947787, 1791948014, 1791948756, and 1791948607; shard 3 retry is 1791949140. All are under /home/liory/.local/state/undertow/undertow-box-logs/. JUnit aggregation is in build/wsl/rjs2-gpu-dc6-final/results-1.xml through results-6.xml and results-3-retry.xml.

### Native parity, look-dev, and editor producer

- Native parity compared VIXEN/shaders/Reference/ExposureTonemapReference.comp against VIXEN/shaders/Generated/ExposureTonemapSystem.comp on AMD Radeon(TM) 8060S Graphics, driver 32.0.22018.5. The source shader SHA-256 values are 8345143f968b09242866d2e323d458b5e9c7eaa5485e4224f87f0d2d266dc94c (reference) and 3381425f9a99aac6012e28abbed8a0e278077ae9e6090e1833deadaf17e45ee5 (generated).
- Each reference/system state-angle capture was its own queued GPU job. Per-row queue IDs and PNG hashes are recorded in rjs2-parity.csv. The capture logs are under /home/liory/.local/state/undertow/undertow-box-logs/ with names beginning dc6-parity-.
- Midday look-dev reference and system jobs passed separately (logs 1791949667 and 1791949685). The pixel comparator reported byte_equal=yes, pixel_diff=0, max_channel_delta=0 for all four angles. All eight images were opened.
- Focused vixen_editor_capture_producer passed (queue log 1791949623). The split’s vixen_hud_capture_producer retry passed in queue log 1791949140.

### Build and generation evidence

- WSL configure: log 1791935449. Tracked-pin tone-map regeneration: log 1791935468. WSL all-target build: log 1791935549, exit 0.
- Native configure: log 1791936106. Native all-target build: log 1791936620, exit 0. The first native all-check attempt lacked tbb12.dll on the discovery PATH; the queued retry added the native TBB runtime path and passed.
- Full build logs are in /home/liory/.local/state/undertow/undertow-box-logs/.

## STOPs

- No blocker remains for the assigned witnesses.
- Native timing was intentionally not run; the controller will run the quiet exclusive --gpu-benchmark window. No timing number is claimed, and the under-0.5-ms gate remains with that controller run.

## CONSOLIDATION ISSUES

New proposals recorded in .spt-proposals/rjs2.jsonl:
- Derive SSBO block provider aliases from generated binding names
- Partition GPU split tests into parallel queued shards
- Resolve the Windows command path for queued WSL jobs
- Budget GPU fixture timeouts for concurrent shared jobs
- Make shared GPU reservation cleanup race-safe

During the parallel parity captures, the queue briefly reported a missing temporary memory-reservation file; both jobs passed without retry. Earlier rjs2 proposals remain in the same inbox file.
