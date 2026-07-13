# Sampled Lighting Inc7 — Mip/Tier-Derived Probe Density — Scoping Note (2026-07)

**Status:** NOT a plan — a scoping note capturing a real idea before it's lost, for a future increment after Inc5 (amortized update + tuned defaults) ships. User-flagged 2026-07-13: "we should also have mip level derived probes, so that we get proper mip capabilities using the efficient space handling for probes."

## The idea

DDGI's probe grid today (Inc4, shipped) is **uniform**: fixed `spacingX/Y/Z`, fixed `countX/Y/Z`, every probe the same density everywhere in the covered volume, regardless of how much actual geometric/lighting detail exists nearby. This is the simplest possible placement and was Inc4's own deliberate scope choice (design doc §6 open-decision #2: "uniform grid only... revisit when Tiered-ESVO's scale ambitions meet lighting").

VIXEN already has a real, shipped mip/tier structure for voxel content itself — Sparse-Mip ESVO LOD (Inc1+Inc2, merged) and Tiered-ESVO (Inc1+Inc2, merged, surface-to-orbit tier-crossing traversal) both exist and are live. The idea: derive probe placement DENSITY from that SAME existing LOD structure, rather than a flat uniform grid — coarser probe spacing (fewer probes, larger spacing) in coarse/distant/low-detail tiers, finer spacing near actual surface/geometric detail — mirroring exactly how the voxel content itself is already stored and traversed at variable resolution. This is the standard "clipmap"/"cascaded" probe idea from DDGI/RTXGI literature (explicitly named but deferred in Inc4's own design doc), now grounded against VIXEN's OWN already-existing tiering mechanism rather than a generic cascade scheme bolted on separately.

## Why this is structurally bigger than Inc5

Inc5 (amortized update + tuned scalar defaults) is **parametric**: same uniform grid shape, fewer active probes/frame, fewer rays/probe, tuned counts. Zero changes to grid addressing, atlas packing, or how a shading point finds its probes.

Mip-derived density is **non-uniform grid addressing** — a materially different data structure, touching:
- **Placement**: probe positions no longer a simple `origin + index*spacing` arithmetic — likely keyed to the same tier/brick-pool residency structure Tiered-ESVO already uses, so probe density naturally follows where content actually IS materialized/resident, not a fixed world-space box.
- **Atlas packing**: the current atlas layout formula (`atlasWidth = countX*countY*texelsPerProbe`, uniform block-per-probe) assumes a flat count — a non-uniform/tiered probe set needs either a different packing scheme (e.g. per-tier sub-atlases) or an indirection table (probe index → atlas texel block) instead of the current direct arithmetic mapping.
- **Gather-side lookup**: `SpatialReuseShade.comp`'s trilinear 8-probe gather (Inc4 M5) currently assumes "find the 8 uniform-grid corners around this shading point" via simple `floor`/`frac` arithmetic on world position. A tiered grid needs "find which TIER this shading point's LOD falls into, then find that tier's 8 nearest probes" — a materially more complex lookup, likely reusing whatever tier-resolution logic Tiered-ESVO's own traversal already has for voxel LOD selection (the natural analog: "which tier does this point's solid-angle/distance-from-camera fall into," per the standing `Tiered-ESVO-Observer-Address-Direction` memory's own LOD-gate generalization work).
- **Staleness/hysteresis story**: probes near a tier BOUNDARY (where content residency crosses from one tier to another, e.g. during a fly-in/fly-out per Tiered-ESVO's own "surface-to-orbit" gate) need a defined behavior — does probe density itself change live as tiers cross, and if so what happens to a probe's accumulated hysteresis history when its effective spacing changes underneath it? This is a genuinely new class of question Inc4's uniform, static grid never had to answer.

## Why sequence it AFTER Inc5, not instead of it

- Inc5 is the cheap, universal, low-risk fix that helps every machine immediately (per the `DDGI-HWRT-Acceleration-And-MultiQueue-Direction-2026-07.md`'s own sequencing recommendation) — it should not wait on a structurally bigger redesign.
- Inc7 inherits Inc5's already-tuned per-probe cost (fewer rays, amortized cadence) as ITS OWN starting baseline — the two changes are orthogonal levers (how many probes update, at what density, vs. how many total probes exist and where) and gating them together would make it hard to attribute a measured win to the right lever.
- Mip-derived density is a genuine research/design question in its own right (how exactly does "tier" map to "probe spacing" — a direct 1:1 with the octree's own tier boundaries? A separate, coarser cascade level count? Does it interact with the direction doc's own deferred HW-RT/multi-queue axes at all?) that deserves its own scoping/design pass before a milestone map is written, the same way Inc4 itself started from a grounding investigation before any Task list existed.

## Open questions for a future scoping pass (not answered here)

1. Does probe density map 1:1 to the octree's existing tier boundaries, or does it need its own independent cascade-level count (potentially coarser or finer than the voxel LOD's own tiering)?
2. How does the atlas repack when tier boundaries move (e.g. camera fly-in changes which tier is "near")? Fully static per-tier sub-atlases sized for a worst case, or a genuinely dynamic/reflowing packing scheme?
3. What is the correct hysteresis behavior for a probe whose effective density/neighbors change as tier residency shifts — reset its history, blend across a tier transition, or something else?
4. Does this compose with, complicate, or get superseded by the HW-RT acceleration axis (direction doc §3) — e.g. does a non-uniform probe placement change the BLAS/TLAS design for probe rays if/when Tier-1 ray-query lands?

None of this is scheduled. This note exists so the idea isn't lost between now and whenever Inc5 ships and a real Inc7 scoping conversation happens.

## Related

- `Sampled-Lighting-Inc5-Plan-2026-07.md` — ships first; Inc7 inherits its tuned baseline.
- `Sampled-Lighting-Design-2026-07.md` §6 open-decision #2 — the original "uniform grid only, revisit later" deferral this note now revisits.
- `Tiered-ESVO-Observer-Addressing-Design-2026-07.md` — the existing tier/LOD structure this note proposes deriving probe density FROM, rather than inventing a separate cascade scheme.
- `DDGI-HWRT-Acceleration-And-MultiQueue-Direction-2026-07.md` — the broader sequencing this note's own "why after Inc5" reasoning is drawn from.
