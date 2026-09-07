---
title: WorkUnit Convention Interface — Kernel-Produced Work-Units, VIXEN-Declared Acceptance, on the ESVO Structures
status: DESIGN / RESEARCH + IMPLEMENTATION-IDEATION (awaiting owner ratification — STOP before implementation)
created: 2026-09-07
revised: 2026-09-07 (owner ruling folded in — Q1 RESOLVED; manifest = first-class type + dispatch↔world handshake)
author: workunitdesign lane (senior-architect research + ideation pass)
parent:
  - TaskConsumer-Contract-Audit-2026-09-07.md   # ratified; this doc REFRAMES its custody model
  - Multicore-Dispatch-Unification-Direction-2026-09.md
  - Domain-Agnostic-MultiChannel-Recipe-Output-Direction-2026-07.md
  - Deep-Field-Mip-Accessor-Policy-2026-08.md
  - Tiered-ESVO-Observer-Addressing-Design-2026-07.md
tags: [architecture, work-unit, convention-interface, task-consumer, esvo, svo, kernel-dispatch, parity, capability-graph, voxel-manifest, blackboard, cpu-first]
---

# WorkUnit Convention Interface Design

**The owner's reframe (the core idea, modeled precisely).** This is NOT "VIXEN is a consumer OF the kernel." It is the inverse: **VIXEN declares — in C++ semantics — an interface-like end-state consumer of a UNIT OF WORK.** The declaration says: *"anything the kernel produces that follows THESE CONVENTIONS is a valid input for this setup."* The kernel gains a capability — *produce work-units conforming to the convention* — and VIXEN's C++ declaration is an interface that accepts any conforming unit. A **convention-conformance interface, not a shared struct passed across.**

**Relation to the ratified TaskConsumer audit.** The [TaskConsumer Contract Audit](TaskConsumer-Contract-Audit-2026-09-07.md) designed the eight-part declared contract (shape, typed attributes, source + missing-data policy, domain-selection, step-modifier, backend-as-profile, provider binding, declared SDI). **All eight parts survive as the *content* of the convention** (one revision: §2.2's vocabulary artifact is superseded by the first-class manifest type — owner ruling, §6/§9). What this doc changes is the **custody model** the audit left open (its Q5, "where does a TaskConsumer declaration live"): the contract does not live in kernel-core as a shared type both sides include. Instead:

- the **convention** is the thing kernel-produced units conform to — a documented, shape-hashed, versioned set of rules whose vocabulary is the voxel content manifest;
- the **acceptance interface** is declared by VIXEN, in C++, compiled into VIXEN, referencing no kernel header;
- the **meeting point** is a structural conformance check at bind time — the kernel-emitted **unit manifest** checked against VIXEN's declared acceptance, exactly the mechanic the audit already designed for the SDI (§5.3: declared `SdiContract` vs runtime-fetched `MEMBERS[]`).

Neither side includes the other's types. The kernel never learns VIXEN's acceptor exists; VIXEN never learns which kernel producer emitted the unit. Conformance is checked, never assumed.

**Scope:** research + implementation ideation. **No source changed.** The owner ratifies before any code moves.

**Ratified refinements folded in** (from the boundary blueprint + this pass's owner ruling): vocabulary = the **voxel content manifest, now a FIRST-CLASS handled TYPE and the dispatch↔world handshake (owner ruling — §6)**; parity = a **per-dispatch-system flag** (deterministic vs lossy) feeding both CPU and GPU codegen; degradation = VIXEN's **capability-based-feature pattern** (`CapabilityGraph`) — smooth, not hard-reject; **SDI = EXACT conformance, no divergence — kept DISTINCT from the manifest handshake's subset-compatible-with-defaults rule (C-7)**; **CPU-first**.

**Two corrections this doc carries over the earlier framing** (verified this pass against both repos):

1. **"Subsume the blackboard" means the dispatcher slot-set, not `[KernelBlackboardLayout]`.** The kernel's `[KernelBlackboardLayout]` (`Packages/com.yeroket.utility.kernel-framework/Runtime/KernelBlackboardLayoutAttribute.cs`) is a *source-level* data-context marker for the transpiler — stripped from generated runtime code. The real runtime persistent state is `SlotRef<T>` + `KernelStageBase` (+ `DispatcherPipeline` for multi-dispatch persistence) and the iterative loop is `FrontierPhase`. §4 anchors the stateful/iterative work-unit there.
2. **The voxel-manifest vocabulary had an asymmetry — now RESOLVED by owner ruling.** The channel *descriptor* (`ChannelDesc`) is codegen-single-source; the semantic *vocabulary* (`SemanticId`/`FieldKind` values) is a hand-authored C++ enum referenced **by value** across repos. The ruling supersedes the hand enum: the manifest itself is promoted to a first-class declared TYPE that is both the vocabulary and the dispatch↔world handshake. §6 records the ruling; Q1 is closed.

---

## 1. The convention — what a valid kernel-produced unit of work is

A **work-unit** is `(manifest, payload, state-contract)`:

- **manifest** — the serialized frozen surface of the unit batch: grain, atom readings, channel list, state-layout hash, termination-gate id, parity flag, domain kind, and a shape-hash over all of it. Emitted by the kernel producer, checked by the VIXEN acceptor. This is the only thing both sides "share," and it is data, not a type.
- **payload** — the buffers themselves, in the layout the manifest describes. For ESVO-shaped units this is exactly the `SerializedOctree` byte-buffer family the shader and the CPU mirror already consume.
- **state-contract** — present only for stateful/iterative units: the declared POD state layout + bounded stack + step/termination convention (§1, C-4; §4).

The convention proper is seven rules. Each is grounded in a structure that already exists in the ESVO code (the parenthetical), which is the load-bearing efficiency claim of §3: **the convention is mostly a description of what ESVO already does, promoted to a checked contract.**

### C-1 — UNIT GRAIN (ESVO-native grains)

A unit is one of four grains, three of which are the octree's own natural units:

| Grain | ESVO incarnation | Dispatch meaning |
|---|---|---|
| `Step` | one phase-function application over `TraversalState` | the unit of a `FrontierPhase` wave (§4) |
| `Node` | one 64-bit `ChildDescriptor` (`SVOTypes.h:36-154`) | the unit of structural walks (descent, shell classification) |
| `Brick` | one 512-voxel (8×8×8) dense block with channel SoA | **the default batch grain** — batch-first, the spanbatch foundation |
| `RayTask` | one ray's full traversal (init → loop → hit/miss) | the unit of the default-step consumer; a `Step` sequence with private state |

The grain is frozen surface (in the shape-hash). Counts per batch are interior.

### C-2 — DESCRIPTOR ATOM (discriminated multi-reading, the farBit precedent)

A unit's atom may pack multiple context-dependent readings into one fixed-width word pair, **selected by a declared discriminator** — never by ambient context. The live exemplar is `ChildDescriptor` (`SVOTypes.h`): 15-bit `childPointer` + `farBit` + `validMask` + `leafMask` in word 1; word 2 has **three** farBit/context-selected readings (contour / brick-index / tier-crossing `TierRef` index), with the header rule "callers MUST check farBit before interpreting… the two are mutually exclusive readings of the same bits" and `static_assert(sizeof(ChildDescriptor) == 8)`. The convention rule: an atom's manifest entry declares its size, its discriminator, and **all** readings; a reader that does not know a reading fails closed (the `descendToNodeOrdinal` farBit guards in `ESVOTraversal.glsl:224-263` are the reference behavior — "fail closed rather than resolve a wrong index").

### C-3 — ATTRIBUTE VOCABULARY: the manifest as a FIRST-CLASS TYPE (owner-RULED — the dispatch↔world handshake)

**Owner ruling (Q1 RESOLVED — supersedes the hand-enum framing; §6 records it).** The voxel content manifest is a **first-class handled TYPE**: it IS the vocabulary declaration, and it IS the **handshake between dispatch patterns and domain worlds**:

- a **domain world** (a voxel tree / content pack / sim domain) **declares its manifest** — the full set of semantic elements it stores or produces: per element `{semantic, elemCount, fieldKind, role, source}`, plus the self-describing layout;
- a **dispatch pattern** (a consumer: default-step, shell classification, a physics solve) **declares the manifest it consumes** — the elements it reads/writes, each carrying its per-element missing-data policy (audit §2.3, unchanged);
- they **handshake through the declared manifest type** at bind — and compatibility is SUBSET-COMPATIBLE-WITH-DEFAULTS, not type-equality (C-7 Rule 1).

**Declaration + codegen handling (the hand enum becomes a declared artifact).** The manifest is declared once, in the same codegen family as the sibling struct that already works this way (`[GpuStruct] ChannelDesc.cs → OctreeConfig.g.h`): a `[VoxelManifest]`-style schema whose elements mint the semantic identities. Codegen emits every language face from that one declaration — `VoxelChannelFormat.g.h` for C++ (superseding the hand-authored `VoxelChannelFormat.h`), the kernel's C# face (retiring `VoxelDocChannel`'s "MUST stay field-identical" comment-contract), and the Python/Blender codec table. **The `SEM_*`/`FK_*` values become part of the declared manifest — generated, never hand-mirrored across repos.** Adding a semantic = editing one declaration; cross-repo value agreement is carried by the generated faces + the manifest's shape-hash, not by discipline.

Today's hand-authored form, for reference (what the generated face replaces):

```c
// VoxelChannelFormat.h (hand-authored TODAY — superseded by the generated manifest face, §6)
enum SemanticId : uint32_t { SEM_SDF=0, SEM_COLOR, SEM_ROUGHNESS, SEM_NORMAL,
                             SEM_METALLIC, SEM_EMISSION, SEM_DENSITY, SEM_COUNT };
enum FieldKind  : uint32_t { FK_NONE=0, FK_DISTANCE=1, FK_DENSITY=2 };

// OctreeConfig.g.h:7-12 (GENERATED from VIXEN/codegen/config-schemas/ChannelDesc.cs — the working precedent)
struct ChannelDesc { uint32_t semanticId, elemCount, channelBaseFloats, fieldKind; };
static_assert(sizeof(ChannelDesc) == 16, "ChannelDesc std430 size");
```

Three properties of the existing vocabulary carry into the declared type unchanged:

- **`fieldKind` is "declared by the data, not inferred"** (the header's own words) — exactly the convention's stance on every declared axis.
- **The layout is self-describing:** `channelBaseFloats` places each channel in the SoA pool (`channelPool[brick*stride + channelBase[c] + comp*512 + voxel]`), scanned at runtime. A producer can add a channel without recompiling any conforming consumer — the data-tier analogue of the audit's SDI-decoupling win, and the mechanical basis of the subset handshake.
- **The kernel side mirrors it** — today by comment (Yeroket's `VoxelDocChannel`, `Packages/com.utility.graph-framework/Runtime/VM/VoxelDocument.cs`: "MUST stay field-identical to VIXEN's ChannelDesc"); under the ruling, by generation from the same declaration.

A unit's manifest lists its channels as `ChannelDesc` entries plus, per the audit §2.2/§2.3 (unchanged), role flags, attribute source, and per-attribute missing-data policy — but the manifest is no longer a serialized listing *against* an external vocabulary: **the manifest type is itself the vocabulary artifact both sides declare against.**

### C-4 — STATE CONTRACT (stateful units: step-function-over-persistent-state)

A stateful unit declares a **POD state struct**, a **bounded, scale/level-indexed stack**, a set of **named step functions** that are the only mutators, and a **declared termination gate**. This is not invented — it is the shape the production ESVO traversal already has (verified this pass):

- State: `TraversalState` (`ESVOTraversal.glsl:40-48`, "matches ESVOTraversalState in C++" — `LaineKarrasOctree.h:260`): `{parentPtr, idx, scale, scale_exp2, pos, t_min, t_max, h}` — a ~44-byte POD.
- Stack: `StackEntry stack[STACK_SIZE=23]` (`ESVOTraversal.glsl:24,34-37`, "matches CastStack in C++" — `LaineKarrasOctree.h:386`, `MAX_STACK_DEPTH=32`).
- Step functions: `initTraversalState` / `checkChildValidity` / `executePushPhase` / `executeAdvancePhase` / `executePopPhase` (`ESVOTraversal.glsl:315/420/456/501/546`), driven by the production loop in `SceneBindings.glsl` (~1724-2116 and the second instanced copy ~2475-2632).
- Termination: the LOD gate `tv_max * pc.raySizeCoef + pc.raySizeBias >= state.scale_exp2` (`SceneBindings.glsl:2070/2602`) plus the phase return codes (`POP_NEEDED`, `EXIT_OCTREE`) and the iteration bound `MAX_ITERS = 512`.

The convention rule: the manifest carries `StateContract{sizeBytes, stackDepth, layoutHash}` + the ids of the step functions and the termination gate. The state is **restartable** — a unit suspended after any whole step can resume from its state block alone (the tier-crossing hop loop in `GpuTraversalMirror.h::castRay` already restarts traversal across trees on exactly this property).

### C-5 — PARITY FLAG (per-dispatch-system, deterministic vs lossy)

Every dispatch **system** (not each dispatch) declares one of:

- **`Deterministic`** — CPU and GPU produce byte/bit-identical observable results; gated by a mirror oracle. The live discipline is `GpuTraversalMirror.h`: "a faithful 1:1 C++ MIRROR … line-by-line port of the cited GLSL … If the shader changes, re-port the changed function" — with GLSL float op-order matched and the IEEE-754 bit tricks mirrored. This is the ratified **SDI = EXACT conformance, no divergence** stance already practiced at the algorithm level.
- **`Lossy`** — backends agree at *policy* level, not bit level; divergence is bounded by consulting ONE shared policy function. The live instance is **`VIXEN_MIP_POLICY`** (`ESVOTraversal.glsl:146-192`): flag-on consults the single shared `mipPolicyLevel(footprint, leafWorldSize, maxLevel)` (`SVOTypes.glsl:297`) "instead of this loop's own per-hop crossing test — same arithmetic … now shared with the DDA/RT gate sites so every backend's level choice agrees"; flag-off re-derives the crossing per hop.

The flag **feeds both CPU and GPU codegen** (ratified): a `Deterministic` system's emitters must produce op-order-matched twins (the mirror shows this is achievable and testable); a `Lossy` system's emitters must route every backend through the shared policy callable. Today `VIXEN_MIP_POLICY` is an `#ifdef`; under the convention it becomes the declared per-system flag the emitters read (open question Q4).

### C-6 — DOMAIN (occupied-sparse today; complement deferred)

Units range over a declared domain. What exists (verified): the occupied domain via `traceBounds` + occupancy stats, the brick grid with the `kBrickUnalloc = 0xFFFFFFFF` void sentinel, and **cross-tier addressing via `TierRef`/`TierRefTable`** (opt-in per leaf via `MarkLeafAsTierCrossing`/`setTierCrossing`) — the closest existing thing to a region-set scheme. The audit's `Normal` domain maps onto this directly; `InvertedSparse` (addressing the void) remains net-new tree capability and stays **declared-but-deferred**, unchanged from the audit (its Q3).

### C-7 — MANIFEST + SHAPE-HASH + TWO DISTINCT CONFORMANCE RULES (owner-ruled — do not conflate)

The frozen surface (C-1 grain, C-2 atom readings, C-3 channel declarations + policy choices, C-4 state contract + gate ids, C-5 parity flag, C-6 domain kind) is canonically serialized and shape-hashed; interior values (counts, `Default` literals, per-instance params, active LOD coefficients) are excluded and hot-reload — the ratified boundary-audit gate, unchanged.

**Conformance is TWO DISTINCT RULES, one per kind of surface — keep them separate:**

**Rule 1 — the manifest handshake (content vocabulary, dispatch↔world): SUBSET-COMPATIBLE-WITH-DEFAULTS — graceful.** The dispatch pattern's consumed manifest need not equal the domain world's declared manifest. The ruling: *a manifest may declare a SUBSET of another and be COMPATIBLE iff every non-declared element is OPTIONAL or has a DEFAULT HANDLER.* The check REUSES the per-attribute missing-data-policy machinery (`Default`/`DeriveFrom`/`InheritNeighbor`/`Densify` — audit §2.3) as the compatibility predicate, so the handshake degrades gracefully **by construction** — the vocabulary-level twin of the capability-based smooth-degradation ruling (§2.3). It runs at **bind time** (§2.2 step 2), like the acceptor check, before any lowering:

```
Handshake(consumed /* dispatch pattern's manifest */, declared /* domain world's manifest */):
  for e in consumed.elements:
    d = declared.find(e.semantic)
    if d exists:
      require d.elemCount == e.elemCount && d.fieldKind == e.fieldKind   # per-element match is exact
    else:                                    # GAP — the world does not declare e
      if e.missing ∈ {Default, DeriveFrom, InheritNeighbor, Densify}:
        bind e's declared handler            # covered — graceful degradation
      else:                                  # Reject policy = required element
        FAIL-CLOSED with a readable diff
  elements in declared but not consumed: ignore-with-log (audit Q8, carried over)
  ⇒ COMPATIBLE iff every gap is optional or covered by a default handler
```

(`DeriveFrom` covers a gap only if its source sibling is itself present-or-covered — resolved over the DerivedOutput dependency DAG; an uncoverable chain or a cycle rejects.)

**Rule 2 — the SDI (the view into a PROCESS): EXACT, no divergence — unchanged.** The declared `SdiContract` vs the runtime-fetched `MEMBERS[]` matches exactly on the declared surface (audit §5.3, the ratified SDI ruling). Likewise the unit's *structural* surface — grain, atom readings + discriminator, state contract + gate ids, parity flag, domain kind — stays exact-checked, fail-closed with a message: those are process-shaped facts, and a divergent process view is a wrong program.

**Why the asymmetry is principled:** the manifest is CONTENT — content degrades, and missing content has a declared policy; the SDI is a PROCESS-VIEW — no policy can paper over a divergent view of a process. Conflating them either subset-tolerates the SDI (silent under-binding, audit Q8's named risk) or exact-matches the vocabulary (the brittle cross-repo lockstep §6 existed to escape).

---

## 2. The VIXEN-side declaration — the C++ acceptance interface

### 2.1 Declaration form (ideation)

VIXEN compiles in an **acceptor**: a `constexpr` declaration of the convention surface it accepts, concept-checked, including **no kernel header**. Its `kChannels[]` **is the dispatch pattern's consumed manifest** (C-3); the vocabulary constants it names (`SEM_*`, `FK_*`) are generated faces of the declared manifest type (`VoxelChannelFormat.g.h` — C-3/§6) — which is precisely the inversion: **the kernel produces against the declared manifest**, not VIXEN against kernel structs.

```cpp
// Ideation sketch — e.g. VIXEN/libraries/SVO/include/WorkUnitAcceptor.h (NOT implemented)
namespace Vixen::WorkUnit {

enum class UnitGrain : uint8_t   { Step, Node, Brick, RayTask };
enum class Parity : uint8_t      { Deterministic, Lossy };
enum class Termination : uint8_t { FootprintGate, ConvergencePredicate, FixedCount };

struct ChannelRequest {          // ChannelDesc minus runtime layout, plus policy
    uint32_t semanticId;         // generated manifest face (SEM_* — C-3/§6)
    uint32_t elemCount;
    uint32_t fieldKind;          // FK_*
    MissingPolicy missing;       // audit §2.3, unchanged
};

struct StateContract { uint32_t sizeBytes; uint32_t stackDepth; uint64_t layoutHash; };

template <typename D>
concept WorkUnitAcceptor = requires {
    { D::kGrain }       -> std::convertible_to<UnitGrain>;
    { D::kChannels }    ;                       // span<const ChannelRequest>
    { D::kParity }      -> std::convertible_to<Parity>;
    { D::kTermination } -> std::convertible_to<Termination>;
    { D::kState }       -> std::convertible_to<StateContract>;   // zeroed for stateless
    { D::kLadder }      ;                       // span<const AcceptanceTier> — §2.3
};

// The first consumer (§5), declared:
struct DefaultStepAcceptor {
    static constexpr UnitGrain   kGrain       = UnitGrain::RayTask;
    static constexpr ChannelRequest kChannels[] = {
        { SEM_SDF, 1, FK_DISTANCE, MissingPolicy::Reject() },
    };
    static constexpr Parity      kParity      = Parity::Deterministic;
    static constexpr Termination kTermination = Termination::FootprintGate;
    static constexpr StateContract kState     = StateContractOf<ESVOTraversalState, /*stack*/23>();
    static constexpr AcceptanceTier kLadder[] = { /* §2.3 */ };
};
} // namespace Vixen::WorkUnit
```

### 2.2 How it binds to a dispatch

Binding reuses the audit's provider model (§2.7) unchanged — a RenderGraph/provider node owns Vulkan lifetime and lowers work onto a `KernelDispatch::Stage` under a `DispatcherProfile`. What changes is the **first step of bind**:

1. The provider fetches the kernel-produced **unit manifest** (content crossing the boundary — like recipe bytecode, like the SDI `MEMBERS[]`).
2. `Conformance::Check(DefaultStepAcceptor, manifest)` — C-7's two rules: the structural surface (grain, atom readings, state contract, parity, domain) matches exactly (hash compare); the vocabulary block runs the **manifest handshake** — subset-compatible-with-defaults, binding the declared handlers for covered gaps. Reject ⇒ fail-closed with the diff (a structural mismatch, or an uncovered required gap).
3. On accept, the provider resolves channels via `channelBaseFloats` (runtime scan — no recompile), binds payload buffers, and lowers onto the stage. For stateful units the state block + stack are slot-allocated (§4).

The precedent that makes this cheap is the audit's own §5.1 finding: the fan-out layer is *already* generic over a `(Metadata, MEMBERS[])` pair. The acceptor is the same move one level up — VIXEN's machinery works off its own declared description; the concrete kernel-produced thing is runtime-checked data.

### 2.3 Capability-based smooth degradation

Degradation follows VIXEN's existing capability-based-feature pattern: `CapabilityGraph` (`libraries/VulkanResources/include/CapabilityGraph.h` — named `CapabilityNode`s with cached `IsAvailable()`, dependency edges, instance/device extension/layer/feature leaves) is consulted at runtime and features select paths, never hard-fail. The acceptor declares an ordered **degradation ladder**:

```cpp
struct AcceptanceTier {
    const char*  requiredCapability;  // CapabilityGraph node name; nullptr = unconditional
    UnitGrain    grain;               // tier may coarsen the grain
    Parity       parity;              // tier may relax Deterministic -> Lossy
    uint32_t     channelMask;         // tier may drop optional channels
};
```

At bind, the provider walks the ladder top-down and selects the **first tier whose capability node reports available**; only an exhausted ladder rejects. The live analogue is exactly the `VIXEN_MIP_POLICY` branch pair: the policy path (shared `mipPolicyLevel`, coarse but agreed) versus the per-hop exact path — one consumer, two conforming behaviors, chosen by environment. Degradation changes **which declared tier binds**, never the conformance rules inside a tier: within the selected tier, C-7 applies unchanged — structural surface exact, vocabulary via the subset-compatible-with-defaults handshake. The ladder (environment capability) and the handshake's default-covered gaps (content coverage) are the two declared degradation axes — both graceful, neither silent.

### 2.4 The inversion, stated as dependency arrows

```
kernel producer ──(conforms to)──> CONVENTION <──(declares acceptance of)── VIXEN acceptor
        │                              ▲                                        │
        └── emits unit manifest ───────┴──────── checks manifest at bind ───────┘
```

- The kernel depends on: the convention document + the declared manifest type (consuming its generated C# face, not hand-mirrored values). No VIXEN headers.
- VIXEN depends on: its own acceptor declaration + its own vocabulary header. No kernel types.
- A new kernel producer that conforms is accepted by an **unchanged, un-recompiled** VIXEN — the end-state the owner named: *"anything the kernel produces that follows these conventions is a valid input for this setup."*

---

## 3. ESVO fit — how far the existing structures already provide the unit shape

### 3.1 Reuse-vs-net-new ledger (MEASURED this pass)

| Convention part | Existing ESVO structure | Verdict |
|---|---|---|
| C-1 grains | node = `ChildDescriptor`; brick = 512-voxel SoA block; ray task = the traversal; step = the phase functions | **REUSE** — the grains are the octree's own units |
| C-2 discriminated atom | `ChildDescriptor` farBit-selected triple reading + "MUST check farBit" rule + fail-closed guards (`ESVOTraversal.glsl:224-263`) | **REUSE** as exemplar; rule text is net-new |
| C-3 vocabulary | `SemanticId`/`FieldKind`/`SemanticElemCount` + generated `ChannelDesc` + self-describing `channelBaseFloats` pool + kernel `VoxelDocChannel` mirror | **REUSE**; §6 asymmetry RESOLVED — manifest promoted to a first-class generated type |
| C-4 state contract | `TraversalState`+`StackEntry` twins, five named phase functions, LOD gate, `MAX_ITERS` | **REUSE** — already decomposed exactly this way |
| C-5 parity | `GpuTraversalMirror` sync contract (Deterministic); `VIXEN_MIP_POLICY`+`mipPolicyLevel` (Lossy) | **REUSE** as the two live modes; flag *declaration* is net-new |
| C-6 domain | `traceBounds`, `kBrickUnalloc` sentinel, `TierRef` cross-tier addressing | **REUSE** for `Normal`; `InvertedSparse` net-new, deferred |
| C-7 manifest + shape-hash | boundary-audit gate machinery; `LAYOUT_HASH` discipline in the SDI tooling | **NET-NEW but small** — serialization of already-existing facts |
| §2 acceptor + conformance check | `<Meta, MEMBERS[]>`-generic fan-out precedent (audit §5.1) | **NET-NEW but small** — mirrors the ratified SdiContract design |
| §2.3 degradation ladder | `CapabilityGraph` + capability-requirement-matrix | **REUSE** pattern; ladder struct is net-new |

Net-new is confined to: the manifest emitter, the acceptor + check, the ladder struct, and (deferred) void-domain addressing. Everything hot-path is existing, shipped ESVO structure.

### 3.2 The efficiency argument

1. **Zero new hot-path formats.** The payload IS `SerializedOctree`'s byte buffers. The CPU mirror already `reinterpret_cast`s them to the shader's SSBO element types (`GpuTraversalMirror.h:63-67`) — the zero-copy, both-backends-one-layout property is proven, not proposed. A conforming unit inherits the ~5-bytes/voxel, 8-byte-node, 512-voxel-SoA-brick layout that Laine-Karras chose *for* traversal efficiency; the convention adds no per-item overhead.
2. **Conformance cost is bind-time only.** One manifest hash compare + channel scan per bind; zero per-ray/per-voxel cost. (The SDI design already accepted this cost model.)
3. **The state machine is register-friendly by construction.** ~44-byte POD state + a 23-entry stack in local memory + phase functions with no hidden state — the exact shape that runs at 1,700 Mrays/sec on the Cornell fixture (SVO README). Declaring it changes nothing at runtime.
4. **Producer evolution without consumer recompile.** `channelBaseFloats` self-description means a kernel producer adding a channel is a content event; the acceptor's subset request still binds. This is the data-tier twin of the audit's headline SDI win (no more 10k-line-TU rebuilds on shape edits), obtained here for free because ESVO already stores layout in data.
5. **Batch-first grain alignment.** The `Brick` grain (512 voxels) is a natural SIMD/workgroup batch; the `RayTask` grain lowers onto per-element dispatch — both map onto `KernelDispatch` item-count semantics with no impedance layer.

**Verdict: the ESVO structures don't merely *fit* the work-unit convention — they are its origin.** The convention is a promotion of ESVO's existing discipline (discriminated descriptors, self-describing channels, phase-decomposed state machine, mirror-enforced parity, policy-shared lossiness) into a checked, named contract that non-ESVO units can also satisfy.

---

## 4. Blackboard subsumption — the stateful/iterative unit, anchored correctly

**Correction first** (verified in the kernel repo): `[KernelBlackboardLayout]` is a source-level authoring shape for the transpiler (write `ctx.Field` instead of slot params; fields use `BufferRef`/`AppendRef`/`SpatialHashRef`/`TableRef`/`IndirectRef`/scalar markers) and is **stripped from generated runtime code**. It is not a runtime state container, and the convention must not cite it as one. (The plant-graph `BlackboardComposer.Concat` is a `NotImplementedException` stub — designed-but-unlanded; not a foundation.)

The **real** kernel-side shapes the stateful/iterative work-unit subsumes are three (all in `Packages/com.yeroket.utility.kernel-framework/Runtime/Dispatcher/`):

- **`SlotRef<T>` + `KernelStageBase`** — the persistent slot-set a stage chain reads/writes. Access mode is *structural*: which id-list (`ReadSlotIds`/`WriteSlotIds`) a slot lands in; there is no `AccessMode` type.
- **`DispatcherPipeline`** — the long-lived materialized form: `BuildPipeline` allocates framework-owned slots once; `Dispatch` reuses them across many calls — explicitly "the path for multi-frame/continuous processes."
- **`FrontierPhase`** — the bounded managed wave loop for converging work: re-dispatches one PerElement batch per wave, threads `NextFrontier` wave→wave, checks a convergence predicate, returns `FrontierPhaseResult{Waves, Converged, HitWaveCap}`. (Native data-dependent stage-repeat is deferred kernel work.)

**The subsumption mapping** — the ESVO traversal is a `FrontierPhase`-shaped unit that happens to run its whole loop inside one dispatch today:

| Convention / ESVO | Kernel dispatcher shape |
|---|---|
| C-4 state block (`TraversalState`) + stack | slot contents (framework-owned slots, allocated once by `DispatcherPipeline`) |
| phase functions (init/check/push/advance/pop) | stages (`KernelStageBase`), state slots in their read/write id-lists |
| one loop iteration | one `FrontierPhase` wave |
| LOD gate + `EXIT_OCTREE` | the convergence predicate |
| `MAX_ITERS = 512` | the wave cap (`HitWaveCap`) |
| tier-crossing hop restart (`GpuTraversalMirror::castRay` hop loop) | `NextFrontier` threading N→N+1 |

So a stateful work-unit's C-4 declaration — *(state slots, step-stage list, convergence predicate, wave cap)* — is precisely the `DispatcherPipeline` + `FrontierPhase` quadruple. `[KernelBlackboardLayout]` remains what it is: a kernel-side authoring convenience for writing the stage bodies, invisible to the convention. The convention's stateful variant therefore **subsumes the blackboard pattern by lowering onto the dispatcher's real state machinery**, and inherits its two execution modes for free: whole-loop-in-one-dispatch (today's shader; the CPU mirror) and wave-per-dispatch (`FrontierPhase`, once a unit's steps need re-dispatch between waves — e.g. residency faults, tier hops that leave the resident set).

---

## 5. Ideation → implementation path — the first CPU slice and codegen touchpoints

CPU-first (ratified; `Backend::GpuCompute` is declared-but-unimplemented — audit's honest starting line, unchanged). The first slice proves the *inversion*, not new compute:

**Slice 1 — the default-step consumer as a declared acceptor over a kernel-produced CPU unit.**

1. **Ratify this convention** (§1) and freeze the manifest format (a small canonical serialization + FNV-1a shape-hash, same discipline as the SDI `LAYOUT_HASH`).
2. **Kernel-side: emit the unit manifest.** A small emitter in the kernel CodegenTool stamps the default-step producer's frozen surface (grain `RayTask`/`Linear`, channels `{SEM_SDF,1,FK_DISTANCE}`, state contract, parity `Deterministic`, domain `Normal`) into the produced pack — the same emitter family and gating discipline as `VoxelDocumentEmitter` (assembly-gated, FQN-resolved). **Touchpoint: manifest emitter only; no new producer logic.**
3. **VIXEN-side: declare `DefaultStepAcceptor`** (§2.1) + implement `Conformance::Check` (C-7: structural-exact + the subset-with-defaults handshake) + the ladder walk. Hand-written C++, small; no codegen needed for slice 1 (the generated manifest faces arrive with the vocabulary touchpoint below).
4. **Bind + lower, unchanged path:** provider node → `KernelDispatch::Stage` → CpuTbb (the implemented backend), running the existing SIMD recipe/default-step eval — the audit's first-consumer choice, now entered through the acceptance check.
5. **Gates:**
   - byte-identical output vs the current direct path (parity `Deterministic`);
   - worker-count 1/2/N invariance (the Multicore gate);
   - traversal parity against `GpuTraversalMirror`/`LaineKarrasOctree::castRay` with `raySizeCoef == 0` (LOD structurally disabled — the mirror's own parity regime);
   - shape-hash stability across an interior edit (recipe param); across declared-surface edits, the C-7 split behaviors, each witnessed: the producer ADDS an undeclared channel ⇒ still binds (ignore-with-log); a `Default`-covered consumed channel is absent ⇒ binds with the handler (graceful gap); a `Reject`-policy consumed channel is absent or type-mismatched ⇒ fail-closed reject;
   - a deliberately nonconforming manifest is rejected with a readable diff (fail-closed, no silent fallback).

**Then, in order (each its own ratify gate):** (2) multi-channel subset requests through the same acceptor (the Multi-Channel fused-codegen program, its own oracle discipline); (3) the parity flag graduates into codegen — `Deterministic` systems get op-order-matched twin emission (the mirror shows the bar), `Lossy` systems get the shared-policy-callable route, and `VIXEN_MIP_POLICY` retires from `#ifdef` to declared flag (Q4); (4) stateful units on `DispatcherPipeline`+`FrontierPhase` (§4) with a wave-per-dispatch traversal fixture; (5) GPU backend + SDI indirection, exactly per the audit §4/§5 sequencing — nothing in this doc changes that ordering.

**Codegen touchpoints, enumerated:** kernel manifest emitter (slice 1); parity-flag input to both CPU and GPU emitters (slice 3); state-struct twin emission from the declared state contract (slice 4 — `ChannelDesc.cs → OctreeConfig.g.h` is the working single-source precedent); manifest-type generation (**Q1 RESOLVED — required**: the declared manifest artifact and its generated language faces, §6). VIXEN-side codegen: none required through slice 3 (acceptors are hand C++ by design — they are *declarations*, few and stable).

---

## 6. The vocabulary asymmetry — RESOLVED: the manifest is the first-class vocabulary type (owner ruling)

**MEASURED:** the channel *descriptor* is codegen-single-source (`[GpuStruct] ChannelDesc.cs → OctreeConfig.g.h`, 16-byte std430, static_asserts). But the semantic *vocabulary* — the `SemanticId`/`FieldKind` **values** — is a hand-authored C++ enum in `VoxelChannelFormat.h`, and the kernel's `VoxelDocChannel` mirror (plus its Python/Blender codec) references those values **by value**, held together by a comment ("MUST stay field-identical"). Adding a semantic today = editing a hand enum in VIXEN + trusting two other languages to match.

The convention makes this vocabulary load-bearing across the repo boundary, so the seam had to be either closed or ratified. The fork was posed as: **(a)** generate the vocabulary — promote `SemanticId`/`FieldKind` to a declared schema (same family as `ChannelDesc.cs`), emit `VoxelChannelFormat.g.h`, the C# mirror, and the Python codec table from one source; vs **(b)** ratify the hand enum as intentional + a mandatory cross-repo value-parity gate.

**OWNER RULING (this pass — Q1 CLOSED): supersedes both options, going further than (a).** The manifest itself becomes a **FIRST-CLASS handled TYPE** — the single vocabulary artifact AND the handshake between dispatch patterns and domain worlds:

- A **domain world declares its manifest**; a **dispatch pattern declares the manifest it consumes**; they handshake through the declared type (C-3).
- **Compatibility is NOT type-equality:** a manifest may declare a subset of another and be compatible iff every non-declared element is optional or has a default handler — the missing-data-policy machinery reused as the compatibility check (C-7 Rule 1). Graceful degradation by construction.
- The hand-authored `SemanticId`/`FieldKind` by-value enum is **superseded**: the values are minted inside the manifest declaration and generated into every language face (C++/C#/Python). What (a) would have generated — the enum — is demoted from *artifact* to *face*; the checkable cross-repo contract is the manifest type + its shape-hash. (b)'s parity gate becomes moot: generated faces cannot drift.
- The handshake rule stays **DISTINCT** from the SDI's exact conformance (C-7 Rule 2) — content degrades; process-views do not.
- Element identity across independently declared manifests follows the ruling's own wording ("a manifest may declare a subset **of another**"): manifest-to-manifest subset/composition is first-class, so shared semantics come from declaring against/importing a base manifest, not from a global enum namespace. (Residual mechanical detail — the concrete declaration syntax for subset/import — is implementation ideation, not a fork.)

This lands where the prior recommendation (a) pointed, and further: consistent with the declaration-sufficiency rule ("a declaration needing manual side-effects is a consolidation issue") and with C-3's own stance (declared, not inferred), and it removes the cross-repo "field-identical by comment" seam entirely rather than gating it.

---

## 7. Open questions for the owner

1. **Vocabulary fork (§6): RESOLVED — owner ruling.** The manifest is a first-class handled TYPE: the vocabulary declaration AND the dispatch↔world handshake. The hand `SemanticId`/`FieldKind` by-value enum is superseded by generated faces of the declared manifest; handshake compatibility = subset-compatible-with-defaults (missing-data policies reused as the check), kept distinct from the exact SDI (C-3, C-7, §6, §9). The old sub-question (which repo authors the schema) dissolves structurally: each domain world authors its manifest declaration, each dispatch pattern authors its consumed-manifest declaration; the codegen face machinery rides the existing `ChannelDesc.cs` family.
2. **Custody + versioning of the convention itself:** the convention document (this §1) is now the only shared artifact. Where does its normative copy live, and is its version pinned by hash in both repos' gates (recommended: yes — a manifest carries the convention version it conforms to)?
3. **First-slice grain:** keep the audit's `Linear`-over-recipe-instances for slice 1 (recommended — smallest, rides the proven path) and introduce the `Brick` batch grain in slice 2, or lead with `Brick`?
4. **`VIXEN_MIP_POLICY` graduation:** confirm the `#ifdef` retires into the declared per-dispatch-system parity flag once slice 3 lands (the flag feeding codegen), leaving no compile-time fork of the level-selection arithmetic.
5. **Rejection UX:** confirm fail-closed-with-diff at bind (no silent fallback to a non-checked path) — consistent with the tree's fail-closed discipline (`descendToNodeOrdinal` guards, gate scripts).
6. **Ladder granularity (§2.3):** per-acceptor ladders (recommended — matches capability-based features, each consumer knows its own degradations) vs a global tier table?
7. **Acceptor home:** does the acceptor + conformance checker live in the SVO library (nearest the vocabulary) or in RenderGraph (nearest the provider/bind point)? Leaning SVO-adjacent, so non-render consumers (physics, tooling) can bind without RenderGraph.
8. **Inherited from the audit:** `InheritNeighbor` policy-vs-step-hook (audit Q1, still open), `InvertedSparse` scope (audit Q3, still open). Audit Q2 (one unified semantic enum extended to physics fields) is **CLOSED by the manifest ruling — it dissolves**: no global enum; unification happens at the handshake via subset-compatibility (§9).

---

## 8. Summary

- The ratified TaskConsumer contract's *content* stands; its *custody* inverts: the **kernel produces work-units conforming to a convention; VIXEN declares, in C++, an acceptance interface** over that convention. The meeting point is a kernel-emitted manifest checked at bind against VIXEN's `constexpr` acceptor — structural surface exact, content vocabulary via the subset-compatible-with-defaults handshake (C-7). No shared struct, no cross-includes, in either direction.
- The convention is seven rules (grain, discriminated atom, first-class manifest vocabulary, state contract, per-system parity flag, domain, shape-hashed two-rule conformance) — and **every hot-path rule is a promotion of something the ESVO structures already do**: `ChildDescriptor`'s farBit-discriminated readings, the self-describing `ChannelDesc` SoA pool, the phase-decomposed `TraversalState`+stack machine with its LOD termination gate, the `GpuTraversalMirror` exact-parity discipline, and the `VIXEN_MIP_POLICY`/`mipPolicyLevel` shared-policy lossiness. Net-new is confined to the manifest emitter, the acceptor + check, and the degradation ladder.
- **Degradation is smooth via `CapabilityGraph`:** acceptors declare a tier ladder walked at bind; exactness lives inside the selected tier, smoothness in the ladder.
- **The stateful/iterative unit subsumes the blackboard correctly:** anchored on `SlotRef`+`KernelStageBase` (structural access), `DispatcherPipeline` (persistent slots across dispatches), and `FrontierPhase` (bounded converging waves) — **not** on `[KernelBlackboardLayout]`, which is a source-level marker stripped at codegen. The ESVO traversal maps onto that machinery term-for-term.
- **CPU-first slice 1** proves the inversion on the implemented CpuTbb path with five gates (byte parity, worker-count invariance, mirror-oracle traversal parity, shape-hash stability/rejection, fail-closed UX) and exactly one new codegen artifact (the manifest emitter). GPU sequencing is unchanged from the audit.
- **The one genuine fork surfaced is RESOLVED (owner ruling):** the voxel content manifest is a **FIRST-CLASS handled TYPE** — the vocabulary declaration AND the handshake between dispatch patterns and domain worlds. A domain world declares its manifest; a dispatch pattern declares the manifest it consumes; compatibility = **subset-compatible-with-defaults** (the missing-data-policy machinery reused as the check — graceful by construction), kept **DISTINCT** from the SDI's exact conformance. The hand `SemanticId`/`FieldKind` enum is superseded by generated faces of the declared manifest (Q1 closed; §6, C-3, C-7, §9).

---

## 9. Addendum: Manifest-as-handshake — supersedes TaskConsumer §2.2 vocabulary + Q2

*(The [TaskConsumer Contract Audit](TaskConsumer-Contract-Audit-2026-09-07.md) is ratified and lives on its own branch; it is not edited here. This addendum records how the owner's first-class-manifest ruling revises its attribute-vocabulary part, so the controller can apply one ruling to both docs from one place.)*

**What the audit says (§2.2 + its Q2).** The audit's `Attribute{semantic, elemCount, fieldKind, role, source, missing}` record reuses the union channel list "verbatim", grounds it in the existing hand enums (`SemanticId`/`FieldKind`, `VoxelChannelFormat.h`), sketches `Semantic` as a "codegen-emitted mirror-enum (SdfOpCodes.g.h style)", and makes the load-bearing claim that the Physics-Spec's `VxFieldSemantic` and the render `SemanticId` are "the same enum, extended" — **one Semantic vocabulary** — with its Q2 asking the owner to confirm one unified enum vs two vocabularies with a mapping.

**How the ruling revises it:**

1. **§2.2's vocabulary artifact changes — no free-standing `Semantic` enum, hand-authored or generated.** The voxel content manifest is itself the first-class vocabulary TYPE (C-3): semantics exist as declared elements OF a manifest, their values minted by the manifest declaration and generated into each language face. The audit's `Attribute` record survives intact — as the manifest's per-element entry, not as a row referencing an external enum.
2. **§2.2's "subset request over the union list" is PROMOTED from usage pattern to the conformance rule itself.** The audit already had consumers making subset requests over the union list; the ruling makes **subset-compatible-with-defaults** the handshake rule (C-7 Rule 1): the dispatch pattern's consumed manifest against the domain world's declared manifest, every gap covered by optional/default handling — the audit's own §2.3 missing-data machinery reused as the compatibility check. Graceful degradation by construction.
3. **Audit Q2 dissolves rather than resolves.** "One unified enum extended to physics, or two vocabularies with a mapping?" — neither. There is no global enum to unify: a physics domain world declares its manifest (Mass/Friction/Stress/…), a render world declares its own; a dispatch pattern handshakes with whichever world covers — or defaults — its consumed manifest. Unification happens **at the handshake, by subset-compatibility**, not up-front by enum union; collision-freedom is a per-manifest-declaration concern, not a global-namespace one. (The Multi-Channel union-list intent survives as manifest composition: a manifest may declare a subset of / build on another — §6.)
4. **What does NOT change:** audit §2.3 (source + missing-data policy — now doing double duty as the compatibility machinery), §2.4–§2.7 (domain, step-modifier, backend-as-profile, provider binding), and crucially **§2.8/§5: the SDI stays EXACT-conformance** — the audit's Q8 answer (exact-match on the declared subset, ignore-with-log on extras) stands for the SDI. **Two distinct conformance rules: the manifest handshake is graceful because the manifest is content; the SDI is exact because it is a process-view. Do not conflate them.**

---

**STOP — awaiting owner ratification of the convention (§1), the acceptance-interface form + ladder (§2), the blackboard anchoring (§4), the slice-1 plan + gates (§5), and the remaining §7 questions before any implementation. Q1 is RESOLVED by the manifest ruling, folded into C-3/C-7/§6/§9.**
