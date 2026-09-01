---
title: Voxel Authoring App — Inc1 Design (VoxelDocument format + editor skeleton)
tags: [architecture, authoring, voxel, sdf, blender, codegen]
created: 2026-07-03
status: COMPLETE — Inc1 landed in the current engine history (`a72ffba2`; implementation
`4d9cb8c6`, close-out `6046cf6a`).
---

# Voxel Authoring App — Inc1 Design (2026-07)

> Engine-side increment 1 of the **Voxel/SDF Layered Editor + Blender Round-Trip** umbrella design
> (undertow repo: `docs/superpowers/specs/2026-07-01-voxel-sdf-blender-editor-design.md`). This doc is the
> VIXEN-side spec of record for Inc1 and records
> the spec-verification deltas found 2026-07-03. Per the ownership rule, **VIXEN owns the content format**;
> undertow is one consumer. The editor app is a VIXEN application (game-agnostic); undertow hosting and the
> query-injector roles (§4b/§4c/§4d of the umbrella) are LATER, undertow-side increments.
>
> **Product/UX continuation (2026-07-26):**
> [[../03-Research/Voxel-Asset-Editor-Product-Research-2026-07]] reconciles the shipped Inc1/AppFlow
> foundation with the proposed sculpt, material, procedural-graph, simulation-context, and Blender workflows.

## 1. Scope of Inc1

**Build:** the `VoxelDocument` layered format (canonical kernel-C#, generated C++ + Python codecs) and a
minimal but real **`vixen_editor` application** that loads a document, shows its layer stack (RmlUi), and
live-renders it through the existing procedural recipe path. Layer enable/disable is the first edit operation
(full loop: UI → document → flatten → re-render).

**Umbrella-slice mapping:**
- Slice 0 is **split**: **0a (this Inc)** = document contract (DataConfig equivalent) + C++/C#/Python *codec*
  goldens + Python opcode-catalogue emission (Slice 6 seed). **0b (deferred)** = Python *field-eval* visitor
  (transpiler-equivalence in Python) — lands with the Blender addon slice, which is its only consumer.
- Slice 2 is **started**: app shell + layer-list UI + live preview. Full editing UI is Inc2+.
- Slice 3 (drawn layers/brushes), Slice 4 (query roles), Slice 5 (Blender addon) — later increments.
  Blender compatibility is kept **structural** in Inc1: one canonical source, generated Python codec +
  opcode constants, append-only contracts, cross-language goldens.

**Non-goals (Inc1):** drawn layers, brushes, voxelization/stored view of documents, material/channel *writes*
per layer, params/knobs, query injection, Blender addon UI, `replace` layer op, any undertow/`core/` change.

## 2. Spec verification vs. the umbrella design (2026-07-03)

Verified against code at VIXEN `origin/main` = `1af1e65b`, Yeroket `main` = `ca4eb7ad`:

| # | Umbrella claim | Reality | Consequence |
|---|---|---|---|
| D1 | §3 "Voxel grid currently `uint8` density" | Inc3 shipped the **multi-channel SoA channel pool** read by semantic (`sdf`, `color`, `roughness`), canonical `ChannelDesc.cs` (`[GpuStruct]`, 1 uvec4) already exists in `codegen/config-schemas/` | LAYER-1 channel schema **reuses `ChannelDesc` verbatim** — no new channel vocabulary (`density/material_id/albedo` names from the umbrella are superseded) |
| D2 | §2.1 DataConfig should follow ViewSchema/KindSchema pattern | The **config-struct codegen epic** (landed 2026-07-02, after the design was written) gives `[GpuStruct]` + std430 model + C++/GLSL emitters + `CodegenTool~` CLI | DataConfig-style schemas build on `[GpuStruct]` codegen, not a new mechanism |
| D3 | §3 render_recipe "binds a compiled recipe id" | The **recipe authoring pipeline shipped** (2026-06-29): canonical VRC1 container (`Yeroket.GraphFramework.VM.RecipeContainer`, magic `0x31435256`) → generated `RecipeContainer.g.h` reader, `RecipeManifest`/`RecipeRegistry`, memory-budgeted octree pool, `SetRecipePool`, live CSG render gates | The document **flattens to a VRC1 blob**; the entire existing registry→pool→render path consumes documents **unchanged** in Inc1 |
| D4 | §8 visitor table (Burst/Burst4/HLSL/C++ exist; Python to add) | Confirmed: P2.4 catalogue complete (87 opcodes, parity 91/91), HLSL+C++ emitters live, no Python emitter | Accurate; Inc1 adds Python **codec** emission only (see split above) |

**Amendment note (reconciled 2026-07-26):** the umbrella now carries a current-state pointer to this
implementation record and the product/UX research companion. This D1–D4 table remains the detailed Inc1
reality check.

## 3. Decisions (umbrella open questions answered for Inc1)

- **Q1 (VoxelKit home):** no new repo. Canonical C# sources live in the Yeroket kernel package (beside
  `RecipeContainer`); consumers vendor generated artifacts **verbatim** (`.g.h` / `.py`), exactly like
  `SdfOpCodes.g.h` / `RecipeContainer.g.h` / `OctreeConfig.g.h` today. Re-house into a standalone module only
  when a third consumer demands it (recorded as future work, not now).
- **Q2 (Python visitor scope):** codec + opcode constants now; field-eval Python later (with Blender addon).
- **Q3 (kernel-C# location/wall):** Env-2 (Yeroket kernel package) — already true for all canonical sources.
  No `core/` involvement in Inc1 at all.
- **Q4 (opcode set v1):** the shipped P2.4 catalogue (87 ops, pinned append-only enum) *is* the v1 set.
- **Q5 (channel config v1):** `ChannelDesc{semanticId, elemCount, channelBaseFloats, fieldKind}` verbatim;
  Inc3 semantics (`sdf`/`color`/`roughness`) are the vocabulary. In documents `channelBaseFloats` MUST be 0
  (pool layout is bake-time-assigned, not authored).
- **Layer ops v1:** `union=0, smooth_union=1, subtract=2, intersect=3` (pinned, append-only). `replace` is NOT
  v1 (meaningful only with per-layer material writes — deferred with them).
- **Names:** fixed `uint8 name[32]` UTF-8, NUL-padded, in the layer record (Blender round-trip needs layer
  names; a string table is overkill at this size).
- **Params:** omitted from VDC1 entirely (recipes ship `paramMask==0`; dynamic params are kernel-P4). Additive
  version bump adds them when a consumer exists.

## 4. VDC1 — the VoxelDocument container (binary contract)

Same discipline as VRC1: little-endian, packed, strict total-length check, magic+version gate, append-only.

```
VoxelDocumentHeader (32 B)
  uint32 magic          = 0x31434456            // 'VDC1'
  uint32 formatVersion  = 1
  uint32 channelCount
  uint32 layerCount
  uint32 reserved0, reserved1, reserved2, reserved3

ChannelDesc (16 B, canonical — reused verbatim from codegen/config-schemas/ChannelDesc.cs)
  uint32 semanticId; uint32 elemCount; uint32 channelBaseFloats /*==0 in documents*/; uint32 fieldKind;

LayerRecord (fixed 48 B header, then payload)
  uint8  type           // 0 = rule            (1 = drawn RESERVED for Inc2 — reader rejects >0 at v1)
  uint8  op             // 0 union | 1 smooth_union | 2 subtract | 3 intersect
  uint8  enabled        // 0 | 1
  uint8  _pad0
  float  blendRadius    // used by smooth_union; 0 otherwise
  uint8  name[32]       // UTF-8, NUL-padded
  uint32 instructionCount
  uint32 _pad1
  --- payload: instructionCount × SdfInstruction (132 B, the VRC1 instruction, reused verbatim)

File layout: header · channelCount×ChannelDesc · layerCount×(LayerRecord·payload) · EOF (length must match exactly)
```

Canonical C# type: `Yeroket.GraphFramework.VM.VoxelDocument` (+ writer), beside `RecipeContainer`.
Generated artifacts: `VoxelDocument.g.h` (C++ reader, vendored to
`VIXEN/libraries/SVO/include/Recipe/generated/`), `voxel_document.py` + `sdf_op_codes.py` (Python reader/writer
+ opcode constants — the Blender seam), each with provenance headers, byte-stable regen.

## 5. Flatten semantics (document → VRC1)

The recipe VM is a postfix stack machine; every layer `program` is a self-contained postfix expression netting
one field value. Therefore:

```
flatten(doc):
  progs = [ (layer.program, layer.op, layer.blendRadius) for layer in doc.layers if layer.enabled and type==rule ]
  if progs empty -> error (nothing to render)
  out = progs[0].program                       // first enabled layer = base field; its op is IGNORED (documented)
  for p in progs[1:]:
      out += p.program                          // pushes p's field
      out += [ CombineInstruction(p.op, p.blendRadius) ]   // A B op — combines the two stack tops
  emit VRC1 blob: header{magic VRC1, formatVersion 1, instructionCount, bakeResolution/bandVoxels/brickDepth copied from defaults used by shipped recipe packs} + out
```

- Combine instruction packing (opcode value, `inputMask`, which `data[i]` holds smooth radius) is read from the
  **canonical C# op kernels** (`SmoothUnion` etc.) — never guessed (durable lesson: trust canonical data-index
  packing; wrong packing passed parity before).
- Validation: reject unknown `op`/`type`, `channelBaseFloats != 0`, stack-depth `sp >= 64` (the shipped VM
  guard bound), zero enabled layers, or instruction opcodes outside the vendored enum.
- Parity gate: flattened program evaluated by the **CPU `evalRecipe` mirror** must match composing the layer
  fields by hand in the test (independent construction), on a numeric grid — not a silhouette (circular-oracle
  lesson).

## 6. Editor skeleton (`vixen_editor`)

- New app target `VIXEN/application/editor/` cloned structurally from `application/main`
  (`VulkanGraphApplication` pattern): EngineContext boot, window-in-graph, the shipped body-octree procedural
  scene (`BodyOctreeSceneNode` + `BodyInstanceRayMarch`), `providerKind==PROCEDURAL`.
- Startup: load `.vxd` from argv/default sample → flatten → register via `RecipeRegistry`/manifest (in-memory
  blob) → `SetRecipePool` → one body instance renders it.
- RmlUi panel (via shipped `UIRenderNode`): layer list (name, op, enabled checkbox). Toggle → set
  `layer.enabled` → re-flatten → re-register/re-upload pool → next frame shows the change (mirror the
  P2.3 `SetBakeRecipe` dirty-flag/in-Execute pattern for safe re-upload). Known HUD gotcha: RmlUi documents
  need `body{position:absolute; inset:0}` sizing or they render 0×0.
- Sample asset `BuiltAssets/documents/sample_tri_layer.vxd` (base box ∪ smooth-union sphere − subtract
  cylinder that **protrudes** a visible face): built by a C# writer test in Yeroket and committed as a golden
  binary + regenerated by tooling — the same asset drives the cross-language goldens and the live gate.
- Live gate (authoritative, lavapipe): boot `vixen_editor`, render, PNG readback: (a) baseline nonzero body
  pixels; (b) **ablation** — disable the subtract layer via the same code path the UI checkbox calls →
  pixel-delta at the cutter region (vary ONLY that; multi-variable ablations prove nothing).

## 7. Risks / cross-session constraints

- A parallel debugging session owns the main VIXEN checkout (2 unpushed commits + dirty tree at session start,
  WSL/Vulkan runtime fixes in flight). Inc1 branches from `origin/main` (`1af1e65b`) in an isolated worktree;
  **re-run the M4 live gate after the user signals "pull from master"** and rebase then. M1–M3 are immune
  (format/codegen only).
- Yeroket `SDFNodeGenerator.dll` rebuilds non-deterministically — commit it only on real source change,
  else `git checkout --` it (standing gotcha).
- Verify HEAD/branch explicitly after any checkout/merge before chaining.

## 8. Worktrees / branches (this increment)

| Repo | Worktree | Branch | Base |
|---|---|---|---|
| VIXEN | `/mnt/c/cpp/VBVS--VIXEN/.claude/worktrees/voxel-authoring-inc1` | `feat/voxel-authoring-inc1` | `origin/main` @ `1af1e65b` |
| Yeroket | `/home/liory/Github/Yeroket-Fantasy/.worktrees/voxeldoc-inc1` | `feat/voxel-document-inc1` | `main` @ `ca4eb7ad` |

No pushes, no merges to main without explicit user go-ahead. Plan of record:
[[Voxel-Authoring-App-Inc1-Plan-2026-07]].

## 9. Next steps (Inc2 candidates, pointer only — not a plan)

Inc1 is complete (M1–M5, all Opus-approved). Priority order for the next planning session, per the
umbrella design's own slice sequencing:

1. **Slice 3 — drawn layers/brushes** (both facets together): editor-side sculpt tools (add a `type=1`
   drawn layer, brush-stroke authoring UI) AND the runtime dig/build consumer path (undertow-side, later).
   This is the next slice per the umbrella; Inc1 deliberately reserved `LayerRecord.type==1` and rejects it
   at the reader, so the format already has the seam.
2. **Bake center-offset — FIXED (Inc2a, 2026-07-03):** `BakeRecipeInstructionsToSdfWorld` now applies
   `center` (`p - center` at eval, matching `evalSdf`'s existing convention on the analytic path).
   Recipe programs are authored object-centered; every existing hand-placed-at-grid-center test/recipe
   was migrated. See [[Voxel-Authoring-App-Inc2a-BakeCenter-Plan-2026-07]].
3. **Panel/viewport concept with auto-fit centering (Inc2b, NOT YET PLANNED):** a sub-window/viewport
   abstraction inside `vixen_editor` that owns its own render context and derives `center` automatically
   from the loaded document's actual geometry bounds (bounds-midpoint auto-fit) whenever a document loads
   or its layers change — removing the need for ANY caller (editor or future consumers) to pass `center`
   by hand. Deliberately deferred past Inc2a to keep that fix small and mechanical; plan properly once
   Slice 3 (drawn layers/brushes, item 1 below) creates a second real multi-view consumer to design
   against, rather than speculatively now.
4. **Slice 4 — query-aware asset config**: data-injection, placement/cardinality, mechanic conformance.
   Undertow-side increment; no VIXEN format change expected, but the query-injector role (§4c/§4d of the
   umbrella) may want new document metadata.
5. **Slice 5 — Blender addon**: the Python *field-eval* visitor (transpiler-equivalence in Python, Q2 in
   §3) plus the actual Blender-side import/export UI and live bridge. Inc1 already shipped the Python
   *codec* (`voxel_document.py`/`sdf_op_codes.py`) as the structural seam; this slice is pure consumption
   of that seam plus the new eval visitor.
