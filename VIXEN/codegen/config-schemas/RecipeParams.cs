using Yeroket.Util.KernelFramework;

// Recipe-Load-Tier-Contract M2 (precision tier): the FIRST [GpuStruct] schema for a recipe
// render-param block (previously hand-authored in ShellOctreeGpu.h's BodyInstanceGpu::recipeParams
// / RecipeInstanceBucketing.comp's BodyInstance::recipeParams — recipeId/providerKind stay outside
// this struct, unaffected). Mirrors ShellOctreeGpu.h's own recipeParams comment:
// "params.xyz = (radius, displaceAmp, displaceFreq); 3 spare" — this schema models exactly those
// 3 meaningful floats (radius/displaceAmp/displaceFreq) as ONE Float3, leaving the 3 spare floats
// unmodeled (this milestone's prototype only needs the currently-meaningful lanes; the spare floats
// are not yet used by any recipe and adding them here would be speculative).
//
// Per-field precision-eligibility decision (direction doc's open question #1, resolved for this
// prototype): ALL THREE render-shape params (radius, displaceAmp, displaceFreq) are marked
// [PrecisionEligible] — they are geometric magnitudes, never used as an array index/lookup/enum
// discriminant, and half-float error on a shape radius/displacement is visually negligible at the
// far distances where the precision tier actually engages (same LOD-appropriate-degradation
// argument M1's gating tier already established for its own far-instance exclusion). There is no
// kind/index field on THIS struct to withhold — recipeId/providerKind live on the OUTER
// BodyInstanceGpu record, not here, so this schema has no "must stay full precision" field to
// demonstrate; the per-field (not blanket-struct) opt-in mechanism itself is what's being proven,
// and every field here happens to qualify.
[GpuStruct]
public struct RecipeParams
{
    [PrecisionEligible] public float radius;
    [PrecisionEligible] public float displaceAmp;
    [PrecisionEligible] public float displaceFreq;
}
