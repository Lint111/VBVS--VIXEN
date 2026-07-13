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

## Update (2026-07-13, later same day): the Vec3f-scalar MODEL-layer gap is now closed

A separate, Yeroket-internal "Type-Shape Recognizer Unification" increment (own plan doc:
`Type-Shape-Recognizer-Unification-Plan-2026-07.md`, merged+pushed to Yeroket `main` `bb7c0ff4`)
extracted a shared `FieldShapeRecognizer` consumed by both `[GpuStruct]` and `[View]`, and added a new
`ViewFieldKind.Vector`/`ViewField.VectorMarkerName` case to `ViewModelBuilder.Classify`. **Confirmed,
proven end-to-end**: `ViewModelBuilder.Build` no longer throws for a `Float3`-marker-typed SCALAR
field (matching `Bodies.Position`/`Bodies.RecipeParams`'s real shape exactly) — it now builds to
`ViewFieldKind.Vector`. This closes the MODEL-layer half of Bodies' Vec3f-scalar gap referenced
throughout this doc's "Bodies MUST stay explicitly deferred" language below.

**This does NOT yet mean Bodies can be fully declared/emitted.** The EMISSION layer — this doc's own
Milestone 2's `TypedAccessorEmitter.cs` (C++ reader) and `ViewWireFormat`/`ViewWriterEmitter` (C#
writer) — has ZERO code handling `ViewFieldKind.Vector` today; it was built before this case existed.
Confirmed via the type-shape-unification increment's own graceful-failure proof: `ViewWireFormat.
EmitToBufferBody` genuinely throws `NotSupportedException` naming the field for a Vector-kind field —
by design, since wiring EMISSION was explicitly left out of that increment's scope, deferred to here.

**New Milestone 2.4 inserted** (before 2.5) to close this emission gap, since without it Bodies stays
deferred regardless of the model-layer unblock — see below.

## Milestone 2.5 — writer-side wiring (discovered 2026-07-13, inserted before Milestone 3)

Milestone 3's first dispatch correctly reported BLOCKED (see Progress Log below) on a real gap this
plan's own Ground Truth got wrong: it assumed undertow's real writer was "already wired... from the
original Inc-5's Milestones 2-3." It was NOT. `HostSession.cs:148`
(`_viewBuffer ??= ViewWriter.WriteView(Frame)`) still calls undertow's OWN in-tree
`EmitViewWriter.cs`-generated writer, producing the OLD `UTVW`-TOC wire — Yeroket's actual new-wire
producer (`<Model>ViewWriter.ToBuffer()`, emitting `UTVA`) is NEVER invoked anywhere in undertow
(confirmed zero references, zero `UTVA` bytes anywhere in the repo). User decision (2026-07-13):
extend this plan with a new Milestone 2.5 to build this writer-side wiring, rather than holding or
partial-scoping to Hud-only.

**Ground truth (researched 2026-07-13):**
- **`Frame`'s real type**: a private cached `SimFrame` (`core/src/Undertow.Sim/SimFrame.cs:353`),
  populated by `_sim.ProjectFrame()` each tick — real production sim-projection data, not a fixture.
  Holds `IReadOnlyList<T>` collections named after the 5 view sections: `.Bodies`/`.Hud`/
  `.HudFactions`/`.HudEvents`/`.HudInspect` (plus others unused by these sections).
- **Current writer** (`EmitViewWriter.cs:35-150`) generates `ViewWriter.WriteView(SimFrame frame)`:
  per section, `IReadOnlyList<T> items = frame.{Collection}`, per column a loop evaluating a verbatim
  `Source` C# expression string per element (e.g. `el.Tick`), packed via `ViewBufferBuilder` into the
  old `UTVW` wire.
- **Yeroket's `<Model>ViewWriter.ToBuffer()` shape** (`ViewWriterEmitter.cs:24-70`): a plain data-holder
  class — public fields matching the `[View]`-declared struct 1:1 (scalars + `List<RowStruct>` for
  struct-arrays), NO ctor takes external input. `ToBuffer()` reads `this.<field>` and serializes to
  `UTVA`. It expects the CALLER to have already populated an instance before calling `ToBuffer()` — it
  has zero knowledge of `SimFrame`.
- **The gap is a mapping/glue problem, NOT structural, for the 4 Hud-family sections.**
  `SimFrame.Hud/HudFactions/HudEvents/HudInspect` already have a near-identical field shape to
  `UndertowHud.cs`'s schema classes (real, already declared+proved in the original Inc-5's Milestone 3,
  not a stub). The needed new code is mechanical: `new <Model>ViewWriter { Tick = frame.Hud[0].Tick,
  ... }.ToBuffer()`, replacing `ViewWriter.WriteView(Frame)` at `HostSession.cs:148`. No structural
  mismatch found.
- **Bodies' Vec3f-scalar gap (`Position`/`RecipeParams`) is confirmed blocked SYMMETRICALLY on the
  writer side too.** `ViewModelBuilder.Classify` (`ViewModel.cs:58-72`) only recognizes int/float/
  bool/string scalars; a Vec3f-shaped scalar field either misclassifies or throws. `ViewWireFormat.
  EmitField` explicitly throws for any plain `Struct`-kind field. Bodies MUST stay explicitly deferred
  on the writer side too — only the 4 Hud-family sections are in scope for Milestone 2.5, mirroring
  the reader-side (Milestone 2) precedent exactly, not a new decision.
- **Scope split recommended and adopted**: Milestone 2.5a (build the adapter classes for the 4
  Hud-family sections + swap `HostSession.ViewBuffer`, prove round-trip via a C# unit test — pure C#,
  no live-gate needed yet) is separable from actually landing it, because a writer-only change with
  the OLD C++ reader still in place would silently break `main.cpp`/`hud_view.h` (both still expect
  `UTVW`-TOC) — a strictly worse state than today's (currently-correct) `UTVW` end-to-end path. **The
  writer (Milestone 2.5) and the reader cutover (Milestone 3) must land and be live-gated TOGETHER as
  one real sim→render proof, not the writer alone.**

## Milestone 2.4 — wire Vector emission through both existing emitters (inserted 2026-07-13)

**Goal**: make `ViewFieldKind.Vector` a genuinely EMITTABLE case in both directions, so Bodies'
`Position`/`RecipeParams` can finally be declared in full — closing the gap the type-shape-unification
increment deliberately left open (model-layer recognition only).

**SCOPE AMENDED 2026-07-13 after Task 1's research + Opus validation**: Task 1 found the original
scope below (2 C# emitters only) is genuinely too narrow. The C++ runtime types (`ViewBlob.h`'s
`ViewKind` enum, `ViewValue`/`ViewCell`, `ViewWireReaderSoa`'s decode switch) have NO `Vector` case at
all today — there is no way to represent 3 floats under one field entry without one. Additionally,
`ViewBlobEmitter.cs` (not originally named) has a silent-default-arm risk: a `Vector`-kind field
currently falls through its `KindEnum` switch to `_ => "Int"`, meaning leaving it untouched would
produce a SILENT MISCOMPILE (wrong blob kind), not a loud failure. Confirmed by the Opus validator
independently against real source — this is a real, decisively-confirmed gap, not a hypothetical.
**Design (A) — a genuine, architecturally pure `ViewKind::Vector` end-to-end — is adopted** (per the
user's own standing "prefer pure/fully-correct solutions" rule) over design (B) — decomposing a
Vector field into 3 synthetic Float-kind sub-fields at the blob layer — which would make `fieldCount`
untruthful and is exactly the "flatten to 3 floats" workaround `UndertowHud.cs`'s own header already
rejected in writing.

**Scope boundary (AMENDED — 6 files now in scope, not 2)**
- **IS**: extend `TypedAccessorEmitter.cs` (Milestone 2's reader emitter) to emit a typed getter for a
  `Vector`-kind field, returning a NEW local `struct Vec3f { float x,y,z; }` emitted into the generated
  header's `Vixen::Views` namespace (no reusable Vec3f-equivalent C++ type exists anywhere in VIXEN's
  own libraries — confirmed by Task 1's research; the only close precedent is undertow's own retiring
  `EmitViewContractHeader.cs`'s `Vec3f`, wrong repo, not reusable).
- **IS**: extend `libraries/RenderGraph/include/Ui/ViewBlob.h` — add a real `Vector` case to the
  `ViewKind` enum (alongside `Int`/`Float`/`Bool`/`String`/`ArrayOfStruct`) and decide `ViewValue`'s
  Vector payload representation.
- **IS**: extend `libraries/RenderGraph/include/Ui/ViewStore.h` + `src/Ui/ViewStore.cpp` — `ViewCell`/
  `ScalarSlot`/`SetScalar`/`ScalarSlotPtr` all need Vector-kind handling (3-float storage/access).
- **IS**: extend `libraries/RenderGraph/src/Ui/ViewWireReaderSoa.cpp` — a new decode case reading 3
  consecutive F32s for a Vector-kind field, one `SetScalar`-equivalent call carrying all 3.
- **IS**: extend `ViewBlobEmitter.cs`'s `KindEnum` to emit `ViewKind::Vector` for a
  `ViewFieldKind.Vector` field — MANDATORY, not optional; leaving this untouched produces a silent
  `ViewKind::Int` miscompile, confirmed by Task 1/validator.
- **IS**: extend `ViewWireFormat.cs`/`ViewWriterEmitter.cs` (the C# writer side) symmetrically: emit a
  `Vector`-kind field's `ToBuffer()`/wire-write logic (write 3 floats via `WF32` calls) and the
  `<Model>ViewWriter` data-holder's public field shape for a Vector column (a small local C# struct,
  e.g. `public struct Vec3 { public float X,Y,Z; }`, since no shared/canonical C# Vec3 was found reused
  elsewhere in Yeroket — confirm this in Task 2, don't assume).
- **Prove**: a NEW minimal proof schema (`VectorProof.cs`, a single `[View] struct { Float3 position;
  }`), NOT modifying the native `Hud` schema (would perturb its version hash / existing 48/48-style
  proof, per Task 1's recommendation). Prove a full round-trip: write a non-trivial, non-zero-in-every-
  component Vec3f value via the writer, read it back via the generated typed accessor, confirm exact
  value match.
- **IS NOT**: touching `Bodies.OrbitPath` (`ListVec3f` — a nullable, per-row variable-length list of
  points, a DIFFERENT and harder gap than the flat Vec3f-scalar case; explicitly stays deferred, its
  own follow-up).
- **IS NOT**: declaring undertow's real `Bodies` schema in full yet — that's Milestone 2.5's job (or a
  Milestone 2.5-adjacent task), once this milestone proves Vector emission works in isolation via a
  proof schema, mirroring Milestone 2's own "prove in Yeroket/VIXEN in-tree before touching undertow's
  real schema" discipline.
- **IS NOT**: fixing the ALSO-flagged-but-unreachable-today NPE landmine in `TypedAccessorEmitter.cs`'s
  struct-array element loop (`ef.Scalar.Value` on a null `Nullable<ViewScalar>` if a Vector-kind
  element ever appears in a row) — Bodies' Position/RecipeParams are both top-level scalar fields, not
  struct-array elements, so this path is unreachable for this milestone's in-scope fields. Noted as a
  real, undocumented landmine for whoever eventually needs a Vector-kind struct-array element.

### Tasks (Milestone 2.4)

**Task 1 (Milestone 2.4a) — Ground the shape + decide the wire layout (READ + REPORT before building)**
- DONE, see Progress Log below. Confirmed `Float3` is a genuinely EMPTY marker struct (recognition by
  name+namespace only). Confirmed the wire-layout premise ("3 consecutive float cells") doesn't hold
  at the C++ `ViewBlob`/`ViewStore`/`ViewWireReaderSoa` layer — `ViewKind` has no `Vector` case, so the
  amended scope (design A, 6 files) is required.

**Task 2 (Milestone 2.4b) — Build + prove (both directions, all 6 amended-scope files)**
- Extend `ViewBlob.h`'s `ViewKind` enum with a real `Vector` case + decide `ViewValue`'s payload.
- Extend `ViewStore.h`/`.cpp`'s `ViewCell`/`ScalarSlot`/`SetScalar`/`ScalarSlotPtr` for Vector storage.
- Extend `ViewWireReaderSoa.cpp`'s decode switch with a new Vector case (3 consecutive F32 reads).
- Extend `ViewBlobEmitter.cs`'s `KindEnum` to emit `ViewKind::Vector` (mandatory — the silent-Int-
  miscompile risk Task 1 found).
- Extend `TypedAccessorEmitter.cs` to emit a typed getter for a Vector field, returning a new local
  `Vec3f{x,y,z}` C++ struct.
- Extend `ViewWireFormat.cs`/`ViewWriterEmitter.cs` (C# writer side) symmetrically — `ToBuffer()`
  writes 3 floats, the `<Model>ViewWriter` data-holder gets a small local C# Vec3 struct for the
  column.
- Prove the full round-trip (write→read, exact value match) via the new `VectorProof.cs` schema +
  gtest, per the Scope boundary above.
- Full regression: every existing View test (Milestone 2's `test_typed_accessor_emitter`, the
  pre-existing `test_view_wire_soa_roundtrip`, and all of Yeroket's `CodegenTool.Tests`) must still
  pass — this is additive capability, not a redesign of any existing shape's emission.

## Milestone 2.5 (renumbered from the original insertion — see note above) — writer-side wiring

**SCOPE AMENDED 2026-07-13, post-Milestone 2.4**: Milestone 2.4 (a+b) closed the Vector-scalar gap at
BOTH the model layer (`ViewFieldKind.Vector`) and the emission layer (`ViewKind::Vector` end-to-end,
both directions, proven on a synthetic `VectorProof` schema). `Bodies.Position`/`RecipeParams` are now
mechanically declarable and emittable — the "Bodies stays deferred" language below is SUPERSEDED for
those 2 columns. `Bodies.OrbitPath` (`ListVec3f`, a variable-length list-of-points — a DIFFERENT,
harder gap than the flat Vec3f-scalar case) remains genuinely out of scope, still deferred.

- **IS**: build ALL 5 sections' adapter/writer classes (`<Model>ViewWriter`-populating glue reading
  real `SimFrame.Bodies/Hud/HudFactions/HudEvents/HudInspect` fields — Bodies now included, using
  Milestone 2.4's Vector writer support for `Position`/`RecipeParams`, with the explicit narrowing
  `Vec3(double)→Vec3f(float)` conversion Milestone 2.4a's research flagged since `SimFrame`'s
  `Undertow.Sim.Vec3`/`Undertow.Generation.StarSystem.Vec3` is a DIFFERENT, double-precision type than
  the kernel's `Float3` marker — this is real glue work, not a 1:1 field copy for these 2 columns),
  replace `HostSession.cs:148`'s call site to produce `UTVA` bytes for all 5 sections EXCEPT
  `Bodies.OrbitPath` (which stays unrepresented/deferred on this section same as before — confirm
  whether the real `Bodies` schema declaration needs a 9th column present-but-unbacked or whether
  `OrbitPath` is simply omitted from the `[View]` schema entirely, matching how the ORIGINAL Inc-5
  Milestone 3 partial-Bodies declaration already handled this 7-of-10-columns split). Prove a real
  round-trip: populate from a REAL captured `SimFrame` (not just Milestone 2's synthetic Hud fixture
  or Milestone 2.4's synthetic `VectorProof` fixture), call `ToBuffer()`, decode via
  `ViewWireReaderSoa::Apply`+the Milestone-2/2.4-generated typed accessors, and confirm decoded values
  match the real `SimFrame`'s real values exactly — INCLUDING at least one real Bodies row's
  `Position`/`RecipeParams` values, proving the Vec3(double)→Vec3f(float) conversion is correct for
  real sim data, not just the synthetic proof schema's hand-picked test values.
- **IS NOT**: cutting the real C++ reader over yet (that's Milestone 3, which must land together with
  this milestone before either is considered "done" for the real seam) — Milestone 2.5 alone leaves
  `HostSession.ViewBuffer` producing a NEW wire that the OLD C++ reader can't read; this milestone's
  own proof must be a C#-side/isolated round-trip only, NOT a claim that the real seam works yet.
- **IS NOT**: touching `Bodies.OrbitPath` (`ListVec3f`) — still genuinely deferred, a different and
  harder gap than the Vec3f-scalar case Milestone 2.4 closed.
- **IS NOT**: making `HostSession.ViewBuffer`'s new output live/consumed by anything real until
  Milestone 3 also lands — until then this is dead-code-in-waiting, deliberately, to avoid a half-swap
  that breaks the real seam.

### Milestone 2.5's own Task 1 (grounding pass) — DONE, findings + decision recorded here

- **Confirmed all 9 real `BodyView` fields map 1:1 to `UndertowBodyRow`'s columns** (7 already-declared
  + `Position`/`RecipeParams` newly includable). `Label`/`TypeLabel`/`Composition` never in scope.
- **Confirmed the Vec3(double)→Vec3f(float) conversion is a plain per-component narrowing cast**, no
  unit/axis conversion — precedented by the OLD hand-rolled writer's own identical cast
  (`PutF32(pos, i*12+0, (float)__p.X)`). Milestone 2's Progress Log already precedent-checked exact
  narrowing-precision safety for `Mass`; the round-trip proof should do the same cheap sanity check for
  Position/RecipeParams' AU-scale range.
- **Confirmed `OrbitPath` handling**: simply OMITTED from the `[View]` schema entirely — the exact
  precedent from the original Inc-5 Milestone 3's own "Gap #4 RESOLVED" resolution (which originally
  omitted Position/RecipeParams/OrbitPath all three; only OrbitPath remains omitted now).
- **CRITICAL finding, independently confirmed rigorously by the Opus validator**: the new Yeroket wire
  has NO multi-section container. The OLD `ViewBufferBuilder`/`EmitViewWriter.All()` combines all 5
  sections into ONE TOC-based buffer (magic+version+3-field-per-section TOC+16-byte-aligned bodies).
  The NEW `<Model>ViewWriter.ToBuffer()` is one independent, self-branded `UTVA` blob PER SCHEMA CLASS
  (5 separate classes) — no combiner exists anywhere. `ViewWireReaderSoa::Apply` is confirmed to be a
  single-schema, non-TOC decoder that hard-errors on trailing bytes — it cannot consume 5 concatenated
  blobs without a per-section unwrap step first. This is a genuine, previously-unaddressed design gap,
  not simple glue — correctly NOT silently decided by the implementer.
- **DECISION (2026-07-13, made here after independent Opus validation of both options)**: adopt
  **option (b)** — build a NEW, minimal multi-section container with its OWN distinct magic (`UTVC`,
  per the validator's own guardrail suggestion, so a stray single-schema `UTVA` blob can never be
  mistaken for a container) wrapping the 5 self-describing `UTVA` sub-buffers (magic + count + a
  simple offset/length TOC + concatenated sub-buffers). Rejected option (a) — exposing 5 separate
  buffers from `HostSession`'s public API — because it would force a new multi-pointer/count ABI
  across the C#↔C++ FFI seam (today `HostSession.ViewBuffer` is ONE `byte[]`, pinned via one
  `GCHandle`, handed to C++ as one `ut_view` pointer), pulling `HostAbi`/`main.cpp`/`hud_view.h` FFI
  plumbing into a milestone scoped as "writer-side wiring," and entangling Milestone 2.5/3 more
  tightly than the plan's own "must land together" already requires. Option (b) instead: preserves
  `HostSession.ViewBuffer`'s existing single-`byte[]` shape unchanged; directly mirrors the retiring
  `ViewBufferBuilder`'s own envelope shape (a well-understood pattern, not novel); and confines all new
  wire-consumption risk to ONE new outer-container-unwrap step in Milestone 3's C++ reader cutover
  (read count+TOC, hand each sub-span to `ViewWireReaderSoa::Apply` in a loop) — additive and
  localized, rather than reshaping the FFI pointer-passing convention itself.
- **Confirmed real-data proof source**: `new UndertowSim(); sim.NewCampaign(7); sim.ProjectObserverFrame()`
  (`ViewContractTests.cs:14-16,33-35`) is real, already-exercised production sim-projection data — no
  new sim-bootstrapping code needed, directly reusable for Milestone 2.5's round-trip proof.

## Milestone Map
- [x] **Milestone 1 (Task 1):** ground the shape, decide the emission template (report-back gate). One
  Sonnet implementer + one Opus validator.
- [x] **Milestone 2 (Task 2, = "Milestone A" above):** build + prove the typed accessor emitter,
  Yeroket/VIXEN in-tree only, no undertow changes. One Sonnet implementer + one Opus validator.
- [x] **Milestone 2.4a (inserted 2026-07-13, Task 1):** ground the shape, decide the wire layout
  (report-back gate). Found the original 2-file scope too narrow — amended to 6 files under design
  (A), a real `ViewKind::Vector`. One Sonnet implementer + one Opus validator.
- [x] **Milestone 2.4b (Task 2):** build across all 6 amended-scope files (`ViewBlob.h`, `ViewStore.h`/
  `.cpp`, `ViewWireReaderSoa.cpp`, `ViewBlobEmitter.cs`, `TypedAccessorEmitter.cs`,
  `ViewWireFormat.cs`/`ViewWriterEmitter.cs`) + prove a full round-trip on the new `VectorProof.cs`
  schema, Yeroket/VIXEN in-tree only. One Sonnet implementer + one Opus validator.
- [ ] **Milestone 2.5 (writer-side wiring, inserted 2026-07-13):** build the 5-section writer/adapter
  classes reading real `SimFrame` data (Bodies now includable per Milestone 2.4's unblock, if it lands
  first — otherwise Hud-family only, Bodies deferred), swap `HostSession.ViewBuffer`'s call site,
  prove a real round-trip in isolation (C#-side only, not yet live). One Sonnet implementer + one
  Opus validator.
- [ ] **Milestone 3 (Task 3, = "Milestone B" above):** cut undertow's real C++ reader over (must land
  TOGETHER with Milestone 2.5, not independently) + live-gate the real sim→render seam. One Sonnet
  implementer + one Opus validator.

## Progress Log

- Milestone 1 (Task 1, research-only): DONE · 2026-07-13 · no files modified in any of the three
  repos
  - **`view_contract.h` shape re-confirmed** (316 lines, 5 section classes: `BodiesSection`/
    `HudSection`/`HudFactionsSection`/`HudEventsSection`/`HudInspectSection`) — ctor caches columns via
    `s_.column(kColumnId, end_, len_out)`, `count()` returns `s_.row_count()`, one bounds-checked typed
    getter per column, offsets-array-then-blob for variable-length columns.
  - **NEW finding: `hud_view.h`'s `ReadHudView()` (~line 58-126) is a SECOND real, live consumer**
    beyond `main.cpp` — constructs all 4 Hud-family section classes, called from `main.cpp:39,785`.
    Milestone 3 must satisfy both consumers, not just `main.cpp`. Additionally found `test_hud_view.cpp`
    and `test_view_contract.cpp` as further real (test-only) consumers to account for.
  - **CRITICAL API correction to this plan's own assumed emission template**: `ViewStore.h`'s
    `ViewCell` members are `.i`/`.f`/`.b`/`.s` (NOT `.f32`/`.i32` as originally assumed).
    `ViewStore::Array(fieldIndex)` is PUBLIC. `FindField`/`FindElemField` are PRIVATE — a generated
    accessor CANNOT do runtime name lookup; it MUST bake field/elem indices as compile-time integer
    literals (codegen knows the declared field order, so this is trivial, just a real constraint to
    respect). `ScalarSlotPtr(fieldIndex)` returns `void*`, cast per the field's known-at-codegen-time
    kind. Both implementer and validator independently confirmed this against real `ViewStore.h`/
    `ViewStore.cpp` — Milestone 2's emitter is buildable on this API as corrected.
  - **`ViewWireReaderSoa::Apply(std::span<const std::byte> wire, ViewStore& store)` signature
    confirmed exactly.**
  - **`ViewBlobEmitter.cs` (92 lines, real path
    `Packages/com.yeroket.utility.kernel-framework/...`) read in full as the pattern-match template** —
    `KindEnum`, `StructArrayDeps`+`NestedCollect.DepthFirst`, `GeneratedBanner.Line`,
    `ViewVersionHash.Compute` all confirmed reusable.
  - **IMPORTANT: the parent Inc-5 plan's "48/48 decoded-value proof" harness was NEVER actually
    committed anywhere** — confirmed by both implementer and validator via independent `git log --all`
    searches across all three repos (the parent plan doc's own line 551 admits "harness uncommitted,
    can't re-run" — this was trusted-from-report at the time, not re-derivable). Milestone 2 must
    RECREATE the proof, not reuse an existing harness.
  - **Proof harness decision**: reuse `libraries/RenderGraph/tests/test_view_wire_soa_roundtrip.cpp`'s
    existing canonical-wire-bytes fixture + `kHudBlob` (confirmed real, 3/3 passing, CMake-registered)
    — build the NEW generated accessor class over the SAME `ViewStore` after `Apply()` and assert the
    same decoded values the raw API test already proves, rather than attempting to resurrect the lost
    undertow-real-writer cross-check (that belongs to Milestone 3 anyway, where it's unavoidable).
  - **Confirmed no analog needed for `kSectionBodies`/`kBodiesPosition`-style enum constants** in the
    new `ViewStore`-position-indexed read path — those constants only served the OLD `SectionView::
    column(id,...)` TOC-lookup API being replaced, not reused.
  - No blockers.
  - **Opus validator (independent re-verification):** APPROVED. Independently re-verified all 8
    findings against real source in all three repos, including the load-bearing `ViewStore` API
    correction (most rigorously) and the missing-proof-harness claim via its own search rather than
    trusting the implementer. Flagged 2 additional real consumer files for Milestone 3
    (`test_hud_view.cpp`/`test_view_contract.cpp`). Cleared to proceed to Milestone 2.

- Milestone 2 (Task 2, "Milestone A" — build + prove the typed accessor emitter): DONE · 2026-07-13
  · Yeroket/VIXEN in-tree only, undertow untouched.
  - **Built `TypedAccessorEmitter.cs`** (Yeroket `Packages/com.yeroket.utility.kernel-framework/
    SourceGenerator~/Transpiler/TypedAccessorEmitter.cs`, sibling to `ViewBlobEmitter.cs`) per
    Milestone 1's corrected emission template exactly: one `<Name>Section` class per top-level
    `ViewStruct`, ctor takes `Vixen::RenderGraph::ViewStore&` stored as a reference member (not
    cached raw pointers), scalar getters cast `ScalarSlotPtr(kFieldIdx)`, struct-array element
    getters read `store_.Array(kFieldIdx)[i].Cell(kElemIdx).<i|f|b|s>`, all field/elem indices
    baked as `static constexpr size_t` literals (no runtime name lookup, since `FindField`/
    `FindElemField` are private). NO `kSectionBodies`/`kBodiesPosition`-style enum constants
    emitted, confirmed correct per Milestone 1.
  - **Handles N struct-array fields per struct**, not just one: a struct with exactly one array
    field gets a plain `count()`; more than one (discovered live via the native `Hud` schema,
    which declares BOTH `factions` and `events`) gets `count_<field>()` per array, and per-element
    getters/index constants are namespaced `<field>_<elem>` to avoid name collisions across two
    arrays. This wasn't explicitly spelled out in the plan (which described a single-array-per-
    section shape matching `view_contract.h`) but was a real correctness gap the first draft
    silently hit (dropped the second array's getters entirely) — fixed before proving.
  - **Wired `--typed-accessor-cpp` CLI flag** into `CodegenTool~/Program.cs`, mirroring
    `--view-blob`'s `--schema`/`--out-header`/`--check` pattern exactly.
  - **Generated headers for all 6 target schemas**: the 5 Undertow-migration schemas named by the
    plan (`UndertowHud`/`UndertowHudFactions`/`UndertowHudEvents`/`UndertowHudInspect`/
    `UndertowBodies`, from VIXEN `codegen/view-schemas/UndertowHud.cs`) plus the native `Hud`
    schema (from `codegen/view-schemas/Hud.cs`) needed for the proof harness below. Checked into
    VIXEN `application/main/include/Generated/` as `*.typed.g.h` (Undertow schemas) and
    `HudTypedAccessor.g.h` (native Hud, distinct name from the pre-existing `Hud.g.h` which is
    `RmlDataModelEmitter`'s unrelated `--view` output).
  - **Equivalence proof**: new gtest `test_typed_accessor_emitter.cpp` (VIXEN `libraries/
    RenderGraph/tests/`, wired into that dir's `CMakeLists.txt`) reuses
    `test_view_wire_soa_roundtrip.cpp`'s EXACT canonical-SoA-wire fixture + `kHudBlob` per
    Milestone 1's decision — fills a `ViewStore` via `ViewWireReaderSoa::Apply`, then wraps the
    SAME store in the generated `Vixen::Views::HudSection` and asserts every decoded value
    (`tick`=7, `bodyCount`=12, `activeLensName`="Ops", `activeLensCount`=4,
    `factions`=[Reds/0.5/T/T/F/T, ""/0/F/F/F/F, Greens/0.75/F/T/T/F], `events`=[war@40,
    truce@99]) matches the raw-API test's proven values exactly. **1/1 PASS.**
    `test_view_wire_soa_roundtrip` re-run alongside: still 3/3 PASS (no regression).
  - **Build note**: this milestone added a new source file to `tests/CMakeLists.txt`, so
    `build.bat build` alone would NOT have picked it up (ninja has no idea the new `.cpp` exists) —
    ran `build.bat all` (full reconfigure + build) per the vixen-build-policy skill's explicit rule
    for this case. Confirmed the `.obj` for the new test actually exists post-build (not a stale
    binary false-pass).
  - **Yeroket-side regression check**: `dotnet test CodegenTool.Tests.csproj` — 43/43 PASS (no
    existing test broken by the new emitter or the `Program.cs` CLI wiring).
  - **SDFNodeGenerator.dll gotcha hit and handled correctly**: `dotnet test` rebuilt this DLL
    non-deterministically (byte diff, no source touched) exactly as the known gotcha predicts —
    `git checkout --` it before committing rather than committing the incidental rebuild.
  - Commits: Yeroket `f83fb68c` (`feat/view-contract-inc5`, `TypedAccessorEmitter.cs` +
    `Program.cs` CLI flag); VIXEN `ae040813` (`feat/view-contract-inc5`, gtest + CMakeLists.txt
    wiring + 6 generated headers).
  - No blockers. Milestone B (undertow cutover + live-gate) is next; Bodies'
    `Position`/`RecipeParams`/`OrbitPath` remain the explicitly out-of-scope gap per this plan's
    own scope boundary.
  - **Opus validator (independent re-verification):** APPROVED. Confirmed the multi-array gap/fix is
    real by reading `Hud.cs`'s actual declaration (2 struct-array fields) and the emitter's current
    logic. Confirmed all index-baking + `.i`/`.f`/`.b`/`.s` cell-member claims directly against the
    generated header output. Could not rebuild from this WSL worktree (Windows MSVC toolchain), so
    verified the equivalence proof via a rigorous side-by-side read of both test files' assertions —
    confirmed the baked indices provably match the blob's declared field/elem order, making the
    agreement genuine, not tautological. Confirmed the pre-existing roundtrip test is byte-unmodified,
    undertow is completely untouched, Yeroket's own test suite (43/43) is unbroken, no
    `SDFNodeGenerator.dll` noise, and no `kSectionBodies`-style enum constants anywhere in the
    generated output. Cleared to proceed to Milestone 3.
  - **Opus validator (independent re-verification):** APPROVED. Confirmed the multi-array gap/fix,
    all index-baking + cell-member claims, and the non-tautological equivalence proof (baked indices
    provably match the blob's declared order). Confirmed no regression, undertow untouched, Yeroket's
    own suite unbroken, no dll noise, no `kSectionBodies`-style enums. Cleared to proceed to
    Milestone 3.

- Milestone 3 (Task 3, first attempt): BLOCKED, then RESCOPED · 2026-07-13 · no files modified in any
  of the three repos
  - Implementer re-confirmed the consumer list (`main.cpp`, `hud_view.h`, `test_hud_view.cpp`,
    `test_view_contract.cpp` — matches Milestone 1 exactly, no new consumers found) then, per this
    program's own "investigate and report BLOCKED, don't force a match" instruction, traced the
    writer side before touching any reader code, since the plan's own Ground Truth claimed the
    writer was "already wired... from the original Inc-5's Milestones 2-3."
  - **That claim was WRONG — a real gap in this plan's own scoping, not the implementer's error.**
    `HostSession.cs:148` (`_viewBuffer ??= ViewWriter.WriteView(Frame)`) still calls undertow's own
    in-tree `EmitViewWriter.cs`-generated writer, producing the OLD `UTVW`-TOC wire. Yeroket's actual
    new-wire producer (`<Model>ViewWriter.ToBuffer()`, emitting `UTVA`) is never invoked anywhere in
    undertow — confirmed via grep (zero references) by both the implementer and the controller
    independently. Switching the C++ reader today would decode garbage from a wire that's still
    old-format.
  - **Secondary, independent blocker also confirmed**: the Milestone-2-generated `UndertowBodiesSection`
    accessor is missing `position()`/`recipeParams()` (the deferred Vec3f-scalar gap) — but `main.cpp`'s
    real `ReadBodies()` actually reads both. Bodies cannot be mechanically cut over regardless of the
    writer-side blocker; the 4 Hud-family sections have no such gap.
  - Correctly made ZERO changes/commits anywhere rather than force a live-gate against a known-wrong
    wire, which would prove nothing.
  - **User decision (2026-07-13): extend this plan with a new Milestone 2.5** (writer-side wiring for
    the 4 Hud-family sections, real `SimFrame` data, C#-only round-trip proof — see the Milestone 2.5
    section above) rather than holding or partial-scoping to Hud-only immediately. Milestone 2.5 and
    Milestone 3 (reader cutover) must land and be live-gated TOGETHER, per the research's own finding
    that a writer-only change would silently break the currently-correct `UTVW` end-to-end path.
  - No blockers on the rescope itself — proceeding to Milestone 2.5.

- Milestone 2.4a (Task 1, research-only, ground shape + decide wire layout for Vector emission): DONE
  · 2026-07-13 · no files modified in any repo
  - **Confirmed `Float3` is a genuinely EMPTY marker struct** (`Runtime/GpuStructAttributes.cs`,
    `public struct Float3 { }`) — recognition is pure name+namespace match, never inspects fields.
  - **Confirmed a real gap**: undertow's real `Bodies.Position`/`RecipeParams` are backed by
    `Undertow.Sim.Vec3` (imported from `Undertow.Generation.StarSystem`, `double X/Y/Z`) — a DIFFERENT
    type than the kernel's `Float3` marker, and uses doubles not floats. Not a recognizer mismatch
    (recognition applies to the `[View]`-schema declaration type, not `SimFrame`'s internal type), but
    means Milestone 2.5's writer-glue needs an explicit narrowing `Vec3(double)→Vec3f(float)`
    conversion, not a 1:1 field copy.
  - **Confirmed no reusable Vec3f-equivalent C++ type exists anywhere in VIXEN's own libraries** — the
    only close precedent is undertow's own retiring `EmitViewContractHeader.cs`'s `Vec3f`, wrong repo.
    Decision: `TypedAccessorEmitter.cs` emits its own local `struct Vec3f{float x,y,z;}`.
  - **CRITICAL finding, independently confirmed by the Opus validator against real source across the
    entire C++ decode surface**: the plan's original "3 consecutive float cells" premise does NOT hold
    at the `ViewBlob`/`ViewStore`/`ViewWireReaderSoa` layer. `ViewKind` (`ViewBlob.h`) has exactly 5
    values (`Int`/`Float`/`Bool`/`String`/`ArrayOfStruct`) — no `Vector` case. `ViewValue`/`ViewCell`
    only carry single int/float/bool/string values. `ViewWireReaderSoa::Apply`'s decode loop switches
    over exactly those 5 kinds, one write per field per row — there is genuinely no way to represent 3
    floats under one field entry without a new `ViewKind::Vector` case. This means the real mechanism
    requires touching `ViewBlob.h`/`ViewStore.h`/`.cpp`/`ViewWireReaderSoa.cpp` (C++ engine substrate)
    PLUS `ViewBlobEmitter.cs` — NONE of which were named in the original 2-file scope.
  - **`ViewBlobEmitter.cs` is not optional to touch — it's a mandatory fix, not just an addition.** Its
    `KindEnum` switch currently falls through a Vector-kind field (whose `Scalar` is null) to
    `_ => "Int"` — a SILENT MISCOMPILE (wrong blob kind emitted), not a loud failure, if left untouched.
  - **Design decision: adopt (A), a genuine end-to-end `ViewKind::Vector`**, over (B) — decomposing into
    3 synthetic Float sub-fields at the blob layer — per the user's own standing "prefer pure/fully-
    correct solutions" rule. (B) would make `fieldCount` untruthful and is exactly the "flatten to 3
    floats" workaround `UndertowHud.cs`'s own header already rejected in writing. The Opus validator
    concurred explicitly against this rule.
  - **Scope boundary AMENDED** (see the section above) to name all 6 required files under design (A),
    not the original 2.
  - **Real infrastructure gap found and fixed during this milestone's own validation pass**: the
    Yeroket worktree (`feat/view-contract-inc5`) was 21 commits behind Yeroket `main` and did NOT
    contain the type-shape-recognizer-unification merge (`bb7c0ff4`) this whole milestone's premise
    depends on — `ViewFieldKind.Vector`/`VectorMarkerName`/`FieldShapeRecognizer.cs` did not exist in
    the worktree at all. Rebased the worktree onto `main` (clean, no conflicts), re-ran the full test
    suite post-rebase (79/79 pass), confirmed the Vector case now genuinely present.
  - No blockers remaining (both required actions — scope amendment + worktree rebase — completed).
  - **Opus validator (independent re-verification):** APPROVED. Independently confirmed the critical
    `ViewKind`-has-no-Vector-case finding across the ENTIRE C++ decode surface (not just spot-checked),
    confirmed the `ViewBlobEmitter.cs` silent-miscompile risk as a genuinely mandatory (not optional)
    fix, and explicitly weighed design (A) vs (B) against the user's own standing pure-solutions rule
    before concurring with (A). Flagged both required actions (scope amendment + worktree rebase) that
    have since been completed. Cleared to proceed to Milestone 2.4b.

- Milestone 2.4b (Task 2, build + prove Vector emission across all 6 amended-scope files): DONE
  · 2026-07-13 · Yeroket/VIXEN in-tree only, undertow untouched.
  - **Found substantial WIP already present, uncommitted, in both worktrees** at dispatch time —
    all 6 amended-scope files' code changes (matching the plan's design exactly) plus the new
    `VectorProof.cs` schema already existed unstaged. Verified each change against the plan/source
    before trusting it, then completed the remaining work: generating the actual headers via the
    CLI, writing the round-trip gtest, running full regression, and committing.
  - **`ViewBlob.h`**: added `ViewKind::Vector` to the enum, a `Vec3f{x,y,z}` payload struct (no
    math-library dependency — `ViewBlob.h` stays dependency-free), `ViewValue::Tag::Vector` +
    `ViewValue::Vec()` factory, `KindAcceptsValue` support.
  - **`ViewStore.h`/`.cpp`**: `ViewCell`/`ScalarSlot` both gained a `Vec3f vec` member;
    `SetScalar`/`AssignCell`/`ScalarSlotPtr` all gained a `Vector` case.
  - **`ViewWireReaderSoa.cpp`**: new decode case reading 3 consecutive F32s (x,y,z) into one
    `SetScalar` call carrying the assembled `Vec3f`.
  - **`ViewBlobEmitter.cs`**: `KindEnum` now maps `ViewFieldKind.Vector` to `"Vector"` (was
    silently falling through to `"Int"` — the mandatory miscompile fix Milestone 2.4a flagged).
    Left `EmitDataFile`'s `KindTag` helper WITHOUT a Vector case deliberately — confirmed
    `ViewBlobFile.cpp`'s runtime `.viewblob` text-format parser has no `"vector"` branch either,
    so emitting a `"vector"` tag there would just move the silent-failure point downstream to an
    unparseable datafile; that parser is outside this milestone's 6-file scope, so left as a
    named, explicit gap rather than silently introduced.
  - **`TypedAccessorEmitter.cs`**: emits a typed `Vec3f position() const` getter for a top-level
    Vector field, casting `ScalarSlotPtr` to `Vixen::RenderGraph::Vec3f*` and copying into a new,
    header-local `struct Vec3f { float x, y, z; };` (emitted once per header, only when the schema
    has a Vector field) — matches the plan's design exactly.
  - **`ViewWireFormat.cs`/`ViewWriterEmitter.cs`** (C# writer side): `EmitField` dispatches a
    Vector field to a new `EmitVector` (3 `WF32` calls, `this.<field>.X/Y/Z`); `ViewWriterEmitter`
    declares a local `public struct Vec3 { public float X, Y, Z; }` once per generated file and
    uses it as the data-holder's field type for a Vector column.
  - **`ViewVersionHash.cs`**: also extended (found already touched in the pre-existing WIP,
    correctly) — hashes a Vector field as `"vector:" + VectorMarkerName`, so the schema version
    changes if a Vector field's marker type ever changes.
  - **Generated + verified via the actual CLI** (not hand-written): ran
    `CodegenTool.dll --schema codegen/view-schemas --typed-accessor-cpp VectorProof --out-header ...`
    and `--view-blob VectorProof --out-header ... --out-datafile ...` — confirmed the emitted
    `VectorProofSection::position()` and `kVectorProofBlob` look exactly as designed (inspected
    output directly), then wrote those same generated files into
    `application/main/include/Generated/VectorProof.{typed,blob}.g.h` + `.viewblob`.
  - **Proof**: extended `test_typed_accessor_emitter.cpp` with a new
    `TEST(TypedAccessorEmitter, VectorFieldRoundTripsExactly)` — hand-built wire bytes (`UTVA` +
    version + fieldCount=1 + 3 F32s for x/y/z = 1.5, -2.25, 3.0, deliberately non-zero in every
    component) decoded via `ViewWireReaderSoa::Apply` into a `ViewStore(kVectorProofBlob, ...)`,
    then read back via the generated `Vixen::Views::VectorProofSection::position()`. **All 3
    components matched exactly** (`EXPECT_FLOAT_EQ`). Built via the scoped
    `build.bat build vixen-ninja test_typed_accessor_emitter` (existing source file extended, no
    new `.cpp` registered in CMakeLists, so the scoped incremental build is valid per the
    build-policy skill's "new source file" caveat — only new *files*, not new *includes* in an
    existing file, require `build.bat all`).
  - **Regression, all green**:
    - `test_typed_accessor_emitter.exe`: **2/2 PASS** (Milestone 2's
      `GeneratedAccessorsMatchRawStoreDecodedValues` unchanged + the new Vector test).
    - `test_view_wire_soa_roundtrip.exe`: **3/3 PASS**, unchanged (rebuilt+rerun explicitly to
      confirm, not merely assumed unaffected).
    - Yeroket `CodegenTool.Tests`: **first run 78/79 — one real, expected, non-regression
      failure**: `ViewModelTests.EmitToBufferBody_ThrowsForVectorField_FailsLoudNotSilently`
      (from the type-shape-recognizer-unification increment) asserted the OLD graceful-failure
      behavior (Vector emission throws `NotSupportedException`) — that test's OWN comment named
      this exact milestone as the intended follow-up that would close the gap. Updated it (renamed
      to `EmitToBufferBody_EmitsThreeFloatsForVectorField_DoesNotThrow`) to assert the NEW correct
      behavior (`Assert.DoesNotThrow` + asserts the emitted body contains the 3 `WF32(b,
      this.position.X/Y/Z)` calls). Re-ran: **79/79 PASS**, zero other failures.
  - **`SDFNodeGenerator.dll` non-deterministic-rebuild gotcha hit again** (as documented) —
    `git checkout --` it before committing, confirmed clean before the commit.
  - Design ambiguity resolved without new guidance needed: none — the pre-existing WIP + the
    plan's own Milestone 2.4a research fully specified the API shape (`Vec3f` payload,
    `ScalarSlotPtr` cast target, wire layout); no further judgment calls were required beyond the
    `KindTag`/`.viewblob`-parser scope boundary noted above (documented, not silently decided).
  - Commits: Yeroket `f4739f93` (`feat/view-contract-inc5`, 6 emitter files + updated
    `ViewModelTests.cs`); VIXEN `691d7a0e` (`feat/view-contract-inc5`, 4 C++ engine files +
    extended gtest + 4 new generated/schema files).
  - No blockers. Next: Milestone 2.5 (writer-side wiring for the 4 Hud-family sections; Bodies now
    includable per this milestone's unblock, per the Milestone Map's own note) or Milestone 3
    (undertow reader cutover + live-gate), per whichever the controller sequences next.
  - **Opus validator (independent re-verification):** APPROVED. Read all 6 changed files in full and
    confirmed every claimed change is real. Mechanically traced the full round-trip against real
    source (cast target matches the actual stored type, baked field index correctly resolves via the
    public `ScalarSlotPtr`, no runtime name lookup introduced) AND independently regenerated all 3
    `VectorProof` headers via the real CLI, confirming byte-identical to the committed files —
    stronger than just reading assertions. Independently re-ran Yeroket's test suite (79/79) and
    confirmed the regression/expected-failure-then-fix story is genuine (the old throw-test's own
    comment named this exact milestone as its intended closer; the new test is strengthened, not
    weakened). Confirmed the `KindTag`/`ViewBlobFile.cpp` scope-boundary decision is sound and
    genuinely outside the named 6-file scope. Confirmed Bodies remains undeclared, no dll noise.
    Flagged one forward-note (not a blocker): `Mat4` also maps to `Vector` but `ViewValue::Vec` is
    fixed at 3 floats — already documented in `ViewBlob.h`'s own comment as needing a future distinct
    `ViewKind` for non-3-component vector shapes. Cleared to proceed to Milestone 2.5/3.
