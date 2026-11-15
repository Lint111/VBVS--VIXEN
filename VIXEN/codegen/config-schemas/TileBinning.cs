using Yeroket.Util.KernelFramework;

namespace Vixen.Codegen.Gpu;

// GB1 (R857.4/R857.5): the schema-owned records shared by the generated tile
// candidate systems and their native render-graph resource nodes.
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
    public int tilesX; public int tilesY; public int listCapacityLog2; public int generation;
    public int instanceCount;
}

[GpuStruct(Layout = GpuLayout.Std430)]
public struct TileBinCounter
{
    public int generation;
    public int activeGeneration;
    public int count;
    public int demand;
    public int listCapacityLog2;
}

[GpuStruct(Layout = GpuLayout.Std430)]
public struct TileHeader
{
    public int count;
    public int demand;
    public int index;
    public int capacityLog2;
}

[GpuStruct(Layout = GpuLayout.Std430)]
public struct TileCandidate
{
    public float nearDepth;
    public uint instance;
}

[GpuStruct(Layout = GpuLayout.Std430)]
public struct TileSortedCandidate
{
    public float nearDepth;
    public uint instance;
}

[GpuStruct(Layout = GpuLayout.Std430)]
public struct TileBinStats
{
    public uint listed;
    public uint behindCamera;
    public uint offScreen;
    public uint occluded;
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
