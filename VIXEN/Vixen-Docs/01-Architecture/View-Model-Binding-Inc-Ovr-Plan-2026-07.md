# View↔Model Binding — Inc-Ovr Plan (2026-07-12)

**Program design:** `View-Model-Binding-Framework-Design-2026-07.md` §5a (identity vs. projection
binding), §5b (override — the escape hatch above projection), §10 item 2 (Inc-Ovr), §12 critic item 7
(projection/override syntax flagged as under-designed, not a hand-wave).
**Prior:** Inc-A (`224e393f`) through Inc-D (`4f397deb`, this session) are ALL DONE — the write path,
model→view reconcile, multi-instance selection, and set-mutation+undo are proven. Inc-Ovr is the LAST
gap in the core binding mechanism itself before Inc-E (authoring tooling).

## Goal

Give the schema a real **projection** and **override** declaration syntax, with a matching codegen
emitter, so that Inc-A's `mask_`→N-checkboxes transform (currently 100% hand-written, one-off, called
from three separate sites) becomes the FIRST instance of a **generalizable** mechanism — not a
generalization for its own sake, but because §5a's whole value proposition ("declare views/actions/
bindings once; the framework generates all the wiring; you supply only the pure projection") is
currently **unproven**: today there is no schema-level concept of "this binding is a projection," so
there is nothing for a second, different projection (a future binding) to reuse. Inc-Ovr proves the
mechanism is real by re-deriving the SAME mask_→checkboxes behavior through it, byte/behavior-identical
to what's already shipped, then removing the hand-written duplicate.

## Ground truth this plan is scoped against (verified by direct code research 2026-07-12 — do not
re-derive these facts, but DO re-verify file:line if code has moved)

- **`[View]`/`[ViewSection]` today** (Yeroket kernel-framework, `Runtime/GpuStructAttributes.cs:8-40`):
  `[View]` is an empty marker attribute (no properties at all). `[ViewSection(Layout=Aos|Soa)]` has
  exactly one property (`Layout`; only `Aos` emit is implemented). **There is no attribute parameter,
  partial-method hook, virtual/override point, or hook facility of ANY kind** in the emitters that
  produce `[View]` output (`SourceGenerator~/Transpiler/RmlDataModelEmitter.cs` and siblings) — confirmed
  by reading the emitter source directly, not inferred. Every Family-A data emitter is a pure
  `static string Emit(Model)` reflection-to-text function with zero injection seam.
- **The `mask_`→checkboxes "projection" is 100% hand-written today, in THREE places, not one:**
  - `EditorLayersView::PopulateFromMask` (`application/editor/include/EditorLayersView.h:39-52`) — the
    actual bit-decomposition loop: `row.isChecked = ((mask >> i) & 1u) != 0u`.
  - `EditorApplication.cpp:232` calls the codegen-generated `Vixen::AppFlow::Generated::applyToggle(mask,
    idx)` (from `[KernelCallable]` `AppFlowCallables.cs:13`, emitted to `AppFlowCallables.g.hpp:7-9`) —
    this is the ONE piece that IS already transplanted/generated, but it's the single-bit-flip inverse
    projection (view→model), not the fan-out (model→view) — the two directions of the SAME logical
    projection currently live in different places, one generated, one hand-written.
  - Three call sites manually assemble the pieces: `EditorApplication.cpp:145`, `:194`, `:249` each
    hand-compose `provider.ReadU32` → (nothing, or `applyToggle`) → `provider.WriteU32` →
    `PopulateFromMask`. There is no single "binding" object that owns this; it is glue code written three
    times.
  - **This is the concrete thing Inc-Ovr re-derives through a real mechanism** — not a new feature, a
    generalization of an already-shipped, already-tested behavior.
- **`IViewDataProvider`** (`libraries/AppFlow/include/IViewDataProvider.h:15-31`) operates purely on raw
  `uint32_t` values keyed by a **hand-declared** `ViewNounId` enum (the header's own comment at line 14:
  "a future codegen pass emits this enum from the [View] schema" — i.e. this is already a known,
  documented gap Inc-Ovr's schema work can close as a side effect, not a new ask). The provider seam has
  ZERO concept of "projection" — it is a value pipe; the transform happens entirely above it.
- **`[KernelCallable]`** (`Runtime/KernelCallableAttribute.cs:80`) and its CLI `--callable-cpp` path are
  REAL and shipping today (not just design-doc vapor) — this is the mechanism §5a says a projection's
  hand-written transform body should be expressed through ("a `[KernelCallable]`-style function, so it
  can itself be transplanted"). Inc-Ovr's projection syntax should PRODUCE a `[KernelCallable]`-shaped
  function for the transform body, reusing this already-proven pipeline, not inventing a parallel one.

## Scope boundary (what Inc-Ovr IS and is NOT)

- **IS:** a new attribute surface on the schema (either new properties on `[ViewSection]`/a per-field
  attribute, or a new sibling attribute — Task 1 decides and justifies) that lets a schema author declare
  a field/binding as a **Projection** (§5a: bound value ↔ view value differ by a transform, transform body
  supplied as a `[KernelCallable]`-shaped function, wiring 100% generated) or an **Override** (§5b: the
  binding's entire generated handler/reconcile is replaced by a consumer-supplied one — the schema still
  DECLARES the binding, so codegen still emits the hook point, but the framework does not generate the
  read/write/reconcile logic itself). A matching emitter change that reads the new attribute and emits the
  transform-invocation wiring (calling the projection's `[KernelCallable]` function on read/write) instead
  of a flat 1:1 field bind.
- **PROOF vehicle:** re-derive `EditorLayersView::PopulateFromMask` + the `applyToggle` single-bit-flip
  pairing through the new Projection mechanism — declare `LayerMask`'s bit-decomposition as a schema-level
  projection (view value = `bool[N]` derived from model value `uint32_t` via a bit-index transform),
  generate the wiring, and confirm the generated behavior is **byte/behavior-identical** to what
  `PopulateFromMask` + `applyToggle` + the three hand-assembly call sites already do today (same RmlUi
  bind shape, same checkbox states for the same mask, same toggle-back behavior) — THEN delete the
  hand-written `PopulateFromMask` fan-out loop (keep `applyToggle` if the projection mechanism naturally
  reuses it, or regenerate an equivalent — Task 1/2 decide) and the three hand-assembly call sites'
  manual wiring, replacing them with whatever the new mechanism's call surface looks like. This is the
  test that the mechanism is REAL, not just schema decoration with no teeth: a real, already-shipped
  behavior gets re-implemented through it and the hand-written duplicate is retired, not left alongside.
- **Override proof vehicle:** a smaller, separate proof — declare ONE existing or new trivial binding as
  `Override` and confirm codegen emits a hook point (NOT generated read/write/reconcile logic) that a
  hand-written function correctly fills, and that build fails clearly (a missing-symbol link error, not a
  silent no-op) if the override function isn't supplied — proving `Override` is a REAL escape hatch with
  a real contract, not just an attribute that does nothing. Does not need to be the mask/checkbox case;
  pick whichever existing binding makes the smallest, clearest proof (Task 1 decides, report which).
- **IS NOT:** a rich declarative projection mini-language (bit/enum/clamp/format kinds expressed AS
  schema syntax, codegen inventing the transform) — the design doc §12's final resolution log is explicit
  that this was decided AGAINST: "NOT a richer declarative projection mini-language... chosen for the
  simplest schema; a declarative projection layer is a possible far-future increment IF hand-authored
  projections prove repetitive, but it is NOT in this program's scope." Inc-Ovr's projection mechanism
  wires an author-supplied `[KernelCallable]` transform function into the generated read/write/reconcile
  path — it does NOT generate the transform body itself, ever. Do not build a bit-decomposition DSL; build
  the wiring that calls a hand-written bit-decomposition function.
- **IS NOT:** the `ViewNounId` codegen gap noted above (auto-generating the noun enum from the schema) —
  real, documented, but a SEPARATE concern from projection/override syntax; note as a Follow-up if Task 1
  finds it's cheap to fold in, but do not treat it as required scope.
- **IS NOT:** retrofitting projection/override onto Inc-B/C/D's Gaia-backed providers or the
  `IViewSelectionProvider`/set-mutation machinery — those are unaffected; Inc-Ovr's proof vehicle is the
  editor's direct-field `LayerControllerViewDataProvider` path (Inc-A's original, simplest provider),
  matching where the `mask_` projection actually lives today. If proving it against the Gaia-backed path
  turns out to be trivial once the direct-field proof works, that's a bonus, not a requirement.
- **Reuse, don't reinvent:** `[KernelCallable]`'s existing CLI path (`--callable-cpp`) for the projection
  transform body — do not invent a second transplant mechanism. `IViewDataProvider`'s existing
  `ReadU32`/`WriteU32` seam for the underlying value pipe — projection wraps it, does not replace it.

## Tasks

### Task 1 — Ground the shape (READ + REPORT before building)

- Read the Yeroket kernel-framework attribute definitions in full
  (`/home/liory/Github/Yeroket-Fantasy/Packages/com.yeroket.utility.kernel-framework/Runtime/GpuStructAttributes.cs`,
  `KernelCallableAttribute.cs`) and at least one full emitter (`RmlDataModelEmitter.cs` or equivalent
  under `SourceGenerator~/Transpiler/`) to confirm the "zero hook points today" finding still holds and to
  understand the emitter's actual code-generation pattern (so Inc-Ovr's new emitter path matches its
  style, not a foreign one).
- Read `EditorLayersView.h`, `LayerControllerViewDataProvider.h`, `AppFlowCallables.cs`/`.g.hpp`,
  `EditorApplication.cpp`'s three call sites (`:145`, `:194`, `:249`) in full — this is the exact behavior
  Inc-Ovr's Projection proof must reproduce.
- **Decide and REPORT the attribute-surface shape**: new properties on `[ViewSection]` (e.g.
  `Projection = typeof(SomeTransformClass)`), a new per-field attribute (e.g. `[Projected(...)]` on the
  bound field), or a new sibling attribute family alongside `[View]`/`[Flow*]` — pick the shape that fits
  the kernel-framework's existing reflection-based emitter pattern with the LEAST structural change, and
  justify against the real emitter code you just read, not in the abstract.
- **Decide and REPORT what codegen emits for a Projection-declared binding**: does it emit a call to a
  `[KernelCallable]`-generated function inline in the `Bind*Model` wiring, or does it emit a named hook
  function signature the schema's C# side must separately mark `[KernelCallable]` and the C++ side
  implements? Trace through how `[KernelCallable]` already gets from C# declaration to `AppFlowCallables.g.hpp`
  and reuse that exact mechanism rather than inventing a variant.
- **Decide and REPORT what codegen emits for an Override-declared binding**: the plan's expectation is a
  hook point (e.g. a forward-declared function signature with no generated body, or an `extern`
  declaration) that fails to LINK (not silently no-ops) if unimplemented — confirm this is achievable with
  the kernel-framework's actual C++ emission style, and pick the smallest concrete binding to prove it
  against (report your choice).
- Confirm the `ViewNounId` hand-declared-enum gap (`IViewDataProvider.h:14`) — decide whether folding its
  auto-generation into Inc-Ovr's schema work is trivial (do it) or adds real scope (defer as a Follow-up,
  document why).

### Task 2 — Projection mechanism: schema + emitter + `[KernelCallable]` wiring

- Implement the attribute-surface decision from Task 1 in the (cross-repo, Yeroket) kernel-framework
  attributes + emitter.
- Implement the codegen emitter change: for a Projection-declared binding, emit the generated
  `Bind*Model`/handler wiring to call the projection's transform function (via the existing
  `[KernelCallable]` pipeline) instead of a flat 1:1 bind.
- This task necessarily touches BOTH repos (Yeroket kernel-framework for the attribute/emitter, VIXEN for
  the schema declaration + consuming the regenerated output) — follow whatever cross-repo workflow prior
  increments in this program used (check `kernel-framework-skill-and-consolidation` memory / the
  `kernel-codegen-framework-direction` topic file for the established pattern before improvising one).

### Task 3 — Re-derive the `mask_`→checkboxes projection through the new mechanism (the real proof)

- Declare `LayerMask`'s bit-decomposition as a schema-level Projection using Task 2's mechanism.
- Regenerate and confirm the generated wiring produces IDENTICAL behavior to today's hand-written
  `PopulateFromMask` + `applyToggle` + the three call sites — same checkbox states for the same mask
  value, same toggle-back result, same RmlUi bind shape (live-gate: editor renders identically, checkbox
  clicks still toggle the right layer).
- **Delete the hand-written duplicate**: remove `PopulateFromMask`'s hand fan-out loop and the three call
  sites' manual assembly, replacing them with the new mechanism's generated call surface. This is not
  optional cleanup — it is the actual proof that the mechanism replaced hand-written glue rather than
  living alongside it as unused scaffolding.

### Task 4 — Override mechanism: schema + emitter + link-fails-if-unimplemented proof

- Implement the Override attribute-surface decision from Task 1.
- Prove it against the smallest binding Task 1 selected: declare it `Override`, confirm codegen emits a
  hook point with NO generated logic, confirm a correct hand-written implementation satisfies it and
  produces correct behavior, and confirm (as a negative test) that OMITTING the implementation fails the
  BUILD with a clear link/compile error — not a silent no-op, not a runtime crash, a build-time failure
  that tells the schema author they forgot to implement their declared override.

### Task 5 — Regression gates + report

- All of Inc-A/A2/B/C/D's named regression gates still hold (the live editor render/checkbox gate
  especially, since Task 3 directly touches that code path) — rebuild fresh, re-run, report exact counts
  against the baselines already recorded in the Inc-C/Inc-D plan docs' Progress Logs.
- Report the codegen drift-guard status (`test_octree_config_sdi_parity`-style parity checks, if this
  program's prior increments established any for `[View]`/`[Flow*]` output — check and confirm none
  broke).

## Gates / guardrails

- Projection mechanism must genuinely wire an AUTHOR-SUPPLIED transform, never generate the transform
  body itself — re-read §12's final resolution log before building if in doubt; a declarative
  bit/enum/clamp mini-language is explicitly OUT of scope, decided against by the user.
- The re-derivation in Task 3 must be a like-for-like behavioral match with the CURRENT shipped
  `PopulateFromMask`/`applyToggle` pair, verified live (editor renders, checkboxes toggle correctly), not
  just unit-tested in isolation.
- Task 3's hand-written duplicate MUST be deleted, not left dormant alongside the new mechanism.
- Override's "fails the build if unimplemented" contract must be demonstrated as a real negative test
  (temporarily omit the implementation, confirm the build breaks, then restore it) — not asserted from
  reading the emitter code alone.
- This program's robin_hood ODR landmine (no TU includes both gaia.h and RmlUi data-model headers) still
  applies to any new bridge file this increment needs.
- rtk masks git exit codes — use `/usr/bin/git`, `sha256sum`, `cmp` for evidence. Poll long builds
  actively (~20s foreground loop per `vixen-build-policy`), never overlap builds of one target.
- Cross-repo work (Yeroket + VIXEN) needs its own worktree-pairing discipline — check how prior
  cross-repo increments (config-struct-codegen epic, kernel-unification program) handled this before
  improvising; do not commit half of a cross-repo change to only one repo without a clear plan for landing
  both sides coherently.
- Commit in the worktree(s) (pre-blessed per this program's convention). Do NOT push without explicit
  confirmation the cross-repo halves are both ready.

## Milestone Map

- [ ] **Milestone 1 (Task 1):** ground the attribute-surface + emitter-shape decisions for BOTH
  Projection and Override (report-back gate, no building until confirmed). One Sonnet implementer + one
  Opus validator.
- [ ] **Milestone 2 (Task 2-3):** build the Projection mechanism, re-derive `mask_`→checkboxes through it,
  delete the hand-written duplicate. One Sonnet implementer + one Opus validator.
- [ ] **Milestone 3 (Task 4-5):** build the Override mechanism + its link-fails-if-unimplemented proof,
  run all regression gates, final report. One Sonnet implementer + one Opus validator.

## Progress Log

*(none yet — plan authored, not yet dispatched)*

## Follow-ups (explicitly out of scope, note for later increments)

- `ViewNounId`'s hand-declared-enum gap (`IViewDataProvider.h:14`) — auto-generate from the schema, unless
  Task 1 finds it's cheap enough to fold into Inc-Ovr's own schema work.
- A declarative projection mini-language (bit/enum/clamp/format kinds as schema syntax) — explicitly
  decided against for this program (§12 final resolution log); revisit only if hand-authored projections
  prove genuinely repetitive across many future bindings.
- Inc-E — authoring tooling / UX API + non-exponential lint guards — the increment after this one, per
  §10 item 6, "built once the surface exists." Inc-Ovr's projection/override syntax is itself part of
  "the surface" Inc-E's tooling would inspect/lint.
- Retrofitting Projection/Override onto the Gaia-backed provider path (Inc-B/C/D) or the set-mutation
  machinery, if Inc-Ovr's direct-field proof turns out to generalize easily — not required, note if
  observed as trivially true.

## Note

Inc-Ovr closes the last open gap in the core binding mechanism (§10 items 1-5 all done after this) before
Inc-E's tooling. Its central discipline, per every prior increment in this program: the proof is not "the
schema compiles" — it is "an already-shipped, hand-written behavior gets re-implemented through the new
mechanism and the hand-written version is deleted." A projection/override mechanism nobody's real code
uses is scaffolding, not a shipped capability.
