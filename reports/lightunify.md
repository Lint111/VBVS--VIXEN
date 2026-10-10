# lightunify — shadow and night-light trace

## Recorded source and required verification scope

Started on `1252e3705a8247f5b0b1478f0164da55c7cd44ba`, branch `lane-lightunify`, with a clean worktree. Fetching and merging `origin/wave/authoring-convergence` returned `Already up to date` before any product edit. Only this VIXEN worktree is writable for this dispatch. The generic paired-lane instructions do not assign a second writable worktree in the dispatch facts.

CodeGraph was queried first with `bash tools/codegraph-vixen.sh 'lookdev shadow lighting'` and `codegraph explore --path . 'lookdev shadow lighting' --max-files 5`. Both reported no index. No index was built; source tracing uses `rg`.

The target is the common body-instance traversal and lighting pipeline, including its demo-host shader construction, live recipe replacement, declared-light upload, and look-dev capture consumer. Required generation checks are all 22 VIXEN codegen checks, plus shader-interface generation if its contract changes. Required build/tests are a fresh native configure/build, RenderGraph and SVO suites, existing capture/fill/lighting-ratio and R6 witnesses, R434 visibility witnesses, and the requested per-family shadow, receiver-colour, night-falloff and R477 timing properties. Before/after evidence requires four separately queued lighting-state jobs, each containing four angles, and a debug shadow-mask sheet. This is wider than a fixture-only lighting adjustment.

The audit itself is independently verifiable from source and requires no product build. The queued baseline configure was cancelled while still waiting for admission, after 8m55s: its only child was the queue's `sleep 0.5`, no CMake payload had started, and no build cache or target verdict existed. This is an inconclusive, deliberately cancelled setup attempt, not a red baseline. No product implementation followed it.

Exact attempted setup command:

```bash
env VIXEN_FETCHCONTENT_CACHE=$PWD/.tmp/fetch \
  bash /home/liory/.local/bin/with-test-lock.sh \
  --agent lightunify --resource build --label lightunify:baseline-configure -- \
  nice -n 10 cmake --preset vixen-wsl -S VIXEN
```

## Diagnosis before product edits

### What is actually in the look-dev scene

The fixture is not a stored SVO building surrounded by a different character renderer. `VIXEN/application/main/include/graph/LookdevSceneDefinition.h:22` defines box and ellipsoid primitives, `:24` defines the material families, `:250` groups them into per-material registered recipes, and `:298` makes a body instance for each material. Every fixture instance has `providerKind = 1` and `recipeId >= 100` (`:321`). The soil plinth, water, rocks, foliage, crawler hull, props, and human silhouette are all procedural body instances. The figure appendages are included in the same recipe collection (`:265`); no sprite or skeletal renderer is used by this fixture.

`VIXEN/application/main/tests/test_lookdev_capture.cpp:77` registers those recipes before building the graph. `:108` uploads the declared lights; `:113` replaces the body-instance list. `VIXEN/application/main/source/VulkanGraphApplication.cpp:4190` forwards that replacement to the body-scene node. `VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:231` refreshes the material/transform streams and instance count, and increments the acceleration-structure instance epoch.

### One common traversal exists, but its compiled consumers disagree

Primary geometry: `VIXEN/application/main/source/graph/BuildRenderGraph.cpp:1892` calls `SpliceProceduralRecipesIntoSource` on the primary march shader and publishes the derived occupancy grid at `:1902`. `VIXEN/shaders/BodyInstanceRayMarch.comp:93` declares the occupancy buffer; `:120` includes the generated SDF kernels; `:121` is the recipe splice marker, before `SceneBindings.glsl`. This defines `VIXEN_UBER_RECIPE_SPLICED` and makes registered recipe fields available to `TraceWorld`.

Shared shadow algorithm: `VIXEN/shaders/TraceWorld.glsl:879` implements `TraceWorldShadowImpl`. Its loop reads the same body materials and affine transforms as the primary ray. It excludes dispatch ownership at `:907`, deliberately ignoring camera visibility. Legacy procedural IDs 0/1 use `traceProceduralBody` at `:954`; registered IDs use `traceUberRecipeBody` at `:966`, **only under `VIXEN_UBER_RECIPE_SPLICED`**. Stored providers descend the common octree at `:1000` onward. The stored path supports binary occupancy and stored-SDF any-hit traversal. Point lights call the same function with source-emitter exclusion (`:871`, `:1127`); directional sources call it without that exclusion (`:1123`).

Default analytic shadow consumer: `VIXEN/shaders/ShadowVisibilityWave.comp:57` includes `SceneBindings.glsl` directly, with no recipe splice marker, SDF-kernel include or occupancy declaration. `VIXEN/application/main/source/graph/BuildRenderGraph.cpp:2236` creates the lighting shader source provider by reading the raw file; `:2289` returns that source without recipe emission. `:2436` uses it for the analytic shadow wave. Consequently its registered-recipe branch compiles out. This is a definite source-level gap, not a reason to add a second shadow algorithm or per-family shadow maps.

The wave reads each hit's position/normal and the declared light set (`VIXEN/shaders/ShadowVisibilityWave.comp:254`), traces the same per-light direction/range (`:271`), and writes visibility into HitRecord bits 0–3 (`:289`). `VIXEN/application/main/source/graph/BuildRenderGraph.cpp:9594` wires the live body count into that wave. Camera visibility cannot legally become a shadow mask. A source-level repair must make the registered fields available to this consumer, preserve that common traversal and mask, and keep shader reflection and graph wiring consistent.

The same raw-source provider is used by `ProbeGather` and `ShadowRayTrace` (`VIXEN/application/main/source/graph/BuildRenderGraph.cpp:2426`) and by the reservoir shadow variant. Those consumers must also see registered fields when enabled. The existing per-cell shade consumer already uses the primary recipe splice with a null occupancy-output pointer (`:2506`), illustrating an existing reuse route. Its large footprint bias (`VIXEN/shaders/HitAccumCellShade.comp:161`) is different from the pixel wave's bias; it is an optional approximation, not evidence that the fixture has one shadow result for every family.

Acceleration structures: `VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp:2038` explicitly excludes procedural providers from the stored-SDF TLAS. Procedural recipes must therefore remain in the common software instance loop even when RayQueryLighting is selected. They cannot acquire shadows merely by enabling ray query. Stored bodies use the existing BLAS/TLAS and stored any-hit traversal; registered procedural bodies need their generated field functions in the consuming shader.

| Element family | Primary geometry | Common shadow eligibility on recorded source | Receiving path |
|---|---|---|---|
| Fixture hull/building and metal props | Registered box/ellipsoid SDF recipes | Instance list is present, but registered branch is compiled out of the default wave | HitRecord → common cel shader |
| Trees and rocks | Registered SDF unions / smooth unions | Same compiled-consumer gap | Same HitRecord and shader |
| Character silhouette | Registered ellipsoid recipe, including appendages | Same compiled-consumer gap | Same HitRecord and shader |
| Plinth and water | Registered rounded-box / ellipsoid recipes | Same compiled-consumer gap | Same HitRecord and shader |
| Actual stored SVO/voxel bodies outside this fixture | Binary or stored-SDF octree | Existing common stored any-hit branch; optional stored-SDF TLAS | Same HitRecord and shader |
| Separate sprites | Not present in the reviewed fixture | No sprite geometry is authored by this capture; no casting claim made | Not exercised |

The owner's editormaterials14 sheet visibly shows a hull-shaped dark patch and missing tree/figure shadows. That sheet is evidence of the reported symptom, but its producing branch is not this recorded source SHA. The source trace above cannot certify that patch as the current wave's physical hull shadow. Fresh captures and mask readback must resolve that distinction; no guessed causation is presented as a measurement.

### Receiver colour and night contributions

`VIXEN/shaders/SpatialReuseShade.comp:594` hue-shifts the receiving material colour and multiplies it by the authored ambient intensity. Its light loop skips invisible lights (`:613`) and adds the receiver's shifted colour times the light spectrum, cel band, and range attenuation (`:629`). It does **not** assign a constant red shadow tint. The fixed -18-degree ambient hue shift can nevertheless push the brown soil toward dark red and remove the lit source's warm contribution completely; validating receiver hue requires comparing receiver pixels, not declaring a fixed tint from the screenshot alone.

`VIXEN/shaders/LightEvaluation.glsl:19` implements point-source falloff `(1 - distance/range)^2`, with zero contribution outside the range; directional sources have no distance falloff. The cel spill term derives from source luminosity, per-light purpose and range attenuation (`VIXEN/shaders/SpatialReuseShade.comp:615`). It changes the band-driving incidence (`:623`), then applies attenuation to the resulting energy (`:629`). Bright spectra are normalized above peak radiance 1 (`:619`), while weak spectra retain their magnitude. Thus a claim that the 0.10 night key is normalized to daylight energy would be incorrect.

The night preset (`VIXEN/application/main/include/graph/LookdevSceneDefinition.h:78`) declares a cool directional key at 0.10 and ambient fill at 0.05. `:331` builds the shared light set: one key, one window-row point source (intensity 3.5, range 8), one roof worklight (5, range 8), and one side floodlight (8, range 8), with purpose scales 4 for the three point sources (`:381`). The twelve window boxes on four sides are represented by **one** point source at the front row's central origin (`:68`, `:375`), not twelve independent spatial sources. Its emitter-instance exclusion skips that entire per-material window recipe for the matching point source (`VIXEN/shaders/TraceWorld.glsl:871`). That grouping and an incomplete shadow wave can explain unintuitive cross-facade light propagation; a fresh per-source witness is still required to attribute the image.

Emissive appearance uses the existing `recipeParams[3]` channel (`VIXEN/application/main/include/graph/LookdevSceneDefinition.h:323`), carried by the primary hit and added as material-coloured emission at `VIXEN/shaders/SpatialReuseShade.comp:693`. Non-emitter fixture materials have zero emission. No facade-specific brightening term exists in this fixture or the default common cel function. Without environment overrides, the host disables the reservoir and DDGI dispatches via `ResolveReservoirEnabled` and `ResolveProbeGridEnabled` (`VIXEN/application/main/source/graph/BuildRenderGraph.cpp:10025`); the look-dev test enables neither. No light-tree population is authored by the look-dev test. The photon cache is opt-in (`VIXEN/libraries/RenderGraph/src/Nodes/PhotonCellParamsConfigNode.cpp:50`); per-cell resolve is opt-in (`VIXEN/application/main/source/graph/BuildRenderGraph.cpp:995`). Those paths require separate enabled-configuration verification; they cannot be silently credited as this fixture's night GI.

Exposure is uploaded per preset and checked in all mapped ring slots (`VIXEN/application/main/tests/test_lookdev_capture.cpp:147`). `VIXEN/shaders/ExposureTonemap.comp:34` multiplies scene radiance by the meter plus authored EV and applies ACES and sRGB conversion. Display luminance alone cannot establish unattributed energy without isolating the key, ambient, three point lights, emission and exposure.

### Required R477 dependency

The recorded wave has no gpuyield budget/submission slicing surface. `VIXEN/libraries/RenderGraph/src/Nodes/ComputeStageNode.cpp:376` derives full-frame group counts and `:442` sends them in one `vkCmdDispatch`. R477 in the read-only Undertow register explicitly assigns bounded submissions, pacing and a declared reference-GPU budget to gpuyield. No timing result or declared budget on this tree establishes the dispatch's required R477 property. This is an external landing dependency, not a license to invent another pacing configuration in the lighting lane.

## Visual references reviewed

Targets: T01 (shared sources and shadows), T13 (practical emitters light surrounding materials), T16 (indirect light where enabled), T21 (bi-modal cel groups).

- C16: four time-of-day states and reference-world values; night must read as local practical pools.
- D-r2-01, `iterations/lighting/r2-01-bimodal-midday.png`: coherent material/value groups across machinery and nature.
- D-r2-12, `iterations/lighting/r2-12-bimodal-night-local-pools.png`: small distributed lamps and gradual falloff.
- D-r3-01, `iterations/lighting/r3-01-bimodal-floodlight-nature.png`: a purposeful strong pool reaching rock, foliage and water.
- Owner symptom sheet: `/home/liory/projects/VBVS--VIXEN/.claude-worktrees/editormaterials/reports/editormaterials14-visual/after/contact-sheet.png`, opened directly.

The bible, lighting iteration index, nightservice report and these images were read/opened before any product change. No new visual result is claimed by this audit.

## LANDABLE NOW

**Audit only.** The lane commit contains this diagnosis, source-hash evidence and byte-preserved copies of the two owner-supplied contact sheets. It does not contain a renderer repair or claim a passing runtime baseline. The product source remains `1252e3705a8247f5b0b1478f0164da55c7cd44ba`; the tip containing the audit is reported by the final message. The second fetch and final merge check again returned that same upstream source and `Already up to date`.

The principal defect is a compiled-consumer mismatch: registered procedural bodies are visible to the primary ray, but their field functions are omitted from the default shadow wave and the optional probe traversal family. Stored voxels and legacy analytic spheres have shared-path branches. The common cel shader derives shadow colour from the receiver, and the night fixture already declares practical sources with range falloff; neither a constant shadow tint nor a facade-specific brightening term was found. The twelve-window/one-point-source grouping and the missing registered occluders need a per-source runtime witness before assigning the night symptom to a specific term.

Source evidence: [lightunify-source-evidence.json](lightunify-source-evidence.json) records SHA-256 hashes and splice-marker presence for the audited source. Source hashes and all file/line anchors were checked against the recorded tree; Markdown whitespace validation passes. No source changes were added, so the R412 added-code checklist is empty. No generated output or pin was edited. No branch was created/switched and nothing was pushed.

### Gate disposition

| Gate | Result |
|---|---|
| Diagnosis before semantic changes | COMPLETE as a source audit; runtime attribution remains unmeasured |
| Per-family shadows in four states × four angles | NOT RUN; the recorded default shader omits registered casters |
| Receiver-colour / lower-cel-band hue property | NOT RUN; source derives colour from the receiver, but the screenshot alone does not prove the required tolerance |
| Night radial falloff / explained luminance | NOT RUN; authored analytic falloff exists, no per-source radial-bin measurements |
| editormaterials fill and lighting-ratio properties | NOT RUN |
| R434 visibility | NOT RUN; preserving camera-independent shadow demand is required for the repair |
| R6 capture property | NOT RUN |
| 22 generation checks | NOT RUN; configure never acquired admission and no fresh target binaries exist |
| RenderGraph / SVO suites | NOT RUN; no stale binary results substituted |
| Generic 11/11 captures and no-op rebuild | NOT RUN |
| R477 gpuyield timing budget | UNAVAILABLE on this source tree; required dependency is not on the fetched wave |
| Report/source-hash evidence and supplied-sheet provenance | PASS |

There was no failed build or test to classify as pre-existing. No product gate is reported green on historical evidence. The SVO opcode-94 allowance was read but was not exercised by this audit.

### Reviewed sheets and one line per tile

- [Owner's before contact sheet](lightunify-visual/owner-before-contact-sheet.png)
- [Owner's after contact sheet](lightunify-visual/owner-after-contact-sheet.png)

These are exact copies of the brief's editormaterials14 sheets, opened before copying and checked by SHA-256 afterward. They are **supplied symptom evidence**, not lightunify before/after captures. No lightunify after image or debug shadow-mask sheet exists, and none is fabricated. The tile notes below refer to the supplied **after** sheet:

| Tile | Observation |
|---|---|
| Midday angle 0 | Machine, foliage and figure are visible; distinct foliage/figure cast footprints are not resolved on the plinth. |
| Midday angle 1 | Large hull-shaped dark-red patch on the right of the plinth; foliage has no separately resolved footprint. |
| Midday angle 2 | Large hull-shaped patch below the front/rear face; no separately resolved tree footprint at left. |
| Midday angle 3 | Bright plinth and visible figure at right; no separately resolved figure footprint. |
| Late-afternoon angle 0 | Warm frontal materials; tree and figure have lit/shaded sides but no clearly resolved receiving-ground footprints. |
| Late-afternoon angle 1 | Long dark-red patch extends to the right/front edge; tree footprint remains unresolved. |
| Late-afternoon angle 2 | Long machine-shaped patch extends to the left edge; other caster footprints remain unresolved. |
| Late-afternoon angle 3 | Warm, bright rear plate and figure; figure cast footprint remains unresolved. |
| Overcast angle 0 | Diffuse bright upper surfaces; individual small-caster footprints remain unresolved. |
| Overcast angle 1 | Small dark region under the hull; no resolved foliage footprint beside it. |
| Overcast angle 2 | Compact machine-shaped dark-red patch beneath the hull; foliage and rock receiving-ground footprints unresolved. |
| Overcast angle 3 | Broad diffuse plinth illumination; no separately resolved figure footprint. |
| Night-service angle 0 | Roof/cab lit while foliage and ground remain dark; visible sources do not by themselves establish the facade's energy attribution. |
| Night-service angle 1 | Warm cab highlights and brown plinth bands; a mask and per-light isolation are needed to relate those bands to the lamps. |
| Night-service angle 2 | Cab highlights, dark facade and ground bands; the grouped window emitter cannot be validated from this tile alone. |
| Night-service angle 3 | Bright roof/cab and dark ground; no radial falloff measurement is available. |

### Bible §7 review of supplied evidence

| Step | Result / observation |
|---|---|
| 1 Log frame | SUPPLIED: four lighting states × four angles; fresh lightunify resolution/regime readback not available |
| 2 Shared light set | FAIL in supplied evidence: small-caster footprints unresolved; source audit confirms a registered-caster omission |
| 3 Value range | UNMEASURED in this audit; no new percentile or exposure witness claimed |
| 4 Depth by haze | N/A to this isolated plinth fixture, which has no distant terrain bands |
| 5 Domain split | PARTIAL: value groups visible; machine ink and painterly nature quality are not established |
| 6 Voxel step | PASS visually on supplied thumbnails: smooth rock/foliage silhouettes, no large foliage cubes |
| 7 Intrusion read | N/A to a material fixture without an authored faction biome |
| 8 Machine lights | FAIL / unproven: small sources exist, but ground pools and source attribution are not established |
| 9 Weight | PARTIAL: broad machine and thick tracks, simplified maintenance silhouette |
| 10 Silhouette | PASS visually: one central machine cluster reads at all four angles |
| 11 Materials | UNMEASURED in greyscale; separable colours alone are insufficient |
| 12 Scale | PASS visually: maintenance figure present |
| 13 Clean frame | PASS visually: no in-world HUD panels or neon; sheet labels are outside the captured scenes |
| 14 Hull colour | PARTIAL: major/minor planes visible; no 70/30 area measurement performed |
| 15 Edges | PARTIAL: rounded nature and hard machine silhouette, no fresh distance/detail witness |
| 16 Extraction read | N/A: no excavation or mined comparison is authored |

### STOP / owner action

**REFER-TO-ORCHESTRATOR: required R477 provider absent.** The brief requires the bounded budget established by gpuyield, while both fetched source checks still point to `1252e370`. That tree has a single full-frame `vkCmdDispatch` and no declared gpuyield budget or slicing witness. The lane asked which landed wave commit and budget to use; no answer was available when this audit was finalized.

Recovery assessment for this specific missing dependency:

- **A — invocation/provisioning:** verified the global queue exists and the documented native Linux preset is present; the configure was queued correctly. Re-fetched the designated VIXEN wave twice and merged it into this lane; no newer source was available. Changing the build entry point cannot supply an absent runtime budget/provider.
- **B — regeneration:** not applicable to the missing R477 capability: it is native dispatch/submission behavior, not stale generated configuration or pin metadata. No generator on this tree can derive a gpuyield budget from an absent declaration, and inventing one is outside the lane's design limits.
- **C — narrower scope:** source audit is valid independently and is delivered here. It cannot satisfy the explicit timing property; excluding R477 or replacing its missing declared budget with a guessed threshold would narrow away a required gate. There is no published budget/result for this recorded source cited by the brief.

Recommended owner action: land gpuyield onto `origin/wave/authoring-convergence` with its declared reference-GPU budget and supported timing-witness command, then resume lightunify from this audit. This is not `STOPPED: baseline unobtainable` on a build error, and queue waiting was not used as the stopping reason. No lighting implementation was abandoned after edits; no product source was changed before the required verification dependency could be identified.

The remaining implementation is to reuse the existing recipe splice for every shared traversal consumer, keep reflected interfaces and live recipe invalidation in sync, capture a real visibility mask, then isolate the four declared night sources plus ambient/emission under the same exposure before adjusting any unattributed term. The windows' one-source representation must be evaluated against the existing light-set capacity, rather than inventing a second lighting system. Fresh required gates and the requested lightunify before/after/mask sheets remain outstanding.

### KFR follow-up boundary

The read-only production host call site is `/home/liory/projects/KernelFederationRenderer/app/src/session_renderer.cpp:28`. Its current override calls `BuildUIGraph()` at `:29`, rather than composing a world-lighting graph. A future integration must compose the same library traversal/light set there when world rendering is wired. The audit does not change KFR or assert that its current UI-only graph exercises these shadows.

## SPT DISPOSITION

No SPT task IDs were assigned in this brief.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Publish a reusable VIXEN CodeGraph index for engine lanes
