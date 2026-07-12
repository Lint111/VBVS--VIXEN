# Undertow Codegen Unification — Increment 4: `[Action]` Registration (2026-07-13)

**Program:** `Undertow-Codegen-Unification-Program-2026-07.md`, Increment 4 (feature #12 in the survey).
**Why fourth:** first of the four "kernel" attribute-driven features (#12 `[Action]`, #13 `[Effect]`,
#14 `[Param]` done, #15 `[System]`) tackled after `[Param]`; chosen next specifically to test whether
`[KernelCallable]`'s existing method-body-transplant pipeline extends to a logic-bearing, class-targeted
attribute BEFORE Increment 7 (`[Effect]`) stresses it further with a dual ctor-shape branch.

## Ground truth (read fresh 2026-07-13 by a research agent — verify file:line if moved)

- **`ActionAttribute`** (`core/src/Undertow.Sim/Actions/ActionAttribute.cs:6-31`) —
  `[AttributeUsage(AttributeTargets.Class, AllowMultiple=false, Inherited=false)]`. Ctor `(string id)`;
  named properties: `Trigger` (enum `Chosen`|`RuleFired`, default `Chosen`), `Name` (string?, defaults to
  id's local part), `TieBreak` (byte), `Capability` (string? prefix), `AlwaysAvailable` (bool),
  `Unlocked` (bool, default true), `BaseWeight` (double). Attribute carries SCALAR METADATA ONLY — the
  actual behavior lives in four separate marker interfaces the decorated class may implement: `IAction`
  (`Execute`), `IActionGate` (`Available`), `IActionDecompose` (`Decompose`), `IActionLifecycle`
  (`Advance`).
- **`ActionRegistrationGenerator.cs`** — Roslyn `IIncrementalGenerator`,
  `ForAttributeWithMetadataName` on `Undertow.Sim.Actions.ActionAttribute`, guarded to fire only in the
  `Undertow.Sim` assembly. Builds an `ActionDecl` per decorated class: scalar metadata + which of the 4
  marker interfaces the class implements (via `sym.AllInterfaces`).
- **`EmitRegisterActions.cs`** — emits `partial void RegisterGeneratedActions()` on `partial class
  UndertowSim`: per decl, `new`s the class, declares a `can:<name>` faction `Param` (seeded by
  `Unlocked`), builds an `ActionDescriptor` wiring `Execute`/`Available`/`Decompose`/`Advance` delegates
  ONLY for interfaces actually implemented, wraps `Available` to gate on the capability flag first.
  Validation: duplicate id; "no behavior interface" (must implement `IAction` or `IActionLifecycle`);
  `Chosen` trigger requires `IAction`; `RuleFired` trigger requires `IActionLifecycle` (diagnostic
  `UTACT001`).
- **Real usage: 5 production classes** — `StructuresAction.cs`, `MoveHaulAction.cs`,
  `ExtractionAction.cs`, `RefineAction.cs` (all `core/src/Undertow.Sim/Systems/Economy/`),
  `ResearchAction`/`EvictAction` (both in `core/src/Undertow.Sim/Actions/Builtins/Builtins.cs`) — 6
  classes total (survey said 5; recount in Task 1). Plus 2 test-only exercises of the contract:
  `core/tests/Undertow.Tests.CodeModFixture/Fixture.cs`,
  `core/tests/Undertow.Core.Tests/Sim/CodeModLoaderTests.cs`.
- **Critical safety constraint — same class as Increment 3's `CodeModLoader.cs` finding, confirmed
  real.** `core/src/Undertow.Sim/CodeModLoader.cs:122-157` is a SECOND, INDEPENDENT, hand-written mirror
  of the exact same registration logic for mod-loaded assemblies (reflection-based
  `TypesWith<ActionAttribute>`, same interface pattern-matching, same capability-flag wiring). Its own
  comment (line 122): *"Mirror ActionRegistrationGenerator/EmitRegisterActions EXACTLY."* Both this file
  AND the attribute's exact shape/validation semantics must stay untouched or kept in lockstep —
  Increment 4 is in scope to VERIFY this file still matches after the port, not to touch its logic.
- **Key structural difference from `[KernelCallable]` — the survey's hypothesis is REFUTED.**
  `[KernelCallable]` (`Runtime/KernelCallableAttribute.cs`, `AttributeTargets.Method`) marks static
  methods whose BODIES are transpiled C#→HLSL/C++ by `CppAstVisitor`/`CppEmitter` — a source-to-source
  body translation. `[Action]` targets CLASSES and its generator never transpiles a method body at all —
  it emits object construction + delegate assignment into a registration/dispatch table
  (`Actions.Register(new ActionDescriptor {...})`), calling UNMODIFIED hand-written C# via direct
  delegate references (`d.Execute = exec.Execute`). These are different codegen KINDS: `[KernelCallable]`
  is a method-body transpiler; `[Action]`'s generator is a class-metadata + interface-presence dispatch
  table builder. No transpilation applies here at all — confirm this in Task 1 before building anything.

## Scope boundary
- **IS:** Task 1 confirms/refines the ground truth above (especially the exact usage-site count and the
  `CodeModLoader.cs` mirror's current content). **CORRECTED mechanism (per Milestone 1's finding):**
  `Undertow.Sim.ActionAttribute` is ITSELF a live `CodeModLoader.cs` runtime-reflection target
  (`TypesWith<ActionAttribute>` at lines 126/128/352/354, for code-mod support) — structurally identical
  to Increment 3's `ParamAttribute` situation, NOT Increment 1/2's "introduce a new Yeroket-owned
  attribute" situation. Build a new `--action-cs` CLI discovery path (extending `--param-cs`'s
  `CompilationLoader.LoadParamFields` discover-by-syntax-name pattern, matching `"Action"`/
  `"ActionAttribute"`, resolving each decorated class's `sym.AllInterfaces` for the 4 marker interfaces)
  + a new emitter porting `EmitRegisterActions.Emit`'s logic line-for-line, including the exact
  validation/diagnostic behavior. Reuse `EnumExprHelper` (already ported in Inc-3) for the `Trigger`
  enum property. Prove equivalence against the real 6 (or actual count) sites,
  including the diagnostic paths (duplicate id, no-behavior-interface, trigger/interface mismatch).
  Retire `ActionRegistrationGenerator.cs`/`EmitRegisterActions.cs` in undertow if safe (full build + full
  test-suite pass, mirroring Increments 2/3). Explicitly re-verify `CodeModLoader.cs`'s mirror logic is
  untouched and still consistent with the (unchanged) attribute/interface contract.
- **IS NOT:** touching `CodeModLoader.cs`'s own logic, touching the 6 real Action classes' behavior, or
  designing a generalized "registration table" mechanism for future features (#13 `[Effect]`, #15
  `[System]`) beyond what this feature actually needs — reuse only what's real fit, per the program's
  "prefer merge into pre-existing logical path" rule; don't over-generalize preemptively.

## Tasks

### Task 1 — Ground the shape + decide mechanism (READ + REPORT before building)
- Read `ActionAttribute.cs`, `ActionRegistrationGenerator.cs`, `EmitRegisterActions.cs`,
  `CodeModLoader.cs`'s Action-mirror section fresh in full.
- Recount real `[Action(...)]` sites in `Undertow.Sim` (confirm 6 vs. survey's 5 — list every file).
- Confirm the method-vs-class mismatch with `[KernelCallable]` holds (it should, per the ground truth
  above) — if Task 1 finds otherwise, report and stop before building.
- **Decide and REPORT**: new small class-targeted attribute + new emitter (expected outcome, mirroring
  Increment 3's "genuinely new mechanism" pattern), or some other genuine reuse if discovered. Justify
  against real code, not the survey's summary.
- Confirm retirement safety: re-verify `CodeModLoader.cs`'s Action-handling section is self-contained
  (does not call into the generator/emitter being retired — it's a hand-written parallel mirror, not a
  dependent) and identify every other call site of `RegisterGeneratedActions()`.

### Task 2 — Build + equivalence proof + retire (if safe)
- Implement per Task 1's CORRECTED decision: a new `--action-cs` CLI branch +
  `CompilationLoader.LoadActionClasses` (discover-by-syntax-name, matching `"Action"`/`"ActionAttribute"`
  the same way `LoadParamFields`/`LoadKernelCallables` do — NOT a new Yeroket-owned attribute type,
  since `Undertow.Sim.ActionAttribute` is itself a `CodeModLoader.cs` runtime-reflection target), with
  `sym.AllInterfaces` resolution for the 4 marker interfaces, and an emitter porting
  `EmitRegisterActions.Emit` line-for-line (interface-gated delegate wiring, capability-flag gating,
  `can:<name>` Param declaration, all 4 diagnostic paths, `EnumExprHelper` reuse for `Trigger`).
- Prove equivalence: same generated `RegisterGeneratedActions()` body for the real 6 (or actual count)
  sites, AND exercise all 4 diagnostic paths explicitly with deliberately-malformed test scenarios (not
  just the happy path) — duplicate id, no behavior interface, `Chosen` without `IAction`, `RuleFired`
  without `IActionLifecycle`.
- Retire `ActionRegistrationGenerator.cs`/`EmitRegisterActions.cs` if Task 1 confirmed safety — full
  `dotnet build` + full `dotnet test` on `core/Undertow.sln`, 0 errors/failures required. Re-verify
  `CodeModLoader.cs` is byte-for-byte untouched by the retirement diff (same discipline as Increment 3).

## Gates / guardrails
- Non-vacuous proof: real multi-site data, all 4 diagnostic paths exercised with deliberately-malformed
  scenarios, not just the clean case.
- rtk masks git exit codes — use `/usr/bin/git` for evidence.
- Isolated undertow worktree (fresh, off `master`) — `.claude/worktrees/codegen-unif-inc4-action`,
  branch `feat/codegen-unif-inc4-action`. Do not touch the main checkout or any other worktree. Do NOT
  push. Commit as work completes.
- Yeroket-side work branches off Increment 3's tip (`feat/codegen-unif-inc3-param`, HEAD `91a297d6`) as
  `feat/codegen-unif-inc4-action`, continuing the single sequential lineage (none of these are merged to
  Yeroket main yet).
- If retiring: full `dotnet build` + full `dotnet test` on undertow's `core/Undertow.sln`, 0
  errors/failures required before calling retirement done. `CodeModLoader.cs` must be byte-identical
  pre/post retirement diff.
- Watch for the `SDFNodeGenerator.dll` non-deterministic-rebuild gotcha in Yeroket — never commit it
  unless a real source change to that generator was made; discard via
  `git checkout -- Packages/com.yeroket.utility.kernel-framework/RoslynAnalyzers/SDFNodeGenerator.dll`.

## Milestone Map
- [x] **Milestone 1 (Task 1):** ground the shape, decide mechanism (report-back gate). One Sonnet
  implementer + one Opus validator.
- [ ] **Milestone 2 (Task 2):** build + equivalence proof + retire. One Sonnet implementer + one Opus
  validator.

## Progress Log

- Milestone 1 (Task 1, research-only): DONE · 2026-07-13 · no files modified in either repo
  - **Confirmed: 6 real production `[Action(...)]` sites** — `StructuresAction.cs`, `MoveHaulAction.cs`,
    `ExtractionAction.cs`, `RefineAction.cs` (all `Systems/Economy/`), `ResearchAction`+`EvictAction`
    (both `Actions/Builtins/Builtins.cs`) — matches this plan's ground truth (survey's "5" undercounted).
    Plus 2 test-fixture classes in `CodeModFixture/Fixture.cs`.
  - **Confirmed: `[KernelCallable]` hypothesis REFUTED, no reconciliation** — `KernelCallableAttribute`
    strictly `AttributeTargets.Method` (line 79); `ActionAttribute` strictly `AttributeTargets.Class`
    (line 11). Different codegen kinds (body-transpiler vs. delegate-wiring dispatch table).
  - **CORRECTION to this plan's original Task 2 framing, applied above**: `Undertow.Sim.ActionAttribute`
    is itself a live `CodeModLoader.cs` runtime-reflection target (`TypesWith<ActionAttribute>` /
    `GetCustomAttribute<ActionAttribute>()` at lines 126/128/352/354, plus a benign 5th at 414) for
    code-mod support — structurally identical to Increment 3's `ParamAttribute` finding, NOT Increment
    1/2's "introduce new attribute" case. Mechanism corrected to: new `--action-cs` CLI branch extending
    `--param-cs`'s `CompilationLoader.LoadParamFields` discover-by-syntax-name pattern (matching
    `"Action"`/`"ActionAttribute"`), `sym.AllInterfaces` resolution for the 4 marker interfaces, reusing
    `EnumExprHelper` for the `Trigger` enum.
  - **Retirement safety: SAFE.** `CodeModLoader.cs`'s Action-handling block is a fully self-contained
    hand-written mirror (own comment: "Mirror ActionRegistrationGenerator/EmitRegisterActions EXACTLY"),
    never calls the generator/emitter being retired. Sole real call site of `RegisterGeneratedActions()`:
    `UndertowSim.cs:460` (declared `partial void` at line 436).
  - No blockers.
  - **Opus validator (independent re-verification):** APPROVED. Confirmed all 4 claims against real
    source, including independently checking Yeroket's `--param-cs`/`LoadParamFields` analog is real and
    extendable (not hand-waved). No scope violations — both worktrees clean, no Inc-4 implementation
    commits yet. Cleared to proceed to Milestone 2.
