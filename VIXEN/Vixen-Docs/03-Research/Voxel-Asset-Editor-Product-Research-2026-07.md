---
title: Voxel Asset Editor — Product, UX, and Interoperability Research
tags: [research, editor, voxel, sdf, material, blender, ux, procedural]
created: 2026-07-26
status: REFERENCE
---

# Voxel Asset Editor — Product, UX, and Interoperability Research

> **Verdict:** a high-quality voxel asset editor is technically credible as the next evolution of the
> existing `vixen_editor` target. VIXEN should not adopt another editor framework or build a second
> application shell. It should combine four proven interaction models on the shipped VIXEN/AppFlow
> foundation:
>
> 1. a **non-destructive layer stack** for understandable top-level composition;
> 2. **direct 3D sculpt and multi-channel paint tools** for local artistic work;
> 3. **typed procedural subgraphs** inside procedural layers, masks, and materials;
> 4. a **schema-driven context inspector** for authored metadata and simulation-query bindings.
>
> `.vxd` remains the authoritative VIXEN procedural geometry document. Blender is a first-class
> companion tool, but arbitrary Blender edits cannot be reverse-engineered into the original
> procedural intent. The round-trip contract therefore has an explicit lossless path for recognized
> VIXEN data and an explicit baked-layer fallback for foreign or destructively edited data.

**Research date:** 2026-07-26
**Scope:** product model, UI/UX, authoring capabilities, metadata/query integration, interoperability,
Blender round-trip semantics, technical feasibility, and staged implementation.
**Umbrella design:** [Voxel / SDF Layered Editor + Blender Round-Trip](../../../../../docs/superpowers/specs/2026-07-01-voxel-sdf-blender-editor-design.md)
**Current engine increment:** [[../01-Architecture/Voxel-Authoring-App-Inc1-Design-2026-07]]
**Current file contract:** [[../02-Implementation/Voxel-Document-Authoring]]

---

## 1. What exists now

The editor is no longer a placeholder. It is a working vertical slice with the correct engine
ownership boundary.

### Status legend

- ✅ **Done** — present in the repository and part of the current target.
- 🟠 **Partial** — the end-to-end seam exists, but the artist-facing capability is incomplete.
- 📐 **Specified** — a technical direction exists, but there is no complete implementation.
- 💡 **Proposed here** — research recommendation, not yet accepted as a contract.

| Capability | Status | Repository reality |
|---|---:|---|
| Standalone VIXEN editor application | ✅ | `application/editor` builds `vixen_editor` on the same `VulkanGraphApplication` and RenderGraph foundation as other VIXEN apps. |
| Layered procedural document | ✅ | VDC1 stores channel descriptors and ordered rule layers and flattens enabled layers to the shipped VRC1 recipe path. |
| Cross-language document codecs | ✅ | The canonical schema emits C++ and Python readers/writers plus the pinned SDF opcode catalogue. |
| Live procedural preview | ✅ | The loaded document is flattened, registered, uploaded, and rendered through the normal recipe-pool path. |
| Layer visibility UI | ✅ | RmlUi rows are data-model-bound; selection dispatches AppFlow actions rather than editor-specific input verbs. |
| Undo, redo, save action infrastructure | ✅ | The editor uses the shared AppFlow runtime and action stack. |
| General-purpose editor panel shell | 🟠 | RmlUi/AppFlow/view binding exist, but the current UI is one fixed 240 px layer panel rather than a dockable workspace. |
| Incremental document evaluation | 🟠 | Dirty/reconcile flow exists, but a layer edit currently flattens and rebakes the whole document. |
| Layer selection scale | 🟠 | The current enabled-state bridge is a 32-bit mask, limiting the present view/controller path to 32 layers. |
| Drawn voxel layers and brushes | 📐 | `type=1` is reserved by the design; the VDC1 reader intentionally rejects it. |
| Per-layer material/channel writes | 📐 | Runtime channels exist, but authoring layers do not yet write material values. |
| Procedural node-graph editor | 📐 | The recipe VM and op catalogue exist; graph authoring, parameters, sockets, and graph serialization do not. |
| Simulation metadata/query authoring | 📐 | The Undertow query roles are designed; there is no schema-driven editor or preview-context adapter. |
| Blender add-on | 🟠 | Python codecs/opcodes are generated; the field evaluator, UI, identity manifest, and import/export policy are not implemented. |
| Runtime/editor operation parity | 🟠 | Shared execution primitives and the dual-facet rule exist; brush and material operations have not yet supplied both consumers. |

### Consequence

The next increment should extend the shipped editor session rather than restart from a mock-up. The
highest-value foundation work is:

1. a document-session model with stable identities and transactions;
2. dockable/workspace UI composition using AppFlow and RmlUi;
3. region- and dependency-aware evaluation;
4. one real drawn-density layer and brush operation shared with runtime editing.

---

## 2. Product north star

The editor should let an author move continuously between **intent**, **direct art**, and **world
context**:

- Build a shape from procedural rules.
- Sculpt or repair local regions by hand.
- Paint physical and visual material values.
- Assign materials or masks procedurally.
- Inspect every intermediate field, channel, query result, and dependency.
- Preview the same asset under several simulation contexts.
- validate and publish a deterministic runtime asset.
- move recognized data to Blender and back without silently losing meaning.

The result is not a general-purpose replacement for Blender or Houdini. It is a **domain editor for
VIXEN-native volumetric assets**, with unusually good exits to those tools.

### Non-goals

- Reimplement Blender's mesh modeling, rigging, animation, UV, or rendering toolsets.
- Recreate all of Houdini's general-purpose dependency graph.
- Make arbitrary polygon edits round-trip into the original SDF program.
- Store a live Undertow world snapshot inside a reusable VIXEN geometry document.
- Build separate editor-only brush or evaluation code.
- Make one unbounded graph contain geometry, paint history, world queries, UI, and publishing.

---

## 3. Comparable-product synthesis

The recommendation is based on interaction and data-model patterns, not visual imitation.

| Product / standard | Strong pattern to adopt | Boundary or failure to avoid | Reuse posture |
|---|---|---|---|
| [Blender Geometry Nodes](https://docs.blender.org/manual/en/dev/modeling/modifiers/generate/geometry_nodes.html) | Reusable node groups, exposed modifier inputs, named attributes, asset reuse, explicit bake points. | A node graph alone is poor for fast local sculpting and can hide evaluation cost. | Interoperate through a VIXEN add-on; do not depend on Blender as the runtime evaluator. |
| [Blender attributes](https://docs.blender.org/manual/en/4.0/modeling/geometry_nodes/attributes_reference.html) and [custom properties](https://docs.blender.org/manual/en/4.0/files/custom_properties.html) | Typed named data plus small metadata attached to stable data blocks. | Domain conversion and tool support are not universally lossless. | Use as an exchange carrier for IDs and metadata, not the canonical schema. |
| [Blender Texture Paint](https://docs.blender.org/manual/en/4.0/sculpt_paint/texture_paint/introduction.html) | Immediate 3D painting with a complementary 2D inspection surface. | UV textures do not naturally represent volumetric material state. | Copy the interaction principle; paint VIXEN channels in volume/object space. |
| [Houdini SOP networks](https://www.sidefx.com/docs/houdini/nodes/sop/index.html) | Inspectable dependency networks, bypass/display flags, reusable assets, and parameter promotion. | A single unconstrained network becomes difficult to teach, navigate, and optimize. | Adopt typed subgraphs inside bounded layer roles. |
| [Houdini geometry attributes](https://www.sidefx.com/docs/houdini/model/attributes.html) and [Geometry Spreadsheet](https://www.sidefx.com/docs/houdini/ref/panes/geosheet.html) | Make invisible authored/derived data directly inspectable by name, type, domain, value, and provenance. | Metadata without schemas becomes a stringly typed dumping ground. | Build a VIXEN Data Inspector with schema-aware editors. |
| [Houdini VDB](https://www.sidefx.com/docs/houdini/nodes/sop/vdb.html) | Named sparse grids with explicit semantic class, storage type, transform, and voxel size. | Grid data does not preserve the procedural network that produced it. | Use VDB as a baked interchange/cache representation. |
| [Substance Painter layer stack](https://experienceleague.adobe.com/en/docs/substance-3d-painter/using/interface/layer-stack/layer-stack) | Paint and fill layers write several semantic channels together; each channel has independent visibility/blend control. | Re-evaluating all upper layers after a low-layer edit can become expensive. | Adopt the channel-semantic layer model and plan region-aware invalidation. |
| [Substance masks and effects](https://experienceleague.adobe.com/en/docs/substance-3d-painter/using/interface/layer-stack/masking-and-effects) | Paintable and procedural masks, nested effects, isolated mask view. | Hidden effect stacks can make causality hard to diagnose. | Show mask provenance and dependencies in the inspector. |
| [Substance Designer graphs](https://experienceleague.adobe.com/en/docs/substance-3d-designer/using/substance-graphs/substance-compositing-graph-key-concepts) | Typed inputs/outputs, subgraphs, exposed parameters, previews, and publishable presets. | Texture-first assumptions do not cover SDF topology or sim context. | Reuse the product model, backed by VIXEN field/channel types. |
| [Material Maker](https://github.com/RodZill4/material-maker) | Open-source graph-authored procedural textures and brushes plus 3D painting in one application. | Its texture/Godot architecture is not a drop-in VIXEN editor core. | MIT: suitable for implementation study; port concepts selectively. |
| [Godot Voxel Tools](https://voxel-tools.readthedocs.io/en/latest/overview/) | Procedural generators and runtime edits share channel-aware volume primitives; block generation is streamable and multi-threadable. | Its own editor docs state that destructive editor tools are absent and modifiers are limited. | MIT: useful algorithm/API reference, not a replacement UI. |
| [Goxel](https://github.com/guillaumechereau/goxel) | Small direct-manipulation voxel toolset, layers, unlimited spatial canvas, broad simple export. | Cube/color editing does not cover SDF programs, typed material fields, or world-query metadata. | GPL-3.0: study behavior; do not copy code into VIXEN without an explicit compatible licensing decision. |
| [TrenchBroom](https://trenchbroom.github.io/manual/latest/index.html) | Fast CSG manipulation, typed external entity definitions, smart property editors, and link visualization. | Raw key/value entity properties are too weak as VIXEN's canonical typed asset contract. | Study UX and schema-driven inspectors; treat code reuse as license-sensitive. |
| [Blockbench](https://github.com/JannisX11/blockbench) | Mode/workspace clarity, approachable panels, format-specific projects, and plugins. | Its own format guidance warns against treating a native project file as generic interchange. | GPL-3.0: pattern reference; VIXEN needs its own authoritative format. |
| [OpenVDB](https://www.openvdb.org/documentation/doxygen/overview.html) | Sparse typed grids separated from index-to-world transforms and metadata; strong CSG/level-set tooling. | A VDB file is evaluated volume state, not editable procedural intent. | Apache-2.0: strong candidate for optional import/export and offline conversion. |
| [USD](https://openusd.org/release/glossary.html) | Stable scene identities, custom data, primvars, composition, variants, and references. | Blender's [USD support](https://docs.blender.org/manual/en/dev/files/import_export/usd.html) does not preserve the full USD composition model and typically exports evaluated/simplified data. | Use for scene/mesh/material interchange, never as the sole VIXEN round-trip record. |

### Licensing conclusion

- **Safe to study:** every product above.
- **Plausible dependency/source reuse:** OpenVDB (Apache-2.0), Material Maker and Godot Voxel
  Tools (MIT), subject to dependency fit and notice requirements.
- **Pattern-only by default:** Goxel and Blockbench are GPL-3.0; TrenchBroom code should also be
  treated as copyleft-sensitive until a specific reuse decision is reviewed.
- **Reference only:** proprietary Substance and Houdini behavior.

This is an engineering screen, not legal advice. Any source incorporation still needs a dependency
and license review at the exact version selected.

---

## 4. Recommended information architecture

### Default workspace

```text
┌─────────────────────────────────────────────────────────────────────────────┐
│ File  Edit  Asset  View  Build     Shape | Sculpt | Material | Context     │
├──────────────────┬──────────────────────────────────────┬───────────────────┤
│ Asset / Outliner │                                      │ Context Inspector │
│                  │             3D Viewport              │                   │
│ ▾ Asset          │                                      │ Selection         │
│   ▾ Geometry     │   authoring/runtime preview toggle   │ Parameters        │
│   ▾ Materials    │   channel/isolation overlays         │ Material channels │
│   ▾ Contexts     │   query/provenance visualization     │ Query bindings    │
│   ▾ Exports      │                                      │ Validation        │
├──────────────────┼──────────────────────────────────────┼───────────────────┤
│ Layer Stack      │ Graph / 2D Slice / Data Inspector / Diagnostics          │
│ visibility, op,  │ typed sockets, intermediate previews, values, cook cost  │
│ mask, solo, lock │                                                           │
└──────────────────┴───────────────────────────────────────────────────────────┘
```

Panels should be dockable and persist as a named layout, but the first implementation only needs
three fixed regions plus a collapsible bottom surface. AppFlow should own workspace state and
commands; RmlUi should remain a view of that state.

### Workspaces

| Workspace | Primary interaction | Required secondary views |
|---|---|---|
| **Shape** | Build/reorder rule layers and edit exposed parameters. | Layer stack, object hierarchy, graph, bounds. |
| **Sculpt** | Add/erase/smooth/stamp drawn density. | Brush settings, active drawn layer, orthographic slices. |
| **Material** | Paint/fill/generate semantic material values. | Channel list, mask stack, material preview, histogram/range. |
| **Procedural** | Author typed geometry/material/mask subgraphs. | Node library, node inspector, intermediate result previews. |
| **Context** | Bind authored inputs to simulation queries and preview contexts. | Query builder, provenance, dependency/link view, resolved values. |
| **Validate / Publish** | Resolve errors, bake caches, and create runtime artifacts. | Diagnostics, dependency manifest, export targets, diff summary. |

Changing workspace reorganizes tools; it does not create a different document or conversion step.

### Selection and inspection rules

1. Selection is stable across workspaces and addresses a persistent UUID, never only a row index or
   display name.
2. The inspector shows **authored value**, **resolved value**, **source/provenance**, and **override
   state** separately.
3. Multi-selection exposes only common editable properties and makes mixed values explicit.
4. Every procedural node, mask, layer, and query can be soloed or previewed at its output.
5. Every expensive update shows evaluating/stale/error state; stale results must never appear as
   silently current.
6. Destructive or lossy operations show the conversion boundary before execution and produce a new
   layer by default.

---

## 5. One coherent authoring model

A high-quality result needs layers, graphs, strokes, and metadata, but they should not all become the
same abstraction.

### Top-level layer roles

| Layer kind | Purpose | Typical contents | Runtime facet |
|---|---|---|---|
| **Rule geometry** | Broad parametric shape and CSG. | A typed SDF subgraph or recognized recipe program. | Deterministic recipe evaluation. |
| **Drawn geometry** | Local authored edits. | Ordered/compacted brush strokes or sparse delta volume. | Dig/build/damage operations. |
| **Material fill** | Assign constant or preset material values under a mask. | Material reference, channel overrides, mask. | Initial material state. |
| **Material paint** | Hand-painted local material variation. | Multi-channel strokes and sparse value deltas. | Wear, repair, coating, contamination. |
| **Material procedural** | Derive material from shape, noise, direction, or context. | Typed field graph, channel outputs, mask. | Deterministic material evaluation. |
| **Group** | Organize, transform, mask, and blend child layers. | Child layer IDs plus group controls. | Same ordered composition. |
| **External/baked** | Hold foreign or intentionally collapsed data. | VDB grids, voxel delta, or imported mesh-to-SDF cache. | Stored provider/cache. |

The layer stack answers **what contributes, in what order, and under what mask**. A subgraph answers
**how one contribution is calculated**. A stroke record answers **what the artist directly changed**.
This prevents the global-spaghetti-graph failure mode.

### Typed graph contract

Sockets should be semantic and closed enough to validate:

- `ScalarField3D`
- `SdfField`
- `MaskField`
- `MaterialIdField`
- `ColorField`
- `VectorField`
- scalar/vector/color parameters
- schema-approved context values

Implicit conversions should be rare and visible. In particular:

- `SdfField → MaskField` requires an explicit threshold/range node.
- material identity is not interchangeable with raw color.
- simulation entities/references do not enter GPU field evaluation directly; a resolver must convert
  them to a declared, deterministic value.

Reusable graphs expose named inputs/outputs, defaults, bounds behavior, version, and a stable asset
ID. Published graph interfaces are append-only within a compatible version.

### Parameter model

Each exposed parameter needs:

- stable ID and display name;
- type, units, range/step, and UI hint;
- authored default;
- optional context binding;
- current resolved value and provenance;
- deterministic fallback;
- invalidation scope/bounds;
- whether a change is runtime-editable.

This is the common surface used by the VIXEN inspector, Blender modifier UI, presets, and runtime
configuration.

---

## 6. Direct sculpting

The first direct tool should be deliberately small:

- add and erase sphere brushes;
- size, strength, falloff, and symmetry;
- one active drawn-density layer;
- continuous stroke resampling;
- a single AppFlow transaction per stroke;
- bounded dirty-region recomputation;
- authoring/runtime use of the same brush operation and payload.

Follow with smooth, flatten/plane, stamp-from-SDF, transform selection, and material-capable brushes.

### Stroke storage

Keep editable strokes until a measured threshold, then compact older operations into a sparse
delta/cache while preserving:

- layer identity;
- source-operation range;
- bake settings and version;
- bounds;
- base dependency/hash;
- an explicit “cannot edit individual compacted strokes” marker.

Undo should operate on semantic transactions rather than complete voxel snapshots. A stroke may
record many samples but is one user action.

### Viewport feedback

The user needs a brush outline, affected bounds, active layer, symmetry planes, voxel/field
resolution, and preview-quality state. A 2D orthographic slice view is valuable for internal
topology that a surface view hides.

---

## 7. Material-value painting and procedural assignment

### Separate material identity from material values

The authoring model should distinguish:

1. **material identity** — a stable reference such as hull steel, glass, insulation, soil;
2. **material values** — color, roughness, metallic, emissive, density, temperature capacity, damage
   resistance, or other registered semantic channels;
3. **material state** — mutable runtime values such as damage, heat, contamination, wear, or wetness.

An authored asset can initialize all three, but runtime state must not be mistaken for immutable
material definition.

### Multi-channel layer behavior

Like Substance Painter, one paint or fill layer may write several channels together, with:

- channel enable/disable;
- channel-specific opacity/blend operation;
- per-layer mask;
- isolate-one-channel preview;
- before/after comparison;
- data range and units;
- a material preset that maps semantic outputs to registered channels.

The brush payload should carry semantic channel writes rather than assuming one hard-coded color
layout. A density-only brush and a density-plus-material brush are therefore the same operation
family with different enabled outputs.

### Procedural assignment

Procedural material layers should support inputs such as:

- position/object coordinates;
- SDF value, gradient, curvature approximation, thickness, and cavity;
- surface normal/direction and height;
- distance to named masks/features;
- noise and reusable graphs;
- resolved, explicitly allowed context parameters.

The output is a set of semantic channel fields plus optional material identity. The editor must
show which writer last contributed to a selected channel at a picked point.

---

## 8. Authored simulation metadata and query-driven generation

### Ownership boundary

VIXEN owns game-agnostic volumetric authoring and evaluation. Undertow owns the meaning of factions,
places, businesses, mechanics, placement eligibility, and its query DSL.

Therefore:

- `.vxd` should remain a portable geometry/channel document.
- A VIXEN asset project/manifest may reference typed metadata schemas without embedding Undertow
  runtime types into the VIXEN core format.
- An Undertow authoring extension supplies schema descriptors, query controls, preview-context
  providers, validation, and publish adapters.
- Runtime world state is resolved into a small typed input block at a declared simulation time; it
  is not copied wholesale into the reusable asset.

### Three distinct query roles

| Role | Question | Authoring surface | Output |
|---|---|---|---|
| **Generation input** | What world data changes this instance's geometry/materials/labels? | Parameter binding in the inspector. | Typed resolved values for recipe evaluation. |
| **Placement/cardinality** | Where may instances exist, and how many? | Asset-level placement rule panel. | Eligibility/cardinality declaration, not geometry. |
| **Mechanic conformance** | What behavior may attach while the asset satisfies a contract? | Conformance panel and validation view. | Verified mechanic bindings and failure/degrade policy. |

They may share Undertow's query language, but they must remain visibly separate because they run at
different times and produce different kinds of results.

### Preview contexts

An asset project stores **preview-context references and overrides**, not authoritative world state.
The Context workspace supports:

- deterministic mock fixtures checked into content;
- viewer-safe exported context slices;
- an optional live provider from a running Undertow host;
- explicit simulation timestamp/epoch;
- source, freshness, and schema-version display;
- fallback preview when a live value is unavailable.

Switching preview context should show a structured parameter/result diff before re-evaluation.

### Binding record

Each generation binding needs at minimum:

```text
binding_id
target_parameter_id
query/schema reference
expected value type + units
selection/cardinality rule
deterministic fallback
resolve phase and simulation epoch policy
staleness/re-resolve policy
viewer-safety classification
```

The resolver output should be hashable and included in the generated-instance provenance. This
makes “why did this asset look like this?” answerable after the world changes.

---

## 9. Blender two-way semantics

### Core rule

**Round-trip data, identity, and declared ownership; never pretend to round-trip lost intent.**

Blender's add-on API can provide operators, panels, import/export, and custom properties. Blender
also supports geometry-node groups and named attributes, but its own [voxel remesh
documentation](https://docs.blender.org/manual/en/4.1/sculpt_paint/sculpting/tool_settings/remesh.html)
warns that remeshing can lose mesh data layers. Those are explicit conversion boundaries, not
implementation details to hide.

### Ownership modes

| Mode | Authority | Blender behavior | Return to VIXEN |
|---|---|---|---|
| **VIXEN native** | `.vxd` rule/layer data | Add-on constructs recognized controls/previews; IDs and generated metadata remain attached. | Lossless for recognized VIXEN nodes, parameters, layer order, masks, and identities. |
| **Blender linked** | A declared Blender object/collection or geometry-node asset | Blender owns the foreign source; VIXEN stores URI/hash/import settings. | Reimport updates a dedicated external layer; local VIXEN overrides remain separate. |
| **Baked interchange** | Evaluated volume/mesh snapshot | User may edit freely after acknowledging loss of procedural intent. | Import as a new external/drawn layer with provenance; never overwrite the native graph silently. |

### Stable identity

Every exported asset, layer, graph, node, material, and exposed parameter needs a UUID stored in the
manifest and Blender custom properties. Names remain editable labels. Reordering or renaming in
either application must not create a new logical element.

### Round-trip manifest

The transfer bundle records:

- VIXEN asset/document/schema versions;
- stable IDs and parent relationships;
- source URI and content hash;
- coordinate system, handedness, up axis, units, object transform, and voxel transform;
- bounds, voxel size, narrow-band/background semantics;
- channel/attribute/material mapping;
- authoring ownership mode for each element;
- export/import timestamp and base revision;
- recognized-feature list and conversion warnings.

### Conflict behavior

If both sides changed a VIXEN-owned property since the common base:

1. do not select a winner by timestamp;
2. show a property/layer-level three-way diff;
3. allow keep-VIXEN, keep-Blender, or duplicate-as-new-layer;
4. save the decision in the import transaction.

### What is actually lossless

| Data | Expected semantics |
|---|---|
| Recognized VIXEN rule nodes/parameters/layer stack | Lossless through the generated codec plus stable manifest IDs. |
| VIXEN material channel names and scalar/vector values | Lossless if Blender can carry the type; otherwise store add-on metadata and show a preview approximation. |
| Arbitrary Blender geometry-node graph | Linked foreign source or baked import, not translated into VIXEN nodes by guesswork. |
| Sculpted polygon mesh | Voxelize/SDF-bake to a new external/drawn layer; topology and procedural history are lossy. |
| OpenVDB named grids | Preserve grid names/types/transforms/metadata where supported; does not preserve VIXEN layer/graph intent by itself. |
| USD scene data | Preserve supported geometry/material/custom data; composition/variant semantics require a manifest because Blender support is incomplete. |
| glTF | Delivery/preview mesh and PBR material carrier; not a volumetric or procedural round-trip format. |

### Add-on architecture

The Blender add-on should consume generated artifacts rather than hand-maintained duplicated
contracts:

1. generated VDC codec and opcode catalogue — already present;
2. generated/verified Python field evaluator for recognized recipe operations;
3. add-on operators/panels and stable-ID helpers;
4. import/export manifest adapter;
5. golden cross-language fixtures and round-trip tests.

A live socket bridge may later improve iteration, but file-first import/export remains the
authoritative, testable path.

---

## 10. Format and ownership recommendation

Do not inflate VDC1 into a universal project file. Add a small project-level aggregate when a real
second payload requires it.

```text
VoxelAssetProject (working concept; extension/name undecided)
├── manifest + stable IDs + versions
├── geometry document reference (.vxd)
├── material layer/graph references
├── metadata schema + query-binding references
├── preview-context references/overrides
├── import/export provenance
└── optional evaluated caches (VRC1/VDB/preview mesh)
```

| Representation | Owner | Purpose | Editable authority |
|---|---|---|---|
| VDC/VXD | VIXEN | Ordered procedural/drawn geometry authoring. | Yes. |
| VRC1 | VIXEN runtime pipeline | Flattened executable recipe. | Generated, not hand-edited. |
| Sparse delta/cache | VIXEN | Drawn layers and bounded evaluated state. | Through strokes/tools, with explicit compaction. |
| Project manifest | VIXEN + extension schemas | Cross-payload identity, provenance, bindings, exports. | Yes. |
| OpenVDB | Interchange/cache | Named sparse fields for DCC exchange. | Foreign/baked boundary. |
| USD | Scene interchange | Transforms, meshes, materials, metadata, scene references. | Supported subset only. |
| glTF | Delivery preview | Evaluated mesh/PBR preview. | Output or baked import. |
| `.blend` | Blender | Blender-native companion authoring. | Blender-owned or add-on-recognized subset. |

---

## 11. VIXEN architecture reuse

### Keep and extend

| Existing seam | Editor use |
|---|---|
| `VulkanGraphApplication` / standard graph | Window, device, render lifecycle, shared application structure. |
| RenderGraph viewport and recipe pool | Authoritative runtime-equivalent preview. |
| RmlUi + generated/data-model views | Panels, lists, property controls, diagnostics. |
| AppFlow runtime, action stack, FSM, input binding | Commands, tools, workspaces, undo/redo, save, modal state. |
| Gaia-backed view state | Selection/layer/session projections and incremental UI reconciliation. |
| VDC1/VRC1 and generated codecs | Canonical document and execution formats. |
| Kernel op catalogue/transpilation | Shared evaluators across C++, shader, C#, and future Python. |

### Add as engine-owned editor services

- `EditorDocumentSession` — loaded project, revisions, dirty state, stable IDs, save/publish.
- `SelectionService` — typed persistent selections and pick results.
- `ToolController` — active tool FSM and pointer capture.
- `EditTransaction` — semantic undo unit with affected bounds and dependencies.
- `FieldDependencyGraph` — layer/subgraph dependencies and dirty propagation.
- `PreviewEvaluator` — interactive/final quality, cancellation, stale-state reporting.
- `ChannelRegistry` — semantic names, types, units, defaults, blend rules, UI hints.
- `SchemaInspector` — descriptor-driven controls with extension providers.
- `InterchangeRegistry` — versioned import/export adapters and conversion reports.

These are generic VIXEN application capabilities. Undertow supplies extension adapters for its
queries and schemas rather than owning the editor loop.

### Runtime-share partition

The partition should be measured by **behavioral ownership**, not current lines of code. Use
**60 / 15 / 17 / 8** as the initial capability budget (each may move by roughly five points as real
tools land): 60% belongs in the minimum runtime-safe asset/edit layer, another 15% is reusable
authoring-session behavior, and the remaining 25% is UI or DCC interoperability. This makes roughly
**75% of editor behavior reusable** by an in-game authoring mode without forcing desktop tooling
into a normal player.

| Partition | Planning share | Proposed target | Normal player build | In-game authoring build | Standalone editor |
|---|---:|---|---:|---:|---:|
| Asset model and deterministic edit/evaluation capabilities | **60%** | `VoxelAssetCore` + `VoxelEditRuntime` | ✅ | ✅ | ✅ |
| Authoring session, transactions, history, and diagnostics model | **15%** | `VoxelAuthoringSession` | Optional | ✅ | ✅ |
| Workspace, tools, panels, and view projection | **17%** | `VoxelAuthoringUi` | No | Optional/reduced | ✅ |
| Blender/OpenVDB/USD/glTF interoperability | **8%** | `VoxelInterchange` adapters/plugins | No | No | ✅ |

The percentages are a scope budget, not a requirement to force arbitrary code into a shared binary.
The hard routing test is:

> If the operation changes, validates, evaluates, serializes, or deterministically queries an asset,
> it belongs below the UI boundary. If it chooses how a human sees or invokes that operation, it
> belongs in the authoring session or UI layer.

### Proposed target graph

```text
Core + generated VDC/VRC/operation schemas
                 │
                 ▼
          VoxelAssetCore
     document · IDs · layers · channels
     manifests · patches · base validation
          │                 │
          ▼                 └──────────────► VoxelInterchange
    VoxelEditRuntime                         VDB/USD/glTF adapters
 brush/material ops · graph compile/eval    Blender uses generated Python
 dependency bounds · deltas · runtime validation
          │
          ├──────────────► VoxelRenderAdapter ──► SVO / VixenApp / RenderGraph
          ├──────────────► VoxelGaiaAdapter   ──► GaiaVoxelWorld
          │
          ▼
  VoxelAuthoringSession
 transactions · undo history · selection model
 preview jobs · diagnostics · AppFlow bindings
          │
          ▼
    VoxelAuthoringUi
 RmlUi panels · workspace/tool FSM · pointer capture
 inspectors · dialogs · visual diagnostics
          │
          ▼
       vixen_editor
```

Dependencies point downward only. In particular:

- `VoxelAssetCore` must not include Vulkan, RenderGraph, RmlUi, AppFlow, Gaia, Blender, or Undertow
  types.
- `VoxelEditRuntime` may depend on domain-blind scheduling and voxel/recipe primitives, but it
  receives storage and context through interfaces/spans; Gaia remains an adapter.
- `VoxelAuthoringSession` may use AppFlow and EventBus, but it must not render or own RmlUi elements.
- `VoxelAuthoringUi` projects session state and emits commands; it does not directly mutate document
  bytes, a voxel pool, or Gaia components.
- RenderGraph consumes evaluated results through `VoxelRenderAdapter`; rendering is not an authoring
  operation and does not belong in the capability library.

### What belongs in the runtime-safe capability layer

`VoxelAssetCore` owns:

- owning document/project models and generated VDC/VRC codecs;
- stable IDs, layer ordering, groups, masks, parameters, channel/material descriptors;
- context-binding records and resolved-input blocks, but not Undertow query execution;
- semantic edit/patch payloads and serialization;
- format/version validation, content hashes, provenance, and deterministic errors;
- file-independent load/save-to-bytes APIs. Filesystem policy stays in an app/host adapter.

`VoxelEditRuntime` owns:

- density and material brush kernels, stroke resampling, affected bounds, and sparse deltas;
- layer composition, graph compilation, field/material evaluation, and channel blending;
- dependency/dirty-region propagation and cache keys;
- operation validation, runtime permission/precondition hooks, and deterministic replay;
- compact/bake operations and the VRC1/recipe-provider handoff;
- CPU/GPU dispatch descriptions through KernelDispatch, without owning a render graph.

These are the capabilities that make “an editor brush and a player mining/damaging/building action
are the same operation” true in code rather than only in design.

### What is shared only when play-time authoring is enabled

`VoxelAuthoringSession` owns:

- `EditTransaction` grouping and semantic undo/redo history;
- selection IDs and active document/layer/tool-neutral state;
- dirty revisions, cancellation, interactive/settled/publish preview requests;
- diagnostics, conversion reports, and validation-result aggregation;
- AppFlow action handlers and bindings over the shared operations.

A normal game does not need desktop-style history, multi-document state, or publish diagnostics.
An in-game construction/creative mode may link this target unchanged or configure a bounded history
budget. Runtime damage/mining can call `VoxelEditRuntime` directly without carrying the session.

### What stays tool-only

`VoxelAuthoringUi` and the editor executable own:

- RmlUi documents/RCSS, docking, workspace layout, property widgets, and toolbars;
- pointer capture, brush cursors, gizmos, viewport overlays, and keyboard profiles;
- file pickers, recent files, save prompts, publish dialogs, and conversion/conflict UI;
- node-graph visual layout and intermediate preview presentation;
- human-readable diagnostics and profiling panels.

`VoxelInterchange` stays optional and out of player builds:

- Blender add-on integration and manifest conflict resolution;
- OpenVDB, USD, glTF, and arbitrary mesh import/export;
- foreign-format parsers, source watching, and DCC-specific conversion warnings.

Keeping foreign parsers out of the player executable reduces binary size, third-party dependency
churn, and attack surface. A game that genuinely imports user assets at runtime can opt into one
adapter explicitly.

### Logical libraries first; DLL only at a real ABI boundary

The current VIXEN build composes `AppFlow`, `SVO`, `GaiaVoxelWorld`, `RenderGraphCore`, and
`VixenApp` as static libraries. Preserve that model initially: a reusable CMake library target
already gives the editor and play runtime one implementation without imposing a dynamic ABI.

Do **not** create one monolithic `EditorTools.dll`. It would pull RmlUi, Blender/interchange code,
filesystem policy, and editor lifecycle into the renderer and would make deployment profiles
impossible.

If hot-loading, managed hosting, or independently versioned plugins later require a physical DLL,
put a narrow facade over `VoxelAssetCore` + `VoxelEditRuntime`, provisionally
`VixenVoxelCapabilities`, with:

- a versioned C function table;
- opaque context/document handles;
- fixed-width POD structs and byte spans;
- explicit result codes and diagnostics buffers;
- paired create/destroy and allocate/free functions;
- no exceptions, STL/GLM/Gaia types, Vulkan handles, or ownership transfer across the ABI.

The existing Undertow `undertow_stage_ffi` / `undertow_gaia_ffi` targets are the local precedent:
domain logic remains in reusable native libraries and the shared object exposes a deliberately
small C ABI.

### Deployment profiles

| Profile | Linked capability set | Intended use |
|---|---|---|
| **Render-only player** | `VoxelAssetCore` read/validate subset + existing recipe/SVO renderer | Display authored assets; no mutation UI. |
| **Destructible player** | `VoxelAssetCore` + `VoxelEditRuntime` + render/Gaia adapters | Damage, mining, construction, material-state edits, network replay. |
| **Creative/in-game editor** | Above + `VoxelAuthoringSession` + reduced runtime UI adapter | Player construction, mod tools, debug authoring. |
| **Standalone `vixen_editor`** | All above + full `VoxelAuthoringUi` + selected interchange plugins | Asset production and publishing. |
| **Headless asset processor/server** | Core + runtime evaluation + selected validators; no Vulkan/RmlUi | Validation, baking, authoritative edit verification. |

Networked play should transmit the same versioned semantic operation payloads used by the editor,
then validate and apply them through `VoxelEditRuntime`. It should not transmit UI gestures or
allow a client to send an already-mutated voxel buffer as authoritative state.

### Extraction map from the current repository

1. Move/rename `application/editor/include/EditorDocumentModel.h` into `VoxelAssetCore`. It is
   already intentionally Vulkan-free and app-free; split its `fstream` methods into a filesystem
   adapter so the core owns bytes, not paths.
2. Keep `VoxelDocumentFlattener` reusable. It currently lives in `SVO`; either expose it through
   `VoxelEditRuntime` initially or later extract its recipe-only dependencies into a lighter
   `RecipeCore`. Do not duplicate the flattener in the editor.
3. Keep `AppFlow` as the shared interaction/action service already consumed by both `vixen_editor`
   and `undertow_host`; compose it into `VoxelAuthoringSession`, not `VoxelAssetCore`.
4. Split `EditorApplication::ApplyDocumentToScene` at its existing seam: shared code produces a
   validated recipe/evaluated delta; the render adapter alone calls `SetRecipePool`,
   `SetBodyInstances`, and residency APIs.
5. Keep `EditorLayersViewBridge`, RmlUi assets, camera framing, capture UI, and pointer/key handling
   in `VoxelAuthoringUi`/`vixen_editor`.
6. Keep the Gaia-backed layer/context providers as adapters. The edit operation accepts an abstract
   channel/delta target; the adapter decides how it maps to `GaiaVoxelWorld`.

### Immediate technical debt to remove

1. Replace the 32-bit layer UI mask with ID-addressed collection state before real documents exceed
   the prototype.
2. Replace full-document rebake on every edit with affected-layer and affected-region invalidation.
3. Give the editor a real message/event adapter where cross-module changes require it; do not let a
   null bus become an implicit editor contract.
4. Complete or explicitly gate non-local AppFlow guards; an unknown external guard must not silently
   pass for publish/destructive actions.
5. Separate document model, view projection, and renderer cache so panel growth does not couple UI
   state to VRC1 rebuilds.

---

## 12. Evaluation and performance model

Substance's published layer-performance guidance notes that changing a low layer requires
reprocessing layers above it. The VIXEN equivalent is more expensive because it may also rebake 3D
fields. The editor should make evaluation incremental from the start.

### Dirty propagation

An edit reports:

- changed stable IDs;
- affected object-space bounds;
- changed output channels;
- dependency revision;
- requested preview quality.

Only downstream layers that read those channels and overlap those bounds become dirty. An
unbounded/noise/global operation explicitly marks whole-asset dirty.

### Preview tiers

- **Interactive:** coarse voxel size/band, cancellable, prioritizes camera-visible bounds.
- **Settled:** normal authoring quality after input stops.
- **Publish:** deterministic target settings with validation and content hash.

The viewport must label interactive or stale output; speed must not masquerade as final accuracy.

### Diagnostics

Expose per layer/node:

- last evaluation duration;
- affected/evaluated voxel count or region;
- cache hit/miss;
- dependency and channel reads/writes;
- error/warning state;
- current revision and stale cause.

This turns procedural performance into authorable information rather than a late profiler surprise.

---

## 13. Validation and publishing

Publishing should fail on contract violations, not subjective aesthetics.

### Hard errors

- unknown or mismatched schema/opcode/channel version;
- missing required material/query input without a deterministic fallback;
- invalid graph types, cycle, stack bound, or unresolved asset reference;
- unsupported runtime operation;
- non-deterministic publish evaluator;
- invalid bounds/voxel transform;
- ownership conflict left unresolved;
- declared mechanic interface not satisfied.

### Warnings

- expensive/global invalidation;
- output outside expected bounds;
- excessive layer/stroke count;
- lossy Blender/USD/VDB conversion;
- stale preview context;
- material channel with no target consumer;
- high-frequency field below target voxel resolution;
- baked layer whose source hash changed.

Publish output includes the runtime artifact, dependency/format manifest, resolved-input hash,
validation report, and optional preview mesh/image.

---

## 14. Recommended implementation sequence

Each slice should finish an artist-visible loop and preserve the dual-facet authoring/runtime rule.

### Slice A — editor session and workspace foundation

- Extract the runtime-safe `VoxelAssetCore` boundary and move the current app-local document model
  behind it; keep filesystem, renderer, Gaia, and RmlUi adapters outside.
- Replace index/bitmask layer identity with stable IDs.
- Introduce document session, selection, transactions, dirty/revision state.
- Build the three-region UI and workspace switcher on AppFlow/RmlUi.
- Add diagnostics/stale-state surface and document validation.
- Preserve the current VDC1 preview/save behavior.

**Exit:** the current sample opens, edits, undoes, saves, and validates through the new shell with no
full-editor architectural fork.

### Slice B — direct density authoring

- Add/erase sphere brush and drawn layer format.
- Share the brush operation with a runtime voxel-edit consumer.
- Region-bounded interactive preview and semantic stroke undo.
- Orthographic slice inspection.

**Exit:** an author can sculpt a usable shape and the same serialized operation can modify a runtime
body.

### Slice C — material authoring

- Channel registry and material identity.
- Multi-channel fill/paint layer, masks, isolate preview.
- One procedural assignment source such as height/slope/noise.
- Runtime material-state mapping and validation.

**Exit:** one asset mixes hand-painted and procedural values across at least three semantic channels.

### Slice D — procedural graph

- Typed graph storage/view, node library, validation, exposed parameters.
- Compile recognized graph to the existing recipe VM.
- Per-node preview and dependency-aware invalidation.
- Reusable subgraph asset.

**Exit:** a graph-authored layer round-trips through save/load and evaluates byte-/tolerance-equivalent
to the existing recipe path.

### Slice E — simulation context

- Schema-inspector extension API.
- Undertow generation-input bindings plus deterministic mock contexts.
- Placement/cardinality and mechanic-conformance panels.
- Live/exported preview provider and provenance diff.

**Exit:** one asset changes geometry/material from a typed Undertow context while `.vxd` remains usable
without Undertow.

### Slice F — Blender recognized round-trip

- Python field evaluator and Blender extension.
- Stable-ID manifest, coordinate/channel mappings, recognized VIXEN UI.
- Golden lossless round-trip for native data.
- Explicit baked import for arbitrary mesh edits.

**Exit:** rename/reorder/parameter edits survive VIXEN → Blender → VIXEN; a foreign mesh returns as a
clearly labeled baked layer without corrupting the native graph.

### Slice G — broader interchange and publish

- OpenVDB named-grid import/export.
- USD/glTF scene and preview adapters.
- conflict UI, conversion reports, publish profiles.
- measured cache/incremental-evaluation refinement.

---

## 15. Feasibility and implementation confidence

These are architecture-confidence ranges, not schedule promises. They assume incremental delivery on
the current target and automated format/evaluator parity tests.

| Outcome | Confidence | Reason |
|---|---:|---|
| Professional editor shell using current VIXEN/AppFlow/RmlUi seams | **90–95%** | The target, actions, views, render path, and save loop already exist. |
| Shared editor/runtime drawn-density brushes | **80–90%** | The dual-facet operation is technically direct; sparse storage, compaction, and good stroke UX are the work. |
| Multi-channel material fill/paint | **75–85%** | Runtime channels exist; authoring semantics, blending, visualization, and persistence must be added. |
| Typed graph compiling to the current recipe VM | **75–85%** | The executable op catalogue exists; graph schema, editor, params, and diagnostics are new. |
| Schema-driven mock-context query authoring | **80–90%** | Descriptor-driven UI and deterministic fixtures are tractable with a clean extension boundary. |
| Live Undertow context preview with robust freshness/provenance | **60–75%** | Cross-process/session lifecycle, viewer safety, epochs, and stale-state behavior need real integration tests. |
| Lossless round-trip of the recognized VIXEN subset in Blender | **70–80%** | Generated Python seams exist; stable identity and property mappings are manageable but version-sensitive. |
| Automatic recovery of arbitrary Blender procedural intent | **10–25%** | Fundamentally ambiguous; it should remain a non-goal and use linked/baked ownership instead. |
| OpenVDB baked-volume interchange | **75–90%** | The format matches sparse named fields well; channel semantics and build/dependency cost need validation. |
| Cohesive first production-quality asset-editor MVP | **70–80%** | Credible if Slices A–E are kept vertical and Blender/general interchange do not block core authoring. |

The main risk is product breadth, not an absent technical foundation. Quality will come from completing
one shape→material→context→publish workflow before adding many tools and formats.

---

## 16. Decisions to make before implementation planning

1. What is the first real asset whose shape, material, and sim-context needs will drive the MVP?
2. Which semantic material channels are required by that asset and the current VIXEN renderer?
3. Does the first drawn layer store editable strokes, sparse deltas, or both from day one?
4. What project-manifest payload is the first concrete reason to introduce a container above `.vxd`?
5. Which context values are safe/deterministic enough for generation at publish and runtime?
6. Is Blender expected to edit recognized procedural parameters first, or only import a preview and
   return baked geometry?
7. Is OpenVDB acceptable as an optional dependency/tool, a separate converter, or not in the VIXEN
   executable?
8. What is the first runtime consumer that proves density and material brush parity?

Recommended default: choose one destructible modular-ship or station component with a procedural hull,
hand-sculpted damage/detail, three material channels, and one faction/context-driven value. It exercises
the intended system without requiring terrain-scale tooling.

---

## 17. Source index

### VIXEN and Undertow

- [[../01-Architecture/Voxel-Authoring-App-Inc1-Design-2026-07]]
- [[../02-Implementation/Voxel-Document-Authoring]]
- [[../01-Architecture/AppFlow-Framework-Design-2026-07]]
- [Umbrella voxel/SDF editor design](../../../../../docs/superpowers/specs/2026-07-01-voxel-sdf-blender-editor-design.md)

### DCC and material tools

- [Blender Geometry Nodes modifier](https://docs.blender.org/manual/en/dev/modeling/modifiers/generate/geometry_nodes.html)
- [Blender attributes](https://docs.blender.org/manual/en/4.0/modeling/geometry_nodes/attributes_reference.html)
- [Blender custom properties](https://docs.blender.org/manual/en/4.0/files/custom_properties.html)
- [Blender Texture Paint](https://docs.blender.org/manual/en/4.0/sculpt_paint/texture_paint/introduction.html)
- [Blender voxel remesh](https://docs.blender.org/manual/en/4.1/sculpt_paint/sculpting/tool_settings/remesh.html)
- [Blender USD import/export](https://docs.blender.org/manual/en/dev/files/import_export/usd.html)
- [Houdini geometry attributes](https://www.sidefx.com/docs/houdini/model/attributes.html)
- [Houdini SOP nodes](https://www.sidefx.com/docs/houdini/nodes/sop/index.html)
- [Houdini VDB node](https://www.sidefx.com/docs/houdini/nodes/sop/vdb.html)
- [Houdini digital assets](https://www.sidefx.com/docs/houdini/assets/index.html)
- [Houdini asset parameter UI](https://www.sidefx.com/docs/houdini/assets/asset_ui.html)
- [Houdini Geometry Spreadsheet](https://www.sidefx.com/docs/houdini/ref/panes/geosheet.html)
- [Substance Painter layer stack](https://experienceleague.adobe.com/en/docs/substance-3d-painter/using/interface/layer-stack/layer-stack)
- [Substance Painter masking/effects](https://experienceleague.adobe.com/en/docs/substance-3d-painter/using/interface/layer-stack/masking-and-effects)
- [Substance Painter geometry masks](https://experienceleague.adobe.com/en/docs/substance-3d-painter/using/interface/layer-stack/geometry-mask)
- [Substance Designer graph concepts](https://experienceleague.adobe.com/en/docs/substance-3d-designer/using/substance-graphs/substance-compositing-graph-key-concepts)
- [Material Maker](https://github.com/RodZill4/material-maker)

### Voxel editors and interchange

- [Godot Voxel Tools overview](https://voxel-tools.readthedocs.io/en/latest/overview/)
- [Godot Voxel Tools generators/modifiers](https://voxel-tools.readthedocs.io/en/latest/generators/)
- [Godot Voxel Tools editor limitations](https://voxel-tools.readthedocs.io/en/latest/editor/)
- [Goxel](https://github.com/guillaumechereau/goxel)
- [TrenchBroom manual](https://trenchbroom.github.io/manual/latest/index.html)
- [Blockbench](https://github.com/JannisX11/blockbench)
- [Blockbench native-format guidance](https://www.blockbench.net/wiki/docs/bbmodel/)
- [OpenVDB overview](https://www.openvdb.org/documentation/doxygen/overview.html)
- [OpenVDB cookbook](https://www.openvdb.org/documentation/doxygen/codeExamples.html)
- [OpenVDB repository/license](https://github.com/AcademySoftwareFoundation/openvdb)
- [OpenUSD terms and concepts](https://openusd.org/release/glossary.html)
