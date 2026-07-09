---
title: Voxel Document Authoring — how to author, run, and regenerate VDC1
tags: [implementation, authoring, voxel, sdf, editor]
created: 2026-07-03
status: ACTIVE
---

# Voxel Document Authoring

How-to for the VDC1 `.vxd` document format and the `vixen_editor` app that loads/renders/edits it.
Design of record: [[../01-Architecture/Voxel-Authoring-App-Inc1-Design-2026-07]] (binary layout, flatten
semantics, decisions). Plan of record: [[../01-Architecture/Voxel-Authoring-App-Inc1-Plan-2026-07]]
(milestones, commits, test evidence).

## 1. What a `.vxd` (VDC1) document is

A `.vxd` file is a layered SDF/voxel document: a small header, a list of channel descriptors, and a list of
**layers**, each of which is itself a self-contained recipe program (the same postfix-stack SDF instruction
format used by the existing VRC1 recipe pipeline).

```
VoxelDocumentHeader (32 B)
  uint32 magic          = 0x31434456   // 'VDC1'
  uint32 formatVersion  = 1
  uint32 channelCount
  uint32 layerCount
  uint32 reserved0..3

ChannelDesc (16 B, reused verbatim from codegen/config-schemas/ChannelDesc.cs)
  uint32 semanticId; uint32 elemCount; uint32 channelBaseFloats /*==0 in documents*/; uint32 fieldKind;

LayerRecord (48 B fixed header + payload)
  uint8  type            // 0 = rule (1 = drawn, RESERVED for Inc2 — reader rejects >0 at v1)
  uint8  op               // 0 union | 1 smooth_union | 2 subtract | 3 intersect
  uint8  enabled          // 0 | 1
  uint8  _pad0
  float  blendRadius      // used by smooth_union; 0 otherwise
  uint8  name[32]          // UTF-8, NUL-padded
  uint32 instructionCount
  uint32 _pad1
  --- payload: instructionCount × SdfInstruction (132 B, the existing VRC1 instruction type)

File layout: header · channelCount×ChannelDesc · layerCount×(LayerRecord·payload) · EOF
(total length must match exactly — strict length check, same discipline as VRC1)
```

Canonical source: `Yeroket.GraphFramework.VM.VoxelDocument` (C#, beside `RecipeContainer`). Generated
consumer artifacts: `VoxelDocument.g.h` (C++ reader/writer), `voxel_document.py` + `sdf_op_codes.py`
(Python codec + opcode constants — the structural seam for a future Blender addon).

### The 4 layer ops

`union=0`, `smooth_union=1` (uses `blendRadius`), `subtract=2`, `intersect=3`. Pinned, append-only —
`replace` is deliberately not in v1 (only meaningful with per-layer material writes, which are deferred).

### Flatten-to-VRC1 semantics

The recipe VM is a postfix stack machine; each layer's `program` is a self-contained postfix expression
netting one field value. Flattening a document into a renderable VRC1 blob:

```
progs = [ (layer.program, layer.op, layer.blendRadius) for layer in doc.layers if layer.enabled and type==rule ]
if progs empty -> error ("nothing to render")
out = progs[0].program              // first enabled layer = base field; its own op is IGNORED
for p in progs[1:]:
    out += p.program                 // push p's field onto the stack
    out += [ CombineInstruction(p.op, p.blendRadius) ]   // pops A,B, pushes combine(A,B)
emit VRC1 blob (magic 'VRC1', formatVersion 1, instructionCount, plus the shipped default
  bakeResolution/bandVoxels/brickDepth) wrapping `out`
```

Implementation: `Vixen::SVO::FlattenVoxelDocument` —
`VIXEN/libraries/SVO/include/Recipe/VoxelDocumentFlattener.h` /
`VIXEN/libraries/SVO/src/Recipe/VoxelDocumentFlattener.cpp`. It takes an optional `enabledOverride` vector
(the editor's live toggle state) so the caller doesn't have to mutate the document to preview a change.
The output VRC1 blob is consumed **unchanged** by the existing `RecipeRegistry` → octree pool →
`BodyInstanceRayMarch` render path — no new render code was needed for Inc1.

Validation performed by the flattener: unknown `op`/`type`, `channelBaseFloats != 0`, stack-depth `sp >= 64`
(the shipped VM guard), zero enabled layers, or an instruction opcode outside the vendored enum are all
rejected with an error string. Combine-instruction packing (opcode ordinal, `inputMask`, which `data[i]`
slot carries the smooth radius) is read from the canonical C# op kernels, not guessed — see
`VoxelDocumentFlattener.cpp` for the cited source lines per op.

## 2. Running `vixen_editor`

```
vixen_editor [path/to/document.vxd]
```

With no argument it loads the default sample, `BuiltAssets/documents/sample_tri_layer.vxd` (a golden
3-layer document: base box, smooth-union sphere bulge at a corner, subtract cylinder that protrudes through
a face so the cut is visible in silhouette).

On startup the app reads the document, flattens it (no overrides yet), registers the resulting VRC1 blob
as an in-memory recipe (`editor.document.current`), calls `SetRecipePool`, and renders one body instance
through it. An RmlUi panel (`application/editor/ui/editor.rml` / `editor.rcss`) lists the layers — name,
op label, enabled checkbox.

## 3. The layer-toggle loop (dirty-flag, not `MarkNeedsRecompile`)

Toggling a layer's checkbox does **not** trigger a full graph recompile. It mirrors the P2.3
`SetBakeRecipe` pattern:

1. The checkbox event sets `enabledOverride[i]` on the app's live state and marks a dirty flag.
2. On the next `Execute`, the app notices the dirty flag, re-runs `FlattenVoxelDocument` with the current
   overrides, re-registers/re-uploads the recipe pool entry, and clears the flag.
3. The next frame renders the new geometry.

This avoids the per-tick recompile cascade that a naive `MarkNeedsRecompile` call would trigger (see the
`SetInstances` comment in `BodyOctreeSceneNode.cpp:132` for why that path is expensive and should not be
hit every toggle). Known Inc1 scope boundary: the checkbox's visual checkmark is static CSS at load time
(no live RmlUi data-model binding on `editor.rml`) — the toggle *logic* is fully live and correct (same
code path the headless gate exercises), only the on-screen checkmark doesn't visually flip. Wiring a live
data-model would mean editing `UIRenderNode` itself, out of scope for Inc1.

## 4. Save

A UI button/hotkey writes the current state back out via the generated `WriteVoxelDocument`, with the
`enabled` byte of each layer replaced by the live `enabledOverride` values — every other field (name, op,
blendRadius, instructions) is preserved unchanged. It never overwrites the input file: the output path is
always `<input>.edited.vxd`. The written path is logged.

## 5. Regenerating the vendored artifacts

The canonical source and generator live in the Yeroket kernel package
(`Packages/com.yeroket.utility.kernel-framework/SourceGenerator~/`), not in VIXEN. To regenerate:

```bash
cd /path/to/Yeroket-Fantasy   # or the relevant worktree
UPDATE_GOLDENS=1 ~/.dotnet/dotnet test \
  Packages/com.yeroket.utility.kernel-framework/SourceGenerator~/Tests/SDFNodeGenerator.Tests.csproj \
  -c Release
```

This runs the in-process build/test path that also (re)writes the generated artifacts —
`VoxelDocument.g.h`, `voxel_document.py`, `sdf_op_codes.py` — into the package's `Generated/` output
directory (`Packages/com.utility.sdf/Runtime/GPU/Generated/`), alongside `RecipeContainer.g.h` /
`SdfOpCodes.g.h`. Confirm byte-stability by running the regen twice and diffing (or comparing sha256) —
the M1/M2 milestones both verified byte-identical regen this way.

**Manual vendoring step** (VIXEN does not build the C# generator itself):

```bash
sha256sum Packages/com.utility.sdf/Runtime/GPU/Generated/VoxelDocument.g.h    # in Yeroket worktree
cp Packages/com.utility.sdf/Runtime/GPU/Generated/VoxelDocument.g.h \
   /path/to/VIXEN/libraries/SVO/include/Recipe/generated/VoxelDocument.g.h
sha256sum /path/to/VIXEN/libraries/SVO/include/Recipe/generated/VoxelDocument.g.h   # must match
```

Never hand-edit the vendored file — it is a verbatim copy, same discipline as the existing
`SdfOpCodes.g.h` / `RecipeContainer.g.h` vendoring.

## 6. Append-only rule for the format

VDC1 is append-only, same discipline as VRC1: the magic (`0x31434456`), `formatVersion` (currently `1`),
and every `LayerOp` ordinal (`union=0, smooth_union=1, subtract=2, intersect=3`) never change value and
are never renumbered. New fields/ops are added by appending new values and/or bumping `formatVersion`,
never by reusing or shifting existing ones. The canonical C# source pins these as `const`/enum literals;
generators emit them from those constants (never hand-typed in emitter code), and a tripwire test in the
Yeroket suite fails if any pinned literal moves.

## 7. Known gotchas worth carrying forward

- **The Yeroket `Generated/` output dir is mostly gitignored, but 4 files in it ARE tracked**:
  `RecipeContainer.g.h`, `SdfCoreKernels.g.hlsl`, `SdfCoreKernels.g.hpp`, `SdfOpCodes.g.h` (pre-gitignore
  adds). Never blind `rm -rf` that directory — check `git status` first.
- **`SDFNodeGenerator.dll` rebuilds non-deterministically** (same size, different bytes, even with no
  source change). Only commit it when the generator's actual C# source changed this session; otherwise
  `git checkout --` it before committing.
- **Smoothness for `SmoothUnion` packs into `SdfInstruction.data[2]`** (i.e. `Data0.z`), not `.x` — verified
  against the real vendored opcode enum during M3, not assumed from a comment.
- **`BakeRecipeInstructionsToSdfWorld` correctly applies `center`** (`p - center` at eval, matching the
  analytic `evalSdf` convention) — fixed in Inc2a. Recipes are authored OBJECT-CENTERED (local origin);
  `center`/`RecipeBakeConfig::center` places them in the grid at bake time. See
  [[../01-Architecture/Voxel-Authoring-App-Inc2a-BakeCenter-Plan-2026-07]].
- **GLFW on headless WSL** needs `#define GLFW_INCLUDE_NONE` before `#include <GLFW/glfw3.h>`, otherwise it
  pulls in `<GL/gl.h>`, which isn't present.
- **Header-order hazard**: any translation unit that includes both a "gaia-pulling" header
  (`RecipeBaker.h`, `ShellOctreeGpu.h`, `BodyOctreeSceneNode.h`) and RmlUi's `UIRenderNode.h` must include
  the gaia-pulling header(s) **first** — gaia's `std::hash<>` specializations must be visible before
  `robin_hood.h` wraps them, or the build fails.
