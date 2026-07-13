# Undertow Codegen Unification — Program Plan (2026-07-12)

**Parent research:** `Vixen-Docs/03-Research/Undertow-Codegen-Full-Survey-2026-07.md` (17-feature inventory,
research only, no decisions). **Sibling/prerequisite:** `View-Contract-Inc5-Undertow-Migration-Plan-2026-07.md`
(the view-contract slice, Milestones 1-3 DONE/Opus-validated, Milestone 4 BLOCKED on a real
writer/reader wire-protocol coupling — see that doc's own Progress Log for the full reasoning; this
program treats that blocker as a durable, load-bearing lesson, not an isolated one-off).

## Goal (user's own words, 2026-07-12)

"Refactor elements out of undertow one by one incrementally, until all codegen responsibilities are in the
kernel framework, cohesive and in their minimum amount of decomposition. If a feature can be achieved by a
pre-existing logical path [in the kernel framework], they should be merged, so that in the end we have a
minimal set of codegen functionality that covers all existing codegen needs and capabilities."

Restated: this is NOT "port 17 features 1:1 into Yeroket." It is a **consolidation** program — every
feature gets evaluated against the ALREADY-SHIPPED Yeroket mechanism family first; a feature merges into
an existing mechanism if one already covers its need, and only gets NEW Yeroket mechanism work if no
existing path can express it. The end state is the smallest coherent set of Yeroket codegen capabilities
that covers everything undertow's 17 features do today — not 17 parallel new emitters.

## Ground truth (from the survey, do not re-derive — re-verify file:line if code has moved)

- 17 real, live, load-bearing generators in ONE Roslyn analyzer assembly (`Undertow.Authoring.Codegen`),
  two input models: 13 schema-JSON-driven (`schemas.json`) + 4 attribute-driven "kernel" generators
  (`[Action]`/`[Effect]`/`[Param]`/`[System]`, undertow's OWN pre-existing kernel-ish pattern, predates
  any Yeroket dependency).
- Existing Yeroket mechanism family this program consolidates AGAINST (not reinvents): `[GpuStruct]`
  (plain-struct field-driven emission), `[View]`/`[ViewSection]` (+ this session's `[Projected]`/
  `[Overridden]`, from View-Model-Binding Inc-Ovr), `[KernelCallable]` (logic transplant via
  `--callable-cpp`), `[Flow*]` (state-machine codegen). Every "Yeroket analog" column in the survey names
  which of these is the closest fit, or states "none exists."
- **The view-contract slice's 4 discovered gotchas are the calibration data for this whole program's risk
  model** (see survey §"Cross-reference"): (1) no typed-C++-accessor emitter exists, only a generic
  reflection-blob model; (2) an attribute's effect can be scoped to ONE consumer face and silently not
  reach another (`[Projected]` reaches RmlUi, not the C# wire writer); (3) a custom binary wire protocol is
  NOT automatically compatible with Yeroket's own wire conventions, even when both are "SoA" at a vague
  conceptual level; (4) the `[View]` schema model's field-shape vocabulary (scalar/struct-array-of-scalars)
  has real gaps (no bare struct-scalar field, no variable-length struct-list column). **Any increment in
  this program should actively check for its OWN version of these four before assuming a clean 1:1
  migration is possible** — this is not paranoia, it's a demonstrated, repeated failure mode in the one
  slice already attempted.
- **The Milestone-4-class blocker (writer/reader wire-coupling) generalizes**: for ANY undertow feature
  with its own binary wire format (features #4, #16 in the survey), retiring the WRITER alone is unsafe if
  a hand-rolled READER on the other side assumes the old wire's exact byte layout and the two are wired as
  one all-or-nothing build unit. Before starting any wire-format feature's migration, explicitly map: (a)
  who writes this wire, (b) who reads it, (c) are writer and reader swappable independently in the build
  graph, or coupled. If coupled (as view-contract's `Undertow.View.csproj`'s single `Analyzer`
  `ProjectReference` was), a reader-side migration (or a full protocol-level equivalence, not just
  writer-output equivalence) must be scoped as PART of the same increment, not deferred.

## Scope boundary (what this program IS and IS NOT)

- **IS:** an ordered sequence of increments, each migrating (or explicitly merging into an existing
  mechanism) ONE feature (or one tightly-coupled cluster, e.g. #4+#5+#6) from the survey, in the priority
  order below. Each increment proves EQUIVALENCE (decoded-value or behavior-identical, per this program's
  own established discipline from Inc-Ovr/Inc-5) before retiring any hand-rolled undertow code — never a
  parallel, never-retired duplicate.
- **IS:** identifying, for each feature, whether it MERGES into an existing Yeroket mechanism (the
  preferred, "minimal decomposition" outcome) or genuinely needs new mechanism work — and if new work is
  needed, scoping THAT as its own explicit sub-decision (mirroring how Inc-5's Milestone 3 gap #4 was
  handled: named, deferred, not silently built ad hoc mid-migration).
- **IS NOT:** a single big-bang migration. Each feature is its OWN increment/worktree/Opus-validated
  milestone cycle, following this session's established `post-brainstorm-context-manager` pipeline
  discipline. Increments run sequentially (not in parallel) given the real cross-feature coupling the
  survey identified (#4↔#5↔#6; #8's registry slots are a foundational dependency for #1-#7).
- **IS NOT:** a mandate to force every feature into Yeroket regardless of cost. If an increment's own
  research finds a genuine architectural gap (per the calibration data above), the correct move — proven
  repeatedly in Inc-Ovr and Inc-5 — is to hold, report, and let the controller/user decide narrow-vs-defer,
  exactly as every prior increment in this session has done. This program's SUCCESS METRIC is "minimal
  covering set of Yeroket mechanisms," not "100% of undertow's codegen forcibly ported."

## Increment order (derived from the survey's own priority signal + dependency structure)

1. **#8 Registry collection slots** — FIRST despite being infrastructure, not because it's user-visible:
   everything else (#1-#7) depends on it. Low mechanical complexity, high blast radius — do this first
   and correctly rather than have 6 later increments each independently discover the same dependency.
2. **#9 Shared map-element structs** — small, self-contained, closest fit (`[GpuStruct]`-style plain
   struct emission already exists). Good second increment: low-risk warm-up validating the "merge into
   existing mechanism" pattern works end-to-end before tackling anything load-bearing.
3. **#14 `[Param]` declarations** — smallest attribute surface (8 sites), simplest shape (data
   declaration, not logic transplant). Third warm-up, proves the attribute-driven (not schema-JSON-driven)
   input model can also merge cleanly.
4. **#12 `[Action]` registration** — closest fit to `[KernelCallable]`'s existing logic-transplant
   pattern (5 sites). First REAL logic-transplant merge — validates whether `[KernelCallable]` genuinely
   covers this need or needs extension (e.g. for the auto-wired capability-gate side effect).
5. **#1/#2 Authored + Baked def carriers, #3 Authored-kind parse/bake table, #7 Sim-side registration** —
   cluster these together (they're the core authoring→content→sim pipeline, tightly coupled via #8's
   slots already migrated in step 1). Evaluate whether the two-tier Authored/Baked split can merge into
   ONE Yeroket mechanism or genuinely needs two.
6. **#10 Test data factories, #11 Schema-driven authoring builders** — low-priority, low-risk, can slot in
   whenever convenient (no hard dependency on anything else); do them once the core pipeline (step 5) is
   stable, as low-stakes practice increments.
7. **#13 `[Effect]` registration** — the ctor-shape dual-path (`HasSimCtor`) is a real wrinkle; do this
   AFTER #12 (`[Action]`) so any `[KernelCallable]` extension #12 needed is already proven before #13
   stresses it further with a second, more complex case.
8. **#15 `[System]` schedule solver** — HARD. A genuinely novel compile-time topological-sort algorithm
   with no Yeroket analog (`[Flow*]` does state machines, not general Before/After scheduling). This
   increment's Milestone 1 (research) should explicitly answer: can `[Flow*]` be EXTENDED to cover
   declarative scheduling, or does this need a wholly new Yeroket capability? Budget real design time,
   mirroring Inc-Ovr's Override mechanism (a real new capability, not a mechanical port).
9. **#4 Content-pack binary codec + #5 Merge + #6 Patch-parser** (one coupled cluster) — HARDEST. Do this
   LAST, after every other feature has proven the "merge into Yeroket" pattern repeatedly on simpler cases.
   **Before starting, apply this program's own writer/reader-coupling check** (see Ground Truth above) —
   determine who reads `Codec.g.cs`'s output and whether reader+writer are independently swappable in
   undertow's build graph BEFORE assuming this is a mechanical schema-declaration exercise. Expect this
   increment to surface its own version of the view-contract wire-format gotcha; do not be surprised by it,
   scope for it.
10. **#16 `[Saved]` codec** — explicitly deferred to LAST, or dropped from this program's active scope
    entirely, pending confirmation from the project owner that it's stable ground (only a synthetic probe
    type exists today; the survey flags this as unproven, not load-bearing). Ask before investing real
    effort here.

**#17 `EnumExprHelper`** is a trivial shared utility, not an independent increment — fold its equivalent
(if any is needed) into whichever increment first needs enum-to-expression-string conversion.

## Per-increment process (do not restate per increment — this section applies to ALL of them)

Each numbered item above is its OWN increment, following this session's established discipline:
1. **Research/Task-1 milestone**: read the real undertow source fresh (files may have moved since the
   survey), confirm the survey's characterization still holds, and explicitly answer "does an EXISTING
   Yeroket mechanism cover this, or is new mechanism work needed?" — report before building, Opus-validate
   the decision before the next milestone starts.
2. **Build milestone(s)**: implement the merge (reuse existing mechanism) or the new mechanism (if
   genuinely needed, scoped as its own explicit decision, not improvised).
3. **Equivalence proof milestone**: decoded-value or behavior-identical proof against the CURRENT
   hand-rolled undertow output for real data (not synthetic/vacuous) — this program's non-negotiable gate,
   per every prior increment's own precedent.
4. **Retirement milestone**: delete the hand-rolled undertow generator/emitter ONLY after the equivalence
   proof holds — never leave a proven-equivalent duplicate running alongside the old one.
5. Each increment gets its own worktree (VIXEN + Yeroket + undertow as needed, following Inc-5's
   established 3-repo worktree-isolation pattern), its own Opus validator per milestone, and updates THIS
   doc's Progress Log with its outcome before the next increment starts.

**A real architectural gap discovered mid-increment is NOT a failure** — every increment in this session
(Inc-Ovr's Override mechanism, Inc-5's four gaps) has hit at least one, and the correct response —
demonstrated repeatedly — is to hold, verify independently, and let the controller/user decide narrow vs.
defer vs. new-mechanism, never to force a match or silently drop data.

## Gates / guardrails (program-wide, apply to every increment)

- Every equivalence proof must be non-vacuous (distinct values, real edge cases exercised — empty
  strings, nullable/sentinel branches, etc.) — per this session's own repeatedly-applied discipline.
- Never assume a "SoA" or "wire format" label means byte-compatible with anything else carrying the same
  label — Inc-5's gotcha #3 proved two conceptually-similar formats can be structurally incompatible.
- Never assume an attribute's effect reaches every consumer that might plausibly want it — Inc-5's gotcha
  #2 proved `[Projected]` reaches only ONE of two plausible consumers.
- For any wire-format feature (#4/#16 in particular): map writer/reader coupling BEFORE assuming a
  writer-only swap is safe — Inc-5's Milestone 4 blocker is the concrete proof this assumption fails.
- rtk masks git exit codes — use `/usr/bin/git`, `sha256sum`, `cmp` for evidence in all repos touched.
- This is real, live, load-bearing game code (undertow's actual content/save/sim pipeline) — the same
  "real data on the line" discipline Inc-5's Milestone 4 applied (hold rather than risk corruption) applies
  to every increment here, especially #4 (save/mod-pack format) and #15 (sim scheduling correctness).
- Commit in worktrees (pre-blessed per convention). Do NOT push without explicit confirmation, especially
  for undertow-touching increments (#4 onward) where a partial/incomplete push could leave undertow's
  build broken.

## Milestone Map (program-level — one row per increment; each increment has its OWN internal milestones,
tracked in that increment's own plan doc once started)

- [x] Increment 1 — #8 Registry collection slots (Opus-validated APPROVED both milestones; new
  `[RegistrySlots]` mechanism proven equivalent, `EmitRegistrySlots.cs` not yet retired by design — see
  `Undertow-Codegen-Unif-Inc1-Registry-Slots-Plan-2026-07.md`)
- [x] Increment 2 — #9 Shared map-element structs (Opus-validated APPROVED both milestones; new
  `[SharedMapElements]` assembly-attribute mechanism, FULL RETIREMENT — first end-to-end retirement of
  live undertow code in this program, 2955/2955 undertow tests pass — see
  `Undertow-Codegen-Unif-Inc2-Shared-Map-Elements-Plan-2026-07.md`)
- [x] Increment 3 — #14 `[Param]` declarations (Opus-validated APPROVED both milestones; new
  `--param-cs` mechanism DISCOVERS undertow's real existing `ParamAttribute` rather than replacing it
  — a different architectural pattern than Inc-1/2, correctly chosen due to a live `CodeModLoader.cs`
  reflection consumer; full retirement, byte-for-byte-diffed equivalence proof in both directions,
  2953/2953 undertow tests pass — see `Undertow-Codegen-Unif-Inc3-Param-Declarations-Plan-2026-07.md`)
- [x] Increment 4 — #12 `[Action]` registration (Opus-validated APPROVED both milestones; discovered
  undertow's real `ActionAttribute` rather than replacing it, same pattern as Inc-3, due to
  `CodeModLoader.cs`'s live reflection use; full retirement, byte-identical equivalence proof
  independently re-derived by the validator, 2951/2951 undertow tests pass — see
  `Undertow-Codegen-Unif-Inc4-Action-Registration-Plan-2026-07.md`)
- [x] Increment 5 — #1/#2/#3/#7 Authored+Baked def carriers, parse/bake table, sim registration (cluster)
  (Opus-validated APPROVED all 3 milestones; largest/riskiest increment yet — 10 files retired incl.
  Increment 1's long-deferred `EmitRegistrySlots.cs`, 22 tests surgically trimmed with method-level
  precision, full equivalence loop closed, 2933/2933 undertow tests pass — see
  `Undertow-Codegen-Unif-Inc5-Def-Carriers-Bake-SimReg-Plan-2026-07.md`)
- [x] Increment 6 — #10 Test factories, #11 Authoring builders (Opus-validated APPROVED all 3
  milestones; #10 confirmed a real, load-bearing ctor-order coupling to Increment 5's def carriers
  (496 real test call sites as the safety net) and its equivalence proof included genuine positional
  field-value correctness testing, not just text-diffing; #11 caught a real enum-collapse bug during
  proving; 2949/2949 undertow tests pass — see
  `Undertow-Codegen-Unif-Inc6-Test-Factories-Authoring-Builders-Plan-2026-07.md`)
- [x] Increment 7 — #13 `[Effect]` registration (Opus-validated APPROVED both milestones; extended
  Increment 4's dispatch-table pattern with a dual ctor-shape discriminator — 15 parameterless +
  33 sim-ctor real sites, over 2x the survey's estimate; caught + fixed a real
  namespace-vs-directory bucket-discrimination bug during build; both real call sites
  + `CodeModLoader.cs` confirmed untouched, 2950/2950 undertow tests pass — see
  `Undertow-Codegen-Unif-Inc7-Effect-Registration-Plan-2026-07.md`)
- [x] Increment 8 — #15 `[System]` schedule solver (Opus-validated APPROVED both milestones; the
  program's own "novel algorithm" risk label was corrected — `SystemScheduleSolver` is textbook
  Kahn's topological sort, verbatim-ported; 37 real sites (not the estimated 34); byte-identical
  equivalence + all 5 diagnostics + a synthetic `Before`-only test (real data was 100% `After`);
  `CodeModLoader.cs`'s Order/Before-After mod-load asymmetry confirmed unchanged, not silently
  fixed; 2942/2942 undertow tests pass — see
  `Undertow-Codegen-Unif-Inc8-System-Scheduler-Plan-2026-07.md`)
- [x] Increment 9 — #4/#5/#6 Content-pack codec + Merge + Patch-parser (cluster) (Opus-validated
  APPROVED all 3 sub-milestones; the hardest increment in the program — byte-identical equivalence
  for all 3 generated files (Codec.g.cs/Patches.g.cs/PatchParser.g.cs) independently re-derived from
  scratch multiple times, an end-to-end parse-then-merge proof confirming #4/#5/#6 cohere as one
  system, full retirement of all 6 generator files + 33 obsolete Roslyn-internal test cases,
  2922/2922 undertow tests pass — see
  `Undertow-Codegen-Unif-Inc9-Content-Pack-Codec-Plan-2026-07.md`)
- [x] Increment 10 — #16 `[Saved]` codec (Opus-validated APPROVED Milestone 1; Milestone 2 build +
  equivalence proof + retirement — user confirmed proceeding 2026-07-13, correcting the survey's
  "unproven/not load-bearing" characterization; new standalone `--save-codec-cs` mechanism,
  FULL RETIREMENT, 2934/2934 + 21/21 undertow tests pass — see
  `Undertow-Codegen-Unif-Inc10-Saved-Codec-Plan-2026-07.md`. **THIS IS THE FINAL INCREMENT — the
  Undertow Codegen Unification program is COMPLETE, 10/10.**

## Progress Log

- Increment 1 (#8 Registry collection slots): COMPLETE · 2026-07-12 · both milestones Opus-validated
  APPROVED. New `[RegistrySlots(schemaPath, typePrefix, gate)]` class attribute + `RegistrySlotsEmitter`
  built (Yeroket `feat/codegen-unif-inc1-slots`, commit `696b432b`) — confirmed genuinely new mechanism
  needed (no existing Yeroket attribute reflects an EXTERNAL file's item list into an UNDECORATED host
  class; every other attribute reflects the decorated type's own fields). Faithful line-by-line port of
  `EmitRegistrySlots.All`'s logic, verified equivalent against a real 5-kind `schemas.json` subset
  spanning all 4 gate combinations (8 non-vacuous checks). `EmitRegistrySlots.cs` left running as-is —
  retirement deferred to a later dependent increment (per this doc's own per-increment process, Task 4)
  once #1/#2/#3/#7's own migration confirms end-to-end behavior with real def-carrier data. Full detail:
  `Undertow-Codegen-Unif-Inc1-Registry-Slots-Plan-2026-07.md`.
- Increment 2 (#9 Shared map-element structs): COMPLETE · 2026-07-13 · both milestones Opus-validated
  APPROVED. **First full end-to-end retirement in this program.** New assembly-level
  `[SharedMapElements(schemaPath)]` attribute + `SharedMapElementEmitter` built (Yeroket
  `feat/codegen-unif-inc2-mapelem`, commit `1af5865e`) — confirmed neither Increment 1's `[RegistrySlots]`
  (properties-into-existing-class) nor `[GpuStruct]` (reflects a decorated type, C++/GLSL-only emission,
  `ScalarKind` limited to U32/I32/F32) fit; a new mechanism was needed, reusing only Inc-1's "attribute
  carries an external JSON pointer" precedent. Faithful line-by-line port of `EmitSharedMapElement.All`'s
  logic for all 12 real `sharedMapElements` entries spanning all 4 CLR value shapes (float/double/
  `AttributeValue`/string). Retired `EmitSharedMapElement.cs`+`MapElementGenerator.cs` in undertow
  (`feat/codegen-unif-inc2-mapelem`, commit `0d6461f2`), checking in the Yeroket-generated
  `MapElements.g.cs` as an ordinary compiled source file — full undertow test suite (2955 tests) passes,
  0 failures, independently re-run by both the controller and the Opus validator from fresh builds. Full
  detail: `Undertow-Codegen-Unif-Inc2-Shared-Map-Elements-Plan-2026-07.md`.

- **Increment 3 — #14 `[Param]` declarations (COMPLETE, both milestones Opus-validated APPROVED).**
  Confirmed 37 real `[Param(...)]` sites across 8 files (`PopulationAttrs.cs`, `KnowledgeAttrs.cs`,
  `ResearchAttrs.cs`, `EconomyAttrs.cs`, `DiplomacyAttrs.cs`, `TrajectoryAttrs.cs`, `PlayerAttrs.cs`,
  `LoyaltyAttrs.cs`). A genuinely different architectural pattern than Inc-1/2: rather than inventing a
  new Yeroket-owned attribute, `--param-cs` DISCOVERS undertow's real, pre-existing
  `Undertow.Sim.ParamAttribute` via Roslyn compilation analysis of undertow's own source
  (`CompilationLoader.LoadParamFields`, zero assembly coupling — same syntax/semantic-name-matching
  pattern `[KernelCallable]`'s `--callable-cpp` already uses), because a live runtime-reflection
  consumer (`CodeModLoader.cs`, via `FieldsWith<ParamAttribute>()`) pins the attribute's exact shape,
  making a replacement attribute unsafe. `ParamRegistrationEmitter.cs` + the newly-shared
  `EnumExprHelper.cs` (ported from undertow's `EnumExpr.cs`, confirmed genuinely reused across
  `[Effect]`/`[System]`/`[Param]`'s generators — folded in now per the program's own "shared utility,
  first needing increment" rule) faithfully port `EmitRegisterParams.Emit`'s logic, including its exact
  dup-id diagnostic behavior. Full retirement in undertow (`EmitRegisterParams.cs`,
  `ParamRegistrationGenerator.cs`, `EmitRegisterParamsTests.cs` deleted; `DeclareParams.g.cs` checked in)
  — `Undertow.Sim.ParamAttribute` and `CodeModLoader.cs`'s live reflection consumer both confirmed
  UNTOUCHED. 2953/2953 undertow tests pass (2955 Inc-2 baseline minus the 2 retired
  `EmitRegisterParamsTests.cs` tests). Strongest equivalence proof in the program to date: the Opus
  validator rebuilt the PRE-retirement commit to regenerate the real original Roslyn output and
  byte-diffed it against the new mechanism's output in BOTH directions — body byte-identical, only the
  generated-banner comment differs. Full detail:
  `Undertow-Codegen-Unif-Inc3-Param-Declarations-Plan-2026-07.md`.

- **Increment 4 — #12 `[Action]` registration (COMPLETE, both milestones Opus-validated APPROVED).**
  Survey's `[KernelCallable]`-fit hypothesis was REFUTED: `[Action]` targets CLASSES
  (`AttributeTargets.Class`) and its generator emits a delegate-wiring dispatch/registration table over
  4 optional marker interfaces (`IAction`/`IActionGate`/`IActionDecompose`/`IActionLifecycle`), never
  transpiling a method body — a different codegen kind than `[KernelCallable]`'s method-body transpiler.
  Also found: `Undertow.Sim.ActionAttribute` is itself a live `CodeModLoader.cs` runtime-reflection
  target (for code-mod support), the SAME constraint class as Increment 3's `ParamAttribute` — so the
  mechanism is "discover undertow's real existing attribute" (Inc-3's pattern), not "introduce a new
  Yeroket-owned attribute" (Inc-1/2's pattern). Confirmed 6 real production `[Action(...)]` sites (survey
  undercounted at 5). New `--action-cs` CLI path + `CompilationLoader.LoadActionClasses` (discover-by-
  syntax-name, `sym.AllInterfaces` resolution) + `ActionRegistrationEmitter.cs` (line-for-line port of
  `EmitRegisterActions.Emit`, all 4 diagnostic paths, `EnumExprHelper` reused for the `Trigger` enum).
  Full retirement in undertow (`ActionRegistrationGenerator.cs`/`EmitRegisterActions.cs`/
  `EmitRegisterActionsTests.cs` deleted; `RegisterActions.g.cs` checked in) — `CodeModLoader.cs` confirmed
  byte-identical pre/post by both the implementer and the Opus validator (who independently re-derived
  the equivalence proof from scratch: own throwaway worktree, real `dotnet build` against the
  pre-retirement commit, direct diff/md5 against the checked-in output). 2951/2951 undertow tests pass.
  Full detail: `Undertow-Codegen-Unif-Inc4-Action-Registration-Plan-2026-07.md`.

- **Increment 5 — #1/#2/#3/#7 Authored+Baked def carriers, parse/bake table, sim registration cluster
  (COMPLETE, all 3 milestones Opus-validated APPROVED).** The largest and riskiest increment in the
  program to date — bundled 4 features sharing one JSON source (`schemas.json`, 38 kinds; per-feature
  real counts 31/36/28/32, three independent gates `data`/`codec`/`simRegister` plus a `ReadFields!=null`
  flat-scalar co-gate that excludes `planet`). Sequenced internally as Part A (Milestone 2: Authored/
  Baked def carrier POCOs, extending Increment 1's `[RegistrySlots]` emitter family — same attribute
  model, a second emitter pass, no new attribute) then Part B (Milestone 3: the bake-table/sim-
  registration method-tables, genuinely new emitters since no existing Yeroket mechanism emits an
  array-of-closures table) — a hard ordering dependency, since Part B's generated code references Part
  A's generated types by name. Confirmed via direct source read: NO CodeModLoader-style runtime-
  reflection constraint applies to this cluster (pure compile-time codegen-to-codegen consumption).
  **This increment finally executed Increment 1's long-deferred `EmitRegistrySlots.cs` retirement** — the
  real dependent migration Increment 1 was waiting on, confirmed via a concrete load-bearing dependency
  chain (`ContentBaker.cs:37`, `ContentLoader.cs:57,66`, `AuthoringModel.cs:192`, `UndertowSim.cs:159`
  all directly consume `[RegistrySlots]`-generated slot properties/`OverridableIds()`). Full retirement:
  10 source files deleted total (all 8 of the #1/#2/#3/#7 cluster's originals + Increment 1's
  `EmitRegistrySlots.cs`/`RegistrySlotGenerator.cs`), 6 generated `.g.cs` files checked in, 3 test files
  fully deleted + 13 surgically trimmed (removed only the retired-generator-calling method, preserved
  each file's still-live `EmitCodec` coverage) — exactly 22 test methods removed net, independently
  recounted method-by-method by the Opus validator via real diffs, not trusted from the implementer's
  report. Equivalence proof fully closed the loop (new-emitter output == checked-in file == old Roslyn
  generator's real build output, all independently re-derived from scratch by the validator), covering
  every real gate combination including nested object-list Bake-lambda conversion
  (`hook`/`personality`/`effect_set`/`rule`) and the `RuntimeCarrier` conversion path
  (`character`/`faction`/`timeline`). 2933/2933 undertow tests pass post-retirement, independently
  re-run by the validator from a fresh state. Full detail:
  `Undertow-Codegen-Unif-Inc5-Def-Carriers-Bake-SimReg-Plan-2026-07.md`.

- **Increment 6 — #10 Test factories + #11 Authoring builders (COMPLETE, all 3 milestones Opus-validated
  APPROVED).** The survey's flat "low-risk" tag for both features held structurally (no binary protocol,
  no novel algorithm) but hid genuinely different dependency profiles — the same pattern that already
  surfaced in Inc-4/Inc-5. #10 is a real, load-bearing dependent of Increment 5's `DefCarriersEmitter`:
  its factory methods construct `Baked{K}Def(...)` positionally, so the exact ctor-argument order must
  match Increment 5's emitter — confirmed to hold exactly by tracing 2 real kinds end-to-end (both
  implementer and validator, independently), then proven again via a genuine positional field-value
  correctness test (constructing real defs and asserting each argument lands on the correctly-named
  property with discriminating, non-default values — not just a text/byte diff) across 3 kinds. 30 real
  gated kinds (`data&&codec` minus `customCodec`), 496 real `Make.*` call sites in the test suite served
  as a large-scale regression safety net. #11 is independent (only depends on the shared field IR + the
  real UTDL parser, not on any Def-carrier type) — its `Build()` assembles a UTDL document string and
  re-parses it through the real parser rather than constructing objects directly; the equivalence
  proving process caught a REAL bug (missing the `Emit.ReadFields` enum-collapses-to-scalar behavior
  silently dropped 2 of 3 real kinds) and a real data-drift fact (`material`, one of `TargetKinds`' 4
  names, no longer exists in `schemas.json` — both old and new mechanisms already silently skip it).
  Both features fully retired (`TestFactoryGenerator.cs`/`EmitTestFactory.cs`/
  `TestFactoryCodegenTests.cs`; `BuilderGenerator.cs` + only the `Emit.Builders`-specific region of
  `Emit.cs`, leaving 9+ other live generators' shared IR helpers untouched). 2949/2949 undertow tests
  pass throughout, independently re-run from a clean state by the validator at every milestone. Full
  detail: `Undertow-Codegen-Unif-Inc6-Test-Factories-Authoring-Builders-Plan-2026-07.md`.

- **Increment 7 — #13 `[Effect]` registration (COMPLETE, both milestones Opus-validated APPROVED).** The
  last of the "kernel" attribute-driven cluster after `[Param]` (Inc-3) and `[Action]` (Inc-4), chosen to
  stress-test whether Inc-4's dispatch-table-builder pattern generalizes to a genuinely data-driven
  branch. `[Effect]` classes split into two ctor-shape buckets: 15 parameterless WorldGraph-only classes
  (`Undertow.Effects/Builtins/*`) vs. 33 sim-ctor classes (taking a single `UndertowSim` parameter,
  `Undertow.Sim/Systems/**`) — 48 real sites total, over 2x the survey's estimate of 21. Confirmed
  `Undertow.Sim.EffectAttribute` is itself a live `CodeModLoader.cs` reflection target
  (`InstantiateEffect` mirrors the SAME dual ctor-shape fallback logic for code-mod support) — same
  discover-don't-replace constraint class as `[Param]`/`[Action]`. Extended Increment 4's
  `CompilationLoader`/dispatch-table-emitter pattern with an added `HasSimCtor` discriminator (via
  `sym.InstanceConstructors`) and two emit templates (`new {Fq}()` vs `new {Fq}(this)`), porting both
  `EmitRegisterEffects.Emit`/`EmitRegisterSimEffects.Emit` plus all 3 diagnostics (`UTFX001` duplicate-id,
  `UTFX002` missing `IEffect`, `UTFX003` sim-bucket wrong ctor shape). Equivalence proving caught a real
  bug: bucket discrimination cannot use C# namespace string, since 4 real sim-ctor classes
  (`StoreFlowEffect`/`RunRecipeEffect`/`KnowledgeModifierEffect`/`TransmitFlowEffect`) are declared
  directly under `namespace Undertow.Sim` rather than a `.Systems.*` sub-namespace — fixed by
  discriminating on the schema file's on-disk project directory instead, matching the real Roslyn
  generator's actual `assemblyName` branch. Full retirement (`EffectRegistrationGenerator.cs`/
  `EmitRegisterEffects.cs`/`EmitRegisterEffectsTests.cs` deleted; both generated files checked in) — BOTH
  real call sites (`UndertowSim.cs` AND the previously-unremarked `HistorySim.cs:79`, which consumes only
  the WorldGraph-only half) confirmed byte-identical pre/post, along with `CodeModLoader.cs`.
  2950/2950 undertow tests pass, independently re-run by the validator from a clean state. Full detail:
  `Undertow-Codegen-Unif-Inc7-Effect-Registration-Plan-2026-07.md`.

- **Increment 8 — #15 `[System]` schedule solver (COMPLETE, both milestones Opus-validated APPROVED).**
  This program's own risk label for the increment — "novel algorithm, no Yeroket analog" — was CORRECTED
  by research before any building began: `SystemScheduleSolver.cs` is a textbook stable Kahn's-algorithm
  topological sort, phase-bucketed with `(Order, Id)`-sorted deterministic tiebreaking, already
  Roslyn-independent and unit-tested standalone — mechanically portable, not a hard algorithmic problem.
  The real difficulty was that Yeroket had no phase-bucketed scheduling CAPABILITY at all (net-new, not
  a hard port). Confirmed 37 real `[System(...)]` sites (not the survey/plan's estimated 34 — one file,
  `CoarseHistoryResolverSystems.cs`, holds 4 classes), forming 3 independent linear dependency chains
  with ZERO cycles or diamonds — and the 11 real `Before`/`After` edges are 100% `After`, 0% `Before`,
  so the equivalence proof needed a deliberately-added SYNTHETIC `Before`-only test to cover a codepath
  real data never exercises. Extended Increment 4/7's `CompilationLoader`/dispatch-table-emitter pattern
  with a verbatim standalone port of the solver (including its `PhaseRank` KEEP-IN-SYNC duplication) and
  all 5 diagnostics (`UTSYS002`-`UTSYS006`). **Found and deliberately did NOT touch a genuine pre-existing
  design gap**: `CodeModLoader.cs`'s mod-load path registers systems with raw declared `Order` only,
  never calling the solver or reading `Before`/`After` at all — an asymmetry between first-party
  (solved) and mod-loaded (unsolved) system ordering that predates this increment and was explicitly
  scoped OUT (documented, not silently "fixed," since fixing it would be a real behavior change beyond
  equivalence). Full retirement (`SystemRegistrationGenerator.cs`/`EmitRegisterSystems.cs`/
  `SystemScheduleSolver.cs` deleted; their tests PORTED to Yeroket rather than dropped, since a solver
  this load-bearing warranted keeping its own unit tests alongside the port) — byte-identical
  equivalence for all 37 real sites (validator independently re-derived via its own fresh Roslyn
  rebuild), `CodeModLoader.cs` confirmed zero-diff including the asymmetry's exact unchanged behavior.
  2942/2942 undertow tests pass. Full detail:
  `Undertow-Codegen-Unif-Inc8-System-Scheduler-Plan-2026-07.md`.

- **Increment 9 — #4/#5/#6 Content-pack codec + Merge + Patch-parser cluster (COMPLETE, all 3
  sub-milestones Opus-validated APPROVED).** The hardest increment in the program, correctly flagged as
  such — but for verification cost, not algorithmic complexity: an 81-version binary content-pack wire
  format with zero structural self-description within a record (field declaration order IS the byte
  order, enforced by nothing but code discipline), spanning 3 tightly-coupled features that must move
  together (#4 the binary codec, #5 conditional-merge patch records, #6 the patch-doc authoring parser).
  A genuine pre-flight concern — the golden-bytes regression fixture (`codec-golden.b64`) appearing
  0 bytes in an earlier checkout — turned out to be checkout-specific, not a real gap; the fixture is
  live, 1384 bytes, actively maintained (69 commits), and the oracle passed cleanly once verified fresh.
  Sequenced as 3 sub-milestones (2a #4, 2b #5, 2c #6) since the combined scope was too large for one
  dispatch, each extending Increment 5's carrier-emitter family over the same shared `Field` IR. The
  load-bearing proof for each sub-milestone was a REAL BINARY BYTE-DIFF, not just generated-text
  comparison: the implementer(s) built a harness that swapped the new Yeroket-generated code into the
  live Roslyn generator's `AddSource` call, rebuilt, and byte-diffed the resulting `.pack` blob against
  the golden fixture and against the original — independently re-derived from scratch by the Opus
  validator at every sub-milestone (never trusting reported hashes). Milestone 2b's proof caught a real
  bug (a missing `MapAuthored` guard producing a spurious element comparer) via the byte-diff itself,
  fixed before reporting done. Milestone 2c's end-to-end parse-then-merge cross-check (parse a real
  patch doc → merge onto a real def → verify correct final values) is the proof that #4/#5/#6 genuinely
  cohere as one working system, not three independently-plausible ports. Full retirement of all 6
  generator files across the cluster plus 33 obsolete Roslyn-internal test cases (spot-checked to hold
  zero real behavioral coverage) — all golden-bytes/merge/patch-authoring/per-kind codec tests kept and
  confirmed exercising the newly-generated checked-in code for real. 2922/2922 undertow tests pass,
  independently re-run by the validator from a completely fresh state at every sub-milestone. Full
  detail: `Undertow-Codegen-Unif-Inc9-Content-Pack-Codec-Plan-2026-07.md`.

- **Increment 10 — #16 `[Saved]` campaign-save codec (COMPLETE, both milestones Opus-validated
  APPROVED). THE FINAL INCREMENT OF THE PROGRAM.** Milestone 1's research CORRECTED the survey's own
  "unproven/not load-bearing" characterization: `[Saved]` is a real, live, tested campaign-save-game
  codec with 2 real production types (`TrendCell`, `FactionPlaceKey`) wired into `UndertowSim.cs`'s
  actual campaign save/load loop (`"UNDERTOW_CAMPAIGN 37"` header, `LoadedCampaignVersion` gating),
  distinct from Increment 9's content-pack codec (a different wire format for a different concern —
  live sim state vs. authored/baked static content; the two mechanisms were deliberately NOT unified
  beyond their superficial `BinaryWriter`/`BinaryReader` similarity). No `CodeModLoader.cs` reflection
  constraint applies (confirmed directly) — a genuinely new standalone emitter was correct, since the
  field model (`[Saved]`/`[SaveField(Intro=N)]`/`[SaveSkip]`/`[SaveCustom]`) needed its own small IR
  (`SavedTypeInfo`/`SaveFieldInfo`), not a shared IR from any prior increment. Milestone 2 ported
  `EmitSaveCodec.cs`'s 223 lines line-for-line (`SaveCodecEmitter.cs`) plus a CLI-side
  `SaveCodecDiscovery.Extract`/`ClassifyType` mirroring `SaveCodecGenerator.cs`'s own `IFieldSymbol`
  walk (`CompilationLoader.LoadSavedClasses`, same discover-by-syntax-name pattern as
  `[Action]`/`[Effect]`/`[System]`). A real gotcha surfaced and was fixed during equivalence proving:
  MSBuild's SDK-style implicit glob orders `@(Compile)` alphabetically by PROJECT-RELATIVE path
  (confirmed via `dotnet msbuild -getItem:Compile`), which differs from `Directory.GetFiles`'
  native OS-enumeration order (a directory's own files before its subdirectories) — the CLI had to
  explicitly re-sort file discovery to match, or generated member order silently diverged from the
  real Roslyn output despite byte-identical per-member content. Equivalence proof: the CLI's output
  against real undertow source was BYTE-IDENTICAL (including the BOM) to the real Roslyn-generated
  `SaveCodec.g.cs` for all 3 real `[Saved]` types (`TrendCell`, `FactionPlaceKey`,
  `_SaveGenProbe`); a genuine byte round-trip (write via generated `Save.<Type>`, read back via
  generated `Load.<Type>`, assert field values) was proven Yeroket-side for both real production
  type shapes; `_SaveGenProbe`'s gate/skip/custom coverage was exercised explicitly for BOTH the
  gated-field-present and gated-field-absent (old-format stream) cases, proving the version-gate
  default path, not just the happy path; both diagnostics (`UTSAVE001`/`UTSAVE002`) were exercised
  with deliberately malformed synthetic IR. Full retirement (`SaveCodecGenerator.cs`/
  `EmitSaveCodec.cs` deleted; no dedicated internals-only test file existed to delete; the checked-in
  `SaveCodec.g.cs` follows Increment 8's exact retirement precedent, banner byte-identical to the
  original Roslyn header) — `UndertowSim.cs`'s campaign orchestration and `CodeModLoader.cs`
  confirmed byte-identical pre/post (empty git diff, neither file touched). All 6 real save-related
  test files kept passing against the newly-generated checked-in code, including BOTH distinct
  `GrowthTrendSaveTests.cs` files (`Systems/` and `Saves/`, correctly disambiguated per Milestone 1's
  finding). 2934/2934 (`Undertow.Core.Tests`) + 21/21 (`Undertow.Vixen.Host.Tests`) = 2955 total
  undertow tests pass, 0 failures. Full detail:
  `Undertow-Codegen-Unif-Inc10-Saved-Codec-Plan-2026-07.md`.

## PROGRAM COMPLETE — 10/10 increments (2026-07-13)

Every one of the 17 features from the original survey has now been evaluated and migrated (or
explicitly merged into an existing mechanism) into the Yeroket kernel-framework's CodegenTool. Final
mechanism count: `[RegistrySlots]`, `[SharedMapElements]`, `--param-cs`, `--action-cs`, the def-carrier/
bake-table/sim-registration cluster, `--test-factories-cs`, `--authoring-builders-cs`, `--effect-cs`,
`--system-cs`, the content-pack codec cluster (`--codec-cs`/`--merge-cs`/`--patch-parser-cs`), and
`--save-codec-cs` — a coherent, minimal-decomposition set covering every real codegen need undertow's
original 17-generator Roslyn analyzer assembly served, per the program's own stated success metric (see
the Note below). Every increment's equivalence proof was non-vacuous and independently re-derived by an
Opus validator; every retirement left the full undertow test suite green. No feature was force-fit —
Increment 1's `EmitRegistrySlots.cs` retirement was correctly deferred until Increment 5 provided the
real dependent migration, and Increment 10's own risk label ("unproven/not load-bearing") was corrected
by its own research before building, the same discipline applied throughout.

## Follow-ups / open questions (explicitly not resolved in this doc)

- View Contract Inc-5's Milestone 4 (writer/reader wire-coupling blocker) is a SEPARATE, already-scoped
  increment (typed-accessor emitter from `ViewBlob`, or migrate `main.cpp` onto `BlobView`/`ViewStore`
  directly) — not folded into this program, but its lessons directly inform Increment 9's approach here.
- The exact "cluster" boundaries in Increments 5 and 9 may need splitting further once Increment 1's own
  research milestone is underway — the survey's dependency notes are a starting hypothesis, not a final
  decomposition; each cluster's own Task-1 research milestone should confirm or refine the grouping.

## Note

This program's success is measured by the FINAL Yeroket mechanism count being smaller than 17, with every
undertow codegen need still covered — not by how many increments get through cleanly. An increment that
correctly concludes "feature X already merges into `[KernelCallable]`, zero new code needed" is exactly as
valuable as one that ships new mechanism work; both reduce the eventual decomposition. Increments that hit
a real architectural gap and correctly defer/narrow (per this session's own repeated precedent) are not
failures — they are the mechanism by which this program stays honest about what's actually achievable.
