# fable-glyph6: dark pixels explained, review set accepted and recorded, R896 rebase, glyph image in the shared cache

Successor of fable-glyph5 on the VIXEN glyph ray-march track. Nothing pushed. Kernel unchanged.
References changed: the 10 GlyphSdf hashes the owner accepted (R892.22).

## STATUS

| Order item | State |
|---|---|
| 1. R892.20 dark pixels | **Done.** Replayed on the CPU; three real sub-pixel features, no defect, nothing fixed (section 1). |
| 2. Rest of the review set | **Done, accepted by the owner (R892.22).** 10 GlyphSdf references re-recorded (sections 2, 3). |
| 3. R896 rebase | **Done.** Branch `lane-glyph6` on slice 2a `1823dad07`; full build, affected GPU set 108 of 108, captures byte-identical to before the rebase (section 3). |
| 4. Baked image in the shared cache | **Done.** Manifest with content key from the build step; image loaded from the cache by key, imported on a miss (section 4). |
| 5. R892.21 glyph LOD | **Not built, by instruction.** Two rulings are needed first; options and my recommendation in section 5. |
| 6. Path-distance bake note | **Done** (design note, section 6). The `f4b200e00` subtree-skip witness was **not started**. |

## SHAs

| Repo | Branch | Commit | Content |
|---|---|---|---|
| Kernel | `lane-glyph5` (on `cb9a4f5b7`) | `c36e2bc82`, `ef88ddac8`, `ff5e3c403` | unchanged from fable-glyph5: GlyphBake library, CLI, tests |
| VIXEN | `lane-glyph6` (on `1823dad07`, R896 slice 2a) | `d3a37617a`, `8a4e23525`, `aaeacc700`, `e4e0650b6`, `6e488112a` | fable-glyph5's baked walk, rebased (`RecipeGlyphBindings.glsl` binds `VXB_GlyphOutlineFlatBuffer`) |
| VIXEN | `lane-glyph6` | `c73a03f30`, `1b2c9b977` | fable-glyph5's report commits, rebased |
| VIXEN | `lane-glyph6` | `536138e63`, `31c1bea35`, `232d61138` | CPU replay of a capture pixel (test side, read-only registry accessor) |
| VIXEN | `lane-glyph6` | `2c0a6d285` | glyph image from the shared cache by content key |
| VIXEN | `lane-glyph6` | `4d718b810` | 10 GlyphSdf references (R892.20, R892.22) |
| VIXEN | `lane-glyph6` | this report's commit | `reports/fable-glyph6.md` |

`lane-glyph5` (tip `71d86a080`) holds the same work before the rebase and is superseded; land
from `lane-glyph6`. The rebase went to a new branch because `vixen-native.sh` only fast-forwards
the native tree. Worktree (ws): `~/projects/VBVS--VIXEN/.claude-worktrees/glyph5`, now on
`lane-glyph6`. Native tree `C:\vixen-native\glyph1` is built from `lane-glyph6` `2c0a6d285`
(`4d718b810` and the report change no built file).

## 1. The dark pixels of latin-embossed (R892.20): explained, no defect, nothing fixed

**Method.** `Recipe/RecipeMarchReplay.h` is a CPU mirror of `traceUberRecipeBodyImpl`, the entry-face
normal rule and the occupancy lookup of `SdfRecipes.glsl`. `VIXEN_LOOKDEV_REPLAY_PIXELS="x:y+x:y"` on a
look-dev capture replays the listed pixels after the frame, in the capture process: the camera march
and each analytic shadow march run against the application's own registry entries (bounds, occupancy
grid, packed glyph rows: the words the GPU walks), from the camera and the lights of the frame. It
also scans the field along each shadow ray at 2.5e-4 recipe units, independent of the march, and
with `VIXEN_LOOKDEV_REPLAY_SUPERSAMPLE` it draws the 13-pixel window at 16 x 16 rays per pixel.

**The replay is the capture's ray.** Over the six 9 x 9 windows of `replay3.replay.txt` (486
pixels): hit distance within 3.1e-5 world units of the HitRecord, normal dot at least 0.999998, and
the same light-0 visibility answer on 486 of 486.

| Pixel (new capture) | HitRecord | Replay finding | Reference |
|---|---|---|---|
| (1162, 483), level 95 | plaque, normal (0,0,1), light 0 occluded | Real cast shadow. The shadow ray is inside the solid from t = 0.4365 to 0.4563 (0.020 units, deepest -0.0013): it clips the rounded corner at the end of the E's top arm. The plaque point is 0.15 units beside that end wall; the camera sees it just past the wall's silhouette because the sun's direction in the sign plane (0.37, -0.93) and the camera's line of sight to that edge (0.366, -0.93) almost coincide. The shadow of the corner therefore lies behind the wall and only a sliver about 1/8 px wide shows beside the silhouette, along its whole length. | Same edge, same sliver: (1160, 477) and (1165, 491). On the new binary those two pixels miss the corner by 0.0018 and 0.0012 units. |
| (1185, 652), level 96 | foot of the N's left wall (0.0023 above the plaque), normal (-1,0,0), light 0 occluded | Real cast shadow. After 4.455 units the ray is inside the solid for 0.021 units, 0.0048 below the top rim of the E. It is the tip of the E's shadow where it reaches the N's wall: a wedge under 1/4 px wide that ends just below the pixel. | None at that place: the tip moved with the outline. |
| (1184, 680), level 106 | wall, normal (-0.964, -0.267, 0), every light visible | Not shadow. The camera ray lands on the rounded vertical corner at the bottom of the N's stem (180 march steps, a grazing hit). The normal is the field's gradient there (length 1.000), turned 15 degrees from the wall's, so NdotL is 0.10 where the wall has 0.35. | Same corner at (1185, 683), level 111. |

**Against the three classes of the brief.**
- *Hit-normal rule at a convex edge (R892.16):* no. Each normal equals the analytic gradient at the
  hit and has unit slope; the entry-face retreat did not fire on any of the pixels.
- *A wall thinner than the any-hit step, missed or invented by the march:* no. The dense scan
  agrees with the march on every ray: one stretch inside where the march certifies a hit, none
  where it reports the light visible. No ray started inside its own surface.
- *Defect of the baked data:* no (fable-glyph5 showed the same pixels with an infinite band).

They are real features narrower than a pixel, sampled by the one ray per pixel of a native capture.
Count 2 -> 3 because where a pixel centre lands on a 1/8 px line changes when the outline moves by a
fraction of a pixel. More samples per pixel would average them; nothing in the walk would.

Sheet: `fable-glyph6-evidence/dark-pixels/review-dark-pixels.png` (reference | new | replay at 256
rays per pixel). Evidence: `replay3.replay.txt` (all six pixels with march samples and scans),
`dark-pixels/*.pgm`, `dark-pixels/glyphsdf-latin-embossed.replay.txt`.

## 2. The rest of the review set: accepted by the owner (R892.22)

Affected native GPU CTest (`suite-affected.ps1`, shared GPU job) on VIXEN lane-glyph5 `71d86a080`:
**108 of 108 passed** (3 disabled, 10 skipped by their own guards; `suite-3.log`, `suite-3.ctest.log`).
Each GlyphSdf capture against opus-rt7's regold PNG of `253de919f` (`review/sheet2.py`, threshold
8 levels; sheets `review/review-<scene>.png`):

| Scene | px > 8 levels | > 32 | max | byte-equal |
|---|--:|--:|--:|---|
| latin-carved | 297 | 123 | 131 | no |
| latin-embossed | 291 | 19 | 61 | no |
| qps-rtl-carved | 284 | 126 | 101 | no |
| qps-rtl-embossed | 317 | 18 | 61 | no |
| rtl-proof-pair | 148 | 56 | 121 | no |
| rtl-proof-submit-guard | 148 | 56 | 121 | no |
| analytic-vs-voxel-128 | 539 | 94 | 158 | no |
| analytic-vs-voxel-128-yaw30 | 795 | 147 | 159 | no |
| analytic-vs-voxel-256 | 175 | 45 | 127 | no |
| analytic-vs-voxel-256-yaw30 | 177 | 84 | 158 | no |
| 10 non-text scenes (5 isolation, 4 sdf-filter, brickmap) | 0 | 0 | 0 | yes |
| look-dev 16 | 0 | 0 | 0 | yes (16 of 16) |

- What moves is glyph edges and their shadow edges by about a pixel. I looked at qps-rtl-carved,
  qps-rtl-embossed and analytic-vs-voxel-128: no new shape and no missing part.
- In the analytic-vs-voxel pairs both halves move: the voxel half is baked on the CPU from the same
  baked rows. The 128 pairs move most because a one-pixel outline shift lands on coarser voxels.
- No look-dev scene uses glyphs: all 16 are byte-identical to the references.
- Captures of unchanged content were byte-identical here (the 10 non-text scenes and look-dev 16
  against the references; all 20 GlyphSdf captures again after the rebase, section 3). I did not
  see fable-glyph5's "about 2 M pixels differ by a few levels on a rerun" and did not look for
  its cause.

**R892.22 (owner, 11-17): the full set is accepted, with the re-golden of the 10 changed GlyphSdf
references.** Recorded in section 3.

## 3. R896 rebase and the recorded references

- `git rebase --onto 1823dad07 253de919f` of lane-glyph5 as `lane-glyph6`. One conflict,
  `VIXEN/shaders/RecipeGlyphBindings.glsl`: the packed glyph buffer keeps my layout comment and
  binds `VXB_GlyphOutlineFlatBuffer`. No literal binding remains in a migrated program; the only
  literals my commits add are in the standalone compute program of
  `test_recipe_glsl_numerical_parity.cpp`, which declares its own four buffers and is not a
  manifest program.
- Full native build of `2c0a6d285`: exit 0, every `*_check` and the SDI drift gate included
  (`native-build-g6-1.log`).
- Affected native GPU CTest on it: **108 of 108** (`suite-g6-1.log`, `suite-g6-1.ctest.log`),
  including `RecipeGlslNumericalParityTest.BakedGlyphRowsMatchTheCpuFace` (GPU against the CPU
  face).
- CPU tests on it (`cpu6.log`): `test_glyph_bake_stream` 17, `test_recipe_registry` 18,
  `test_uber_shader_splice` 16, `test_recipe_eval_parity` 105, `test_text_shaping` 11, all pass.
- **Hash check:** the 20 GlyphSdf captures and the look-dev 16 of the rebased tree are
  byte-identical to the captures of `71d86a080` the owner reviewed (`glyphsdf-g6.sha256` equals
  `glyphsdf-suite3.sha256`).
- **References (`4d718b810`):** `reports/glyphsdf-visual/reference-hashes.sha256` (10 of 20 lines
  change: the four text variants, the RTL pair and its submit-guard run, the four
  analytic-vs-voxel frames) and `reference-provenance.json` (source commit `2c0a6d285`, rulings
  R892.20 and R892.22 added). PNGs: `fable-glyph6-evidence/regold/glyphsdf/` on ws, verified
  against the hash list.

## 4. The glyph image in the shared cache (`2c0a6d285`; `71d86a080` before the rebase)

- **Build step.** `glyph-bake/manifest.tsv` in the build tree: one line per bake with face, band,
  content key, image file, pack file. `cmake/VixenGlyphBakeManifest.cmake` reads the key from the
  header of the image the kernel tool wrote (font bytes + bake parameters), so the manifest names
  the key of the file on disk. No kernel change.
- **Runtime.** `Recipe/GlyphBakeCache.h`: `LoadGlyphBakeFace(face, manifest, cacheDirectory)`
  takes each image from `<SharedDeviceCacheDirectory>/glyph-bake/<key>.glyphimage`. On a miss it
  copies the import step's image into the cache (temporary file, then rename) and loads it. A
  cached file that does not load or holds another key is replaced. An import image of another
  key, or none, fails with the reason and the fix (`build target vixen_glyph_bake`). An
  unwritable cache directory is an error, not a silent fallback.
- **Why import at load and not only at build:** the build cannot know the run's cache directory.
  Captures and CTest runs set `VIXEN_SHARED_CACHE_DIR` per run, so a build-time copy into the
  default cache would miss on every isolated run. The import is a file copy; VIXEN still has no
  Brotli decoder and the compressed `.glyphbake` stays the pack file.
- **Consumers.** The look-dev glyph scenes and the recipe preview load through it; one compile
  definition (`TEXT_GLYPH_BAKE_MANIFEST`) replaces the two per-face path lists, and the old
  joined-path `LoadGlyphBakeFace` and `vixen_glyph_bake_images` are removed. The low-level reader
  tests still open the build images directly.
- **Tests** (5 new in `test_glyph_bake_stream`, CPU): the manifest's keys equal the images' keys
  and the pack files exist; a miss imports and leaves one file in the cache, and a hit does not
  read the import file; a truncated cached file and a sound image of another font under the key
  are both replaced; an import image of another key and a missing one are refused; a face loads
  every band and an unknown face or manifest fails by name.
- **Not done here:** the undertow pack bake (the pack side of R892.19) still waits for
  `pack_asset`; this slice only moves VIXEN's own two fonts.

## 5. R892.21 glyph LOD: not built; what I found, and two questions that block it

Read, not changed. Facts below are from the code at kernel `ff5e3c403` and VIXEN `71d86a080`.

**Band 2 (MSDF from about 16 px down).**
- `SelectWorldTextRenderBand` (`Text/GlyphOutline.h:38`) is a pure function with no caller outside
  `test_text_shaping.cpp`.
- The MSDF atlas is read by the UI only (`Text/src/shaders/text.frag`,
  `RenderGraph/src/Ui/shaders/ui.frag`). Nothing in the recipe march samples it, and a glyph row
  carries no atlas rectangle.
- So "wire it" is a new evaluation of the GlyphOutline row inside the march and the shadow wave: a
  sampled image in both passes, the median of three channels scaled to row units, selected per
  row. It needs (a) a descriptor binding in the march domain, which under R896 must come from the
  generated allocator, so it sits on opus-bind1's slice 2a (order item 3, not yet received);
  (b) a CPU face that samples the same texels (R619.1), or the CPU evaluates a different shape
  from the GPU below 16 px; (c) where the band is chosen: per instance on the CPU (the existing
  `lodLadder` / `SelectLodBand` path in `MultiBodyRenderer.cpp`, stateless per frame as R860.2
  asks) or per sample in the shader from the ray footprint.
- **Question 1:** per instance through the recipe's LOD ladder (one more ladder band, a recipe
  variant whose glyph rows are MSDF rows), or per sample in the shader? The ladder is the path
  R860 already uses and keeps the shader free of a per-sample branch; I recommend it.

**Band 3 (R860 shell without the glyph subtree, colour folded).**
- The shell deriver (`RecipeAnalysisServiceText.cs`, `EnumerateRewrites`) already has the rewrite
  that drops one operand of a hard union (`RuleDropUnionOperand`). Its error is an estimate from
  the bounding spheres of the two operands, marked not analytic, so the sampled certificate must
  confirm it. The sampled certificate needs both tapes to be Lipschitz-certified with occupancy
  coverage; a tape with a GlyphOutline row is not (the analysis service has no glyph rows), so a
  glyph subtree is never dropped today. A carved glyph is a Subtract, for which there is no drop
  rule at all.
- The existing `SdfRecipeRelaxation` members describe combining operations (2 inputs to 1: a
  smooth form to its hard form, or keep the first input) with a bound of scale x |argument| on an
  instruction-data argument. GlyphOutline is a leaf (0 value inputs), and its only instruction
  datum is the row index. `ReadRelaxation` rejects that shape
  (`RecipeLoweringModel.cs:905`: value pop must be 2, or at least 2).
- What bounds the drop geometrically: on the full surface, the dropped glyph's top stands the
  emboss depth above the kept plaque (carved: the cut depth below it); on the shell surface, the
  full field is at most half a stroke width inside the glyph. The certificate is two-sided
  (R860.6), so the bound is the emboss or cut depth; the stroke width only bounds one side. The
  emboss depth is not a property of the glyph row: it comes from the Box the glyph is intersected
  with and the Transform above it. The stroke width is a property of the baked glyph (its largest
  inside distance; the bake has it per glyph as the far-cell bound).
- **Question 2:** which declaration does the owner's "relaxation bound on the glyph-outline
  SdfCoreKernel" mean? Two shapes fit "no new attribute class":
  - **A (recommended).** A new `SdfRecipeRelaxation` member for a leaf whose solid may vanish
    (say `Vanish`), with `RecipeRelaxationArgument` naming a new instruction-data float on
    GlyphOutline: the row's largest inside distance in row units, written at registration from
    the bake (as the band need is). The deriver gains one rule: a subtree that is
    Intersect(vanishing leaf, bounded primitive) under a hard union or subtract may be dropped,
    with the analytic error = the depth of that primitive's extent outside (union) or inside
    (subtract) the kept operand's bound, and the leaf's inside distance as the bound of the other
    side. No glyph data in the analysis service; the certificate stays rule-based.
  - **B.** Give the analysis service the baked rows so the sampled certificate can run on glyph
    tapes. Larger: a service ABI change, and sampling 2D glyph fields in the certificate.
  Either way the colour fold needs the glyph's ink coverage of its box (area fraction), which the
  bake can store per glyph; R860.2's fold today has no leaf that vanishes inside a kept operand.

**Witness plan once built** (unchanged from the order): band chosen per projected glyph height
on a distance sweep of the RTL pair; captures on both sides of each switch (sheet only if a
switch is visible, R860.2); frame time of a far-text scene (the load scenario's crowd, R860.4,
with a sign recipe) against the walk-only tree.

## 6. Design note: bake the path distance (proposal, nothing built)

**What runs today.** `SdfCore_PathDistance` (kernel `SdfCoreKernels.cs:101`; generated
`SdfCoreKernels.g.glsl:439`) is evaluated per march sample and per gradient tap:

- nearest point: for each of up to 256 segments, 5 seeds x 8 Newton iterations, each evaluating
  the point, the tangent and the second derivative of the curve: up to 10,240 curve evaluations;
- sign (2D closed paths, from line 594): every segment is cut into 16 equal chords for the winding
  count: up to 4,096 chord tests.

The loops run to `PathSegmentCapacity`, with the count as a mask, so a 3-segment path walks 256
slots. It is the walk the glyph outline had before the bake (R892 audit: 96 to 99% of the RTL frame).

**Proposal: one bake, two consumers.** The glyph bake already is a bake of closed 2D paths. A path
is the same data with three differences, each a parameter of the existing layout, not a new format:

| | Glyph (built) | Path (proposed) |
|---|---|---|
| Source | TrueType `glyf` outline | the `PathInstance` row (line, quadratic, cubic segments) |
| Key | font bytes + bake parameters | row bytes + bake parameters |
| Curves | quadratics, cut by curvature into 1 to 16 chords (R892.13) | the same cut, plus cubics by the same error rule |
| Fill | non-zero union, overlap removed | the row's fill rule; an open path keeps no sign |
| Dimension | 2D | 2D as built; 3D paths (tubes) need a 3D grid or stay on the walk |
| Result | exact distance in the band, bound + sign in far cells | the same |

So the work is: (1) the kernel bake library takes a segment list beside a font (the chord cutter,
the union, the cell lists and the far bound are unchanged); (2) `PathDistance` rows carry a baked
row in the packed stream exactly as glyph rows do, and the generated walk (SPT `7205585d3323`: one
kernel callable for C#, C++ and GLSL) serves both; (3) registration derives the band a path needs
with the rule glyph rows use (`GlyphBakeBand.h`: Round, Onion, smooth union widths).

**Where the bake happens.** A path row is recipe content, not a font: it is baked when the recipe
is registered or the pack is built (R892.14's "nothing fitted per scene or per frame" holds: once
per content hash, cached by key in the shared cache next to the glyph images). A live edit of a
path (R864, recipe editing as fast as possible) re-bakes that one row: Lato's 464 glyphs take
0.45 s, about 1 ms per outline, so an edit stays interactive; the unbaked walk remains the
fallback for the frame in between only if that 1 ms proves visible.

**Limits and open questions (for the owner or the controller).**
1. 3D paths (`Dimension != 2`): a tube around a space curve has no 2D cell grid. Options: keep the
   Newton walk for them (they are rare today), or bake chords into a 3D cell grid. I would measure
   a scene that uses one before choosing.
2. The band: path recipes often use a path as a thick stroke (Onion or Round by a large radius).
   The band is then large and the exact region covers most cells; the bake still removes the
   Newton iterations and the 256-slot loops, but the chord lists per cell grow. Needs one measured
   stroke recipe.
3. The US 9,020,272 constraint (R892.13) is unchanged: chords, never arcs, in the cells.
4. Parity (R619.1): the CPU face must walk the same baked rows, as it now does for glyphs.

**Expected gain.** Not measured. The glyph case went from about 43 chords per sample to 3.4 to 3.7
and the march from 34.6 to 8.0 ms; a path costs more per segment today than a glyph did (Newton
iterations instead of fixed chords), so the ratio should be at least the glyph's, on scenes where
paths dominate. No look-dev scene uses `PathDistance` today (only
`test_editor_document_model_preview.cpp` and the registry reference it), so the first step is a
scene that does.

## 7. Not done, and what each needs

- **R892.21 glyph LOD:** the two rulings of section 5, then the build.
- **Kernel `f4b200e00` subtree-skip witness:** not started. Merge kernel lane-glyph3 onto a scratch
  branch on `ff5e3c403`, move VIXEN's pin, rebuild, compare GlyphSdf 20 + look-dev 16 hashes with
  `fable-glyph6-evidence/regold` and `suite3-captures/lookdev`.
- **Analytic extruded-glyph face A/B (R892.13.4):** not started.
- **Frame gap to R892.1:** the controller gave it to cx-framegap1-r2 (own tree, `779f17858`).
- **Reference pixels on the reference binary:** I replayed the reference's two pixel positions
  on the new binary only (both miss the corner there by under 0.002 units). The reference binary
  was not replayed; the reference image shows the same sliver on the same edge.

## SUCCESSOR BRIEF

Bases: kernel `lane-glyph5` `ff5e3c403`; VIXEN `lane-glyph6` tip (this report's commit). Native
tree `C:\vixen-native\glyph1` is this lane's alone (controller, 11-17) and is built from
`lane-glyph6`. Harness in `fable-glyph6-evidence/`: `nbuild6.sh N [TARGET]` (queue build of
lane-glyph6), `suite6.sh N` (affected GPU CTest; start with `setsid nohup`), `cap.sh JOB
shared|bench RUN...` (the last field of a RUN is extra environment, `KEY=VALUE[,KEY=VALUE]`),
`cpu6.runs.sh`,
`review/sheet2.py REF_DIR NEW_DIR OUT_DIR`.

Pixel replay: `cap.sh x shared "x:glyph:1920:1080:2:stock:latin-embossed:0:VIXEN_LOOKDEV_REPLAY_PIXELS=1162:483+1185:652,VIXEN_LOOKDEV_REPLAY_SUPERSAMPLE=1"`
writes `<capture>.replay.txt` and one `.pgm` per pixel beside the PNG. It handles registry
recipes on axis-aligned, uniformly scaled instances, light 0 in the neighbourhood table, and
uses ShadowConfigNode's default bias (0.01) and the 0.05 secondary cone.

In order:
1. Land: the controller has the stack (LANDABLE NOW).
2. R892.21 after the two rulings (section 5).
3. `f4b200e00` witness, then the analytic extruded-glyph face A/B.
4. Path-distance bake (section 6) if the owner wants it: first a scene that uses `PathDistance`.

Traps I paid for:
- `vixen-native.sh --lane glyph1` syncs the native tree to the branch's committed tip only: a
  change must be committed to be built, and a rebased branch needs a new branch name.
- A waiter written as `until ! pgrep -f PATTERN` matches its own command line and never ends.
- `capture.ps1 -ExtraEnv` splits on commas, so a value cannot contain one.
- A two-frame capture has not written LightingConfig ring slot 0.

## LANDABLE NOW

| Repo | Commit | State |
|---|---|---|
| Kernel `lane-glyph5` | `c36e2bc82`, `ef88ddac8`, `ff5e3c403` | GlyphBake.Tests 60 of 60 (fable-glyph5); standalone tooling. |
| VIXEN `lane-glyph6` | `1823dad07..` tip (12 commits + this report) | Owner accepted the image change (R892.20, R892.22); on R896 slice 2a; full native build green; affected GPU set 108 of 108; references recorded. It must land after, or with, opus-bind1's `1823dad07` and with the kernel pin `ff5e3c403`. |

`PREFLIGHT lane-preflight-248f655d626c 248f655d626ca08f556385fc70f7f1dbf02b740a: green all-gates`
(kernel `ff5e3c403`, undertow `248f655d6` = origin/main-wave, launcher `e994efb6a` = origin/main;
report `reports/lane-preflight-cli/20261117T140910Z-1225287/report.md`; `preflight.log`).

## REDS

None found on the bases. Two of my full builds failed at configure with `ninja: error: failed
recompaction: Permission denied`: another lane's build was running in the same native tree
(CONSOLIDATION ISSUES). Not a code red.

## CANONICAL-SHAPE CHECKLIST

No kernel or undertow line changed. VIXEN is outside the checklist. No Python added to a repo
(`sheet2.py` and the edit scripts are evidence-folder or scratch files). No binding number chosen
by hand (R896). No reference changed without an owner ruling.

## CONSOLIDATION ISSUES

- **Two lanes in one native tree.** cx-framegap1 was briefed to build in `C:\vixen-native\glyph1`
  while I synced it to every new tip. Their benchmark binary was being built from my newest
  commit, and their ninja made my configure fail twice. The controller moved them to their own
  tree. A native tree should have one owner recorded where `vixen-native.sh` can refuse a second.
- **A native build needs a commit.** The tree follows the committed tip, so each try of a
  diagnostic is a commit; the replay tool took three. A dirty-tree option for measurement builds
  would have saved two commits and two sync cycles.
- **Configure takes 1 to 9 minutes** on a warm tree when it waits for the machine-wide configure
  lock; the full build after the rebase waited 14 minutes in the queue before it started.
- **GlyphSdf references are hashes that no test compares.** The affected GPU CTest passed 108 of
  108 with 10 captures that differ from the recorded hashes. The comparison lives in lane
  scripts; a CTest that checked `reference-hashes.sha256` would make a re-golden visible as a red.
- **`ExportLightingReadback` cannot run on a glyph scene:** it throws on any recipe that is not a
  look-dev material recipe. I did not use it; the replay reads the snapshot itself.
- I wrote two transient files under `/tmp` on ws (an edit script and its input, removed) before
  switching to the evidence folder.

## SPT DISPOSITION

No `spt` write command run.

| Id | Disposition |
|---|---|
| `7205585d3323` generated walk with the grid | PARTIAL, unchanged: the walk is still written three times (C#, C++, GLSL). Section 6 proposes the path walk join it. |
| `5e02066c9b3c` cold compile check in witnesses | NOT TOUCHED. |

## EVIDENCE (`~/codeman-cases/undertow/reports/fable-glyph6-evidence/` on ws)

`dark-pixels/` (sheet, three supersampled windows, replay text, capture), `replay1..4.log`,
`replay3.replay.txt`, `native-build-1..3.log`, `native-build-all-1..3.log` (1 and 2 are the
failed configures), `suite-3.log`, `suite-3.ctest.log`, `suite3-captures/`, `cache1.log`,
`native-build-g6-1.log`, `suite-g6-1.log`, `suite-g6-1.ctest.log`, `cpu6.log`,
`glyphsdf-g6.sha256`, `glyphsdf-suite3.sha256`, `regold/` (the 20 reference PNGs and their hash
list), `review/` (10 sheets, `sheet2.py`, `table-glyphsdf.md`), `preflight.log`, harness scripts.
