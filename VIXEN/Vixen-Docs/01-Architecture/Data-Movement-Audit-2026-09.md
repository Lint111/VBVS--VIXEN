# Engine Data-Movement Audit — 2026-09

**Scope:** `VIXEN/` engine source in this checkout, from authored/world state to GPU-visible
buffers and images. This is a static audit; payload classes and rankings are source-derived,
not measured on a running device.

**Payload classes:** XS ≤128 B; S 129 B–64 KiB; M 64 KiB–4 MiB; L >4 MiB. For vectors whose
length is runtime-dependent, the table gives the governing formula rather than inventing a
fixture size.

**Verdict vocabulary:** every hop is sentenced as exactly one of `ELIMINATE`, `FUSE`, `DIRECT`,
`BATCH`, or `KEEP`. `ELIMINATE` means the producer already has the consumer's data and the
intermediate has no required external contract. `FUSE` means the transformation remains needed
but can be absorbed into an existing pass or compiler/backend seam. `DIRECT` means the producer
can write the consumer layout or persistent mapping. `BATCH` means the work remains but its
submission/copy granularity should be coalesced. `KEEP` names the correctness or ownership
invariant that makes the hop real.

## Executive finding

The highest-frequency confirmed tax is the instance table. `BodyInstanceGpu` is already a
64-byte std430 record (`VIXEN/libraries/SVO/include/ShellOctreeGpu.h:458-473`), but every
`BodyOctreeSceneNode::ExecuteImpl` allocates a second vector, copies the whole instance list,
allocates a byte vector, and memcpy's that byte vector into the persistent ring
(`VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:573-590`; the packer is a
memcpy-only helper at `VIXEN/libraries/SVO/include/ShellOctreeGpu.h:1378-1386`). This is a
per-frame `N × 64 B` CPU movement with no format conversion.

The next recurring tax is opt-in recipe bucketing: the CPU maps, clears, fills, and unmaps the
skip-mask, bound-sphere, and bucket-meta SSBOs every pre-tick
(`VIXEN/application/main/source/VulkanGraphApplication.cpp:752-834`). The buffers are already
the shader layouts; persistent mapped write-through plus dirty-on-change publication is the
direct fix.

The largest change-time tax is the Stored-SDF source/shell fork. `CreateOctreeBuffers` copies the
full source channel pool and lookup unless wholesale shell admission is active
(`VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:1060-1090`), then shell derivation
copies the reachable bricks and lookup into a compact pool (`VIXEN/libraries/SVO/include/ShellDerive.h:290-359`),
and both compact slots are uploaded even though the renderer binds `SHELL_DATA_BUFFER` and
`SHELL_LOOKUP_BUFFER` (`VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:1199-1203`,
`VIXEN/application/main/source/graph/BuildRenderGraph.cpp:8059-8068`). This is not a per-frame
copy, but it is a high-byte boot/edit cost and should be removed only after the source-versus-shell
descriptor/lifetime decision in T-RULING-1.

## Evidence and prior art boundary

The existing design already establishes the important no-copy boundary: `SerializedOctree`
streams are pre-ordered for shader consumption, and `ChildDescriptor`, brick/channel pools,
lookup, mips, tier refs, and `OctreeConfig` are upload-ready
(`VIXEN/Vixen-Docs/01-Architecture/Voxel-Mutation-Replacement-2026-09.md:5-9`). The current
CPU backend still stages through Gaia internally; the same document explicitly calls that a
compatibility cost and leaves direct recipe-to-page construction as follow-on work
(`VIXEN/Vixen-Docs/01-Architecture/Voxel-Mutation-Replacement-2026-09.md:15-19,54-56`).

This audit does not re-report shipped prior art: the request-time SIMD4 lowering/unrolling
ownership in `RecipeSimd.g.hpp` (`VIXEN/libraries/SVO/include/Recipe/generated/RecipeSimd.g.hpp:1073-1127`),
the existing codegen/golden-check pipeline (`VIXEN/codegen/CMakeLists.txt:236-279`), the
source-derived RenderGraph artifact manifest (`VIXEN/Vixen-Docs/01-Architecture/RenderGraph-System.md:434-440`),
or sparse-mip/tiered-ESVO residency and streamed uploads
(`VIXEN/Vixen-Docs/01-Architecture/Sparse-Mip-ESVO-LOD-Direction-2026-07.md:3-7`). Rows below
identify residue around those mechanisms and new cross-boundary copies.

## Path 1 — recipe/SDF declarations → materialized voxel state → shell → GPU

Path map: `SdfInstruction[]` → lowered SIMD4 program → narrow-band occupancy/active sets →
Gaia voxel entities → `SerializedOctree` streams → concatenated pool → source GPU buffers →
shell compact pool/remap → shell GPU slots and proxy AABBs.

| ID | Producer → consumer | Transformation / evidence | Frequency | Payload | Verdict |
|---|---|---|---|---|---|
| P1-H1 | `SdfInstruction[]` → `LoweredRecipeInstruction[]` | Validates stack shape, records value/position bases, resolves `ReadParam` values, and binds generated SIMD executors; `InvokeRecipe` is rejected because unrolling must already have happened (`VIXEN/libraries/SVO/include/Recipe/generated/RecipeSimd.g.hpp:1073-1127`). | per bake/change | S, instruction-count dependent | **KEEP** — closure validation and bake-time parameter snapshot are the invariant; this is the existing kernel-codegen lowering seam. |
| P1-H2 | lowered program → four-lane SDF evaluator | Subtracts the bake center into a four-lane local array, pads unused lanes, and calls `Evaluate4` (`VIXEN/libraries/SVO/src/BulkMaterialization.cpp:46-63`). | per bake/change | XS working set; M total samples | **KEEP** — the SIMD4 request-time lowerer is the shipped 0dt path; no second lowering pipeline is warranted. |
| P1-H3 | SIMD4 SDF samples → `occupiedBrick[]` → `activeBrick[]` | Pass 1 evaluates x in batches of four and marks `sd <= band`; a 26-neighbour dilation creates the active set for the trilinear/gradient stencil (`VIXEN/libraries/SVO/include/SdfBake.h:167-220`). | per bake/change | M, `O((n/brick)^3)` bytes plus samples | **KEEP** — occupancy and the outward one-brick skirt are distinct correctness predicates; fusing them without retaining the predicate would break the stencil invariant. |
| P1-H4 | active samples → `ComponentQueryRequest` / `VoxelCreationRequest` arrays | Re-evaluates active voxels in SIMD4 groups, creates Density/Color/Roughness/Material/Emission variants, stores spans into reserved component arrays, and calls one `createVoxelsBatch` (`VIXEN/libraries/SVO/include/SdfBake.h:222-297`). | per bake/change | L at dense 64³-class bakes | **FUSE** — extend the existing `CpuRecipeMaterializer` backend to emit its already-required `SerializedOctree` streams without Gaia staging; keep `createVoxelsBatch` only as the editor/compatibility path. The backend seam is already `IMaterializationBackend::materialize` → `SerializedOctree` (`VIXEN/libraries/SVO/include/BulkMaterialization.h:26-55`). |
| P1-H5 | Gaia entity/component storage → serialized material bricks + channel SoA | `SerializeSdf` walks every brick and voxel through `EntityBrickView::getEntityFast`, reads Density/Color/Roughness/Emission, writes material words and canonical SDF/Color/Roughness/Emission lanes, then memcpy's temporary vectors into byte streams (`VIXEN/libraries/SVO/include/ShellOctreeGpu.h:812-915`). | per bake/change | L; 512 voxels × 24 B/channel payload per brick before sparsity | **FUSE** — place the same canonical writes beside the existing SIMD4 lowering/materializer and return `SerializedOctree`; do not invent a parallel emitter or new schema vocabulary. |
| P1-H6 | baked world/registry → `LaineKarrasOctree` | Moves world and registry ownership, rounds the grid up to a power of two, marks signed-distance semantics, rebuilds, and enables body traversal (`VIXEN/libraries/SVO/include/SdfBake.h:407-448`). | per bake/change | M metadata plus rebuilt tree | **KEEP** — Morton addressing, signed-interior retention, and power-of-two world bounds are required by the octree contract. |
| P1-H7 | per-body serialized pages → `ConcatenatedOctrees` | `ConcatenateSdfWithMips` copies nodes, material bricks, channel pool, lookup, mip pool, and tier refs while stamping per-octree bases (`VIXEN/libraries/SVO/include/MipBake.h:358-422`). | boot/change; once per pool rebuild | L | **KEEP** — a shared pool needs contiguous ranges and base offsets. The precomputed-page parameter already removes a repeated serialize+mip pass where a caller has a page (`VIXEN/libraries/SVO/include/MipBake.h:337-383`). |
| P1-H8 | concatenated streams → source host-visible GPU buffers | `CreateHostBuffer` allocates host-visible/coherent memory and maps/copies the bytes (`VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:68-126`); `CreateOctreeBuffers` applies it to nodes, bricks, materials, and configs (`VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:948-1058`). | boot/change | L | **DIRECT** — this is already a CPU write-through upload for the layout-ready streams. The remaining question is whether the source SDF/lookup pair must exist when shell is the active binding; see T-RULING-1. |
| P1-H9 | concatenated mips/tier refs/occupancy → persistent SSBOs | The already-packed pools are copied directly into host-visible buffers, with one-byte placeholders for empty optional streams (`VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:1095-1137`). | boot/change | M or S; formula is serialized vector size | **DIRECT** — retain the placeholders and direct writes; there is no observed format conversion to remove. |
| P1-H10 | full source channel pool + source lookup → shell classification/remap | Shell scans SDF min/max, inverts the dense lookup, dilates surface bricks in 26 directions, copies full channel strides for survivors, and builds a grid→shell-slot remap and proxy list (`VIXEN/libraries/SVO/include/ShellDerive.h:212-359`). | boot/change; value edits use a narrower revalidate path | L read, M–L compact output | **KEEP** — dropping interior/far-exterior bricks is the bandwidth feature, and 26-neighbour dilation is the lossless trilinear-stencil invariant. |
| P1-H11 | `ShellPool` result → two CPU shell-cache slots | The derived pool is copied into slot 0 and moved into slot 1 (`VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:1270-1303`). | boot/change | 2 × compact pool, M–L | **KEEP** — distinct CPU/GPU read/write generations prevent edits from mutating the slot being rendered. |
| P1-H12 | compact shell bytes → each shell GPU slot | Each slot's compact channel pool and remap are mapped and overwritten in place when capacity permits, otherwise recreated (`VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:1306-1347`). | boot/change; dirty value edit | M–L per slot | **DIRECT** — the bytes already match binding 11/12; use persistent mappings or an upload-heap write-through for the existing slot buffers to remove repeated map/unmap churn. |
| P1-H13 | per-octree proxy lists → temporary flat proxy vector | `UploadShellSlot` counts, reserves, and inserts every `ShellProxyAabb` into `flatProxies` before upload (`VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:1349-1361`). | boot/change and every shell-slot upload | S–M; `32 B × proxy count` | **DIRECT** — `ShellDeriveResult` already owns stable `proxyAabbs` (`VIXEN/libraries/SVO/include/ShellDerive.h:100-103`); cache the flattened byte representation once per derive instead of reconstructing it for each slot/revalidate. |
| P1-H14 | flat proxy vector → `proxyAabbBuffer_[slot]` | The temporary vector is copied into a host-visible proxy SSBO (`VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:1362-1367`). | boot/change; repeated with P1-H13 | S–M; `32 B × proxy count × 2 slots` | **BATCH** — publish both slot copies from one cached contiguous payload and one coalesced upload plan; keep two destination buffers because the raster-proxy reader and shell writer use disjoint generations. |
| P1-H15 | compact config mirror → config GPU buffer | Shell compact pool bases are different from source bases, so `CreateShellBuffers` rewrites the config bytes after bootstrapping both slots (`VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:1369-1390`). | boot/change | S; `432 B × octree count` | **KEEP** — `poolBrickBase`/lookup-base rebasing is required for compact addressing; the copy is already direct and only occurs when the compact generation is created. |

### Binary-shell residue

The default non-SDF route still materializes a vector of integer shell cells, creates one Gaia
entity per cell with Density/Color/Material, rebuilds the octree, and serializes it
(`VIXEN/libraries/SVO/include/ShellOctree.h:59-110`; `VIXEN/libraries/SVO/include/ShellOctreeGpu.h:513-620`).
That is a compatibility route distinct from the shipped SIMD4 Stored-SDF materializer. It is
not a dead hop: the entity world is the current owner read by `Serialize`. **KEEP** — the
invariant is the binary shell fixture/authoring contract. A future direct binary page writer
belongs behind the same materialization/backend contract, not in a second codegen pipeline.

## Path 2 — `.vxd` layered document → VRC1 → RecipeRegistry → pool → render

Path map: `.vxd` bytes → non-owning `VoxelDocumentView` → flattened instruction stream/VRC1 →
parsed `RecipeEntry` → validated registry copy → recipe bake/concatenated pool → body node →
shell/source GPU streams.

| ID | Producer → consumer | Transformation / evidence | Frequency | Payload | Verdict |
|---|---|---|---|---|---|
| P2-H1 | `.vxd` file → `rawBytes_` + `VoxelDocumentView` | The editor reads the whole file into owned bytes, then generated `ReadVoxelDocument` installs pointers into that backing storage (`VIXEN/application/editor/include/EditorDocumentModel.h:21-45`). | load/open | L, document-size dependent | **KEEP** — pointer lifetime requires the backing bytes to stay alive while the view is used. |
| P2-H2 | enabled layered document → flattened `SdfInstruction[]` | Flatten validates enabled layers, appends the first layer, appends later layers plus a generated combine opcode, and emits a closed instruction stream (`VIXEN/libraries/SVO/include/Recipe/generated/RecipeSimd.g.hpp:1254-1300`). | every edit/apply | M, instruction-count dependent | **FUSE** — retain flattening semantics but let the render-preview path populate the existing `RecipeEntry`/materializer input directly; keep this instruction stream as the canonical VRC1/save/export representation. |
| P2-H3 | flattened instruction stream → VRC1 byte blob | `FlattenVoxelDocument` writes the VRC1 header and memcpy's the instruction stream behind it (`VIXEN/libraries/SVO/include/Recipe/generated/RecipeSimd.g.hpp:1301-1318`). | every edit/apply | M | **KEEP** — VRC1 is an external/document container contract when bytes are saved or handed to another consumer. |
| P2-H4 | VRC1 blob → `RecipeEntry` | `FlattenToRecipeEntry` allocates a temporary blob, calls `ReadRecipeContainer`, then copies instructions and bake fields into `outEntry` (`VIXEN/application/editor/include/EditorDocumentModel.h:77-97`). | every render-preview edit | M, one full blob plus instruction vector | **ELIMINATE** — on the render-preview-only path the blob is immediately parsed and copied; bypass this round-trip while preserving VRC1 for save/export. |
| P2-H5 | `RecipeEntry` → `RecipeRegistry::entries_` | `Register` validates opcode/param masks/stack shape, derives an implicit LOD ladder when needed, copies to `normalizedEntry`, then moves the normalized entry into the map (`VIXEN/libraries/SVO/include/Recipe/RecipeRegistry.h:168-245`). | every apply/register | M, one recipe | **KEEP** — the registry owns a validated, normalized lifetime and is the lookup source for bake/render metadata. |
| P2-H6 | registry → `BakeRegistryToPool` → body node | The editor registers id 1, bakes the registry, moves the resulting pool into `SetRecipePool`, and supplies one `BodyInstanceGpu` (`VIXEN/application/editor/source/EditorApplication.cpp:337-379`). `SetRecipePool` latches the pool dirty for the next execute (`VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:307-316`). | every edit/apply; next Execute performs rebuild | L pool plus 64 B instance | **KEEP** — registry validation, pooled slot ownership, and fence-safe rematerialization are the current update contract. |
| P2-H7 | editor instance vector → render instance ingress | `VulkanGraphApplication::SetBodyInstances` moves the caller vector into the body node (`VIXEN/application/main/source/VulkanGraphApplication.cpp:4003-4013`), which records count and only requests ring growth when needed (`VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:241-258`). | per scene/apply; per-frame in animated demos | `N × 64 B` | **KEEP** — this ingress is already move-based; the remaining per-frame copy is P3-H3. |

## Path 3 — ECS/world/view state → read-model faces → frame GPU state

### Engine-visible ECS and view boundary

The engine checkout contains Gaia voxel storage and generated view consumers, but not the
external Undertow simulation host that would prove a Gaia `density cell`, `map claim`, or
`deep-field cell` producer call into `SetBodyInstances`. The generated C++ view faces are
therefore audited as CPU data movement, and the absence of a view/read-model→GPU call is
explicitly sentenced rather than inferred.

| ID | Producer → consumer | Transformation / evidence | Frequency | Payload | Verdict |
|---|---|---|---|---|---|
| P3-H1 | UTVA SoA wire → `ViewStore` array-of-struct cells | The SoA reader validates the header/version, allocates `rows` and per-row `ViewCell` arrays, decodes each scalar column, and constructs strings for string columns (`VIXEN/libraries/RenderGraph/src/Ui/ViewWireReaderSoa.cpp:59-145`; allocation is `VIXEN/libraries/RenderGraph/src/Ui/ViewStore.cpp:55-76`). | each wire apply/update | M, row-count dependent; strings add allocations | **KEEP** — the UTVA wire is a schema/version boundary and the `ViewStore` is the renderer/UI binding owner; no GPU consumer is attached to this representation. |
| P3-H2 | `ViewStore` cells → typed deep-field accessors | Generated accessors read fields from `Array(...)[i].Cell(...).{i,f,s}` (`VIXEN/application/main/include/Generated/UndertowDeepFieldCells.typed.g.h:10-72`). | each consumer read | XS per field; no bulk copy | **KEEP** — typed access is a zero-copy face over `ViewStore`, not a new payload. |
| P3-H3 | deep-field typed face → `UndertowDeepFieldCellsReadModel` | `FromSection` reserves an AoS vector, reads every generated field, and pushes a row copy (`VIXEN/application/main/include/Generated/UndertowDeepFieldCells.readmodel.g.h:26-50`). | each read-model refresh | `N × (2 int + 11 float + string)` | **ELIMINATE** — no engine GPU consumer is connected to this read model; do not build this AoS copy on a GPU-bound path. Keep it only for CPU/UI consumers. |
| P3-H4 | map-claim typed face → `UndertowMapClaimsReadModel` | The generated read model similarly reserves and copies body, claimant, kind, strength, confidence, and contested fields (`VIXEN/application/main/include/Generated/UndertowMapClaims.readmodel.g.h:19-37`). | each read-model refresh | `N × 28 B` before container overhead | **ELIMINATE** — the engine checkout has no GPU consumer of this read-model face; a GPU path should consume a source-owned packed page if one is later introduced. |
| P3-H5 | `VoxelCreationRequest` → Gaia entity/component storage | The request is a position plus a span; `createVoxelsBatch` adds entities/components and grouped mode uses `copy_n`, then performs one cache invalidation (`VIXEN/libraries/VoxelComponents/include/ComponentData.h:69-96`; `VIXEN/libraries/GaiaVoxelWorld/src/GaiaVoxelWorld.cpp:556-644`). | bake/edit change | L, one request + component variants per voxel | **KEEP** — the span is deliberately non-owning for synchronous calls and the world is a structural-mutation owner; async boundaries must use the owning request type. |
| P3-H6 | Gaia world components → GPU-facing `SerializedOctree` | Stored-SDF serialization reads Density/Color/Roughness/Emission from Gaia and writes canonical GPU SoA streams (`VIXEN/libraries/SVO/include/ShellOctreeGpu.h:812-915`). | bake/edit change | L | **FUSE** — this is the same Gaia staging residue as P1-H4/P1-H5; remove the ECS round-trip only inside the existing `CpuRecipeMaterializer`/future backend contract. |
| P3-H7 | read-model/view output → `BodyInstanceGpu` | No call in `VIXEN/` consumes `Undertow*ReadModel` or generated view sections to produce body instances. The engine-visible ingress is an explicit `std::vector<BodyInstanceGpu>` setter (`VIXEN/application/main/source/VulkanGraphApplication.cpp:4003-4013`). | absent in current engine path | `N × 64 B` at explicit ingress | **ELIMINATE** — this is a confirmed absent/dead GPU hop in this checkout, not a hidden conversion to report. T-RULING-2 asks the owner whether an external producer should publish a canonical instance page. |

### Per-frame body, recipe, and camera uploads

| ID | Producer → consumer | Transformation / evidence | Frequency | Payload | Verdict |
|---|---|---|---|---|---|
| P3-H8 | `instances_` → `toPack` → packed byte vector | Execute copies the whole `instances_` vector (or creates a placeholder), then `PackInstances` allocates and memcpy's an identical byte layout before the ring memcpy (`VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:573-590`; `VIXEN/libraries/SVO/include/ShellOctreeGpu.h:1378-1386`). | every frame | `N × 64 B` plus transient duplicate | **DIRECT** — write `instances_.data()` directly into the already persistent per-frame ring, retaining only the empty placeholder and capacity clamp. This is the top bandwidth×frequency finding. |
| P3-H9 | body instance list → CPU recipe groups/hot IDs | The bucketed pre-tick takes a const reference, gathers indices into an unordered map, filters hot recipes, and sorts ids (`VIXEN/application/main/source/VulkanGraphApplication.cpp:735-748`). | every frame when bucketed dispatch is enabled | S, `N × 4 B` indices plus map overhead | **KEEP** — grouping is the producer of GPU indirect membership and needs recipe-id knowledge; it avoids a GPU readback. |
| P3-H10 | hot recipe groups → low skip-mask words | `RunRecipeBucketedDispatchPreTick` builds the low-word payload only on an instance-generation change, then publishes changed byte ranges into the selected persistent frame slot (`VIXEN/application/main/source/VulkanGraphApplication.cpp:814-861,876-903`; ring setup is `VIXEN/application/main/source/graph/BuildRenderGraph.cpp:1466-1469`). | generation change or first catch-up of a frame slot | dirty byte ranges; 4 B × low mask word | **DIRECT** — T-078 keeps the existing mask layout and GPU consumers while eliminating per-frame clear/map/unmap work. |
| P3-H11 | `RecipeRegistry` entries → bound-sphere SSBO | The immutable canonical table is rebuilt only when the registry generation changes; each frame slot retains and consumes only the changed entry ranges (`VIXEN/application/main/source/VulkanGraphApplication.cpp:814-841,876-903`). | registry change or first catch-up of a frame slot | dirty byte ranges; 32 B × changed entry | **DIRECT** — T-078 caches the table by registry generation and writes the existing shader layout through persistent frame mappings. |
| P3-H12 | hot groups → bucket-meta SSBO | Hot-recipe metadata is part of the generation-keyed canonical snapshot and is published to each frame slot through retained dirty ranges (`VIXEN/application/main/source/VulkanGraphApplication.cpp:843-861,876-903`). | instance-generation change or first catch-up of a frame slot | dirty byte ranges; 32 B × changed entry | **DIRECT** — T-078 preserves stale cold-recipe entries and publishes only changed meta bytes; no format conversion is needed. |
| P3-H13 | camera pose → `CameraData` | `UpdateCameraData` computes projection/view, derives basis vectors, fills the 64-byte push-constant prefix plus inverse matrices, and retains current/previous view-proj (`VIXEN/libraries/RenderGraph/src/Nodes/CameraNode.cpp:315-395`; layout is `VIXEN/libraries/RenderGraph/include/Data/CameraData.h:17-32`). | every frame | 192 B camera struct | **KEEP** — current/previous matrices and the fixed push-constant layout are separate temporal contracts; this is CPU state, not a redundant GPU buffer. |
| P3-H14 | `CameraData` fields → ray-march push constants | RenderGraph extracts camera fields directly from `CameraData` into reflected push-constant slots (`VIXEN/application/main/source/graph/BuildRenderGraph.cpp:7939-7963`). | every frame | 7 fields; 48 B effective camera pose | **DIRECT** — preserve direct push-constant writes; no intermediate uniform upload is present to remove. |
| P3-H15 | previous camera state → B1 constants → occlusion-cull push constants | PreTick writes the previous view-proj/camera position and live instance count into constants, then stashes the current camera (`VIXEN/application/main/source/VulkanGraphApplication.cpp:690-713`); graph wiring supplies those constants to cull (`VIXEN/application/main/source/graph/BuildRenderGraph.cpp:7550-7565`). | every frame with B1 | XS, 80 B push block | **KEEP** — one-frame history is required because cull reads the last frame's depth tiles; eliminating it would invalidate reprojection. |

## Path 4 — streaming/residency, partial pools, HiZ/depth, staging

| ID | Producer → consumer | Transformation / evidence | Frequency | Payload | Verdict |
|---|---|---|---|---|---|
| P4-H1 | residency decision → brick-pool upload request | `UploadBrickPool` gates on a whole-pool residency request, skips already uploaded data, and passes the serialized brick vector to `VulkanDevice::Upload` (`VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:1401-1441`; device forwards to the uploader at `VIXEN/libraries/VulkanResources/src/VulkanDevice.cpp:565-585`). | residency grant after camera change; also after rebuild | L; serialized brick bytes | **KEEP** — current shipped sparse-mip/tiered-ESVO path intentionally uses whole-pool residency; paged/partial replacement is a future owner decision, not a new row to invent here. |
| P4-H2 | CPU brick bytes → persistently mapped staging bytes | `BatchedUploader::Upload` acquires a pooled staging buffer and memcpy's source bytes into its persistent mapping (`VIXEN/libraries/ResourceManagement/src/Memory/BatchedUploader.cpp:75-132`). | each queued brick upload | L | **DIRECT** — the destination is device-local/transfer-dst and therefore needs a staging copy today; a future upload-heap/write-through path belongs in ResourceManagement, preserving the existing uploader API. |
| P4-H3 | staged bytes → device-local brick buffer | `Flush` submits pending copies as one command buffer, while `UploadBrickPool` flushes immediately and polls completion later (`VIXEN/libraries/ResourceManagement/src/Memory/BatchedUploader.cpp:209-224,410-530`; `VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:1431-1438`). | each residency transition | L and one copy command | **BATCH** — queue the brick copy with the dependent config update in one ordered submission when the uploader can preserve the visibility barrier; remove the forced granularity without exposing `brickResident` early. |
| P4-H4 | brick-copy completion → active config upload | Completion stamps `brickResident`/active compact configs, uploads the config buffer, then waits for that second handle before publishing ready (`VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:1521-1572`). | once per residency transition; polled every frame | S; `432 B × octree count` | **BATCH** — preserve the invariant “brick data visible before resident bit,” but co-submit the ordered config copy or use one transfer timeline point so the current two-phase submission does not add an avoidable frame of latency. |
| P4-H5 | serialized mip pool → mip SSBO | `CreateOctreeBuffers` copies the mip stream into a persistent storage buffer, padding only when empty (`VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:1095-1104`). | boot/change | M, serialized mip bytes | **DIRECT** — already layout-ready and directly copied; sparse-mip prior art covers the residency policy. |
| P4-H6 | serialized tier-ref table → tier-ref SSBO | The node sizes and copies the concatenated `TierRef` table or binds a one-byte unavailable placeholder (`VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:1106-1124`). | boot/change; uncommon tier-crossing content | S–M | **DIRECT** — retain placeholder semantics for unavailable tables and write the existing packed table directly. |
| P4-H7 | Stored-SDF source → shell compact buffers | Shell data and lookup are emitted as the active shader providers (`VIXEN/application/main/source/graph/BuildRenderGraph.cpp:8059-8068`), while `CreateOctreeBuffers` still retains/copies the source pair except in wholesale shell-admission mode (`VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:1060-1090`). | boot/change | L duplicate class | **ELIMINATE** — when shell is the guaranteed active Stored-SDF binding, do not allocate/copy the unreachable source pair. T-RULING-1 is required because binary/procedural placeholders, non-shell modes, and future source-side edits still use the pair. |
| P4-H8 | depth image → HiZ tile image | HiZ reads the previous-frame R32F depth image and writes the maximum distance of each clipped 16×16 block to one R32F tile (`VIXEN/shaders/HiZDownsample.comp:1-50`). `DepthTargetNode` ping-pongs two persistent images and selects read/write slots by frame parity (`VIXEN/libraries/RenderGraph/src/Nodes/DepthTargetNode.cpp:45-104`; format/contract is `VIXEN/libraries/RenderGraph/include/Data/Nodes/DepthTargetNodeConfig.h:17-37`). | every frame with B1 | source `4 B × W × H`; tile `4 B × ceil(W/16) × ceil(H/16)` | **KEEP** — this is a GPU-local reduction, not a CPU data hop; ping-pong/history prevents read-after-write hazards. |
| P4-H9 | HiZ tiles + instance/config buffers → high skip-mask words | The cull tests each Stored instance against projected tile maxima and ORs camera visibility bits into the high mask region (`VIXEN/shaders/InstanceOcclusionCull.comp:89-169`). RenderGraph wires the tile RAW dependency and mask write dependency explicitly (`VIXEN/application/main/source/graph/BuildRenderGraph.cpp:7595-7609`). | every frame with B1 | XS; up to 6 × 32-bit words for the 192-instance cap | **KEEP** — GPU producer/consumer locality and the RAW ordering are the invariant; there is no CPU readback in this chain. |
| P4-H10 | shell dirty SDF edits → compact shell slot | A dirty source brick is revalidated in place, then the write slot is uploaded while the current read slot remains bound (`VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:537-565`; edit entry point is `VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:319-325`). | per value edit; only dirty bricks | `dirty brick count × brick stride`; M typical | **BATCH** — coalesce dirty-brick ranges into one slot upload plan; retain full rederive for membership changes because `sourceToShellSlot` and proxy membership can change. |
| P4-H11 | per-frame host-visible mask/table writes → GPU reads | `StorageBufferNode::MapForReadback` performs `vkMapMemory` for the full buffer and `UnmapReadback` immediately unmaps (`VIXEN/libraries/RenderGraph/src/Nodes/StorageBufferNode.cpp:134-146`); the pre-tick writers are P3-H10–P3-H12. | every frame when bucketed dispatch is enabled | S–M | **DIRECT** — use frame-indexed persistent mappings with fence ownership; this is the latency companion to the bandwidth rows. |
| P4-H12 | staging pool request → pooled mapped staging allocation | The staging pool clamps requested sizes, reserves quota, reuses a size bucket when possible, and otherwise allocates a persistently mapped buffer (`VIXEN/libraries/ResourceManagement/src/Memory/StagingBufferPool.cpp:69-100`). | each queued upload; allocation only on pool miss | 64 KiB–64 MiB bucketed allocations | **KEEP** — quota/backpressure and lifetime-safe recycling are required; improve only through the existing `StagingBufferPool`/`BatchedUploader` pipeline. |

## Ranked elimination/fusion backlog

Rank is qualitative bandwidth × frequency, with high-frequency rows first and one-time L rows
kept below them.

| Rank | Finding | Action and owner seam |
|---:|---|---|
| 1 | P3-H8: whole instance list copy + memcpy-only packing every frame | `DIRECT` from `BodyInstanceGpu` to the existing persistent ring; change `BodyOctreeSceneNode::ExecuteImpl` and retain ring capacity/fence invariants. |
| 2 | P3-H10/P3-H11/P3-H12/P4-H11: per-frame map/fill/unmap of masks and recipe tables | **T-078 COMPLETE:** `DIRECT` through existing frame-indexed storage buffers; cache the canonical payload by registry/instance generation and retain per-slot dirty ranges until consumed. |
| 3 | P4-H3/P4-H4: brick upload and resident-config visibility use separate forced flush/phase transitions | `BATCH` in `BatchedUploader`/`BodyOctreeSceneNode`, preserving brick-before-config visibility. This is a latency win as much as a submission-count win. |
| 4 | P2-H4: editor render-preview VRC1 serialize → parse → copy | `ELIMINATE` the preview-only blob round-trip; retain VRC1 for save/export and external interchange. |
| 5 | P1-H4/P1-H5/P3-H6: active SIMD samples → Gaia → serialize back to GPU-oriented SoA | `FUSE` inside the existing `IMaterializationBackend`/`CpuRecipeMaterializer` pipeline. The output remains `SerializedOctree`; no parallel codegen path. |
| 6 | P1-H13/P1-H14: proxy AABB flattening and duplicated upload preparation | `DIRECT`/`BATCH`: cache the existing `ShellProxyAabb` bytes and reuse the payload for both destination slots. |
| 7 | P4-H7/P1-H8: full source SDF/lookup allocation beside shell data | `ELIMINATE` only after T-RULING-1 selects shell-only binding/lifetime for Stored-SDF. |
| 8 | P1-H10/P4-H10: source full-pool scan and full-stride copy on shell derivation | `KEEP` the lossless shell transform; add dirty brick range coalescing and direct page output in the existing backend before considering any algorithm change. |

## Latency tax

These are not all bandwidth duplications. They are the stalls, synchronization boundaries, map
churn, or submission granularity that make a byte movement more expensive than its byte count.

| ID | Latency site | Evidence | Impact / ruling |
|---|---|---|---|
| L1 | Brick residency is intentionally asynchronous but spans two visible phases | `UploadBrickPool` flushes and stores a handle (`VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:1431-1438`); completion first waits for bricks, then queues config, then waits for config (`VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:1521-1572`). | **BATCH** the ordered transfers into one submission/timeline point where legal; `brickResident` must remain false until brick bytes are visible. |
| L2 | Uploader can fall back to queue idle when command buffers are exhausted | `SubmitBatch` retries, then calls `vkQueueWaitIdle` if no command buffer is available (`VIXEN/libraries/ResourceManagement/src/Memory/BatchedUploader.cpp:410-429`). | **KEEP** as a bounded safety fallback, but track it as a latency counter; reduce incidence by preserving the existing batch limits and avoiding forced one-upload submissions. |
| L3 | Shell slot writes map/unmap each update | Reused shell buffers call `vkMapMemory`, memcpy, and `vkUnmapMemory` (`VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:1319-1329`). | **DIRECT** to a persistent mapped slot allocation; fence/slot ownership remains the invariant. |
| L4 | Recipe bucket tables map/unmap on every pre-tick | T-078 uses `StorageBufferNode::MapCurrentForWrite` only for non-empty retained dirty ranges; unchanged frame slots take no mapping call (`VIXEN/application/main/source/VulkanGraphApplication.cpp:876-903`; persistent mapping implementation is `VIXEN/libraries/RenderGraph/src/Nodes/StorageBufferNode.cpp:197-200`). | generation change or frame-slot catch-up | dirty byte ranges | **DIRECT** — persistent frame slots retain fence ownership and eliminate the steady-state map/unmap churn. |
| L5 | Per-frame body list is copied before the persistent ring write | `toPack = instances_` and `PackInstances` add two CPU copies/allocations before the ring memcpy (`VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:573-590`). | **DIRECT**; this removes both transient allocations and one full CPU read/write pass per frame. |
| L6 | Instance ring growth stalls the whole device | Capacity overflow calls `vkDeviceWaitIdle`, destroys the ring, and recreates all ring buffers (`VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:1206-1235`). | **KEEP** — safe resize needs no in-flight references; it is rare and explicitly not the per-frame path. Pre-size from the source instance-page maximum if the owner can state one. |
| L7 | B1 depth clear waits for queue idle during image creation | Both depth images are cleared/transitioned in one one-shot submit and `DepthTargetNode` waits for queue idle (`VIXEN/libraries/RenderGraph/src/Nodes/DepthTargetNode.cpp:210-229`). | **KEEP** as boot/resize initialization; it is not per frame and establishes the miss-sentinel/layout invariant. |
| L8 | B1 depth/HiZ/cull history is one frame delayed | Depth slots are selected by `frame & 1`, with the other slot read, and the cull receives previous-frame camera constants (`VIXEN/libraries/RenderGraph/src/Nodes/DepthTargetNode.cpp:92-104`; `VIXEN/application/main/source/VulkanGraphApplication.cpp:690-713`). | **KEEP** — removing the delay would require a same-frame dependency/barrier and could make cull read depth while march writes it. |
| L9 | Explicit diagnostic readbacks can add `vkDeviceWaitIdle`/map stalls | The application contains gated diagnostic readback blocks that map storage/readback buffers; for example the hit-accum path reads mapped records around `VIXEN/application/main/source/VulkanGraphApplication.cpp:1345-1370` and has additional gated readback sites at `VIXEN/application/main/source/VulkanGraphApplication.cpp:1774-1822`. | **ELIMINATE** from the production frame path — keep diagnostics opt-in and off the hot path; no render data should wait for these readbacks. |
| L10 | Uploader staging copy is persistent-mapped but still a CPU memcpy | `BatchedUploader::Upload` explicitly copies source data into staging (`VIXEN/libraries/ResourceManagement/src/Memory/BatchedUploader.cpp:85-100`). | **DIRECT** only after a device-supported upload-heap/write-through path is proven in the existing ResourceManagement seam; preserve the public uploader contract and fence ownership while removing this CPU copy. |

## T-RULING list

Questions for an owner decision; these are not new schema or attribute proposals.

1. **T-RULING-1 — Is Stored-SDF shell-only binding authoritative for all production render paths?** If yes, remove the full source `channelPool`/`brickGridLookup` GPU allocation/copy when a compact shell exists, and make shell/source edit tooling retain only whatever CPU source is required for rederive. If no, identify the non-shell consumer and keep P4-H7.
2. **T-RULING-2 — What is the authoritative external producer for body instances?** The engine sees `SetBodyInstances(std::vector<BodyInstanceGpu>)`, not a Gaia/read-model producer. Decide whether the external simulation should publish the existing 64-byte `BodyInstanceGpu` page directly, or whether the explicit setter remains the ownership boundary.
3. **T-RULING-3 — May recipe-bucketing tables be cache-invalidated by registry/instance generation?** If yes, make P3-H10–P3-H12 dirty-range writes rather than per-frame full-table rewrites. If no, name the consumer that requires a complete rewrite every frame.
4. **T-RULING-4 — Can brick data and the resident-bit config update share one ordered uploader submission?** The required invariant is brick bytes visible before `brickResident=1`; confirm whether the current `BatchedUploader` timeline/barrier API can express that in one submission.
5. **T-RULING-5 — Is VRC1 required for every editor preview update, or only persistence/interchange?** If preview-only, accept P2-H4 elimination. If every preview must preserve exact container validation semantics, retain the blob parse and measure its change-frequency cost.
6. **T-RULING-6 — Is the external simulation view contract in scope for this engine lane?** The current checkout cannot cite its Gaia density-cell/map-claim/deep-field producer calls. If it is in scope, provide the producer-to-engine boundary and generation/epoch contract so a follow-up audit can sentence that missing hop without guessing.

## Coverage and verification notes

- Covered the recipe/SDF, VXD, ECS/view/read-model, and streaming/HiZ routes, including
  `UploadShellSlot`, shell proxy AABBs, brick pool, `proxyAabbBuffer_`, instance ring,
  `BodyInstanceGpu`, skip masks, tier refs, camera constants, mip pool, staging, and the
  B1 depth/HiZ chain.
- The external Undertow simulation host is not present under `VIXEN/`; the ECS/view section
  explicitly marks that boundary as a T-ruling instead of fabricating file/line evidence.
- No code or generated artifact was changed by this audit. Any implementation of the backlog
  must extend the existing kernel compiler/materializer and ResourceManagement upload seams.
