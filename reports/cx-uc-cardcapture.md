# cx-uc-cardcapture — native look-dev unblock and undercard card capture

Date: 2026-11-03
Branch: `lane-uc-recipes`
Merged base: `origin/wave/authoring-convergence` at merge `60ef1afa`

## LANDABLE NOW

- Native tooling fix commits: `569a333a`, `2cce9fef`, `8fb92406`, and `5e21191e`.
- Final queued native build: `cx-uc-cardcapture:native-lookdev-build-fixed3`, `exit=0 state=OK`; `vixen_lookdev_capture.exe` linked at Ninja step 667/668.
- Merged-tree incremental native build: `cx-uc-cardcapture:native-lookdev-build-merged`, `exit=0 state=OK`; configure completed and the capture executable relinked in 11 Ninja steps.
- Merged-tree launcher regression: `cx-uc-cardcapture:native-launcher-test-merged`, all 15 checks passed, `exit=0 state=OK`.
- Stage page and questions file were updated externally; this report is copied to `/home/liory/codeman-cases/undertow/reports/cx-uc-cardcapture.md`.

## STALL ROOT CAUSE AND FIX

The first WSL cancellation stopped the wrapper but left Windows-side `cmake.exe` alive. That orphan retained the machine-wide `Global\\VixenConfigureLock`; the next native run waited behind it and then encountered an abandoned mutex. Separately, the flattened `C:\\vixen-native\\uc-recipes` checkout had no paired-repository ancestry, so CMake could not infer `UNDERTOW_ROOT` and failed in `libraries/Text/CMakeLists.txt`.

The VIXEN fix now terminates the exact Windows child on cancellation, recovers abandoned configure/build mutexes, emits a 30-second heartbeat, streams Ninja progress, and infers/forwards the paired Undertow and Yeroket roots through `WSLENV`. The final run configured in 56 seconds and compiled the target successfully. Windows samples showed responsive CMake/Ninja and active `cl.exe` processes, proving the earlier apparent stall was a slow provider/configure/compile path plus lock ownership, not a dead link step.

## CAPTURE STATUS

No card capture pixels were produced or opened. The five assigned inputs are JSON review graphs, while the VIXEN named-recipe path accepts `.vrc`/`.vxd`. The queued Undertow Author CLI checks for JSON bake, `new renderrecipetemp`, and template bake all failed before the CLI ran because the current Undertow base has the pre-existing `Undertow.Modding.NamespacedId` compile errors. Evidence: [recipe-bake-20261103](/home/liory/codeman-cases/undertow/reports/cx-uc-cardcapture-evidence/recipe-bake-20261103/).

The corrected five-input GPU preflight was submitted through the GPU queue, but remained queued behind pinned-drain/CPU pressure at the report checkpoint and was cancelled before admission; no per-recipe process or image was emitted. Evidence: [json-gpu-preflight-corrected-20261103](/home/liory/codeman-cases/undertow/reports/cx-uc-cardcapture-evidence/json-gpu-preflight-corrected-20261103/).

| Block-out | Recipe | Capture path | Readability verdict |
|---|---|---|---|
| B01 | `b01-shared-title-forward.json` | None; preflight queued | Not run; no pixels. Concept reference opened and supports the recommended shared-face baseline. |
| B02 | `b02-pip-forward-wide.json` | None; preflight queued | Not run; no pixels. |
| B03 | `b03-footer-grouped-compact.json` | None; preflight queued | Not run; no pixels. |
| B04 | `b04-utility-brass-lamp.json` | None; preflight queued | Not run; no pixels. |
| B05 | `b05-transport-copper-notch.json` | None; preflight queued | Not run; no pixels. |

§7 pixel checks are therefore all `N/A`: luma p5/p50/p95, haze/emissive share, greyscale paper/brass/copper distinction, pip grouping, and edge/silhouette cannot be claimed without captures. No duo contact sheet was made because there were no captures to open beside the references. The questions file cites the four concept images that were opened.

## WITNESS PATHS

- Native build logs: [fixed-build3-20261103](/home/liory/codeman-cases/undertow/reports/cx-uc-cardcapture-evidence/fixed-build3-20261103/).
- Merged-tree build logs: [merged-build-20261103](/home/liory/codeman-cases/undertow/reports/cx-uc-cardcapture-evidence/merged-build-20261103/).
- Merged launcher test: [native-launcher-test-merged](/home/liory/codeman-cases/undertow/reports/cx-uc-cardcapture-evidence/native-launcher-test-merged/).
- Earlier stale-lock reproduction and Windows process evidence: [repro-20261103](/home/liory/codeman-cases/undertow/reports/cx-uc-cardcapture-evidence/repro-20261103/).

## STOPs

- **Capture source unavailable:** no documented JSON→VXD/VRC route was verified. The supported shape is `render_recipe` `.vxd` → `undertow-author bake` → VRC1 in a pack, but the CLI could not build on the current Undertow base. This is independent of the now-green VIXEN native target.
- **GPU preflight not admitted at checkpoint:** the corrected queued job has no capture output yet; do not treat its queue wait as a renderer result.

## CONTINUATION

Provide a supported flattened source for B01–B05 (or repair the Undertow Author CLI base red), then rerun the corrected GPU route for all four lighting states and both face-on/table-distance views. Open every image, measure §7, make duo sheets, and only then choose B01 versus B03 for Stage 4.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

- proposed: Recover abandoned Windows native build locks and surface queue diagnostics
- proposed: Define JSON review graph to VXD or VRC bake contract
- proposed: Add a queued native executable capture route with process cleanup
- existing lane proposals remain in `.spt-proposals/cx-uc-recipes.jsonl`.
