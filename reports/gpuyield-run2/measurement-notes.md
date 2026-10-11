# Timing isolation notes (run 2)

The first two `native-unloaded` / `native-traced` files are historical filenames,
not claims of an idle adapter. The native process had no VIXEN child, but queued
VIXEN tests from other lanes can overlap a `gpu` reservation. They are contended
measurements. `native-contended.csv` preserves the first CSV before an isolated
repeat. `native-traced.csv` has no accompanying ETL: WPR reported 0xc5585011
and a subsequent stop reported 0xc5583000, despite process exit 0.

Native fence polling is a diagnostic control, not a proposed runtime fix. The
polling run still had >1 s host service tails, so it does not establish that
vkWaitForFences alone causes the delay. Native timestamp intervals stayed in
microseconds during those long waits. A GPU interval measures elapsed GPU-clock
time and can include preemption; neither it nor a host fence wait measures
exclusive active shader execution.

`native-diagnostics-recovery.log` is a later snapshot, not a synchronized trace of
the first probe. It reports vmwp engine usage and no explicit HwSchMode registry
value; absence of that registry value does not establish effective HAGS state.
Read-only /proc inspection also observed editormaterials' vixen_lookdev_capture
and togglelatency's VIXEN in queued scopes. Their worktrees were not read or edited.

Source inspection: SubmitRecord submits every tile without an intermediate
completion check. ComputeDispatchNode's TOP_OF_PIPE timestamp precedes image
and entry barriers, while the acquire wait uses COMPUTE_SHADER. Run 2 retains
that original whole-command interval and adds a separate COMPUTE_SHADER pair
immediately around each dispatch. The budget gate is not weakened.

The exclusive native control (global queue, all 15 pair tokens; no other queued
GPU fixture) completed 899 post-first samples in 15 seconds. GPU elapsed p99
0.00696 ms / max 0.00812 ms; submit-call p99 0.251 ms / max 0.4407 ms; fence
wait p99 0.3393 ms / max 1.0007 ms; total service p99 0.4852 ms / max 1.1612 ms.
Cadence p99 31.642 ms / max 31.7811 ms includes host wake timing. This control
establishes that concurrent fixtures can cause the native probe's second-long
completion tails; it does not alone classify the original VIXEN tile interval.
