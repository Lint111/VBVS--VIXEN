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
    // [ViewSection] here carries section-registry metadata only (Arg): this is a single-row scalar
    // section, so the generated registry builds it from frame.Hud[0], not the frame.Hud list. The
    // kernel round-trips the string verbatim (domain-blind); it does not move the ViewVersionHash.
    [View]
    [ViewSection(Arg = "frame.Hud[0]")]
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
        // Row-delta stable row key (undertow step-7b): the row's source EntityId (or synthetic id
        // for entity-less HudEvents rows), u64 on the wire — LAST column, matching undertow
        // ViewSchema.cs's VersionRowDelta ordering. Transport identity only: the kernel's
        // RmlDataModelEmitter deliberately SKIPS u64 fields in every Rml binding surface.
        public ulong rowId;
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
        // Row-delta stable row key (undertow step-7b): the row's source EntityId (or synthetic id
        // for entity-less HudEvents rows), u64 on the wire — LAST column, matching undertow
        // ViewSchema.cs's VersionRowDelta ordering. Transport identity only: the kernel's
        // RmlDataModelEmitter deliberately SKIPS u64 fields in every Rml binding surface.
        public ulong rowId;
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
    // Single-row scalar section (like Hud) — registry builds it from the guarded first element.
    [View]
    [ViewSection(Arg = "frame.HudInspect.Count > 0 ? frame.HudInspect[0] : default")]
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

    // --- Bodies row (ViewSchema.cs SectionBodies, "Bodies") -- Milestone 0 recipe-split
    // (View-ReadModel-Codegen-Plan-2026-07.md): Kind/OwnerInLens/OwnerRecentEventAge are identity
    // U8 (widened to int); Mass is identity F32 but its VIEW-FIELD NAME differs from its C# source-
    // member name (Mass <- el.MassKg, ALSO hiding a double->float narrowing -- the third name-
    // mismatch column the Milestone 1 Opus validator flagged, same IdentityFloat name-binding
    // mechanism as HudInspect.topRelSig/cause above); OrbitParent is the nullable-unwrap-with-
    // -1-sentinel transform (UndertowViewCallables.OrbitParentOrSentinel); RecipeId is identity U32
    // (widened to int -- ViewScalar has no uint type either, same widen-and-narrow-back discipline
    // as every U8 column in this file). RadiusAu is the per-BODY radius_AU component of the old
    // Vec3f RecipeParams, decomposed to a plain float column (see UndertowRecipes below for the
    // per-RECIPE relAmp/relCycles components + provider). RecipeProvider MOVED to UndertowRecipeRow
    // (it is a per-recipe attribute, not a per-body override -- confirmed: main.cpp's
    // ToBodyInstanceGpu reads provider per body but the sim (UndertowSim.ResolveRecipe) sets it per
    // matched recipe, never per-instance). Position (Vec3f) is NOT declared here -- it is genuinely
    // per-body and remains blocked on the struct-array-element Vector emitter gap (a separate
    // increment); see the file header and body_view.cpp's WarnBodiesPositionUnbackedOnce.
    //
    // Milestone 0 finding (verified by reading TypedAccessorEmitter.cs/ViewWriterEmitter.cs): the
    // "top-level array Vector already works" premise does NOT extend to struct-array ELEMENT
    // fields -- ViewWriterEmitter.cs:62 (`CsType(rf.Scalar.Value)`) and TypedAccessorEmitter.cs:159
    // (`CppType(ef.Scalar.Value)`) both unconditionally deref a null `Scalar` for a Vector-kind
    // element field. The genuine sidestep is decomposing radius_AU/relAmp/relCycles into plain
    // `float` element columns (this row's `radiusAu`, `UndertowRecipeRow`'s `relAmp`/`relCycles`) --
    // every column below is a plain scalar (int/float), so this schema needs the emitter gap fixed
    // NOWHERE. ---
    public struct UndertowBodyRow
    {
        public int kind;                // U8, widened to int; source: el.Kind
        [Projected(typeof(UndertowViewCallables), nameof(UndertowViewCallables.IdentityFloat))]
        public float mass;              // NAME MISMATCH + narrowing: source (float)el.MassKg (double)
        // Projected via IdentityInt, not OrbitParentOrSentinel: UndertowFrameAdapter.Bodies already
        // ran the nullable-unwrap+sentinel transform at WRITE time (el.Orbit.HasValue ? ParentBodyIndex
        // : -1), so the wire's stored cell IS the final -1-or-real-index value. Post-Yeroket-main-
        // 7d6c8b4e, the row-context typed accessor reads that stored cell and calls the [Projected]
        // callable on it directly (real cell first arg, row index second) -- re-running
        // OrbitParentOrSentinel here would treat the already-computed index as a fresh `hasOrbit`
        // bool and double-apply the sentinel logic. IdentityInt is the correct name-binding-only
        // callable, matching mass's IdentityFloat / HudInspect's cause/topRelSig pattern.
        [Projected(typeof(UndertowViewCallables), nameof(UndertowViewCallables.IdentityInt))]
        public int orbitParent;         // source: el.Orbit.HasValue ? el.Orbit.Value.ParentBodyIndex : -1
        public int ownerInLens;         // U8, widened to int; source: el.OwnerInLens
        public int ownerRecentEventAge; // U8, widened to int; source: el.OwnerRecentEventAge
        public int recipeId;            // U32, widened to int; source: el.RecipeId
        public float radiusAu;          // per-BODY: source el.RecipeParams.X (radius_AU); decomposed Vec3f component
        // Per-body on-rails position (AU), decomposed into three plain float columns — the SAME
        // emitter-gap sidestep as radiusAu above (a packed Vec3f struct-array-ELEMENT column still
        // null-refs ViewWriterEmitter/TypedAccessorEmitter; three scalars do not). Sources el.Position.
        // {X,Y,Z}. Closes the "every body renders at the origin" gap (body_view.cpp's
        // WarnBodiesPositionUnbackedOnce) without the undesigned Vec3f-scalar-field emitter work.
        public float posX;              // source el.Position.X (AU)
        public float posY;              // source el.Position.Y (AU)
        public float posZ;              // source el.Position.Z (AU)
        // Row-delta stable row key (undertow step-7b): the row's source EntityId (or synthetic id
        // for entity-less HudEvents rows), u64 on the wire — LAST column, matching undertow
        // ViewSchema.cs's VersionRowDelta ordering. Transport identity only: the kernel's
        // RmlDataModelEmitter deliberately SKIPS u64 fields in every Rml binding surface.
        public ulong rowId;
    }

    [View]
    public struct UndertowBodies
    {
        [ViewSection(Layout = ViewLayout.Soa)] public UndertowBodyRow[] rows;
    }

    // --- Recipes (Milestone 0, View-ReadModel-Codegen-Plan-2026-07.md, D0): normalizes the
    // per-recipe portion of the old per-body RecipeParams Vec3f -- one row per DISTINCT recipeId
    // actually in use in a frame (N bodies sharing M recipes send recipe params once per recipe, not
    // once per body). provider/relAmp/relCycles are per-recipe (set by UndertowSim.ResolveRecipe
    // from the matched render-recipe definition, never per-body-instance); recipeId is the join key
    // back to UndertowBodyRow.recipeId. All columns are plain scalars (no Vec3f) -- same emitter-gap
    // sidestep as UndertowBodyRow.radiusAu above. ---
    public struct UndertowRecipeRow
    {
        public int recipeId;    // U32, widened to int; join key -> UndertowBodyRow.recipeId
        public int provider;    // U8, widened to int; source: el.RecipeProvider (0=stored, 1=procedural)
        public float relAmp;    // per-recipe: source el.RecipeParams.Y (relAmp); decomposed Vec3f component
        public float relCycles; // per-recipe: source el.RecipeParams.Z (relCycles); decomposed Vec3f component
    }

    // Recipes is a DERIVED section (registry Arg = frame.Bodies): the recipe rows are a distinct-by-
    // recipeId projection of the bodies list, not a standalone frame collection. No rowId column ⇒
    // not delta-capable. Revision defaults to "Recipes" (its own SectionRevisions stamp).
    [View]
    [ViewSection(Arg = "frame.Bodies")]
    public struct UndertowRecipes
    {
        [ViewSection(Layout = ViewLayout.Soa)] public UndertowRecipeRow[] rows;
    }

    // ---------------------------------------------------------------------------------------------
    // slice6 (relational vertical slice, step 6): three row-bearing sections that mirror undertow's
    // ViewSchema.cs SectionBuildingFacets/SectionBuildingPower/SectionBuildingLabor (ids 6/7/8,
    // FormatVersion 12). Columns match the undertow SectionDefs EXACTLY (name, kind, order; the U64
    // key `rowId` LAST, mirroring UndertowBodyRow/UndertowHudFactionRow). No new ViewScalar kind is
    // introduced -- every column is F32/I32/U8/U32/U64/Str, all shipped (U64 landed with the
    // row-delta arc, ViewScalar.U64). These are the C++-reader half of the M-wire lockstep: the
    // generated read-model decodes the wire the hand-authored undertow writers emit, and the kernel
    // --view-writer regen recomputes the structural SchemaVersion hash that replaces undertow's
    // M-wire placeholder writer files.

    // --- BuildingFacets row (ViewSchema.cs SectionBuildingFacets, "BuildingFacets"): the lightweight
    // facet-index + ownership row. def is the building definition id (identity Str); flags is the
    // BuildingFacet bitset (identity U8, widened to int -- no byte ViewScalar); owner is the owning
    // faction's EntityId (U64); isOwnerViewer is the §5 owner-only-affordance flag (bool->byte
    // widened to int). Topology-cadence data (§7.10), keyed by rowId = Building.Id. ---
    public struct UndertowBuildingFacetRow
    {
        public string def;              // identity Str; source: BuildingFacetView.Def id
        public int flags;              // U8 widened to int; source: (byte)BuildingFacetView.Facets
        public ulong owner;            // U64; source: BuildingFacetView.Owner (owning faction EntityId)
        public int isOwnerViewer;      // bool->byte widened to int; source: IsOwnerViewer ? 1 : 0
        // Row-delta stable row key: the building's source EntityId, u64 on the wire -- LAST column,
        // matching undertow ViewSchema.cs's VersionBuildingRelational ordering. Transport identity
        // only: the kernel's RmlDataModelEmitter SKIPS u64 fields in every Rml binding surface.
        public ulong rowId;
    }

    [View]
    public struct UndertowBuildingFacets
    {
        [ViewSection(Layout = ViewLayout.Soa, Revision = "Buildings")] public UndertowBuildingFacetRow[] rows;
    }

    // --- BuildingPower row (ViewSchema.cs SectionBuildingPower, "BuildingPower"): per-building power
    // telemetry (§7.10 load+generation+membership+impact). demand/generated/stored/net are F32;
    // connected is a bool->byte (widened to int); impact is the BuildingImpact enum (Ok/Throttled/
    // Halted) as a byte (widened to int). Hot channel (changes every settlement), keyed by rowId. ---
    public struct UndertowBuildingPowerRow
    {
        public float demand;           // input kg/tick the recipe draws
        public float generated;        // GeneratedPowerSupply share at the place
        public float stored;           // battery StoreFlowState.Charge
        public float net;              // NetFlow(faction, place, power)
        public int connected;          // bool->byte widened to int; place connectivity
        public int impact;             // BuildingImpact widened to int (0=Ok,1=Throttled,2=Halted)
        public ulong rowId;            // key: Building.Id
    }

    [View]
    public struct UndertowBuildingPower
    {
        [ViewSection(Layout = ViewLayout.Soa, Revision = "Buildings")] public UndertowBuildingPowerRow[] rows;
    }

    // --- BuildingLabor row (ViewSchema.cs SectionBuildingLabor, "BuildingLabor"): per-building labor
    // occupancy (§7.10 workforce:building-occupancy). supply/need are F32; needMet is a bool->byte
    // (widened to int). Hot channel, keyed by rowId = Building.Id. ---
    public struct UndertowBuildingLaborRow
    {
        public float supply;           // NetFlow/LaborSupplyAt labor at the place
        public float need;             // recipe's labor need
        public int needMet;            // bool->byte widened to int; whether the labor need is met
        public ulong rowId;            // key: Building.Id
    }

    [View]
    public struct UndertowBuildingLabor
    {
        [ViewSection(Layout = ViewLayout.Soa, Revision = "Buildings")] public UndertowBuildingLaborRow[] rows;
    }
}
