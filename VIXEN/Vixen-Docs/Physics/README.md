---
title: Physics Module Docs
aliases: [Physics, VIXEN Physics, Physics Index]
tags: [physics, fields, simulation, index]
created: 2026-10-09
related:
  - "[[../03-Research/Voxel-Field-Physics-Research-2026-07]]"
  - "[[../01-Architecture/Kernel-Physics-Dispatch-Contract-Spec-2026-07]]"
---

# Physics Module Docs

The home for physics-specific design: physical fields and their coupling, material handling and flow, units, and the physics job domains. The owner asked on 2026-10-09 for physics docs to live in their own physics module section rather than spread across general architecture and research folders.

## Documents here

- [[Field-Coupling-Addendum]]: sparse typed fields (heat, pressure, kinetic, magnetic and so on) are the interface between physics and gameplay systems. Producers and consumers don't know about each other, and each field exists only where a physics job domain needs it. Emergent interactions come from shared fields (lava heats a region and water in that region turns to steam), not from wired pairs.
- [[Material-Handling-Matflow]]: the matflow research and design report covering physical material handling, depth tiers, shape-driven flow, exact mass ownership, and the sand-to-machine first slice.

## Existing physics docs elsewhere (not moved, to keep links stable)

- [[../03-Research/Voxel-Field-Physics-Research-2026-07]]: the field-native physics research; §5.5 *Sparse Field System* is what the addendum extends.
- [[../01-Architecture/Kernel-Physics-Dispatch-Contract-Spec-2026-07]]: the kernel physics dispatch contract.

## Governing rulings (undertow `docs/in-progress.md`)

- **R256:** the MPM-led material portfolio.
- **R449:** calculus tooling and gradient normals.
- **R454:** matflow Option A, with every recommendation accepted, including typed field coupling.
- **R457:** field facets.
- **R459, R460:** dimensional units and the unit decisions.
- **R461:** fields attach to a `Region` kind for open space and to existing spatial owners elsewhere.

## Where the code lives

- **Kernel:** the unit vocabulary and the field facets (`PhysicalUnitVocabulary`; `.Unit`, `.PhysicalQuantity`, `.Combine`, `.SparseFieldCapacity`), landed in batch b25 (P723).
- **Undertow content:** field kinds and the region owners (lane fieldsem); material accounting (lane matacct).
