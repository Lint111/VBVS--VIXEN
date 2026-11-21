// The recipe LOD band choice (R860, RL0), authored once (R619.1). The GPU-system emitter inlines it
// into TileBinPrepareSystem.comp; `--callable-cpp` generates the C++ face that SVO's SelectLodBand
// calls, so the CPU ladder and the per-instance GPU selection pick the same band bit for bit.
// Exemplar: codegen/aa-callables/TemporalResolveCallables.cs (one callable, GPU system + C++ face).
using Yeroket.Util.KernelFramework;

namespace Vixen.Lod
{
    public static class LodBandCallables
    {
        // A ladder holds at most four bands; band b is active while q < maxQ(b), and the ladder's
        // last band is the first whose maxQ is +infinity, so a fourth threshold is never read. q is
        // the pixel footprint over the bound diameter (it grows with distance); NaN and negative q
        // select the finest band. The result is clamped to bandCount - 1 (bandCount >= 1). A table
        // that pads a shorter ladder to four rows by repeating its infinite last band therefore
        // picks the ladder's own last band, even for q = +infinity.
        [KernelCallable]
        public static uint SelectLodBand(float q, float maxQ0, float maxQ1, float maxQ2, uint bandCount)
        {
            float safeQ = q > 0.0f ? q : 0.0f;
            uint last = bandCount > 1u ? bandCount - 1u : 0u;
            if (last == 0u || safeQ < maxQ0 || maxQ0 > MaxFiniteQ) return 0u;
            if (last == 1u || safeQ < maxQ1 || maxQ1 > MaxFiniteQ) return 1u;
            if (last == 2u || safeQ < maxQ2 || maxQ2 > MaxFiniteQ) return 2u;
            return 3u;
        }

        // The largest finite float: only +infinity compares greater.
        public const float MaxFiniteQ = 3.4028234e38f;
    }
}
