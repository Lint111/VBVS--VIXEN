using Yeroket.Util.KernelFramework;

// Canonical ReservoirRecord — one source for C++ (Vixen::Gpu) + GLSL.
// Sampled Lighting Inc3 M4: the per-pixel RIS/ReSTIR reservoir record, ping-ponged
// across frames (CURRENT + PREVIOUS buffers, sized to render extent — one record per
// pixel, mirrors HitRecord's own flat row-major pixelCoords addressing).
//
// Fields (per the plan's Task 4, and the standard weighted-reservoir-sampling shape):
//   y            -- index of the CHOSEN light-tree node (LightTreeBuffer.nodes[y]),
//                    the reservoir's currently-selected sample. 0xFFFFFFFF = empty/
//                    invalid reservoir (no sample chosen yet — frame-1 / disocclusion).
//   weightSum     -- W_sum, running sum of RIS candidate weights (Sum(w_i)) seen so far
//                    -- the WRS running-sum accumulator, NOT the final unbiased weight.
//   sampleCount   -- M, number of samples this reservoir has stochastically seen (RIS
//                    candidates this frame + temporally-carried M from reuse, capped by
//                    ReservoirConfig.temporalCap).
//   targetPdf     -- p_hat(y), the target-function value (unshadowed contribution
//                    estimate) evaluated at the CURRENTLY CHOSEN sample y -- needed to
//                    reconstruct the unbiased contribution weight W = (1/targetPdf) *
//                    (weightSum/sampleCount) without re-evaluating p_hat(y) every read.
//
// std430 layout: y@0 (uint), weightSum@4, sampleCount@8 (uint), targetPdf@12 -- 16 B,
// naturally aligned, no padding.
[GpuStruct]
public struct ReservoirRecord
{
    public uint y;
    public float weightSum;
    public uint sampleCount;
    public float targetPdf;
}
