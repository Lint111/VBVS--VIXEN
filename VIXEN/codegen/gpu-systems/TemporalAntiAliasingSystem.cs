using System;
using Undertow.Modding.Schema;
using Undertow.Sim.Systems;
using Vixen.Gpu;
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
//   instance transforms; the background moves with the camera's rotation) with a 4x4
//   Catmull-Rom Previous[] fetch. A bilinear fetch low-passes the history once per frame, so
//   under motion every resolved edge (silhouettes, contact lines, cast-shadow boundaries) washed
//   back towards a blur; the cubic keeps the accumulated sub-pixel coverage (link-only prior
//   art: https://advances.realtimerendering.com/s2016/ "Filmic SMAA", the history filter).
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
    // Keep intrinsic spellings local so callable reach does not emit their C# bodies.
#pragma warning disable CS8981
    private static class math
    {
        private static float abs(float value) => value < 0.0f ? -value : value;
        private static float min(float left, float right) => left < right ? left : right;
        private static float max(float left, float right) => left > right ? left : right;
        private static float frac(float value) => value - (float)Math.Floor(value);
        private static float clamp(float value, float minimum, float maximum)
            => value < minimum ? minimum : value > maximum ? maximum : value;
        private static float select(float whenFalse, float whenTrue, bool condition)
            => condition ? whenTrue : whenFalse;
    }
#pragma warning restore CS8981

    [GaiaQuery]
    public static QuerySpec Select() => Gaia.Query()
        .All<FrameTargetsRef, Rgba16f>(row => row.SceneRadianceHistoryOutput)
        .All<FrameTargetsRef, Rg32f>(row => row.MotionVectors)
        .All<FrameTargetsRef, R32f>(row => row.ViewDepth)
        .All<FrameTargetsRef, Rgba16f>(row => row.ResolvedRadiance);

    [Update]
    public static void Run(
        in Texture2D<Rgba16f> sceneRadianceHistoryOutput,
        in Texture2D<Rg32f> motionVectors,
        in Texture2D<R32f> viewDepth,
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

        // Reproject: the motion vector is previous UV minus current UV. It is the vector of the
        // nearest surface in the 3x3 neighbourhood, not this texel's own: a silhouette pixel
        // holds both the foreground and what lies behind it, and its one jittered sample lands
        // on either from frame to frame. Its own vector would fetch the far side's history
        // (the sky turns with the camera, a foreground object parallaxes against it) and the
        // accumulated edge coverage would be lost on every frame of a camera move; the nearest
        // surface's vector keeps the edge on the object that owns it.
        // Neighbour reads go through the sampled views (texel-centre, lod 0: exact texels; the
        // sampler clamps at the frame edge).
        float nearestU = u;
        float nearestV = v;
        float nearestDepth = math.abs(viewDepth.Current[i].x);
        for (int oy = -1; oy <= 1; oy++)
        {
            for (int ox = -1; ox <= 1; ox++)
            {
                float su = u + (float)ox * du;
                float sv = v + (float)oy * dv;
                float depth = math.abs(viewDepth.Sample(new float2(su, sv), 0.0f).x);
                if (depth < nearestDepth)
                {
                    nearestU = su;
                    nearestV = sv;
                    nearestDepth = depth;
                }
            }
        }
        Rg32f motion = motionVectors.Sample(new float2(nearestU, nearestV), 0.0f);
        float previousU = u + motion.x;
        float previousV = v + motion.y;
        bool onScreen = previousU >= 0.0f && previousU <= 1.0f && previousV >= 0.0f && previousV <= 1.0f;
        float px = math.clamp(previousU * (float)width - 0.5f, 0.0f, (float)(width - 1));
        float py = math.clamp(previousV * (float)height - 0.5f, 0.0f, (float)(height - 1));
        // px and py are clamped non-negative, so the fraction is the in-cell offset.
        float ax = math.frac(px);
        float ay = math.frac(py);
        int x0 = (int)(px - ax);
        int y0 = (int)(py - ay);
        // Catmull-Rom over the 4x4 texels around the reprojected point, edge-clamped. Each tap's
        // weight is the kernel at its distance d from the point: 1.5|d|^3 - 2.5|d|^2 + 1 inside
        // one texel, -0.5|d|^3 + 2.5|d|^2 - 4|d| + 2 out to two.
        float historyR = 0.0f;
        float historyG = 0.0f;
        float historyB = 0.0f;
        for (int ky = 0; ky < 4; ky++)
        {
            float dy = math.abs((float)(ky - 1) - ay);
            float wy = math.select(((-0.5f * dy + 2.5f) * dy - 4.0f) * dy + 2.0f,
                                   (1.5f * dy - 2.5f) * dy * dy + 1.0f, dy < 1.0f);
            int sy = (int)math.clamp((float)(y0 - 1 + ky), 0.0f, (float)(height - 1));
            for (int kx = 0; kx < 4; kx++)
            {
                float dx = math.abs((float)(kx - 1) - ax);
                float wx = math.select(((-0.5f * dx + 2.5f) * dx - 4.0f) * dx + 2.0f,
                                       (1.5f * dx - 2.5f) * dx * dx + 1.0f, dx < 1.0f);
                int sx = (int)math.clamp((float)(x0 - 1 + kx), 0.0f, (float)(width - 1));
                Rgba16f h = resolvedRadiance.Previous[sx + sy * width];
                historyR += h.x * wx * wy;
                historyG += h.y * wx * wy;
                historyB += h.z * wx * wy;
            }
        }
        // The nearest tap's count: an interpolated count would blur the reset boundary.
        int cx = (int)math.min((float)x0 + math.select(0.0f, 1.0f, ax > 0.5f), (float)(width - 1));
        int cy = (int)math.min((float)y0 + math.select(0.0f, 1.0f, ay > 0.5f), (float)(height - 1));
        float count = resolvedRadiance.Previous[cx + cy * width].w;
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
