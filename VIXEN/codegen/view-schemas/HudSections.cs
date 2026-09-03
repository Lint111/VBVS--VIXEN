// viewrestore: the VIXEN-side [View] mirrors of undertow's live per-section HUD wire faces, so the
// engine can decode each section by its OWN structural version through the section-composition query
// layer (view_query.h) — instead of the retired monolithic merged-Hud read whose single version hash
// rejected the whole frame (0x2EEF0434 vs 0x886D047D).
//
// These are NOT new readmodels and NOT a producer contract: undertow ALREADY writes these exact
// sections every frame (core/src/Undertow.View/Generated/UndertowHud{Factions,Events,Inspect},
// UndertowBuilding{Power,Labor}.ViewWriter.g.cs, dispatched by ViewSectionSpanWriter as container
// sections 1/2/3/7/8). Each struct here reproduces that section's committed structural hash
// (ViewVersionHash.Compute hashes the OUTER struct name + each field's name|kind|layout, recursing
// element fields; nested struct names are NOT hashed) so ViewWireReaderSoa::Apply accepts the wire:
//
//   UndertowHudFactions  0xCC4C8C33   rows[]: name,grievance,focused,known,inLens,strengthBand,
//                                             confidence,recentEventAge,rowId
//   UndertowHudEvents    0xB2C48BDD   rows[]: kind,tick,perpName,victimName,rowId
//   UndertowHudInspect   0x398FCD18   (scalar) selected,name,maxGrievance,strength,topRelName,
//                                             topRelSig,cause
//   UndertowBuildingPower 0x302BE8A8  rows[]: demand,generated,stored,net,connected,impact,rowId
//   UndertowBuildingLabor 0xC9C17A25  rows[]: supply,need,needMet,rowId
//
// The undertow wire widens every bool column to int (the derived-query bool->int compute) and every
// EntityId row key to U64 — mirrored here as `int`/`ulong` so the kinds hash identically. The row
// sections wrap their columns in an outer `[ViewSection(Soa)] Row[] rows` member, matching the
// derived-rows lowering (ViewModelBuilder.FromDerivedRowsShape); the inspect section is a flat
// one-row scalar (FromDerivedScalarShape), like Hud itself.
using Yeroket.Util.KernelFramework;

namespace Vixen.ViewSchemas
{
    // --- section 0: Hud root scalars (0x2EEF0434) — tick/bodyCount/activeLens/activeLensCount.
    //     The surviving Hud root the retired monolithic ReadHudView used to decode; queried here as
    //     its own section so the root scalars ride the same per-section-versioned path. Named
    //     UndertowHudRoot (NOT UndertowHud) so it does not collide with the VIXEN merged `Hud`
    //     [View]; the version hash is over the OUTER name + fields, and 0x2EEF0434 was minted from
    //     the container name "UndertowHud" — so this struct is named to reproduce that hash below. ---
    [View]
    public struct UndertowHud {
        public int tick;
        public int bodyCount;
        public int activeLens;
        public int activeLensCount;
    }

    // --- section 1: HudFactions (0xCC4C8C33) ---
    public struct UndertowHudFactionRow {
        public string name;
        public float  grievance;
        public int    focused;          // bool->int on the wire
        public int    known;            // bool->int
        public int    inLens;           // bool->int
        public int    strengthBand;     // StrengthBand enum->int
        public int    confidence;       // Confidence enum->int
        public int    recentEventAge;   // byte->int; ticks-since-event capped 255 (the RESTORED richer field)
        public ulong  rowId;            // faction EntityId (the RESTORED join key)
    }
    [View]
    public struct UndertowHudFactions {
        [ViewSection(Layout = ViewLayout.Soa)] public UndertowHudFactionRow[] rows;
    }

    // --- section 2: HudEvents (0xB2C48BDD) ---
    public struct UndertowHudEventRow {
        public string kind;
        public int    tick;
        public string perpName;
        public string victimName;
        public ulong  rowId;
    }
    [View]
    public struct UndertowHudEvents {
        [ViewSection(Layout = ViewLayout.Soa)] public UndertowHudEventRow[] rows;
    }

    // --- section 3: HudInspect (0x398FCD18) — flat one-row scalar section ---
    [View]
    public struct UndertowHudInspect {
        public int    selected;         // bool->int (gates the panel data-if)
        public string name;
        public float  maxGrievance;     // RESTORED
        public float  strength;         // RESTORED
        public string topRelName;       // RESTORED
        public float  topRelSig;        // RESTORED (top-relationship significance)
        public string cause;
    }

    // --- section 7: BuildingPower (0x302BE8A8) ---
    public struct UndertowBuildingPowerRow {
        public float demand;
        public float generated;
        public float stored;
        public float net;
        public int   connected;         // bool->int
        public int   impact;            // BuildingImpact enum->int
        public ulong rowId;             // Building.Id — the predicate key for the inspector
    }
    [View]
    public struct UndertowBuildingPower {
        [ViewSection(Layout = ViewLayout.Soa)] public UndertowBuildingPowerRow[] rows;
    }

    // --- section 6: BuildingFacets (0x657E4C79) — def/owner/isOwnerViewer, joined to power+labor
    //     by rowId to give the building inspector its identity + ownership header. ---
    public struct UndertowBuildingFacetRow {
        public string def;             // building definition id (e.g. "core:power_plant")
        public int    flags;           // BuildingFacet bitset
        public ulong  owner;           // owning faction EntityId
        public int    isOwnerViewer;   // bool->int: the §5 owner-only-affordance flag
        public ulong  rowId;           // Building.Id
    }
    [View]
    public struct UndertowBuildingFacets {
        [ViewSection(Layout = ViewLayout.Soa)] public UndertowBuildingFacetRow[] rows;
    }

    // --- section 8: BuildingLabor (0xC9C17A25) ---
    public struct UndertowBuildingLaborRow {
        public float supply;
        public float need;
        public int   needMet;           // bool->int
        public ulong rowId;             // Building.Id — the predicate key for the inspector
    }
    [View]
    public struct UndertowBuildingLabor {
        [ViewSection(Layout = ViewLayout.Soa)] public UndertowBuildingLaborRow[] rows;
    }
}
