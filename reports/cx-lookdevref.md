# cx-lookdevref — retained look-dev reference provenance

## LANDABLE NOW

YES. VIXEN commit `5ab76560` refreshes the retained R432 look-dev references from the canonical current-tip capture set and adds a provenance-aware comparison guard. The retained set now represents VIXEN source `6f3c64e1a49a663fa5049a44028517276f6e5aa7` on `ws`, captured 2026-10-25 with the Dozen Vulkan driver at 500×500.

Changed surfaces:

- `reports/lookdev-visual/captures-shadow-on/` and `captures-shadow-on-repeat/` — 32 refreshed PNGs, four states × four angles × first/repeat.
- `reports/lookdev-visual/reference-provenance.json` — source commit, capture date, machine, display, resolution, warm-up, driver and known visual-change commits.
- `tools/compare-capture-pixels.py` — optional `--reference-provenance`, `--repository` and `--source-commit` arguments. It prints provenance and fails with `references older than <commit>` when an accepted visual change lies between the reference and compared tree.
- `tools/tests/test_compare_capture_pixels.py` — current-reference and stale-reference coverage.
- `reports/cx-lookdevref-visual/` — one reference | current | diff contact sheet per state.

## DRIFT CAUSE

The original retained files were all introduced by `0e405d71ec4b8f93d9d2ec34aa747d6d91eb702a` (`Add R432 look-dev scene and capture witness`, 2026-10-08 02:47 +0300). They had no source commit, machine or driver metadata, so the later comparison could only report pixel drift.

The drift is an accepted visual evolution on the wave branch, not an unexplained driver or resolution difference. The first relevant accepted change after the retained capture is `bff5507184efcfb6bbbcaf6a079d1e28b741f2fc` (`feat(lookdev): place scene instances through affine transforms`, 2026-10-08 11:48 +0300). Further accepted changes altered the rendered look and are recorded in the provenance JSON: generated hull/material residency (`7108bd88`, `75d3d282`), night-service emitters and moved light sources (`4b9ddc50`, `363bf151`), scene-declared sky/cel defaults (`8f6e990a`) and the bimodal capture set (`652ea869`).

The independent train-7 control and current-tip capture sets were built with the same Release/CMake setup and are byte-identical for all 32 first/repeat images. That rules out the retained-reference delta being caused by the current driver, display resolution or a SIMD4 widening change. The old reference/current and diff images were inspected before replacement; the final refreshed reference/current/diff sheets were inspected after replacement.

## VISUAL BASIS

The retained scene serves T01, T04, T06, T07, T10, T13, T18, T21 and T22 from the visual bible. The review references used were:

- C16 — `/home/liory/codeman-cases/undertow/reports/visual/images/C16.png`: overall world palette and machine/nature balance.
- R2-09 — `/home/liory/codeman-cases/undertow/reports/visual/iterations/lighting/r2-09-bimodal-midday-four-groups.png`: midday contrast.
- R2-10 — `/home/liory/codeman-cases/undertow/reports/visual/iterations/lighting/r2-10-bimodal-late-afternoon-hue.png`: warm late-afternoon key.
- R2-11 — `/home/liory/codeman-cases/undertow/reports/visual/iterations/lighting/r2-11-bimodal-overcast-fill.png`: diffuse overcast form.
- R2-16 — `/home/liory/codeman-cases/undertow/reports/visual/iterations/lighting/r2-16-split-night-lifted-shadow.png`: night/service readability and local light contrast.

## WITNESS

Reference metadata and comparison output:

- Provenance: `reports/lookdev-visual/reference-provenance.json`.
- The canonical current-tip capture evidence is `/home/liory/codeman-cases/undertow/reports/cx-simd4recipe2-evidence/`; its train-7/current comparison reports 32/32 byte-identical images.
- State comparisons against the refreshed references: 16/16 reference/current pairs passed `--require-byte-identical`; every pair reports `byte_diff=0`, `pixel_diff=0`, `max_channel_delta=0`, `bbox=none`.
- First/repeat image reuse check: 32/32 PNG pairs match byte-for-byte.
- Comparator unit tests: 6 passed, including the stale-reference message assertion. `python3 -m py_compile` passed.
- Contact sheets, all visually reviewed: [midday](cx-lookdevref-visual/midday-reference-current-diff.png), [late afternoon](cx-lookdevref-visual/late-afternoon-reference-current-diff.png), [overcast](cx-lookdevref-visual/overcast-reference-current-diff.png), [night/service](cx-lookdevref-visual/night-service-reference-current-diff.png). Each sheet is reference | current | diff; the diff column is uniformly unchanged grey because the images are identical.
- RenderGraph/SVO control: the prior canonical train-7 witness reports 1,437 CPU tests with 0 failures and 826 selected GPU tests with 12 known failures; no new failing name appeared against train 7. Evidence: `/home/liory/codeman-cases/undertow/reports/cx-simd4recipe2-evidence/rendergraph-svo-final/`.

No product build was required for this docs/reference/tooling-only delta. The product capture binary and RenderGraph/SVO result set are the canonical current-tip witness described above; the modified Python surface was tested directly.

## STOPs

None. The previously retained reference drift is explained by accepted visual commits and is corrected.

## CONTINUATION

None. The next look-dev capture should pass `--reference-provenance reports/lookdev-visual/reference-provenance.json --repository .` so a future accepted visual change produces an actionable `references older than <commit>` diagnostic before pixel comparison.

## SPT DISPOSITION

No SPT task ID was provided or worked on in this lane.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

None. Provenance recording and stale-reference detection were the requested look-dev comparison repair, not incidental scaffolding.
