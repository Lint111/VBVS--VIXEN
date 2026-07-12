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
- [ ] **Milestone 1 (Task 1):** ground the shape, decide mechanism (report-back gate). One Sonnet
  implementer + one Opus validator.
- [ ] **Milestone 2 (Task 2):** build + equivalence proof + retire (if safe). One Sonnet implementer + one
  Opus validator.

## Progress Log

*(none yet — plan authored, not yet dispatched)*
