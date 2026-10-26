// The TAA provider's history feedback, authored once (R619.1). The GPU-system emitter inlines it
// into TemporalAntiAliasingSystem.comp; `--callable-cpp` generates the C++ face the resolve
// tests read, so the CPU model and the shader cannot drift. Exemplar:
// libraries/SVO/schemas/MaterialColorCallables.cs (one callable, both backends).
using Yeroket.Util.KernelFramework;

namespace Vixen.AntiAliasing
{
    public static class TemporalResolveCallables
    {
        // The share of the history weight a half-texel reprojection gives up. Every frame of a
        // camera or object move resamples the history at a sub-texel offset, and each resample
        // low-passes it a little; through the feedback loop that compounds into a soft edge, and
        // in display space into a bright halo on the dark side of every moving silhouette (the
        // tone curve lifts a small linear leak of the bright side). Integer offsets, which
        // include a still camera, resample exactly and keep the full weight.
        [KernelCallable]
        public static float ResampleBlur(float offsetX, float offsetY)
        {
            float blurX = 4.0f * offsetX * (1.0f - offsetX);
            float blurY = 4.0f * offsetY * (1.0f - offsetY);
            return blurX > blurY ? blurX : blurY;
        }

        // History weight for this pixel: the 1/N ramp of the valid-history count up to the
        // provider's ceiling, reduced by 0.3 of the reprojection's resample blur. offsetX and
        // offsetY are the reprojected point's in-texel fractions (0..1). 0.3 measured on the
        // CPU model of the resolve (moving silhouette, 1.3-3.3 px/frame): moving-edge error
        // -25..30%, still frames unchanged; 0.4 gained nothing further.
        [KernelCallable]
        public static float HistoryFeedback(float historyWeight, float count, float offsetX, float offsetY)
        {
            float ramp = count / (count + 1.0f);
            float ceiling = historyWeight < ramp ? historyWeight : ramp;
            return ceiling * (1.0f - 0.3f * ResampleBlur(offsetX, offsetY));
        }
    }
}
