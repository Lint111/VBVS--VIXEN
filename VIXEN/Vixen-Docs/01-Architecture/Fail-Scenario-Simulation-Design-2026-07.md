---
tags: [architecture, fail-scenarios, fault-injection, validation, error-model, design]
created: 2026-07-02
status: design approved 2026-07-02 (user). Not yet implemented — next step is the Inc 1 implementation plan.
related: ["[[Error-Model-Refactor-2026-06]]", "[[Device-Loss-Recovery-2026-06]]", "[[RenderGraph-System]]"]
---

# Fail-Scenario Simulation (Build-Time Graph Fault-Injection Gate) — Design

> **Goal:** every node declares its known fail conditions as *injectable scenarios*, colocated with the
> node code via macros and **compiled out of real build artifacts**. A validation build assembles the
> real application graph, runs it headlessly, injects every declared scenario, and asserts graceful
> handling — so a green gated build means "every known failure mode of this assembled graph was
> exercised and recovered". Motivated by the fullscreen-button crash (2026-07): window-stimulus bugs
> of exactly this class should be caught automatically at build time, not discovered live.

## 0. The key framing decision: declarations GENERATE scenarios, they are not statically checked

A static check ("does every node declare a handler for condition X?") would pass today's code and
still miss the fullscreen crash — `WindowNode` already handles Maximize/Resize and `SwapChainNode`
already has an OUT_OF_DATE path; the bug is in the *behavior* of that handling. Behavioral bugs are
only caught by executing the path (established project rule: live-run gates are authoritative for
GPU work; static review repeatedly passed runtime bugs in auto-sync P4). Therefore:

- Per-node declarations **enumerate** the scenario matrix for whatever graph is assembled.
- A **headless simulation** (real graph, lavapipe) **verifies** each scenario by injection.
- A purely static contract layer was considered and rejected as the primary mechanism (may bolt on
  later as a cheap assembly-time diagnostic; the declaration schema must not preclude it).

## 1. Decisions of record (user-approved 2026-07-02)

1. **Audience:** engine CTest/build gate first (Inc 1–2); later exposed as a host-callable
   validation pass so consumer apps (UNDERTOW) can sweep *their* assembled graphs (Inc 3).
2. **Failure classes, Inc 1 vocabulary:** `WindowStimulus` (resize / minimize-0×0 / maximize /
   fullscreen-extent-jump / restore / focus-loss) and `VkTransient` (DEVICE_LOST, OUT_OF_DATE /
   SUBOPTIMAL at acquire/present, submit failure, host/device OOM at allocation sites).
   Content/asset failures (shader-compile fail, corrupt recipe) deferred to Inc 3. The ~396
   mis-wired-graph invariant throws stay out of scope — they are programmer errors that correctly
   fail fast and are already covered by static graph/type validation at assembly.
3. **Declarations are macro-marked, per node, in the node's own .cpp**, self-registering into a
   scenario registry — assembled automatically into "the coherent array of scenarios the node
   presents outwards".
4. **Zero footprint in real builds:** everything (declarations, registry, harness, *and the
   injection seams*) sits behind a build flag and compiles to nothing when off.
5. **Gate semantics:** "build succeeded ⇒ scenarios passed" is provided by an opt-in gate target
   that *runs* the sweep and fails the build; compilation alone only proves declarations are
   well-formed. CI enables the gate; local builds run the same sweep on demand via ctest.

## 2. Existing foundations (this is a generalization, not a green-field build)

| Foundation | What it gives us |
|---|---|
| `VIXEN_SIMULATE_DEVICE_LOSS=<frame>` harness ([[Device-Loss-Recovery-2026-06]] Inc 2) | The proven prototype: synthetic fault → teardown/rebuild → continued rendering, zero validation errors. Its promotion to an automated test was explicitly deferred (Inc 3 there) — this design IS that promotion, generalized. |
| Error-model Phases 1–3 ([[Error-Model-Refactor-2026-06]]) | No exception escapes `Prepare`/`Update`/`Render`/`RenderFrame`; failures surface as statuses — the harness can observe failures without dying. |
| `WindowNode::pendingEvents` queue | The natural window-stimulus injection seam: synthetic events enter exactly where GLFW callbacks do, so the full downstream path (MessageBus → resize → swapchain recreate) executes for real. |
| Lavapipe live gates + validation-layer-if-installed pattern (`c3cbfdb6`) | Headless GPU execution + the "enable the layer only if installed" gating already used by render tests. |
| Static node/graph type system | Keeps wiring errors out of this system's scope. |
| CleanupReason lifecycle (`Recompile`/`DeviceLost`/`FinalTeardown`) | Recovery semantics scenarios assert against. |

## 3. Architecture

### 3.1 Declaration schema (macro + typed descriptors)

Declared at the bottom of the node's .cpp, colocated with the code they exercise:

```cpp
// SwapChainNode.cpp — after the implementation
VIXEN_FAIL_SCENARIOS(SwapChainNode,
    VIXEN_SCENARIO(AcquireOutOfDate,
        VkTransient{ .site = FaultSite::Acquire, .result = VK_ERROR_OUT_OF_DATE_KHR },
        [](ScenarioContext& c) { c.ExpectSwapchainRecreatedWithin(3 /*frames*/); }),
    VIXEN_SCENARIO(FullscreenExtentJump,
        WindowStimulus{ .events = { Ev::Maximize, Ev::Resize{2560, 1440} } },
        [](ScenarioContext& c) { c.ExpectSwapchainExtent(2560, 1440); })
);
```

- Descriptors are **typed structs** (`WindowStimulus`, `VkTransient`), not strings — a malformed
  declaration is a compile error, so declarations cannot rot silently.
- The third element is the **recovery contract**: a callable `void(ScenarioContext&)` asserting
  what "handled smoothly" means for this scenario *beyond* the global pass criteria (§3.5).
- A scenario may carry an optional **expected-terminal** marker for failures whose correct outcome
  is a graceful abort, not recovery (e.g. unrecoverable device loss): contract then asserts the
  documented terminal behavior (persistent error status from `RenderFrame`, no spin, no crash).
  This finally exercises the `deviceLostUnrecoverable_` path deferred by Device-Loss Inc 3.

### 3.2 ScenarioRegistry (self-registration, validation builds only)

In a validation build the macro expands to a static registrar object in the node's own TU that
inserts `{node-type-name → scenario array}` into a global `ScenarioRegistry` at load time — the
same self-registration idiom as node-type registration, and because the registrar lives in the
node's implementation TU it has exactly the linkage guarantees the node itself has (if the node is
linked into the app/harness, its scenarios are too). In a normal build the macro expands to
**nothing** (no registry, no lambdas, no included harness headers).

### 3.3 Injection seams (both behind the flag; both already exist in embryo)

1. **Window stimuli — `InjectWindowEvent()`**: pushes synthetic `WindowEvent`s into
   `WindowNode::pendingEvents` under its existing mutex. Downstream is 100% production code.
   Fullscreen headlessly = Maximize + extent-jump Resize (see limits, §7).
2. **Vulkan transients — `FaultInjector`**: generalizes the device-loss latch into a graph-owned,
   dormant-unless-armed service: *"at frame N, site S returns VkResult X once (or persistently)"*.
   Engine call sites register as named fault points: `Acquire`, `Present`, `Submit`, `FenceWait`,
   `Allocate`. Each site is one flag-compiled check (`VIXEN_FAULT_POINT(site, realCall)`).
   `VIXEN_SIMULATE_DEVICE_LOSS` migrates to become one armed instance of this service.

### 3.4 Scenario enumerator

Walks the **assembled** graph (the real application graph, not a fixture), collects declared
scenarios from the node types actually present, and emits the scenario matrix. Per-application
coverage is therefore *derived*, never hand-maintained: add a node to the graph and its scenarios
join the sweep; add a scenario to a node and every graph containing that node sweeps it.
Applicability predicates (optional, per scenario) can skip scenarios whose preconditions the graph
doesn't meet (e.g. no swapchain in a compute-only graph).

### 3.5 Headless runner + pass criteria

Per scenario: **boot the real graph on lavapipe → render W warm-up frames → inject → render N
observation frames → evaluate → teardown**. Default isolation is boot-per-scenario (state bleed
between scenarios is a false-green risk); batching into one boot is a later optimization if sweep
time demands it.

Global pass criteria (every scenario, mirroring the proven device-loss gate):
1. **No crash / nothing escapes the host boundary** — process stays alive; `RenderFrame` returns
   statuses only.
2. **Zero Vulkan validation errors** (layer enabled when installed — existing gating pattern).
3. **Frame progress** — ≥ N frames complete post-injection within a wall-clock watchdog. A hang
   IS a failure (the historical resize deadlock class); watchdog = runner-side timer + CTest
   `TIMEOUT` as backstop.
4. **Scenario contract** — the declaration's callable passes (`ExpectSwapchainExtent`, …).
5. **Expected-terminal scenarios** — graceful persistent error status instead of criterion 3.

`ScenarioContext` (harness-side, flag-only) provides: frame stepping, graph/node lookup,
swapchain/extent queries, validation-error and log-capture counters, and the `Expect*` helpers.

### 3.6 Build integration

- `VIXEN_FAIL_SCENARIOS` (CMake option, default **OFF**): compiles declarations + registry +
  seams + harness. The test configuration sets it ON, so the machinery is continuously exercised;
  every other configuration compiles it all out.
- `VIXEN_GATE_FAIL_SCENARIOS` (default **OFF**; CI **ON**): adds a post-build custom target that
  runs the sweep for each gated application graph and **fails the build** on any scenario failure.
  Locally the same sweep runs via `ctest` (one CTest case per scenario — parallelizable,
  filterable).
- The runner obtains each application's graph via its real assembly path (factored into a callable
  if it isn't already) — the gate validates *the graph the app actually ships*, not a replica.

## 4. Rejected approaches

1. **Static contract validation as the primary mechanism** — provably insufficient for the
   motivating bug class (handling exists; handling is wrong). Kept only as a possible future
   cheap diagnostic layered on the same declarations.
2. **Runtime-dormant instrumentation in shipping builds** (today's env-var latch pattern) —
   rejected by the zero-footprint requirement; everything moves behind the compile flag.
3. **Hand-maintained scenario list in the test tree** — rejected: drifts from the nodes, dies the
   usual checklist death. Colocated macros + derived matrix instead.

## 5. Increments

- **Inc 1 — Foundations + first real scenarios (the fullscreen class).**
  Flag + macros + `ScenarioRegistry`; `FaultInjector` service + `Acquire`/`Present`/`FenceWait`
  fault points; `InjectWindowEvent` seam; `ScenarioContext` + runner + gtest target sweeping the
  default VIXEN app graph; first declarations: `SwapChainNode` (AcquireOutOfDate, PresentOutOfDate),
  `WindowNode` (Minimize0x0, FullscreenExtentJump, RestoreAfterMinimize); migrate
  `VIXEN_SIMULATE_DEVICE_LOSS` → a `FrameSyncNode` DeviceLostRecovery scenario (fence-wait is the
  primary detection site per [[Device-Loss-Recovery-2026-06]]).
  **Acceptance:** FullscreenExtentJump reproduces the live fullscreen crash (red) or the divergence
  is documented; remaining scenarios green; compile-out proven empirically (a normal build's
  artifacts contain no scenario/harness symbols).
- **Inc 2 — Coverage + the gate.**
  Declarations across remaining relevant nodes (`Submit`/`FenceWait`/`Allocate` fault points —
  compute + graphics submit nodes, allocator OOM); expected-terminal scenarios (unrecoverable
  device loss); `VIXEN_GATE_FAIL_SCENARIOS` target + CI wiring; **empirical tamper self-test** — a
  deliberately-broken fixture node whose scenario must turn the gate red (per project rule:
  self-tamper must be empirical, not structural).
- **Inc 3 — Host-callable + content failures.**
  Public validation entry point (shape decided then — e.g. sweep-on-demand over a host-assembled
  graph) so UNDERTOW/mod graphs get the same guarantee; content/asset failure class
  (shader-compile fail, missing/corrupt recipe) enters the vocabulary.

## 6. Open questions (resolve in the Inc 1 plan, none block the design)

1. **Headless window/WSI mechanism** on lavapipe/WSL: `VK_EXT_headless_surface` vs hidden X window
   (note KI-001 xcb build gap) vs GLFW null platform — audit how existing windowed render tests do
   it and reuse. Fullscreen semantics headlessly are extent-jump only (§7.2).
2. Warm-up/observation frame counts + watchdog budget (device-loss gate history suggests small
   W, N ≈ tens of frames, seconds-scale timeout).
3. Whether app-graph assembly is already factored for reuse by the runner, or needs extraction.
4. MSVC + GCC self-registration parity in the harness link (whole-archive semantics if any node
   library is linked without direct symbol references).

## 7. Honest limits (stated up front, not discovered later)

1. **Validated bytes ≠ shipped bytes.** Compiled-out instrumentation means the gated binary is not
   byte-identical to the shipping one. Standard price of the assert/validation-layer pattern, and
   consistent with existing gates (Debug/lavapipe config). Mitigation: seams are single branches;
   everything else lives outside engine code paths.
2. **Class vs instance.** Headless fullscreen is an extent-jump simulation; if the live crash turns
   out to be real-WSI/driver-specific (e.g. exclusive-mode switch), the harness catches the *class*
   but may miss this *instance*. The live fullscreen bug is being debugged in parallel — the
   harness is not a substitute for that root-cause, it is the regression net for its class.
3. **Coverage = declarations.** An undeclared fail condition is unswept. Mitigations: scenario
   declarations become part of node-review convention; possible future lint (a node registering
   fault-point sites but declaring zero scenarios is suspicious).
4. **Compilation alone proves only well-formedness.** The "build ⇒ validated" guarantee requires
   the gate target actually running (§3.6); the spec deliberately words it as *green gated build*.

## 8. Acceptance (whole design)

- ✅-when-done: A normal build contains zero scenario/registry/seam/harness bytes (verified
  empirically, not by inspection).
- ✅-when-done: `ctest` (validation build) sweeps every declared scenario of the default app graph
  headlessly; all pass criteria enforced, hangs fail via watchdog.
- ✅-when-done: The fullscreen-crash class is represented by a scenario that reproduced (or
  documented why not) before the live bug's fix, and gates it after.
- ✅-when-done: `VIXEN_GATE_FAIL_SCENARIOS=ON` fails the build on a red scenario; the tamper
  fixture proves it can go red.
- ✅-when-done: Device-loss simulation runs as a first-class scenario (env-var harness retired into
  `FaultInjector`).
