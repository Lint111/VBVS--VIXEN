# Undertow Codegen Unification — Increment 7: `[Effect]` Registration (2026-07-13)

**Program:** `Undertow-Codegen-Unification-Program-2026-07.md`, Increment 7 (feature #13 in the survey).
**Why seventh:** the last of the "kernel" attribute-driven cluster after `[Param]` (Inc-3) and `[Action]`
(Inc-4) — chosen next specifically because the survey flagged it as harder than `[Action]` due to a
"dual ctor-shape branch," a genuine stress-test of whether Increment 4's dispatch-table-builder pattern
generalizes to a data-driven branch rather than a single uniform shape.

## Ground truth (read fresh 2026-07-13 by a research agent — verify file:line if moved)

- **`EffectAttribute`** (`core/src/Undertow.Effects/EffectAttribute.cs:9-20`) —
  `[AttributeUsage(AttributeTargets.Class, AllowMultiple=false, Inherited=false)]`. Ctor `(string id)`;
  named props `Category`/`Scope`/`Persistence` (enums) + `Description` (string). Scalar metadata only —
  behavior lives in `IEffect` (`Apply`, required) and optional `IEffectPrecondition` (`Precondition`),
  both defined in the same file (lines 40-42). A companion `EffectParamAttribute` (line 24,
  `AllowMultiple=true`, also class-targeted) declares the input schema: `Name`/`Type`/`Required`/
  `Vocab`/`Enum[]` — repeatable, declaration order = param-schema order.
- **Generator** — `EffectRegistrationGenerator.cs` (Roslyn `IIncrementalGenerator`,
  `ForAttributeWithMetadataName` on `Undertow.Effects.EffectAttribute`) branches on `assemblyName`,
  producing TWO separate outputs from `EmitRegisterEffects.cs`'s two static classes:
  - `EmitRegisterEffects.Emit` → `GeneratedEffects.RegisterAll(EffectRegistry)` in `Undertow.Effects`
    (WorldGraph-only path, `new {ClassName}()` parameterless).
  - `EmitRegisterSimEffects.Emit` → `UndertowSim.RegisterGeneratedSimEffects()` partial-method body in
    `Undertow.Sim` (sim-ctor path, `new {ClassName}(this)`).
- **The dual ctor-shape branch — CONFIRMED REAL** (`EffectRegistrationGenerator.cs:37-54`, `Extract`
  lines 88-90), not just a namespace difference. Classes in `Undertow.Effects/Builtins` (16 sites) must
  have a PARAMETERLESS ctor and implement `IEffect`; violation → diagnostic `UTFX002`. Classes in
  `Undertow.Sim/Systems/**` (32 sites) must instead have a public ctor taking a single
  `Undertow.Sim.UndertowSim` parameter (`HasSimCtor`, detected via `sym.InstanceConstructors`) so `Apply`
  can close over sim blackboard state unreachable at gen-time; violation → diagnostic `UTFX003`
  ("WorldGraph-only effects belong in Undertow.Effects"). The emitted `new {Fq}()` vs `new {Fq}(this)`
  call shape differs per path, and each path targets a different generated file/class/assembly.
- **Real usage: 48 sites** (survey estimated 21 — real count is over 2x). 16 parameterless
  WorldGraph-only sites: `Undertow.Effects/Builtins/Builtins.cs` (8), `Undertow.Effects/Builtins/
  PairEffects.cs` (8). 32 sim-ctor sites spread across `Undertow.Sim/Systems/{ActionEffects.cs,
  EffectSetEffects.cs, Diplomacy/DiplomacyEffects.cs, Economy/{ClaimEffects,EmitterEffect,
  KnowledgeModifierEffect,LinkEffect(x2),RoleEffects,RunRecipeEffect,StoreFlowEffect,
  TransmitFlowEffect}.cs, FactionAi/{FactionAiEffects(x4),LoyaltyEffects(x2)}.cs, Knowledge/
  {KnowledgeEffects(x4),SensorEffects(x4)}.cs, Market/MarketEffects.cs(x2), Narrative/QuestEffects.cs(x2),
  Places/PlaceEffects.cs, Stock/StockEffects.cs(x2)}` (recount exact files/counts in Task 1 — list above
  may drift slightly from a fresh read).
- **CodeModLoader.cs safety — CONFIRMED, same class as `[Param]`/`[Action]`.** `RegisterAssemblySeams`
  (`core/src/Undertow.Sim/CodeModLoader.cs:89-104`) reflects `TypesWith<EffectAttribute>(asm)`, reads
  `GetCustomAttribute<EffectAttribute>()` + `GetCustomAttributes<EffectParamAttribute>()`, and —
  critically — mirrors the SAME dual ctor-shape branch via `InstantiateEffect` (lines 419-428): tries a
  public `UndertowSim`-taking ctor first, falls back to parameterless, throws
  `CodeModSignatureException` if the type doesn't implement `IEffect`. A second scan at line 342 checks
  duplicate ids at load time. This is a hand-written parallel mirror (not a caller of the generator) —
  mechanism must be "discover, don't replace" (Inc-3/Inc-4's pattern), not a new attribute.
- **Additional dependents (contained blast radius, unlike Inc-5's cluster):** `UndertowSim.cs:444-450`
  calls both `GeneratedEffects.RegisterAll(Effects)` and `RegisterGeneratedSimEffects()` in sequence at
  construction. `core/src/Undertow.Generation/HistorySim.cs:79` ALSO calls
  `GeneratedEffects.RegisterAll(r)` directly — a second, independent first-party consumer of the
  WorldGraph-only output (gen-time history simulation, distinct from live `UndertowSim`) — must be
  verified byte-identical too, not just `UndertowSim.cs`. Six test files consume the generated output or
  test the mechanism directly (`EffectsTests.cs`, `GeneratedEffectsTests.cs`, `PairEffectsTests.cs`,
  `HookEffectsTests.cs`, `InterSystemTests.cs`, `EmitRegisterEffectsTests.cs`,
  `CodeModLoaderTests.cs`/`Fixture.cs`).

## Scope boundary
- **IS:** Task 1 confirms/refines the ground truth above (exact site count per ctor-shape, confirm
  `HistorySim.cs`'s second call site). Build a new `--effect-cs` CLI discovery path extending Increment
  4's `[Action]`-style mechanism (`CompilationLoader.LoadEffectClasses`, discover-by-syntax-name,
  `sym.AllInterfaces` + `sym.InstanceConstructors` resolution for the dual ctor-shape discriminator) +
  a new emitter porting BOTH `EmitRegisterEffects.Emit`/`EmitRegisterSimEffects.Emit`'s logic
  line-for-line, including both `UTFX002`/`UTFX003` diagnostic paths. Prove equivalence for all real
  sites in BOTH ctor-shape buckets, against BOTH generated output files, against BOTH real call sites
  (`UndertowSim.cs` AND `HistorySim.cs`). Retire `EffectRegistrationGenerator.cs`/`EmitRegisterEffects.cs`
  if safe — full build + full test-suite pass. Re-verify `CodeModLoader.cs`'s Effect-handling section
  (including its own dual-ctor-shape `InstantiateEffect` mirror) is untouched.
- **IS NOT:** touching `CodeModLoader.cs`'s own logic, touching the 48 real Effect classes' behavior, or
  designing a generalized "dual ctor-shape dispatch" abstraction beyond what this feature needs.

## Tasks

### Task 1 — Ground the shape + decide mechanism (READ + REPORT before building)
- Read `EffectAttribute.cs`, `EffectParamAttribute`, `EffectRegistrationGenerator.cs`,
  `EmitRegisterEffects.cs` (both `Emit`/`EmitSim`-equivalent classes), `CodeModLoader.cs`'s
  Effect-handling section (including `InstantiateEffect`) fresh in full.
- Recount real `[Effect(...)]` sites in each ctor-shape bucket (parameterless vs. sim-ctor) — confirm
  the 16/32 split and list every file.
- Confirm `HistorySim.cs:79`'s call site is real and must be included in the equivalence proof.
- **Decide and REPORT**: confirm extending Increment 4's dispatch-table-builder mechanism with the
  added `HasSimCtor` discriminator + two emit templates (expected outcome) — or report a different
  finding if the dual-shape branch turns out to need a genuinely different mechanism.
- Confirm retirement safety: `CodeModLoader.cs`'s Effect-handling block (including its own ctor-shape
  fallback logic) is self-contained and does not call the generator/emitter being retired.

### Task 2 — Build + equivalence proof + retire (if safe)
- Implement per Task 1's decision: `--effect-cs` CLI branch, `CompilationLoader.LoadEffectClasses`,
  and an emitter porting both `EmitRegisterEffects.Emit`/`EmitRegisterSimEffects.Emit` line-for-line —
  including the `new {Fq}()` vs `new {Fq}(this)` construction-shape branch, both diagnostic paths
  (`UTFX002`/`UTFX003`), and `EffectParamAttribute`'s repeatable-attribute param-schema ordering.
- Prove equivalence: same generated `GeneratedEffects.RegisterAll`/`RegisterGeneratedSimEffects()` method
  bodies for the real 16+32 sites, byte-diffed against the real Roslyn generator's actual build output
  for BOTH generated files. Exercise both diagnostic paths with deliberately-malformed synthetic
  scenarios (parameterless class in the sim-ctor bucket and vice versa). Confirm `HistorySim.cs:79`'s
  call site behavior is preserved (not just `UndertowSim.cs`'s).
- Retire `EffectRegistrationGenerator.cs`/`EmitRegisterEffects.cs` if safe. Full `dotnet build` + full
  `dotnet test` on `core/Undertow.sln`, 0 errors/failures required. Re-verify `CodeModLoader.cs` is
  byte-identical pre/post retirement.

## Gates / guardrails
- Non-vacuous proof: real multi-site data across BOTH ctor-shape buckets, both diagnostic paths
  exercised with deliberately-malformed scenarios, both real call sites (`UndertowSim.cs` +
  `HistorySim.cs`) verified.
- rtk masks git exit codes — use `/usr/bin/git` for evidence.
- Isolated undertow worktree (fresh, off `master`) — `.claude/worktrees/codegen-unif-inc7-effect`,
  branch `feat/codegen-unif-inc7-effect`. Do not touch the main checkout or any other worktree. Do NOT
  push. Commit as work completes.
- Yeroket-side work branches off Increment 6's tip (`feat/codegen-unif-inc6-factories`) as
  `feat/codegen-unif-inc7-effect`, continuing the single sequential lineage.
- If retiring: full `dotnet build` + full `dotnet test` on undertow's `core/Undertow.sln`, 0
  errors/failures required. `CodeModLoader.cs` byte-identical pre/post retirement diff.
- Watch for the `SDFNodeGenerator.dll` non-deterministic-rebuild gotcha in Yeroket.

## Milestone Map
- [ ] **Milestone 1 (Task 1):** ground the shape, decide mechanism (report-back gate). One Sonnet
  implementer + one Opus validator.
- [ ] **Milestone 2 (Task 2):** build + equivalence proof + retire. One Sonnet implementer + one Opus
  validator.

## Progress Log

(none yet)
