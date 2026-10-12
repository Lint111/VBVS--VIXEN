# Lane vixenmodules2 — structured report

Base: `d15a7141e983abc379b3cab1b16508c15ed20b7d` (`lane-vixenmodules2`). The required merge of `origin/wave/authoring-convergence` was attempted after fetch; it was already up to date at the same SHA.

Implementation checkpoint: 2026-10-12 14:36 UTC. I stopped before semantic source edits because the registered-group and SceneLook prerequisites below are absent from this merge base. No product source files were changed.

## LANDABLE NOW

The report and a baseline visual artifact are landable. The baseline capture records the existing default look; it is not evidence for any new profile or photon-cell output.

### Baseline and focused checks

- `cmake --preset vixen-wsl` completed with exit 0. Configure printed a `glslang directory not found` diagnostic for the SDK include path, but the subsequent fresh targets built successfully.
- The queued build of `VIXEN`, `test_rendergraph_subgraph`, `test_photon_cell_nodes`, and `test_cel_shading_graph` completed successfully (560 build steps). The look-dev capture executable was built separately after the first capture attempt reported it missing.
- Focused CPU CTest selection `^(SubGraphNodesTest|PhotonCellNodes)\.` passed 7/7.
- All 53 CMake targets ending in `_check` passed through the light queue. The generated checks ran 29 shared actions; `no_new_mutex_check` passed with existing unclassified-inventory warnings in `KernelDispatch/TaskExecutor.h` and `.cpp`.
- CodeGraph exploration was attempted first. The CLI reported that this worktree has no `.codegraph` index and that indexing is the user's decision, so I continued with `rg` rather than creating an index.
- The full CTest suite was not run: no product source changed, and the user-requested minimum-capture constraint made the existing 64-frame multi-angle capture suite a poor fit for this audit checkpoint.

### Default capture

The queued GPU capture `Application.LookdevCapture.RendersFourStatesAtFourAnglesDeterministically`, filtered to `midday` and one frame per scenario, passed in 49.53 seconds. It produced four 500×500 images, one per orbit angle. The sheet was opened and reviewed: the current flat cel image has a violet sky, warm ochre ground, pale central structure, and hard dark shadows; the four views show the scene rotating around the same lighting setup.

- Contact sheet: [base-midday-contact-sheet.png](vixenmodules2-visual/base-midday-contact-sheet.png)
- Source captures: [angle 0](vixenmodules2-visual/base-midday-angle-0.png), [angle 1](vixenmodules2-visual/base-midday-angle-1.png), [angle 2](vixenmodules2-visual/base-midday-angle-2.png), [angle 3](vixenmodules2-visual/base-midday-angle-3.png)
- Minimal capture settings used: `VIXEN_LOOKDEV_CAPTURE_STATE=midday`, `VIXEN_LOOKDEV_CAPTURE_FRAMES=1`.

The first 64-frame attempt was stopped after about 263 seconds with two of four angles complete. It was deliberately cancelled to respect capture cost; the one-frame rerun above completed. The initial GPU attempt also found the capture executable had not been included in the first target build; building that target through the queue recovered it.

### Requested profile matrix and timing

| Cel look | Indirect provider | Capture status |
|---|---|---|
| Flat (`SceneLookDeclaration`) | None | Baseline midday only; no new profile capture |
| Flat (`SceneLookDeclaration`) | Photon cells | Not captured |
| Bi-modal (`SceneLookBimodal`) | None | Not captured |
| Bi-modal (`SceneLookBimodal`) | Photon cells | Not captured |

| Measurement | Result |
|---|---|
| Photon resolve, native 8060S; R494 lighting/GI 2.0 ms bucket | Not measured: the resolve module is not implemented and the named native checkout is absent at `/mnt/c/vixen-native/vixenmodules2` |
| Repeat-capture byte identity | Not measured; only one frame per angle was captured |
| Default before/after byte identity | Not measured; no implementation was made |

## STOPs

1. **Registered node groups are not expressible through the existing registry/wiring API.** `NodeTypeRegistry::Register<T>` requires `T` to derive from `NodeType`; `SubGraphType<Derived>` in `RenderGraph.h` is currently only a marker and is instantiated by C++ type. `VIXEN_REGISTER_NODE` cannot register a group, and the dynamic `AddNode(name, instance)` path only creates `NodeInstance`s. Static `RenderGraph::Instantiate<TSubGraph>` and its typed `Connect` overloads do not provide the R492 group-as-node name lookup, registered boundary wiring, or capability roll-up. Adding a parallel group registry would violate R492 and the brief. Required next step: extend the existing NodeType/NodeTypeRegistry and graph wiring contract so a group is the registered node type, with its `PortConfig` as the node config; then register and compose photon cells through that same path.

2. **The required SceneLook profile seam is not in this branch's merge base.** `origin/wave/authoring-convergence` remains `d15a7141`; the referenced `lane-bimodal` commit `17798c83` is not its ancestor. This worktree has no `SceneLook`, `SceneLookDeclaration`, `SceneLookBimodal`, planet scene definitions for C1/C7, or test-only cel-profile selector to move onto a declared lighting profile. The lane brief says to integrate through that seam and not edit its shader, so the four-way profile comparisons and C1/C7 captures must wait for those changes to land on the wave branch.

3. **The current photon-cell path does not yet satisfy the S2 output contract.** It has deposit/fold/clear table nodes but no per-pixel irradiance resolve image and no optional indirect-light input on the shading config. Deposit currently processes hit records rather than a fixed photon emission budget split across emitters as required by R502.2. These are module implementation gaps; no core shader or module workaround was added around them.

4. **Native timing cannot be witnessed from the supplied checkout.** The named Windows path `/mnt/c/vixen-native/vixenmodules2` does not exist; sibling native lane directories were left untouched. Since there is no photon resolve module to time, I did not create a replacement checkout or substitute WSL timing for the native-only R494 gate.

## PROPOSALS

No additional product-design proposal was made. The module work remains stopped on the exact prerequisites above.

## SPT DISPOSITION

Added the required tooling proposal: `Provide a pixel-preserving contact-sheet utility for visual lanes` (`8ca26d36eace`), in `.spt-proposals/vixenmodules2.jsonl`.

## CONSOLIDATION ISSUES

- proposed: Provide a pixel-preserving contact-sheet utility for visual lanes
