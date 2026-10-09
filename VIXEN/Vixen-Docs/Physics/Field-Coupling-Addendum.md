---
title: Field Coupling Addendum
aliases: [Field Coupling, Sparse Field Coupling]
tags: [physics, fields, research]
created: 2026-10-09
related:
  - "[[README]]"
---

# Addendum: sparse fields as the coupling interface between physics and gameplay systems

**Owner direction (2026-10-09):** "using a heat field to let different physics / gameplay elements add to or be affected by a sparse heat field that acts as the intermediate between different systems without them needing to know about each other directly? for example kinetic field / magnetic field / pressure field etc? each only updated and maintained for domains of physics jobs that actually need them instead of being some kind of universal data layer? this can make lava causes water to turn to steam an emergent behaviour for example instead of hard wired interaction"

**Extends:**
- VIXEN `Vixen-Docs/03-Research/Voxel-Field-Physics-Research-2026-07.md` §5.5 *Sparse Field System*: `FieldLayer = semantic + region key + resolution + storage policy + lifetime + producer`, with heat, pressure, kinetic, electromagnetic and gravity among its candidate semantics;
- undertow `reports/research/2026-09-26-R11/research-R11-physics-library.md` §§2.1, 4.3: physical fields bound to the canonical region and address hierarchy; `FieldQuery`'s role kept as typed field-query callables; `DeltaWriter` replaced by ordinary typed outputs with generated invalidation; separate semantics for distance, occupancy, density, temperature, velocity and pressure.

What this adds: those documents treat a field as **storage for one simulation**. This addendum makes a field the **interface between simulations**. The idea is not a new data layer; it is a rule for how systems talk.

## 1. The idea in one paragraph
Systems never call or reference each other to interact physically. They **contribute to** and **sample from** shared sparse fields. Lava doesn't know about water: lava contributes heat. Water doesn't know about lava: water reads temperature (and pressure), and its **material phase rule** says what happens above its boiling point. Steam appears because the field crossed a threshold the material declares, not because anyone wrote "lava + water → steam". Every pairwise interaction becomes emergent from **N producers + M consumers + the material table**, instead of N×M hard-wired rules. A magnet doesn't know about iron filings: it contributes to a magnetic field, and anything whose material declares a magnetic response reads that field. An explosion doesn't know about crates, dust or doors: it writes a pressure and kinetic impulse.

## 2. Principles
1. **Fields are declared, typed semantics.** Each field (heat or enthalpy, pressure, kinetic impulse or velocity, magnetic, electric, gas composition, moisture, light or radiation, and so on) is one declared field kind, with:
   - unit and value shape;
   - combine law;
   - decay and diffusion law;
   - resolution ladder;
   - capacity policy (R213).

   They follow the existing schema-kind and generated-view forms (R11 §4.3 `PhysicalField` attached to `Region`). Their identity is never a renderer or GPU address.
2. **Producers and consumers declare their access, not their partners.**
   - A system declares "contributes heat in my region" or "samples temperature at my cells", as ordinary typed inputs and outputs.
   - Partners are never named. The schedule is derived from the read and write sets (the existing access-derived scheduler; R440 for any explicit precedence).
   - Coupling is therefore **horizontal through data**, per the vertical/horizontal dependency model, never a direct call.
3. **Demand-driven existence ("only where a job needs it").**
   - A field exists in a region only while at least one active producer or consumer there declares it, per brick or region and per field semantic.
   - When nothing contributes and nothing reads, the region's field relaxes to its ambient value and is deallocated (sleeping, not erasure, per R11).
   - Lifetime and residency follow the hot/cold working-set rule.
   - There is no world-wide universal layer. A cold, empty asteroid holds no heat field at all; it is ambient by definition.
4. **Contributions are combine-law operations, so they're order-free and deterministic.**
   - Each field declares how contributions merge: additive sources (heat, impulse), max or min, weighted average, vector sum.
   - Systems write contributions into a per-tick accumulation; the field solver integrates them (diffusion, advection, decay); consumers read the post-solve value.
   - Commutative, associative combines (or fixed reduction order) keep results deterministic for saves, replays and networking.
5. **Responses live in the material table, not in systems.**
   - Phase changes, ignition, magnetic susceptibility, conductivity and pressure response are **material properties** (R443 static data by material ref), for example `water: boils at T(P), latent heat L, becomes steam`.
   - A generic "material response" system evaluates those declared rules against sampled fields.
   - New interactions come from authoring materials, not writing code.
6. **Conservation where physics demands it.**
   - Heat or enthalpy and mass transfers are conserved: the energy that boils water is taken from the field (latent heat), so lava cools and boiling stops when energy runs out.
   - Fields are not free signals. Each field kind declares whether it is conserved (energy, mass) or a non-conserved potential or signal (light, magnetic potential).
7. **Resolution and level of detail per field and region.**
   - Each field picks its own resolution ladder (the R11 per-family scale ladders), independent of render resolution, from coarse compartment values (a room's temperature) to fine bricks near active interaction.
   - The bounds and reuse passes (R441/R442) skip regions whose field is provably ambient or unchanged.

## 3. How it sits on what exists
- **Storage:** the §5.5 `FieldLayer` storage policies (sparse region map, local brick set, mip or coarse grid, temporal ring, event-only impulse list), bound to canonical regions (R11 §4.3).
- **Static and dynamic data:** field values are **dynamic channels** (R443). Material response parameters are **static** material-table data.
- **Generated, single source:** field kinds, combine laws and response rules are authored. The solver kernels, storage, GPU layouts and render taps (heat shimmer, steam, glow) are generated (R447). No hand-written per-field plumbing.
- **Scheduling:** producer → solve → consumer phases come from the read and write sets. Propagation over networks (heat through pipe walls, pressure along a duct graph) can use the frontier kernel type (R451/R452) with blackboard-derived working memory.
- **Capacity:** every field and region store has a declared cap or ring (R213); exhaustion is a typed diagnostic, never a throw.
- **Material handling (`matflow`):** material in containers and pipes carries temperature and pressure as dynamic channels, so a pipe of water next to a furnace boils through the same mechanism as lava next to a lake.

## 4. Examples (emergent, not wired)
- **Lava next to water:** lava contributes heat; water samples temperature and pressure; the phase rule boils it, consuming latent heat; steam contributes to pressure and gas composition; the pressure field pushes light debris and particles. Lava cools and crusts when its heat drains.
- **A furnace:** a fuel material contributes heat while burning (its consumption rule). Ore in the furnace container samples temperature; its material rule melts it into slag and metal phases. A cooling pipe carrying water removes heat.
- **A magnet:** it contributes to the magnetic field; iron filings, ferrous ore and ship plates respond by their declared susceptibility; non-ferrous material ignores it.
- **An explosion:** event-only pressure and kinetic impulse contributions; dust and sand particles, loose items, doors and fluids each respond through their own material or body rules.
- **Wind or ventilation:** pressure differences between compartments produce flow; dust and gas advect with it; heat moves with the air.

## 5. Questions for the owner (when this becomes a design lane)
1. **Field catalogue for the first slice:** heat and pressure first (lava to steam is the witness), then kinetic, then magnetic?
2. **Conservation strictness:** exact energy accounting, or bounded-error energy for performance in large coarse fields?
3. **Where response rules live:** purely in the material table, or also allow per-object overrides (for example an insulated container)?
4. **Visibility:** do players see fields (a heat-vision overlay or gauges) or only their effects?
5. **Determinism level:** single-player replay, or lockstep networking (this decides between fixed-point and ordered float reductions)?

## 6. Witness ideas for the first slice
- **Emergence:** lava → steam with no lava- or water-specific code. Grep proof that neither system references the other; only field kinds and material rules.
- **Conservation:** heat removed from lava equals sensible plus latent heat gained by water/steam, within the declared tolerance.
- **Demand-driven allocation:** a region with no producers or consumers holds zero field storage; allocation and release are measured.
- **Determinism:** two runs with different system iteration orders produce byte-identical field states.
