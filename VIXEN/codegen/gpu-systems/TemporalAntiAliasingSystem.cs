using Undertow.Modding.Schema;
using Undertow.Sim.Systems;
using Yeroket.Util.KernelFramework;

namespace Vixen.Codegen.Gpu;

// Texture coordinates for the R493 Sample(uv, lod) read; syntax only, like `math`.
public struct float2
{
    public float x, y;
    public float2(float x, float y) { this.x = x; this.y = y; }
}

// The anti-aliasing module's `taa` provider (R676, R677): temporal accumulation of jittered
// samples, resolved in linear HDR before the exposure meter (R676.1).
//
// - The current sample is this pixel's jittered scene radiance; the jitter sequence is the
//   engine's deterministic Halton(2,3) cycle (ReconstructionJitter.h).
// - History is last frame's resolve, reprojected through the engine motion vectors (camera and
//   instance transforms) with a bilinear Previous[] fetch.
// - The history is clamped to the current 3x3 neighbourhood's colour box (strict clamp,
//   R676.5), so a value the current frame does not support, such as a stochastic night-lighting
//   sample from an earlier frame, is not averaged in.
// - The blend weighs each side by 1 / (1 + luma) (Karis 2014, link-only prior art:
//   https://advances.realtimerendering.com/s2014/), so bright outliers do not dominate the edge.
// - Alpha carries the valid-history sample count: the weight ramps 1/N until it reaches the
//   declared history-weight ceiling, so a still camera converges like the 1/N accumulation the
//   design measured (8 frames ~ a 4-sample render).
[KernelSystem(Emit = KernelEmitTargets.Glsl)]
public static class TemporalAntiAliasingSystem
{
    [GaiaQuery]
    public static QuerySpec Select() => Gaia.Query()
        .All<StateFrameTargetsRef, Rgba16f>(row => row.SceneRadianceHistoryOutput)
        .All<StateFrameTargetsRef, Rg32f>(row => row.MotionVectors)
        .All<StateFrameTargetsRef, Rgba16f>(row => row.ResolvedRadiance);

    [Update]
    public static void Run(
        in Texture2D<Rgba16f> sceneRadianceHistoryOutput,
        in Texture2D<Rg32f> motionVectors,
        ref Texture2D<Rgba16f> resolvedRadiance,
        in PrevCameraConfig[] reconstruction,
        int i)
    {
        int width = (int)reconstruction[0].inputWidth;
        int height = (int)reconstruction[0].inputHeight;
        float historyWeight = reconstruction[0].historyWeight;
        int x = i % width;
        int y = i / width;
        float du = 1.0f / (float)width;
        float dv = 1.0f / (float)height;
        float u = ((float)x + 0.5f) * du;
        float v = ((float)y + 0.5f) * dv;

        // The current sample and its 3x3 neighbourhood colour box (texel-centre reads at lod 0
        // return the stored texel exactly; the sampler clamps at the frame edge).
        Rgba16f centre = sceneRadianceHistoryOutput.Current[i];
        float minR = centre.x; float maxR = centre.x;
        float minG = centre.y; float maxG = centre.y;
        float minB = centre.z; float maxB = centre.z;
        Rgba16f tap = sceneRadianceHistoryOutput.Sample(new float2(u - du, v - dv), 0.0f);
        minR = math.min(minR, tap.x); maxR = math.max(maxR, tap.x);
        minG = math.min(minG, tap.y); maxG = math.max(maxG, tap.y);
        minB = math.min(minB, tap.z); maxB = math.max(maxB, tap.z);
        tap = sceneRadianceHistoryOutput.Sample(new float2(u, v - dv), 0.0f);
        minR = math.min(minR, tap.x); maxR = math.max(maxR, tap.x);
        minG = math.min(minG, tap.y); maxG = math.max(maxG, tap.y);
        minB = math.min(minB, tap.z); maxB = math.max(maxB, tap.z);
        tap = sceneRadianceHistoryOutput.Sample(new float2(u + du, v - dv), 0.0f);
        minR = math.min(minR, tap.x); maxR = math.max(maxR, tap.x);
        minG = math.min(minG, tap.y); maxG = math.max(maxG, tap.y);
        minB = math.min(minB, tap.z); maxB = math.max(maxB, tap.z);
        tap = sceneRadianceHistoryOutput.Sample(new float2(u - du, v), 0.0f);
        minR = math.min(minR, tap.x); maxR = math.max(maxR, tap.x);
        minG = math.min(minG, tap.y); maxG = math.max(maxG, tap.y);
        minB = math.min(minB, tap.z); maxB = math.max(maxB, tap.z);
        tap = sceneRadianceHistoryOutput.Sample(new float2(u + du, v), 0.0f);
        minR = math.min(minR, tap.x); maxR = math.max(maxR, tap.x);
        minG = math.min(minG, tap.y); maxG = math.max(maxG, tap.y);
        minB = math.min(minB, tap.z); maxB = math.max(maxB, tap.z);
        tap = sceneRadianceHistoryOutput.Sample(new float2(u - du, v + dv), 0.0f);
        minR = math.min(minR, tap.x); maxR = math.max(maxR, tap.x);
        minG = math.min(minG, tap.y); maxG = math.max(maxG, tap.y);
        minB = math.min(minB, tap.z); maxB = math.max(maxB, tap.z);
        tap = sceneRadianceHistoryOutput.Sample(new float2(u, v + dv), 0.0f);
        minR = math.min(minR, tap.x); maxR = math.max(maxR, tap.x);
        minG = math.min(minG, tap.y); maxG = math.max(maxG, tap.y);
        minB = math.min(minB, tap.z); maxB = math.max(maxB, tap.z);
        tap = sceneRadianceHistoryOutput.Sample(new float2(u + du, v + dv), 0.0f);
        minR = math.min(minR, tap.x); maxR = math.max(maxR, tap.x);
        minG = math.min(minG, tap.y); maxG = math.max(maxG, tap.y);
        minB = math.min(minB, tap.z); maxB = math.max(maxB, tap.z);

        // Reproject: the motion vector is previous UV minus current UV.
        Rg32f motion = motionVectors.Current[i];
        float previousU = u + motion.x;
        float previousV = v + motion.y;
        bool onScreen = previousU >= 0.0f && previousU <= 1.0f && previousV >= 0.0f && previousV <= 1.0f;
        float px = math.clamp(previousU * (float)width - 0.5f, 0.0f, (float)(width - 1));
        float py = math.clamp(previousV * (float)height - 0.5f, 0.0f, (float)(height - 1));
        // px and py are clamped non-negative, so the fraction is the bilinear weight.
        float ax = math.frac(px);
        float ay = math.frac(py);
        float fx = px - ax;
        float fy = py - ay;
        int x0 = (int)fx;
        int y0 = (int)fy;
        int x1 = (int)math.min(fx + 1.0f, (float)(width - 1));
        int y1 = (int)math.min(fy + 1.0f, (float)(height - 1));
        Rgba16f h00 = resolvedRadiance.Previous[x0 + y0 * width];
        Rgba16f h10 = resolvedRadiance.Previous[x1 + y0 * width];
        Rgba16f h01 = resolvedRadiance.Previous[x0 + y1 * width];
        Rgba16f h11 = resolvedRadiance.Previous[x1 + y1 * width];
        float w00 = (1.0f - ax) * (1.0f - ay);
        float w10 = ax * (1.0f - ay);
        float w01 = (1.0f - ax) * ay;
        float w11 = ax * ay;
        float historyR = h00.x * w00 + h10.x * w10 + h01.x * w01 + h11.x * w11;
        float historyG = h00.y * w00 + h10.y * w10 + h01.y * w01 + h11.y * w11;
        float historyB = h00.z * w00 + h10.z * w10 + h01.z * w01 + h11.z * w11;
        // The nearest tap's count: a bilinear count would blur the reset boundary.
        float count = math.select(math.select(h00.w, h10.w, ax > 0.5f), math.select(h01.w, h11.w, ax > 0.5f), ay > 0.5f);
        // A first frame, an off-screen or undefined history has no valid count (NaN fails both tests).
        bool historyValid = onScreen && historyWeight > 0.0f && count >= 1.0f && count <= 1024.0f;

        historyR = math.clamp(historyR, minR, maxR);
        historyG = math.clamp(historyG, minG, maxG);
        historyB = math.clamp(historyB, minB, maxB);

        float alpha = math.select(0.0f, math.min(historyWeight, count / (count + 1.0f)), historyValid);
        float centreWeight = (1.0f - alpha) / (1.0f + 0.2126f * centre.x + 0.7152f * centre.y + 0.0722f * centre.z);
        float historyLumaWeight = alpha / (1.0f + 0.2126f * historyR + 0.7152f * historyG + 0.0722f * historyB);
        float norm = 1.0f / (centreWeight + historyLumaWeight);
        float red = math.select(centre.x, (centre.x * centreWeight + historyR * historyLumaWeight) * norm, historyValid);
        float green = math.select(centre.y, (centre.y * centreWeight + historyG * historyLumaWeight) * norm, historyValid);
        float blue = math.select(centre.z, (centre.z * centreWeight + historyB * historyLumaWeight) * norm, historyValid);
        float nextCount = math.select(1.0f, math.min(count + 1.0f, 1024.0f), historyValid);

        resolvedRadiance.Current[i] = new Rgba16f(red, green, blue, nextCount);
    }
}
