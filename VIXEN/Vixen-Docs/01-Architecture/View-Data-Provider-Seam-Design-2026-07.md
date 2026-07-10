# View-Data-Provider Seam — Design Note (2026-07-10)

**Status:** FINAL — verified against Gaia v0.9.2 (`gaia-sync`, commit `7ae1a1c8`). See [v0.9.2 confirmation](#v092-confirmation).

**Purpose.** Define the accessor seam that source-generated view-management actions (the #2 program)
target, so the datum a UI view reads/writes goes through an **indirection** — a direct-field provider
today, a Gaia-ECS provider later — **without rewiring any generated action** when the backing store
changes. This is the "build it ECS-ready now so we don't rewire for ECS linkage later" decision
(user, 2026-07-10). Investigation by `gaia-probe` (Opus, read-only) over the current tree.

## The decision context
- The editor today hand-writes handlers that do direct read-modify-write against a concrete store
  (`rt_.Layers().SetMask(applyToggle(rt_.Layers().Mask(), idx))`, `EditorApplication.cpp:153`).
- #2 will source-generate those handlers. If the generated body bakes in `rt_.Layers()`, swapping to
  a Gaia-backed datum later means editing every generated action. Targeting a seam interface instead
  makes it a **provider swap**.
- Chosen scope (user): **investigate + seam design only** — no live Gaia wiring in #2; the direct-field
  provider is the day-one implementation.

## Decisive finding: Gaia access is SYNCHRONOUS + IMMEDIATE
`GaiaVoxelWorld` get/set/ref operate directly on `getWorld()` and return/apply immediately — **no
system-tick, scheduler, command-buffer, or deferred-commit gate** (impls `GaiaVoxelWorld.h:596/609/623`;
grep for `commit|System|schedule|defer|CommandBuffer` in the header is empty). The editor's current
transaction is likewise an immediate synchronous RMW (`EditorApplication.cpp:146-156`).
→ **The seam stays synchronous. Do NOT bake a deferred/commit shape in** — it would be speculative
complexity neither store needs.

## Recommended seam
```cpp
// Typed noun id emitted from the same codegen source as the view schema (NOT a raw string),
// so both providers implement a compile-checked switch.
enum class ViewNounId : uint32_t { /* generated: LayerMask, ... */ };

// Optional provider-interpreted instance slot. Direct-field ignores it; a future Gaia provider
// may use it as a morton/entity-index. Present from day one so retrofitting per-instance identity
// never has to touch generated handlers.
struct ViewNounKey { ViewNounId noun; uint64_t instance = 0; };

struct IViewDataProvider {
    virtual ~IViewDataProvider() = default;
    virtual bool ReadU32 (ViewNounKey key, uint32_t& out) const = 0; // fallible: false = absent
    virtual void WriteU32(ViewNounKey key, uint32_t value)       = 0; // immediate
    // widen with typed overloads (ReadF32/WriteF32, ...) as more noun value-types appear
};
```
- **Undo stays OUTSIDE the provider** — the generated handler wraps the write in
  `ActionStack::Dispatch` (forward/inverse closures), exactly as today (`EditorApplication.cpp:148`).
- **Direct-field provider** (day one, ~3 lines): noun `LayerMask` → `LayerController::Mask()/SetMask()`
  (`LayerController.h:22,23`). Always returns `true` for reads; ignores `instance`.
- **Gaia provider** (future): noun → internal map → `EntityID` + component `T` →
  `getComponentValue<T>`/`setComponent<T>` (`GaiaVoxelWorld.h:129,150`); positional identity
  (morton/pos→entity, `:235,495`) hidden behind the noun / `instance` slot.

## The four Gaia-forced shapes (why the interface looks like this)
These are the shapes the direct-field provider wouldn't naturally need but which keep the interface
stable when the Gaia provider arrives — build them in NOW:
1. **By-value reads, never held refs.** `getComponentRef`/`get<T>&` is invalidated by any structural
   change (chunk relocation). Seam read returns by value via out-param, never a `T*`/`T&`. **Most
   important.**
2. **Fallible/optional read.** Gaia reads return `optional` (component may be absent). Seam read
   returns `bool`; direct-field always `true`.
3. **Provider-interpreted `instance` slot.** The only structural concession to entity-addressing;
   unused by direct-field, cheap insurance vs. retrofitting identity into every handler.
4. **Typed noun ids, not strings.** Codegen emits a compile-checked switch on both sides.

## Identity model
Noun key is a **typed id in a flat namespace**; it does **not** carry an entity handle. Today's noun
is a `layerIndex` scalar with no entity concept; forcing an entity into every noun is the one shape
the direct-field provider shouldn't need. Providers resolve identity internally. The optional
`instance` slot covers the future multi-instance case without changing the interface.

## Packaging note
`GaiaVoxelWorld`/`GaiaArchetypes` are **STATIC** libs (not DLLs), and there is **no separate
kernel/compute access path** for host data — the GPU-facing path is a bulk zero-copy read
(`getBrickEntitiesInto`, `GaiaVoxelWorld.h:264`) at upload time. So "Gaia DLL + kernel access
patterns" for view transactions resolves to: **the C++ host API of a static lib, synchronous.** (If
a true DLL boundary or a kernel-side view-data path is desired later, that's a separate concern from
this seam — the seam is a host-side C++ interface.)

## Open risks / future gates (NOT day-one requirements)
- **Off-thread Gaia systems don't exist yet.** `application/main` does not even link `GaiaVoxelWorld`
  (only `CashSystem` + `SVO` tests consume it). If future Gaia mutation systems run on worker threads,
  a synchronous UI write would need sequencing — a future gate, not a day-one shape.
- **Whether layer/UI state ever becomes a Gaia component.** The GaiaVoxelWorld expansion proposal is
  about *voxel data* (density/color), not UI/layer state. If layer state stays a singleton, even the
  Gaia provider needs no per-instance identity — the `instance` slot is cheap insurance.
- **Indexed component access is STUBBED** in the wrapper (`getComponentValueByIndex`/`setComponentByIndex`
  are TODO no-ops, `GaiaVoxelWorld.h:657,677,696`). A noun→multi-instance-component mapping is not yet
  implemented; single-instance is.

## <a name="v092-confirmation"></a>v0.9.2 confirmation
`gaia-probe` read the tree while the fetched Gaia was `6f0a947` (~18 commits behind). `gaia-sync`
then bumped the pin to v0.9.2 (`2293594`→`f2ea77a`), built the wrapper + downstream green, and
reported exactly what upstream changed. Net effect on this design:

- **The wrapper's HOST API is unchanged** — `getComponentValue`/`setComponent`/`getComponentRef`
  signatures (`GaiaVoxelWorld.h:129,150,176`) are identical on v0.9.2, so the seam's provider mapping
  is unaffected. All B/C citations hold.
- **Confirmed & SHARPENED — writes are immediate, not a deferred proxy.** v0.9.2's `World::set<T>()`
  now returns a **direct mutable reference into chunk storage** (was a commits-on-destruction
  write-proxy). The stale "proxy" model was actively wrong: `GaiaArchetypes` had `auto stats =
  set<VolumeStats>(...)` silently mutating a throwaway copy, dropping every write — fixed to `auto&`.
  This *strengthens* the decisive finding: the synchronous, immediate seam is correct; there is no
  deferred-commit anywhere to model.
- **New correctness gate the future Gaia provider must honor:** v0.9.2 added a hard runtime
  `GAIA_ASSERT` that a query's declared access matches the `each` functor's constness — an immutable
  `.all<T>()` with a mutable `T&` lambda now crashes. Implication for the Gaia provider (NOT the seam
  interface): a `WriteU32` path that reaches Gaia via a query must declare mutable access
  (`.all<T&>()`); a `ReadU32` path uses immutable. The seam interface is unaffected (read=const
  method, write=non-const), but the provider IMPL must pick the right access mode. Noted here so the
  future Gaia-provider author doesn't trip the assert.
- **Multi-instance is still stubbed on v0.9.2** — indexed component access remains TODO in the
  wrapper. Unchanged from the draft: the flat-noun + optional-`instance` design already accommodates
  this without interface churn when it lands.

**Conclusion:** the seam design stands as written, with higher confidence — v0.9.2 removed the one
thing that could have forced a deferred shape (the write-proxy) and made immediacy explicit.
