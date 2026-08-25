// [KernelCallable] logic-transplant walking skeleton (Inc-4 reframe R4, design D12/D13) —
// proves the kernel transplants flow LOGIC exactly as it transplants data. This method has
// NO [Flow*] attributes, so the AppFlowEmitter's whole-compilation attribute sweep (--appflow)
// ignores it entirely; only --callable-cpp discovers [KernelCallable] methods.
using Yeroket.Util.KernelFramework;

namespace Vixen.AppFlow.Reference
{
    public static class AppFlowCallables
    {
        // Self-inverse: applyToggle(applyToggle(mask,i),i) == mask. Transplanted C# -> C++ (D12).
        // CAVEAT: index must be < 32 — C# masks the shift count, C++ shift >= 32 is UB (transplant divergence, R4 review).
        [KernelCallable] public static uint applyToggle(uint mask, uint index) => mask ^ (1u << (int)index);

        // Inc-Ovr: the model->view HALF of the mask<->checkboxes projection (design §5a). Reads bit
        // `index` of `mask` -- byte-identical to EditorLayersView::PopulateFromMask's hand-written
        // `((mask >> i) & 1u) != 0u` (the loop re-derived by the [View] schema's
        // EditorLayerRow.isChecked projection declaration). applyToggle above is the
        // inverse (view->model, single-bit flip); bitAt is the forward direction this proof needed
        // authored fresh -- Milestone 1 found only the write half was already transplanted.
        // CAVEAT: index must be < 32, same shift-UB caveat as applyToggle.
        [KernelCallable] public static bool bitAt(uint mask, uint index) => ((mask >> (int)index) & 1u) != 0u;
    }
}
