using Yeroket.Util.KernelFramework;

// Canonical EmitterTotals — one source for C++ (Vixen::Gpu) + GLSL.
// Count-agnostic lighting (R502, lightdesign §2.4): the per-frame aggregate the two-level
// emitter mixture needs. Written by the instance uploader (BodyOctreeSceneNode), read by
// sampleEmitter()/emitterTotalPower(). 16 B std430.
//
//   phiRecipe        -- sum of recipe-instance power (pi * luminance(radiance) * area), W.
//   phiSparse        -- sum of stored-instance sparse power (octree root lane * instance scale), W.
//   recipeEntryCount -- number of RecipeEmitterEntry rows (CDF length).
//   sparseEntryCount -- number of SparseEmitterEntry rows (CDF length).
[GpuStruct]
public struct EmitterTotals
{
    public float phiRecipe;
    public float phiSparse;
    public uint recipeEntryCount;
    public uint sparseEntryCount;
}
