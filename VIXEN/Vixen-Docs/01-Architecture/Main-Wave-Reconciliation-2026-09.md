# Main–Wave Reconciliation — 2026-09

Status: **blocked; merge aborted; no engine changes delivered**.

## Scope and measured inputs

- Pinned main base: `7c744ead4f08be976e5ac9a7c6f981051204b099`
- Fixed incoming tip: `6c1623910ec2db9d0b230508a469d75b2f638040`
- Merge command: `git merge --no-ff --no-commit 6c162391`
- Observed result: the expected 11 conflicts, followed by an abort back to the clean pinned main base.
- History inspection: `git log --left-right --oneline main...6c162391 -- <file>` was run for every conflict before resolution was attempted.

## Blocking design contradiction

The add/add `test_footprint_regime.cpp` conflict exposed two different public policies with the same C++ identity:

| Line | Header | Public type | Meaning |
|---|---|---|---|
| Main / deep-field | `libraries/SVO/include/FootprintRegime.h` | `Vixen::SVO::FootprintRegime` | `Surface=1`, `MipHit=2`, `Cosmic=3`; classifies cell-sampling/residency from world distance, cell size, ray footprint coefficients, and `cosmicK` |
| Wave / LOD A+B | `libraries/SVO/include/Recipe/FootprintRegime.h` | `Vixen::SVO::FootprintRegime` | `SurfaceDetail=0`, `Orbital=1`, `System=2`, `DeepField=3`; classifies body-scale render LOD from camera distance and body radius |

The functions share `ClassifyFootprintRegime` but overload by arity. The enums do not: they are two incompatible definitions of the same fully qualified type, with different members and numeric meanings. Including both headers in one translation unit is a redefinition error; compiling them in separate translation units of `SVO` creates an ODR violation. Selecting either definition would drop one shipped policy and its consumers.

An architecture ruling is required before retrying the merge: assign distinct domain-owned names/namespaces to the body-scale LOD policy and the cell-sampling/residency policy, then update their C++ tests/consumers and GLSL twins together. No rename was selected during reconciliation because the brief requires a stop rather than silently choosing between incompatible subsystem architectures.

## Conflict disposition

These are the measured dispositions and required resolutions for the next attempt. No conflict was staged because the design blocker caused the merge to be aborted.

| File | Class | Required resolution | Why |
|---|---|---|---|
| `application/main/include/Generated/UndertowHudInspect.blob.g.h` | Generated; modify/delete | Delete if the catalogue-derived view retirement remains authoritative; verify by running the merged View codegen gates | The incoming line retires the dead `[View]` root and generated read-model family; main only changed the generated version hash after later schema evolution. A retired source must not leave a hand-preserved generated artifact. |
| `application/main/source/graph/BuildRenderGraph.cpp` | Hand code | Retain main's complete wavefront/HDR/gather/apply include set and adopt the incoming `.g.h` generated-header naming | Main contains many later rendering stages; incoming contributes the generated-file naming convention. Keeping only either hunk loses one side. |
| `codegen/CMakeLists.txt` | Hand code / generator wiring | Retain main's catalogue-derived noun enum, retired legacy readmodel/umbrella checks, and callable shim/output split; keep the incoming `.g` naming changes that merged outside the hunks | The incoming conflict hunks predate main's wave-29/wave-39 removal of stale checks and would reintroduce known-red generator edges. |
| `generated/sdi/merged/ProbeGather-SDI.h` | Generated; rename/rename | Regenerate as `ProbeGather-SDI.g.h` from the merged shader manifest | Main renamed the retired ProbeUpdate program to ProbeGather; incoming renamed generated C++ artifacts to `.g.h`. Both transformations must compose. |
| `generated/sdi/merged/ProbeUpdate-SDI.g.h` | Generated; rename/rename | Remove after merged SDI regeneration | Its contents identify `Program: ProbeGather`; the path is the incoming side's rename of the obsolete pre-split filename, not the correct merged identity. |
| `generated/sdi/merged/ProbeUpdate-SDI.h` | Generated; deleted/deleted rename source | Remove after merged SDI regeneration | ProbeUpdate was retired by W1a/W1b and replaced by gather/wave/apply stages. |
| `libraries/AppFlow/include/generated/ViewNounId.g.h` | Generated | Regenerate from the merged ordered catalogue with `view_noun_enum_regen` | Main's catalogue-derived nouns and incoming dead-root retirement cannot be reconciled by editing enum ordinals. |
| `libraries/GaiaArchetypes/src/VoxelVolumeArchetype.cpp` | Hand code; comment-only hunk | Keep main's proxy-lifetime explanation and the shared by-value `auto` binding | The executable code is identical. Main is based on the newer Gaia proxy pin; the incoming comment incorrectly describes `set<T>()` as a direct reference while still binding it by value. |
| `libraries/GaiaArchetypes/tests/test_relationship_observer.cpp` | Hand code; comment-only hunk | Keep main's proxy-lifetime explanation and identical by-value mutation code | Both sides implement the same fix; main documents why the named by-value proxy is required under the merged Gaia pin. |
| `libraries/SVO/tests/test_footprint_regime.cpp` | Hand code; add/add; **design blocker** | Do not choose one. Rename/domain-separate the two public policies first, then retain both non-duplicative test sets | The tests prove two distinct classifiers, not alternate coverage of one classifier. Unioning them under the current public type causes a redefinition/ODR defect. |
| `shaders/SceneBindings.glsl` | Hand code | Retain main's later coarse-grid/deep-field implementation and incoming shader include only after the FootprintRegime domain names are separated | The textual hunk is main-only later rendering work and must survive. The auto-merged incoming body-scale classifier currently shares the ambiguous public concept name and must follow the architecture ruling. |

## Generated artifacts and generators

No regeneration was run because the merge was aborted at the architecture stop condition.

| Artifact family | Authoritative generator / gate | Required next-attempt action |
|---|---|---|
| Undertow View blobs, including the conflicted `UndertowHudInspect.blob.g.h` | Yeroket `CodegenTool~`, `--view-blob`; VIXEN `codegen/CMakeLists.txt` View check/regen targets | Confirm the retired root is absent from the merged catalogue and that no live target emits the artifact; run the merged View/codegen checks. |
| Merged SDI headers | `sdi_tool merge-variants shaders/sdi-variants.json`; checked by CTest `sdi_merged_drift_check` | Run without `--check` to regenerate `ProbeGather-SDI.g.h`, then run the drift check. Do not hand-merge any SDI header. |
| `ViewNounId.g.h` | CMake target `view_noun_enum_regen`, invoking Yeroket `--view-noun-enum` with the native schemas plus ordered schema catalogue | Regenerate on the merged tree, then run `view_noun_enum_check`. |

## Verification

The merged-tree build and tests were intentionally not started. The brief requires an immediate stop on a genuine design contradiction, and there is no valid merged source tree to gate until the public FootprintRegime ownership is decided.

| Required suite | Result |
|---|---|
| Full worktree-local configure/build | Not run — merge aborted before resolution |
| Gaia core (expected 26) | Not run |
| Gaia coverage (expected 34) | Not run |
| Replacement queue (expected 9) | Not run |
| SVO injection/materialization (expected 5) | Not run |
| SoA SDF serialization (expected 13) | Not run |
| SoA mip serialization (expected 6) | Not run |
| Shell/GPU serialization (expected 9) | Not run |
| `test_footprint_regime` | Not run — source-level architecture blocker |
| `test_relationship_observer` | Not run |
| GaiaArchetypes affected suites | Not run |

No pass counts are claimed.

## Anything dropped and why

Nothing was selectively dropped. The entire attempted merge was aborted, as required, so the branch remains at the pinned main base plus this blocker report. The incoming rendering, LOD, FootprintRegime, and voxel-materialization commits are **not** present on `main`.

## Not delivered vs brief

- The wave/voxmut tip `6c162391` was not merged into engine `main`.
- No conflict resolution was staged.
- Generated artifacts were not regenerated because the merged source-of-truth remains architecturally invalid.
- No merged-tree configure, full build, or test suite was run; therefore no exact passing suite counts are available.
- No merge commit was created and nothing was pushed.
