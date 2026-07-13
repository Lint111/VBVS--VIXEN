// Inc-5 Milestone 3 (Task 4): schema-driven [View] declarations that re-derive undertow's REAL
// ViewSchema.cs sections (core/src/Undertow.View/ViewSchema.cs) -- Hud, HudFactions, HudEvents,
// HudInspect, and (PARTIAL) Bodies. Bodies.Position/RecipeParams (Vec3f SCALARS) and
// Bodies.OrbitPath (a nullable ListVec3f) are explicitly OUT of scope this milestone (gap #4,
// plan doc Progress Log) -- neither shape is representable by the shipped [View] model, which only
// supports int/float/bool/string scalars and T[] of scalar-only structs. UndertowBodies below
// declares only the 7 REPRESENTABLE Bodies columns (Kind, Mass, OrbitParent, OwnerInLens,
// OwnerRecentEventAge, RecipeProvider, RecipeId) per the controller's option-1 decision -- Vec3f-
// scalar-field and ListVec3f-field support as new [View] model shapes is real, undesigned Yeroket
// kernel-framework mechanism work, not something to force via a flatten-to-3-floats workaround
// (see the Follow-ups entry for this gap).
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

    // --- Bodies row, PARTIAL (ViewSchema.cs SectionBodies, "Bodies") -- gap #4: only the 7
    // REPRESENTABLE columns are declared. Kind/OwnerInLens/OwnerRecentEventAge/RecipeProvider are
    // identity U8 (widened to int); Mass is identity F32 but its VIEW-FIELD NAME differs from its
    // C# source-member name (Mass <- el.MassKg, ALSO hiding a double->float narrowing -- the third
    // name-mismatch column the Milestone 1 Opus validator flagged, same IdentityFloat name-binding
    // mechanism as HudInspect.topRelSig/cause above); OrbitParent is the nullable-unwrap-with-
    // -1-sentinel transform (UndertowViewCallables.OrbitParentOrSentinel); RecipeId is identity U32
    // (widened to int -- ViewScalar has no uint type either, same widen-and-narrow-back discipline
    // as every U8 column in this file). Position, RecipeParams (Vec3f scalars) and OrbitPath
    // (ListVec3f) are NOT declared here -- see the file header and the plan doc's gap #4 entry.
    //
    // BLOCKED (Milestone 2.5, 2026-07-13): Position/RecipeParams CANNOT be added as columns of this
    // row struct today. Bodies.rows is a [ViewSection(Layout = ViewLayout.Soa)] struct-ARRAY, so
    // Position/RecipeParams would have to be STRUCT-ARRAY-ELEMENT fields (per-row columns), not
    // top-level scalar fields on UndertowBodies. Milestone 2.4 only wired ViewFieldKind.Vector
    // support for TOP-LEVEL scalar fields (proven via the top-level-only VectorProof.cs schema) --
    // its own Scope boundary explicitly named the struct-array-element case as an "ALSO-flagged-
    // but-unreachable-today NPE landmine", deliberately left unfixed. Confirmed BY ACTUALLY RUNNING
    // the CLI with Position/RecipeParams added as UndertowBodyRow columns: --view-writer crashes
    // with an unhandled `System.InvalidOperationException: Nullable object must have a value` at
    // ViewWriterEmitter.cs:62 (`CsType(rf.Scalar.Value)` -- rf.Scalar is null for a Vector-kind row
    // field, per ViewModel.cs's Classify). --typed-accessor-cpp's struct-array element loop
    // (TypedAccessorEmitter.cs:159, `CppType(ef.Scalar.Value)`) has the identical latent NPE on the
    // read side, not yet independently confirmed by running it (the writer crash already blocks).
    // This is a genuine, previously-unaddressed emitter gap -- NOT decided/worked around here, per
    // this milestone's "report a genuine design gap rather than silently deciding something
    // material" instruction. Bodies.Position/RecipeParams stay OUT OF SCOPE this milestone; the 4
    // Hud-family sections are unaffected (no Vector fields, no struct-array-element Vector case). ---
    public struct UndertowBodyRow
    {
        public int kind;                // U8, widened to int; source: el.Kind
        [Projected(typeof(UndertowViewCallables), nameof(UndertowViewCallables.IdentityFloat))]
        public float mass;              // NAME MISMATCH + narrowing: source (float)el.MassKg (double)
        [Projected(typeof(UndertowViewCallables), nameof(UndertowViewCallables.OrbitParentOrSentinel))]
        public int orbitParent;         // source: el.Orbit.HasValue ? el.Orbit.Value.ParentBodyIndex : -1
        public int ownerInLens;         // U8, widened to int; source: el.OwnerInLens
        public int ownerRecentEventAge; // U8, widened to int; source: el.OwnerRecentEventAge
        public int recipeProvider;      // U8, widened to int; source: el.RecipeProvider
        public int recipeId;            // U32, widened to int; source: el.RecipeId
    }

    [View]
    public struct UndertowBodies
    {
        [ViewSection(Layout = ViewLayout.Soa)] public UndertowBodyRow[] rows;
    }
}
