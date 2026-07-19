// Negative-test fixture (seam M2c): a [FlowDataTarget] naming a View noun that does not exist.
// Feeding this to `--appflow --view-schema <real view-schemas>` MUST fail generation (non-zero
// exit) — that is the build-gate proving an unknown/typo'd Data target is caught at codegen time,
// not review. NOT a real schema; never generated into a committed artifact.
using Yeroket.Util.KernelFramework;

namespace Vixen.AppFlow.BadNounFixture
{
    [FlowStateEnum] public enum FlowState { Editing = 0 }
    [FlowGuardEnum] public enum FlowGuard { DocumentValid = 0 }
    [FlowActionEnum] public enum FlowAction { Data = 0 }
    [FlowParamTypeEnum] public enum FlowParamType { String = 0, Int = 1, Float = 2, EntityRef = 3 }

    // "noSuchNoun" is not a field on any [View] struct — resolution throws AppFlowTargetException.
    [FlowDataTarget(nameof(FlowAction.Data), "noSuchNoun")]
    public static class DataToNowhere { }
}
