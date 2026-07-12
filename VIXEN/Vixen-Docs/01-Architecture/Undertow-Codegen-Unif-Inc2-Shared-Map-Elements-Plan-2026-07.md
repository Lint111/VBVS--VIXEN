# Undertow Codegen Unification — Increment 2: Shared Map-Element Structs (2026-07-12)

**Program:** `Undertow-Codegen-Unification-Program-2026-07.md`, Increment 2 (feature #9 in the survey).
**Why second:** small, self-contained, no cross-feature coupling — a low-risk increment validating the
"merge into existing mechanism" pattern before tackling anything load-bearing.

## Ground truth (read `EmitSharedMapElement.cs` in full, 2026-07-12 — 51 lines, verify file:line if moved)

`core/src/Undertow.Authoring.Codegen/EmitSharedMapElement.cs` — `EmitSharedMapElement.All(json)`:
- Parses `schemas.json`'s top-level `sharedMapElements` array (a DIFFERENT top-level key from Increment
  1's per-kind `codegen` gates — same file, same `Json.cs` parser, unrelated data).
- For each entry (`name`, `keyName`, `valueName`, `scalar`), emits a `public readonly struct {name} :
  IEquatable<{name}>` with exactly 2 fields (a `string` key + a typed value, e.g. `SignatureEntry`,
  `RecipeIo`), plus: a 2-arg constructor, an implicit tuple-conversion operator, implicit
  `KeyValuePair<string,T>` conversions (both directions), `Equals`/`GetHashCode`/`==`/`!=`.
- This is a genuinely simple, self-contained, boilerplate-struct emitter — no wire format, no logic
  transplant, no cross-feature coupling to Increment 1's registry slots or anything else.

**Compare against `[GpuStruct]`'s real emission** (the survey's own claimed closest analog) — Task 1 must
confirm or refute this concretely: does `[GpuStruct]`'s reflection-over-a-decorated-type shape actually fit
here, given this feature (like Increment 1's registry slots) ALSO reads an external list from JSON rather
than reflecting a decorated type's own fields? If Increment 1's finding (external-file-driven emission has
no existing analog) generalizes here too, this may ALSO need new mechanism work — possibly even the SAME
`[RegistrySlots]`-style mechanism Increment 1 just built, generalized, rather than a second bespoke
attribute. Task 1 should explicitly check: can Increment 1's new mechanism be reused/extended for this
shape, or does THIS feature's shape (which reflects VALUES from JSON directly into new struct TYPES,
not properties into an existing class) need something structurally different again?

## Scope boundary

- **IS:** Task 1 determines whether Increment 1's new `[RegistrySlots]`-family mechanism can be reused/
  extended, or a second small new mechanism is needed, or (least likely, per the above) an existing
  mechanism already fits. Implement whichever is justified, prove equivalence against
  `EmitSharedMapElement.cs`'s current real output for `schemas.json`'s actual `sharedMapElements` entries
  (e.g. `SignatureEntry`/`RecipeIo`, and any others present — read the real file, don't assume only 2
  exist), retire the old file ONLY once equivalence + at least the standalone-usage proof holds (this
  feature has no dependents within the program the way Increment 1's slots do, so retirement can likely
  happen within THIS increment if the proof is solid — confirm no other undertow codegen feature emits
  code that assumes `EmitSharedMapElement.cs`'s specific file/namespace shape before retiring).
- **IS NOT:** touching `schemas.json`'s `sharedMapElements` content.

## Tasks

### Task 1 — Ground the shape + decide mechanism (READ + REPORT before building)
- Read `EmitSharedMapElement.cs` fresh, read `schemas.json`'s real `sharedMapElements` array in full.
- Decide: reuse/extend Increment 1's `[RegistrySlots]`-family mechanism, or new small mechanism, or (if
  genuinely fits) an existing one. Justify against real code — do not assume Increment 1's mechanism
  applies just because both read external JSON; check the actual shape match (properties-into-existing-
  class vs. new-struct-types-from-JSON-entries are different emission shapes).
- Confirm whether retiring `EmitSharedMapElement.cs` this increment is safe (check for any consumer
  assuming its specific output shape/namespace beyond the structs themselves).

### Task 2 — Build + equivalence proof
- Implement per Task 1's decision. Prove equivalence (same struct shape/fields/conversions/equality
  behavior) for the real `sharedMapElements` entries in `schemas.json` — non-vacuous, real multi-entry
  data, not a single toy struct.
- If Task 1 confirmed retirement is safe: retire `EmitSharedMapElement.cs`. If not: leave it running,
  same "proven equivalent, not yet retired" pattern as Increment 1, and report why.

## Gates / guardrails
- Non-vacuous equivalence proof (real entries, exercise the implicit conversions + equality operators).
- rtk masks git exit codes — use `/usr/bin/git` for evidence.
- Isolated undertow worktree (create fresh, off `master`, following Increment 1's pattern) — do not touch
  the main checkout or any other worktree. Do NOT push. Commit as work completes.

## Milestone Map
- [x] **Milestone 1 (Task 1):** ground the shape, decide mechanism (report-back gate). One Sonnet
  implementer + one Opus validator.
- [x] **Milestone 2 (Task 2):** build + equivalence proof + retire. DONE 2026-07-13. One Sonnet
  implementer (Opus validation pending).

## Progress Log

- Milestone 1 (Task 1, research-only): DONE · 2026-07-12 · no files modified in either repo
  - **Corrected ground truth**: `sharedMapElements` has **12 entries**, not the 2 examples this plan doc
    cited — `SignatureEntry`/`TagThreshold`/`CategoryThreshold` (float), `RecipeIo`/`CompositionMix`/
    `EconomicNeed`/`EconomicProduce`/`KnobValue` (number→double), `ParamEntry`/`AttributeEntry` (attr→
    `Undertow.Substrate.AttributeValue`), `ArchetypeLayer` (id→string, a deliberate map-value special
    case per `Emit.MapValueScalar`), `RecipeParam` (float) — controller independently verified all 12
    names/scalars against the real `schemas.json`. Task 2's equivalence proof must exercise all 4 distinct
    CLR value shapes (double/float/AttributeValue/string), not just float.
  - **Finding: a genuinely new, small mechanism is needed — neither `[RegistrySlots]` reuse NOR
    `[GpuStruct]` fits.** `[RegistrySlots]` injects properties into ONE existing decorated class;
    `EmitSharedMapElement` emits N freestanding new struct TYPES with no host class at all — different
    emission shape at the most basic level. `[GpuStruct]` was explicitly re-examined (not dismissed by
    analogy) and also rejected: it reflects an EXISTING decorated C# struct's own fields and emits C++
    std430 layout only (`ScalarKind` strictly `{U32,I32,F32}` — controller independently confirmed, no
    double/string/AttributeValue support at all); `EmitSharedMapElement` has no C# source type to reflect
    from (pure JSON-driven) and emits a C# struct with a rich hand-authored API (tuple/KVP conversions,
    equality) — the inverse on every axis. Verdict: a new small `[SharedMapElements(schemaPath)]`-style
    attribute + `SharedMapElementEmitter` porting `EmitSharedMapElement.All`'s logic line-for-line, mirror-
    ing Increment 1's own "attribute carries an external JSON pointer" precedent (the one part of Inc1's
    pattern that DOES generalize) but NOT the properties-into-existing-class emission shape.
  - **Retirement safety: SAFE to retire within THIS increment** (unlike Increment 1, which deferred).
    Grepped ~40 real consumer files (`SimScenario.cs`, `BakedContentPack.cs`, parsers, tests, etc.) —
    every consumer uses the generated structs as plain ordinary types (tuple/ctor construction, named
    property access, implicit KVP conversions, equality) with zero coupling to the generator's specific
    file/namespace mechanics beyond the fixed `Undertow.Content` target namespace (which the new mechanism
    must reproduce, confirmed straightforward). `MapElementStructTests.cs` (the one existing test)
    exercises exactly this API surface — a natural equivalence-proof template for Milestone 2. Zero
    dependents within the program, unlike Increment 1's registry slots.
  - No plan-doc assumption disproven beyond the corrected entry count.
  - **Opus validator (independent re-verification, actively re-examined the GpuStruct rejection):**
    confirmed all 12 entries/scalars exactly; confirmed `RegistrySlotsEmitter` genuinely only emits
    properties into one host class (no freestanding-type path); confirmed `GpuStructModel`'s `ScalarKind`
    is exactly `{U32,I32,F32}` with `TryMapScalar` throwing `NotSupportedException` on anything else, and
    `StructLayout.Build` genuinely requires reflecting an existing decorated type. **One wording
    correction**: GpuStruct's emission is "GPU-shader-side (C++/GLSL) only" (a sibling
    `GpuStructGlslEmitter.cs` also exists), not strictly "C++-only" as originally phrased — cosmetic, does
    not affect the rejection (neither emitter can produce a C# struct with conversions/equality).
    Confirmed `Emit.MapValueScalar("id")` genuinely returns `"text"`→`string` (not `NamespacedId`),
    verified against `Emit.cs:353` and `356-365` and `MapElementStructTests.cs:48`. Spot-checked 4 real consumer
    files directly (`RecipeParser.cs`, `BakedContentPack.cs`, `BodyArchetypeParser.cs`,
    `MapElementStructTests.cs`) — all plain ordinary-type usage, retirement safe. **Design note carried
    to Milestone 2**: unlike Increment 1's `[RegistrySlots]` (decorates a real existing aggregator), the
    shared-map-element structs are freestanding with no natural host class — Milestone 2 must pick an
    attribute carrier (a marker/placeholder class, or investigate a module-level attribute) and must
    reproduce the exact `namespace Undertow.Content` + `#nullable enable` header shape. **APPROVED —
    Milestone 2 may proceed.**

- Milestone 2 (Task 2, build + equivalence proof + retire): DONE · 2026-07-13
  - **Attribute-carrier decision: assembly-level `[SharedMapElements(schemaPath)]`, not a marker
    class.** Justification: unlike `[RegistrySlots]` (Inc-1), which decorates a REAL existing
    aggregator class (AuthoringRegistries/BakedRegistries), the shared-map-element structs have NO
    natural host type at all — inventing a placeholder marker class would be pure ceremony with no
    referent in the domain. An assembly-level attribute (`AllowMultiple = true`) carries the external
    schema pointer directly, mirroring N assembly attributes over one class with N properties at
    equivalent expressiveness. `CompilationLoader.LoadSharedMapElementsSchemaPaths` reads
    `Compilation.Assembly.GetAttributes()` instead of scanning decorated type symbols (the shape every
    other Loader method in this file uses).
  - **New mechanism** (Yeroket `feat/codegen-unif-inc2-mapelem`, branched off Inc-1's
    `feat/codegen-unif-inc1-slots` per the plan's "keep both increments together pre-consolidation"
    option, commit `1af5865e`): `[SharedMapElements]` attribute (`Runtime/GpuStructAttributes.cs`),
    `SharedMapElementsModel` + `SharedMapElementEmitter` (`SourceGenerator~/Transpiler/
    SharedMapElementEmitter.cs`, ports `EmitSharedMapElement.All` line-for-line including its own
    `MapValueScalar`/`Clr` scalar-mapping helpers — these had no existing home in Yeroket's shared
    JSON utilities, ported fresh alongside the emitter rather than reusing `RegistrySlotsEmitter`'s
    copy, since Inc-1's are `internal` to a different static class), `--shared-map-elements` CLI flag
    in `CodegenTool~/Program.cs` (mirrors `--registry-slots`'s `--schema`/`--out-cs`/`[--check]` shape,
    plus a `--namespace` override defaulting to `Undertow.Content`).
  - **Equivalence proof, two independent methods, all 4 CLR value shapes exercised** (float/double/
    AttributeValue/text-for-id), against all 12 real `sharedMapElements` entries:
    1. `SharedMapElementEmitterTests.cs` (7 NUnit tests, Yeroket `CodegenTool.Tests`) compiles the
       generated source to an in-memory assembly (Roslyn, `CompileRun`-style) and exercises REAL
       runtime behavior via reflection: tuple-ctor construction, both `KeyValuePair` conversion
       directions, `Equals`/`GetHashCode`/`==`/`!=`, mirroring `MapElementStructTests.cs`'s exact
       scenarios (`SignatureEntry`/`RecipeIo`/`ParamEntry`/`ArchetypeLayer`/`RecipeParam` cases) —
       all 7 pass.
    2. An end-to-end CLI run against undertow's ACTUAL `schemas.json` (real file, not a hand-typed
       subset), diffed byte-for-byte against a direct standalone invocation of
       `EmitSharedMapElement.All` on the same file: **struct bodies for all 12 entries are IDENTICAL**
       (only the provenance banner line differs, by design — new mechanism, new banner text).
  - **Retirement executed** (undertow `feat/codegen-unif-inc2-mapelem`, commit `0d6461f2`):
    `EmitSharedMapElement.cs` and `MapElementGenerator.cs` (the `[Generator]`/`IIncrementalGenerator`
    wiring) deleted; `MapElements.g.cs` (the new mechanism's output against the real schema) checked
    in as an ordinary compiled source file under `Undertow.Content`. **Wiring-model change discovered
    and handled**: undertow's ONLY existing codegen integration is Roslyn analyzer-based
    (`Undertow.Content.csproj` references `Undertow.Authoring.Codegen` as an `OutputItemType=Analyzer`
    project reference + `AdditionalFiles` for `schemas.json`) — there is no existing build-time
    invocation of an external CLI tool anywhere in undertow (confirmed via grep), unlike Yeroket's own
    package build. Since Inc-1 left `[RegistrySlots]` unwired for the same reason (deferred retirement),
    and this increment's retirement is in-scope now, the pragmatic choice was a checked-in generated
    file (regenerate via `dotnet run -- --schema <dir> --shared-map-elements schemas.json --out-cs
    core/src/Undertow.Content/MapElements.g.cs` against the kernel-framework `CodegenTool~`) rather
    than inventing new MSBuild pre-build-step plumbing undertow has never had — smallest change that
    satisfies "genuinely switch the build wiring."
  - **Consumer verification**: full `dotnet build` of `core/Undertow.sln` — 0 errors, 0 warnings. Full
    `dotnet test` — **2955 tests pass, 0 failures** (2934 `Undertow.Core.Tests` + 21
    `Undertow.Vixen.Host.Tests`), including `MapElementStructTests.cs` (the 4-test template Milestone 1
    flagged) exercising the exact API surface real consumers depend on. Spot-checked no source file
    still references the deleted `MapElementGenerator`/`EmitSharedMapElement` symbols (grep, zero hits).
  - Gotcha avoided: `SDFNodeGenerator.dll` rebuilt non-deterministically during the Yeroket test build
    (known drift per program-level memory) — reverted via `git checkout --` before committing, since
    no source change to that generator was made.
  - No plan-doc assumption disproven. **Opus validation of Milestone 2 not yet run** — flagging this
    explicitly since the program's established pattern is Sonnet-implement + Opus-validate per
    milestone.
