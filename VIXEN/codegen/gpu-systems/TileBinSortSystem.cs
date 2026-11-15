using Undertow.Modding.Schema;
using Undertow.Sim.Systems;
using Yeroket.Util.KernelFramework;

namespace Vixen.Codegen.Gpu;

[KernelSystem]
public static class TileBinSortSystem
{
    [GaiaQuery] public static QuerySpec Tiles() => Gaia.Query()
        .All<FrameTargetsRef, R32f>(row => row.ViewDepth)
        .All<TileBinView>();

    [Update]
    public static void Run(in Texture2D<R32f> viewDepth, in TileBinView[] view,
        in TileBinCounter[] counters, in TileCandidate[] candidates,
        ref TileHeader[] tiles, ref TileSortedCandidate[] sortedCandidates, int i)
    {
        if (i >= view[0].tilesX * view[0].tilesY) return;
        if (viewDepth.Previous[0].x < -3.402823e38f) return;
        int tileIndex = i;
        tiles[i].index = tileIndex;
        int offered = counters[tileIndex].generation == counters[tileIndex].activeGeneration ? counters[tileIndex].count : 0;
        int stored = TileBinOps.StoredCount(offered, counters[tileIndex].listCapacityLog2);
        int first = tileIndex << counters[tileIndex].listCapacityLog2;
        int capacity = 1 << counters[tileIndex].listCapacityLog2;
        tiles[i].capacityLog2 = counters[tileIndex].listCapacityLog2;
        for (int slot = 0; slot < capacity; slot++)
        {
            if (slot < stored)
            {
                sortedCandidates[first + slot].nearDepth = candidates[first + slot].nearDepth;
                sortedCandidates[first + slot].instance = candidates[first + slot].instance;
            }
            else
            {
                // Pad to the declared power-of-two list size so the generated
                // bitonic network has a stable far-depth sentinel for every
                // inactive lane.
                sortedCandidates[first + slot].nearDepth = 3.402823e38f;
                sortedCandidates[first + slot].instance = 4294967295u;
            }
        }

        // Sort the complete fixed-size slab with a bounded bitonic network.
        // The prior insertion loop was quadratic in the per-tile candidate
        // count; a full slab network keeps the generated GPU work bounded at
        // capacity * log2(capacity)^2 even when a dense tile receives all
        // 512 candidates.
        for (int k = 2; k <= capacity; k <<= 1)
        {
            for (int j = k >> 1; j > 0; j >>= 1)
            {
                for (int slot = 0; slot < capacity; slot++)
                {
                    int other = slot ^ j;
                    if (other <= slot) continue;
                    bool shouldSwap = false;
                    if ((slot & k) == 0)
                    {
                        shouldSwap = TileBinOps.CandidateBefore(
                            sortedCandidates[first + other].nearDepth,
                            sortedCandidates[first + other].instance,
                            sortedCandidates[first + slot].nearDepth,
                            sortedCandidates[first + slot].instance);
                    }
                    else
                    {
                        shouldSwap = TileBinOps.CandidateBefore(
                            sortedCandidates[first + slot].nearDepth,
                            sortedCandidates[first + slot].instance,
                            sortedCandidates[first + other].nearDepth,
                            sortedCandidates[first + other].instance);
                    }
                    if (!shouldSwap) continue;
                    float movingDepth = sortedCandidates[first + slot].nearDepth;
                    uint movingInstance = sortedCandidates[first + slot].instance;
                    sortedCandidates[first + slot].nearDepth = sortedCandidates[first + other].nearDepth;
                    sortedCandidates[first + slot].instance = sortedCandidates[first + other].instance;
                    sortedCandidates[first + other].nearDepth = movingDepth;
                    sortedCandidates[first + other].instance = movingInstance;
                }
            }
        }
        tiles[i].demand = offered;
        tiles[i].count = stored;
    }
}
