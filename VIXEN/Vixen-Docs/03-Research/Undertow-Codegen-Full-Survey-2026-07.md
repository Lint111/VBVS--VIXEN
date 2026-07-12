# Undertow Codegen Full Survey (2026-07-12)

**Purpose:** a factual, feature-by-feature inventory of EVERY codegen/schema-derived feature in the
`undertow` repo (`/home/liory/Github/undertow`), so future planning can decide which features to migrate
onto the shared Yeroket kernel-framework (`/home/liory/Github/Yeroket-Fantasy/Packages/
com.yeroket.utility.kernel-framework`) and in what order. This is a RESEARCH artifact, not a migration
plan — no design decisions are made here.

**Context:** dispatched mid-flight during View Contract Inc-5's Milestone 3 (the view-contract slice's own
migration), after that increment hit 3-4 consecutive real architectural gaps between undertow's codegen and
Yeroket's shipped mechanisms. The user asked for a wider survey to understand the FULL scope before
deciding how far the "undertow onto Yeroket" epic should go. The view-contract slice itself
(`ViewSchema.cs`/`EmitViewContractHeader.cs`/`EmitViewWriter.cs`) is tracked separately in
`View-Contract-Inc5-Undertow-Migration-Plan-2026-07.md` and is NOT re-covered here in depth.

## Architecture note

The whole non-view-contract codegen surface is ONE Roslyn analyzer assembly
(`Undertow.Authoring.Codegen`, netstandard2.0) wired as an `Analyzer`-type `ProjectReference` into 6 real
projects: `Undertow.Authoring`, `Undertow.Content`, `Undertow.Sim`, `Undertow.Effects`, `Undertow.View`,
`Undertow.Content.TestKit`. Every `[Generator]` runs in ALL six and self-guards via
`context.CompilationProvider.Select(c => c.AssemblyName)` against a `GeneratedAssembly` constant — a
single generator class often no-ops in 5 of 6 assemblies and emits in the 6th.

Two orthogonal input models:
- **Schema-JSON-driven** (13 generators) — read `core/src/Undertow.Authoring/Schema/schemas.json` (378
  lines, 38 kinds opted into `codegen`) via a hand-rolled recursive-descent JSON parser (`Json.cs`,
  deliberately NOT `System.Text.Json` to avoid analyzer-load version conflicts). The parsed `Field` model
  (`SchemaKeys.cs` + `Emit.cs`'s `ReadFields`) is the shared IR every one of these generators consumes.
- **Attribute-driven "kernel" generators** (4 generators, #12/13/14/15 below) — `ForAttributeWithMetadataName`
  Roslyn incremental pipelines over hand-authored `[Action]`/`[Effect]`/`[Param]`/`[System]` C# attributes.
  Explicitly self-labeled in-repo as "the Nth KERNEL source-gen" — undertow's own pre-existing
  "kernel-ish" pattern, structurally the closest thing to Yeroket's `[KernelCallable]` already present,
  independent of and predating any Yeroket dependency.

All 17 generators confirmed to produce real on-disk `.g.cs` output under `obj/**/generated/
Undertow.Authoring.Codegen/...` for both Debug and Release, in all relevant assemblies — nothing here is
a stub, unlike the `TagDensityKernel.cs` aspirational-comment false lead found earlier this session.

## Schema-JSON-driven features (source: `schemas.json`, IR: `Emit.ReadFields`)

| # | Feature | Emitter / Generator | Output → assembly | What it does | Consumption model | Real usage | Yeroket analog | Risk flags |
|---|---|---|---|---|---|---|---|---|
| 1 | Authored def carriers | `EmitDefs.cs`+`EmitRecord.cs` / `AuthoredDefGenerator.cs` | `AuthoredDefs.g.cs` (563 lines) → `Undertow.Authoring` | One immutable `Authored{K}Def` record class per `codegen.data=true` flat kind | Plain C# POCO, constructed by hand-written parsers | Confirmed live (8+ test files) | `[GpuStruct]`'s plain-struct emission, but no two-tier authoring/baked split | None major |
| 2 | Baked def carriers | `EmitDefs.cs`+`EmitRecord.cs` / `BakedDefGenerator.cs` | `BakedDefs.g.cs` → `Undertow.Content` | Same shape, gated on `codegen.codec=true` — the runtime/serialized carrier | Plain C# POCO | Confirmed live via `RegistrySlots.g.cs`/`RegisterKinds.g.cs` | none direct | Two-tier split has no Yeroket equivalent |
| 3 | Authored-kind parse/bake table | `EmitAuthoredKinds.cs` / `AuthoredKindsGenerator.cs` | `AuthoredKinds.g.cs` → `Undertow.Authoring` | `AuthoredKind[]` dispatch table + inline Authored→Baked bake lambdas | Data-driven delegate array | Live — drives the whole authoring pipeline | none | Bake-lambda codegen is intricate string-templated codegen-inside-codegen |
| 4 | Content-pack binary codec | `EmitCodec.cs` (556 lines, biggest schema emitter) / `CodecGenerator.cs` | `Codec.g.cs` — **2836 generated lines** → `Undertow.Content` | Per-kind `Write{Plural}`/`Read{Plural}` — a hand-matched binary codec with per-field `introVersion` gating, plus a parallel patch-table codec | Sequential `BinaryWriter`/`BinaryReader`, field-declaration order — a bespoke versioned binary wire format, NOT TOC-sectioned like the view-contract wire | Confirmed live and large — a real content/mod-pack save format | **No Yeroket analog** — no schema-driven binary-codec-with-version-gating emitter exists anywhere in Yeroket | **HIGH — same "custom binary protocol" gotcha class as view-contract, but a DIFFERENT protocol.** Byte order is declaration-order; any migration must preserve exact op sequence or break save/mod-pack compatibility |
| 5 | Conditional-merge patch records + Merge | `EmitMerge.cs` (298 lines) / `MergeGenerator.cs` | `Patches.g.cs` (844 lines) → `Undertow.Content` | Per-kind `Baked{K}Patch` (presence-aware delta) + `BakedPatches.Merge(def, patch)` pure fold + generated field-wise comparers | Plain C# pure functions, direct calls | Live — coupled to #4's version gates | **No Yeroket analog** — no patch/delta/mod-overlay concept exists at all | Medium — logic-only (3 merge flavors) but rides #4's codec, so lower risk than #4 alone |
| 6 | Patch-doc authoring parser | `EmitPatchParser.cs` (412 lines) / `PatchParserGenerator.cs` | `PatchParser.g.cs` → `Undertow.Authoring` | Parses a `patch: true` UTDL doc's present-only fields into a `Baked{K}Patch`, matching #5's shape | Hand-rolled DocNode-tree walker → typed patch record, `+`/`-`/bare-token grammar | Live — feeds #5's patch tables | none — bespoke text-authoring-language parser | Medium — undertow-specific mod-authoring syntax, unlikely portable concept |
| 7 | Sim-side content registration | `EmitRegisterKinds.cs` / `RegisterKindsGenerator.cs` | `RegisterKinds.g.cs` → `Undertow.Sim` | `RegisterKind[]` — registers each baked def by id into `sim.Content` | Data-driven delegate array | Live | closest conceptually to a static ECS-component-registration table | Low |
| 8 | Registry collection slots | `EmitRegistrySlots.cs` / `RegistrySlotGenerator.cs` | `RegistrySlots.g.cs` (×2) → `Undertow.Authoring`/`Undertow.Content` | Adds `List<{Prefix}{K}Def>` (+ baked-only `List<{K}Patch>`) properties to registry partials | Generated partial-class property injection | Live — foundational plumbing everything else depends on | Closest: `[ViewSection]`'s per-section collection-slot generation, different registry | Low complexity, HIGH blast radius (everything else depends on it) |
| 9 | Shared map-element structs | `EmitSharedMapElement.cs` / `MapElementGenerator.cs` | `MapElements.g.cs` → `Undertow.Content` | Small `readonly struct` per `sharedMapElements` entry (e.g. `SignatureEntry`, `RecipeIo`) with implicit KVP conversions | Plain struct | Live — referenced across #1-#8 | Closest: `[GpuStruct]` plain-struct emission | Low, small, self-contained |
| 10 | Test data factories | `EmitTestFactory.cs` / `TestFactoryGenerator.cs` | `Make.g.cs` → `Undertow.Content.TestKit` | `Make.{Kind}(...)` minimal-instance factory methods, only `id` required | Fluent-ish static factories, test-only | Live — test-support surface | none direct | Low — test-only, safe to skip/defer |
| 11 | Schema-driven authoring builders | `Emit.cs` (425 lines, hosts shared `Field` IR used by #1-#10) / `BuilderGenerator.cs` | `Builders.g.cs` + `CodegenMarker.g.cs` → `Undertow.Authoring` | Staged fluent wizard builders for 4 hardcoded flat kinds (`material`/`role`/`relationship_kind`/`place`); `Build()` renders a UTDL doc string, re-parsed by the real parser | Generated fluent staged builder (compile-time required-field ordering) | Live but narrow — 4 hardcoded kinds, not schema-driven membership | none — doc-authoring DX convenience | Low risk, low priority |

**Shared IR/utility files (not independent features):** `SchemaKeys.cs` (reserved-key filter), `Json.cs`
(hand-rolled JSON parser), `GeneratedHeader.cs` (the `<auto-generated/>` banner convention, same as the
view-contract slice), `GeneratedAssembly.cs` (the assembly-name guard every generator branches on).

## Attribute-driven "kernel" features (source: hand-authored C# attributes, NOT `schemas.json`)

| # | Feature | Emitter / Generator | Output → assembly | What it does | Consumption model | Real usage | Yeroket analog | Risk flags |
|---|---|---|---|---|---|---|---|---|
| 12 | `[Action]` registration | `EmitRegisterActions.cs` / `ActionRegistrationGenerator.cs` (self-commented "the first KERNEL source-gen") | `RegisterActions.g.cs` → `Undertow.Sim` | Finds `[Action(id, Trigger=, TieBreak=, Capability=, ...)]` classes implementing `IAction`/`IActionGate`/etc.; emits a `partial void RegisterGeneratedActions()` that instantiates+registers each, auto-wiring a generated capability gate | Direct object-instantiation + delegate wiring — logic transplant, generated code calls INTO the hand-written class | 5 confirmed `[Action(...)]` sites | **Closest analog: `[KernelCallable]`** — attribute-marks a hand-written class, generator wires it into dispatch. Validation (duplicate id, trigger/interface consistency) mirrors Yeroket's compile-time contract checks | Low-medium — closest existing fit to a Yeroket pattern |
| 13 | `[Effect]` registration (2 paths) | `EmitRegisterEffects.cs` (both `EmitRegisterEffects`+`EmitRegisterSimEffects`) / `EffectRegistrationGenerator.cs` | `RegisterEffects.g.cs` → `Undertow.Effects`; `RegisterSimEffects.g.cs` → `Undertow.Sim` | Finds `[Effect(id, Category=, Scope=, Persistence=, ...)]` + `[EffectParam(...)]` classes; TWO emission paths branch on ctor shape (closure-over-sim-state vs. parameterless) | Same logic-transplant pattern as #12, dual dispatch on ctor shape | 21 confirmed `[Effect(...)]` sites | Closest: `[KernelCallable]` again, but the ctor-shape dual-path is a wrinkle Yeroket's single-path model doesn't have | Medium — the `HasSimCtor` branch is a real structural difference from a simple callable transplant |
| 14 | `[Param]` (knob) declarations | `EmitRegisterParams.cs` / `ParamRegistrationGenerator.cs` | `DeclareParams.g.cs` → `Undertow.Sim` | Finds `[Param(id, Base=, Scope=, SeedOnApply=)]`-marked FIELDS; emits `reg.Declare(...)` calls | Data-driven declaration list, not logic transplant | 8 confirmed `[Param(...)]` sites | Similar in spirit to `[GpuStruct]` field scanning, but output is a runtime registry call, not a struct | Low — simplest of the 4 kernel generators |
| 15 | `[System]` registration + schedule solve | `EmitRegisterSystems.cs` / `SystemRegistrationGenerator.cs`, using `SystemScheduleSolver.cs` | `RegisterSystems.g.cs` (82 lines) → `Undertow.Sim` | Finds `[System(id, Phase=, Order=, Fidelity=, Before=[], After=[])]` classes; **compile-time topological sort** of Before/After edges within each `TickPhase`, bakes the solved rank as `Order`; validates cycles/dangling-refs/phase contradictions; dual Fine/Coarse fidelity dispatch | Logic transplant (registers `.Run`/`.Setup`/`.Cleanup`/`.Save`/`.RunCoarse` delegates) PLUS a genuinely novel compile-time constraint-solving pass | 34 confirmed `[System(...)]` sites — the largest attribute surface | **No close Yeroket analog** — `[Flow*]` does state-machine codegen but not a general Before/After topological scheduler with phase-rank validation | **Notable — a standalone algorithm (Kahn's algorithm + phase-rank table) with its own diagnostic codes. Porting would need to replicate the solver or accept losing declarative ordering** |
| 16 | `[Saved]` save/load codec | `EmitSaveCodec.cs` (223 lines, pure C#/no Roslyn) / `SaveCodecGenerator.cs` | `SaveCodec.g.cs` (69 lines — smallest real output) → `Undertow.Sim` | Finds `[Saved]` struct/class/record; per-field `[SaveField(Intro=N)]`/`[SaveCustom]`/`[SaveSkip]`; emits `Save.{Type}`/`Load.{Type}` + a generated `IComparer<T>` for deterministic ordering | Binary write/read pair, `SaveIo.Gated(...)` version-gate wrapper | **Only ONE type opts in today: `_SaveGenProbe.cs`** — its own comment says "Generator coverage ONLY... Not persisted by any slice" | Closest: `[GpuStruct]`'s field-driven codegen, for a different (save-game) format | **SPECULATIVE, not load-bearing — same pattern as `TagDensityKernel.cs`'s aspirational comment. Confirmed file:line: `core/src/Undertow.Sim/Saves/_SaveGenProbe.cs:5`. A genuinely SEPARATE binary format from #4 — don't conflate** |
| 17 | `EnumExprHelper` | `EnumExpr.cs` | shared helper, no own output | Converts a Roslyn `TypedConstant` enum value into a `"EnumType.Member"` expression string, robust to byte/int boxing | Build-time utility, used by #13/14/15's extract phases | Used internally | N/A | None — trivial |

## Prioritized migration-safety signal (research only, not a plan)

**Safest to migrate first** (small, self-contained, closest fit to an existing Yeroket mechanism):
- **#12 `[Action]`** and **#14 `[Param]`** — smallest attribute surfaces (5 and 8 sites), simplest
  logic-transplant/data-declaration shape, closest fit to `[KernelCallable]`-style patterns already
  shipped.
- **#9 shared map-element structs** and **#10 test factories** — small, no binary format, no cross-feature
  coupling; #10 is test-only so a partial/failed migration is low-stakes.
- **#8 registry slots** — mechanically trivial (partial-class property injection) though it's a
  load-bearing dependency for everything else — "safe to write" but "must be done carefully given blast
  radius."

**Hardest / riskiest** (custom binary protocols, typed-consumption gaps, or novel algorithms with no
analog):
- **#4 content-pack binary codec** — the single largest, most load-bearing feature (2836 generated
  lines), a bespoke versioned binary wire format with zero Yeroket equivalent. Same risk CLASS as the
  already-known view-contract wire-format gotcha (a real section+column TOC with 16-byte alignment), but
  a genuinely DIFFERENT protocol — solving one does not solve the other.
- **#5/#6 merge + patch-parser** — tightly coupled to #4's codec version gates; migrating #4 without them
  (or vice versa) will break byte-compatibility.
- **#15 `[System]` schedule solver** — a genuinely novel compile-time topological-sort-with-diagnostics
  algorithm; nothing in Yeroket does declarative Before/After scheduling today. Would likely need bespoke
  new Yeroket capability, not a mechanical port.
- **#16 `[Saved]` codec** — flagged not for structural difficulty but for being effectively unproven: only
  a synthetic probe type exists; real save-game types haven't migrated onto it yet. Confirm with the
  project owner whether this is stable ground before building on it.

## Cross-reference: the already-known view-contract slice's 4 gotchas (for pattern-matching against future work)

Discovered during Inc-5's Milestone 3 (tracked in `View-Contract-Inc5-Undertow-Migration-Plan-2026-07.md`),
listed here because they're the exact "gotcha class" this survey's risk flags above are calibrated against:
1. No Yeroket emitter produces a `view_contract.h`-shaped TYPED C++ accessor header (only a generic
   reflection-blob model exists) — a real, live consumption site (`main.cpp`) depends on the typed shape.
2. `[Projected]` currently only affects the RmlUi/View-Model-Binding C++ face, not any C# wire-writer path
   — an attribute whose effect doesn't reach the consumer you'd expect.
3. Undertow's real `UTVW` wire uses a section+column TOC with 16-byte alignment — a genuinely different
   binary protocol from Yeroket's SoA emitter's flat declared-order stream, not just different content.
4. The `[View]` schema model has no representation for a scalar `Vec3f` struct field or a nullable
   variable-length `ListVec3f` per-row column — `Bodies.Position`/`RecipeParams`/`OrbitPath` cannot be
   declared at all under the shipped model.

Any future increment touching #4 (content-pack codec) in particular should expect to find its OWN version
of gotcha #1/#3 (a custom wire protocol, a typed-consumption pattern) — the survey's risk flags above
already anticipate this rather than assuming it'll be discovered fresh.
