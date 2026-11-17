using Yeroket.Util.KernelFramework;

namespace Vixen.Codegen.Gpu;

// GB1 (R857.4/R857.5): the schema-owned records shared by the generated tile
// candidate systems and their native render-graph resource nodes.
//
// z-bin form (fable-research1 B-A, R892.12 measured against the per-tile sort): instances are
// ranked once per frame by near depth (ties by instance id), each tile holds one bit per rank,
// and the march visits a tile's set bits in rank order, i.e. front to back. No per-tile list,
// no per-tile sort and no per-tile header exist: the march derives the word count from the view.
[GpuStruct(Layout = GpuLayout.Std430)]
public struct InstanceBound
{
    public float centerX;
    public float centerY;
    public float centerZ;
    public float radius;
}

[GpuStruct(Layout = GpuLayout.Std430)]
public struct TileBinView
{
    public float viewXx; public float viewXy; public float viewXz; public float viewXw;
    public float viewYx; public float viewYy; public float viewYz; public float viewYw;
    public float viewZx; public float viewZy; public float viewZz; public float viewZw;
    public float focalX; public float focalY; public float centerX; public float centerY;
    public float nearPlane; public float tileSize;
    public int tilesX; public int tilesY;
    // Rank words allocated per tile (32 ranks each); instanceCount above wordCapacity * 32
    // marks every tile incomplete and the march falls back (R861.1).
    public int wordCapacity;
    public int instanceCount;
}

// One word of a tile's rank set. Word-major: word w of tile t is entry w * tileCount + t, so a
// pass over tiles touches consecutive words.
[GpuStruct(Layout = GpuLayout.Std430)]
public struct TileRankWord
{
    public uint bits;
}

// Entry r: the instance ranked r (front to back) and its rank key, the near view depth of its
// bound (a lower bound on any camera-ray hit distance, so the march stops once the key passes
// its nearest hit). Entry i: the rank of instance i.
[GpuStruct(Layout = GpuLayout.Std430)]
public struct InstanceOrder
{
    public uint instance;
    public uint rank;
    public float depth;
}

[GpuStruct(Layout = GpuLayout.Std430)]
public struct TileBinStats
{
    public uint listed;
    public uint behindCamera;
    public uint offScreen;
    // Scatter slices (ScatterLanes invocations per instance) whose tiles all lie behind
    // last frame's depth; one instance spans several slices.
    public uint occludedSlices;
    public uint overflowed;
}

[GpuStruct(Layout = GpuLayout.Std430)]
public struct TileRect
{
    public int minX; public int minY; public int maxX; public int maxY;
    public float nearDepth; public int cull;

    public TileRect(int minX, int minY, int maxX, int maxY, float nearDepth, int cull)
    {
        this.minX = minX; this.minY = minY; this.maxX = maxX; this.maxY = maxY;
        this.nearDepth = nearDepth; this.cull = cull;
    }
}
