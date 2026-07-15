# Recipe Container Format Contract

**Status:** Stable (Plan B, Inc4 / M1–M3 complete)  
**Date:** 2026-06  
**Authors:** Engine team

---

## Purpose

This document specifies the binary wire format for SDF recipe containers (`VRC1`),
the in-memory manifest schema (`RecipeRegistry`), and the bake-parameter semantics
used by `BakeRegistryToPool`. It is the authoritative reference for anyone writing
or consuming recipe containers in VIXEN or Yeroket.

---

## 1. Wire Format (binary blob)

A recipe container is a flat byte buffer laid out as:

```
+-------------------+-----------------------------...---+
|  Header (32 B)    |  Instructions[N] (N × 132 B)     |
+-------------------+-----------------------------...---+
```

### 1.1 RecipeContainerHeader (32 bytes)

```cpp
// Yeroket::Sdf::Generated::RecipeContainerHeader (generated; do not edit by hand)
struct RecipeContainerHeader {
    uint32_t magic;            // 0x31435256 == 'VRC1' (LE)
    uint32_t formatVersion;    // must be 1
    uint32_t instructionCount; // N — number of SdfInstruction records that follow
    uint32_t bakeResolution;   // voxel grid size (default 64)
    float    bandVoxels;       // SDF narrow-band half-width in voxels (default 2.5)
    uint32_t brickDepth;       // octree brick depth (default 3 → 8 bricks/axis)
    uint32_t reserved0;        // must be 0
    uint32_t reserved1;        // must be 0
};
static_assert(sizeof(RecipeContainerHeader) == 32);
```

| Field | Notes |
|---|---|
| `magic` | Literal bytes `56 52 43 31` (ASCII 'V','R','C','1') |
| `formatVersion` | Bump on breaking changes; 1 = initial |
| `instructionCount` | Total length = 32 + N×132 bytes |
| `bakeResolution` | Grid side length; must be a power of 2 ≥ 8 |
| `bandVoxels` | Narrow-band SDF half-width; 2.5 is the standard |
| `brickDepth` | ESVO brick-tree depth; 3 gives 8³ bricks in a 64³ grid |

### 1.2 SdfInstruction (132 bytes)

```cpp
// Yeroket::Sdf::Generated::SdfInstruction (generated; do not edit by hand)
struct SdfInstruction {
    uint8_t opCode;     // SdfOpCode enum (see §2)
    uint8_t inputMask;  // reserved; always 0 in VIXEN runtime
    uint8_t paramMask;  // 0 = all-baked; ≠0 = dynamic param, REQUIRED on ReadParam/
                        // ReadParamFloat3 (P4, shipped — see §6), still rejected on every
                        // other opcode
    uint8_t _pad1;      // padding; always 0
    float   data[32];   // Data0..Data7 packed as 8 float4s = 32 floats = 128 bytes
};
static_assert(sizeof(SdfInstruction) == 132);
```

#### data[] field packing (selected opcodes)

| OpCode | data[0] | data[1] | data[2] | data[3] | data[4..31] |
|--------|---------|---------|---------|---------|-------------|
| `Sphere` | cx | cy | cz | radius | — |
| `Box` | halfX | halfY | halfZ | — | — |
| `BoxRounded` | halfX | halfY | halfZ | radius | — |
| `Capsule` | aX | aY | aZ | bX | data[4]=bY, data[5]=bZ, data[6]=radius |
| `Cylinder` | halfHeight | radius | — | — | — |
| `Torus` | outerR | innerR | — | — | — |
| `Ellipsoid` | rx | ry | rz | — | — |
| `Transform` | quatX | quatY | quatZ | quatW | data[4]=txX, data[5]=txY, data[6]=txZ, data[11]=distScale |
| `SmoothUnion` | smoothK | — | — | — | — |
| `Subtract` | — | — | — | — | (binary CSG — no params) |
| `MirrorX` | — | — | — | — | (symmetry — no params) |

Full data-index table lives in the C# canonical source
(`Yeroket.GraphFramework.VM / SDFInstruction`) and is mirrored by the codegen output
in `SdfOpCodes.g.h`. Canonical values in C# are the source of truth; VIXEN mirrors.

---

## 2. Valid Opcodes

The complete set of valid opcodes is defined in:

```
VIXEN/libraries/SVO/include/Recipe/generated/SdfOpCodes.g.h
```

This file is **codegen-generated** from the Yeroket canonical
(`Yeroket.GraphFramework.VM / SDFOpCode.cs`); do not hand-edit.

The enum is `Vixen::SVO::Recipe::SdfOpCode : uint8_t`. All values with explicit
numeric assignments are stable and append-only. Key values as of P2.4 completion:

| Value | Name | Category |
|-------|------|----------|
| 0 | `Sphere` | Primitive |
| 1 | `Box` | Primitive |
| 22 | `Union` | CSG binary |
| 23 | `SmoothUnion` | CSG binary |
| 24 | `Subtract` | CSG binary |
| 25 | `SmoothSubtract` | CSG binary |
| 26 | `Intersect` | CSG binary |
| 35 | `Round` | Modifier |
| 36 | `Onion` | Modifier |
| 37 | `Transform` | Position |
| 40 | `MirrorX/Y/Z` | Symmetry |
| 86 | `RestorePos` | VM control |

See `SdfOpCodes.g.h` for the full 87-entry catalogue.

---

## 3. In-Memory Manifest (`RecipeRegistry`)

`Vixen::SVO::RecipeRegistry` is the host-side recipe store. Before baking, every
recipe must be registered here.

### 3.1 RecipeEntry

```cpp
struct RecipeEntry {
    std::vector<SdfInstruction> bytecode;  // program; must not be empty
    uint32_t bakeResolution = 0;  // 0 = use RecipeBakeConfig default (64)
    float    bandVoxels     = 0.f; // 0 = use config default (2.5)
    uint32_t brickDepth     = 0;  // 0 = use config default (3)
    uint32_t octreeSlot     = kUnbakedSlot; // stamped by BakeRegistryToPool
};
```

### 3.2 Registration rules (enforced by Register())

1. `recipeId` must be unique within the registry.
2. `bytecode` must be non-empty.
3. Every `opCode` must be a valid `SdfOpCode` enumerator.
4. `paramMask` must be 0 on every instruction EXCEPT `ReadParam`/`ReadParamFloat3`, which
   REQUIRE `paramMask != 0` (P4, shipped — see §6).
5. Static stack simulation must not underflow or overflow 64 slots.

Violation returns a `RegisterResult` enum value other than `Ok`.

---

## 4. Bake-Parameter Semantics

`BakeRegistryToPool(RecipeRegistry&, const RecipeBakeConfig&)` bakes all registered
recipes in ascending `recipeId` order and returns a `RecipeBakeResult`.

### 4.1 RecipeBakeConfig

```cpp
struct RecipeBakeConfig {
    glm::vec3 center            = { 32.f, 32.f, 32.f }; // voxel-space centre of each SDF
    uint32_t  defaultResolution = 64;   // grid side length
    float     defaultBand       = 2.5f; // narrow-band half-width in voxels
    uint32_t  defaultBrickDepth = 3;    // ESVO brick-tree depth
    uint64_t  byteBudget        = 0;    // 0 = unbounded; >0 = hard byte cap
};
```

Per-entry overrides (non-zero fields in `RecipeEntry`) take priority over config defaults.

### 4.2 Box SDF origin convention

**Critical:** the Box primitive's SDF evaluator uses the raw voxel coordinate as
the position argument (`pos ∈ [0, n]^3`):

```glsl
// SdfRecipeEval — Box case:
stack[sp++] = SdfCore_Box(pos, halfExtents);
// sdBox: length(max(abs(pos) - halfExtents, 0)) + min(max(abs(pos) - halfExtents), 0)
```

Since `pos ≥ 0`, `abs(pos) == pos`. A `Box(halfExtents=(36,36,36))` therefore
occupies the voxel region `[0, 36]^3`. To centre a box in the 64³ grid, use
`halfExtents = (32, 32, 32)` (the full centred box) or offset with a `Transform`.

Sphere uses an explicit centre parameter and is NOT affected by this convention.

### 4.3 octreeSlot assignment

After baking, `BakeRegistryToPool` stamps `entry.octreeSlot = k` (0-based insertion
order by ascending `recipeId`). The pool's `configs[]` array (one `OctreeConfig` per
octree) aligns with this order. `BodyInstanceGpu.octreeIndex` must match the recipe's
assigned slot:

```
recipeId  10 → slot 0 → octreeIndex 0
recipeId  11 → slot 1 → octreeIndex 1
...
```

### 4.4 Budget

`byteBudget > 0` sets a hard cap on the total serialised pool size
(`nodes + bricks + channelPool` bytes). Exceeding it sets `RecipeBakeResult::ok = false`
with a descriptive `err` string. The pool is still populated; the caller decides
whether to reject or trim.

---

## 5. GPU Layout (`ConcatenatedOctrees` → `BodyOctreeSceneNode`)

`BakeRegistryToPool` returns a `ConcatenatedOctrees` pool. The node consumes it via
`SetRecipePool(pool)` and uploads the data to 6 GPU buffers:

| Binding | Buffer | Content |
|---------|--------|---------|
| 1 | `OCTREE_NODES_BUFFER` | ESVO node words |
| 2 | `OCTREE_BRICKS_BUFFER` | ESVO brick words |
| 3 | `OCTREE_MATERIALS_BUFFER` | material palette |
| 5 | `OCTREE_CONFIG_BUFFER` | `OctreeConfig[N]` (SSBO, std430, 432 B/entry) |
| 11 | `OCTREE_SDF_BUFFER` | SoA SDF channel pool |
| 12 | `OCTREE_BRICKLOOKUP_BUFFER` | brick-grid lookup table |

`OctreeConfig` is 432 bytes (verified by `static_assert`). The shader indexes it as
`configs[inst.octreeIndex]`. **The SSBO must never be bound as UBO** — the M2 change
(`binding 5` → `STORAGE_BUFFER`) is load-bearing for the runtime-sized pool.

---

## 6. Format Evolution

- **P2.4 (2026-06):** Initial VRC1 format; 87 opcodes; `paramMask` reserved.
- **P4 (shipped 2026-07-15):** `paramMask ≠ 0` opcodes (`ReadParam=96`, `ReadParamFloat3=111`);
  per-instance override via `BodyInstanceGpu::recipeParams[6]` (binding 10, uploaded every
  frame — no new GPU binding/buffer). `paramMask` is now a narrow allow-list: `!=0` is required
  on exactly these two opcodes (an explicit "data[0] is a runtime param-array index" marker) and
  still rejected on every other opcode, unchanged from P2.4. See
  [[Recipe-Parameterization-Plan-2026-07]] for the full design/implementation (commits
  `353e6b8e..0ae8ea48` M1-M3, M4 doc-closure commit TBD at merge). Known carried gap: the M4
  baked-vs-virtual parity gate for a `ReadParam` recipe is CODE DONE / LIVE GATE PENDING — see
  [[Known-Issues|KI-032]] (a pre-existing RenderGraph test-harness readback bug, unrelated to P4
  itself; CPU-eval/GLSL-emit/registry/no-recompile claims are all independently live-verified via
  other M1-M3 gates that don't share KI-032's stale-readback path).
