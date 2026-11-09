# cx-recipeclamp3

## STATUS

PARTIAL. The byte-exact bestT clamp and conservative smooth-blend bound proof are complete and committed. The owner-required chunked procedural BLAS/TLAS candidate path is not complete in this checkpoint.

Base: `52096d567507be239855d84fa1f2f39b0ab3237f`.

## MEASURED

- Fresh WSL configure: passed through `with-test-lock.sh` (`cx-recipeclamp3:configure-baseline`).
- Fresh baseline build of `test_recipe_bounds`, `test_uber_shader_splice`, and `VIXEN`: passed.
- Changed-tree build of the same targets: passed (`cx-recipeclamp3:build-8a-bounds`).
- Focused tests: 18 bounds tests passed; 11 shader-splice tests passed, with 1 disabled test (`cx-recipeclamp3:test-8a-bounds`).
- The 8a clamp is applied to both primary and shadow/any-hit uses of `traceUberRecipeBody`. For the legacy effectively-infinite endpoint, `min(tFar,tMaxWorld)` selects the original `tFar` value and preserves the existing march sequence.
- Smooth unions, smooth subtract/intersect, smooth-max, and cubic blend variants now widen the derived sphere by `abs(k)`. A field-sampling test checks points just outside the resulting bound.
- Runtime-parameter recipes are logged as `runtimeParameters=range-bound-required` and remain on authored/default bounds because no parameter range declaration exists yet.
- The 13 look-dev procedural bodies remain authored `boundRadius=9` while the opt-in flag is off.
- Chunk count, BLAS bytes, recipes marched, steps/ray, and 500/720/1080 ray-march timings: not measured; procedural RT culling is not yet wired.

## IMAGES

No native Windows images were captured. The flag remains off by default, so no golden claim is made for the new image class. Native `C:\vixen-native` acceptance is still required after the chunked RT path and owner re-golden.

## LANDABLE NOW

- `SdfRecipes.glsl`: adds the effective `tMaxWorld` endpoint and updates the R716 exhaustion meaning.
- `TraceWorld.glsl`: passes the current primary `bestT` and shadow `tmax` into the march.
- `RecipeBounds.h`: smooth blend extents are conservatively widened.
- `VulkanGraphApplication.cpp`: adds opt-in `VIXEN_RECIPE_DERIVED_BOUNDS=1` override for authored bounds on non-runtime-parameter recipes; replacement registration follows the same rule.
- `test_recipe_bounds.cpp`: samples smooth-union fields outside the derived bound.

## REDS

- No product red in the focused WSL witnesses.
- The shared test queue was temporarily head-blocked by another lane, then admitted and completed successfully.
- Native GPU acceptance, image inspection, performance counters, TLAS counts/memory, and default-scene regression were not run in this WSL slice.

## STOPs

- The current `EnsureRtQueryTlasBuilt` still skips `providerKind != 0`, and `RayQueryTraversal.glsl` still consumes only stored `ShellProxyAabb` records. The required surface-shell chunk boxes, coarse-parent fallback, procedural BLAS/TLAS candidates, software bound fallback, and candidate-order tests remain for the next slice.
- Demand-driven chunk residency/drop-after-N-frames is intentionally deferred as allowed by the amendment, but the minimum chunked hardware path is also not yet landed.
- `LookdevSceneDefinition.h` authored `boundRadius` was not deleted because the owner ruling requires the native bundled re-golden before flipping the default.

## CONSOLIDATION ISSUES (non-facade deltas this lane needed)

None.

## SPT DISPOSITION

No proposal filed.
