---
title: Domain-Agnostic Multi-Channel Recipe Output — Milestone-Chunked Execution Plan
status: PLAN (full, 4 increments) — user greenlit "write full plan, start Increment 1" 2026-07-18. Runs under post-brainstorm-context-manager. Direction: [[Domain-Agnostic-MultiChannel-Recipe-Output-Direction-2026-07]].
created: 2026-07-18
program: domain-agnostic-multichannel-recipe-output
---

# Domain-Agnostic Multi-Channel Recipe Output — Execution Plan

Executes [[Domain-Agnostic-MultiChannel-Recipe-Output-Direction-2026-07]]. Migrate Unity's
multi-channel SDF-graph OUTPUT contract into the domain-agnostic kernel-framework core; make the
channel vocabulary derive from the content-config (one union list, per-entry role flags); add
subset-fused codegen (request `{subset}` → one method walking the shared DAG once). First consumer:
emission. Run under `post-brainstorm-context-manager` (thin controller, Sonnet-medium implementer +
Opus-high validator per milestone, this doc = memory).

## Ground rules (program-wide)

- **CROSS-REPO — this is the defining constraint.** Touches BOTH Yeroket (`$KF =
  /home/liory/Github/Yeroket-Fantasy/Packages/com.yeroket.utility.kernel-framework`, SDF graph at
  `com.utility.sdf`) AND VIXEN. Follow the `kernel-framework` skill §7 (codegen boundary) and §10
  (footguns) on EVERY milestone.
- **Isolation:** NEITHER repo is worked on `main` directly.
  - Yeroket: create a feature branch off `main` (currently `main` @ `c23fc557`, only the
    non-deterministic `SDFNodeGenerator.dll` dirty — `git checkout --` it, never commit it). Branch
    name suggestion `feat/kernel-multichannel-output`.
  - VIXEN: a dedicated worktree off `main` (the baked-perf worktree is a DIFFERENT program — do NOT
    reuse it). Branch `feat/vixen-multichannel-output`.
  - Pre-bless the VIXEN worktree tier; Yeroket branch commits are in-tree (that repo's own branch).
- **Codegen discipline (non-negotiable, skill §7/§10):** schema-side edits ONLY; NEVER hand-edit
  `.g.h`/`.glsl`/`.g.cs`. Regen is dotnet-only (`~/.dotnet/dotnet build -c Release` in
  `SourceGenerator~`, then `dotnet test Tests/SDFNodeGenerator.Tests.csproj` — verify NON-ZERO test
  count, the false-green trap §9). `SDFNodeGenerator.dll` is non-deterministic — commit ONLY on real
  source change, else `git checkout --`. Vendored `.g.*` stay byte-verbatim. Drift-guards (`X_check`)
  must pass; on Windows the WSL-bridge (KI-015) applies.
- **Build/test Windows-native for VIXEN render/gates** per `vixen-build-policy` (ONE build at a time).
  Kernel codegen tests are dotnet (WSL-side). Path-scoped `taskkill` for KI-041 hygiene (bare
  `/IM VIXEN.exe` clobbers sibling worktrees — 2026-07-18 incident; see Known-Issues KI-041).
- **Live-run gate authoritative** for VIXEN render; **parity harness authoritative** for codegen
  (VIXEN's ~91-opcode eval C#-vs-C++/HLSL/Burst). Validate outputs against an INDEPENDENT reference,
  never circular fused-vs-its-own-flatten (skill §9 / the Pyramid circular-oracle lesson).
- **Precedent to mirror:** the AppFlow migration lifted attrs into kernel-core
  (`$KF/Runtime/AppFlowAttributes.cs`, schema instance consumer-side) — Increment 1 is the SAME split
  for the output contract. Study it first.

## Milestone Map

| Inc | Name | Repos | Effort | Gate |
|---|---|---|---|---|
| 1 | Contract-migration → kernel-core | Yeroket (+VIXEN vendor) | M | contract domain-agnostic in core; Unity+VIXEN both reference it; byte-identical regen; drift-guards green |
| 2 | Channel-vocab unification (content-config) | VIXEN (+Yeroket if core enum) | M–L | union-list-with-roles schema; SemanticId mirror-enum; ChannelDesc = Stored projection byte-identical; recipe-output = Outputtable projection |
| 3 | Subset-fused codegen | Yeroket + VIXEN | L | request `{subset}` → ONE method, shared DAG walked once (CSE spans subset); fused == N-separate (parity, independent oracle) |
| 4 | Emission first consumer | VIXEN | M | a real body requests `{emission}`/`{roughness,brightness}` end-to-end; retire light-tree side-bake; render + parity vs virtual oracle |

Increment 1 is the lowest-risk pure move and unblocks the rest; 2 must precede 3 (fusion needs the
channel list); 4 validates. Full-plan written up front (user request); dispatch begins at Inc 1 and
proceeds milestone-to-milestone without per-increment check-in EXCEPT the design-carrying points noted.

---

## Increment 1 — Contract-migration to kernel-core (domain-agnostic)

Lift the output CONTRACT out of `com.utility.sdf` into domain-blind kernel-core, so Unity AND VIXEN
consume one source. AppFlow-style split: contract/attribute definitions kernel-owned; schema/graph
INSTANCES stay consumer-side.

- [x] Task 1.1 — Study the AppFlow migration precedent (`$KF/Runtime/AppFlowAttributes.cs` + how
  `AppFlowReference.cs` stays consumer-side + `[FlowNamespace]` schema-declared namespace). Confirm the
  exact split pattern before moving anything.
- [x] Task 1.2 — Move the domain-agnostic parts of `SDFOutputChannel` / `IMultiOutputNode` /
  `SDFVariantResult` / `SDFOutputSlot` / (the non-Unity parts of) `SDFOutputNode` into kernel-core
  (`$KF/Runtime/`), namespace `Yeroket.Util.KernelFramework` (or the codegen namespace family per
  skill §11). Keep Unity-serialization/editor-specific bits (`[SerializeField]`, `ISerializationCallbackReceiver`,
  migration) on the consumer side — same as AppFlow's instance/definition split. Distance-always-present
  + Normal-derived-from-gradient semantics preserved.
- [x] Task 1.3 — Rewire `com.utility.sdf` (Unity) to reference the moved contract; confirm Unity-side
  compiles/tests still pass (the SDF graph editor still enumerates channels via the moved types).
- [x] Task 1.4 — VIXEN vendors the contract: it currently has NONE of these types. Vendor via the codegen
  path (if the contract becomes codegen-emitted) or as a referenced core type per the consumption pattern
  (skill §7). At this increment VIXEN only needs the TYPES available (no fused emit yet).

**Gate:** the output contract lives in kernel-core, domain-agnostic; Unity references it and its editor
still works; VIXEN has the types; any codegen regen is byte-identical (drift-guard `_check` green);
`SDFNodeGenerator.dll` not spuriously committed; no `.g.*` hand-edited. Validator confirms tree integrity
across BOTH repos.

---

## Increment 2 — Channel-vocab unification (content-config-driven) ★ carries the design decision

Make the channel vocabulary ONE union list in the content-config, driving both voxel-storage and
recipe-output as projections. (Design decision #6 RESOLVED: union list with per-entry role flags.)

- [ ] Task 2.1 — Codegen-emit `SemanticId` as a mirror-enum (like `SdfOpCodes.g.h`) so C++/GLSL/the
  channel schema reference ONE list. Today it's a hand-written C++ enum (`VoxelChannelFormat.h:6`),
  invisible to codegen — bring it under the schema.
- [ ] Task 2.2 — Define the union channel-list schema in content-config: each entry
  `{ semantic, elemCount, fieldKind (nullable for derived), roleFlags: Stored|DerivedOutput|Outputtable }`.
  Lift `kChannelSpecs[]` (`ShellOctreeGpu.h:767`) into this schema — it already has ChannelDesc's shape.
- [ ] Task 2.3 — Make `ChannelDesc` / voxel `out.channels[]` population the **Stored projection** of the
  union list (`ShellOctreeGpu.h:778-870` reads the schema, not the hardcoded table). PROVE the generated
  `OctreeConfig.g.h`/`.glsl` + the serialized `channels[]` are byte-identical to today (or intentionally
  changed — drift-guard byte-compare is the check; a change here is a correctness event).
- [ ] Task 2.4 — Expose the **Outputtable projection** as the requestable set for recipe outputs (the
  vocabulary a `{subset}` request draws from). No fusion yet — just the list a request validates against.

**Gate:** one union channel-list schema drives both sides; `SemanticId` codegen-emitted; ChannelDesc
population is a Stored-projection and byte-identical (or the diff is explained + re-blessed intentionally);
Outputtable projection available; all VIXEN render gates + parity unaffected (no behavior change yet);
drift-guards green. This is the design-carrying milestone — validator scrutinizes the byte-identical claim.

---

## Increment 3 — Subset-fused codegen ★ the core mechanism

Request `{subset}` → ONE method walking the shared DAG once, CSE spanning the whole subset, returning
exactly the requested channels. Built by widening Unity's existing per-function CSE cache scope.

- [ ] Task 3.1 — Study `SDFHLSLCompiler.EmitNode` CSE cache (`SDFHLSLCompiler.cs:415-441`, `_nodeVars`
  `:1744`) + why each channel gets a FRESH `emitted` set (`:305`). The fusion = ONE `emitted`/`_nodeVars`
  scope spanning the requested channels in one emitted method.
- [ ] Task 3.2 — Implement the fused emit: given (DAG + requested channel mask), gather per-channel source
  nodes (`IMultiOutputNode.GetChannelSourceIndex`), walk the union sub-DAG ONCE with a shared CSE cache,
  emit one method populating a result for exactly the requested channels. Handle DerivedOutput deps
  (Normal needs Distance in-subset) within the single walk.
- [ ] Task 3.3 — Add the VIXEN backends (GLSL + CPU) — VIXEN has NO multi-output scaffold today. Per skill
  §3-B this is a 2nd/3rd multi-output backend → EXTRACT A SHARED BASE, do not copy the visitor a 3rd time.
- [ ] Task 3.4 — Parity: fused multi-output MUST equal N-separate single-output evals, proven against an
  INDEPENDENT reference (not fused-vs-its-own-flatten — circular-oracle trap, skill §9). Add to VIXEN's
  opcode harness. Verify the "one walk" claim empirically (a node feeding 2 channels emits/evaluates once).

**Gate:** a `{subset}` request emits one method, shared nodes computed once (proven, not assumed);
fused == N-separate on the parity harness against an independent oracle; VIXEN GLSL+CPU backends via a
shared base; no regression to existing single-output recipes (byte-identical); drift-guards green.

---

## Increment 4 — Emission first consumer (end-to-end)

Wire a real VIXEN body to request emission through the fused contract; retire the side-bake.

- [ ] Task 4.1 — Add emission as a channel in the union list (Stored+Outputtable). A recipe/body requests
  `{emission}` (or `{roughness, brightness}` to exercise multi-channel) via a config call; the fused method
  produces it.
- [ ] Task 4.2 — STORED path consumes the fused emission into the SEM_EMISSION bake (replaces the
  hand-lambda `EmitFn` at `BuildRenderGraph.cpp:4006`); PROCEDURAL path evaluates it live.
- [ ] Task 4.3 — Retire `BakeRecipeInstructionsToSdfWorldWithEmission` + the side-bake block
  (`BuildRenderGraph.cpp:4400-4464`) once native emission flows — BUT only if the procedural→GI path is
  satisfied (see direction §7: the light-tree currently needs a baked octree; if native procedural→GI is
  still out of scope, KEEP the side-bake for GI and note it — retiring it is gated on the separate
  procedural→GI-bridge work, NOT this program).
- [ ] Task 4.4 — Render + parity vs the VIRTUAL oracle: emission renders correctly for both providers;
  geometry parity byte-identical 0/625; feeds M11's Cornell lighting (the light panel emission).

**Gate:** a real body requests emission via the fused contract and renders correctly for both providers
vs the virtual oracle; multi-channel `{roughness,brightness}` request proven; geometry parity 0/625;
side-bake retired OR its retention explained (procedural→GI dependency); parity harness green.

**Program done:** contract in kernel-core (domain-agnostic); one content-config channel vocabulary driving
storage + output; subset-fused codegen (no per-property recompute); emission native to both providers.

## Progress Log

- Increment 1 (Tasks 1.1–1.4): DONE · Yeroket branch `feat/kernel-multichannel-output` commit `6fa9cb2c`
  (worktree `.worktrees/kernel-multichannel-output`) · Sonnet-medium impl · Opus validator APPROVED ·
  2026-07-18. 4 domain-agnostic contract types (`SDFOutputChannel`/`SDFVariantResult`/`IMultiOutputNode`/
  `SDFOutputSlot`+SDFOutputType) `git mv`'d to `$KF/Runtime/`, namespace `Utility.SDF.Graph`→
  `Yeroket.Util.KernelFramework`, .meta GUIDs verified IDENTICAL old→new (asset refs survive). Diffs =
  namespace line + lifted comment ONLY, zero logic edits (relocation not redesign). `SDFOutputNode.cs`
  correctly STAYS consumer-side (Unity-serialization-bound = the "schema instance", AppFlow-style split).
  NO UnityEngine leak in core (grep clean; only `System` + `Unity.Mathematics`). 33 consumer .cs files got
  the `using` (commit prose says "31" — harmless surplus, rewire exhaustively complete per validator grep) +
  2 asmdef refs added. dotnet tests reproduced NON-ZERO: SDFNodeGenerator 232/0, CodegenTool 124/0.
  `SDFNodeGenerator.dll` NOT committed (non-deterministic, checkout'd clean). No `.g.*` hand-edited.
  **DEFERRED (user-approved): Unity-editor verification** — agents can't run Unity MCP. Controller/user to
  check: `com.utility.sdf` compiles clean; SDF graph editor still shows Distance/Normal/Color/… output slots;
  no "missing script" on assets referencing SDFOutputSlot (GUIDs preserved, so expected clean). Touched-file
  list captured in the implementer report (11 Runtime/Editor + 2 asmdefs + ~18 test files). NEXT: Increment 2
  (channel-vocab unification) — but hold for the deferred Unity check + M11.2 validation first.
