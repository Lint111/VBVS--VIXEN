# capstatic lane report

## LANDABLE NOW

- **R485 inventory:** written to [`reports/capstatic-determinism.md`](capstatic-determinism.md). It identifies one editor-state GPU output, its five-pass dependency chain, sixteen visual-only programmable pass families, and the cross-implementation floating-point sites. No shader was changed.
- **Base:** started from `4fd3f2d617cff83f5f932286ac2509a3f6ddf7ba` and fast-forwarded to `1252e3705a8247f5b0b1478f0164da55c7cd44ba` before the final report. The merged delta adds Windows Vulkan provisioning and `tools/multiinstance_cache_gate.py`; neither changes the production shader graph.
- **VIXEN configure:** `cmake --preset vixen-wsl` passed from `VIXEN/` with `VIXEN_FETCHCONTENT_CACHE="$PWD/../.tmp/fetch"`; log: `.tmp/capstatic/baseline-configure.log`.
- **VIXEN baseline build:** passed, exit 0, 1149 Ninja steps. Command: `set -o pipefail && nice -n 10 cmake --build ../build/wsl -j4 2>&1 | tee ../.tmp/capstatic/baseline-build.log` from `VIXEN/`. It ran on the assigned `4fd3f2d6` source tip before the required fast-forward; the merged changes are confined to Windows provisioning and a Python test helper.

## R484 STATUS — STOP FOR A SHAPE RULING

The current graph has `VersionInputCapability`, `VersionRequirementCapability`, extension/feature nodes, promoted-feature nodes, and composites in `VIXEN/libraries/VulkanResources/include/CapabilityGraph.h`. These cover the existing SDK/API/toolchain floors and current device extension/feature predicates. The requested personalized configure contract also needs graph-declared platform and product-variant facts, queue-family facts, and driver/device identity facts. Those do not fit the current node forms: `PhysicalDeviceInfo` omits identity/queue capability data, and queue selection is currently performed separately by `VulkanDevice`/`DeviceNode`.

The base `CapabilityNode` also has no binding-time field. Adding a binding-time property to that existing base would be a plausible implementation, but it does not supply the missing fact declarations. The brief says to stop when the declaration does not fit an existing form, so I have not invented a new node type or split declaration channel.

Options for the owner:

1. **Recommended:** approve a graph declaration form for typed build facts (platform/product variant and selected-device identity/queue properties), then add binding time to the existing graph node base and generate the configure-time constexpr header from that graph.
2. Keep platform/product/device facts outside the capability graph in a separate configure header and limit graph binding-time metadata to currently representable nodes. This is smaller, but no longer satisfies the graph-owned declaration contract as written.

R484 gates are therefore **not run**: personalized-path symbol/size elision; full-mode compile-all; mismatched-device startup refusal; RenderGraph and SVO suites in both modes; and same-device full-versus-personalized render comparison. They need the declaration shape and both build modes first.

## R485 RESULT

- State-affecting chain: **5 pass families**, with **7 shader source variants/stages** when counting the B2 compute writer and its vertex/fragment twin. The semantic sink is the in-memory editor `SelectionSet` after a pick. No GPU result routed to save, network, or replay state was found in the active graph.
- Visual-only candidates: **16 programmable pass families**, plus sky-sphere, presentation blit, and UI composition work.
- Shader changes: **none**, as requested.

## VERIFICATION

| Check | Result |
|---|---|
| CodeGraph explore (required first lookup) | Unavailable: this worktree has no `.codegraph` index. Continued with source inspection. |
| `cmake --preset vixen-wsl` | Pass; configure completed in the worktree. |
| Fresh VIXEN baseline build | Pass, exit 0; full build and generated golden checks completed. Log: `.tmp/capstatic/baseline-build.log`. |
| R484 personalized/full-mode and GPU test gates | Not run; stopped for the graph fact declaration ruling above. |
| R485 artifact review | Pass; `git diff --check` clean. |

## SPT DISPOSITION

No SPT task IDs were assigned in this brief. R484 and R485 are Undertow register entries; R485 is complete as an inventory, and R484 remains open pending the shape ruling.

## CONSOLIDATION ISSUES

- proposed: Provide a CodeGraph index in VIXEN lane worktrees
