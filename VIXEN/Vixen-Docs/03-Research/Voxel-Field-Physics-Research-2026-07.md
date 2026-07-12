---
title: Voxel Field Physics Research
aliases: [Field Physics, Voxel Physics Architecture, VIXEN Physics]
tags: [research, physics, voxel, sdf, fields, simulation, architecture]
created: 2026-07-10
status: research direction
related:
  - "[[../01-Architecture/Lazy-Procedural-Delta-Baseline-Design-2026-07]]"
  - "[[../01-Architecture/Kernel-Physics-Dispatch-Contract-Spec-2026-07]]"
  - "[[../01-Architecture/Voxel-Content-Format-Contract-Design-2026-06]]"
  - "[[../01-Architecture/Destructible-Body-Rendering-Direction-2026-06]]"
  - "[[../05-Progress/Production-Roadmap-2026]]"
  - "[[../_archive/2026-07/feature-proposal-plans/soft-body-voxel-physics-design]]"
---

# Voxel Field Physics Research

> [!summary]
> VIXEN physics should be **field-native and voxel-aware**, not mesh-backed. Meshes may exist as
> export/debug/cache artifacts, but simulation authority must stay in recipes, SDFs, voxel assets,
> materialized deltas, sparse fields, and region identities. External physics engines are useful as
> references or optional solvers, not as the canonical data model.

## 1. Context

The current VIXEN direction is instructions-first: scene content is described by recipe programs,
params/seeds, deltas, and stored voxel assets. Materialized bricks are cache state, not the source of
truth. A physics system that depends on triangle meshes would insert a lossy middle representation:

```text
recipe/SDF/voxel delta -> mesh extraction -> mesh collision -> physics -> voxel/SDF update
```

That loop is structurally wrong for VIXEN. The preferred loop is:

```text
recipe/SDF/voxel delta -> field/voxel queries -> contacts/forces/constraints -> recipe or materialized deltas
```

This doc captures a research direction for a custom physics module that is independent from storage
placement and can be developed either as a self-contained library or as a VIXEN module using shared
base dependencies.

## 2. Design Goals

1. **Data independence.** Physics must consume interfaces, not concrete render buffers:
   `sampleField`, `sampleGradient`, `queryOccupancy`, `queryMaterial`, `queryRegion`, and
   `writeDelta`.
2. **No mesh authority.** Triangle meshes are derived artifacts only. They may help debug or export,
   but they must not own collision truth or damage truth.
3. **Sparse by default.** Simulate only the active spatial set and only the channels required by a
   job. Heat in a ship corridor should not allocate heat data for every voxel in the ship.
4. **Multi-resolution by design.** Fluids, gases, weather, integrity, and rigid-body behavior all need
   independent spatial and temporal resolution policies.
5. **Mip-scale semantic awareness.** Each simulation family must define what it means at each
   mip/scale level; a local ocean hover solve is not the same model as an orbital ocean summary.
6. **Predictive refinement.** Sparse mip summaries and temporal deltas should identify likely
   high-detail events before full-resolution simulation is allocated.
7. **Dispatch-native execution.** Physics should push VIXEN toward explicit CPU job, GPU dispatch,
   multi-queue, and cross-GPU orchestration instead of ad-hoc frame-local work.
8. **Field interaction first.** Heat, pressure, kinetic impulse, gravity, material state, density,
   strain, damage, and phase transitions should be fields or sparse overlays, not permanent payload
   on every voxel.
9. **Delta-native destruction.** Mining, explosions, blowtorch cutting, hull breaches, and ship
   splitting must produce recipe-deltas when expressible and materialized deltas when not.
10. **Backend-optional solving.** A solver like Jolt may integrate conventional rigid bodies and
   constraints, but VIXEN owns field queries, material response, voxel integrity, and delta writes.

## 3. External Approaches

| Source | Useful ideas | Pros | Cons for VIXEN |
|---|---|---|---|
| Jolt Physics | Mature rigid bodies, broadphase, constraints, sleeping, characters, deterministic game-oriented solver | MIT, C++17, CMake, production-proven, good optional rigid solver | Shape/contact model is not field-native. Forking it into a voxel engine likely means rewriting its core while inheriting upstream complexity. |
| Voxelyze / VoxCAD lineage | 3D voxel soft-body simulation, heterogeneous materials, volumetric actuation, collision between deforming voxel bodies | Closest conceptual match to voxel bodies and material heterogeneity | Research-era code and architecture. Better as algorithm reference than dependency. |
| Cronos soft/hybrid robot simulator | GPU-parallel mass-spring soft body simulation, heterogeneous stiffness, volumetric actuation | Strong reference for GPU voxel/soft-body throughput | Robotics/research target, CUDA-centric, not a game/runtime architecture. |
| nvblox / voxblox | Incremental TSDF/ESDF construction and distance/gradient queries | Good reference for local SDF updates and collision-distance queries | Mapping/planning libraries, not dynamics solvers. |
| OpenVDB / NanoVDB | Sparse volumetric data structures and tools | Mature sparse-volume design reference | Not a physics engine and not aligned with VIXEN's existing SVO/brick layout without major adapter cost. |
| Project Chrono | Multiphysics, deformable bodies, granular dynamics, fluid-solid interaction | Serious scientific engine, broad algorithms | Large scientific dependency, not voxel-native, likely too heavyweight for VIXEN runtime. |
| Rigs of Rods | Real-time soft-body node/beam simulation and deformation | Useful node/beam design lessons | GPL and vehicle/node-beam centric, not voxel/SDF canonical. |

Research conclusion: there is no obvious drop-in open-source voxel physics engine that matches
VIXEN's data model. The likely best path is a custom field-native module that borrows algorithms
selectively.

## 4. Proposed Architecture

### 4.1 Module Boundary

Create a `VixenPhysics` module with minimal required dependencies:

| Dependency | Why it is allowed |
|---|---|
| Math types | Vectors, matrices, AABB, Morton/TierAddress keys |
| Task scheduling | Budgeted dispatch, worker jobs, dependency waves |
| Resource management | GPU/CPU buffer allocation, staging, budget limits |
| Dispatch orchestration | CPU worker pools, GPU queues, async compute, cross-device transfer |
| Kernel/codegen | Optional shared schemas, dispatch-chain contracts, field layouts, deltas |
| Event bus | Optional fracture, impact, migration, thermal, pressure events |

The module should not depend on RenderGraph nodes or app-specific consumers. RenderGraph integration
is an adapter layer.

### 4.2 Core Interfaces

```cpp
struct FieldSample {
    float value;          // signed distance, density, temperature, pressure, etc.
    vec3 gradient;        // optional; valid when the provider can compute it
    uint32_t materialId;
    uint32_t flags;
};

struct FieldQuery {
    FieldSample Sample(vec3 worldPos, FieldSemantic semantic, float radiusOrLod);
    OccupancySummary QueryOccupancy(RegionKey region, LodLevel lod);
    MaterialSummary QueryMaterial(RegionKey region, LodLevel lod);
};

struct DeltaWriter {
    void AppendRecipeDelta(RegionKey region, RecipeDelta delta);
    void AppendMaterializedDelta(RegionKey region, BrickDelta delta);
    void MarkDirty(RegionKey region, FieldSemantic semantic);
};
```

The important point is that physics sees **semantics and regions**, not storage placement. A provider
may answer from an analytic recipe, a materialized brick pool, a stored voxel asset, a sparse field
overlay, or a cache.

### 4.3 Job Graph

Simulation should be a set of jobs that declare:

| Field | Meaning |
|---|---|
| spatial scope | AABB, region key set, body id set, or dynamic active set |
| input fields | Example: density + pressure + velocity, or temperature + material |
| output fields | Example: pressure delta, heat delta, fracture event, material phase change |
| resolution policy | Full, brick, mip, adaptive, or analytic |
| scale semantics | What the job means at local, regional, planetary, and orbital mip levels |
| cadence | Every frame, N-frame cadence, event-driven, or background |
| budget class | Critical, interactive, background, or offline |

This prevents compounding every voxel with every possible property. A heat job allocates and updates
only heat-related sparse data in the active region. A pressure job does not force temperature,
integrity, or material damage channels to be resident unless requested.

### 4.4 Dispatch and Orchestration Substrate

The physics direction implies a broader VIXEN engine requirement: simulation work must be easy to
split, schedule, validate, and dispatch across CPU threads, GPU queues, and eventually multiple GPUs.
Physics should not become a special-case scheduler; it should be one demanding client of a shared
dispatch substrate.

Every simulation job should declare:

| Declaration | Purpose |
|---|---|
| execution domain | CPU scalar, CPU SIMD, CPU task graph, GPU compute, async compute, transfer, or multi-device. |
| resource reads/writes | Field layers, brick buffers, body state, history deltas, staging buffers, and event queues. |
| hazards | Read-after-write, write-after-read, write-after-write, ownership transfer, and aliasing constraints. |
| granularity | Region, brick, body, tile, particle, field layer, or whole-scene batch. |
| residency needs | CPU resident, GPU resident, mirrored, streamed, or cross-device replicated. |
| synchronization | Fence, timeline semaphore, barrier, dependency wave, or frame-latency allowance. |
| validation metadata | Debug labels, producer/consumer ids, semantic field contracts, expected bounds, and budget class. |

This makes simulation parallel-friendly by construction. A heat job, pressure job, integrity job,
and mip-delta predictor can run in parallel when their declared regions and field layers do not
conflict. When they do conflict, the scheduler has enough information to insert barriers, split
regions, delay background work, or choose a lower-resolution fallback.

CPU dispatch patterns:

- Region-parallel jobs over independent spatial sets.
- Body-parallel jobs for rigid body transforms, mass properties, and broadphase candidates.
- Graph-parallel jobs for connectivity islands, room topology, and fracture propagation.
- Task-stealing for irregular sparse regions where active work is uneven.
- Deterministic reduction stages for totals such as impulse, energy, pressure, and damage.

GPU dispatch patterns:

- Field kernels over sparse region lists rather than full-world grids.
- Prefix/sort/compact passes for active cells, contacts, debris, or refinement requests.
- Async compute for mip summaries, thermal diffusion, pressure propagation, and broadphase assist.
- Transfer queues for streaming bricks, readback summaries, and cross-device exchange.
- Persistent or indirect dispatch where active region counts are GPU-produced.

Cross-GPU or multi-device support should remain optional, but the architecture should avoid blocking
it. The lowest-risk model is domain partitioning: one device owns rendering-critical visible regions,
another can process background simulation, coarse history deltas, or large planetary/weather fields.
The hard requirements are explicit ownership, explicit transfers, deterministic merge points, and
debug visibility into where each field layer currently resides.

Validation is load-bearing. A dispatch graph debugger should be able to answer:

- Which job produced this field or delta?
- Which jobs read stale or lower-LOD data by policy?
- Which resource barriers or ownership transfers were inserted?
- Which refinement requests were dropped, delayed, or downgraded by budget?
- Which CPU/GPU/device ran the job, and how much time/memory it consumed?

### 4.5 Kernel Framework Dependency Decision

The Yeroket kernel framework is a serious candidate for the job contract and dispatch-chain layer,
because it already owns cross-domain schema/codegen concepts and has a dispatch chain implemented
for the Unity consumer. The question is whether VIXEN should depend on that framework directly, use
only generated artifacts, or define a pure VIXEN substrate with an adapter.

Candidate dependency models:

| Model | Shape | Pros | Risks |
|---|---|---|---|
| Pure VIXEN substrate | VIXEN defines job graph, field schemas, and dispatch contracts itself. | Maximum control, no external coupling, easiest to keep C++/Vulkan-native. | Duplicates kernel-framework concepts and may diverge from existing tooling. |
| Kernel-generated contracts | VIXEN authors schemas/callables and consumes generated C++/GLSL/HLSL-style artifacts, with generated files committed beside VIXEN code. | Reuses proven layout/codegen path while keeping runtime ownership in VIXEN. | Requires drift guards and clear generated-artifact workflow. |
| Kernel framework as contract dependency | VIXEN depends on a small runtime-neutral slice of kernel-framework attributes/contracts. | Shared vocabulary for `KernelLayout`, slots, blackboards, dispatch descriptors, and generated kernels. | Dependency boundary can become unclear if Unity/Burst-specific assumptions leak into VIXEN. |
| Kernel framework as orchestrator dependency | Kernel-framework dispatch chain becomes the primary job orchestrator. | Most reuse if the existing chain maps cleanly. | Highest coupling; risky if the chain is shaped around Unity scheduling rather than VIXEN's Vulkan/CPU/GPU/multi-device needs. |

Preferred research direction: start with **kernel-generated contracts**, not a hard orchestrator
dependency. VIXEN should own runtime scheduling, Vulkan queue submission, GPU resource ownership,
cross-device transfer, and validation. The kernel framework can generate shared declarations,
layout-stable buffers, kernel callables, blackboard-like job inputs, slot manifests, and possibly
debug metadata. If this proves too restrictive, the fallback is a pure VIXEN substrate that borrows
the contract shape but does not depend on the tool.

The strongest argument for kernel-framework reuse is **consumer-side simulation intent injection**.
Consumer projects should be able to declare domain-specific simulation jobs without forking VIXEN's
physics engine. For example, a spaceship gameplay package could declare an engine heat simulation as
a kernel dispatch chain:

```text
consumer spaceship system
  -> declares ShipEngineHeatJob schema, inputs, outputs, cadence, and dispatch chain
  -> kernel framework emits job descriptors, buffer layouts, slots, and kernel artifacts
  -> VIXEN discovers or links the generated artifact at compile time or runtime
  -> VixenPhysics validates declared reads/writes, hazards, budgets, and field semantics
  -> VixenPhysics schedules the work inside its own CPU/GPU dispatch graph
  -> output deltas feed VIXEN-owned heat, damage, render, and simulation fields
```

This mirrors the existing direction for view data types and flow actions: the consumer owns the
intent declaration, while VIXEN owns the generic execution substrate and hard engine invariants. The
physics engine should not need built-in knowledge of every ship system, weapon, reactor, ecosystem,
weather layer, or alien material interaction. It should expose a constrained declaration surface
where those systems can inject jobs safely.

This also aligns with the ongoing kernel unification philosophy. The goal is not to create another
one-off plugin interface for physics, but to extend the same pattern already being consolidated:

```text
consumer-authored declaration
  -> kernel/codegen validation
  -> generated cross-domain artifact
  -> engine-owned runtime execution
  -> drift-checked contract between authoring and runtime
```

Physics jobs, view data, flow actions, SDF callables, and future simulation modules should converge
on the same contract idea: authored intent lives close to the consumer domain; generated artifacts
provide byte-stable data/logic boundaries; the engine remains authoritative over scheduling,
resource ownership, validation, and safety. This keeps VIXEN extensible without turning VIXEN core
into a dumping ground for every game-specific system.

Longer term, the artifact contract should be language-neutral. C# can be the first and best-supported
authoring frontend because the current kernel framework already exists there, but VIXEN should not
make "C# authored" part of the runtime contract. A Rust application, Python tooling stack, or future
DSL should be able to author the same kind of simulation intent if its transpiler emits the same
validated artifact shape.

```text
C# / Rust / Python / DSL consumer declaration
  -> frontend-specific parser/transpiler
  -> shared kernel artifact ABI and manifest
  -> VIXEN validation
  -> native VIXEN physics dispatch
```

This would let future consumers keep their own ergonomics while still interacting with the physics
engine at native performance. A Rust consumer could declare physics artifacts in Rust-native types
and build tooling; a Python-heavy procedural/content pipeline could generate declarations from its
own scripts; VIXEN would consume the resulting manifest, layouts, kernels, and capability metadata
without embedding an interpreter or paying dynamic-language runtime costs in the simulation loop.

The non-negotiable part is the artifact ABI, not the authoring language:

- stable field and buffer layouts
- explicit job descriptors and dispatch shapes
- declared field semantics, reads/writes, hazards, and capabilities
- generated native/shader code where executable logic is required
- deterministic versioning, drift checks, and validation diagnostics
- no runtime dependency on the authoring language for performance-critical simulation

Possible declaration categories:

| Consumer declaration | VIXEN-owned authority |
|---|---|
| Ship engine heat, reactor leak, weapon charge, shield dissipation | Field semantics, resource residency, thermal/material response rules |
| Weapon impact, laser cutting, mining beam, repair foam | Contact/damage validation, delta writes, integrity constraints |
| Life-support pressure, fire, atmosphere composition | Pressure/gas solver, room topology, conservation rules |
| Planet weather, star flare, gas giant band interaction | Field allocation, simulation LOD, render-facing field publication |
| Creature/tool/material custom reactions | Allowed input/output fields, safety limits, deterministic writeback |

Injection can happen in two modes:

- **Compile-time injection:** generated descriptors and kernels are committed with the consumer or
  linked into the VIXEN build. This is easiest to validate, drift-check, and ship.
- **Runtime injection:** generated packages are loaded as plugins or content modules. This is more
  powerful, but needs stricter ABI/version checks, capability declarations, sandboxing, and budget
  limits.

The injected job must be declarative enough for VIXEN to reject unsafe work before it runs. Minimum
metadata:

- declared input and output `FieldSemantic`s
- region/body scope rules
- execution domain and dispatch shape
- resource reads/writes and hazards
- required slots, buffers, and blackboard layout
- budget class, cadence, and maximum work envelope
- deterministic seed policy and save/load behavior
- permission/capability tags for destructive writes, persistent deltas, and high-cost fields

This keeps a clean split: consumer modules describe *what simulation intent exists*; VIXEN decides
*whether, when, where, and at what fidelity it executes*.

The dependency boundary should follow these rules:

- Kernel framework may define or generate byte-stable data contracts.
- Kernel framework may generate CPU/GPU kernel entry points when the emitted code is domain-blind.
- VIXEN owns resource residency, Vulkan barriers, queue ownership, multi-GPU partitioning, and frame
  scheduling.
- Unity-specific concepts such as Burst stages, Unity job timing, and editor/runtime assumptions
  must not become VIXEN runtime requirements.
- Generated artifacts must be committed and drift-checked so VIXEN builds do not depend on a hidden
  local tool state.

Potential mapping:

| VIXEN physics concept | Kernel-framework reuse candidate |
|---|---|
| `FieldLayer` layout | `[GpuStruct]` / generated C++ and shader structs |
| Job input/output declaration | `KernelBlackboardLayout`-style schema |
| Append/event buffers | `AppendBufferSlot`-style slot manifest |
| Spatial neighbor queries | `SpatialHashSlot` or `BoundaryHashSlot` pattern |
| Per-region compute kernels | `VMKernel` / `KernelCallable`-style authored logic |
| Dispatch debug metadata | Generated slot/job manifest consumed by VIXEN validation |
| Consumer job injection | View/AppFlow-like generated descriptor artifacts |
| Kernel unification | Shared glue-source workflow for declarations, validation, generated artifacts, and drift checks |
| Multi-language authoring | C#, Rust, Python, or DSL frontends emitting the same VIXEN-facing artifact ABI |

Hard exit criteria for using kernel-framework as more than codegen:

- It must support VIXEN's C++/Vulkan execution without Unity runtime dependencies.
- It must express CPU and GPU jobs without assuming a single Unity consumer dispatch model.
- It must expose enough metadata for VIXEN barriers, queue ownership, and cross-device transfers.
- It must not prevent VIXEN from scheduling non-kernel jobs such as Jolt adapter steps, graph
  connectivity, IO/streaming, or high-level gameplay simulation.
- It must remain optional enough that a pure VIXEN dispatcher can consume the same generated
  contracts if the orchestration layer diverges.

## 5. Simulation Families

### 5.1 Voxel Rigid Bodies

Rigid voxel bodies should be represented as connected voxel/region sets with a body-level transform,
mass properties, and material summaries. The collision surface is queried from the underlying field:

1. Broadphase: body bounds from recipe metadata, SVO region bounds, or materialized brick bounds.
2. Midphase: occupied/coarse regions and mip summaries.
3. Narrowphase: SDF/occupancy sampling for penetration, normal, contact point, and material response.
4. Solver: optional rigid-body solver integrates body transforms and velocities.
5. Writeback: transform deltas or materialized deltas, depending on whether the body remains rigid.

Jolt can be evaluated as a solver for this family only if VIXEN can provide custom contacts or
coarse proxy shapes without making cooked meshes canonical.

### 5.2 Voxel Body Integrity

Integrity simulation covers ships, asteroids, stations, hull panels, and destructible structures.

Core state:

- Connectivity graph at region/brick/voxel LOD.
- Material strength, thermal weakening, stress, accumulated damage.
- Face/edge bonds for local integrity.
- Fracture thresholds and propagation rules.
- Body-part ownership for split detection.

Use cases:

- Mining asteroids.
- Firing at and exploding a ship.
- Cutting into a ship with a blowtorch.
- Hull breaches and decompression.
- Localized weak points from heat, corrosion, fatigue, or prior damage.

Algorithms to evaluate:

| Algorithm | Best use | Notes |
|---|---|---|
| Flood-fill connectivity over occupied regions | Body splitting | Run at coarse LOD first, refine near fracture. |
| Bond/face strain model | Local crack and break propagation | Matches archived soft-body voxel design. |
| Graph cut / union-find with dirty updates | Persistent connected components | Good for mining and detached chunks. |
| Damage field accumulation | Explosions, heat, repeated impacts | Sparse overlay keyed by region. |
| Recipe-delta CSG subtract | Clean cuts and mining when expressible | Avoids materialization when possible. |
| Materialized brick deltas | Arbitrary damage and per-voxel edits | Requires topology-self-sufficient deltas. |

### 5.3 Soft / Deformable Voxel Bodies

The archived soft-body design remains useful, but should be reframed under the lazy-procedural
baseline:

- 1-element mode for stable/semi-rigid voxels.
- 8-particle or equivalent high-fidelity mode near active deformation.
- Face-to-face constraints for connected voxels.
- Strain and fracture thresholds per material.
- Spatial, representation, and temporal LOD.
- Re-voxelization/migration when deformation crosses cell thresholds.

This should be treated as an optional simulation family, not the default representation for all
bodies. Most rigid or procedural bodies should stay cheap until contact, edit, fracture, or gameplay
requires higher fidelity.

### 5.4 Fluids, Gases, Weather, and Arbitrary Gravity

VIXEN needs multiple fluid-like regimes, not one universal fluid solver:

| Use case | Candidate approach | Resolution policy |
|---|---|---|
| Water near player or ship hull | Local Eulerian grid, FLIP/PIC hybrid, or shallow/local SPH | High local resolution, active only near interaction. |
| Gas giants / stars | Procedural volumetric fields plus low-frequency advection/noise/weather state | Very coarse simulation plus high-quality procedural rendering. |
| Procedural weather | Sparse vector/scalar fields: pressure, humidity, temperature, wind | Planetary/coarse LOD, refined only where needed. |
| Ship atmosphere | Sparse pressure/temperature/gas composition cells over rooms/corridors | Topology/room graph first, voxel refinement near breaches/fire. |
| Arbitrary gravity water | Field-driven gravity vector and signed potential, local fluid solve in active basins | Solver must sample gravity field, not assume global -Y. |

Important rule: fluid simulation resolution is independent from render voxel resolution. A star's
rendered turbulence may be procedural at high visual detail while simulation tracks only coarse
energy, density, magnetic/weather bands, or gameplay-relevant fields.

Fluid jobs must also be **mip-scale aware**. The solver used when hovering over an ocean is a
different semantic object than the solver used when viewing the planet from orbit:

| Scale | Ocean/water meaning | Likely simulation/render contract |
|---|---|---|
| Local interaction | Waves, spray, buoyancy, hull contact, wake, foam, local arbitrary-gravity flow | Local high-resolution fluid or surface solver with collision/contact feedback. |
| Regional surface | Currents, wind response, tides, shoreline interaction, storm fronts | Coarse vector/scalar fields, wave spectra, shallow-water or procedural advection. |
| Planetary/weather | Ocean temperature bands, depth influence, cloud coupling, sunlight exposure, albedo/color gradients | Low-frequency fields derived from depth, latitude, weather, cloud cover, and light exposure. |
| Orbital view | Planet-scale color, reflectance, atmosphere/cloud shadowing, sun and secondary light contribution | Mostly render-facing summary fields; no detailed water dynamics unless an event is strategically relevant. |

This should not be treated as one fluid solver with bigger cells. At high mips, the "fluid" may be
only a conserved or visual summary: ocean depth gradients, temperature/color bands, cloud shadow
coverage, sunlight and secondary illumination, or weather-state parameters. At low mips near the
player, the same region can refine into interactive water with collision, buoyancy, spray, and local
surface response.

The contract should allow simulation families to declare a scale ladder:

```text
ScalePolicy = semantic + mip range + representation + solver + conservation rules + refinement path + transition rules
```

Required behavior:

- A job must state which mip ranges it is valid for.
- Refinement from coarse to fine must preserve important totals where applicable: mass, energy,
  momentum, heat, damage, pressure, or weather state.
- Coarsening from fine to coarse must summarize persistent outcomes back into stable fields.
- Render-facing fields are allowed at coarse mips when no gameplay-grade solve is required.
- Player proximity, visibility, interaction, and strategic relevance decide when to move between
  scale policies.

Mip transitions must be smooth enough that the player does not notice the representation swap, or
notices it only as a subtle increase in detail. The transition itself should be part of the policy,
not an afterthought hidden in rendering code:

```text
ScaleTransition = fromMip + toMip + distanceBand + hysteresis + blendFields + promotionRules
```

Transition requirements:

- Use distance bands rather than a single threshold, so two adjacent mip policies overlap.
- Add hysteresis so camera/player movement near the boundary does not repeatedly promote/demote the
  same region.
- Cross-fade render-facing fields such as color, foam, wave spectra, cloud shadowing, heat glow, and
  surface normals where possible.
- Blend or reconcile simulation state through conserved summaries rather than directly lerping
  invalid physics state.
- Allow prefetch/prewarm of finer mips before the player reaches the transition band.
- Prefer gradual injection of local detail: wake patterns, wave normals, spray particles, fracture
  debris, heat shimmer, or smoke should appear over a short band rather than popping in one frame.
- Keep debug overlays for active mip level, transition weight, source fields, and promoted/demoted
  regions.

For the ocean example, orbital water may start as depth-derived color and cloud/sun lighting. As the
player descends, the renderer can blend in regional current/wave spectra, then local surface normals,
then only near interaction distance activate expensive buoyancy, wake, spray, and collision feedback.
The visible transition should read as natural sharpening, not as a switch to a different ocean.

### 5.5 Sparse Field System

Field storage should be a first-class subsystem:

```text
FieldLayer = semantic + region key + resolution + storage policy + lifetime + producer
```

Candidate semantics:

- kinetic impulse / velocity
- pressure
- temperature / heat
- gas composition
- humidity / weather state
- gravity vector or potential
- electromagnetic or shield fields
- damage
- stress / strain
- material phase
- occupancy override

Storage policies:

- analytic recipe
- sparse region map
- dense local brick set
- mip/coarse grid
- temporal ring buffer
- event-only impulse list
- derived cache

This lets a local heat simulation in a ship allocate temperature and heat-flow data only for the
rooms, hull bricks, or machinery currently participating in the thermal job.

### 5.6 Sparse Mip Delta Prediction

Sparse mip layers should be treated as simulation observability data, not only render LOD. Every
field layer may optionally maintain coarse summaries and rate-of-change data:

- value summary: min, max, mean, variance
- spatial gradient: contrast between neighboring cells at the same mip level
- temporal delta: change in value or gradient over time
- scale delta: difference between a parent mip cell and its children
- vector summary: divergence, curl, speed range, and dominant direction for velocity-like fields
- event score: budgeted priority derived from semantic-specific predicates

The key signal is not only "this region is detailed"; it is "this region is becoming different
across scale or time." A high parent/child delta, a rapidly changing gradient, or a mismatch between
coarse and fine velocity summaries can predict where physics needs to spend work next.

| Signal | Likely interpretation | Refinement action |
|---|---|---|
| Velocity direction or speed discontinuity | Near collision, shearing, orbital crossing, or high relative motion | Pre-refine broadphase, contact, and local SDF queries. |
| Kinetic impulse spike | Explosion, projectile impact, or dense multi-body interaction | Allocate impulse, pressure, damage, and debris jobs. |
| Heat gradient or temporal heat spike | Laser strike, fire front, reactor failure, stellar burn event | Allocate thermal transfer and material response fields. |
| Pressure divergence | Hull breach, shockwave, decompression, weather front | Allocate gas/pressure solver in the affected topology. |
| Damage or strain gradient | Crack propagation, bending, or impending split | Refine integrity graph and bond checks. |
| Occupancy/material delta | Mining, cutting, fracture, or body separation | Materialize affected regions and update connected components. |

The predictor should emit budgeted refinement requests rather than directly forcing full-resolution
simulation:

```cpp
struct RefinementRequest {
    RegionKey region;
    FieldSemantic semantic;
    RefinementReason reason;
    LodLevel targetLod;
    float urgency;
    float timeToLiveSeconds;
    BudgetClass budgetClass;
};
```

Scheduler policy:

- **Near or visible events:** refine eagerly so the player sees live collision, fracture, heat,
  fluid, and debris behavior.
- **Offscreen events:** run coarse conservation-level simulation, store the resulting history delta,
  and defer expensive local detail until observation or gameplay relevance requires it.
- **Strategic or cinematic events:** allow higher fidelity even when offscreen if the outcome is
  mission-critical, network-visible, or likely to become observable soon.
- **Background events:** keep only summarized field deltas, component splits, and conserved totals.

For example, if an asteroid is redirected into a station while the player is elsewhere, the physics
system should not need to simulate every fractured voxel in real time. It can evaluate the coarse
collision, conserve the important totals, update station damage and body state, and store a
deterministic aftermath recipe:

```cpp
struct HistoryDelta {
    RegionKey region;
    EventKind eventKind;
    double startTime;
    double endTime;
    ConservedTotals totals;       // momentum, energy, mass/material loss where applicable
    DamageSummary damage;
    uint64_t deterministicSeed;
    RefinementRecipe unresolvedDetail;
};
```

When the player later arrives, VIXEN can refine the stored `HistoryDelta` into visible debris,
fracture surfaces, heat scars, pressure loss, and persistent material deltas. The refined result must
remain consistent with the coarse totals so offscreen simulation does not produce a different
strategic outcome than the later high-detail view.

## 6. Data Flow

### 6.1 Explosion Example

```text
Explosion event
  -> pressure/heat/impulse field layers over affected region
  -> integrity job accumulates damage and breaks bonds
  -> connectivity job detects detached components
  -> rigid body job creates or updates separated body chunks
  -> delta writer emits recipe/materialized deltas
  -> render producer updates affected regions only
```

### 6.2 Blowtorch Breach Example

```text
Tool contact path
  -> localized heat field
  -> material weakening / phase change
  -> CSG subtract if cut remains recipe-expressible
  -> materialized delta if cut becomes arbitrary
  -> pressure job handles atmosphere leak
  -> integrity job updates hull connectivity
```

### 6.3 Ship Heat Example

```text
Fire or reactor heat source
  -> sparse heat layer on room/corridor graph
  -> only rooms with heat gradient allocate voxel/brick refinement
  -> material response modifies strength, atmosphere, damage
  -> render consumes optional glow/smoke/weather fields
```

### 6.4 Offscreen Asteroid Impact Example

```text
Asteroid trajectory update
  -> coarse velocity/kinetic mip delta predicts high-energy contact
  -> scheduler creates refinement request for target region
  -> visibility/proximity policy selects coarse offscreen simulation
  -> collision job writes HistoryDelta with conserved totals and damage summary
  -> station state records hull loss, component splits, pressure/heat events
  -> later observation refines HistoryDelta into visible fracture/debris/field deltas
```

## 7. Jolt Branch Decision

Forking Jolt into a voxel-centric engine is not the first-choice architecture.

Reasons:

- Jolt's value is its mature rigid-body pipeline, not voxel field ownership.
- Replacing the shape/contact core would be a forked engine rewrite.
- Keeping up with upstream would become expensive.
- VIXEN's core challenge is field/delta/contact generation, which Jolt does not solve.

Recommended use:

1. Keep Jolt external and optional.
2. Build a narrow adapter for conventional rigid body solving.
3. Feed it VIXEN-derived proxies or contacts only where that stays clean.
4. Do not let Jolt's mesh/shape model define VIXEN's physics data model.

If a fork is explored later, it should be a spike with a hard exit criterion: demonstrate custom
field-generated contacts and VIXEN-owned body/region identity without cooked mesh dependence.

## 8. Increment Sketch

1. **Physics substrate interfaces.** Define `FieldQuery`, `DeltaWriter`, region keys, field
   semantics, sparse field descriptors, and job declarations.
2. **Dispatch substrate contract.** Define CPU/GPU execution domains, resource hazards, barriers,
   queue ownership, debug labels, and validation metadata for simulation jobs.
3. **Kernel-framework spike.** Test generated contracts for field layers, job inputs, slot manifests,
   consumer-authored job injection, and a minimal dispatch-chain adapter while keeping VIXEN runtime
   scheduling authoritative.
4. **Mip-scale policy prototype.** Define scale ladders for at least ocean/water, atmosphere, and
   integrity so each family has explicit local, regional, planetary, and orbital meanings.
5. **Mip delta predictor.** Maintain field summaries, temporal deltas, scale deltas, hotspot
   predicates, `RefinementRequest`s, and offscreen `HistoryDelta`s.
6. **Collision/query prototype.** Primitive-vs-SDF and body-vs-field contact generation over
   analytic and materialized providers.
7. **Sparse field layers.** Local heat/pressure/impulse fields with independent resolution and
   event-driven lifetimes.
8. **Integrity MVP.** Region connectivity, bond breaking, explosion damage, and body splitting.
9. **Rigid voxel body adapter.** Body transforms, mass properties, broadphase/midphase, optional
   Jolt adapter experiment.
10. **Fluid/gas pilot.** Ship atmosphere and water under arbitrary gravity before planetary-scale
   weather.
11. **Soft/deformable body pilot.** Revisit archived Gram-Schmidt/dual-representation model under
   the new substrate.

## 9. Open Questions

1. What is the minimal stable `FieldSemantic` vocabulary for v1?
2. Should field layers use the render SVO region key directly, or a parallel physics region key that
   maps to render regions?
3. What is the first ship/asteroid integrity gate: mining, projectile explosion, or blowtorch breach?
4. Can Jolt accept enough custom contact input to be useful without adopting its shape model?
5. Which fluid family should be first: ship atmosphere, local water, gas giant weather, or star
   plasma-like visual simulation?
6. How much of field evaluation should be CPU-first vs GPU-first in v1?
7. How should save/load serialize transient fields vs persistent deltas?
8. Which mip-delta predicates are stable enough for v1, and which are debug-only heuristics?
9. What invariants must `HistoryDelta` preserve so offscreen outcomes refine consistently when seen?
10. How should player proximity, visibility, strategic relevance, and network relevance compete for
    refinement budget?
11. Which dispatch graph concepts should be shared with RenderGraph, and which should remain
    physics/simulation-specific?
12. What is the first acceptable cross-GPU model: explicit user-selected device roles, automatic
    domain partitioning, or no v1 support beyond clean ownership boundaries?
13. How much determinism is required across CPU/GPU scheduling differences for gameplay, replay,
    save/load, and multiplayer?
14. Should VIXEN depend on kernel-framework runtime contracts, consume only generated artifacts, or
    keep a pure VIXEN dispatcher with a kernel-framework adapter?
15. Which existing kernel-framework dispatch-chain concepts are Unity-specific, and which are
    genuinely domain-blind enough for VIXEN?
16. What should be allowed through consumer-side job injection, and which physics/render/simulation
    operations must remain VIXEN-only?
17. Should runtime-injected simulation packages be supported in v1, or should v1 require compile-time
    generated descriptors only?
18. Which parts of the physics declaration surface should become part of the broader kernel
    unification contract rather than remaining physics-specific?
19. What is the minimum stable artifact ABI that would let non-C# frontends generate native VIXEN
    physics jobs without depending on a C# runtime?
20. What is the required scale ladder for each major simulation family, and which mip levels are
    simulation-authoritative versus render-summary-only?
21. How should orbital/coarse ocean summaries refine into local interactive water without visible or
    gameplay-breaking discontinuities?
22. What transition bands, hysteresis rules, and cross-fade fields are required so render and
    simulation mip changes stay subtle during player movement?

## 10. References

- Jolt Physics: https://github.com/jrouwe/JoltPhysics
- Voxelyze / soft heterogeneous voxel simulation: https://arxiv.org/abs/1212.2845
- Cronos soft/hybrid robot simulator: https://arxiv.org/abs/2207.09334
- nvblox: https://arxiv.org/abs/2311.00626
- voxblox: https://arxiv.org/abs/1611.03631
- Kernel-SDF: https://arxiv.org/abs/2603.29227
- OpenVDB: https://github.com/AcademySoftwareFoundation/openvdb
- Project Chrono: https://github.com/projectchrono/chrono
- Rigs of Rods: https://github.com/RigsOfRods/rigs-of-rods

## Related VIXEN Docs

- [[../01-Architecture/Lazy-Procedural-Delta-Baseline-Design-2026-07]]
- [[../01-Architecture/Kernel-Physics-Dispatch-Contract-Spec-2026-07]]
- [[../01-Architecture/Voxel-Content-Format-Contract-Design-2026-06]]
- [[../01-Architecture/Destructible-Body-Rendering-Direction-2026-06]]
- [[../05-Progress/Production-Roadmap-2026]]
- [[../_archive/2026-07/feature-proposal-plans/soft-body-voxel-physics-design]]
