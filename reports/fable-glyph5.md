# fable-glyph5: baked glyph walk on the GPU and the CPU (R892.13, R892.14, R892.18, R892.19)

Successor of fable-glyph4 on the VIXEN glyph / recipe ray-march track. Nothing pushed. No reference
changed. Visible image change: yes, small; it needs the owner review of section 5 before it lands.

## STATUS

| Order item | State |
|---|---|
| 1. GlyphBake test project + solution entry, tests green | **Done.** Kernel `ff5e3c403`. 60 tests. One bake defect found and fixed (grid pad narrower than the band for small glyphs; layout version 3). |
| 2. GPU face of the baked walk in VIXEN | **Done** for the walk, the reader, the upload, the CMake step, the band derivation and the CPU face. CPU face == kernel tool bit for bit; GPU vs CPU face 0 sign mismatches (section 3). |
| 3. Quiet-window timings, cold compile, review package | **Timings done** (two exclusive windows): 1080p march 8.0 ms / wave 5.7 ms, 4K 27.5 / 20.6 ms (baseline 34.6 / 33.3 and 117.2 / 74.9). Cold compile done (faster than the old walk). **Review package: three of the scenes** (RTL pair, latin carved, latin embossed), accepted by the owner as R892.20 with one condition (section 5); the rest of the set is in the successor brief. |
| 4. Witness of kernel `f4b200e00`; analytic extruded-glyph face A/B | **Not started.** |

## SHAs

| Repo | Branch | Commit | Content |
|---|---|---|---|
| Kernel | `lane-glyph5` (from `cb9a4f5b7`, VIXEN's tracked pin) | `c36e2bc82`, `ef88ddac8` | cherry-picks of lane-glyph4 `02e61c303`, `333e3d993` (GlyphBake~), without `f4b200e00` |
| Kernel | `lane-glyph5` | `ff5e3c403` | test project, pad >= band, layout 3, `unpack` / `bake --image` / `samples` |
| VIXEN | `lane-glyph5` (from `lane-rt6` `253de919f`) | `1010658fe` | build step, image reader, packed rows, CPU walk |
| VIXEN | `lane-glyph5` | `b9214e76b` | shader walk, registry, CPU evaluation, text scenes |
| VIXEN | `lane-glyph5` | `4de6669c9`, `d54c9a3b9` | tests |
| VIXEN | `lane-glyph5` | `585cbeddb` | the bake tool rebuilds on a pin change only |

Why the kernel branch was rebased: `lane-glyph4` sits on `f4b200e00` (subtree skip), which changes
generated recipe code. VIXEN builds the bake tool from its tracked kernel pin, so the pin must
contain GlyphBake~; on `cb9a4f5b7` + GlyphBake~ the pin moves without regenerating anything, and the
timings below measure the walk only. **Land GlyphBake from `lane-glyph5`, not from `lane-glyph4`.**
`f4b200e00` stays on `lane-glyph3` for its own witness.

Worktrees (ws): `~/projects/Yeroket-Fantasy/.claude-worktrees/glyph5`,
`~/projects/VBVS--VIXEN/.claude-worktrees/glyph5`. Native tree `C:\vixen-native\glyph1` holds the build
of VIXEN `lane-glyph5`. Captures: `C:\vixen-native\glyph5-scratch`.

## 1. Kernel: GlyphBake tests and one bake fix (`ff5e3c403`)

- `GlyphBake~/Tests/GlyphBake.Tests.csproj` (NUnit, in `Yeroket-Fantasy.sln`). The fonts are written in
  memory by `SyntheticFont` (head, maxp, loca, glyf, cmap format 4), so the suite needs no font file.
  60 tests: reader (lines, implied on-curve points, composite offset and scale, cmap, CFF refused);
  non-zero union (two overlapping squares, a self-crossing contour, a shared edge, a collinear
  overlap, nested contours of the same and of the opposite direction, input order); chord cutting
  against the formula; the walk against every chord of the glyph; the far-cell bound; the key (its
  exact contents, and that every parameter and the font bytes change it); byte-identical bake at
  width 1 and full width; image layout; a moved chord is seen by verify; the command line.
- **Defect fixed.** Outside a glyph's grid the walk returns the distance to the bounds. The grid pad
  was 1/8 of the glyph size, less than the 0.03 em band for a small glyph (a period, 40 units wide),
  so a recipe that rounds such a glyph by less than the band would have rendered a rounded box
  there. The grid now reaches at least the band past the bounds. Same parameters, other blocks:
  layout version 2 -> 3, a new content key. Verify now counts a sample outside the grid and inside a
  finite band as a mismatch.
- Command line: `bake --image F` and `unpack` write the decompressed image (controller decision 3:
  no Brotli in VIXEN); `samples` writes points and the walk's values for another implementation to
  reproduce bit for bit; bad input exits 1 with one line.
- Real fonts after the fix (`kernel-tests-2.log`): Lato 464 glyphs, Noto Sans Arabic 1704 glyphs, 400
  samples each: 0 sign, 0 distance, 0 bound, 0 union mismatches; 1.10 MB / 7.4 MB at rest, 2.9 MB /
  20.4 MB loaded; 3.4 / 3.7 chords per sample.

Checks: `GlyphBake.Tests` 60/60 (`kernel-tests-2.log`, label `fable-glyph5:full:glyphbake-tests`). The
SG and CG suites were not run: no source they build changed (GlyphBake~ is referenced by nothing).

## 2. VIXEN: what was built

**Build step** (`cmake/VixenGlyphBake.cmake`, `codegen/CMakeLists.txt`). The tracked kernel pin is
`ff5e3c403`; the snapshot archives `GlyphBake~`. codegen builds `glyph-bake` from the snapshot and
bakes each declared font (Lato, Noto Sans Arabic) once per band in `VIXEN_GLYPH_BAKE_BANDS_EM`
(default `0.03`): `<build>/glyph-bake/<font>.band<b>.glyphbake` (compressed, the pack file),
`.glyphimage` (decompressed, what the runtime reads) and `.glyphsamples`. Target `vixen_glyph_bake`.

**Reader, rows, CPU walk** (`Recipe/GlyphBakeStream.h`, `src/Recipe/GlyphBakeWalk.cpp`).
`GlyphBakeImage` checks every block when it loads (sizes, list offsets, chord indices), so the walk
cannot read outside a block. A row is a list of placements of baked glyphs; `BuildGlyphBakeStream`
packs rows into the words of the packed glyph buffer, each block once. The CPU walk is compiled
without fused multiply-add so it gives the bits of the kernel tool.

**Shader walk** (`shaders/RecipeGlyphBindings.glsl`). Binding 52, same buffer and declaration, new
words (R896: no binding number chosen). Per sample: a loop over the row's placements; per placement
one cell lookup, then the stored bound and sign (far cell, no chord read) or a counted loop over
the cell's chords: squared distance, one square root, sign from two axis-aligned crossings.

**Placements.** The RTL pair packs several glyphs into one 64-segment row. A baked row therefore
lists placements and the walk takes the least signed distance. That is exact outside every glyph,
and inside while the glyphs of a row do not overlap. Where they do overlap (kerning, Arabic marks)
the sign stays right (inside any glyph gives a negative value, and the minimum keeps it) and the
magnitude is a lower bound of the true inside distance, never more. The recipe programs are
unchanged, so the timings compare like for like.

**Band** (controller decision 1; `Recipe/GlyphBakeBand.h`, `RecipeRegistry::Register`). The band is a
bake parameter in the key. Register follows every GlyphOutline value through the program: Round and
Onion add their radius, a smooth union its width, RestorePos converts by the Transform distance
scale; any other operation that consumes the value makes the need unbounded. Need = offset in em +
0.005 em margin. The row uses the narrowest bake of its face that is exact that far around every
glyph of the row (the per-glyph reach is read from the block, so a narrow grid cannot hide). No
such bake: `RegisterResult::GlyphBandUnavailable` and `GlyphDiagnostic()` names the recipe, the
instruction, the row, the band and the fix. The look-dev text recipes round by 0.035 world units:
0.0049 to 0.0066 em, inside 0.03.

**CPU evaluation reads the same words** (controller: required before landing; done). `evalRecipe`
takes the packed stream; a baked row is evaluated by the CPU face of the walk, so CPU and GPU
evaluate one shape (R619.1). A row without a baked form still evaluates its segment row on the
CPU; when such a row has an outline the shader build stops with the reason (never silent).

**Text.** `GlyphAtlas::AttachGlyphBake(face, bake)`; outline rows then carry a baked row (font units
to 26.6 units by the pixel size alone: unhinted) and bounds that hold both forms. Shaping still uses
FreeType/HarfBuzz at 24 px. The glyph look-dev scenes and the recipe preview attach the bundled
bakes.

## 3. Correctness

| Check | Result | Evidence |
|---|---|---|
| Native build, all targets and every `*_check` | exit 0 | `native-build-3.log`, `native-build-4.log` |
| `test_glyph_bake_stream` (12): C++ walk vs kernel tool, 64 samples x every glyph | Lato 29,696 and Noto 109,056 samples, **0 bit mismatches** | `tests1.log` |
| same: rows, band selection, malformed images, band derivation, registry refusal, CPU evaluation | pass | `tests1.log` |
| `RecipeGlslNumericalParityTest.BakedGlyphRowsMatchTheCpuFace` (GPU, AMD 8060S): production shader text vs CPU face, 82 rows, 335,872 samples | **0 sign mismatches**; 312,409 bit-equal (93%); the rest differ by at most 3.5e-5 row units | `tests1.log`; rerun with the bound below: pass, worst difference 2.4e-4 row units where 2.1e-3 is allowed (`tests2.log`) |
| `test_uber_shader_splice` 16, `test_recipe_registry` 18, `test_recipe_eval_parity` 105, `test_text_shaping` 11 | pass | `tests1.log` |

The GPU is not required to round a division exactly; the projection onto a chord subtracts numbers
of the size of the sample coordinates, so the test's bound is 2 units in the last place of the
larger coordinate. "0 mismatches" in the strict bit sense holds C# vs C++, not C++ vs GPU.

## 4. Timings

RTL pair, exclusive device (`--gpu-benchmark`, jobs `bench1080`, `bench4k`), warm compile, stock
shaders of `d54c9a3b9`, median (range) of single-submission reads:

| | march | shadow wave | evidence |
|---|--:|--:|---|
| 1080p baked walk (n=25) | **8.0** (7.9..8.9) | **5.7** (5.6..5.8) | `bench1080.log` |
| 1080p recorded quiet baseline | 34.6 | 33.3 | fable-glyph4 |
| 4K baked walk (n=17) | **27.5** (27.1..28.6) | **20.6** (20.0..20.8) | `bench4k.log` |
| 4K recorded quiet baseline | 117.2 | 74.9 | fable-glyph4 |

Ratios: 1080p 4.3x and 5.8x; 4K 4.3x and 3.6x. The baseline was measured on the glyph4 base
(`6d0f8a23d`), which differs from this base (`253de919f`) by the sliver normal rule on primary rays
only (controller: acceptable). No tree on `253de919f` was available as a same-window control.

**Where the 1080p frame goes now** (same run, `summ.sh bn-bake`, worst read per pass): march 8.9,
shadow wave 5.8, spatial_reuse 1.35, b1_hiz_downsample 0.24 (1.07 on the first read), exposure_meter
0.36, tile sort + scatter 0.25, tonemap 0.09 ms: about 16 to 17 ms of GPU passes. The steady frame
period of the run is 18.8 to 23.1 ms (n=23). The 3 to 6 ms between the two is not attributed by
this lane: it is outside the per-pass GPU reads (CPU frame work, submit and readback of the capture
harness). That, and the march itself, are the gap to 16.7 ms.

**Cold pipeline compile**, RTL pair 1080p, shared jobs, a fresh constant in reachable walk code per
run (`mkvariant.sh`, `phases.sh`): first compile ever of the new shaders 37.1 s march / 52.9 s
waves (`cap1`), keyed runs 30.8 / 56.5 and 48.7 / 45.0 (`cold1`). Mean 38.9 s / 51.5 s. The old
walk cold was 55.5 s / 67.2 s (fable-glyph4 `cold1`, glyph4 base, also shared): 0.70x and 0.77x.
Not one job, not one base: the claim is "not slower", inside the 1.5x rule with room.

Shared-device frame times (`cap1`: 24.1 / 13.8 ms) were 3x the quiet ones; they are not used.

Import on Windows native gives the same image bytes as on Linux (`cmp` of the build tree's Lato
image against the WSL bake).

## 5. Review package

Sheets (ws): `fable-glyph5-evidence/review/review-rtl.png`, `review-carved.png`,
`review-embossed.png`: reference above new, boxes on regions that moved by more than 8 levels, then
x6 zooms (reference | new) of the six largest changes. Reference = opus-rt7's regold PNGs of
`253de919f`; new = `cap1` (shared captures of `b9214e76b`). Made by `review/sheet.py`.

| Scene | pixels moved > 8 levels | > 32 levels | of |
|---|--:|--:|--:|
| RTL pair | 146 | 57 | 2,073,600 |
| latin-carved | 296 | 126 | |
| latin-embossed | 297 | 23 | |

About 2.03 M pixels differ by a few levels in every pair; a rerun of an unchanged binary does that
too (the references are compared by hash, see CONSOLIDATION ISSUES), so only the thresholded counts
describe the walk. What moves: glyph edges by a fraction of a pixel (unhinted outlines, chords cut
by curvature). Arabic glyphs with overlapping contours render as one outline.

**R892.20 (owner, 11-17): accepted for these three scenes, on one condition: explain the new dark
pixel below the E's bottom bar in `review-embossed.png`.** What I measured:

- It is one of three isolated dark pixels in the new capture: (1162, 483), (1185, 652),
  (1184, 680) in the 1920x1080 image. The owner's pixel is one of the two near the E.
- The accepted reference has two pixels of the same kind in the same image, at (1160, 477) and
  (1165, 491). So the count goes 2 -> 3, and the positions move.
- They are deterministic: a second capture of the same binary has the same three.
- **They do not come from the band or the far-cell bounds.** I baked Lato with an infinite band
  (every cell exact, no bound anywhere), put that image in place of the banded one, and captured:
  the same three pixels (`speck-inf.log`, `speck-band.log`). The banded image was restored and
  compared byte for byte.
- The lists are sound for every Lato glyph (verify: 0 sign, 0 distance mismatches in 185,600
  samples), and the GPU agrees with the CPU face in sign on all 335,872 parity samples.

So it is not a defect of the baked data or of the band. It is the kind the reference already has:
a single shadowed pixel in a lit area beside the shadow edge of an extruded glyph wall (the
"dark pixels in lit areas" left after R892.9, filed as findings there). The outline moved by a
fraction of a pixel, and the pixels of that kind moved with it. **What I did not do:** replay the
shadow ray of that pixel on the CPU to say whether it is the hit-normal rule at a convex edge or a
thin wall under the any-hit march. The CPU now evaluates the same rows as the GPU, so the replay
is possible; it is the first item of the successor brief. I fixed nothing here because nothing in
the walk is shown wrong.

## 6. Not done, and what each needs

- **Image location (next slice, controller 11-17).** The build writes the image under
  `<build>/glyph-bake` and passes paths as compile definitions. R892.19 wants the decompressed
  image in the CashSystem cache: a manifest written by the build step (font -> content key ->
  band) and the image at `SharedDeviceCacheDirectory()/glyph-bake/<key>.glyphimage`.
- **WSL dotnet bridge**: the bake step is skipped with a warning there (GlyphOutline recipes then
  fail the shader build with the reason). Native Windows and Linux bake.
- **The generated walk** (SPT `7205585d3323`): the walk is hand-written twice in VIXEN (GLSL, C++)
  plus the C# original. One kernel callable should generate the three.
- **Kernel `f4b200e00` witness** and the **analytic extruded-glyph face A/B** (R892.13.4).
- **Variable fonts**: default instance only. **Glyph subset bakes**: not built.

## SUCCESSOR BRIEF

Bases: kernel `lane-glyph5` `ff5e3c403`; VIXEN `lane-glyph5` tip (this report's commit). Native tree
`C:\vixen-native\glyph1` is built from VIXEN `lane-glyph5`. Harness in `fable-glyph5-evidence/`:
`nbuild.sh N [TARGET]`, `tests.sh NAME` (runs `NAME.runs.sh` as one shared GPU job),
`cap.sh JOB shared|bench RUN...` (start it with `setsid nohup` on ws: a 10-minute tool timeout
killed a waiting benchmark job), `passms.sh`, `summ.sh`, `phases.sh`, `mkvariant.sh NAME`,
`review/sheet.py`. Do not set `VIXEN_FETCHCONTENT_CACHE` for native builds.

In order:
1. **The dark pixel (owner condition, R892.20).** Replay the shadow ray of pixel (1185, 652) or
   (1184, 680) of `glyphsdf-latin-embossed.png` on the CPU: `evalRecipe` with the registry's packed
   stream evaluates what the GPU walks. Classify: hit-normal rule at a convex edge (sliver class,
   R892.16), a wall thinner than the any-hit step, or a defect. Same for the reference's two pixels
   on a `253de919f` binary if the classes must be compared. Report to the controller before landing.
2. **Rest of the review set**, as captured, to the owner: `qps-rtl-carved`, `qps-rtl-embossed`, the
   two `analytic-vs-voxel` pairs, `rtl-proof-submit-guard`, and the look-dev scenes that use glyphs.
   Run the affected native GPU CTest (`glyph4-scratch\suite-affected.ps1 -OutDir
   C:\vixen-native\glyph5-scratch\suite1`, about 25 minutes shared), compare each GlyphSdf capture
   with `opus-rt7-evidence/regold/glyphsdf/<name>/` through `sheet.py` (threshold 8 levels). The
   GlyphSdf CTest hashes will differ: that is the re-golden, after the owner accepts.
   The voxel comparison scenes now bake from the baked rows on the CPU (same shape as the GPU).
3. **R896 rebase**: when the controller sends opus-bind1's slice 2a, rebase; the buffer
   declaration becomes `VXB_GlyphOutlineFlatBuffer`. My change keeps binding 52's buffer and name.
4. **Image in the CashSystem cache** (section 6, first item).
5. **The frame gap to R892.1** (section 4): attribute the 3 to 6 ms between the GPU pass sum and
   the steady frame period at 1080p; spatial_reuse is 1.35 ms; the march is 8 ms.
6. Kernel `f4b200e00` witness (merge lane-glyph3 into a scratch kernel branch on top of
   `ff5e3c403`, move the pin, rebuild, GlyphSdf + look-dev hashes), then the analytic
   extruded-glyph face A/B (R892.13.4): with baked chords it is a DDA over the same grid.
7. Before DONE: full kernel preflight line for the final tips; affected GPU CTest; `git status`.

## LANDABLE NOW

| Repo | Commit | State |
|---|---|---|
| Kernel `lane-glyph5` | `c36e2bc82`, `ef88ddac8`, `ff5e3c403` | GlyphBake.Tests 60/60; standalone tooling. Landable. |
| VIXEN `lane-glyph5` | tip | **Not before the owner review** (image change) and the R896 rebase onto opus-bind1 slice 2a (`VXB_GlyphOutlineFlatBuffer`). |

`PREFLIGHT lane-preflight-248f655d626c 248f655d626ca08f556385fc70f7f1dbf02b740a: green all-gates`
(kernel `ff5e3c403`, undertow `248f655d6` = origin/main-wave, launcher = origin/main; report
`reports/lane-preflight-cli/20261117T131811Z-2285150/report.md`).

## REDS

None found on the bases. Mine, fixed before commit: the pad/band defect (section 1).

## CANONICAL-SHAPE CHECKLIST

Kernel added lines: a test project and tooling code; no kind, system, pipe, relation, attribute,
`KindRef`, `EntityId`, `Catalog<`, `.Baked()` or dispatch signature; `throw`s are import-time. VIXEN
is outside the checklist. No Python added to a repo. `Program.Legacy.cs`: untouched.

## CONSOLIDATION ISSUES

- **The kernel snapshot cache ignores its path list.** `vixen_kernel_snapshot` keys the cached
  source by commit only. Adding `GlyphBake~` to the archived paths did not invalidate the snapshot
  my first configure had made for the same commit; I deleted that one directory
  (`%LOCALAPPDATA%\VIXEN\kernel-codegen\sources\yeroket-ff5e3c40...`). The key should include a hash
  of the path list.
- `VIXEN_FETCHCONTENT_CACHE=<worktree>/.tmp/fetch` from the lane rules breaks the native Windows
  configure (a WSL path); it applies to WSL builds only. The rule should say so.
- `libraries/` is configured before `codegen/`, so a library target cannot see a variable codegen
  sets; the bake outputs are named in a module both include.
- The captured GlyphSdf references are hashes; the PNGs live in another lane's evidence folder
  (`opus-rt7-evidence/regold`). A rerun of an unchanged binary moves about 2 M pixels by a few
  levels, so "changed pixels" needs a threshold to mean anything; I used 8 levels.
- I overwrote one file (`img/new-rtl.png`) of another agent in the session scratch folder shared
  on the laptop; scratch only.
- Foreground waits are cut near 180 s by the agent harness (as fable-glyph4 noted).

## SPT DISPOSITION

No `spt` write command run.

| Id | Disposition |
|---|---|
| `7205585d3323` generated walk with the grid | PARTIAL: the bake, the GPU walk and the CPU face exist and agree; generating them from one kernel callable remains. |
| `5e02066c9b3c` cold compile check in witnesses | PARTIAL: still a script (`patch_key.py`), not a harness feature. |

## EVIDENCE (`~/codeman-cases/undertow/reports/fable-glyph5-evidence/` on ws)

`kernel-tests-1.log`, `kernel-tests-2.log`, `native-build-1..6.log`, `tests1.log`, `tests2.log`,
`cap1.log`, `bench1080.log`, `bench4k.log`, `cold1.log`, `speck-inf.log`, `speck-band.log`,
`preflight.log`, `review/`, harness `ktest.sh`, `nbuild.sh`, `tests.sh`, `cap.sh`, `passms.sh`,
`phases.sh`, `summ.sh`, `bake/` (layout 3 bakes and images of both fonts).
