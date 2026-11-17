// GB1 (R857.4/R857.5): generated camera-ray tile candidate lists.
// The scatter pass consumes a lean bound stream and the previous frame's Hi-Z tile depth;
// the march consumes the depth-ordered list, so no CPU ordering or fixed instance cap is needed.
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

    [KernelCallable]
    public static int StoredCount(int offered, int listCapacityLog2)
    {
        int capacity = 1 << listCapacityLog2;
        return offered < capacity ? offered : capacity;
    }
}

[KernelSystem]
public static class TileBinScatterSystem
{
    [GaiaQuery]
    public static QuerySpec Instances() => Gaia.Query()
        .All<InstanceBound>()
        .All<FrameTargetsRef, R32f>(row => row.ViewDepth);

    [Update]
    public static void Run(in InstanceBound[] bounds, in TileBinView[] view,
        in Texture2D<R32f> viewDepth, ref TileBinCounter[] counters,
        ref TileCandidate[] candidates,
        ref TileBinStats[] stats, int i)
    {
        // Every instance gets ScatterLanes invocations that walk its tile rectangle in
        // stride (R857.3 binning budget). One invocation per instance walked a screen-sized
        // rectangle alone, one atomic per tile: 8 to 17 ms at 1080p for three instances.
        // The append order changes; the sort orders each list by (depth, instance), so the
        // lists are the same whenever no tile overflows.
        if (i >= view[0].instanceCount * TileBinOps.ScatterLanes(view[0])) return;
        int lanes = TileBinOps.ScatterLanes(view[0]);
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
        int capacity = 1 << view[0].listCapacityLog2;
        int width = rect.maxX - rect.minX + 1;
        int area = width * (rect.maxY - rect.minY + 1);
        uint visible = 0u;
        uint listed = 0u;
        for (int offset = lane; offset < area; offset += lanes)
        {
            int row = offset / width;
            int tileY = rect.minY + row;
            int tileX = rect.minX + (offset - row * width);
            int tile = tileY * view[0].tilesX + tileX;
            if (TileBinOps.IsBehindTileDepth(rect.nearDepth, viewDepth.Previous[tile].x)) continue;
            visible++;
            // Wait-free append: one atomic per (instance, tile); no invocation waits on
            // another. TileBinSortSystem consumes and zeroes every tile count, so the
            // counters start each frame at zero.
            int slot = Interlocked.Increment(ref counters[tile].count) - 1;
            if (slot >= capacity)
            {
                Interlocked.Increment(ref stats[0].overflowed);
                continue;
            }
            int entry = (tile << view[0].listCapacityLog2) + slot;
            candidates[entry].nearDepth = rect.nearDepth;
            candidates[entry].instance = (uint)instance;
            listed++;
        }
        if (lane < area && visible == 0u) Interlocked.Increment(ref stats[0].occludedSlices);
        if (listed > 0u) Interlocked.Add(ref stats[0].listed, listed);
    }
}
