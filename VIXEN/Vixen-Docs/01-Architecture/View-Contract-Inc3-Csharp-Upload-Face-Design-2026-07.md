---
title: View Contract Inc-3 — C# Data-Upload Face (Design)
status: COMPLETE-WITH-KNOWN-GAPS — Inc3's C# upload face and C++ UTVA round-trip shipped in the
current engine history (`06c9e31b`, `a9204e21`, `3b61efe2`, close-out `3ad08519`); the live gate
remains deferred on the documented external engine-side blocker.
created: 2026-07-07
parent: Renderer-Agnostic-View-Contract-Design-2026-07.md
tags: [view-contract, codegen, csharp, marshaling, soa, aos, undertow-subsume]
---

# View Contract Inc-3 — C# Data-Upload Face

**Goal (one sentence):** Generate a typed C# data-upload face from the `[View]` schema — field-setters that
marshal a consumer's view data into an AoS wire buffer stamped with the schema-version hash — and prove it
round-trips through a generic C++ blob-guided reader into the Inc-2b `ViewStore`, so the C# upload face and
the renderer face are shown to agree across the seam **without touching the GPU or the undertow consumer repo**.

This is **face 3 of 4** in the renderer-agnostic View Contract program
(`Renderer-Agnostic-View-Contract-Design-2026-07.md` §3, §6). It is designed to **subsume** undertow's
hand-versioned view codegen (§6.5 of that doc); the actual undertow migration is deferred to Inc-5+ (D1).

---

## 1. Locked decisions

| # | Decision | Choice |
|---|----------|--------|
| D1 | **Scope reach into undertow** | **In-tree C# face + stub consumer.** Build the generated C# setter API in VIXEN/Yeroket; prove it against a small in-tree C# stub consumer + a C++ round-trip. **Do NOT modify undertow.** The undertow migration (regenerate `ViewWriter.g.cs`, retire `FormatVersion`) is Inc-5+. Keeps Inc-3 self-provable like every prior increment; honors [[vixen-owns-content-format-not-consumer]]. |
| D2 | **API shape** | **Typed by-field setters are the primary API**, and the SoA-transpose is **factored** (a shared `ViewLayout` core) so a future projection-writer (undertow-style) and a future SoA target reuse it. |
| D3 | **Layout + kind model** | A **`[ViewSection(Layout=Soa\|Aos)]` per-section attribute** (attaches to struct-array fields; matches undertow's mixed scalar+collection reality). The scalar **kind enum is already first-class** in the model (`ViewScalar`/`ViewFieldKind`) — reused, not re-created. Inc-3's emitter ships the **AoS** path only; `Soa` is a valid, model-recorded value that the emitter **rejects with a clear "Inc-5+" error** (the abstraction is real contract now; only the AoS emit ships). |
| D4 | **Version in the wire** | The generated writer **stamps the Inc-2b `ViewVersionHash` value into the wire header**, replacing undertow's hand-bumped `FormatVersion`. The renderer's blob already carries the matching hash; a mismatch is the hard boundary error (§5.4/§8 of the parent doc). Single source, cannot go stale. |
| D5 | **Proof gate** | **C# setters → `UTVA` AoS wire → C++ `ViewWireReader` → `ViewStore` → assert field values read back**, plus version-match (passes) and perturbed-mismatch (hard boundary error). No GPU anchor (that re-proves Inc-2b's blob→GPU path; the new claim is the C#↔C++ seam, which is GPU-independent). |
| D6 | **Setter ergonomics** | Plain **public fields + `List<Row>`** (`view.Tick = 42; view.Factions.Add(new FactionRow{…})`), not property setters with change-tracking. Dirtying is the renderer's job (parent §4.2). |
| D7 | **AoS wire format** | A **fresh, minimal `UTVA` AoS format** (not undertow's `UTVW` SoA framing). AoS composes with `ViewStore` with zero un-transposer and keeps the proof format visibly separate from the eventual migration format. |

---

## 2. Architecture & the seam

One `[View]` schema drives two independently-generated faces that meet at a runtime wire buffer:

```
[View] C# schema  ──(one source)──┐
   │ (Yeroket emitters)           │
   ▼                              ▼
ViewWriterEmitter (NEW)      ViewBlobEmitter (Inc-2b, exists)
   │                              │
   ▼                              ▼
 Hud.view.g.cs               Hud.blob.g.h / hud.viewblob
 (typed setters +            (name+kind description +
  AoS transpose +             version hash)
  version-hash header)             │
   │                               │
   ▼   consumer sets fields        ▼
 HudViewWriter ──ToBuffer()──▶ UTVA wire ──▶ ViewWireReader (NEW, C++, generic)
                                              │  blob-guided walk, version-checked
                                              ▼
                                         ViewStore (Inc-2b, exists) ──▶ live RmlUi data model
```

**New vs. reused:**

- **New (Yeroket C#):** `ViewWriterEmitter` + a shared `ViewLayout` serializer core (the AoS transpose, factored per
  D2); the `ViewSectionAttribute` + `ViewLayout` enum; a `Layout` property on `ViewField`.
- **New (VIXEN C++):** ONE generic `ViewWireReader` — reads a `UTVA` AoS wire *guided by the Inc-2b `ViewBlob`*
  (declared field order + kind), driving `ViewStore`'s existing setters. Blob-driven, not per-schema codegen.
- **Reused unchanged (Inc-2b):** `ViewBlob`, `ViewStore`, `ViewValue`/`ViewKind`, `BlobView`, `ViewVersionHash`,
  the generated `Hud.blob.g.h`. **Nothing in Inc-2b changes.**

**The join / correctness argument.** The C# writer and the C++ reader share no code, but both derive field order +
kinds from the **same `[View]` schema** — the writer from `ViewModel` (`ViewStruct`/`ViewField`), the reader from the
`ViewBlob` (which `ViewBlobEmitter` generates from that same `ViewModel`). The version hash is the runtime guard that
both were generated from the same schema. AoS layout makes the reader a straight declared-order walk into by-field
setters — no offset/stride math, no un-transposer.

**Boundary discipline.** Entirely in-tree: VIXEN worktree (C++ + generated headers + CMake) and the Yeroket branch
(C# emitters + tests). No undertow files touched. The "stub consumer" is the C++ round-trip test's known-value wire
(and, on the C# side, the direct field-setter calls in the writer test) — they stand in for undertow's `SimFrame`
projection, which is subsumed in Inc-5+.

---

## 3. The C# data-upload face

### 3.1 The `[ViewSection]` layout attribute + kind

```csharp
public enum ViewLayout { Aos, Soa }

[AttributeUsage(AttributeTargets.Field)]
public sealed class ViewSectionAttribute : Attribute
{
    public ViewLayout Layout { get; set; } = ViewLayout.Aos;
}
```

- **Scalar / single-row fields** (the `Hud` top-level scalars) have no array — layout is moot, they serialize
  identically either way.
- **Struct-array fields** (`Factions`, `Events`) may carry `[ViewSection(Layout=…)]`; default `Aos`. The in-tree
  proof view uses `Aos` throughout (self-provable). `Soa` is a valid value the **model records** but the **Inc-3
  emitter rejects** it with a clear `NotSupportedException("SoA emit is Inc-5+; use Aos for now")` — the attribute is
  real contract now; only the AoS *emitter path* ships.

### 3.2 Model extension (`ViewModel.cs`)

`ViewField` gains `public ViewLayout Layout { get; }` (default `Aos`), populated in `ViewModelBuilder.Classify` by
reading a `ViewSectionAttribute` off `StructArray` fields (via `IFieldSymbol.GetAttributes()`). The **kind enum is
already first-class** — `ViewScalar{Int,Float,Bool,String}` + `ViewFieldKind{Scalar,Struct,StructArray}`
(`ViewModel.cs:8-9`) — so Inc-3 adds no new kind enum; it carries `Layout` alongside the existing kind, and the C++
reader dispatches on the existing `ViewKind` from the blob.

### 3.3 The generated writer (`Hud.view.g.cs`)

`ViewWriterEmitter` emits a `<Model>ViewWriter` class from the `ViewStruct`:

The shape below is **the real `Hud` schema** (from `Hud.blob.g.h`, version `0x55D27B8C`), not an illustrative
sketch — it is used verbatim as the proof view because it already exercises **every kind**: `Int`, `String`,
`ArrayOfStruct`, and `Bool` + `Float` inside the array rows. No synthetic proof view is needed.

```csharp
// generated from [View] Hud  (fields + kinds match Hud.blob.g.h exactly, declared order)
public sealed partial class HudViewWriter
{
    public const uint SchemaVersion = 0x55D27B8C;   // == ViewVersionHash.Compute(Hud)

    public int    Tick;
    public int    BodyCount;
    public string ActiveLensName;
    public int    ActiveLensCount;

    // one nested row type + List per struct-array field, in declared order:
    public struct FactionRow {
        public string Name; public float Grievance;
        public bool Focused; public bool Known; public bool InLens; public bool RecentChanged;
    }
    public readonly List<FactionRow> Factions = new();
    public struct EventRow { public string Kind; public int Tick; }
    public readonly List<EventRow> Events = new();

    public byte[] ToBuffer();   // UTVA AoS wire + version header, via the shared ViewLayout core
}
```

- Setter ergonomics (D6): plain public fields / `List<Row>` — `view.Tick = 42; view.Factions.Add(new FactionRow{
  Name="Reds", Grievance=0.7f, Focused=true });`. Indexed mutation (`view.Factions[i]`) also works via the list.
- **The field set + kinds are pinned by `Hud.blob.g.h`** — the C# writer's fields must match the blob's
  `kHud_fields` / `kHudFaction_fields` / `kHudEvent_fields` names, kinds, and declared order exactly (the round-trip
  and the version hash both depend on this identity). The C# `[View]` source struct the emitter reads from is
  reconciled to that schema in the plan (the C# `Hud` `[View]` type already exists from Inc-1/2b as the blob's
  source; Inc-3 adds the writer emitter over the same type).

### 3.4 The shared `ViewLayout` core

`ToBuffer()` delegates to a `ViewLayout` serializer (own file) that walks the `ViewStruct` in declared order and
writes the **`UTVA` AoS wire** (§4). Factoring it is the D2 hedge: a future SoA target is a second method
(`WriteSoa`) on this core, and a future projection-writer reuses the same per-field encoders. **Inc-3 ships only
`WriteAos`.**

---

## 4. The `UTVA` AoS wire format

A fresh, minimal AoS format for the round-trip proof — deliberately **not** undertow's `UTVW` SoA framing (that is
the SoA target, Inc-5+). It only needs to carry the `[View]` schema's kinds losslessly and be trivially walkable
blob-guided. All multi-byte values **little-endian** (written explicitly by the emitter, not host-dependent).

### 4.1 Header (fixed 12 bytes)

| Offset | Size | Field | Value |
|---|---|---|---|
| 0 | 4 | magic | `UTVA` (`'U','T','V','A'` — **A** for AoS view, distinct from undertow's `UTVW`) |
| 4 | 4 | version hash | `SchemaVersion` (LE `uint32`, == `ViewVersionHash.Compute`) |
| 8 | 4 | top-field count | LE `uint32`, == `blob.fields.size()` (structural sanity check) |

### 4.2 Body — top-level fields in declared order

No field ids/names on the wire — **declared order is the identity** (the blob supplies names). Per kind:

- **Int** → 4 bytes LE `int32`.
- **Float** → 4 bytes LE IEEE-754.
- **Bool** → 1 byte (`0`/`1`).
- **String** → 4-byte LE `uint32` byte-length, then that many UTF-8 bytes.
- **ArrayOfStruct** → 4-byte LE `uint32` row count, then **each row** = its element scalar fields in declared order
  (recursively, same per-kind encoding). AoS = rows contiguous, one full row after another.

The format is **self-delimiting given the blob** — every kind's length is either fixed or length-prefixed — so the
reader never guesses.

### 4.3 Why `UTVA`, not `UTVW`

- **AoS is what `ViewStore` ingests** — rows contiguous → `ResizeArray(n)` then per-cell `RowHandle::Set` — a
  straight walk. No column offset arrays, no un-transpose.
- **Blob-guided** — the reader carries zero schema knowledge; it reads what the blob's kinds tell it to, in order.
  Same abstraction that makes `BlobView` generic.
- **Distinct magic** makes the proof format unmistakably different from undertow's `UTVW`. When undertow migrates
  (Inc-5+), `ViewLayout.WriteSoa` produces `UTVW`-compatible framing; this `UTVA` AoS path stays as the AoS option.

---

## 5. The C++ reader & boundary guard

### 5.1 The component

One new generic component in `libraries/RenderGraph` (`Ui/ViewWireReader.h`/`.cpp`), sibling to `ViewStore`/
`BlobView`. **Blob-guided and schema-agnostic** — no per-view code, no codegen output:

```cpp
namespace Vixen::RenderGraph {

// Reads a UTVA AoS wire buffer into a ViewStore, guided by the store's ViewBlob
// (declared field order + kinds). Version-checked at entry. Returns false + logs on
// any mismatch/malformed input; never partially-writes on failure, never throws.
class ViewWireReader {
public:
    static bool Apply(std::span<const std::byte> wire, ViewStore& store);
};

}  // namespace Vixen::RenderGraph
```

### 5.2 The walk

`Apply` (1) validates the header, (2) checks the version hash (§5.3), (3) walks `store.Blob().fields` in declared
order, decoding each per its `ViewKind` exactly as §4 defines, driving the existing `ViewStore` setters:

- `Int/Float/Bool/String` → `store.SetScalar(field.name, ViewValue::I/F/B/S(decoded))`.
- `ArrayOfStruct` → read row count → `auto h = store.ResizeArray(field.name, rowCount)` → for each row, for each
  `field.elem[j]`, decode per its kind → `h.Set(row, field.elem[j].name, ViewValue…)`.

Because it drives the **same validated by-field setters** `BlobView` binds against, a successful `Apply` leaves
`store` in exactly the state a native by-field consumer would have produced — that is the round-trip identity the
proof asserts.

### 5.3 Version guard (the crux — parent §5.4)

Before decoding the body, `Apply` compares the wire's header hash against `store.Version()` (the consumer-version the
`ViewStore` was constructed with, sourced from the blob). This is the automatic replacement for undertow's
hand-bumped `FormatVersion`:

- **Match** → decode proceeds.
- **Mismatch** → `LT_ERROR("ViewWireReader: schema version mismatch (wire=0x%08X store=0x%08X) — skipping")`,
  return `false`, **store untouched** (visibly empty view, never wrong-byte marshaling). Mirrors parent §8 +
  Inc-2b's `ViewBlobFile` "surface, don't swallow."

### 5.4 Malformed-input handling (hard-fail, like Inc-2b)

Every decode step bounds-checks against the wire length before reading. Any of: truncated header, wrong magic,
top-field-count ≠ `blob.fields.size()`, a length-prefix that overruns the buffer, trailing garbage → `LT_ERROR` +
`return false`, no partial write. Same discipline as `ViewBlobFile::Parse`. The reader takes the wire by
`std::span`, so it never over-reads.

### 5.5 What it does NOT do

No SoA path (the `UTVA` reader is AoS-only; the SoA reader is the Inc-5+ branch). No offset/stride math (the blob
has none). No RmlUi calls (it fills `ViewStore`; `BlobView::Register` + `ViewStore::Flush` drive the model,
unchanged from Inc-2b).

---

## 6. Proof gate & testing

### 6.1 The claim

The C# upload face and the renderer face, both generated from one `[View]` schema, **agree across the seam** — C#
setters → `UTVA` wire → C++ reader → `ViewStore` reads back the exact field values — and the version hash catches a
schema mismatch as a hard boundary error.

### 6.2 C# side (NUnit, Yeroket `CodegenTool~/Tests/`)

Sibling to `ViewBlobEmitterTests`/`ViewVersionHashTests`.

- **`ViewWriterEmitterTests`** (offline codegen): compile the `[View] Hud` schema through `ViewWriterEmitter`;
  assert the generated `HudViewWriter` (a) exposes the expected typed fields + row types, (b)
  `SchemaVersion == ViewVersionHash.Compute(Hud)` (assert equality to the **computed** value — no magic literal),
  (c) `ToBuffer()` on a known input produces the expected `UTVA` bytes: header (`UTVA` + hash + field-count) then
  declared-order body. The golden byte-expectation is built **explicitly in the test** (documents the format), not a
  captured blob.
- **`ViewSectionLayoutTests`**: a schema with `[ViewSection(Layout=Soa)]` throws the "Inc-5+" `NotSupportedException`
  from the emitter (proves the attribute is wired to the model + the deferral is enforced, not silently ignored). An
  `Aos` (or unattributed) section emits normally.

### 6.3 C++ side (gtest, `libraries/RenderGraph/tests/`) — the cross-seam round-trip (the new claim)

- **`test_view_wire_roundtrip`**: build the `Hud` `ViewBlob` (reuse the generated `Hud.blob.g.h` from Inc-2b) →
  construct a `ViewStore` → feed it a `UTVA` wire buffer with **known values** (`Tick=42`, `BodyCount=9`,
  `Paused=true`, two faction rows, one event row) → `ViewWireReader::Apply` → assert `true` → assert every scalar
  slot + every array row cell reads back the exact value via `ViewStore`'s accessors. **The wire buffer is the
  byte-for-byte twin of what the C# `ToBuffer()` golden produces.**
- **`test_view_wire_version_mismatch`**: same wire but perturb the header hash → `Apply` returns `false`, logs, and
  the `ViewStore` is untouched (all slots still default) — the §5.3/parent §8 hard boundary error.
- **`test_view_wire_malformed`**: truncated body / wrong magic / field-count mismatch / overrunning string length →
  each returns `false`, no partial write.

### 6.4 The cross-language tie (why this is non-vacuous)

The C# test asserts `ToBuffer()` on a known input emits byte sequence **B**; the C++ test asserts `Apply(B)`
reconstructs the exact same input field values. **B is the same canonical `UTVA` byte layout** — the plan pins the
layout (header + declared-order body for the `Hud` schema), and both tests assert against that identical literal
sequence for the identical known input. A drift on either side (C# encodes differently, or C++ decodes differently)
fails one of the two — they cannot silently disagree. No GPU, no undertow, no shared runtime.

### 6.5 No-regression

All Inc-2b tests stay green (`test_view_blob_equiv`, `ViewBlobEmitterTests`, `ViewVersionHashTests`, etc.) — Inc-3
adds **beside** them and changes **nothing** in Inc-2b. The real-GPU HUD anchor is **not** re-run (it re-proves the
blob→GPU path Inc-2b owns; the new claim is the C#↔C++ seam, which is GPU-independent).

### 6.6 Drift-guards (CMake, following Inc-2b's KI-015 pattern)

The generated `Hud.view.g.cs` gets a `view_hud_writer_check` / `view_hud_writer_regen` target pair, **wsl.exe-bridged
like the existing five** (`_CODEGEN_RUNNER` + `_codegen_to_wsl_path` helpers), so a schema change that isn't
regenerated fails the build cross-OS.

---

## 7. Build & platform notes

- **Windows-side build** via the `vixen-ninja` preset / `build.bat` (the strong default for this repo); poll long
  builds on a foreground interval, never blind-wait; never overlap same-target builds.
- **Yeroket tests** run from `CodegenTool~/Tests/` (running from `CodegenTool~/` discovers 0 tests — a false green);
  they are **NUnit** (the csproj has no xUnit).
- **Cross-OS drift-guards**: the `wsl.exe` bridge routes the Yeroket tool (a WSL-only .NET ELF) from the Windows
  configure — `find_program`/`EXISTS` check presence, not runnability (KI-015).
- **`SDFNodeGenerator.dll` / bin / obj** are never committed (rebuild non-deterministically) — `git checkout --` them
  if they appear staged.

---

## 8. Out of scope (this increment / deferred within the program)

- **Inc-3 does NOT:** modify the undertow repo (Inc-5+); emit the SoA (`UTVW`) writer or reader (the `Soa` layout is
  recorded but its emit path throws "Inc-5+"); build any projection-driven writer (the `Source`-expression form —
  factored-for but not shipped); re-run the real-GPU HUD anchor; touch Inc-2b's `ViewBlob`/`ViewStore`/`BlobView`;
  build face 2 (authoring reference) or face 4 (view→action).
- **The program still owes (later slices):** SoA layout emit + reader (Inc-5-adjacent, needed for the undertow wire);
  the undertow migration proper — `ViewSchema.cs` sections/columns/ids/`Source` → `[View]` schemas, regenerate
  `view_contract.h` + `ViewWriter.g.cs`, preserve/regenerate the `UTVW`/`UTFB` wire, and the per-body render-recipe
  columns (parent §6.5); face 4 (the AppFlow convergence, parent §7).

---

## 9. Ground-truth references (read before planning)

**Inc-2b (VIXEN) — reused unchanged:**
- `libraries/RenderGraph/include/Ui/ViewBlob.h` — `ViewKind`, `ViewFieldDesc` (name+kind+elem, **no offsets**),
  `ViewBlob` (model + fields + version), `ViewValue` (`I/F/B/S` factories), `KindAcceptsValue`.
- `libraries/RenderGraph/include/Ui/ViewStore.h` — `ViewCell`, `ViewRow`, `ViewStore(blob, consumerVersion)`,
  `SetScalar`, `ResizeArray`→`RowHandle::Set`, `Version()`, `Blob()`. **The reader drives these.**
- `libraries/RenderGraph/include/Ui/ViewBlobFile.h` — the hard-fail+`LT_ERROR` parse discipline to mirror.
- `libraries/RenderGraph/include/Ui/BlobView.h` — the generic host (unchanged; drives the model from `ViewStore`).
- `application/main/include/Generated/Hud.blob.g.h` — the `Hud` `ViewBlob` the round-trip reuses (version
  `0x55D27B8C`).
- `libraries/RenderGraph/tests/test_view_blob_equiv.cpp` — the Inc-2b proof + the gtest wiring pattern to mirror.
- `codegen/CMakeLists.txt` — the five existing `*_check`/`*_regen` wsl.exe-bridged target pairs to copy for
  `view_hud_writer_check`.

**Yeroket — the emitter neighborhood:**
- `$KF/SourceGenerator~/Transpiler/ViewModel.cs` — `ViewStruct`/`ViewField`/`ViewScalar`/`ViewFieldKind` +
  `ViewModelBuilder.Classify` (extend with `Layout`).
- `$KF/SourceGenerator~/Transpiler/ViewVersionHash.cs` — `ViewVersionHash.Compute(ViewStruct)` (the writer stamps
  this value; the test asserts equality to it).
- `$KF/SourceGenerator~/Transpiler/ViewBlobEmitter.cs` — sibling emitter shape to follow for `ViewWriterEmitter`.
- `$KF/CodegenTool~/Program.cs` — the CLI branch structure (`--view-blob`) to extend with a `--view-writer` branch.
- `$KF/CodegenTool~/Tests/ViewBlobEmitterTests.cs`, `ViewVersionHashTests.cs` — the NUnit test shape to follow.
  (`$KF = /home/liory/Github/Yeroket-Fantasy/Packages/com.yeroket.utility.kernel-framework`)

**Program docs:**
- `Renderer-Agnostic-View-Contract-Design-2026-07.md` — parent program (§3 four faces, §5.4 version, §6 slices,
  §6.5 the undertow system this subsumes, §8 error handling).
- `View-Contract-Inc2b-Reflection-Blob-Design-2026-07.md` + `...-Plan-2026-07.md` — the blob/marshaler this builds on.
- Undertow target (read-only, NOT modified this increment): `undertow/core/src/Undertow.View/ViewSchema.cs`,
  `undertow/core/src/Undertow.Authoring.Codegen/{ViewWriterGenerator,EmitViewWriter}.cs`.
