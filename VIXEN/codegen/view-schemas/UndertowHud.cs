// Inc-5 Milestone 3 (Task 4): schema-driven [View] declarations that re-derive undertow's REAL
// ViewSchema.cs sections (core/src/Undertow.View/ViewSchema.cs) -- Hud, HudFactions, HudEvents,
// HudInspect. Bodies is explicitly OUT of scope this milestone (see the plan doc's Progress Log,
// gap #4: Position/RecipeParams are Vec3f SCALARS and OrbitPath is a nullable ListVec3f -- neither
// shape is representable by the shipped [View] model, which only supports int/float/bool/string
// scalars and T[] of scalar-only structs).
//
// Distinct type names (UndertowHud, not Hud) so this schema does NOT collide with the existing,
// live, drift-guarded native Hud/HudFaction/HudEvent schema (Hud.cs) that VIXEN's own HUD already
// consumes via --view/--view-blob/--view-markup/--view-writer -- these are a SEPARATE proof vehicle
// for undertow's migration, not a replacement for VIXEN's own HUD view.
//
// [Projected] on the transform columns below is DOCUMENTATION/TRACEABILITY only for this proof
// (gap #2, plan doc Progress Log): ViewWireFormat's generated ToBuffer() never dispatches
// [Projected] (only the RmlUi C++ face does), so the callable named here does not actually run in
// the generated writer -- UndertowFrameAdapter.cs (hand-written, not generated) performs the
// identical transform when constructing rows, calling the SAME UndertowViewCallables methods this
// attribute names, so callable and adapter can never semantically drift apart.
using Yeroket.Util.KernelFramework;

namespace Vixen.ViewSchemas
{
    // --- Hud (ViewSchema.cs SectionHud, "Hud"): Tick/BodyCount/ActiveLens/ActiveLensCount, all
    // identity 1:1 binds (U8 ActiveLens widens to int -- ViewScalar has no byte type; the adapter
    // narrows back to byte when it matters for decoded-value comparison against undertow's real U8). ---
    [View]
    public struct UndertowHud
    {
        public int tick;
        public int bodyCount;
        public int activeLens;         // U8 in ViewSchema.cs; widened to int (no byte ViewScalar)
        public int activeLensCount;
    }

    // --- HudFactions row (ViewSchema.cs SectionHudFactions, "HudFactions"): Name/Grievance are
    // identity; Focused/Known/InLens are bool->byte ternaries (UndertowViewCallables.BoolToByte);
    // StrengthBand/Confidence are enum->byte casts (their own callables); RecentEventAge is an
    // identity U8 (widened to int, same as Hud.activeLens above). ---
    public struct UndertowHudFactionRow
    {
        public string name;
        public float grievance;
        [Projected(typeof(UndertowViewCallables), nameof(UndertowViewCallables.BoolToByte))]
        public int focused;            // byte widened to int; source: el.IsFocused ? 1 : 0
        [Projected(typeof(UndertowViewCallables), nameof(UndertowViewCallables.BoolToByte))]
        public int known;              // source: el.IsKnown ? 1 : 0
        [Projected(typeof(UndertowViewCallables), nameof(UndertowViewCallables.BoolToByte))]
        public int inLens;             // source: el.InLens ? 1 : 0
        [Projected(typeof(UndertowViewCallables), nameof(UndertowViewCallables.StrengthBandToByte))]
        public int strengthBand;       // source: (byte)el.StrengthBand
        [Projected(typeof(UndertowViewCallables), nameof(UndertowViewCallables.ConfidenceToByte))]
        public int confidence;         // source: (byte)el.Confidence
        public int recentEventAge;     // identity U8, widened to int
    }

    [View]
    public struct UndertowHudFactions
    {
        [ViewSection(Layout = ViewLayout.Soa)] public UndertowHudFactionRow[] rows;
    }

    // --- HudEvents row (ViewSchema.cs SectionHudEvents, "HudEvents"): Kind/Tick/PerpName/VictimName,
    // all identity 1:1 binds -- no transforms, no name mismatches. ---
    public struct UndertowHudEventRow
    {
        public string kind;
        public int tick;
        public string perpName;
        public string victimName;
    }

    [View]
    public struct UndertowHudEvents
    {
        [ViewSection(Layout = ViewLayout.Soa)] public UndertowHudEventRow[] rows;
    }

    // --- HudInspect (ViewSchema.cs SectionHudInspect, "HudInspect"): a single-row scalar section
    // (like Hud). Selected is a bool->byte ternary; MaxGrievance/Strength/TopRelName are identity;
    // topRelSig/cause carry NO value transform but their VIEW-FIELD NAMES differ from their C#
    // source-member names (TopRelSig <- el.TopRelSignificance; Cause <- el.CauseString) -- the
    // Milestone 1 Opus validator's finding that the [View] model has no "backing member name"
    // concept. Name-binding mechanism (this milestone's decision): a trivial identity [Projected]
    // callable per field (IdentityFloat/IdentityString) -- [Projected]'s (hostType, methodName)
    // constructor is the only per-field slot the model offers to attach ANY note about a field's
    // true provenance, so reusing it (even for a no-op transform) is the cleanest way to keep the
    // source-name traceability machine-readable rather than only in a comment. The adapter still
    // does the actual (identity) copy -- these callables exist purely for documentation parity with
    // the 3 real Bodies-side name mismatches (Mass, out of scope this milestone), not because the
    // writer path invokes them (same gap #2 caveat as every other [Projected] use in this file).
    [View]
    public struct UndertowHudInspect
    {
        [Projected(typeof(UndertowViewCallables), nameof(UndertowViewCallables.BoolToByte))]
        public int selected;           // source: el.Selected ? 1 : 0
        public string name;
        public float maxGrievance;
        public float strength;
        public string topRelName;
        [Projected(typeof(UndertowViewCallables), nameof(UndertowViewCallables.IdentityFloat))]
        public float topRelSig;        // NAME MISMATCH: source el.TopRelSignificance, no value transform
        [Projected(typeof(UndertowViewCallables), nameof(UndertowViewCallables.IdentityString))]
        public string cause;           // NAME MISMATCH: source el.CauseString, no value transform
    }
}
