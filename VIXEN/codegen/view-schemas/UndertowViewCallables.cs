// Inc-5 Milestone 3: the transform callables for undertow's real ViewSchema.cs columns whose
// Source expression isn't a flat 1:1 bind. Attached to the schema fields below via [Projected]
// for DOCUMENTATION/TRACEABILITY only in this proof -- gap #2 (see the plan doc's Progress Log)
// found that ViewWireFormat's ToBuffer() generator never dispatches [Projected] (only the RmlUi
// C++ face does), so the ACTUAL transform happens in the hand-written adapter
// (UndertowFrameAdapter.cs), which calls these same methods to guarantee identical semantics
// between the documented callable and the value the writer actually serializes.
using Yeroket.Util.KernelFramework;

namespace Vixen.ViewSchemas
{
    public static class UndertowViewCallables
    {
        // ViewSchema.cs: "el.IsFocused ? (byte)1 : (byte)0" / IsKnown / InLens / Selected --
        // one shared callable serves all 4 bool->byte columns (Milestone 1 Opus validator's
        // "[KernelCallable]'s fixed typed signature means ONE shared BoolToByte callable" finding).
        // Returns int, not byte: --callable-cpp's CppMappingTables has no byte->C++ mapping (a bare
        // "byte" is not a valid C++ type), and this schema's own convention throughout (see
        // UndertowHud.cs: "byte widened to int; source: ...") already widens every byte-shaped
        // column to int -- this callable follows the same widening rather than emitting an
        // unmappable type. HudInspect.selected calls this 1-param overload directly (raw is int).
        [KernelCallable] public static int BoolToByte(bool v) => v ? 1 : 0;

        // 2-param overload: TypedAccessorEmitter's row-context [Projected] call site (Yeroket main
        // 7d6c8b4e) reads the stored cell (Cell(kElem_focused/known/inLens).i, an int -- "byte
        // widened to int" per UndertowHud.cs) and passes it as the real first arg, row index second
        // -- true identity-through-bool now, not the old (rowIndex, rowIndex) stand-in stub. Returns
        // int, not byte: --callable-cpp's CppMappingTables has no byte->C++ mapping.
        [KernelCallable] public static int BoolToByte(int a, int b) => a != 0 ? 1 : 0;

        // ViewSchema.cs: "(byte)el.StrengthBand" -- StrengthBand : byte { Unknown=0, Weak, Moderate,
        // Strong, Overwhelming } (IntelBanding.cs). A real enum cast, not a bool: needs its own
        // callable per the Milestone 1 Opus validator's "2 enum casts collapse per enum type" finding.
        // Row context only (HudFactions.strengthBand) -- same real-cell-first-arg shape as BoolToByte
        // above (post 7d6c8b4e); returns int, no bare-byte C++ type.
        [KernelCallable] public static int StrengthBandToByte(int strengthBand, int rowIndex) => strengthBand;

        // ViewSchema.cs: "(byte)el.Confidence" -- Confidence : byte { Fresh=0, Aging, Stale }.
        // Row context only (HudFactions.confidence) -- same 2-param/int-return shape as StrengthBandToByte.
        [KernelCallable] public static int ConfidenceToByte(int confidence, int rowIndex) => confidence;

        // ViewSchema.cs: "el.Orbit.HasValue ? el.Orbit.Value.ParentBodyIndex : -1" -- nullable-unwrap
        // with a -1 sentinel. This is the WRITE-time transform (UndertowFrameAdapter.Bodies calls it
        // directly to compute the wire value); it is NOT [Projected] on UndertowBodyRow.orbitParent
        // (see UndertowHud.cs's comment there) because the stored cell is already the post-transform
        // int -- re-running this on it at READ time would double-apply the sentinel logic.
        [KernelCallable] public static int OrbitParentOrSentinel(bool hasOrbit, int parentBodyIndex) => hasOrbit ? parentBodyIndex : -1;

        // Row-context identity for orbitParent (see UndertowHud.cs): the wire cell already holds the
        // final -1-or-index value: this callable is name-binding/traceability only, same role as
        // IdentityFloat/IdentityString for mass/cause/topRelSig.
        [KernelCallable] public static int IdentityInt(int v, int rowIndex) => v;

        // Name-binding-only identity callables (HudInspect.TopRelSig/Cause, gap #2's Milestone-1-flagged
        // name-mismatch columns whose Source carries no VALUE transform, only a different C# source-member
        // name than the view field). No-op bodies; exist purely so [Projected]'s (hostType, methodName)
        // constructor can carry a machine-readable pointer back to the true source-member name, matching
        // the traceability level the real transform callables above already provide.
        [KernelCallable] public static float IdentityFloat(float v) => v;

        // 2-param overload: reached in the Bodies row context (mass) -- Yeroket main 7d6c8b4e fixed
        // TypedAccessorEmitter's row-Projection branch to read the stored cell first and pass it as
        // the callable's real first arg (raw is float, matching Cell(kElem_mass).f), with the row
        // index as the second arg (mirrors the Vector-Projection branch's existing shape). True
        // identity now -- no stand-in stub needed.
        [KernelCallable] public static float IdentityFloat(float v, int rowIndex) => v;

        // NOT [KernelCallable]: --callable-cpp's CppMappingTables has no string->C++ mapping (a bare
        // "string" is not a valid C++ type, unlike int/float/bool/uint) -- transpiling this would
        // emit an uncompilable `inline string IdentityString(string v)`. The real C++ symbol
        // (Vixen::AppFlow::Generated::IdentityString(Rml::String)), reached by
        // UndertowHudInspect.typed.g.h's cause() accessor, is hand-authored in
        // libraries/AppFlow/include/AppFlowCallablesHandString.h instead (same "consumer must
        // define by hand" precedent as EditorLayersView's activeLayerCountOverride hook) and
        // included alongside the generated header. This C# method stays for [Projected]'s
        // (hostType, methodName) source-traceability pointer only -- it is never invoked from C#.
        public static string IdentityString(string v) => v;
    }
}
