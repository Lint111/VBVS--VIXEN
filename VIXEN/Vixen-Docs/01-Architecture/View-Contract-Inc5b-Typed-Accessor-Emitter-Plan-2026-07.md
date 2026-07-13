# View Contract Inc-5b — Typed C++ Accessor Emitter (2026-07-13)

**Program:** `Renderer-Agnostic-View-Contract-Design-2026-07.md` §6.5. **Directly follows:**
`View-Contract-Inc5-Undertow-Migration-Plan-2026-07.md` (Milestones 1-3 DONE: SoA wire emit, a
SoA-aware C++ reader, undertow's real schema declared + byte-identical proof for 4-of-5 sections +
Bodies' 7-of-10 columns). Milestone 4 of that plan was BLOCKED on Gap #3: undertow's real hand-rolled
reader (`view_contract.h`) does raw byte-offset arithmetic hard-wired to the OLD `UTVW` wire's exact
shape (per-section/per-column TOC, 16-byte alignment), while Yeroket's Milestone-2 SoA writer produces
a structurally different wire (`UTVA`, no TOC, positional fields) — swapping the writer alone would
make `main.cpp` decode garbage from real sim frames. User decision (2026-07-13): unblock via path (a)
from that plan's own Follow-ups — build a NEW typed-C++-accessor emitter generating `view_contract.h`-
equivalent code from the generic `ViewBlob`/`BlobView`/`ViewStore` model, so `main.cpp`'s call sites
barely change while the reader becomes generated, not hand-rolled, and matches the new wire.

## Ground truth (read fresh 2026-07-13 by a research agent — verify file:line if moved)

- **`view_contract.h`'s real accessor shape** (`undertow/vixen/render/view_contract.h`, 316 lines) —
  5 section classes (`BodiesSection`/`HudSection`/`HudFactionsSection`/`HudEventsSection`/
  `HudInspectSection`). Each: ctor `Section(const SectionView& s, const uint8_t* buf_end)` caching
  every column's `const uint8_t* ptr + size_t len` via `s_.column(kColumnId, end_, len_out)`; a
  `count()` returning row count; one bounds-checked typed accessor per column (`T field(uint32_t i)
  const`). Variable-length columns (`Str`/`ListVec3f`) do offsets-array-then-blob arithmetic. This IS
  undertow's real `UTVW` wire's exact layout — a generated replacement must reproduce the shape
  (ctor-caches-columns / one-typed-method-per-column / `count()`), not the byte-level reading logic,
  since call sites depend on this API surface, not the internal implementation.
- **`main.cpp`'s real call-site shape** (`undertow/vixen/app/src/main.cpp:38,183-198`) — narrow and
  mechanical: `#include "view_contract.h"`, `SectionView sec; wire.section(kSectionBodies, sec);
  BodiesSection bs(sec, wire.end());` then a loop calling `bs.count()`/`bs.position(i)`/`bs.mass(i)`/
  etc. Only `Bodies` is consumed here today; other sections exist in the header but this call site
  doesn't touch them (other files may — Task 1 of Milestone B must find them). Any generated
  replacement need only preserve: construct-from-buffer, `.count()`, named per-column typed getters
  with the SAME names/return types — this is what makes call-site churn minimal.
- **Substrate already exists — `ViewBlob`/`BlobView`/`ViewStore`/`ViewWireReaderSoa`** (VIXEN
  `libraries/RenderGraph/include/Ui/`): `ViewBlob.h` is a pure reflection description (name/kind/elem,
  no offsets). `ViewStore.h` holds runtime data GENERICALLY (`ScalarSlot`/`ViewRow` cells,
  `SetScalar`/`ResizeArray`, lookup by NAME STRING — `FindField`/`FindElemField`, not typed methods).
  `ViewWireReaderSoa::Apply(wire, store)` decodes the `UTVA`/SoA wire into a `ViewStore` driven by the
  store's own blob (declared field order + kind). **A typed accessor emitter sits ON TOP OF
  `ViewStore`** — generating `T field(i)` wrapper methods that internally do
  `store.Array(fieldIndex)[i].Cell(memberIndex).f` (etc.), not reimplementing byte-level reads.
- **Yeroket codegen infrastructure already reusable, minimal net-new work**: `SourceGenerator~/
  Transpiler/ViewModel.cs`'s `ViewModelBuilder.Build` already produces the exact typed model
  (`ViewStruct`/`ViewField` with Name/Kind/Scalar/Struct/Layout) any new emitter would consume — no
  model-walking infrastructure is net-new. `ViewBlobEmitter.cs` is the closest existing sibling
  (depth-first nested-struct-array emission via `NestedCollect.DepthFirst`, banner stamping,
  `ViewVersionHash.Compute`) — all directly reusable patterns. The new emitter is structurally a
  SIBLING to `ViewBlobEmitter.cs`/`RmlDataModelEmitter.cs`/`ViewWriterEmitter.cs` (same `Transpiler/`
  dir, same `ViewStruct` input) — only the EMISSION TEMPLATES (per-field typed getter methods,
  section/column-id enums, a ctor calling `store.Array(idx)`) are genuinely new code.
- **The Vec3f-scalar/ListVec3f and `[Projected]`-dispatch gaps (Milestone 3's known follow-ups) do
  NOT block this emitter.** Both gaps are write/declaration-side only (`ViewModelBuilder.Classify`
  throws on Vec3f-scalar/ListVec3f shapes; `ViewWriterEmitter.ToBuffer()` never dispatches to a
  callable) — the accessor emitter only needs to walk whatever `ViewStruct`/`ViewField` model already
  exists from Milestone 3's declarations (4 fully-declared sections + Bodies' 7-of-10 columns) and can
  be built/proven against that TODAY. `Bodies.Position`/`RecipeParams`/`OrbitPath` remain an
  explicitly-named, deferred gap on the EMITTER's output too — mirroring Milestone 3's own scope
  split, not a new decision.

## Scope boundary — TWO milestones, not one (the emitter's proof is separable from the reader cutover)

- **Milestone A (Yeroket-only, in-tree) IS**: build `TypedAccessorEmitter.cs` (sibling to
  `ViewBlobEmitter.cs`) that walks `ViewStruct`/`ViewField` and emits a `view_contract.h`-equivalent
  header whose classes wrap a `ViewStore` — ctor takes the store, `count()` reads array length, each
  field gets a named typed getter delegating to the store's generic cell API. Prove it against the 4
  fully-declared sections + Bodies' 7 columns: generate the header, write a small C++ harness that
  fills a `ViewStore` via `ViewWireReaderSoa::Apply` and calls the generated accessors, diff decoded
  values against Milestone 3's existing 48/48 proof frame. **IS NOT**: touching undertow at all this
  milestone — pure Yeroket/VIXEN in-tree proof, mirroring every prior increment's "prove in-tree
  before touching undertow" discipline.
- **Milestone B (undertow-side, the harder half — pays off Gap #3 for real) IS**: switch `main.cpp`
  (and any other real `view_contract.h` consumers found in Task 1) from the hand-rolled header to the
  Milestone-A-generated one — which necessarily also means switching the read path from
  `view_contract.h`'s own `WireReader`/`SectionView` (`UTVW`-TOC) to `ViewWireReaderSoa`/`ViewStore`
  (`UTVA`/SoA). Live-gate against the real sim→render seam (real host + renderer, not just a unit
  test) — this is the actual proof the wire-protocol swap didn't silently break anything. **IS NOT**:
  retiring `EmitViewContractHeader.cs`/`ViewWriterGenerator.cs`/`ViewSchema.cs`'s hand-declared
  constants yet — that's the ORIGINAL plan's Task 5, which can only proceed once Milestone B here
  lands and is live-gated; this Inc-5b doc's own scope ends at "undertow consumes the generated
  reader correctly," not at "the old generator is deleted." Retirement itself is a follow-on
  Milestone C (or folds into whichever increment closes out the original plan's Task 5).
- **IS NOT** (both milestones): touching `Bodies.Position`/`RecipeParams`/`OrbitPath` (the deferred
  Vec3f-scalar/ListVec3f gap) or wiring `[Projected]`-dispatch into the C# writer — both stay
  explicitly out of scope, named follow-ups, not silently absorbed into this plan.
- **IS NOT**: touching undertow's `core/` Env-1-authoritative sim types — same hard wall as the
  original Inc-5 plan; all undertow-side changes land in the ENV-2-permitted `Undertow.View`/
  `Undertow.Bridge`/`vixen/` areas.

## Tasks

### Task 1 — Ground the shape + decide the emission template (READ + REPORT before building)
- Re-verify `view_contract.h`'s accessor shape, `main.cpp`'s real call sites (and find any OTHER real
  `view_contract.h` consumers beyond `main.cpp`), and `ViewStore`'s generic cell-access API fresh in
  full, confirming file:line against current HEAD in all three repos.
- Decide the exact emission template: per-field getter body shape (how it maps a `ViewField` to a
  `store.Array(idx)[i].Cell(member)` call chain), how column/section IDs get emitted (an enum? a
  constant? — match whatever `view_contract.h`'s current `kColumnId`/`kSectionBodies`-style constants
  look like so call sites don't need renaming), and the exact proof harness shape (a small standalone
  C++ program or a gtest, matching Milestone 3's existing 48/48 proof-frame mechanism).

### Task 2 — Milestone A: build + prove the typed accessor emitter (Yeroket/VIXEN in-tree only)
- Implement `TypedAccessorEmitter.cs` per Task 1's decision. Generate the header for the 4
  fully-declared sections + Bodies' 7 columns.
- Prove equivalence: decoded values via the generated accessors must match Milestone 3's existing
  proof-frame decoded values exactly, for every declared column.

### Task 3 — Milestone B: cut undertow's real reader over + live-gate
- Find every real `view_contract.h` consumer in undertow (not just `main.cpp`).
- Switch them to Milestone A's generated header + the `ViewWireReaderSoa`/`ViewStore` read path
  (replacing `view_contract.h`'s own `WireReader`/`SectionView`/`UTVW`-TOC path).
- Live-gate: undertow's actual sim→render seam (real host + renderer) must function identically to
  before the cutover — this is the proof the wire-protocol swap didn't silently break real behavior.

## Gates / guardrails
- Byte/decoded-value-identical proof (Milestone A) is mandatory before Milestone B begins.
- Live-gate (Milestone B) is mandatory before this Inc-5b plan can be considered done — a passing unit
  test is not sufficient proof for a wire-protocol cutover, per this program's own established
  discipline (View-Model-Binding Inc-Ovr, Inc-5 Milestone 3).
- No generated/consuming code lands in undertow's `core/` Env-1-authoritative sim types.
- Cross-repo work (Yeroket + VIXEN + undertow) — follow the established worktree-pairing convention
  from Inc-5 rather than improvising a new one. Reuse the EXISTING worktrees where clean:
  `undertow/.claude/worktrees/view-contract-inc5-m4` (undertow), `VIXEN/.claude/worktrees/
  view-contract-inc5` (VIXEN), and create a fresh Yeroket worktree branching off Yeroket `main`
  (post-codegen-unification-program merge).
- rtk masks git exit codes — use `/usr/bin/git`, `sha256sum`, `cmp` for evidence in all three repos.
- Do NOT push without explicit confirmation all repos' halves are ready to land together — a partial
  cross-repo push here is worse than usual, since undertow's build wiring couples to what Yeroket
  actually emits.

## Milestone Map
- [ ] **Milestone 1 (Task 1):** ground the shape, decide the emission template (report-back gate). One
  Sonnet implementer + one Opus validator.
- [ ] **Milestone 2 (Task 2, = "Milestone A" above):** build + prove the typed accessor emitter,
  Yeroket/VIXEN in-tree only, no undertow changes. One Sonnet implementer + one Opus validator.
- [ ] **Milestone 3 (Task 3, = "Milestone B" above):** cut undertow's real reader over + live-gate the
  real sim→render seam. One Sonnet implementer + one Opus validator.

## Progress Log

(none yet)
