using Yeroket.Util.KernelFramework;

namespace Vixen.SVO.Codegen
{
    /// <summary>
    /// VIXEN-owned recipe opcodes. These use the kernel's SdfCoreOp declaration schema and are
    /// merged with canonical SDFOpCode fields by the recipe codegen jobs.
    /// </summary>
    public enum VixenRecipeOpCode : byte
    {
        [SdfCoreOp(
            Derivative = SdfDerivativeRule.Reject,
            Hazard = SdfDerivativeHazard.NonDifferentiable,
            Cost = SdfDerivativeCost.Fallback,
            RecipeExecution = SdfRecipeExecutionClass.Elementwise,
            RecipeLowering = SdfRecipeLoweringKind.PositionStackControl,
            RecipeControl = SdfRecipeControlBehavior.DeclarePosition,
            RecipeDispatch = SdfRecipeDispatchKind.DeclarePosition,
            RecipeResult = SdfRecipeResultKind.None,
            RecipeValuePop = 3,
            RecipeValuePush = 0,
            RecipePositionPop = 0,
            RecipePositionPush = 0)]
        DeclarePosition = 112,

        [SdfCoreOp(
            Derivative = SdfDerivativeRule.Reject,
            Hazard = SdfDerivativeHazard.NonDifferentiable,
            Cost = SdfDerivativeCost.Fallback,
            RecipeExecution = SdfRecipeExecutionClass.LoweredAway,
            RecipeLowering = SdfRecipeLoweringKind.Invocation,
            RecipeControl = SdfRecipeControlBehavior.InvokeRecipe,
            RecipeDispatch = SdfRecipeDispatchKind.InvokeRecipe,
            RecipeResult = SdfRecipeResultKind.Value,
            RecipeValuePop = 0,
            RecipeValuePush = 1,
            RecipePositionPop = 0,
            RecipePositionPush = 0)]
        InvokeRecipe = 113,
    }
}
