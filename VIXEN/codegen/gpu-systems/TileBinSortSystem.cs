using Undertow.Modding.Schema;
using Undertow.Sim.Systems;
using Yeroket.Util.KernelFramework;

namespace Vixen.Codegen.Gpu;

[KernelSystem]
public static class TileBinSortSystem
{
    [GaiaQuery] public static QuerySpec Tiles() => Gaia.Query()
        .All<FrameTargetsRef, R32f>(row => row.TileFarDepth);

    [Update]
    public static void Run(in Texture2D<R32f> tileFarDepth,
        in TileBinCounter[] counters, in TileCandidate[] candidates,
        ref TileHeader[] tiles, ref TileSortedCandidate[] sortedCandidates, int i)
    {
        if (tileFarDepth.Previous[0].x < -3.402823e38f) return;
        int tileIndex = tiles[i].index;
        tiles[i].index = tileIndex;
        int offered = counters[tileIndex].generation == counters[tileIndex].activeGeneration ? counters[tileIndex].count : 0;
        int stored = TileBinOps.StoredCount(offered, counters[tileIndex].listCapacityLog2);
        int first = tileIndex << counters[tileIndex].listCapacityLog2;
        tiles[i].capacityLog2 = counters[tileIndex].listCapacityLog2;
        if (stored > 0)
        {
            sortedCandidates[first].nearDepth = candidates[first].nearDepth;
            sortedCandidates[first].instance = candidates[first].instance;
        }
        for (int next = 1; next < stored; next++)
        {
            TileCandidate moving = candidates[first + next];
            int hole = next;
            while (hole > 0 && TileBinOps.CandidateBefore(moving.nearDepth, moving.instance,
                       sortedCandidates[first + hole - 1].nearDepth, sortedCandidates[first + hole - 1].instance))
            {
                sortedCandidates[first + hole] = sortedCandidates[first + hole - 1];
                hole--;
            }
            sortedCandidates[first + hole].nearDepth = moving.nearDepth;
            sortedCandidates[first + hole].instance = moving.instance;
        }
        tiles[i].demand = offered;
        tiles[i].count = stored;
    }
}
