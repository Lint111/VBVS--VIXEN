---
title: Node Self-Registration Portable Fix — Inc-1 Implementation Plan
aliases: [Intrusive Node Registrar Inc-1]
tags: [architecture, rendergraph, nodes, build, plan]
created: 2026-07-12
status: Planned — not started
related:
  - "[[Node-Self-Registration-Portable-Fix-Direction-2026-07]]"
  - "[[RenderGraph-System]]"
  - "[[Graph-Derived-Node-Linkage-Inc1-Plan-2026-07]]"
---

# Node Self-Registration Portable Fix — Inc-1 Plan

> **For agentic workers:** REQUIRED SUB-SKILL: use the post-brainstorm-context-manager
> pipeline to implement this plan milestone-by-milestone (fresh implementer + Opus validator
> per milestone, worktree-isolated, progress persisted in this doc). **Live-run gates are
> authoritative** — this plan touches core node-discovery machinery every app/editor/test
> depends on; do not accept a passing unit test alone as sufficient evidence for M3/M4 (see
> the design doc's §3.2 rationale — a unit test proves the mechanism, not the shipping app).
> Build Windows-native via the repo's `.bat` entry points through `cmd.exe /c` per
> `.claude/skills/vixen-build-policy/SKILL.md`; never overlap two builds of one target;
> actively poll the build lock/queue, never hand a wait off to ScheduleWakeup/Monitor.

**Goal:** Ship [[Node-Self-Registration-Portable-Fix-Direction-2026-07]] — replace
`VIXEN_REGISTER_NODE`'s anonymous-namespace-static + `std::vector<std::function>` pattern
with an intrusive self-registering linked list, fixing `NodeSelfRegistration.*`'s MSVC
COMDAT-GC failure at the mechanism level (portable across toolchains) rather than via a
linker-flag workaround.

**Explicitly NOT this increment:** anything under
[[Graph-Derived-Node-Linkage-Direction-2026-07]]'s scope (which node types a given app links —
already shipped, unaffected by this work per the design doc §4); changing the archive-level
whole-archive requirement itself (§9.3's `RenderGraphNodes` whole-archive stays, unmodified);
adding new node types or changing `NodeTypeRegistry`'s public API beyond what's needed to
preserve `RegisterAllNodes(NodeTypeRegistry&)`'s existing signature/contract.

---

## Milestone Map

- **M1 — Mechanism swap, GCC/WSL-verified first.** Replace the internals of
  `NodeRegistration.h`/`.cpp` with the intrusive-linked-list pattern (design doc §3.1).
  `VIXEN_REGISTER_NODE`'s macro call sites in all ~53 node `.cpp` files are **not edited** —
  only the macro definition and the two functions it expands into change. Gate: full existing
  test suite green on GCC/WSL (the toolchain where registration already worked, proving the
  refactor is behavior-preserving before it needs to also fix MSVC), `test_node_self_registration`
  count unchanged (still ≥32, same specific type-keyed `Has<>()` checks pass).
- **M2 — MSVC verification, the actual bug this plan exists to fix.** Same build, Windows-native.
  Gate: `NodeSelfRegistration.RegistersAllBuiltInNodes`/`ReplaysIntoEachRegistryInstance` both
  green (count ≥32, all four `Has<>()` checks pass) — the specific regression this plan closes.
  If still red, the intrusive-list mechanism itself has a gap (re-examine before assuming a
  second linker-flag layer is needed — that path was already tried and rejected, see design
  doc §1).
- **M3 — Live-app verification (not a unit-test-only close-out).** Per this plan's own
  live-run-gate requirement: build and run `VIXEN.exe` (and, if convenient, `vixen_editor`)
  Windows-native, confirm the app boots and the render graph builds/runs normally — the real
  evidence that node discovery still works end-to-end for the shipping app, not just the
  registry-count unit test. Gate: windowed boot + at least one render frame confirmed, no new
  Vulkan validation errors vs. a pre-change baseline run.
- **M4 — Documentation close-out.** Apply design doc §5's required doc updates:
  `RenderGraph-System.md` §9.3 debugging guidance corrected (name COMDAT/section GC as a
  distinct cause from a missing whole-archive flag, link to the direction doc); resolve/close
  any `Known-Issues.md` entry opened for this during the 2026-07-12 audit with a pointer here.

### Progress Log

(populated as milestones complete — one entry per milestone: commit hash, gate evidence, Opus
validator verdict; follows this vault's established plan-doc convention.)

---

## Tasks

### M1 — Mechanism swap

**Task 1 — Implement `NodeRegistrarLink`.** In `NodeRegistration.h`: add the intrusive-list
node type and the `HeadLink()` Meyers-singleton accessor (design doc §3.1). Keep
`NodeRegistrars()`/the old `std::vector` path removable only after M1's own gate passes —
consider leaving both compiled briefly during development for a differential check, removed
before commit (implementer's judgement call, record the choice in the Progress Log).

**Task 2 — Rewrite `VIXEN_REGISTER_NODE`.** New macro expansion per design doc §3.1: a
free function performing the actual `reg.Register<NodeTypeClass>()` call, plus a
`NodeRegistrarLink` static object whose constructor links it in. Preserve the
`__COUNTER__`-based uniqueness scheme (two names per macro invocation now, not one — both
must stay collision-free within one TU, which `__COUNTER__` already guarantees). Do **not**
require any change to the ~53 node `.cpp` files' own `VIXEN_REGISTER_NODE(...)` call sites —
verify this by grep after the header change compiles, confirming zero node source files
needed edits.

**Task 3 — Rewrite `RegisterAllNodes`.** Walk `HeadLink()`'s chain instead of iterating
`NodeRegistrars()`'s vector. Signature unchanged (`void RegisterAllNodes(NodeTypeRegistry&)`).

**Task 4 — GCC/WSL gate.** Full existing SVO/RenderGraph suite, WSL build, binaries run
directly per KI-014. `test_node_self_registration` both tests green, count and specific
`Has<>()` checks unchanged from pre-change baseline (capture the baseline count/checks before
starting, for an exact before/after diff — this is a refactor, the numbers should not move).

### M2 — MSVC verification

**Task 5 — Windows-native build + targeted gate.** Build via the worktree's own `build.bat`
(absolute path, per vixen-build-policy). Run `NodeSelfRegistration.RegistersAllBuiltInNodes`
and `.ReplaysIntoEachRegistryInstance` directly (KI-014). This is the actual regression the
whole plan exists to close — record the before/after count explicitly in the Progress Log
(before: 0 per this session's 2026-07-12 discovery; after: expected ≥32).

**Task 6 — Full MSVC sweep.** No regressions in the broader RenderGraph/SVO suite from the
mechanism swap — run whatever this branch's established broad-sweep script covers
(`VIXEN/temp/run_ctest_sweep*.bat` or equivalent at implementation time), confirm no new
failures beyond whatever was already triaged/pre-existing as of this plan's authoring.

### M3 — Live-app verification

**Task 7 — Windowed live-gate run.** `VIXEN.exe`, Windows-native, `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`
set. Confirm: app boots, default scene renders at least one frame, no new validation errors
vs. a pre-change baseline capture (same VUID-count-not-raw-grep discipline this project's
other plans already use). If `vixen_editor` is convenient to also run in the same session,
include it — not a hard requirement if it adds significant time, per implementer judgement,
but record which was actually run.

### M4 — Documentation close-out

**Task 8 — `RenderGraph-System.md` §9.3 correction.** Add a short addendum (not a rewrite) to
§9.3 naming COMDAT/section GC as a distinct, real cause of the same symptom (`GetNodeTypeCount()`
collapsing) separate from a missing whole-archive flag, with a link to
[[Node-Self-Registration-Portable-Fix-Direction-2026-07]] for the full story. Keep the
existing whole-archive guidance intact — it is still correct and still the first thing to
check for the archive-selection half of the problem; this only adds the second half.

**Task 9 — Known-Issues reconciliation.** If a KI entry exists for the `NodeSelfRegistration`
failures from the 2026-07-12 audit sweep, mark it RESOLVED with a pointer to this plan's
completion (commit hash) rather than duplicating the root-cause writeup — the direction doc
already carries the full explanation.

---

## Risks / decision points

- **`__COUNTER__` doubling.** The new macro expansion needs two uniquely-named symbols per
  invocation instead of one (the free function + the link-node static). Verify this doesn't
  collide with `__COUNTER__`'s per-TU monotonic guarantee in a way that's fragile — it
  shouldn't (each `__COUNTER__` use just advances once more), but confirm during Task 2's
  implementation, not assumed.
- **Constructor-order-dependent list traversal.** The intrusive list's insertion order across
  ~53 TUs is unspecified (link order dependent). `RegisterAllNodes`'s existing contract never
  promised a specific registration order (confirmed: `ReplaysIntoEachRegistryInstance` only
  checks count equality between two independent replays, not order) — but worth an explicit
  check during M1 that nothing downstream secretly depends on node-type registration order.
- **Escalation path.** Per this project's dispatch-escalation norm: if M1's GCC/WSL gate or
  M2's MSVC gate fails twice on Sonnet-medium, escalate to Opus-max before a third attempt;
  if that also fails, ask the user rather than attempting a fourth variant.
