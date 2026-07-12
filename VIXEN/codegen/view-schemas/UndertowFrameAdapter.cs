// Inc-5 Milestone 3 (Task 4): the hand-written adapter that populates the schema-driven
// UndertowHud*ViewWriter types from a real undertow SimFrame, performing the SAME transforms
// ViewSchema.cs's Source expressions declare. This is where gap #2's resolution lives:
// ViewWireFormat's generated ToBuffer() never dispatches [Projected] (only the RmlUi C++ face
// does), so this adapter calls the SAME UndertowViewCallables methods each transform column's
// [Projected] attribute names, guaranteeing the two can never semantically drift apart even
// though only THIS file's calls actually execute in the writer path.
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
    }
}
