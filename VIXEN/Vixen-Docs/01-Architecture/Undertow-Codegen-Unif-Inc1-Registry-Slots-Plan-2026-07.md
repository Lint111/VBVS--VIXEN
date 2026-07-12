# Undertow Codegen Unification — Increment 1: Registry Collection Slots (2026-07-12)

**Program:** `Undertow-Codegen-Unification-Program-2026-07.md`, Increment 1 (feature #8 in the survey).
**Why first:** everything else in the schema-JSON-driven cluster (features #1/#2/#3/#7, and indirectly
#5/#6 via #4) depends on the registry slots existing — do this correctly once, first, rather than have
6 later increments each rediscover the dependency.

## Ground truth (read `EmitRegistrySlots.cs` in full, 2026-07-12 — verify file:line if moved)

`core/src/Undertow.Authoring.Codegen/EmitRegistrySlots.cs` (74 lines) — `EmitRegistrySlots.All(json,
typePrefix, ns, registryClass, emitMerge, emitOverridableIds, headerWhat)`:
- Parses `schemas.json`, collects one `(slotName, defTypeName, isSimRegister)` tuple per opted-in flat
  kind (gated on `codegen.data=true` for the Authored registry, `codegen.codec=true` for the Baked
  registry — same `ReadFields`-based flat-kind gate `Emit.cs` uses elsewhere).
- Emits a `partial class {RegistryClass}` with one `public List<{Prefix}{Kind}Def> {Plural} { get; } = new
  List<...>();` property per slot.
- Baked-registry-only: ALSO emits a parallel `List<{Kind}Patch> {Plural}Patches` per slot (the
  conditional-merge delta side-table, feature #5's own data).
- Conditionally emits `MergeGeneratedFrom(other)` (Authored registry: `AddRange` every slot from another
  instance — used for mod-pack overlay merging) and `OverridableIds()` (Baked registry: yields every
  `simRegister`-opted-in slot's ids — the set a mod may legally override).
- Called TWICE from the generator entry point (once per registry class, `Undertow.Authoring`'s
  `AuthoringRegistries` and `Undertow.Content`'s `BakedRegistries` — confirm exact call sites during Task 1).

This is a PURE partial-class property-injection generator: no binary wire format, no logic transplant, no
attribute consumption — it reads `schemas.json` (already-parsed `Field`/kind-gate IR shared with features
#1/#2/#3) and emits declarative C# property + method bodies. Per the survey's own read, this is the
CLOSEST FIT among all 17 features to something `[GpuStruct]`'s existing field-driven emission style could
plausibly extend to cover, though the exact mechanism (a NEW attribute marking "this class gets slots for
these kinds," vs. reusing schema-JSON directly) is Task 1's decision to make, not assumed here.

**Known calibration risks (per the program doc's Ground Truth) to actively check for, not assume absent:**
- Does the `MergeGeneratedFrom`/`OverridableIds()` method-generation logic have a clean Yeroket analog, or
  would merging this feature require inventing new emitter capability beyond plain field/property
  emission (which none of `[GpuStruct]`/`[View]`/`[KernelCallable]`/`[Flow*]` currently do — they emit
  structs/wiring/dispatch, not arbitrary generated METHOD BODIES over a dynamic list of slots)?
- Is `schemas.json` itself something Yeroket's toolchain can consume directly (its own JSON, distinct
  format from anything Yeroket already parses), or does merging this feature imply ALSO deciding whether
  Yeroket should learn to read `schemas.json`, or whether undertow's kinds should be re-declared as
  Yeroket `[GpuStruct]`/`[View]`-style C# attributes instead (a bigger, cascading decision touching
  features #1/#2/#3/#7 too, since they share the same JSON source)?

## Scope boundary (what Increment 1 IS and IS NOT)

- **IS:** Task 1 (research) determines definitively: does an existing Yeroket mechanism (or a small,
  well-justified extension of one) cover "emit N collection-slot properties + 2 optional generated
  methods from a data-driven list of kinds," or is this genuinely new mechanism work? Report the finding
  before building anything.
- **IS:** if a merge is possible, implement it and prove equivalence (the regenerated
  `AuthoringRegistries`/`BakedRegistries` partial classes are behavior-identical to today's hand-rolled
  `EmitRegistrySlots.cs` output — same slot names, same types, same `MergeGeneratedFrom`/`OverridableIds`
  behavior) for a REAL subset of undertow's actual `schemas.json` kinds, not a synthetic toy schema.
- **IS NOT:** deciding the fate of features #1/#2/#3/#7 (they depend on this feature's slots existing, but
  their OWN migration is separate increments, later in the program). Increment 1 only needs to prove the
  SLOT mechanism transfers — not that the def-carrier types the slots hold have also migrated.
- **IS NOT:** touching `schemas.json`'s content or undertow's kind-authoring workflow at all — this
  increment reads the JSON as an input, it does not change what's IN it.
- **IS NOT:** retiring `EmitRegistrySlots.cs` until the equivalence proof holds AND at least one dependent
  increment (a later item in the program) has confirmed the migrated slots actually work end-to-end with
  real def-carrier data flowing through them — do not retire on a standalone unit-test pass alone if that
  risks breaking dependents before they're ready.

## Tasks

### Task 1 — Ground the shape (READ + REPORT before building)

- Read `EmitRegistrySlots.cs` fresh (confirm it matches this doc's Ground Truth — files may have moved).
- Read `schemas.json`'s actual structure for a representative sample of kinds (both `codegen.data=true`
  and `codegen.codec=true` cases, at least one `simRegister`-opted-in kind) to ground the real input shape.
- Read the actual call sites that invoke `EmitRegistrySlots.All(...)` twice (find the generator class(es)
  that call it — confirm which Roslyn `[Generator]` this logic lives inside, per the survey's
  `RegistrySlotGenerator.cs` citation).
- **Decide and REPORT**: does this feature merge into an existing Yeroket mechanism, or need new work?
  Consider concretely: `[GpuStruct]`'s emission is per-TYPE (one struct, decorated with an attribute,
  reflection over ITS OWN fields) — `EmitRegistrySlots` is fundamentally different: it emits INTO an
  externally-named class (`AuthoringRegistries`/`BakedRegistries`) properties for a LIST OF OTHER TYPES
  read from an external data file, not reflected from the decorated type itself. This may mean NO existing
  Yeroket mechanism covers this shape as-is, and either (a) a genuinely new attribute/mechanism is needed
  (e.g. a new `[RegistrySlots(schemaPath, gate)]`-style class attribute), or (b) the "right" Yeroket-native
  way to express this is different enough from undertow's current shape that a straight port doesn't make
  sense and a redesign is warranted. Report your finding plainly — this may be a case where "the pre-existing
  logical path" the user wants doesn't fully exist yet and this increment's real job is deciding what
  minimal NEW capability to add, not finding a hidden existing fit.
- Confirm whether `MergeGeneratedFrom`/`OverridableIds()`'s generated METHOD BODIES (not just properties)
  have any Yeroket precedent — check `RmlDataModelEmitter.cs`'s `Bind*Model` function-body generation (the
  closest thing Yeroket does to "emit a method body over a dynamic list," from the View Contract work) as
  a possible pattern to mirror, even though its domain is completely different.

### Task 2 — Build (scope depends entirely on Task 1's finding)

- If merge is possible via an existing/lightly-extended mechanism: implement it.
- If genuinely new mechanism work is needed: implement the smallest correct new capability (mirroring
  Inc-Ovr's Override mechanism's discipline — real design work, not hand-waved), scoped explicitly per
  Task 1's finding, not improvised beyond what Task 1 justified.

### Task 3 — Equivalence proof

- Regenerate the `AuthoringRegistries`/`BakedRegistries` partial classes via the new mechanism for a REAL
  subset of `schemas.json`'s actual kinds (not a toy schema) and diff against `EmitRegistrySlots.cs`'s
  current real output — same slot names/types, same `MergeGeneratedFrom`/`OverridableIds` behavior
  (exercise both with real multi-kind data, not a single-kind vacuous case).

### Task 4 — Do NOT retire yet

- Per the scope boundary above: leave `EmitRegistrySlots.cs` running as-is even after Task 3's proof holds.
  Retirement is deferred until a later program increment confirms end-to-end dependent behavior. Report
  this explicitly as the increment's final state — "proven equivalent, not yet retired, by design."

## Gates / guardrails

- Non-vacuous proof: exercise both registries (Authored + Baked), a `simRegister`-opted-in kind AND one
  that isn't, and both `MergeGeneratedFrom`/`OverridableIds` code paths with real multi-item data.
- If Task 1 concludes new Yeroket mechanism work is required, that decision must be reported and
  Opus-validated BEFORE Task 2 starts building — same discipline as every prior increment's Task 1 gate.
- rtk masks git exit codes — use `/usr/bin/git` for evidence in all repos touched.
- This undertow worktree (`.claude/worktrees/codegen-unif-inc1-slots`, branch `feat/codegen-unif-inc1-slots`)
  is isolated and pre-cleared for in-tree destructive/git operations. Do NOT touch undertow's main
  checkout or any OTHER undertow worktree.
- Do NOT push in any repo. Commit as work completes.

## Milestone Map

- [x] **Milestone 1 (Task 1):** ground the shape, decide merge-vs-new-mechanism (report-back gate, no
  building until confirmed). One Sonnet implementer + one Opus validator.
- [ ] **Milestone 2 (Task 2-3):** build (scope per Milestone 1's decision) + equivalence proof. One Sonnet
  implementer + one Opus validator.

## Progress Log

- Milestone 1 (Task 1, research-only): DONE · 2026-07-12 · no files modified in either repo
  - **Finding: NEW Yeroket mechanism work needed — no existing mechanism merges this feature.**
    Confirmed `RegistrySlotGenerator.cs` (sole caller of `EmitRegistrySlots.All`) branches on
    `context.CompilationProvider`'s `AssemblyName`, calling it exactly twice: `("Authored",
    "Undertow.Authoring", "AuthoringRegistries", emitMerge: true, emitOverridableIds: false, ...)` and
    `("Baked", "Undertow.Content", "BakedRegistries", emitMerge: false, emitOverridableIds: true, ...)`.
    Read `schemas.json`'s real heterogeneous kind gating (`data`/`codec`/`simRegister` flags vary
    independently per kind — confirmed via `composition_profile`/`tag`/`planet` as representative
    examples). Read `GpuStructCppEmitter.Emit(StructModel m)` — confirmed single-model signature, no
    path for injecting properties for a LIST of unrelated external models into a third, undecorated
    host class. Read `RmlDataModelEmitter.Emit(ViewStruct v)` (closest "generated method body over a
    dynamic list" precedent) — confirmed it always iterates ONE schema's own declared fields, never a
    computed cross-cutting slot list gated by an external file.
  - **Root cause of the mismatch**: every existing Yeroket attribute reflects the DECORATED type's own
    fields (`[GpuStruct]`, `[View]`, etc.). `EmitRegistrySlots` inverts this — it populates an
    UNDECORATED class (`AuthoringRegistries`/`BakedRegistries`) with properties/methods derived from an
    EXTERNAL JSON file's gated kind list. No existing mechanism's reflection axis matches this shape;
    retrofitting `[GpuStruct]` would require inventing an alien "populate me from an external file"
    semantic, or making all ~15 schema.json kinds their own `[GpuStruct]`-tagged types plus a NEW
    aggregator — which is new mechanism work just relocated, not an actual merge.
  - **Sketch for Milestone 2 (not implemented, pending Opus validation)**: a new class attribute (e.g.
    `[RegistrySlots(schemaPath, gate: RegistryGate.Data, emitMerge:true, emitOverridableIds:false)]`) on
    `AuthoringRegistries`/`BakedRegistries` themselves — self-describing the target vs. today's hardcoded
    assembly-name branch — paired with a new emitter reusing the existing kind-gate parsing logic
    (`OptsIntoData`/`OptsIntoCodec`/`ReadFields`-equivalents) to inject one `List<T>` property per gated
    kind plus the two conditional method bodies.
  - No plan-doc assumption disproven — the Ground Truth and calibration risks all held up under direct
    inspection. Controller independently verified both call sites and the `GpuStructCppEmitter.Emit`
    signature against real source before accepting this finding.

## Follow-ups (explicitly out of scope, note for later increments)

- Retiring `EmitRegistrySlots.cs` — deferred until a dependent increment (#1/#2/#3/#7's own migration)
  confirms end-to-end behavior with real def-carrier data.
- Features #1/#2/#3/#7's own migration — separate, later program increments; this increment only proves
  the slot mechanism, not the def-carrier types the slots will eventually hold.
