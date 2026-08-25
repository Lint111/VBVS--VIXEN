// Inc-5 Milestone 3 (Task 4): the hand-written adapter that populates the schema-driven
// UndertowHud*ViewWriter types from a real undertow SimFrame, performing the SAME transforms
// ViewSchema.cs's Source expressions declare. This is where gap #2's resolution lives:
// The generated writer does not invoke callable metadata, so this adapter calls the relevant
// UndertowViewCallables methods directly before serialization; only THIS file's calls execute in
// the writer path.
//
// NOT itself codegen output -- hand-written, one-time proof-harness code, not committed to the
// VIXEN build. Lives in codegen/view-schemas/ alongside the schemas it adapts for discoverability;
// referenced directly by the Milestone 3 proof harness (a standalone console project, not part of
// the VIXEN or Yeroket build graphs).
using System.Collections.Generic;
using Undertow.Sim;
using Vixen.Views;

namespace Vixen.ViewSchemas
{
    public static class UndertowFrameAdapter
    {
        public static UndertowHudViewWriter Hud(HudView el)
        {
            var w = new UndertowHudViewWriter();
            w.tick = el.Tick;
            w.bodyCount = el.BodyCount;
            w.activeLens = el.ActiveLens;            // identity U8->int widen
            w.activeLensCount = el.ActiveLensCount;
            return w;
        }

        public static UndertowHudFactionsViewWriter HudFactions(IReadOnlyList<HudFactionView> factions)
        {
            var w = new UndertowHudFactionsViewWriter();
            foreach (var el in factions)
            {
                w.rows.Add(new UndertowHudFactionsViewWriter.UndertowHudFactionRowRow
                {
                    name = el.Name,
                    grievance = el.Grievance,
                    focused = UndertowViewCallables.BoolToByte(el.IsFocused),
                    known = UndertowViewCallables.BoolToByte(el.IsKnown),
                    inLens = UndertowViewCallables.BoolToByte(el.InLens),
                    strengthBand = UndertowViewCallables.StrengthBandToByte((int)el.StrengthBand),
                    confidence = UndertowViewCallables.ConfidenceToByte((int)el.Confidence),
                    recentEventAge = el.RecentEventAge,   // identity U8->int widen
                });
            }
            return w;
        }

        public static UndertowHudEventsViewWriter HudEvents(IReadOnlyList<HudEventView> events)
        {
            var w = new UndertowHudEventsViewWriter();
            foreach (var el in events)
            {
                w.rows.Add(new UndertowHudEventsViewWriter.UndertowHudEventRowRow
                {
                    kind = el.Kind,
                    tick = el.Tick,
                    perpName = el.PerpName,
                    victimName = el.VictimName,
                });
            }
            return w;
        }

        public static UndertowHudInspectViewWriter HudInspect(HudInspectView el)
        {
            var w = new UndertowHudInspectViewWriter();
            w.selected = UndertowViewCallables.BoolToByte(el.Selected);
            w.name = el.Name;
            w.maxGrievance = el.MaxGrievance;
            w.strength = el.Strength;
            w.topRelName = el.TopRelName;
            w.topRelSig = UndertowViewCallables.IdentityFloat(el.TopRelSignificance);   // name mismatch only
            w.cause = UndertowViewCallables.IdentityString(el.CauseString);             // name mismatch only
            return w;
        }

        // PARTIAL (gap #4): only the 7 representable Bodies columns. Position/RecipeParams/OrbitPath
        // (Vec3f/ListVec3f) are not populated here -- out of scope this milestone.
        public static UndertowBodiesViewWriter Bodies(IReadOnlyList<BodyView> bodies)
        {
            var w = new UndertowBodiesViewWriter();
            foreach (var el in bodies)
            {
                w.rows.Add(new UndertowBodiesViewWriter.UndertowBodyRowRow
                {
                    kind = (byte)el.Kind,
                    mass = UndertowViewCallables.IdentityFloat((float)el.MassKg),   // name mismatch + double->float narrowing
                    orbitParent = UndertowViewCallables.OrbitParentOrSentinel(
                        el.Orbit.HasValue, el.Orbit.HasValue ? el.Orbit.Value.ParentBodyIndex : -1),
                    ownerInLens = el.OwnerInLens,
                    ownerRecentEventAge = el.OwnerRecentEventAge,
                    recipeProvider = el.RecipeProvider,
                    recipeId = (int)el.RecipeId,
                });
            }
            return w;
        }
    }
}
