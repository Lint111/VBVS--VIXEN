# Sky-Point / Light-Tree Integration — Scoping Note (2026-07)

**Status:** NOT a plan — a scoping note capturing a real idea + its real prerequisite chain, for a future increment. User-flagged 2026-07-13: shouldn't the star/sky-point render be hooked in as the extreme far end of the same mip-averaged light-tree aggregation used for every other light source in the scene, rather than being a separate system?

## The idea, confirmed architecturally sound (investigated 2026-07-13)

VIXEN's mip-averaged emissive light-tree (Sampled Lighting Inc3) already does exactly the aggregation this idea wants — `BuildLightTreeCut` walks an octree's own emissive mip pyramid top-down and cuts (stops descending) once a coarse node's aggregate power is small enough, turning "a million glowing voxels" into "a handful of coarse light-tree nodes." That is precisely the mechanism a distant object's light needs: a star, seen from far enough away, IS just one coarse aggregate emissive node.

The current sky-point/star renderer (`SkyProjectionNode`, Tiered ESVO Observer Addressing Inc1 M3) is a **wholly separate, currently-synthetic** system — confirmed via direct source read: `ComposeLocalDirection`'s only call site is `SkyProjectionNode::BuildSyntheticFixture()`, explicitly labeled "NOT THE PRODUCTION DATA PATH," with hardcoded `intrinsicBrightness` literals and no relationship to any real emission/voxel data anywhere. It shares zero code, zero data, and zero render-graph connection with the light-tree pipeline.

## Why they're separate today — and it's a REAL reason, not an oversight

The light-tree's cut is strictly bounded to one already-baked, GPU-resident octree's own mip pyramid — it has no concept of "beyond this tree" and cannot represent content that isn't currently baked. The Tiered-ESVO design doc's own §6/§7 explicitly scopes distant tier content (system → galaxy scale) as **procedurally-implied, addressed by `(TierAddress, seed)` alone, and only materialized/baked once it becomes tier-crossing-resolvable** — "VIXEN never stores the universe." So today, a genuinely distant star has no baked octree, no emissive mip pyramid, and therefore nothing for a light-tree cut to walk — the light-tree literally cannot represent it yet, independent of any wiring work.

## The real prerequisite chain (this is the important finding)

User's own follow-up (2026-07-13) named the missing piece precisely: **the lazy-procedural/recipe-provider program needs to actually have real lazy-materializable data before there's anything real to hook a star's light-tree cut into.**

Checked against the standing project memory (`lazy-procedural-delta-baseline-program`, `tiered-esvo-observer-address-direction`):

- **Good news: Inc0+Inc1 of the Lazy-Procedural Delta-Baseline program are ALREADY DONE and merged** (2026-07-11, `feat/lazy-baseline-inc0-inc1`, folded into main). This includes the "zero-bake GPU-direct" thesis — recipes splice into the runtime-compiled march shader, structurally proven to reach zero bake calls for pure-recipe bodies. So the underlying "materialize content on demand from an instruction/recipe, not eagerly" mechanism the star idea needs conceptually already exists, for content that's ALREADY addressed and requested.
- **The actual missing piece is narrower and still open**: `TierRef`/`TierRefTable` — the mechanism that lets a **far-tier, not-yet-resident** object (a star three tiers up, with no octree at all yet) exist as a real, addressable reference in the first place, so that "this star becomes resident" is an event with a defined trigger and a defined transition into something the light-tree CAN walk. Per the design doc's own §9, this is explicitly still unscheduled. Without it, there's no principled way to say "this TierAddress just became resident, bake it, and now feed its light-tree cut node into the sky-point buffer" — there's no TierRef state machine to hang that transition off of.
- **KI-LPD-001** (open, non-blocking on the lazy program's own close-out) — domain-modifier recipes (Twist/Bend/Mirror/Repeat) render `virtualHits=0` GPU-direct due to a step-relax overshoot bug. Not directly blocking for a star (likely a simple leaf/CSG recipe, not a domain-modified one), but worth knowing the zero-bake path has a known gap in modifier coverage.

## Revised integration shape (once TierRef lands)

1. A far-tier candidate object (a star, per its own `TierAddress`) starts as a pure address + procedural seed — exactly what `SkyProjectionNode`'s CURRENT synthetic fixture stands in for, except real instead of hardcoded.
2. `TierRef` residency transition fires (object becomes resolvable/relevant — tier-crossing, proximity, or an explicit query) → the lazy-procedural pipeline (already built, Inc0+Inc1) materializes/bakes it on demand, zero-bake GPU-direct where recipe-representable.
3. Once baked, it has a real emissive mip pyramid → `BuildLightTreeCut` can walk it → its coarsest cut node(s) become its light-tree representation.
4. **The actual sky-point integration point**: feed that coarsest cut node (or a small aggregate of the object's top-level cut nodes) into the `SkyPointBuffer` as its `SkyPoint` entry — replacing the fixture's hardcoded `intrinsicBrightness` with the light-tree node's real `intensity`/`coverage`/`worldExtent` aggregate, run through the existing `TierMagnitude.h::ApparentMagnitude` falloff (which already accepts an `intrinsicBrightness` parameter — just needs a REAL source instead of a literal).
5. For content that's genuinely still non-resident (too far/irrelevant to ever bake) — the sky-point system's current "address + procedural seed, no materialized geometry" shape remains exactly correct as-is; that's the right representation for content the light-tree structurally cannot ever reach. The integration only replaces the SYNTHETIC placeholder for objects that HAVE crossed into residency, not the whole system.

## Sequencing

This is NOT close — it sits behind `TierRef`/`TierRefTable` (still unscheduled), which itself is the natural infrastructure prerequisite for Tiered-ESVO's own further scale work generally, not just this idea. Do not scope a plan for this integration until `TierRef` has its own increment. This note exists so the idea (and its correct dependency order) isn't lost between now and whenever that happens.

## Related

- `Sampled-Lighting-Inc7-Scoping-Note-2026-07.md` — a sibling deferred idea (mip/tier-derived probe density) with the exact same "depends on Tiered-ESVO's own further scale work" shape — worth scoping together once `TierRef` lands, since both consume the same underlying tier-residency transition machinery.
- `Tiered-ESVO-Observer-Addressing-Design-2026-07.md` §6/§7/§9 — the design doc sections this note's "why separate today" and "TierRef is the missing piece" reasoning is drawn from directly.
- Project memory `lazy-procedural-delta-baseline-program` — Inc0+Inc1 DONE (the zero-bake mechanism this idea will reuse once TierRef exists); KI-LPD-001 (open, minor, domain-modifier recipes).
- `DDGI-HWRT-Acceleration-And-MultiQueue-Direction-2026-07.md`, `Sky-Depth-Test-Fix-Plan-2026-07.md` — other queued/deferred work in this same render-graph area; none of these block or are blocked by this note.
