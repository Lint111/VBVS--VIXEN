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
    }
}
