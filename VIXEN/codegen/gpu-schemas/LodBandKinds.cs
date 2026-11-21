using Undertow.Modding.Schema;
using Yeroket.Util.KernelFramework;
using static Undertow.Modding.Schema.Schema;

namespace Vixen.Codegen.Gpu;

// R860 RL0 (R879.4): GPU-only rows of per-instance recipe LOD band selection, declared as vixen: kinds.
// Their std430 GPU facet and C++/GLSL faces are derived (R879): no [GpuStruct], no hand struct.
// InstanceRecipe is instance i's base recipe id and the slot of its recipe's ladder in the band
// table, written by the scene beside its bound. Recipe ids are sparse, so the table is indexed by a
// dense slot: slot 0 is the one-band ladder of every recipe without a variant ladder.
// RecipeLodBand is one band of a ladder, four rows per slot (row slot * 4 + band): the band is
// active while q < maxQ and marches variantRecipeId, or the instance's own recipe when that is
// TileBinPrepareSystem.SelfRecipe. Rows past a ladder's end repeat its last band.
// InstanceBand is the band the generated tile prepare pass chose this frame and the recipe the
// march, shadows and GI evaluate for the instance (stateless per frame, R860.2).
public readonly struct InstanceRecipe { }
public readonly struct RecipeLodBand { }
public readonly struct InstanceBand { }

// The owner of the three rows (exemplar: FrameTargets.cs, FrameTargetsSystem): the scene writes
// InstanceRecipe and RecipeLodBand, the generated tile prepare pass writes InstanceBand.
[KernelSystem("vixen:lod-bands")]
[Declare(typeof(InstanceRecipe))]
[Declare(typeof(RecipeLodBand))]
[Declare(typeof(InstanceBand))]
public sealed class LodBandsSystem { }

public static class LodBandKinds
{
    public static readonly KindSchema InstanceRecipe = Simple(
        F<UInt>("RecipeId").Dynamic(),
        F<UInt>("LodSlot").Dynamic());

    public static readonly KindSchema RecipeLodBand = Simple(
        F<Float>("MaxQ").Dynamic(),
        F<UInt>("VariantRecipeId").Dynamic());

    public static readonly KindSchema InstanceBand = Simple(
        F<UInt>("VariantRecipeId").Dynamic(),
        F<UInt>("Band").Dynamic());
}
