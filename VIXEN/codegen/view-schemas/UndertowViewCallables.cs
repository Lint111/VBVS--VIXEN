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
        [KernelCallable] public static byte BoolToByte(bool v) => v ? (byte)1 : (byte)0;

        // ViewSchema.cs: "(byte)el.StrengthBand" -- StrengthBand : byte { Unknown=0, Weak, Moderate,
        // Strong, Overwhelming } (IntelBanding.cs). A real enum cast, not a bool: needs its own
        // callable per the Milestone 1 Opus validator's "2 enum casts collapse per enum type" finding.
        [KernelCallable] public static byte StrengthBandToByte(int strengthBand) => (byte)strengthBand;

        // ViewSchema.cs: "(byte)el.Confidence" -- Confidence : byte { Fresh=0, Aging, Stale }.
        [KernelCallable] public static byte ConfidenceToByte(int confidence) => (byte)confidence;

        // ViewSchema.cs: "el.Orbit.HasValue ? el.Orbit.Value.ParentBodyIndex : -1" -- nullable-unwrap
        // with a -1 sentinel. Bodies-section only; kept here for completeness even though Bodies
        // itself is out of scope this milestone (see plan doc gap #4) -- documents the full
        // categorization Milestone 1 found, so a future Bodies-capable increment doesn't re-derive it.
        [KernelCallable] public static int OrbitParentOrSentinel(bool hasOrbit, int parentBodyIndex) => hasOrbit ? parentBodyIndex : -1;

        // Name-binding-only identity callables (HudInspect.TopRelSig/Cause, gap #2's Milestone-1-flagged
        // name-mismatch columns whose Source carries no VALUE transform, only a different C# source-member
        // name than the view field). No-op bodies; exist purely so [Projected]'s (hostType, methodName)
        // constructor can carry a machine-readable pointer back to the true source-member name, matching
        // the traceability level the real transform callables above already provide.
        [KernelCallable] public static float IdentityFloat(float v) => v;
        [KernelCallable] public static string IdentityString(string v) => v;
    }
}
