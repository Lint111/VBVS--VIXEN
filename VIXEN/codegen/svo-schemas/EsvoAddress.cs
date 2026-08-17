using Yeroket.Util.KernelFramework;

// ESVO Address Extraction, Slice V1 (2026-08-17, docs/superpowers/specs/2026-08-17-esvo-address-
// extraction-design.md, RULING B): the production EsvoAddress schema. Kernel slice K1 (undertow
// 6f317426) proved this exact shape (depth + 8 flattened hop fields, [GpuStruct]/[KernelCallable]
// with CppNamespace="Vixen::SVO") against a TEST FIXTURE at Vixen.SVO.Reference.EsvoAddress
// (CodegenTool~/Tests/EsvoAddressTests.cs) — this file is the real one, in VIXEN's own codegen
// tree, feeding VIXEN's own CMake regen/check targets (codegen/CMakeLists.txt).
//
// Isolated in its own svo-schemas/ directory, NOT config-schemas/ or view-schemas/: the existing
// --callable-cpp sweep (CMakeLists.txt's _schema_callables_run = the whole codegen/ root) emits
// ONE header committed to ONE namespace (Program.cs BuildCallableCppHeader throws
// NotSupportedException on a namespace mismatch within one invocation) — AppFlow/view callables
// use the default "Vixen::AppFlow::Generated" namespace, so SharedPrefixLength's explicit
// "Vixen::SVO" would collide if it sat under that same sweep root. A dedicated --schema
// svo-schemas --callable-cpp invocation (its own CMake target, its own --out-header) avoids the
// collision entirely rather than threading an --exclude through the shared sweep.
//
// GpuStruct field shape: uint Depth + uint Hop0..Hop7 (9 scalars, NOT a [GpuArray(8)] on one
// field — GpuArrayAttribute's C# authoring shape is schema-metadata-only, so it cannot back real
// per-index access; see EsvoAddressTests.cs's own comment). Depth is uint32_t, not a C++ byte:
// ScalarKind has no byte member by established policy (FieldShapeRecognizer.cs:23-26) — std430
// alignment pads a byte up to 4 bytes ahead of Hop0 regardless, so this costs nothing. This is
// the [GpuStruct]-generated FACE only: it is an emitted POD (public fields, no methods — see the
// committed OctreeConfig.g.h for the established shape). PushHop/Depth()/Hop(i)/equality/ToString
// stay hand-authored C++ members on TierAddress.h's own wrapper (RULING B: only the value MATH
// crosses the language boundary as [KernelCallable]; a single-struct mutator/accessor needs no
// kernel derivation, [GpuStruct] already gives the byte-identical mirror for free).
namespace Vixen.SVO
{
    [GpuStruct(CppNamespace = "Vixen::SVO")]
    public struct EsvoAddress
    {
        public uint Depth;
        public uint Hop0;
        public uint Hop1;
        public uint Hop2;
        public uint Hop3;
        public uint Hop4;
        public uint Hop5;
        public uint Hop6;
        public uint Hop7;
    }

    public static class EsvoAddressMath
    {
        // Port of TierAddress::SharedPrefixLength (TierAddress.h) — "shared-prefix = shared
        // ancestor". Flattened depth+8-hop parameters per address: the only [KernelCallable]
        // parameter shape this codegen tool supports (no struct-by-value/by-ref precedent
        // anywhere in the repo). depthA/depthB are int (not byte) because CppMappingTables has
        // no `byte` entry.
        [KernelCallable(CppNamespace = "Vixen::SVO")]
        public static int SharedPrefixLength(
            int depthA, uint a0, uint a1, uint a2, uint a3, uint a4, uint a5, uint a6, uint a7,
            int depthB, uint b0, uint b1, uint b2, uint b3, uint b4, uint b5, uint b6, uint b7)
        {
            int limit = depthA < depthB ? depthA : depthB;
            int i = 0;
            for (int k = 0; k < limit; k = k + 1)
            {
                uint ha = k == 0 ? a0 : k == 1 ? a1 : k == 2 ? a2 : k == 3 ? a3 : k == 4 ? a4 : k == 5 ? a5 : k == 6 ? a6 : a7;
                uint hb = k == 0 ? b0 : k == 1 ? b1 : k == 2 ? b2 : k == 3 ? b3 : k == 4 ? b4 : k == 5 ? b5 : k == 6 ? b6 : b7;
                if (ha != hb) return i;
                i = i + 1;
            }
            return i;
        }
    }
}
