// GB1 (R857.4/R857.5): generated camera-ray tile candidate sets, z-bin form (fable-research1 B-A).
// One pass ranks the instances front to back (near depth, then instance id) and clears the tile
// rank sets; the scatter sets one bit per (instance, tile) at the instance's rank. The march visits a tile's set bits in rank order, so it sees the
// candidates in the same order a per-tile sort by (near depth, instance) produced, without one.
using System;
using System.Threading;
using Undertow.Modding.Schema;
using Undertow.Sim.Systems;
using Yeroket.Util.KernelFramework;

namespace Vixen.Codegen.Gpu;

public static class TileBinOps
{
    public const int CullNone = 0;
    public const int CullBehindCamera = 1;
    public const int CullOffScreen = 2;
    public const int RankBits = 32;

    [KernelCallable]
    public static TileRect ProjectBound(float centerX, float centerY, float centerZ, float radius, TileBinView view)
    {
        float vx = view.viewXx * centerX + view.viewXy * centerY + view.viewXz * centerZ + view.viewXw;
        float vy = view.viewYx * centerX + view.viewYy * centerY + view.viewYz * centerZ + view.viewYw;
        float vz = view.viewZx * centerX + view.viewZy * centerY + view.viewZz * centerZ + view.viewZw;
        float nearDepth = vz - radius;
        float farDepth = vz + radius;
        int lastX = view.tilesX - 1;
        int lastY = view.tilesY - 1;
        if (farDepth <= view.nearPlane) return new TileRect(0, 0, -1, -1, 0.0f, CullBehindCamera);
        if (nearDepth <= view.nearPlane) return new TileRect(0, 0, lastX, lastY, view.nearPlane, CullNone);
        float left = vx - radius;
        float right = vx + radius;
        float bottom = vy - radius;
        float top = vy + radius;
        float leftDepth = left < 0.0f ? nearDepth : farDepth;
        float rightDepth = right > 0.0f ? nearDepth : farDepth;
        float bottomDepth = bottom < 0.0f ? nearDepth : farDepth;
        float topDepth = top > 0.0f ? nearDepth : farDepth;
        float minPixelX = view.centerX + view.focalX * (left / leftDepth) - 0.5f;
        float maxPixelX = view.centerX + view.focalX * (right / rightDepth) + 0.5f;
        float minPixelY = view.centerY + view.focalY * (bottom / bottomDepth) - 0.5f;
        float maxPixelY = view.centerY + view.focalY * (top / topDepth) + 0.5f;
        float width = view.tileSize * (float)view.tilesX;
        float height = view.tileSize * (float)view.tilesY;
        if (maxPixelX < 0.0f || maxPixelY < 0.0f || minPixelX >= width || minPixelY >= height)
            return new TileRect(0, 0, -1, -1, nearDepth, CullOffScreen);
        int minX = Math.Max((int)Math.Floor(minPixelX / view.tileSize), 0);
        int minY = Math.Max((int)Math.Floor(minPixelY / view.tileSize), 0);
        int maxX = Math.Min((int)Math.Floor(maxPixelX / view.tileSize), lastX);
        int maxY = Math.Min((int)Math.Floor(maxPixelY / view.tileSize), lastY);
        return new TileRect(minX, minY, maxX, maxY, nearDepth, CullNone);
    }

    // The rank key: ProjectBound's near depth (the near plane for a bound that crosses it).
    // A bound behind the camera is never binned; it ranks last.
    [KernelCallable]
    public static float RankDepth(float centerX, float centerY, float centerZ, float radius, TileBinView view)
    {
        float vz = view.viewZx * centerX + view.viewZy * centerY + view.viewZz * centerZ + view.viewZw;
        float nearDepth = vz - radius;
        if (vz + radius <= view.nearPlane) return 3.402823e38f;
        return nearDepth <= view.nearPlane ? view.nearPlane : nearDepth;
    }

    // Zero is the ViewDepth history clear value: nothing has written depth yet (the
    // first frame) or this configuration has no ViewDepth producer (no reconstruction
    // module, e.g. the look-dev captures). Such a tile occludes nothing; comparing
    // against it culled every instance and the march rendered sky only.
    [KernelCallable]
    public static bool IsBehindTileDepth(float nearDepth, float tileFarDepth)
        => tileFarDepth > 0.0f && nearDepth > tileFarDepth;

    [KernelCallable]
    public static bool CandidateBefore(float depth, uint instance, float otherDepth, uint otherInstance)
        => depth < otherDepth || (depth == otherDepth && instance < otherInstance);

    // Scatter invocations per instance (R857.3): up to four 8x8 workgroups' worth, fewer when
    // the view-depth dispatch cannot hold instanceCount of them. The dispatch has at least
    // (tilesX - 1) * (tilesY - 1) * TilePixels invocations, since a tile is 8x8 pixels.
    public const int ScatterLaneCount = 256;
    public const int TilePixels = 64;

    [KernelCallable]
    public static int ScatterLanes(TileBinView view)
    {
        int available = (view.tilesX - 1) * (view.tilesY - 1) * TilePixels;
        if (view.instanceCount <= 0) return ScatterLaneCount;
        int lanes = available / view.instanceCount;
        if (lanes > ScatterLaneCount) return ScatterLaneCount;
        return lanes < 1 ? 1 : lanes;
    }

    // Invocations of a view-depth-extent pass that are certain to exist (see ScatterLanes).
    [KernelCallable]
    public static int GuaranteedInvocations(TileBinView view)
    {
        int available = (view.tilesX - 1) * (view.tilesY - 1) * TilePixels;
        return available < 1 ? 1 : available;
    }

    // Rank words in use per tile this frame.
    [KernelCallable]
    public static int TileWords(TileBinView view)
    {
        int words = (view.instanceCount + RankBits - 1) / RankBits;
        return words < view.wordCapacity ? words : view.wordCapacity;
    }
}

[KernelSystem]
public static class TileBinPrepareSystem
{
    [GaiaQuery]
    public static QuerySpec Instances() => Gaia.Query()
        .All<InstanceBound>()
        .All<FrameTargetsRef, R32f>(row => row.ViewDepth);

    [Update]
    public static void Run(in InstanceBound[] bounds, in TileBinView[] view,
        in Texture2D<R32f> viewDepth, ref InstanceOrder[] order, ref TileRankWord[] words, int i)
    {
        // Clear this frame's tile rank sets, word-parallel across the dispatch.
        int clearLanes = TileBinOps.GuaranteedInvocations(view[0]);
        int wordCount = view[0].tilesX * view[0].tilesY * TileBinOps.TileWords(view[0]);
        if (i < clearLanes)
        {
            for (int word = i; word < wordCount; word += clearLanes) words[word].bits = 0u;
        }
        if (i >= view[0].instanceCount) return;
        // More instances than the rank words hold: nothing is ranked (the order buffer holds
        // wordCapacity * 32 entries) and the march falls back for every tile (R861.1).
        if (view[0].instanceCount > view[0].wordCapacity * TileBinOps.RankBits) return;
        // The query binds ViewDepth for the dispatch extent; the generator refuses a texture view
        // that is never read (YK_KF_4020), so read one texel (no history value is below -FLT_MAX).
        if (viewDepth.Previous[0].x < -3.402823e38f) return;
        // Rank = number of instances ordered before this one. Every invocation counts over the
        // same keys, so the ranks are a permutation and need no sort.
        float depth = TileBinOps.RankDepth(bounds[i].centerX, bounds[i].centerY, bounds[i].centerZ,
            bounds[i].radius, view[0]);
        int rank = 0;
        for (int other = 0; other < view[0].instanceCount; other++)
        {
            float otherDepth = TileBinOps.RankDepth(bounds[other].centerX, bounds[other].centerY,
                bounds[other].centerZ, bounds[other].radius, view[0]);
            if (TileBinOps.CandidateBefore(otherDepth, (uint)other, depth, (uint)i)) rank++;
        }
        order[rank].instance = (uint)i;
        order[rank].depth = depth;
        order[i].rank = (uint)rank;
    }
}

// Prepare clears the rank words and publishes instance ranks; Scatter consumes those ranks before setting tile bits.
[KernelSystem]
[AfterSystem(typeof(TileBinPrepareSystem))]
public static class TileBinScatterSystem
{
    [GaiaQuery]
    public static QuerySpec Instances() => Gaia.Query()
        .All<InstanceBound>()
        .All<FrameTargetsRef, R32f>(row => row.ViewDepth);

    [Update]
    public static void Run(in InstanceBound[] bounds, in TileBinView[] view,
        in Texture2D<R32f> viewDepth, in InstanceOrder[] order, ref TileRankWord[] words,
        ref TileBinStats[] stats, int i)
    {
        // Every instance gets ScatterLanes invocations that walk its tile rectangle in
        // stride (R857.3 binning budget); each sets the instance's rank bit in every tile it
        // covers. The bit's position, not the write order, carries the front-to-back order.
        // The lane count is computed before the bound check: a callable inside the leading
        // guard is hoisted into main() with its block argument unindexed (generator gap).
        int lanes = TileBinOps.ScatterLanes(view[0]);
        if (i >= view[0].instanceCount * lanes) return;
        int instance = i / lanes;
        int lane = i - instance * lanes;
        TileRect rect = TileBinOps.ProjectBound(bounds[instance].centerX, bounds[instance].centerY,
            bounds[instance].centerZ, bounds[instance].radius, view[0]);
        if (rect.cull == TileBinOps.CullBehindCamera)
        {
            if (lane == 0) Interlocked.Increment(ref stats[0].behindCamera);
            return;
        }
        if (rect.cull == TileBinOps.CullOffScreen)
        {
            if (lane == 0) Interlocked.Increment(ref stats[0].offScreen);
            return;
        }
        if (view[0].instanceCount > view[0].wordCapacity * TileBinOps.RankBits)
        {
            if (lane == 0) Interlocked.Increment(ref stats[0].overflowed);
            return;
        }
        uint rank = order[instance].rank;
        int rankWord = (int)(rank / 32u);
        if (rankWord >= TileBinOps.TileWords(view[0]))
        {
            if (lane == 0) Interlocked.Increment(ref stats[0].overflowed);
            return;
        }
        uint rankBit = 1u << (int)(rank % 32u);
        int tileCount = view[0].tilesX * view[0].tilesY;
        int wordBase = rankWord * tileCount;
        int width = rect.maxX - rect.minX + 1;
        int area = width * (rect.maxY - rect.minY + 1);
        uint visible = 0u;
        for (int offset = lane; offset < area; offset += lanes)
        {
            int row = offset / width;
            int tileY = rect.minY + row;
            int tileX = rect.minX + (offset - row * width);
            int tile = tileY * view[0].tilesX + tileX;
            if (TileBinOps.IsBehindTileDepth(rect.nearDepth, viewDepth.Previous[tile].x)) continue;
            visible++;
            Interlocked.Or(ref words[wordBase + tile].bits, rankBit);
        }
        if (lane < area && visible == 0u) Interlocked.Increment(ref stats[0].occludedSlices);
        if (visible > 0u) Interlocked.Add(ref stats[0].listed, visible);
    }
}
