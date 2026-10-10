using Yeroket.Util.KernelFramework;

// Canonical RecipeEmitterEntry — one source for C++ (Vixen::Gpu) + GLSL.
// Count-agnostic lighting (R502.2, lightdesign §2.2): one row per EMISSIVE procedural body
// instance, ascending instance order, with an inclusive running power CDF (cdf of the last
// row == EmitterTotals.phiRecipe). The surface-cell list is shared per recipe: rows of the
// same recipe point at the same [cellOffset, cellOffset + cellCount) slice of the cell-index
// list; a cell index is x + dim * (y + dim * z) into the recipe's occupancy grid.
// 48 B std430.
[GpuStruct]
public struct RecipeEmitterEntry
{
    public uint instIdx;     // @0   index into the instance streams
    public float phi;        // @4   instance power, W
    public float cdf;        // @8   inclusive cumulative power
    public uint flags;       // @12  bit0: area is the bound-sphere fallback (no occupancy grid)

    public uint cellOffset;  // @16  into the surface-cell index list
    public uint cellCount;   // @20  0 => bound-sphere fallback
    public uint gridDim;     // @24
    public float cellSize;   // @28  recipe-local edge length

    public Float3 gridAabbMin; // @32 recipe-local
    public float areaInst;     // @44 world-space area of this instance, m^2
}
