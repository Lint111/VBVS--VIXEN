---
title: View Contract Inc-2b — Reflection Blob + Generic Dynamic Marshaler
aliases: [View Contract Inc-2b, Reflection Blob, BlobView, ViewStore]
tags: [architecture, view-contract, codegen, rmlui, ui]
created: 2026-07-07
related:
  - "[[Renderer-Agnostic-View-Contract-Design-2026-07]]"
  - "[[View-Contract-Codegen-Design-2026-07]]"
---

# View Contract Inc-2b — Reflection Blob + Generic Dynamic Marshaler

**Status:** Design approved 2026-07-07. Fourth increment of the View Contract program (Inc-1 data-side emitter + Inc-2 renderer-agnostic host both shipped; VIXEN side pushed to origin/main). This spec covers the **blob path (face-1 blob delivery)** — the crux increment of `Renderer-Agnostic-View-Contract-Design-2026-07.md` §5.

**Goal (one sentence):** Make the renderer host *any* view from a runtime **description** (not just C++-compiled ones), so a consumer with **zero engine C++** can drive the HUD — proven in-tree against VIXEN's own HUD, rendering identically to Inc-2's native `HudView` path.

**Architecture:** One `[View]` schema generates a **reflection blob** (kind-catalogue field descriptors + a schema-version hash), delivered two ways (a generated `constexpr` C++ header **and** a runtime `.viewblob` data file), both producing the same in-memory `ViewBlob` struct. A new engine-side generic host `BlobView : IView` walks the blob to build RmlUi's dynamic data-model definitions at `Register` time and marshals consumer data into a generic typed `ViewStore`. The existing `UIRenderNode` (already generic via `IView`) and native `HudView` fast-path are untouched.

**Relationship to prior docs.** This is `Renderer-Agnostic-View-Contract-Design-2026-07.md` §5.2 (blob path) + §5.4 (generated version), specified in full. §5.1 (native fast-path) shipped in Inc-2. The old `View-Contract-Codegen-Design-2026-07.md` framed this as "later increments"; this doc is the concrete spec for the blob slice.

---

## 1. Motivation & the honest RmlUi ground truth

Inc-2 made the renderer generic **for C++ consumers**: `UIRenderNode` hosts any `IView`, and VIXEN's app implements `HudView : IView` calling the generated compile-time `BindHudModel`. But that path still requires the consumer to compile C++ (the `HudBind` pointer bundle, the generated row structs). A modding consumer, or the C# sim in undertow, ships no engine C++ — it needs to hand the renderer a **description** and **data**, and have the engine build the live model generically.

**Ground truth that shaped this design (from reading the vendored RmlUi source, `DataVariable.h`/`DataTypeRegister.h`/`DataModelHandle.h`):**

- RmlUi's **leaf** value definitions are **compile-time C++ templates**: `ScalarDefinition<T>::Get` does `*static_cast<const T*>(ptr)`; `ArrayDefinition<Container>` calls `Container::size()/begin()` and `std::advance`. You **cannot** instantiate `ScalarDefinition<T>` for a `T` unknown at compile time, and these read **real C++ types** (`std::string`, `std::vector<Row>`) — not raw bytes at an offset.
- Only the **assembly** is genuinely runtime: `StructDefinition::AddMember(name, UniquePtr<VariableDefinition>)`, `DataTypeRegister::RegisterDefinition(FamilyId, def)`, and `DataModelConstructor::BindCustomDataVariable(name, DataVariable(def, ptr))` — all `RMLUICORE_API`-public.

**Consequence (a locked design decision):** the blob describes a view using the `[View]` type system's **finite kind catalogue** (`int/float/bool/string` scalars; `vector`-of-struct arrays whose members are those scalars). The engine has a fixed `kind → ScalarDefinition<T>` dispatch and marshals into a **generic typed `ViewStore`** (real typed slots, not a byte buffer). This covers 100% of what `[View]` can express today and is honest to RmlUi's typed model. It is **not** an arbitrary-C++-layout mechanism — `[View]` cannot express those anyway. The spec's earlier "`DataVariable(base+offset)` into raw storage" mental model is superseded: there are **no byte offsets or strides** in this design; dispatch is on `kind` to a typed slot.

---

## 2. Unit boundaries

One `[View]` schema → these units, each independently testable (most without a GPU):

| Unit | Owner (repo/lib) | Responsibility |
|---|---|---|
| **`ViewBlob`** (struct) | VIXEN `RenderGraph` | In-memory description: model name, ordered field descriptors (name + kind + array element layout), schema-version hash. The shared contract both delivery front-ends produce; the generic host is written against `const ViewBlob&`. |
| **`ViewBlobEmitter`** (`--view-blob`) | Yeroket tool | Sibling of `RmlDataModelEmitter`. From a `ViewStruct`, emits **(a)** `Hud.blob.g.h` (a `constexpr ViewBlob`) and **(b)** `hud.viewblob` (runtime data file). Both carry the same generated version hash from a shared helper. |
| **`ViewBlobFile`** (parser) | VIXEN `RenderGraph` | Parses a `.viewblob` file → `ViewBlob` (owning its backing storage). All failures hard + logged → no blob. |
| **`ViewStore`** | VIXEN `RenderGraph` | Generic typed storage (typed slots per field + row-vectors) + the variant-based by-field setter API. Blob-validated by field name + kind. |
| **`BlobView : IView`** | VIXEN `RenderGraph` | The generic host. `Register(c)` walks the `ViewBlob`, builds dynamic RmlUi definitions, enforces the version guard, binds into its owned `ViewStore`. |

**Flow:** `constexpr ViewBlob` (header) **or** `parse(.viewblob)` → `ViewBlob` → `BlobView(blob)` → `UIRenderNode` calls `view_->Register(c)` (unchanged) → consumer pushes via `ViewStore` setters → `Flush` dirties vars.

**Untouched:** `UIRenderNode` (already generic via `IView` since Inc-2), native `HudView`/`BindHudModel` (stays the compiled fast-path), the RML document/RCSS. The blob path is purely *a second `IView` implementation* plus its codegen.

---

## 3. The `ViewBlob` contract & kind catalogue

The kind catalogue is exactly what `[View]` expresses today:

```cpp
enum class ViewKind : uint8_t { Int, Float, Bool, String, ArrayOfStruct };
```

- Scalars map 1:1 to `ScalarDefinition<int | float | bool | Rml::String>`.
- `ArrayOfStruct` maps to `ArrayDefinition<std::vector<RowStore>>` whose element is a `StructDefinition` built from the element's own scalar fields.
- **No** standalone nested-struct (non-array) kind — the live `[View]` schema has none; the untested `ViewFieldKind.Struct` path from Inc-1's notes stays out of scope (YAGNI). If a single-struct view field is ever authored, it is a follow-up increment.

Descriptor structs (POD, `constexpr`-friendly so the header form is a compile-time constant):

```cpp
struct ViewFieldDesc {
    std::string_view name;
    ViewKind         kind;
    std::span<const ViewFieldDesc> elem;  // element scalar fields; empty unless ArrayOfStruct
};
struct ViewBlob {
    std::string_view               model;    // e.g. "hud"
    std::span<const ViewFieldDesc> fields;   // top-level fields, declared order
    uint32_t                       version;  // generated schema hash (§5)
};
```

**Why `string_view`/`span`:** the header form is a `constexpr ViewBlob` over `constexpr` field arrays — zero runtime construction, zero allocation. The `.viewblob` parser fills the *same* struct (parser owns backing storage, hands out views into it). The generic host never knows which front-end produced the blob — delivery is a swappable front-end (the property that lets Inc-2b build both forms over one host).

**Order is contract.** Field order in `fields[]` and within `elem[]` is significant: it is part of the version hash and the order the content-hash proof walks. Codegen emits in schema-declaration order; both front-ends preserve it.

---

## 4. The generic `BlobView` host & dynamic registration

```cpp
class BlobView final : public Vixen::RenderGraph::IView {
public:
    explicit BlobView(const ViewBlob& blob, std::string documentPath);
    const char* ModelName()   const override;  // blob.model
    const char* DocumentPath() const override;  // ctor arg (consumer picks the .rml)
    void Register(Rml::DataModelConstructor& c) override;  // the dynamic build
    ViewStore& Store();                          // consumer pushes data here
private:
    const ViewBlob&       blob_;
    ViewStore             store_;   // typed storage sized from the blob
    Rml::DataModelHandle  model_;
};
```

**`Register(c)` — the dynamic assembly** (mirrors `BindHudModel` statically, built from the blob):

1. **Version guard first.** If `store_.Version()` (consumer-side version) ≠ `blob_.version` (engine-side), log a hard `LT_ERROR` ("View '<model>' version mismatch: engine <a> vs consumer <b> — skipping register") and **return without registering** — model stays empty; HUD renders nothing, never garbage.
2. For each `ArrayOfStruct` field: build a `StructDefinition` for the element — one `AddMember(elemFieldName, MakeScalarDef(kind))` per element scalar in `elem[]` order — then `c.GetDataTypeRegister()->RegisterDefinition(familyId, std::move(structDef))`; build an `ArrayDefinition` over the row-vector and register it.
3. For each top-level field: `c.BindCustomDataVariable(name, DataVariable(def, store_.SlotPtr(name)))`.
4. `model_ = c.GetModelHandle();`

**`MakeScalarDef(ViewKind)`** — the only place the finite catalogue is enumerated:

```cpp
switch (kind) {
    case ViewKind::Int:    return Rml::MakeUnique<Rml::ScalarDefinition<int>>();
    case ViewKind::Float:  return Rml::MakeUnique<Rml::ScalarDefinition<float>>();
    case ViewKind::Bool:   return Rml::MakeUnique<Rml::ScalarDefinition<bool>>();
    case ViewKind::String: return Rml::MakeUnique<Rml::ScalarDefinition<Rml::String>>();
}
```

**Lifetime contract (the one real hazard, handled):** `RegisterDefinition`/`AddMember` take `UniquePtr` ownership — RmlUi's `DataTypeRegister` owns the definitions. The backing pointers passed to `BindCustomDataVariable` point into `store_`, which `BlobView` owns and which outlives the model — the same contract `HudView` honors with its member storage. `BlobView` outlives the `UIRenderNode` context by construction (the consumer holds it).

**Family IDs (the one RmlUi-internals touchpoint, approved).** RmlUi keys definitions by a `FamilyId`; the static path derives it from the C++ type, which the dynamic path lacks. `BlobView` allocates a **stable synthetic `FamilyId`** per blob struct (a monotonic counter or a hash of the element's field-name list). This is pinned against RmlUi's `FamilyId` allocation in the implementation plan; every other call is public API.

---

## 5. `ViewStore` marshaling & the schema-version hash

**`ViewStore` — typed storage + by-field setter API.** Sized from the blob at construction (one typed slot per top-level field; one row-vector per `ArrayOfStruct`):

```cpp
class ViewStore {
public:
    explicit ViewStore(const ViewBlob& blob);   // allocates typed slots from the blob
    void SetScalar(std::string_view field, ViewValue v);       // top-level scalar
    RowHandle ResizeArray(std::string_view field, size_t n);   // array-of-struct
    // RowHandle::Set(size_t rowIndex, std::string_view elemField, ViewValue v)
    void Flush(Rml::DataModelHandle& model);   // DirtyVariable every changed field
    uint32_t Version() const;                  // consumer-side version
    void*    SlotPtr(std::string_view field);  // the pointer BlobView binds
};
```

- `ViewValue` is a small tagged union (`int/float/bool/Rml::String`). Every `SetScalar`/`Set` **validates name + kind against the blob** — a wrong name or type mismatch is a logged error, not a silent write.
- Typed slots are real C++ objects (`int`, `Rml::String`, `std::vector<RowStore>`) so `BlobView`'s `ScalarDefinition<T>`/`ArrayDefinition` read them correctly — no POD/offset games.
- `Flush(model)` dirties changed vars (the dynamic equivalent of `HudView`'s `DirtyVariable` list), ending a push cycle by telling RmlUi to re-render.

**The schema-version hash (`Renderer-Agnostic` §5.4 centerpiece).** A `uint32_t` over the view's **structure** only: `H(model name; per field: name + kind; for arrays: element field names + kinds; all in declared order)`. Data/values are **not** hashed — only the shape.

- **Deterministic & generated:** codegen computes it in a shared C# helper `ViewVersionHash.Compute(ViewStruct)` and emits the *same* value into both `Hud.blob.g.h` and `hud.viewblob`. **Codegen is the SOLE author of the version** — the C++ engine never recomputes it, it only *reads* the emitted `blob.version` and compares. This sidesteps the cross-language hash-agreement hazard entirely: there is exactly one implementation of the hash (C#), so the header value, the data-file value, and (in Inc-3) the C# consumer value all come from the same code path and cannot disagree. The hash **algorithm** must still be simple and fully specified in the plan (e.g. FNV-1a over a canonical UTF-8 serialization of `model | field.name | field.kind | …` in declared order) so it is auditable and stable across tool versions — but it is only ever *run* in C#. (`Core/VixenHash.h` is referenced for the separate **content**-hash in the proof test §7, not for the version hash.)
- **Cannot go stale:** any field add/rename/reorder/retype changes the hash deterministically. This is what **replaces undertow's hand-bumped `FormatVersion`** — the human never bumps or checks a number.
- **The guard:** `ViewStore::Version()` carries the version the consumer was generated against; `blob_.version` the version the engine loaded. Mismatch at `Register` → logged hard error + skip-register (empty view). In-tree, the matching tests use the codegen-emitted version on both sides; the mismatch test injects a deliberately wrong consumer version to prove the guard fires. (Inc-3's C# upload face carries its own generated version over the bridge — the real cross-generated desync this guard exists for.)

**Failure taxonomy — all hard + logged, never garbage or crash:**

| Failure | Detected by | Result |
|---|---|---|
| Version mismatch | `BlobView::Register` | skip-register → empty view + `LT_ERROR` |
| Malformed / missing `.viewblob` | `ViewBlobFile` parser | no blob → caller falls back or empty |
| Unknown `ViewKind` in file | parser | reject file → logged error |
| Bad field name/kind in a setter | `ViewStore` setter | logged error, no write |

---

## 6. Codegen emitter & delivery front-ends

**`ViewBlobEmitter` (Yeroket, `--view-blob`)** — sibling of `RmlDataModelEmitter`/`RmlMarkupEmitter`, following the established pattern:

- `ViewBlobEmitter.EmitHeader(ViewStruct) → string` — `Hud.blob.g.h` with `constexpr ViewFieldDesc[]` arrays + `constexpr ViewBlob kHudBlob` + the computed `version`.
- `ViewBlobEmitter.EmitDataFile(ViewStruct) → string` — `hud.viewblob` (same structure/version, file form).
- The version hash is computed **once** in `ViewVersionHash.Compute(ViewStruct)` and fed to both emitters — single source, guaranteed identical across header and file.
- New `--view-blob` CLI branch in `Program.cs` with `--out-header` + `--out-datafile` (+ `--check`), reusing the existing `CheckOrWrite` golden-gate machinery. C# emitter unit tests mirror `RmlMarkupEmitterTests`.

**CMake wiring** (VIXEN `codegen/CMakeLists.txt`, mirroring `view_hud_check`/`view_hud_regen`):

- `view_hud_blob_check` (ALL) + `view_hud_blob_regen` → generate/verify `Hud.blob.g.h` (committed, under `application/main/include/Generated/`) and `hud.viewblob` (committed, staged as a UI asset next to `hud.rml`).
- Same **KI-015** caveat: `--check` no-ops on a Windows configure where the Yeroket repo is unreachable — pre-existing, documented, no new issue.

---

## 7. Proof gate — hash equivalence + one GPU anchor

The claim "the blob path renders byte-identically to native" is proven by **data-model content hashes** (all four paths feed the same RML document + RCSS, so identical resolved model state ⇒ identical render) plus **one** real-GPU anchor render for render-truth.

**CPU tests (fast, exhaustive)** — a new `test_view_blob_equiv`, mirroring `test_view_hud_golden`'s fixture (registers a `SystemInterface`/null `RenderInterface`, `Rml::Initialise`, a real `DataModelConstructor`):

1. `HashModel(DataModelHandle)` — walk the model's fields in declared order, read each resolved value (scalars + array rows), fold into a hash via `Core/VixenHash.h`.
2. Build the model **four ways** against the same schema and push the **same** fixture data:
   - native `HudView` (Inc-2 path) → `H_native`
   - `BlobView(header constexpr blob)` → `H_header`
   - `BlobView(parse("hud.viewblob"))` → `H_datafile`
   - `BlobView(blob with injected bad version)` → `H_bad` (empty model)
3. Assert `H_native == H_header == H_datafile`, `H_bad != H_native`, and that the mismatch `LT_ERROR` line was emitted.
4. **Parser round-trip subtest:** `parse("hud.viewblob")` yields a `ViewBlob` structurally equal to the header's `kHudBlob` (belt-and-suspenders on the data-file front-end).

**GPU anchor (one ~50s render)** — reuse `test_hud_render_capture` unchanged: render the native `HudView` on the real GPU, assert it matches Inc-2's baseline PNG. This proves the model→pixels path works; the CPU hashes prove all four data-model states are equivalent to it. Full render-truth at 1× GPU cost, not 3–4×.

**No-regression:** Inc-1 golden (`test_view_hud_golden`), `test_ui_hud_smoke`, and the live HUD stay green — Inc-2b adds a parallel path and touches neither `UIRenderNode` nor `HudView`.

---

## 8. Cross-repo shape

Same two-repo pipeline as Inc-1/Inc-2:

- **Yeroket** — `ViewBlobEmitter` (header + data-file), `ViewVersionHash`, the `--view-blob` CLI branch, C# emitter tests. Local-only (like the rest of the View Contract Yeroket work; push timing is the user's).
- **VIXEN** — `ViewBlob`/`ViewFieldDesc`/`ViewKind`, `ViewStore`/`ViewValue`, `BlobView`, `ViewBlobFile` (parser) engine units; generated `Hud.blob.g.h` + `hud.viewblob`; CMake targets; `test_view_blob_equiv`; the reused GPU anchor.

---

## 9. What Inc-2b deliberately does NOT do

- No C# data-upload face (typed C# setters over the SoA bridge) — **Inc-3**.
- No input→action / `BindEventCallback` / `ParseLayerToggleId` retirement — **Inc-4**.
- No standalone nested-struct (non-array) view field — out of the current schema; a follow-up when first authored.
- No undertow migration — **Inc-5+**.
- No change to `UIRenderNode` or native `HudView` — the blob path is additive.

**Inc-2b's single job:** prove the engine can host a view from a **runtime description** (both delivery forms) and marshal data into it generically, rendering identically to the native path, with the schema-version hash as the honest boundary guard — the load-bearing 80% of the "runtime-loadable mod views" end-goal.
