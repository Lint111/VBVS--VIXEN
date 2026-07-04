---
title: Voxel Authoring App — Inc1 Plan (milestones M1–M5)
tags: [plan, authoring, voxel, sdf, blender, codegen]
created: 2026-07-03
status: ACTIVE
---

# Voxel Authoring App — Inc1 Implementation Plan

> **For agentic workers:** executed via the post-brainstorm context-manager pipeline — one fresh worker per
> milestone, controller-gated, Opus validation per milestone, progress persisted into this doc (append a
> `## Progress` entry per milestone: state DONE/BLOCKED, commits, test evidence). Spec of record:
> [[Voxel-Authoring-App-Inc1-Design-2026-07]] — read it FIRST; it contains the binary format (VDC1), flatten
> semantics, decisions, and risk notes that bind every task below.

**Goal:** VoxelDocument layered format (canonical kernel-C# → generated C++ & Python codecs) + `vixen_editor`
app that loads/renders/toggles/saves a layered document through the shipped recipe path.

**Architecture:** one canonical C# source in the Yeroket kernel package (mirroring `RecipeContainer`/VRC1);
generated readers/writers vendored verbatim into consumers; documents flatten to VRC1 blobs so the existing
`RecipeRegistry` → octree pool → `BodyInstanceRayMarch` path renders them unchanged; the editor app is a new
`application/editor` target reusing `VulkanGraphApplication` + `UIRenderNode` (RmlUi).

**Tech stack:** C# (netstandard, kernel SourceGenerator~/ dotnet-only), C++23/CMake/Vulkan (VIXEN, WSL builds,
lavapipe gates), Python 3 (generated codec, no deps beyond stdlib `struct`/`dataclasses`).

## Global constraints

- Repos/worktrees/branches: see design §8. **All work happens in those two worktrees.** Never touch the main
  VIXEN checkout (a parallel debugging session owns it).
- Append-only binary contracts: VDC1 field order/values per design §4 exactly; layer-op enum
  `union=0, smooth_union=1, subtract=2, intersect=3`; magic `0x31434456`, version `1`.
- Vendored `*.g.h` / `*.py` artifacts are **VERBATIM** copies of generator output — never hand-edit.
- Yeroket: do NOT commit `RoslynAnalyzers/SDFNodeGenerator.dll` unless generator *source* changed this
  milestone; otherwise `git checkout --` it before committing.
- rtk git masks exit codes: after every checkout/merge/commit, verify with `git log --oneline -1` +
  `git status -sb` explicitly.
- VIXEN logging: `NODE_LOG_*` macros to file, never console. Coding style: match neighboring files.
- Nodes use base `NodeInstance` `SetDevice`/`GetDevice` — never a private `device_` member.
- TDD: write the failing test first within each task where a test target exists. Commit per task.
- No pushes; no merges to any `main`. HacknPlan logging is deferred to session end (controller).

---

## Milestone Map (context-manager pipeline, pinned 2026-07-03)

| # | Milestone | Repo/worktree | Implementer | Validator |
|---|---|---|---|---|
| M1 | Canonical VoxelDocument + C++ reader/writer emission | Yeroket `.worktrees/voxeldoc-inc1` | Sonnet | Opus |
| M2 | Python codec + opcode constants + C#↔Python golden | Yeroket `.worktrees/voxeldoc-inc1` | Sonnet | Opus |
| M3 | Vendor artifacts + VoxelDocumentFlattener + parity tests | VIXEN `.claude/worktrees/voxel-authoring-inc1` | Sonnet | Opus |
| M4 | `vixen_editor` app: load/render/toggle/save + live gate | VIXEN `.claude/worktrees/voxel-authoring-inc1` | Sonnet | Opus |
| M5 | Docs, index, handoff | both | Sonnet | Opus |

---

## M1 (Yeroket) — canonical `VoxelDocument` + C++ reader/writer emission

**Worktree:** `/home/liory/Github/Yeroket-Fantasy/.worktrees/voxeldoc-inc1`

**Files:**
- Discover first (read-only): the canonical `RecipeContainer` C# source — locate via
  `grep -rn "class RecipeContainer\|struct RecipeContainer" Packages/com.yeroket.utility.kernel-framework --include="*.cs"`
  (namespace `Yeroket.GraphFramework.VM`); its writer/tests
  `SourceGenerator~/Tests/RecipeContainerWriterTests.cs`; the emitter
  `SourceGenerator~/Transpiler/RecipeContainerEmitter.cs`; and **where the build writes
  `RecipeContainer.g.h` in-process** (grep the generator/build hooks for `RecipeContainer.g.h`).
- Create: `VoxelDocument.cs` **beside** the `RecipeContainer.cs` canonical source (same namespace
  `Yeroket.GraphFramework.VM`).
- Create: `SourceGenerator~/Transpiler/VoxelDocumentEmitter.cs` (mirror `RecipeContainerEmitter.cs`).
- Create: `SourceGenerator~/Tests/VoxelDocumentWriterTests.cs`, `SourceGenerator~/Tests/VoxelDocumentEmitterTests.cs`.
- Modify: the artifact-write hook so the build also emits `VoxelDocument.g.h` beside `RecipeContainer.g.h`.

**Canonical C# shape (write exactly this contract; adapt naming/style to the RecipeContainer file you read):**

```csharp
namespace Yeroket.GraphFramework.VM
{
    // VDC1 — layered voxel/SDF document. Design of record:
    // VIXEN Vixen-Docs/01-Architecture/Voxel-Authoring-App-Inc1-Design-2026-07.md §4.
    public static class VoxelDocument
    {
        public const uint Magic = 0x31434456u;   // 'VDC1'
        public const uint FormatVersion = 1u;
        public const int  LayerNameBytes = 32;
        public const byte LayerTypeRule = 0;      // 1 = drawn RESERVED (reader rejects at v1)
        public enum LayerOp : byte { Union = 0, SmoothUnion = 1, Subtract = 2, Intersect = 3 } // append-only
    }

    public struct VoxelDocumentHeader   // 32 B — mirror RecipeContainerHeader's struct discipline
    {
        public uint Magic, FormatVersion, ChannelCount, LayerCount;
        public uint Reserved0, Reserved1, Reserved2, Reserved3;
    }

    public struct VoxelDocChannel       // 16 B — MUST stay field-identical to codegen ChannelDesc (design Q5)
    {
        public uint SemanticId, ElemCount, ChannelBaseFloats /*==0 in documents*/, FieldKind;
    }

    public struct VoxelDocLayerHeader   // 48 B fixed, then InstructionCount × SDFInstruction (132 B)
    {
        public byte Type, Op, Enabled, Pad0;
        public float BlendRadius;
        public byte[] Name;             // exactly 32 B, UTF-8 NUL-padded (serialize as raw bytes)
        public uint InstructionCount, Pad1;
    }
    // + a writer (Write(doc) -> byte[]) and reader (TryRead(byte[], out ...)) matching how
    //   RecipeContainer's writer/reader pair is structured; strict total-length check like VRC1.
}
```

**Emitter output contract (`VoxelDocument.g.h`, namespace `Yeroket::Sdf::Generated`):** mirrors
`RecipeContainer.g.h` — provenance comment, packed structs with `static_assert`s (32/16/48), reuse of the
existing generated `SdfInstruction` (132 B), `VoxelDocumentView { header; channels*; layers }` where layers
expose `(const VoxelDocLayerHeader*, const SdfInstruction*)` per index, `ReadVoxelDocument(blob,len,out)`
with magic/version/length/type gates, **and** `WriteVoxelDocument(doc-parts…, std::vector<uint8_t>& out)`
(the editor saves in M4). Magic/version emitted **from the canonical consts** (`0x31434456u`), never hand-typed.

**Steps (TDD, commit per green step):**
1. Failing tests in `VoxelDocumentWriterTests.cs`: golden header bytes (magic/version at offsets 0/4);
   C# write→read identity for a 2-channel/3-layer doc; wrong-magic, truncated-length, `Type==1`,
   `ChannelBaseFloats!=0` all rejected; name is NUL-padded to exactly 32 B.
2. Implement `VoxelDocument.cs` until green.
3. Failing tests in `VoxelDocumentEmitterTests.cs` (mirror `RecipeContainerEmitterTests.cs`): emitted header
   contains `0x31434456u`, the three `static_assert` sizes, `ReadVoxelDocument` + `WriteVoxelDocument`, and
   provenance line; consts sourced from canonical (assert the emitter references `VoxelDocument.Magic`).
4. Implement `VoxelDocumentEmitter.cs` + wire the artifact write; build; confirm `VoxelDocument.g.h` appears
   in the same output location as `RecipeContainer.g.h`, byte-stable across two consecutive builds.
5. Append-only tripwire test: pinned literals for `Magic`, `FormatVersion`, each `LayerOp` value — a moved
   ordinal fails the test with a message forbidding renumbering.

**Verify:** the kernel test suite command used by prior sessions (discover exact csproj:
`~/.dotnet/dotnet test` the `SourceGenerator~` test project, `-c Release`) — all new tests green, zero
regressions. **Commit** (message `feat(voxeldoc): canonical VDC1 document + C++ reader/writer emission`),
excluding a rebuilt-but-source-unchanged `SDFNodeGenerator.dll`.

**Acceptance:** tests listed above green from fresh output; `VoxelDocument.g.h` exists, regenerates
byte-identically; no hand-authored magic numbers in the emitter.

---

## M2 (Yeroket) — Python codec + opcode constants + C#↔Python golden

**Worktree:** same as M1.

**Files:**
- Create: `SourceGenerator~/Transpiler/VoxelDocumentPythonEmitter.cs` → emits `voxel_document.py`.
- Create/extend: the opcode-enum emission (mirror `EmitSdfOpCodeEnum`, which today emits `SdfOpCodes.g.h`)
  to also emit `sdf_op_codes.py` (same subset selection, same pinned values, append-only).
- Create: `SourceGenerator~/Tests/VoxelDocumentPythonTests.cs`.
- Create: golden asset generator test that writes `sample_tri_layer.vxd` (see M4 asset spec below) to the
  artifact output dir — this exact file is reused by M3 (C++ decode) and M4 (editor render).

**`voxel_document.py` contract:** stdlib-only (`struct`, `dataclasses`, `typing`); little-endian explicit
(`<`); `MAGIC = 0x31434456`, `FORMAT_VERSION = 1` emitted from canonical consts; `read_voxel_document(bytes)`
→ dataclasses (header/channels/layers with `instructions` as raw 132-B records: `op_code, input_mask,
param_mask, data: tuple[float, ...]`); `write_voxel_document(doc)` → bytes; same strict gates as C++/C#
(magic, version, exact total length, type==0 only, channel_base_floats==0). Provenance comment header.

**Steps:**
1. Failing C# test: emitter output contains `0x31434456`, `def read_voxel_document`, `def write_voxel_document`,
   provenance line; `sdf_op_codes.py` contains the same pinned ordinals as `SdfOpCodes.g.h` (compare against
   the canonical enum values in-source, not a copied list).
2. Implement both emitters; build; artifacts appear beside the `.g.h` outputs, byte-stable.
3. Cross-language golden test (C# test, `python3` via subprocess — verify `python3 --version` ≥3.8 first):
   C# writes `sample_tri_layer.vxd`; C# re-reads (identity); Python decodes it and prints a canonical digest
   (header fields, per-layer `(type,op,enabled,blend_radius,name,instruction_count)`, per-instruction
   `(op_code, data[0..3])` at fixed precision); test compares digest to the C#-computed expected string.
   Then Python **re-encodes** (`write_voxel_document(read_voxel_document(b)) == b`) — asserted byte-identical.
4. Commit `feat(voxeldoc): Python codec + opcode constants + cross-language golden`.

**Acceptance:** C#↔Python round-trip byte-identical on the golden; opcode ordinals provably sourced from the
canonical enum; artifacts regenerate byte-stable. (C++ leg of the tri-language golden completes in M3.)

---

## M3 (VIXEN) — vendor artifacts + `VoxelDocumentFlattener` + parity tests

**Worktree:** `/mnt/c/cpp/VBVS--VIXEN/.claude/worktrees/voxel-authoring-inc1` (build with the repo's WSL
CMake preset — read `VIXEN/CLAUDE.md` + `.claude/skills/project-rules/rules/commands.md` in-tree for the
exact configure/build/test commands; render tests run ICD-only on lavapipe, validation layer optional).

**Files:**
- Vendor (VERBATIM from the Yeroket worktree build output): `VIXEN/libraries/SVO/include/Recipe/generated/VoxelDocument.g.h`.
- Vendor golden: `VIXEN/BuiltAssets/documents/sample_tri_layer.vxd` (from M2's generator output).
- Create: `VIXEN/libraries/SVO/include/Recipe/VoxelDocumentFlattener.h`,
  `VIXEN/libraries/SVO/src/Recipe/VoxelDocumentFlattener.cpp` (add to the SVO CMake target beside existing
  Recipe sources).
- Create: `VIXEN/libraries/SVO/tests/test_voxel_document_flatten.cpp` (register in SVO tests CMake).

**Flattener interface (exact):**

```cpp
namespace Vixen::SVO {
// Flattens an enabled-rule-layer document into a VRC1 recipe blob the existing
// RecipeRegistry/octree-pool path consumes unchanged. Design §5 (Voxel-Authoring-App-Inc1-Design-2026-07).
// enabledOverride: optional per-layer override (editor toggles) — nullptr = use document flags.
// Returns false with err set on: empty/zero enabled layers, unknown op/type, opcode outside vendored enum,
// stack-depth would exceed the VM sp<64 guard, malformed view.
bool FlattenVoxelDocument(const Yeroket::Sdf::Generated::VoxelDocumentView& doc,
                          const std::vector<uint8_t>* enabledOverride,
                          std::vector<uint8_t>& outVrc1Blob,
                          std::string& err);
}
```

**Combine-instruction packing:** read the canonical C# op kernels (`SmoothUnion`, `Union`, `Subtract`,
`Intersect`) and/or the shipped recipe-pack builder in Yeroket to determine each combine `SdfInstruction`'s
`opCode` ordinal (from vendored `SdfOpCodes.g.h`), `inputMask`, and which `data[i]` slot carries the smooth
radius. **Do not guess slots** — cite the canonical line in a comment. VRC1 header fields
(`bakeResolution`, `bandVoxels`, `brickDepth`): copy the values the shipped pack-builder/tests use
(grep `test_recipe_pool_render.cpp` / pack-build tooling for the constants).

**Tests (TDD order):**
1. C++ golden decode (completes the tri-language golden): `ReadVoxelDocument` on
   `BuiltAssets/documents/sample_tri_layer.vxd` → header/layer/instruction fields match the pinned digest
   from M2 (hard-code the expected values; they are append-only).
2. Flatten parity on a numeric grid (the authoritative oracle): for the golden doc, evaluate the flattened
   VRC1 program with the CPU `evalRecipe` mirror (existing test utility — locate via
   `grep -rn "evalRecipe" libraries/SVO`) at ≥ 5×5×5 grid points spanning the bounds, and compare against
   **independent composition in the test**: `combine(evalRecipe(layer0), evalRecipe(layer1), op, radius)`
   computed pointwise with the reference formulas (e.g. smooth-union
   `h=clamp(0.5+0.5*(b-a)/k,0,1); mix(b,a,h)-k*h*(1-h)`; subtract `max(a,-b)`; intersect `max(a,b)`;
   union `min(a,b)`). Tolerance 1e-5. This is an independent oracle — do NOT reuse the flattener to build
   the expected values.
3. `enabledOverride` drops a layer: flatten with layer-2 disabled == flatten of a 2-layer doc (byte-compare
   the instruction stream).
4. Error paths: zero enabled layers; `type==1`; unknown `op==4`; `channelBaseFloats!=0` (already rejected at
   read — assert reader gate); a 70-layer single-instruction doc → sp-depth reject with message.
5. VRC1 blob is registry-consumable: `ReadRecipeContainer` (vendored VRC1 reader) accepts the output; header
   `instructionCount` == Σ enabled-layer instructions + (enabledLayers−1) combine ops.

**Verify:** full SVO test suite green on WSL (fresh output pasted into the Progress entry) + no regressions in
the existing recipe tests. **Commit** `feat(svo): VoxelDocument vendored reader + VRC1 flattener (+goldens)`.

---

## M4 (VIXEN) — `vixen_editor` app: load → render → toggle → save + live gate

**Worktree:** same as M3.

**Files:**
- Create: `VIXEN/application/editor/CMakeLists.txt` (target `vixen_editor`; clone the structure of
  `application/main/CMakeLists.txt`, adjust target/source names) and register the subdirectory in
  `VIXEN/application/CMakeLists.txt`.
- Create: `VIXEN/application/editor/source/main.cpp`, `EditorApplication.h/.cpp` — start from
  `application/main/source/VulkanGraphApplication.cpp`'s boot pattern (EngineContext, window-in-graph,
  body-octree procedural scene). Read `test_recipe_pool_render.cpp` + the main app for how
  `RecipeRegistry`/manifest + `SetRecipePool` + one `BodyInstanceGpu` (providerKind PROCEDURAL, recipeId)
  are wired; replicate minimally.
- Create: `VIXEN/application/editor/ui/editor.rml` + `editor.rcss` — layer list bound to the loaded document:
  one row per layer (name, op label, enabled checkbox). **`body { position: absolute; inset: 0; }`** in the
  rcss (0×0-document gotcha). Wire through `UIRenderNode` the same way the existing HUD is mounted (grep for
  the HUD document load in the app/graph setup).
- Create: `VIXEN/libraries/RenderGraph/tests/Nodes/test_editor_document_render.cpp` — the authoritative
  headless live gate (see below); register in the tests CMake beside `test_recipe_pool_render.cpp`.

**Editor behavior (Inc1 exact scope):**
- Startup: path from `argv[1]`, default `BuiltAssets/documents/sample_tri_layer.vxd`; read (vendored reader)
  → keep the raw blob + a `std::vector<uint8_t> enabledOverride` initialized from the document flags — the
  ONLY mutable state in Inc1.
- Render: flatten(doc, &enabledOverride) → register as recipe (in-memory manifest entry, e.g.
  `editor.document.current`, recipeId constant) → `SetRecipePool` → one body instance shows it.
- Toggle: checkbox event sets `enabledOverride[i]` and marks a dirty flag; on the next Execute the app
  re-flattens and re-registers/re-uploads the pool (mirror the P2.3 `SetBakeRecipe` dirty-flag/in-Execute
  re-bake pattern — grep `SetBakeRecipe` for it). No per-tick recompile cascades (`SetInstances` comment in
  `BodyOctreeSceneNode.cpp:132` explains why).
- Save: a UI button/hotkey writes the document back via generated `WriteVoxelDocument` with `enabled` bytes
  replaced by the overrides, to `<input>.edited.vxd` (do not overwrite the golden in-place); log the path.

**Sample asset spec (already produced in M2; listed here for the gate):** 3 rule layers —
`base` box (op ignored, base field), `bulge` sphere overlapping a box corner (op smooth_union,
blendRadius 0.15), `cut` cylinder passing **through** the box so the cut is visible in silhouette from the
default camera (op subtract). Bounds within the shipped default scene scale (mirror the body placement the
recipe render tests use).

**Headless live gate (`test_editor_document_render.cpp`) — authoritative:**
1. Boot the graph headless exactly like `test_recipe_pool_render.cpp` (lavapipe OK), load the golden doc,
   flatten (no overrides), render N frames, PNG readback: assert body pixel count > a floor AND read the PNG
   (validator-style pixel sampling), not just a count.
2. **Ablation (vary ONLY the cut layer):** re-run with `enabledOverride[cut]=0` → pixel delta in the cut
   region > threshold; assert the two runs differ ONLY by that toggle (same doc, same camera). A carved-in
   depression is invisible to counts — the cylinder protrudes, so silhouette pixels change; assert both
   count-delta and a sampled row across the cut.
3. Determinism: same doc + same toggles twice → identical flattened blob bytes.

**Windowed smoke (best-effort, non-blocking):** run `vixen_editor` under WSLg/lavapipe, click a toggle, see
the change, save, confirm the `.edited.vxd` re-loads. If the in-flight WSL runtime bugs (parallel debugging
session) block the windowed run, record BLOCKED-ON-PULL in Progress — the headless gate remains the
milestone gate, and the smoke re-runs after the user's "pull from master" signal.

**Verify:** full WSL build green (all targets incl. `vixen_editor`), RenderGraph + SVO test suites green,
gate test output pasted. **Commit** `feat(editor): vixen_editor app — document load/render/toggle/save + live gate`.

---

## M5 (both repos) — docs, index, handoff (controller-assisted)

- Update `Vixen-Docs/00-Index/Quick-Lookup.md`: Recent Updates row + Architecture row linking
  [[Voxel-Authoring-App-Inc1-Design-2026-07]]; add `vixen_editor` to the app/code-paths section.
- Create `Vixen-Docs/02-Implementation/Voxel-Document-Authoring.md`: how to author/edit/save a `.vxd`,
  the flatten path, the append-only rules, regen commands for the artifacts (from M1/M2 fresh output).
- Update `memory-bank/activeContext.md` (worktree copy): state, next steps (Inc2 = drawn layers/brush MVP +
  stored-view bake; Python field-eval visitor with Blender addon slice).
- Append final `## Progress` summary here (all milestone entries, commit hashes both repos, test evidence).
- Yeroket worktree: confirm clean status (no stray `.dll`), branch left unpushed. VIXEN branch left unpushed.
- Controller then: session summary + memory update + offer HacknPlan logging (delegated, user-approved).

---

## Self-review (against the design + umbrella)

- Umbrella Slice-0a/2 coverage: VDC1 contract (M1), Python codec + catalogue sync seed (M2), engine
  consumption (M3), app + first edit loop + save (M4) — matches design §1 scope; deferred items are recorded
  as non-goals with their landing slices.
- Every binary constant (magic, version, ops, sizes 32/16/48/132) appears identically in design §4, M1 code,
  M2 contract, M3 tests — single source is the canonical C#; tests pin, never re-derive.
- Independent oracles: M3 grid parity composes reference formulas in-test (not flattener output); M4 ablation
  varies exactly one factor; goldens are C#-authored and cross-checked in three languages.
- Known trap coverage: sp<64 (M3 test 4), data-slot packing (M3 cite-the-canonical rule), 0×0 RmlUi body
  (M4 rcss), per-tick recompile cascade (M4 dirty-flag pattern), silhouette-blind subtract gate (M4 protruding
  cutter + sampled row), verbatim vendoring (global constraints), dll gotcha (global constraints).

## Progress

*(appended by the pipeline — one entry per milestone: status, commits, fresh test output)*

- **M4 (vixen_editor app: load/render/toggle/save + live gate): DONE** · VIXEN `feat/voxel-authoring-inc1` commits `4d9cb8c6` (feature) + `743c60aa` (RML asset-path fix found by windowed smoke), on top of M3's `0d27f179` · account hit a monthly spend-limit failure mid-dispatch on the first attempt (zero work lost, immediate redispatch after user raised it) · implementer salvaged sound prior-session drafts (EditorApplication/EditorDocumentModel) rather than restarting; **corrected a wrong instruction in the controller's own brief** — traced real render path and found `SetRecipePool`/Stored-octree (`providerKind=0`) is correct, not the Procedural live-eval path (`providerKind=1`, unrelated hardcoded demo recipes) the brief had specified · **root-caused a genuine coordinate-scale bug**: `BodyInstanceRayMarch.comp`'s transform chain fixes the base octree's world span at `kWorldGridSize=10` regardless of grid resolution (`ShellOctreeGpu.h`), so the golden's small object-centered geometry was invisible at `renderScale=1`; fixed via a derived (not guessed) grid-to-world factor at `renderScale=5.0`, validator recomputed the arithmetic by hand and confirmed exact · **surfaced (correctly, did not touch) a pre-existing bake convention**: `BakeSdfWorld` samples raw grid-integer coords with no center-offset (`RecipeBakeConfig::center` is metadata-only) — every existing render-gate test already hand-authors positive-octant coordinates to work around this; M4 correctly camera-framed the ablation on that corner rather than modifying M3's approved, out-of-scope bake constants (validator confirmed zero diff to `libraries/SVO/`) (subsequently fixed — see [[Voxel-Authoring-App-Inc2a-BakeCenter-Plan-2026-07]]) · ablation result validator-verified as genuinely decisive, not a technicality: `hitWithCut=10190` vs `hitNoCut=96288` (9.4×), `centreDiffPixels=5564/6400` (87%), validator opened both PNGs and visually confirmed a real silhouette split (solid top face vs. two disconnected corner fragments where the cylinder bore punches through) · dirty-flag re-flatten on toggle (no `MarkNeedsRecompile` in the hot path, per the standing gotcha) · Save preserves all fields except `enabled`, writes `<path>.edited.vxd` · windowed smoke ATTEMPTED AND PASSED under WSLg (`DISPLAY=:0`) — found+fixed one more real bug (RML path needed the `assets/ui/` prefix convention `ResolveUiAsset` expects, not a bare filename); app boots, compiles the 29-node graph, stable 40s run with real FOCUSED/UNFOCUSED window events · documented scope boundary (not a bug): checkbox `.checked` CSS is static at load (no RmlUi data-model on editor.rml) — toggle *logic* is fully live/correct via the same path the headless gate proves, only the visual checkmark doesn't flip; a live data-model would require editing UIRenderNode itself, out of Inc1 scope · two WSL build-toolchain gotchas newly documented: `GLFW_INCLUDE_NONE` needed before `<GLFW/glfw3.h>` (else pulls absent `<GL/gl.h>`); any TU including both gaia-pulling headers (RecipeBaker.h/ShellOctreeGpu.h/BodyOctreeSceneNode.h) and RmlUi's UIRenderNode.h must order gaia's headers FIRST (its `std::hash<>` specializations must be visible before robin_hood.h wraps them) · fresh full suite (validator's own independent rebuild): `test_editor_document_render` 3/3, `test_recipe_pool_render` 1/1, `test_recipe_authoring_gate` 2/2, `test_body_octree_lifetime` 2/2, `test_body_instance_raymarch_render` 6/6, `test_voxel_document_flatten` 8/8 — zero regressions · Opus validator **APPROVED**, only a Minor pre-existing/unrelated repo-hygiene nit (two gtest-discovery cache JSONs not gitignored — predates this milestone, no action needed) · 2026-07-03

**★ Inc1 COMPLETE — all 5 milestones (M1–M5 minus docs) done and Opus-approved. ★**

- **M3 (vendor artifacts + VoxelDocumentFlattener + parity tests): DONE** · VIXEN `feat/voxel-authoring-inc1` commit `0d27f179` (single commit, 7 files, 800 insertions, on top of `de5f43a3`) · NOTE: first implementer attempt stalled twice (silent 10-20 min gaps with zero file/commit activity, unreachable via SendMessage — root cause unclear, possibly Monitor/background-wait overhead without proactive reporting per the eventual v2 self-report); a fresh continuation agent (v2) picked up the stalled agent's already-good uncommitted code as-is rather than restarting, verified every referenced symbol via CodeGraph before building, fixed one real but minor bug (missing `#include "Recipe/RecipeRegistry.h"` in the test file), built and committed · new tests `test_voxel_document_flatten` 8/8 PASSED (golden decode, grid-parity vs independent oracle, ref-formula sanity, enabledOverride byte-equality, zero-enabled/unknown-op/deep-stack error paths, registry-consumable) · regression: 10 pre-existing SVO recipe test binaries, 132/132 PASSED, zero regressions (incl. `test_recipe_eval_parity` 91/91) · vendored `VoxelDocument.g.h` sha256 `25242bda…503ee` byte-identical to Yeroket source, no hand-edits · **opcode/packing mapping validator-verified against the REAL vendored enum** (not the comment): layerOp 0→Union(24), 1→SmoothUnion(25), 2→Subtract(26), 3→Intersect(28) — correctly dodges the SmoothSubtract(27)/SmoothIntersect(29) off-by-one trap; blend radius in `data[2]`/Data0.z cross-checked against `evalRecipe`'s SmoothUnion read · Opus validator **APPROVED**, no issues · 2026-07-03
- **M2 (Python codec + opcode constants + C#↔Python golden): DONE** · Yeroket `feat/voxel-document-inc1` commits `68c8768b, 23dd6897, 9b87281a, 16a007a2` (HEAD `16a007a2`; history rewritten once to purge non-deterministic dll rebuilds — old `a420e89c`/`78675ccd` replaced content-identically, validator-verified) · suite `Failed: 4 (pre-existing), Passed: 135, Total: 139` · artifacts (Yeroket worktree `Packages/com.utility.sdf/Runtime/GPU/Generated/`, gitignored-materialized): `voxel_document.py`, `sdf_op_codes.py`, `sample_tri_layer.vxd` (588 B = 32+16+3×(48+132); base Box ∪ SmoothUnion Sphere r=0.15 − Subtract Cylinder halfHeight 1.5 > box halfExtent 1.0, protrudes both faces; 1 sdf channel semanticId=0/fieldKind=1) · packing canonically cited: combine InputMask=3, smoothness **Data0.z**; Sphere `data=(0,0,0,r)`, Box `(hx,hy,hz,0)`, Cylinder `(halfHeight,radius,0,0)` · BONUS: opcode-catalogue golden now asserts vs the REAL 87-op canonical set (closes the old "stage-3 stubs the kernel set" gap) · GOTCHA surfaced: 4 files in that Generated/ dir ARE git-tracked (RecipeContainer.g.h, SdfCoreKernels.g.hlsl/.hpp, SdfOpCodes.g.h — pre-gitignore adds); never blind-`rm -rf` the dir · Opus validator **APPROVED** (initial ISSUES: 1 Minor dll-hygiene → fixed → targeted re-check APPROVED) · 2026-07-03
- **M1 (canonical VoxelDocument + C++ emission): DONE** · Yeroket `feat/voxel-document-inc1` commits `7fd19765..039536b5` · new tests 19/19 green; full suite `Failed: 4, Passed: 119, Total: 123` — the 4 are pre-existing RefKindEnforcementTests (validator-confirmed: last touched `176f7ca7`, before base) · artifact `Packages/com.utility.sdf/Runtime/GPU/Generated/VoxelDocument.g.h` byte-stable (sha256 `25242bda…` across two regens; dir gitignored like RecipeContainer.g.h — VIXEN vendors the tracked copy in M3; NOTE: it `#include "RecipeContainer.g.h"` for SdfInstruction, vendor side-by-side) · Opus validator **APPROVED** · 2026-07-03
