# Main–Wave Reconciliation — 2026-09

Status: **gated and delivered in the merge commit containing this report**.

## Scope and authority

- Pinned engine-main code base: `7c744ead4f08be976e5ac9a7c6f981051204b099`
- Run-2 first parent: `a8c742503bff30252ae166ed84af3b1dcac4fb21`, which adds only the two
  run-1 reconciliation-report commits above that code base
- Fixed incoming parent: `6c1623910ec2db9d0b230508a469d75b2f638040`
- Merge command: `git merge --no-ff --no-commit 6c162391`
- The expected 11 conflicts were reproduced. Before resolution, each file was measured with
  `git log --left-right --oneline main...6c162391 -- <file>`.
- The run-1 stop was resolved by the controller's `0dv` ruling in the authoritative lane
  preamble at `514cffd70`: the cell-sampling policy and body-scale LOD policy are separate
  domains and must have separate names in C++ and GLSL, with no compatibility alias.
- Context used for the semantic merge included the architecture Quick Lookup and
  `Voxel-Mutation-Replacement-2026-09.md`.

## Ruled regime split

| Domain | C++ contract | GLSL contract | Meaning |
|---|---|---|---|
| Cell sampling / residency | `libraries/SVO/include/CellFootprintRegime.h`: `CellFootprintRegime`, `ClassifyCellFootprintRegime(...)`, `kBrickDivisor` and the cell thresholds | `classifyCellFootprintRegime(...)` in `SceneBindings.glsl`, consumed by `TraceWorld.glsl` and `RayQueryTraversal.glsl` | Chooses Surface, MipHit, or Cosmic from cell scale, ray footprint, and world distance |
| Body render LOD | `libraries/SVO/include/Recipe/BodyFootprintRegime.h`: `BodyFootprintRegime`, `ClassifyBodyFootprintRegime(cameraDistance, bodyRadius)`, and the distance-over-radius thresholds | `shaders/BodyFootprintRegime.glsl`: `classifyBodyFootprintRegime(...)` | Chooses SurfaceDetail, Orbital, System, or DeepField from body scale |

Both original `8.0` thresholds remain, each under its own domain-specific constant. They are
semantically unrelated and were not unified. Every C++ and GLSL consumer was updated. The two
proof sets were split into `test_cell_footprint_regime.cpp` and
`test_body_footprint_regime.cpp`.

The final whole-word retirement check was:

```bash
rg -n -w 'Footprint''Regime' . --glob '!build/**'
```

Result: **zero matches**. There is no alias or compatibility `using` declaration.

## Per-conflict disposition

| File | Class | Resolution | Why |
|---|---|---|---|
| `VIXEN/application/main/include/Generated/UndertowHudInspect.blob.g.h` | Generated; modify/delete | Deleted with the retired Undertow read-model family; the merged View/codegen ALL gates confirm that no live schema target emits it | Incoming retires the dead catalogue root. Main's hunk only changed a generated version hash, so retaining it would create an orphaned generated artifact. |
| `VIXEN/application/main/source/graph/BuildRenderGraph.cpp` | Hand code | Kept main's complete later HDR, gather/apply, shadow-wave, spatial-reuse, and hit-accumulation stages; adopted generated `.g.h` names only for artifacts the merged SDI generator actually emits under that suffix | This preserves main's 62-commit render evolution and incoming generated-file naming. `ExposureMeter` and `ExposureTonemap` remain on their generator-owned legacy `.h` paths. |
| `VIXEN/codegen/CMakeLists.txt` | Hand code / generator wiring | Kept main's catalogue-derived noun enum, callable generated-output/shim split, and retirement of stale readmodel/umbrella checks. Added schema-catalogue discovery for both normal superproject and nested engine-worktree layouts | Incoming conflict hunks predate main's wave-29/wave-39 generator cleanup. Reintroducing them would restore known-stale ALL edges. Worktree-local discovery is required to run the authoritative merged generator rather than hand-edit its output. |
| `VIXEN/generated/sdi/merged/ProbeGather-SDI.h` | Generated; rename/rename | Removed and regenerated as `ProbeGather-SDI.g.h` | Main supplied the ProbeUpdate-to-ProbeGather program split; incoming supplied the `.g.h` convention. The generator composes both. |
| `VIXEN/generated/sdi/merged/ProbeUpdate-SDI.g.h` | Generated; rename/rename | Removed; merged SDI regeneration emits no ProbeUpdate artifact | Its contents identified the program as ProbeGather, so this was an obsolete filename rather than a distinct program. |
| `VIXEN/generated/sdi/merged/ProbeUpdate-SDI.h` | Generated; deleted/deleted rename source | Removed; merged SDI regeneration and drift check confirm its retirement | ProbeUpdate was replaced by gather/wave/apply stages. |
| `VIXEN/libraries/AppFlow/include/generated/ViewNounId.g.h` | Generated | Regenerated with `view_noun_enum_regen`, then passed `view_noun_enum_check` | Enum order and membership come from the merged native schemas plus ordered catalogue; textual ordinal merging is invalid. |
| `VIXEN/libraries/GaiaArchetypes/src/VoxelVolumeArchetype.cpp` | Hand code; comment-only conflict | Kept main's accurate proxy-lifetime explanation and the identical named by-value mutation proxy | Both sides had the same executable fix. Main documents the newer Gaia proxy API correctly. |
| `VIXEN/libraries/GaiaArchetypes/tests/test_relationship_observer.cpp` | Hand code; comment-only conflict | Kept main's accurate proxy-lifetime explanation and identical named by-value mutation code | Both proofs survive; only the technically incorrect incoming comment was discarded. |
| `VIXEN/libraries/SVO/tests/test_footprint_regime.cpp` | Hand code; add/add | Split into domain-owned cell and body test files and CMake targets; kept every non-duplicate proof | The two sides test distinct classifiers. Unioning them under one public identity would recreate the run-1 ODR/redefinition defect. |
| `VIXEN/shaders/SceneBindings.glsl` | Hand code | Kept main's later stored-SDF/coarse-grid/deep-field traversal implementation and incoming body-regime include, with both GLSL classifiers renamed by domain | Both render features coexist; neither classifier nor main's later traversal work is lost. |

No unresolved index entries or conflict markers remain.

## Regenerated artifacts and authoritative generators

| Artifact(s) | Generator | Verification |
|---|---|---|
| `generated/sdi/merged/{BodyInstanceRayMarch,DirectLighting,SpatialReuseShade,ShadowVisibilityWave,HitAccumulate,HitAccumClear,SpatialReuseGather,ProbeGather,ProbeApply,RecipeInstanceBucketing,HiZDownsample,InstanceOcclusionCull,ShadowRayTrace,HitAccumCellShade,SkySphereAccumulate}-SDI.g.h` | `sdi_tool merge-variants shaders/sdi-variants.json` | A second run with `--check` reported all merged SDI headers up to date. The obsolete ProbeUpdate paths were not emitted. |
| `libraries/AppFlow/include/generated/ViewNounId.g.h` | CMake `view_noun_enum_regen`, invoking Yeroket `CodegenTool~ --view-noun-enum` over the merged native schemas and ordered Undertow catalogue | `view_noun_enum_check` passed. |
| `libraries/AppFlow/include/generated/AppFlowCallables.generated.g.hpp` | CMake `callables_regen`, invoking Yeroket `CodegenTool~ --callable-cpp` over the merged codegen root | `callables_check` passed. This additional stale output was found by the full ALL build and regenerated rather than patched. |
| Retired `UndertowHudInspect` generated blob/readmodel/typed artifacts | Yeroket View generators formerly rooted in the retired catalogue entry | No merged target emits them; the full ALL build passed every remaining View golden check. Deletion, not synthetic empty output, is the canonical generated result. |

The final full ALL build also passed the golden checks for config structs, View headers/RML/blob/C#,
AppFlow references, the callable header, and the noun enum.

## Verification

Configuration and all build/test commands used the shared box queue. The build directory was
`build/mainmerge`, inside this worktree. The final full `cmake --build build/mainmerge --parallel 1`
completed with **0 warnings and 0 errors** in its fresh output; main's test targets retained their
`-Werror` policy. A preceding parallel verification attempt exposed a race between concurrent
`dotnet run` checks sharing Yeroket's external `CodegenTool~` output (`RC=154`, transient missing
DLL). Serializing the ordinary full build removed that tool-output race without changing source or
using a custom runner.

### Required voxel-mutation set — 102/102

| Executable / suite | Passed | Result |
|---|---:|---|
| `test_gaia_voxel_world` | 26 | Green |
| `test_gaia_voxel_world_coverage` | 34 | Green |
| `test_voxel_injection_queue` | 9 | Green |
| `test_voxel_injection` | 5 | Green |
| `test_soa_sdf_serialize` | 13 | Green |
| `test_soa_mip_serialize` | 6 | Green |
| `test_shell_octree_gpu` | 9 | Green |
| **Total** | **102** | **Green** |

### Conflict-owner and renamed-consumer suites — 187/187

| Executable / suite | Passed | Result |
|---|---:|---|
| `test_cell_footprint_regime` | 14 | Green |
| `test_body_footprint_regime` | 7 | Green |
| `test_lod_ladder` | 8 | Green |
| `test_multi_body_renderer` | 5 | Green |
| `test_recipe_composition` | 4 | Green |
| `test_occlusion_gate` | 15 | Green |
| `test_wholesale_availability` | 6 | Green |
| `test_gaia_archetypes` | 128 | Green |
| **Total** | **187** | **Green** |

The GaiaArchetypes aggregate includes all **54** relationship-observer source proofs:
RelationshipType 7, RelationshipObserver 23, RelationshipTypeRegistry 10,
RelationshipObserverEdgeCase 10, RelationshipObserverThread 1, and
RelationshipObserverIntegration 3.

Across the explicitly gated executables, **289/289 tests passed**.

## Anything dropped and why

- The retired Undertow catalogue roots and their blob/readmodel/typed outputs were dropped because
  the merged source catalogue no longer contains them; preserving generated remnants would be a
  defect.
- ProbeUpdate generated paths were dropped because main replaced that program with the
  gather/wave/apply split; merged SDI regeneration emits the replacement programs.
- The old unsuffixed regime header, shader, test path, public type, functions, and constants were
  dropped under ruling `0dv`. Their behavior and proofs survive under the two domain-owned APIs.
- Incoming codegen conflict text that would restore retired readmodel/umbrella checks was dropped
  in favor of main's later authoritative generator topology.
- Main's blanket old SDI filename suffix was dropped only where the merged generator emits `.g.h`;
  the two legacy exposure headers retain their actual generated paths.

No rendering, LOD, deep-field, relationship-observer, or voxel-materialization behavior was
dropped.

## Not delivered vs brief

None. The fixed wave/voxmut tip is reconciled with engine main, generated artifacts are
generator-owned and drift-clean, the full build and required tests are green, and this report is
included in the merge. No push was performed, as prohibited. No golden or fixture recapture was
needed.
