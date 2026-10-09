---
title: Material Handling (matflow)
aliases: [Matflow, Material Flow]
tags: [physics, fields, research]
created: 2026-10-09
related:
  - "[[README]]"
---

# Matflow: physical material handling, depth tiers, and shape-driven flow

Research and design report · 9 October 2026 · lane matflow

**Status:** research complete; implementation and performance witnesses are proposed, not executed. Product code and SPT were read-only. The recommendation is a staged physical-material module built on the existing field-native physics direction, with exact quantity ownership independent of numerical solver fidelity.

## Result in 10 lines

1. Recommend bounded local handling within R256's MPM-led material portfolio, supported spatial bulk interiors, and explicit physical transfer ports.
2. Deliver sand mining → spill → scoop → container → typed inserter → machine before adding broad fluid or multiphase simulation.
3. Establish exact mass quanta and single ownership first; current floating-point planetary depletion and efficiency-adjusted stock need an explicit bridge.
4. Treat surface depth as a detail selector, with additional activation for tools, openings, support loss, material interfaces, pressure, and moving containers.
5. Preserve full state when first implementing sleep; replacing sleeping grains with a bulk closure is a separate, measured approximation.
6. Preserve quantity, momentum, occupancy, and necessary history across representation changes; overlap samples must never become duplicate matter.
7. Trial small rigid sphere clumps for shape-sensitive handling, then calibrate cheap interior closures against repose, shear, packing, discharge, and jamming.
8. Use local DFSPH for the first liquid trial; retain coupled pressure degrees of freedom when reducing deep liquid detail.
9. Begin transport with finite stored contents and rate-limited buffered transfers; make pressure networks and suspension rheology explicit later choices.
10. Keep authority on compiled CPU until deterministic replay, save continuation, backend parity, and complete cost measurements justify expansion.

## Method: scope, queries, sources opened, and date

### Research procedure

Live searches and primary-source retrieval were performed on **2026-10-09**. The requested contemporary window is 2020–2026. Older work is included only where it establishes a named method: PBD/PBF, IISPH, APIC, narrow-band FLIP, Drucker–Prager MPM, MLS-MPM/CPIC, Hybrid Grains, SPGrid, Noita's talk, and Factorio's original belt optimization. Publication year is distinguished from retrieval date, preprint revision, and a live software repository.

Queries covered the following families; variants added author names, publication venues, dates, and official-domain restrictions:

| Query family | Representative searches | Primary evidence used |
|---|---|---|
| Granular mechanics and adaptation | “adaptive sampling interactive granular material 2025”; “Hybrid Grains continuum discrete”; “granular MPM DEM coupling mass momentum”; “GPU MPM 2020 G2P2G” | Author papers, publisher pages, an MIT dissertation record, and author project pages [G1]–[G13] |
| Hidden geometry and rheology | “multi sphere rolling resistance superquadric packing shear”; “elongated particles silo discharge orientation”; “shear thickening particle interlocking spherical asperities”; “physics informed elastoplastic surrogate sand” | Controlled shape comparisons, measured orientation, suspension studies, and a constitutive-surrogate preprint [G6], [G8], [G11], [S1], [S2] |
| Fluids and depth reduction | “DFSPH IISPH PBF”; “narrow band FLIP pressure interior”; “adaptive phase field FLIP 2025”; “real time water sand heightfield 2023”; “LBM MPM air water sand 2026” | Original papers, official tutorials, project pages, and simulation repositories [F1]–[F11], [F15] |
| Sparse infrastructure | “NanoVDB sparse GPU”; “SPGrid multigrid”; “GVDB voxels CUDA” | Official project documentation and publications [F12a], [F12b], [F13], [F14] |
| Industrial transport | “Factorio fluids 2.0 416 430 release”; “Factorio 442 fluid buffers”; “Satisfactory Fluids Update 2020”; “EPANET 2.2 hydraulic solver” | Developer posts, release notes, official feature announcements, EPA manual [T1]–[T9] |
| Determinism and shipped simulation limits | “Box2D determinism 2024”; “CUDA 13.1 deterministic reductions”; “Vulkan SPIR-V floating point precision”; “Noita GDC”; “Oxygen Not Included small mass fixes” | Engine author discussion, vendor specification, official announcements, and GDC session metadata [T10]–[T14] |

Each bibliography URL was **actually opened**. Author abstracts and project descriptions are identified where the full paper was not available. Search snippets alone are not evidence. Three parallel research contributions were reconciled, and the report author independently reopened the cited public sources. Technique costs below are analytical work estimates unless explicitly labeled as a source's measurement. Recommendations, thresholds, schemas, and algorithms are this report's design proposals.

### Repository access and reproducibility

The requested workstation paths were not mounted in this workspace. In particular, /home/liory/scripts/codex-briefs/_lane-rules.md and /home/liory/projects/undertow/.claude-worktrees/matflow were unavailable. The repository's docs/notes/LANE-RULES.md, AGENTS.md, CLAUDE.md, and .claude/skills/kernel-authoring-rules/SKILL.md were read instead. CodeGraph was attempted first but its CLI and a callable index were unavailable. Targeted repository-tree discovery and rg over downloaded, read-only source snapshots provided the fallback.

GitHub reads pinned the following inputs:

| Repository and requested branch | Inspected commit | Scope |
|---|---|---|
| Lint111/undertow · main-wave | b22dce29434295ad9813a4ddcbc19199d11d1fed | Rules, rulings, current extraction/economy/resource code, and materialization design |
| Lint111/Yeroket-Fantasy · main | c3fb3847979e3f9eef04783510c42b70d7058ae7 | Kernel schema vocabulary, frontier implementation, bounds witness report; matches Undertow's kernel.pin |
| Lint111/VBVS--VIXEN · wave/authoring-convergence | bd8e3f2588fc20eeabdbf57b8bc6ae7d6bbd1360 | The two requested physics research/dispatch documents |

These are pinned source observations, not a claim that the requested local lane was checked out. KFR and the launcher were not independently audited. VIXEN coverage was the requested architectural documents, not every current native implementation. No build, product test suite, or solver benchmark was run. A small standalone arithmetic check was used only to illustrate the precision of the existing double mass representation.

The relevant source files and pinned repository entry points are listed in References. File:line citations below refer to these snapshots; later branch movement does not change their meaning.

## What exists in our tree

### Current extraction and resource economy

| Verified location | Observation | Consequence for matflow |
|---|---|---|
| Undertow core/src/Undertow.Content.Core.Declarations/StarSystem/Mass.cs:7–22 | Mass stores a double Kg; EarthKg is 5.972e24. Arithmetic operates on that double. | Existing mass is useful for astronomical quantities but cannot itself implement exact gram-level planetary depletion. |
| core/src/Undertow.Generation/StarSystem/Extraction/Extraction.cs:12–31,38–55 | Extraction selects planet/moon material, checks depth and availability, then subtracts from material, layer, and body and updates a MassDeltaTracker. | Join the physical chain at an explicit depletion/transfer transaction; changing visual particles alone would leave accounting disconnected. |
| same/ExtractionTypes.cs:26–38 | MassDeltaTracker accumulates a double delta relative to original mass and tests a fractional recomputation threshold. | It tracks recomputation debt. It is neither a second physical holding nor an exact material-ownership ledger. |
| same/DepthAccess.cs:3–13 | Depth access is a geological/technology tier. | It is unrelated to the proposed metric depth below a loose pile's current free surface. Preserve that distinction in names and policies. |
| core/src/Undertow.Content.Core.Declarations/StarSystem/MassModel.cs:9–35 | Body layers retain per-material mass and layer structure. | Preserve source-body/layer/material provenance when material becomes loose matter. |
| core/src/Undertow.Content.Core/Systems/Economy/EconomyCommands.cs:74–108 | ExtractRaw reconstructs the tracker, performs extraction, stores the tracker, and captures depletion state. | The exact ledger must integrate with this current path and its persistence boundary. |
| same/ExtractionSystem.cs:509–523 | After successful extraction, surface placement is resolved; the path can return if placement is unavailable. Stock is then credited with extracted mass multiplied by extraction efficiency. | Reserve a valid destination before depletion. Make the difference between removed source mass and useful product an explicit retained fraction, waste, tailings, or another authored disposition. This is a source-level integration risk, not an executed bug reproduction. |
| same/ExtractionSystem.cs:331–355 | A coarse extraction path computes production and publishes stock. | Reification and scope changes must not run both coarse credit and local physical production for the same extraction. |
| core/src/Undertow.Content.Core/Systems/Stock/StockKinds.cs:15–27; StockSystem.cs:9–35 | Placed stock and persistence now exist, with faction/place/resource amounts stored as Num Kg. | The archive's “later economy/storage” description is historical. Matflow must reconcile an existing economy, not assume a blank inventory layer. |
| core/src/Undertow.Content.Core/Systems/Economy/ExtractionKinds.cs:22–106 | Extraction, economy save roots, recipe flows, building recipes, store flows, and link buffers exist. Some declarations retain legacy raw IDs, strings, or marker types. | Reuse their domain relationships and ownership, while following current typed declaration rules for new work. Do not copy legacy declaration defects. |
| core/content/core/resources/resources.utdl:15–45; core/src/Undertow.Content.Core.Declarations/ContentKinds.cs:148–156 | Extractable raw resources have density values; refined goods include physical materials without equivalent physical data. Power and labor use conservation: flow. | Add an explicit resource-to-physical-material/phase association. Economic “flow” is not a liquid/gas classification, and positive raw density is not a complete physical-material predicate. |

**A concrete precision boundary.** For the existing EarthKg constant, binary64 spacing is 1,073,741,824 kg. A standalone IEEE-754 check gives EarthKg − 1 kg = EarthKg. Conversely, signed 64-bit grams cover approximately 9.22 × 10^15 kg, well below planetary mass; representing the EarthKg magnitude in grams needs about 93 magnitude bits. The design therefore needs bounded local balances plus a wider or composed source-total representation. “Use integers everywhere” without an explicit range design is insufficient.

**Gravity must count an assembly consistently.** Mining material into a nearby pile changes deposit custody without necessarily removing mass from the planet and its attached/nearby material system. If the old body mass is reduced, gravitating loose matter must compensate according to the chosen gravity representation; if body mass already includes the pile, do not count it again. This is a design consequence of the current subtraction path, not a claim about an audited gravity-consumer implementation.

### Existing architecture and governing constraints

| Evidence | What it establishes and how this report uses it |
|---|---|
| VIXEN/Vixen-Docs/03-Research/Voxel-Field-Physics-Research-2026-07.md:24–68,423–606 | Recipes, SDFs, voxel assets, materialized deltas, and sparse fields are the intended authority. Local fluid regimes, arbitrary gravity, independent render/simulation resolution, sparse state, and activation/prediction are already covered. Matflow specializes this direction for custody, machinery, depth transitions, and morphology. |
| VIXEN/Vixen-Docs/01-Architecture/Kernel-Physics-Dispatch-Contract-Spec-2026-07.md:84–108,138–355,423–548 | Historical consumer-to-native slot/stage/scale/chain design. Its missing-systems list is dated; later rules require existing generated-plan and runtime mechanisms, not a new PhysicsDispatchGraph or FieldResourceRegistry. |
| Undertow docs/in-progress.md:24641–24660,24901–24958, R232/R238/R239 | Two Gaia authorities, sim and voxel, share one access-derived schedule. A runtime is a lowering/consumer choice, not another world. Atomic dig edits wait on full materialization capacity. Authoritative GPU physics requires the complete voxel-physics transfer/publication path, not merely a fast shader. |
| docs/in-progress.md:25551–25562, R256; reports/research/2026-09-27-R18/research-R18-material-and-mechanism-simulation.md:7–19,286–372,713–727 | An accepted portfolio already favors MPM for dense granular material, discrete consequential pieces, conservative transfers, ordinary machine kinds, CPU/server authority, and shared physical/acoustic material content. The report's older attribute-family proposal is corrected by R255/R256 to literal fields on the same kind. Matflow specializes this accepted program rather than replacing it. |
| Undertow .claude/skills/kernel-authoring-rules/SKILL.md:35–56,179–182,221–262,341–371,432–467; docs/design/2026-09-10-materialization-dispatch.md | Schema kinds, generated typed references, implicit identity, args-to-query dispatch, derived scheduling, and system-plus-queue materialization are the approved vocabulary. Prefer a derived result for a pure computation; use a queued materializer when work must create or update stored state. |
| Kernel Packages/com.yeroket.utility.kernel-framework/Runtime/Schema/SchemaTypes.cs:14–26,47–59 | Num, Float, Int, Long, UInt, generated typed references, and lists exist. A native 128-bit quantity marker is not evidenced here. Wider totals must first be demonstrated with existing forms, or raised as an owner decision. |
| Undertow docs/in-progress.md:24091–24104, R213 | Declared capacity/value behavior and no runtime throw. A full capacity may refuse a transfer or defer refinement; it must never silently saturate away physical mass. |
| docs/in-progress.md:30318–30421, R419;30696–30710, R424 | The art direction includes bimodal lighting and material-dependent edge treatment. Hot/cold render streams concern update behavior. Out-of-core residency is an additional requirement from this brief, not automatically implemented by that stream split. |
| docs/in-progress.md:30917–30938, R435 | An ordered virtual/stored voxel stack, deletion of overwritten stored payloads, and bounded semantic history constrain persisted spatial material changes. Unlimited per-tick voxel snapshots are not the answer. |
| docs/in-progress.md:31020–31046, R437;31321–31329, R450 | Generated per-pack native migrations, ordered pack digests, and historical schema views govern saves. Migrate old rows before pack-set changes; loss requiring salvage must be explicit. |
| docs/in-progress.md:31083–31090, R438 | Compiled/unrolled recipe variants and complete cost accounting. No runtime material interpreter or generic solver language is proposed. |
| docs/in-progress.md:31168–31194, R441/R442 | Bounds guards and revision-based reuse are exact optimizations with unguarded oracles and explicit dependency coverage. An approximate bulk model or motion threshold is a separately authorized simulation choice. |
| docs/in-progress.md:31198–31208, R443;31225–31233, R445;31266–31280, R447 | Material-table static data, dependency-proved static/dynamic channels, lossless authority, material output slots, and generated renderer handling. Compact derived data being useful for wire transport does not itself define a network protocol. |
| docs/in-progress.md:31333–31355, R451/R452; Kernel Runtime/Dispatcher/FrontierPhase.cs:1–157 | Frontier machinery exists, and the chosen typed-query direction carries bounded state through generated blackboard slots. Treat the ruling's new query form as a target to verify before relying on its generated spelling. |
| docs/in-progress.md:31360–31405; Kernel reports/boundscore-run3.md:24–28 | Bounds work includes reported CPU/SIMD and conditional GPU witnesses; unknown operators remain unpruned. The reported Dozen evidence is narrow and does not prove a new contact/pressure solver is deterministic on GPU. |

No new attributes, handwritten renderer glue, world/context service bags, manual entity identity, or parallel authored material catalogs are recommended. Some old source examples visibly conflict with newer rulings; the newer rule and generated form take precedence.

## Technique survey

### How to read the costs and dispositions

N denotes active particles; C candidate contacts; k primitives in a clump; G active grid cells/nodes; H heightfield columns; E active graph edges; I solver iterations; S physics substeps per gameplay tick. Costs include these multiplicative factors, not just particle counts. Spatial indexing, geometry updates, sparse activation, conversion, and rendering are additional work. CPU/GPU descriptions identify plausible implementation domains, not measured VIXEN performance.

**Adopt** means adopt the design principle or a scoped representation; **trial** means implement a bounded comparison with an acceptance gate; **watch** means useful evidence but too much cost or uncertainty for the first slice; **reject as universal** means it may remain valid in a restricted regime.

### Granular matter, depth, and shape

| Technique and primary source | What it does; place in handling chain | Cost and state | Conservation/determinism and project fit | Disposition |
|---|---|---|---|---|
| Frictional particle PBD [G1], 2014 foundation | Position constraints approximate granular contact for spills, scoops, piles, buckets, and chutes. | Approximately S·I·C after neighbor search; bounded CPU first, parallel variants later. | Fixed parcel mass is straightforward. Projection, averaging, friction, contact order, and stabilization require mechanical residual tests. SDF contacts fit field authority. | **Trial bounded comparator; replacing the R256 sand default needs an explicit owner choice.** |
| Sphere DEM with rolling resistance [G6], 2021 | A torque/contact-history proxy for angularity and rolling obstruction. Useful for sand/gravel handling. | Contact work plus spin and possibly persistent rolling history; stiffness controls S. | Repose agreement does not guarantee shear, dilation, or porosity. Save history when it influences the next step. | **Trial calibrated proxy.** |
| Small rigid multisphere clumps [G10], 2023 | Hidden bumps/elongation create actual contact geometry for ore, grain orientation, outlets, and jamming. | Transform N·k primitives; naive pair narrow phase can approach C·k². Rotation/inertia and multiple contacts add work. | The rigid clump owns one mass, not one mass per overlapping sphere. Ordered contact generation and consistent prototype mass properties are necessary. | **Trial 2–4 primitive shapes in bounded regions.** |
| Superquadric DEM [G6], 2021 | Compact aspect-ratio/blockiness parameters provide a continuous shape family. | More difficult nonlinear contact geometry than spheres; orientation and contact state. | Good morphology control; robustness and stiffness change with shape. Not a cheap replacement by assumption. | **Watch; offline calibration comparator.** |
| Polyhedral DEM [G13], 2023 | Facet/edge contacts for angular ore, wedging, hoppers, and screw conveyors. | Candidate pairs plus geometric overlap work; GPU paper is not a game-budget proof. | More direct shape mechanics; expensive narrow phase and persistent contacts. Contact primitives need not make render meshes authoritative. | **Trial only a small population of coarse ore bodies.** |
| Drucker–Prager granular MPM [G2], 2016 foundation | Dense sand becomes an elastoplastic continuum; useful for collapsing piles and yielding interiors. | S·(particle stencil work + G updates); deformation and plastic history at material points. | Material points naturally retain constant mass; transfer, contact, angular momentum, and dissipation depend on the scheme. Grain-scale clogging needs another model. | **Retain R256's accepted direction; first bounded CPU sand trial.** |
| MLS-MPM/CPIC [G3], 2018 foundation | Particle/grid transfers and compatibility support discontinuities, cutting, and two-way rigid interaction. | Local stencils, sparse grid, compatibility data; CPU then SIMD/GPU. | A discretization/coupling technique, not a sand constitutive law. Useful for thin shovel or container boundaries. | **Trial with MPM; avoid a second material declaration language.** |
| Scalable GPU MPM [G5], 2020 | Sparse layouts, AoSoA blocks, and fused G2P2G reduce data movement. | Large GPU workloads; cited demonstrations include seconds or minutes per frame. | Different integration state may need persistence. Parallel reductions and work ordering still need the project's parity tests. | **Watch optimization after CPU correctness.** |
| Hybrid discrete/continuum grains [G4], 2018 foundation; [G9], 2023 thesis | A dynamic partition, overlap, enrichment, and homogenization retain grains where discrete effects matter. | Both solvers plus coupling, classification, and conversion. | Closest precedent for the requested tiers. The opened thesis abstract supports conservative transfer goals, not a ready implementation. | **Adopt principle; trial transfer operators separately.** |
| Granular continuum plus discrete intruder [G7], 2022 version | MPM sand coupled to a rigid/discrete impacting object; useful for tools and boulders. | Continuum solve plus solid contacts. | Experimental force/intrusion/splash comparison is relevant. This is not the same problem as replacing individual sand grains at a moving DEM/MPM boundary. | **Trial coupling reference.** |
| Adaptive granular sampling [G12], 2025 | Boundary, boundary-neighbor, and interior classification; gradual merging/splitting and visual upsampling. | Reduced simulation N plus classification/conversion and a separate render population. | Merge mass/momentum properties do not prove the whole PBD pipeline conservative. Changing parcel size must not change the physical grain/aperture ratio. | **Trial depth classifier and bounded transitions.** |
| Heightfield/repose transport [F9], 2023 | A small number of values per column approximates shallow sand/water beds. | O(H) flux work; substantially less state than full 3D. | Eligible shallow patches need a consistent local down direction; reframing needs validation. The paper prioritizes mass over momentum. A single heightfield cannot represent arbitrary caves, overhangs, or tumbling buckets. | **Adopt only an eligible specialization; reject as universal.** |
| Falling-sand cells [T13], 2019 foundation | Local cellular matter and destructible-world interaction are demonstrated by Noita. | Active-cell stencil work, with scheme-specific scheduling. | Exact integer transfers are possible as a design, but the opened talk abstract does not verify Noita's exact chunk/threading/conservation algorithm. | **Option C trial; internals remain unverified.** |
| Learned constitutive closure [G8], 2022 | Physics-informed elastoplastic surrogates replace part of a material update. | Compact model evaluation plus history; training/validation cost outside runtime. | The paper does not demonstrate a shape-to-jamming game model. Bound extrapolation and preserve conservation outside the model. | **Watch; prefer fitted tables initially.** |

### Liquids, gases, and sparse fields

| Technique and primary source | Role in handling chain | Cost and state | Conservation/determinism and project fit | Disposition |
|---|---|---|---|---|
| SPH/WCSPH [F1] | Meshless spills, jets, containers, and free surfaces. | S·N·neighbors; pressure stiffness may force small steps. | Fixed particle mass helps accounting, while density and occupied volume can drift. Stable neighbor ordering is required. | **Comparator, not the default water trial.** |
| IISPH [F2], foundation; current reference [F1] | Iterative pressure-based incompressibility for local fluid handling. | Neighbor loops multiplied by pressure iterations. | Density targets and convergence can be witnessed; wall reactions and boundaries still require audits. | **Reference/comparator for local SPH.** |
| DFSPH [F3], [F1] | Density and divergence corrections for scooping, spills, and moving reservoirs. | Two pressure-related solves, iteration limits/residuals, neighbor state. | More explicit incompressibility control than a visual-only criterion; no automatic global momentum or device-determinism guarantee. | **First bounded liquid trial.** |
| Position-based fluids [F4], 2013 foundation | Robust interactive incompressible-looking particles. | S·I·N·neighbors with density projections. | Fixed mass is not exact volume; artificial pressure and velocity postprocessing alter response and energy. Symmetric internal corrections do not alone validate the full boundary-coupled solver. | **Cheaper visual comparator if owner accepts quantified deviations.** |
| Eulerian pressure grid + FLIP/APIC [F5] | 3D tanks, pools, jets, and hull interactions; particles carry detail through a grid solve. | Particles + G cells + iterative pressure; sparse topology adds overhead. | APIC's result concerns specified transfers, not arbitrary reseeding or all boundary conditions. Use conservative mass transport and explicit reactions. | **Option B trial for larger liquid domains.** |
| Narrow-band FLIP [F6], 2016 foundation | Keeps detailed particles near the surface and uses the grid in deeper liquid. | Particle work shrinks; volumetric pressure work remains. | Combination/resampling bands prevent naive unstable coupling. Thin sheets offer less interior to remove. | **Adopt architecture lesson; later trial.** |
| Adaptive phase-field FLIP [F7], 2025 | Adaptive two-phase fluid representation for very large simulations. | Adaptive particles/grid/pressure solve; substantial infrastructure. | Consistent transport is a core goal. Very large offline counts are not a runtime budget. | **Watch for Option B multiphase development.** |
| Adaptive SPH splitting/merging [F8], 2024 | Spatial refinement changes particle resolution in selected regions. | Conversion and neighbor costs in addition to reduced active detail. | Source is an astrophysical application, not a bucket/machinery validation. Coarse/fine errors and feasible splits must be checked. | **Trial only after uniform fluid reference.** |
| Shallow water / sand-water heightfields [F9], 2023 | Broad puddles, wash beds, and nearly horizontal pools. | O(H) local transport plus internal state. | Mass flux can be conservative; vertical velocity structure and general overhang geometry are absent. | **Restricted optimization with a 3D fallback.** |
| Lattice Boltzmann; FluidX3D [F11] | Local lattice collision/streaming for selected fluid/air workloads. | O(Q·G) distributions per step; bandwidth and memory can dominate. | Weak compressibility and boundaries need tests; the cited free-surface mode omits gas dynamics. FluidX3D is OpenCL and noncommercial/source-available at retrieval. | **Watch; not a drop-in Vulkan dependency.** |
| LBM–MPM air/water/sand [F10], [F10-code], 2026 | Saturation, water retention, and coupled wet granular behavior. | Fluid distributions, MPM state, and coupling stages. | The available example implementation is 2D CUDA/Taichi. Neither general 3D gameplay cost nor Vulkan parity is established. | **Later slurry research trial.** |
| SPH rigid-fluid coupling [F15], 2023 | Accurate wall treatment and rigid reactions for buckets, gates, hulls, and tools. | Boundary samples/operators plus fluid neighbors. | Boundary accuracy directly controls leakage and measured machine loads. | **Trial alongside first fluid slice.** |
| OpenVDB/NanoVDB [F12a], [F12b]; SPGrid [F13]; GVDB [F14] | Dynamic sparse-volume infrastructure, compact field access, paging/layout, and compute/render support. | Grid topology, tiles/leaves, halos, and representation conversion. | These are data structures/infrastructure, not fluids or conservation laws. SPGrid's virtual-memory design and GVDB's CUDA focus are target-specific. | **Study layouts; adopt only after native integration measurement.** |

### Transport and determinism

| Technique/source | Chain role and cost | Conservation, scope, and fit | Disposition |
|---|---|---|---|
| Aggregated pipe segments [T1]–[T4] | A connected segment stores common contents; work centers on ports and topology. | Factorio's prototype, launch, and later revisions differ. Instant availability and mixing are authored abstractions; splitting/merging must retain contents. | **Owner option for remote or simplified industrial networks.** |
| Ordered belt runs/gaps [T7], 2017 foundation | Advance parcel sequences through long conveyors; exceptional work at deposits, withdrawals, junctions, or spills. | Preserve order, distance, composition, and mass without updating every rendered grain. Historical item-belt optimization does not make free sand constant-cost. | **Adopt principle for constrained belts.** |
| Throughput, head lift, directional valves [T8], 2020 | Distinct player controls for pipes and pumps. | Official Satisfactory feature evidence; internal equations were not verified. Useful requirements, not a copied solver. | **Adopt the distinctions if pressure gameplay is chosen.** |
| Quasi-steady hydraulic graph [T9], 2020 | Solve heads, edge headloss, and nodal continuity for filled single-phase pipes. | Sparse nonlinear iteration; tanks store quantity. Not a gas/slurry/free-surface/transient-pressure solver. | **Later pressure option.** |
| Controlled CPU determinism [T10], 2024 | Stable creation/contact order, predictable parallel merging, controlled math, and cross-platform tests. | Race freedom alone is insufficient. Hidden state matters for replay/rollback. | **Adopt first-authority discipline.** |
| Reproducible reductions [T11], 2025; Vulkan arithmetic rules [T12] | Reduction ordering and arithmetic contracts across backends. | CUDA-specific modes illustrate scoped guarantees. Vulkan contraction/precision/denormal behavior still needs feature checks and witnesses. | **Adopt validation requirements; do not infer portable GPU lockstep.** |
| Mature-game small-mass fixes [T14], 2026 | ONI's official maintenance notes include small-mass and phase-transfer defects. | Supports stress-testing tiny flows and phase changes; does not reveal its full cell solver. | **Adopt test cases, not an unverified implementation.** |

### Source measurements that must not become our frame budget

The 2025 adaptive-granular paper reports an excavator example with approximately 53,000 coarse simulation particles, 1.5 million display particles, and 14.55 ms per simulation frame on an RTX 4080 SUPER, using a 0.002 s timestep. That is not evidence of one second of simulation per second at 60 display frames: eight or nine such physical steps may be needed per display frame before other costs. Its useful contribution is the adaptation strategy, not a transferable throughput promise. [G12]

The 2020 multi-GPU MPM demonstrations include seconds/minutes per frame, and Chrono's cited active-box rover example still takes about 30 hours for 15 simulated seconds on two A100s. Narrow-band FLIP reduces some work while retaining pressure cost; its reported full-example improvement remains an offline result. These are valuable architectural precedents, not product performance claims. [G5] [G10] [F6]

## Design options and recommendation

**Precedence:** R256 already selected the broad material portfolio, and R239 fixes two data authorities under one schedule. The choices below concern implementation scope, depth reduction, and the new handling/shape mechanisms. Option A keeps CPU Drucker–Prager MPM as the dense-sand default. PBD is a bounded cost/fidelity comparator, not an unrecorded replacement. If the owner deliberately chooses it for local sand handling, its quantitative gates still apply.

### Option A — bounded local material models with conservative spatial bulk and physical ports — recommended

**Chain.** Geological extraction removes a reserved source quantity into active granular parcels. They collide with recipe/SDF machinery, spill into a world pile, can be scooped into a spatial container, and cross a physically reachable typed intake. A machine owns admitted work in process and emits product plus all other material outputs through ports. Chutes retain free granular dynamics; constrained conveyors and pipe segments may use finite stored runs/compartments. Local liquid handling is added separately with DFSPH.

**Tiers.** Begin with a bounded CPU MPM sand region and sleeping material points retaining the same continuation state; admit discrete contacts for consequential pieces. Later replace only validated, resting, supported interiors with spatial bulk cells/regions containing mass, composition, occupied geometry, packing, support/reaction state, and necessary closure history. Keep an active shell and interaction regions. A transition band handles conservative conversion and boundary tractions. Dynamic coarse regions must have a validated mechanical model; “bulk” cannot mean a shape-free inventory entry beneath a cosmetic surface.

**Hidden geometry.** First establish a full-detail reference for the selected sand model and a separate discrete contact fixture. Trial true small rigid clumps with angular dynamics for morphology-sensitive materials, then fit inexpensive bulk/rolling/constitutive parameters within a declared operating range. Shape-driven jamming is resolved at grain scale where it matters; an oversized numerical parcel is not allowed to become a fictitious boulder blocking an aperture. R256's continuum parcels do not need durable identity as individual physical grains unless their particular identity is consequential.

**Integration.** Existing schema kinds hold quantities, material references, port relations, and persistent state. Generated CPU systems own simulation phases; a materializer queue provisions spatial detail where needed. Rendering derives from authoritative occupancy and active state through generated slots. Saves persist exact holdings and continuation state; networking begins with authoritative snapshots/deltas.

**Why recommended.** This option makes the whole gameplay chain reviewable using the accepted local material direction before funding general adaptive continuum/discrete and multiphase coupling. Its principal risk is reduced-interior validity: deep support loss or broad motion may wake a large region. The budget must allow a declared fallback or slower authoritative progress rather than invalid frozen matter.

### Option B — coupled sparse continuum with discrete interaction regions

Use granular MPM for dense deforming sand, discrete particles/clumps at exposed, ballistic, narrow-aperture, or contact-sensitive regions, and conservative interface coupling. Use sparse Eulerian pressure plus FLIP/APIC for larger 3D liquid regions, reducing deep particle detail while retaining the necessary coarse pressure field. Wet sand and slurry become a later explicit multiphase model.

Extraction, containers, ports, machine work in process, and graph transfers use the same exact quantity contract as Option A. Shared sparse addressing and generated dispatch can serve multiple solvers, while each material class retains its own constitutive update and state. Continuum cells retain stress/plastic/fabric or phase state when needed; rendering is independent of material-point density. Saves include the actual integration state, including grid-carried modes if the chosen method requires them.

**Benefit:** stronger candidate for broad yielding interiors, sloshing, erosion, and pressure transmission. **Cost/risk:** two-way coupling, adaptive pressure, stencil compatibility, solver residuals, state conversion, and CPU/GPU parity become substantial projects. Hidden geometry reaches the continuum only through a validated closure; MPM does not directly preserve every grain interlock. Choose this if moving deep piles and wet-material physics are central enough to justify the larger program.

### Option C — conservative cells, eligible heightfields, and spatial compartments

Use finite-volume/cellular transfers for world matter, heightfields only in shallow eligible regions, and finite spatial compartments/runs for containers, belts, pipes, and machines. The exposed layer receives more frequent/finer cell updates and visual grains; deeper cells use a coarser authored transport law. Exact disjoint cell quantities and face-flux correction handle depth changes.

Material-table morphology affects closure tables, repose flux, yield thresholds, and aperture rules. True shape contacts are limited to a small optional coarse-object layer. The data and dispatch integrate most simply with generated field updates and deterministic integer transfers; spatial contents, flow residuals, and topology are persisted.

**Benefit:** broad construction/automation gameplay with a smaller runtime envelope. **Limit:** much less direct grain mechanics, angular packing, arbitrary 3D shear, and emergence of jamming. It satisfies physical custody and visible matter, but fulfills the owner's hidden-geometry goal mainly through calibrated laws. Choose it explicitly if that fidelity tradeoff is acceptable.

### Comparison

| Criterion | Option A | Option B | Option C |
|---|---|---|---|
| Earliest complete chain | Strong | Slowest | Strong |
| Shape-sensitive scooping/jamming | Bounded discrete fixture, then selected coupled hotspots | Broad adaptive discrete/continuum coupling | Mostly closure/heuristics |
| Deep dynamic interiors | Wake full MPM region; cheap reduction has a restricted validity range | Broader adaptive dynamic coupling | Approximate transport |
| General liquid pressure/slosh | Local fluid solver, then expansion | Core strength | Limited compartment/cell model |
| Cost control | Active detail + eligible reduction | Sparse fields + adaptive solvers, high infrastructure cost | Most predictable |
| Deterministic CPU introduction | Manageable bounded scope | Larger numerical surface | Easiest if flux/order contracts are explicit |
| Recommendation | **Start here** | Escalate when required mechanics justify it | Owner-approved stylized alternative |

## Depth tiers and conservative handover

### Track three different things

1. **Solid boundary:** the current terrain/tool/container recipe or SDF, including stored modifications.
2. **Material boundary:** current loose-material occupancy, volume fraction, or particle-neighborhood surface, including internal voids and free interfaces.
3. **Activity and validity:** tool contact, support/load changes, ports, material interfaces, stress/strain or pressure residuals, gravity changes, and validity of the coarse closure.

The free-surface field must update after digging, pouring, erosion, or transfer. An unchanged recipe SDF cannot by itself locate the new surface of a spilled pile. A boundary/neighbor/interior classifier is a useful first implementation; a metric distance band can follow when it adds value. [G12]

The candidate full-detail domain is the union of the exposed shell and all required interaction/instability regions. Build a transition halo covering solver support and travel before synchronization, plus relevant stress-wave propagation or complete dependent stencil updates. Nearly stationary grains can transmit disturbances. Add handling for fast tools and thin walls, and hysteresis/dwell before coarsening; an outlet or support change overrides that delay. Incompressible pressure has the broader coupling requirement described below.

R441 can prove a query does not intersect a relevant region, or that an authored operation's certified bound permits skipping. R442 can reuse outputs with unchanged complete dependencies and arithmetic order. Neither proves equilibrium merely from depth or low speed. An approximate sleep threshold or bulk law belongs in the selected simulation model and its acceptance budget.

### Sleeping, reduced bulk, and active mechanics are different states

~~~mermaid
flowchart TD
    A["Active material"] -->|"Validated rest"| B["Sleeping full state"]
    B -->|"Support or boundary changes"| A
    B -->|"Closure valid; conversion fits"| C["Spatial bulk interior"]
    C -->|"Detailed interaction; feasible reconstruction"| A
    C -->|"Changed state remains in validated regime"| D["Coarse mechanical solve"]
    D -->|"Detail required"| A
    D -->|"Validated equilibrium"| C
~~~

The first sleep implementation retains particle identity, position, velocity, orientation when present, and continuation-relevant contact/solver state. It saves computation without claiming that a reduced model can reproduce the old contact network.

For MPM, retaining point state is not enough to permit skipping arbitrary resting points in particle-to-grid work. Those points still contribute nodal mass, stress, and gravity/support balance, and active neighbors may share their interpolation stencils. Sleep an isolated region with validated boundary reactions, or retain/reuse the necessary grid contributions under complete dependency checks and wake overlapping stencil support. A state-preserving implementation that cannot skip meaningful work is correct but has not earned its performance claim.

Bulk conversion is more ambitious. A resting region needs occupied shape, packing/void volume, mechanical support, and a path for boundary force and torque. Every contact step must deliver equal-and-opposite reaction to the supporting body or an explicitly fixed external environment. A static bulk collision surface with no reaction path behaves like an infinite-mass object.

Undercutting can mobilize a connected pile far beyond a fixed-radius wake band. Complete required wake/recoupling before advancing physics with the altered support. If allocation exceeds capacity, an optional player-triggered extraction can wait before committing. An unavoidable collapse requires a validated coarse fallback, or slower authoritative progress through a bounded staged/out-of-core algorithm that actually fits memory. More time alone cannot repair a hard particle/contact/state cap. Silently freezing invalid support or discarding contacts is not an admissible overload policy; this distinction applies to every slower-progress option in the report.

### Deep liquids need an interior response

Narrow-band FLIP removes many interior particles but keeps a volumetric grid pressure solve. Pressure in an incompressible connected region is not a local disturbance that can always wait for a finite wake front. [F6]

Use one of three explicit interior laws: a coupled coarse pressure/velocity field, a validated compartment/reduced model with interface reactions, or hydrostatics while its equilibrium conditions hold. A changing inlet, immersed moving object, outlet, gravity field, or accelerating tank can invalidate hydrostatics. For arbitrary gravity, equilibrium requires grad(p) = rho·g; a general nonconservative or changing field need not admit it. A rotating container also needs either world-space dynamics or the appropriate frame forces.

### A handover is an accounting and mechanical operation

Use a synchronized transaction, not an opacity blend:

1. Identify the affected ownership region and its physical state at a defined tick/substep boundary.
2. Reserve destination capacity, valid geometry, and required state/halo storage before changing the source.
3. Restrict or prolong exact material quantities, constituent fractions, center of mass, linear/angular momentum, and required energy/history.
4. Define one ownership map. Ghost particles/cells and overlapping interpolation samples contribute zero additional inventory.
5. Couple tractions or fluxes across the band. Accumulate fine-substep fluxes, then correct the coarse face to the same integrated flux; do not accept both independent predictions.
6. Commit the representation change once, invalidate dependent spatial/render summaries, and release old state only after its successor is authoritative.

For masses m_i and velocities v_i, merging uses M = sum(m_i) and P = sum(m_i·v_i), with mean velocity P/M. Preserve the center of mass and angular momentum about a consistent origin. The relative kinetic energy lost by a simple velocity average is one half of sum(m_i·|v_i − P/M|²); retaining only mean velocity therefore erases motion. Store an appropriate subgrid/spin state, or declare and account for dissipative transfer and support reactions. Audit gravitational potential where the authored field admits one; otherwise account for displacement and force/work under that field without asserting a global potential-energy invariant.

MPM conversion also carries reference volume, deformation gradient, plastic/hardening history, and affine/APIC modes required by its formulation. Averaging deformation gradients can create inadmissible states; use a constitutively valid projection/reconstruction and account for elastic and affine-motion residuals. A reduced model may preserve only an approved sufficient history, not the original microstate. Occupancy is matched for representation-only conversion; physical compaction/dilation can legitimately change bulk volume.

Reconstruction must fit material into feasible volume without wall intersection or unphysical overlap. Failure to find admissible packing means defer or retain the old representation; an arbitrary separating impulse is not a conservation fix. Merge/split mass properties and ghost ownership need standalone witnesses before enabling a moving depth boundary.

### Depth acceptance gates

These are proposed engineering gates, not paper results:

| Witness | Initial acceptance target |
|---|---|
| Integer quantity through 100,000 representation round trips | Exactly unchanged by material/constituent and globally; no duplicate owner or negative balance |
| Transfer-only linear momentum | Report absolute residual and relative residual against max(sum(norm(m_i·v_i)), M·v_floor); target relative residual ≤ 10^-6 in the isolated CPU conversion fixture |
| Angular momentum, center of mass, and occupied volume | Explicit residuals; no unexplained torque, spatial displacement, or invalid packing; agree on scale-dependent tolerances before accepting |
| Mechanical dynamics of a reduced pile | Compare to the same full-detail baseline at matched simulated time, material, geometry, step size, and driving conditions |
| Repose and discharge | Initial targets: repose within 2 degrees, non-negligible steady discharge within 5%; also absolute leakage/low-flow and jam-onset bounds |
| Boundary crossing and excavation | No unaccounted transition-induced impulse or surface jump; use a world-space surface tolerance tied to physical resolution/fixture geometry, plus a separate visual/pixel check. Real collision impulses and physical topology changes remain valid |
| Undercut, moving container, deep outlet, and gravity change | Correct wake before altered mechanics; no unsupported frozen islands or capacity-triggered matter loss |
| Runtime benefit | Target at least 2× complete cost reduction against the always-active thick-pile fixture; also compare against already-sleeping full state to isolate bulk reduction. Include classification, wake, conversion, SDF updates, all substeps, and p95/p99 |

The tight transfer residual does not assert that an entire MPM or frictional PBD trajectory conserves momentum to that tolerance, or that discarded microhistory is recoverable. Wall reactions, solver dissipation, and accepted constitutive-reduction residuals are measured separately.

## Hidden geometry and emergent behavior

### What is supported

True grain geometry can change packing, alignment, rolling, shear, discharge, and jamming. A controlled 2021 comparison shows why matching a pile's angle of repose is insufficient: models can disagree in shear, dilation, and porosity. Measurements of elongated particles also support retaining evolving orientation/fabric when alignment changes flow. [G6] [G11]

There is direct precedent for the owner's suspension idea. Blair and Ness model rigid particles assembled from frictionless spherical asperities, with hydrodynamic drag, pair lubrication, short-range repulsion, and contacts. Their 2022 study obtains thinning, thickening, and jamming driven by interlocking. Its tested setting is a two-dimensional monolayer with roughly a hundred aggregates; it establishes a mechanism, not an affordable general 3D slurry solver. Rolling-constraint suspension research gives a complementary lower-cost modeling precedent. [S1] [S2]

The implication is positive but specific: geometry can produce nontrivial rheology, while the carrier fluid, stress-activated contacts, concentration, and history still matter. Dry contacts alone do not define every liquid or suspension law. Wet ore requires coupled fluid/grain dynamics or a calibrated suspension closure with a declared regime.

### Separate material-static structure from evolving state

| Material-table/static declaration | Per-particle or per-region dynamic state |
|---|---|
| Prototype sphere offsets/radii or shape family; shape weights and size distribution | Selected variant/scale only if not reproducibly derived from immutable identity and saved seed/version |
| Consistent prototype center of mass, occupied volume, and inertia | Position, orientation, linear/angular velocity, and deformation if modeled |
| Base density, contact compliance/restitution/friction parameters, surface interaction law | Required contact histories, compaction, coordination/fabric, plastic strain, damage, wetness, temperature/enthalpy |
| Calibrated closure tables with validity ranges and provenance | Current pressure, shear rate, concentration, saturation, anisotropy, and hysteresis state used by those tables |
| Authored visual material properties and edge treatment | Dynamic render channels derived from the physical state |

Do not store the same prototype on every particle. Generate a deterministic variant from stable identity and a saved material-distribution version when that suffices. If breakage, erosion, wear, or aggregation changes a shape, that change becomes dynamic state. Merging identities must not silently redraw an entirely different distribution.

Overlapping constituent spheres belong to one rigid grain: compute consistent union/effective volume, center of mass, and inertia offline. Summing overlapping sphere volumes as independent physical mass duplicates matter. Multiple contact points can also change effective stiffness and torque; calibrate the contact algorithm together with the prototype.

### Fidelity ladder and calibration

Within the discrete calibration fixture, start with spheres and an explicit contact baseline. Add rotational particles and rolling resistance as a cheap shape proxy. Then compare bounded 2–4 sphere clumps, followed only if necessary by superquadrics or a few polyhedral ore bodies. Feed measured closure parameters into the MPM/bulk law where appropriate. A point-PBD particle has no spin/orientation; adding “shape” to a material table without angular dynamics does not implement clump mechanics.

For each material family, record full-detail reference runs over:

- Loose and dense packing, repose/avalanche, direct shear, dilation, and settling.
- Hopper openings spanning several physical grain diameters, varying orientation, fill height, wall friction, and drive rate.
- Jam onset/probability across multiple recorded seeds, restart after agitation, segregation, and mixed grain sizes.
- Where relevant, suspension shear-rate/stress sweeps, concentration, hysteresis, and settling.

Fit static closure tables or a small physically constrained parameterization from these results. Interior state may need a fabric tensor or an orientation-distribution summary; it cannot generally forget the loading history and reproduce the same contact network. A learned closure is a later comparator, bounded to training-supported regimes, with explicit dissipativity/positivity checks and fallback outside them. [G8]

Coarsening represents a larger **parcel of the same physical grains**. Keep physical grain-size statistics independent of numerical parcel radius. Near a grain-sensitive aperture, reify sufficiently fine contacts or use an openly declared calibrated aperture law. A visual million-grain overlay has no authority to create collectible material or real clogging.

## Conservation and determinism

### Quantity authority and the legacy boundary

Define an exact accounting quantum before implementation. **Recommend a one-microgram candidate for local sand balances**, then validate it against the smallest consequential grain/transfer and the largest local capacity. A whole-gram quantum can demonstrate bulk accounting but is too coarse for many literal sand grains. Signed 64-bit micrograms cover about 9.22 × 10^9 kg locally; the existing EarthKg magnitude would need 113 magnitude bits at that quantum. Finer dust/constituents may require another declared quantum or representation. Numerical positions, velocities, pressures, and surface reconstruction may remain floating point; exact mass accounting does not require pretending those mechanics are exact.

Use bounded signed Long quantities locally after proving each capacity, sum, product, and accumulator range. Planetary source totals require a wider CPU representation or a composed quantity using existing schema fields. The first implementation lane must prove that representation and codec path; a new 128-bit schema marker or backend intrinsic would be a separate owner choice, not an assumption in this report.

Every extraction domain needs **one exact authoritative depletion state**. Material/layer/body aggregates derive from it, with doubles serving numerical/display/gravity roles as appropriate. Continuing to subtract only from a huge double source while crediting an integer local spill would still create matter relative to the source ledger.

MPM's floating-point grid mass is a non-owning numerical projection. The canonical integer quantity stays on its authoritative material representation; do not reconstruct it by rounding projected grid sums. A particle-to-grid transfer is not an economic deposit or a second material holding.

For migration, establish a documented baseline epoch from historical saved quantities, recording conversion policy and any residual. Historical sub-ULP depletion or omitted waste cannot be recovered from a rounded double by declaration. Existing saves may need an accepted reconciliation baseline; do not claim retrospective gram accuracy. The same policy governs old stock rows, coarse production, and partial machine batches.

### One quantity, one owner

At a synchronized state boundary, each mass quantum belongs to exactly one authoritative holding:

| Holding | Meaning |
|---|---|
| Source deposit | Unextracted body/layer/material quantity |
| Active or sleeping material | Physical parcels/particles with exactly assigned quantity |
| Spatial bulk region/cell | A reduced representation with location, occupied volume, and a declared mechanical regime |
| Container compartment | Material physically inside the compartment under its layering/mixing law |
| Conveyor/pipe/in-flight batch | Finite material in transit, with position/order/residence-time semantics |
| Machine intake, work in process, or output | Material retained by the machine at a particular physical stage |
| Explicit environment or waste holding | Tailings, dust, emitted gas, or spilled material still inside the accounting domain |

A reservation, preview mesh, ghost sample, visible subgrain, source recomputation tracker, or aggregate summary is not another owner. A gravitational summary is likewise not a second material inventory. Material crossing an explicitly declared external boundary leaves the domain's holdings and updates an audit counter; that counter is not counted as retained matter. An economy query may summarize holdings. If offscreen economy rows remain authoritative custody, reifying them transfers authority instead of copying their quantity into local particles.

For a closed, nonreacting material, sum all holdings before and after each tick; the difference must be exactly zero. With explicit external flows, the change equals recorded source minus sink transfers. For phase change, preserve material constituents and latent/thermal energy according to the model. For chemistry, material IDs may change; total mass and the declared elemental/constituent balance, rather than each product label, are the invariant. Integer recipe ratios need a balanced batch basis or saved residuals.

### Reserve, solve, and commit

Use a deterministic transfer phase:

1. Read a defined snapshot of source contents, intake accessibility, destination occupancy, material acceptance, topology, and rates.
2. Propose admissible withdrawals/arrivals in integer quanta, carrying material/constituent and energy information.
3. Jointly allocate source availability and destination capacity across competing edges, respecting all incoming reservations and mixture-slot limits.
4. Validate owner generations and state epochs. A changed destination, filter, packing law, or topology invalidates a stale reservation.
5. Commit the accepted signed debit and credit together at a state boundary. If travel is represented, debit into a real in-flight owner and later transfer from it.
6. Retain rejected quantity at its source; cancellation releases a claim and never refunds an amount that was not debited.

Independently clipping each edge is incorrect: a source containing 10 units and two requests for 8 can otherwise authorize 16. Deterministic priorities or a joint constrained allocator solve this. A first implementation can use a simple declared priority policy; if weighted fairness is required, preserve bounded service-deficit/fractional-share state.

Largest-remainder allocation with rotating equal ties does **not** guarantee weighted fairness over time. With one quantum per tick and weights 9:1, unequal remainders can starve the smaller requester forever. Witness 9:1 and 99:1 contention across saves, and distinguish service-deficit state from owned mass. Rate rounding residuals may retain fractional throughput, but blocked whole-unit demand must not accumulate into an unlimited future burst.

Idempotence also needs a bounded-history contract. Requests identify owner generation, epoch, and sequence. A saved committed/processed-result watermark plus a bounded reorder window can reject obsolete retries after individual journal entries are evicted. Persist the final result/watermark atomically with the debit-credit commit; pending reservations remain separate. An old request must not become valid again merely because a ring buffer forgot it or an entity slot was reused. Snapshots occur with a coherent debit/credit boundary, or preserve the explicit in-flight state.

Close this transaction loop with the mechanical solve. Accepted withdrawals/injections become boundary/source terms in the relevant physical substep. A rejected crossing must be prevented or resolved through the wall/intake/backpressure model; leaving its ledger quantity at the source is insufficient if the particle has already moved through the mouth. Partial parcel withdrawal must update solver mass, reference volume, and necessary state consistently, or wait in a real holding until an admissible whole-parcel transfer is possible. Prediction may iterate with arbitration; publish only a jointly valid mechanical and accounting state.

The same rule covers mining geometry: an extraction intent may mark work pending, but the carved source coverage/page and released matter commit together after required materialization. Do not publish the hole first and hope a later particle allocation succeeds.

### Capacity is a physical law

For the first sand slice, choose the packing/volume law together with the MPM constitutive model and bounded container geometry. Derive occupied volume and admission from the same reference/current-volume state. Fixed bulk density is acceptable only as an explicit fixed-packing or bounded-dilatancy simplification with measured occupancy error; it must not contradict an independently compressing/dilating solver. Mixtures, thermal expansion, entrained air, and pressure may change admissible contents even without a transfer.

Reservation validity therefore depends on the occupancy-law revision and relevant dynamic state. If a later state change would overfill a container, perform the authored compression, spill, vent, or blocked transition; do not clamp an accepted mass balance. A full composition table must reject/defer or use a declared compatible representation, never evict a species to make room.

For liquids, mass/rho determines material volume within the model, while numerical density and geometric reconstruction receive independent error tests. A gas requires density/pressure/temperature and an equation of state or explicit reduced law. A liquid-style fixed fullness clamp is not a general gas model.

### Physical intakes and machine lifecycle

A typed inserter controls compatibility at a reachable intake. It does not gain access through walls or extract an arbitrary hidden constituent from a homogeneous mixture. A mixed bulk region supplies its declared mixture; a layered bin exposes its accessible layer; a resolved mouth samples local matter. Selective separation requires an authored screen, sorter, separator, or other physical mechanism.

Machines own intake, work in process, finished product, and byproducts under bounded capacities. Output blockage retains matter. Destruction, unloading, recipe change, and cancellation need explicit recovery rules: a partially reacted batch cannot refund all original input if product has already left. Extraction efficiency partitions useful output and tailings/retained matter; rounding remains in a real balance or recorded residual. Dust or evaporated water is an explicit owner/transfer until it crosses a declared simulation boundary.

### Transport laws: choose semantics before acceleration

**Recommended first law: rate-limited buffered transport with finite contents.** Each physical segment has authored capacity and rate, and old-state proposals are jointly committed. It produces filling, starvation, backpressure, and residence time without claiming a hydraulic pressure solve.

This law has two visible limitations to acknowledge:

- Old-state free-capacity checks can prevent throughflow around a completely filled closed loop, even when simultaneous inflow/outflow would be feasible.
- Adding physical storage bins can add phase latency. Computational chunking or LOD must not silently change the physical discretization or travel time.

Keep physical segments/bins or explicit transit-time parcels independent of worker partitions, render chunks, and frontier traversal. On split/merge, preserve mass, position/order, and residence-time state. Compare the same pipe with different computational partitions.

**Pressure option:** solve simultaneous net continuity, with each node constrained by 0 ≤ M + incoming − outgoing ≤ capacity, and edge laws coupled to pressure/head, pumps, valves, and resistance. A full-pipe quasi-steady graph is a useful scoped next step; it does not include water hammer, partial fill, gases, slurries, or free surfaces automatically. [T9]

For a primed incompressible fixture, non-storage junctions require equal total inflow and outflow; tanks or explicitly compliant nodes update stored quantity. Realize proposed fluxes in balanced quanta. A depleted pipe cannot silently remain in the fully primed regime: either the fixture excludes that state or a declared partial-fill/buffered transition handles it.

Topology changes preserve old segment contents. A removed pipe spills, vents, retains, or transfers its material according to an authored physical outcome. Segment aggregation may be an owner-approved distant model, but instantaneous common availability and mixing are gameplay semantics. Factorio's dated revisions demonstrate why those choices must be explicit rather than inferred from a generic “fluid network” label. [T1] [T2] [T3] [T4]

### Determinism contract

Separate four levels:

| Level | Required evidence | Recommendation |
|---|---|---|
| Exact quantity | Integer conservation, no overflow, balanced commits, codec round trips | Mandatory on every backend and representation |
| Repeatable compiled CPU continuation | Same build/input/seed/state gives identical authoritative ticks, including allocation and scheduler state | First supported contract |
| Cross-CPU/SIMD replay | Compiler/architecture/worker-count matrix; controlled operation order and math; no divergent event decisions | Admit each supported combination after witnesses |
| Cross-GPU/CPU lockstep | Identical numerical/event behavior across actual Vulkan/Dozen/native drivers, supported features, and compiler versions | Do not promise initially; owner may fund separately |

Stable input order, canonical particle/region/edge IDs, sorted contact/neighborhood order, deterministic work admission, and fixed reduction trees matter alongside integer arithmetic. Atomics can remove races while leaving insertion or acceptance order variable. Integer addition is commutative only within proven range, and it does not determine which competing transfer wins. [T10]

Vulkan arithmetic permits choices that ordered summation alone cannot eliminate. Contraction, denormals, division, square root, integer capabilities, and compiler lowering must be verified on the target. CUDA's reproducible reduction modes are evidence that reproducibility is a specific algorithm contract, not a universal property inherited by a Vulkan shader. [T11] [T12]

Use saved deterministic seeds rather than wall-clock randomness. Step counts, region priorities, frontier limits, and overload responses must depend on state and declared capacities, not worker completion timing or elapsed milliseconds. Timers observe performance; they do not choose authoritative matter outcomes.

## Gameplay elements as schema kinds

The following are **proposed domain kinds and fields**, not implemented declarations or newly minted SPT entries. They use the existing Simple/F schema vocabulary, generated typed references, dynamic fields, relations, lifecycle methods, and dispatch patterns. Exact final declaration names remain a coding-lane decision.

Keep spatial material payload and history under the existing voxel-region owner; the simulation world references it and owns machine/rigid-body state as appropriate. Temporary solver arrays are views/workspace, not a third material world or registry. Reuse existing ownership, revision, occurrence, request, and completion mechanisms rather than adding parallel counters or services. The physical material kind must reconcile with R256/R254's shared mechanical/acoustic content; this report proposes additional fields and relations on that program, not another catalog.

### Data ownership map

| Proposed kind/relationship | Authored/static data | Dynamic/persisted data | Systems and physical meaning |
|---|---|---|---|
| Physical material profile linked to a resource/phase | Density/phase law, contact/shape prototypes, closure tables, accepted class relations, thermal/visual properties | Only evolving material state belongs elsewhere | Resolve existing economic resources to physical semantics and the shared material table; do not duplicate catalogs |
| Material holding / constituent rows | Allowed quantity range and composition capacity | Exact quantity, typed owner/material references, necessary energy/residuals | Single accounting owner; aggregate views derive from these rows |
| Active granular parcel or particle state | Solver/profile reference, declared capacity | Position/velocity, quantity, reference volume, deformation gradient, plastic/hardening and affine state as required by MPM; identity/orientation/spin/history where consequential | Generated local mechanics; hidden clump geometry comes from the profile |
| Spatial material region | Resolution/closure policy and bounded state layout | Occupancy, quantities, tier, support/fabric/pressure state, revisions | Select active detail, update sparse fields, and preserve mechanical validity |
| Container and compartment | Shape/volume, accepted classes, packing/mixing law, apertures | Constituent holdings, accessible layers, fill geometry, state epoch | A spatial vessel with bounded contents and physical intake accessibility |
| Inserter/intake relation | Reach/contact region, material/class filter, rate, mechanical mode | Source/destination refs, phase, fractional-rate/service state, in-flight material when applicable | Sample reachable matter, propose transfer, animate a real scoop/pump/feed action |
| Pipe segment and junction | Physical geometry/length, capacity, flow-law parameters, accepted phases | Segment contents, pressure state if used, residence time, topology epoch | Finite material transport; graph partition is an execution detail |
| Conveyor run | Path, width, speed, containment, admissible load | Ordered parcels/gaps, composition, progress, transfer/spill boundaries | Efficient motion of constrained matter, with free granular behavior where containment fails |
| Chute | Collision/shape field, outlet, surface properties | Usually local physical matter and optional region state | Gravity/contact-driven flow; a graph shortcut requires a declared validated approximation |
| Machine port and work stage | Recipe/phase relations, input/output capacity, cycle/kinetic policy | Intake, WIP, products, tailings, stage, progress, residuals | Own material through processing, interruption, destruction, and output blockage |
| Transfer request/reservation/result | Policy and bounded queue/window capacities | Typed endpoints, requested/accepted quantity, epochs, sequence, status | Requests carry intent; an owning system validates and commits authority changes |
| Wake/refinement request and derived region query | Capacity/priority and model-validity policies | Required carried state, revision keys, deterministic pending order | Existing materialization and frontier mechanisms provision/update detail |

### Existing declaration forms, without a new surface

Use Simple(F<...>) declarations with existing marker types: Long for bounded exact quantities, Num/Float for the chosen numerical state, Bool/EnumOf where appropriate, and KindRef to generated target references. A conceptual quantity field is F<Long>("MassQuanta").Dynamic(); a reference uses the generated ref for its actual target kind. No handwritten marker kind, explicit identity column, or CLR-state class is required merely to introduce these domain concepts.

A vector, tensor, composed large quantity, or bounded collection must use an already supported schema shape and demonstrated generated layout; rotations use the ruled unit-quaternion number kind rather than an untyped Float4. This report does not invent a magical MaterialQuantity, Int128, FluidSolver attribute, or a solver DSL. If existing shapes cannot express a needed layout or arithmetic, document the concrete gap and ask the owner to choose a kernel extension.

Kind declaration and code generation own IDs, codecs, and typed references. Static shape/profile data is read through the material relation/table; dynamic flags follow actual dependency proofs. Accepted material classes can be ordinary authored kinds and relations queried by typed signatures rather than ad hoc strings or an unrelated routing registry.

### Systems, queues, and dependencies

Use existing KernelSystem and lifecycle attributes with schema-typed arguments. “SystemExecute” in the brief denotes the execution concept; the current authoring rules do not require that literal method name. The dispatcher owns entity, pair, set, and relation iteration. Bodies operate on supplied data, not a World/Context/Registry object or hand-written table scans.

Producers emit typed transfer or refinement requests; one declared owner drains each mutation queue. Pure surface/bounds/eligibility summaries should prefer a derived result when supported. A materializer is a normal system plus its request queue when persisted detail must be created or modified.

Vertical dependencies represent prerequisite provisioning/derived data. Horizontal dependencies follow typed read/write conflicts and freshness requirements. Same-frame coupling barriers must be expressed by those dependencies, with the existing SchedulePrecedencePolicy only when a genuine ordering requirement cannot be derived. Do not introduce retired AfterSystem/BeforeSystem attributes or a second native task graph.

Network traversal can use the existing frontier implementation and the R452 typed-query direction once its exact generated surface is verified. Blackboard carried-state capacities cover frontier queues, visited sets, component work, and residual state. Traversal computes connectivity/active work; the hydraulic or quantity equations remain a separate compiled stage.

## Scheduling and performance budgets

### Proposed phase organization

The stages below are logical dependency boundaries, not a handwritten imperative runner:

| Phase | Inputs and work | Output/consumer |
|---|---|---|
| Intent and boundary changes | Player tools, machine actions, proposed source edits, and relevant committed gravity/thermal/topology changes | Typed requests and revised dependencies; extraction geometry remains pending until its material commit |
| Residency and validity | Required regions/halos, support and pressure connectivity, destination capacity | Ready state or deterministic deferred request |
| Classification/provisioning | Surface/activity bands, bounds guards, exact cache eligibility, conservative conversions | Valid active/coarse representation and one ownership map |
| Mechanical substeps | Contacts/forces, granular or fluid solve, interface reactions/fluxes, accepted intake boundary conditions | Candidate motion/state and port fluxes reconciled with arbitration before publication |
| Transfer arbitration | Snapshot proposals, accessible composition, joint source/capacity allocation | Accepted signed transfers and retained rejected demand |
| Atomic ownership update | Depletion, transit, machine stages, products/tailings, revision changes | Coherent authoritative tick state |
| Derived outputs | Occupancy/SDF summaries, render channels, network deltas, diagnostic balances | Generated consumers and save boundary |

Some solver stages require iteration or coupling within a tick. Their loop bounds and carried state must be compiled and declared. Integrate rates over the actual physical substeps; changing display frame rate must not change delivery quantity. Moving walls and fast grains need travel limits or validated swept/compatible collision, especially around the narrowest bucket wall and inserter opening.

### Activation and residency

Prioritize regions affecting immediate mechanical validity, then reachable tools/ports and visible interactions, then optional detail. A camera is a rendering input; leaving its frustum does not stop a loaded hopper or sever pressure continuity.

Keep a hot authoritative working set with needed halos. Persist cold state and keep sufficient conservative offscreen summaries/events to continue declared transport and production. Before local interaction, hydrate state and dependencies or defer the initiating action. A sleeping region with no relevant changes can stay cold; an offscreen flow connected to active machinery cannot simply disappear from scheduling.

Use revision keys for geometry, support, material/closure version, gravity, topology/ports, temperature/phase, and every input actually used. Unknown bounds remain conservative. Exact cache eviction changes cost, not the answer. Approximate closure selection is separately versioned and included in replay/saves.

### Initial experimental envelope

**All numbers here are proposed starting gates, not measured product capacity or promised minimum hardware performance.** The owner must select a target CPU/GPU and representative scene before accepting a production budget.

| Budget item | Proposed starting envelope | Measurement and overload behavior |
|---|---|---|
| Gameplay step | 60 Hz authority for the first fixture | Fixed authoritative time; contact/fluid substeps counted inside it |
| CPU material time | p95 ≤ 2 ms, p99 ≤ 4 ms in a sustained representative first-slice run | Include extraction, contacts, classification, transfers, and derived physical updates; report worst wake events separately |
| Active granular detail | Start at 8,192 CPU parcels; measure 32,768 and 65,536 as scaling experiments | Counts are experimental declarations, not expected physical grain counts; bound contacts/neighbor storage independently |
| Shape population | Start with 2–4 spheres per clump in one bounded hotspot | Report primitive pairs, contacts, substeps, and angular-state memory relative to the sphere baseline |
| Local fluid trial | 4,096–16,384 particles in a controlled container fixture | Record density/divergence residuals and iterations, not merely particle count |
| Transport graph | 1,000 active edges, then 10,000-edge scaling fixture | Include arbitration/frontier/topology changes; long idle graphs should not incur full scans |
| Hot material memory | Trial cap 128 MiB, independent of renderer and global exact-cache budgets | Count every active buffer, double buffer, contact/history, grid halo, queue, and conversion scratch allocation |
| Visual grains | Trial up to 100,000 nearby instances, with a measured ≤ 1 ms incremental GPU draw/update target | Entirely derived; reduce visual population without changing physics |
| Wake/conversion work | Declared count/priority caps with mandatory validity work distinguished from optional refinement | Defer optional causes before commit; use validated fallback or slower authority for unavoidable work |

A rough memory account is N·bytes_per_particle + C·bytes_per_contact + G·bytes_per_cell + queues + staging + conversion scratch. At 128 bytes per particle, 65,536 particles alone consume 8 MiB; a contact/history population of 16 records per particle at 48 bytes each consumes another 48 MiB. These are illustrative layout assumptions, not known generated sizes, and show why a particle cap is not a complete memory budget.

Do not borrow R442's example cache allocations as a physics solver budget. Report both scopes and their combined memory pressure. R213 cap/ring behavior may bound requests or stale diagnostic history; it never licenses dropping authoritative holdings, required contact effects, or unprocessed commit records.

### CPU, SIMD, and GPU admission

Implement the reference as generated compiled CPU stages. Measure one material, one model, one deterministic order. SIMD should preserve the chosen reduction/decision behavior and pass the supported replay contract. Authoritative GPU physics also depends on R232/R239's complete world-binding, transfer, publication, and restart contract; parity alone is insufficient. Move only proven stages after that dependency and the actual Dozen/native feature set and generated arithmetic are verified.

Potential GPU stages include field sampling, broad-phase construction, dense neighbor/grid work, and rendering; those are candidates, not permission to scatter floating atomic updates into authority. Keep ownership commits on CPU initially. Avoid per-frame CPU/GPU round trips that erase solver savings; profile upload, synchronization, sparse allocation, readback, and staging.

Deterministic work admission is based on bounded counts and saved priorities. If 2 ms is exceeded, report the miss. Do not drop the last contacts or change the accepted transfer set according to how fast a worker happened to finish.

## Rendering

Render the same authoritative material through the generated declaration → output-slot → layout/bake/decode path required by R445/R447. Resolve the per-voxel material ID through the shared table. Resource identity, physical phase/profile, and render material may be linked, but must not be assumed to be the same namespace.

Settled bulk derives a surface from stored occupancy/field state. Active granular material may render explicit instances or a reconstructed surface, depending on material and scale. Liquid rendering derives a surface and appropriate dynamic channels from its solver; simulation resolution remains independent of render voxel resolution. Collision authority stays with the physical fields and declared proxies.

Use a derived high-detail grain population to improve nearby appearance only when it follows real quantity distribution and remains visibly consistent at intakes. It has no separate gameplay identity or mass. Fade/reseed visual detail using stable seeds and matched surface motion; conservative physical handover happens at the separate ownership boundary.

R419's bimodal lighting and material-dependent edge treatment should remain legible on wet/dry sand, water, slurry, and ore. Smooth liquid surfaces and sharper/angular granular silhouettes can come from authored material properties. Do not add per-material handwritten shader switches or pin an outdated normal-estimation recipe in this report; use the current generated renderer path and its later rulings.

Hot streams carry changing occupancy/surface/motion channels; cold streams carry material/recipe data according to dependency evidence. A slow-moving physical region is not automatically static material data. Rendering may use approved derived compression/LOD; authoritative masses, material identities, and continuation state remain lossless.

Visual witnesses cover pouring across a tier boundary, digging through the shell, moving a full bucket, fine streams at an inserter, and a mixed-material outlet. Measure transition-induced surface motion and visible leaks, and inspect under both art-lighting modes. Attractive upsampling cannot excuse incorrect physical flow or concealed mass loss.

## Saves and networking

### Persistent state versus reconstruction

| Persist or preserve by a proven canonical reconstruction | Normally derived and rebuildable |
|---|---|
| Exact deposit/holding quantities, constituents, explicit sources/sinks, baseline migration epoch/residuals | Aggregate economy totals and gravitational/display approximations |
| Particle positions/velocities; MPM reference volume, deformation gradient, plastic/hardening and affine/APIC modes; shape orientation/spin/history required by the chosen integrator | Render particles, interpolation ghosts, draw buffers, cosmetic trails |
| Bulk occupancy/packing, constitutive/fabric/plastic/thermal state, support or equilibrium state needed for continuation | Surface mesh/SDF caches that can be regenerated from authority with the same defined result |
| Pipe/belt contents, parcel order/progress/residence time, topology and owner generations | Acceleration structures and component indexes with canonical rebuild order |
| Machine intake/WIP/output/byproducts and partial-stage progress | UI fullness, rates, colors, and previews |
| Fractional rate and service-deficit state, seeds, closure/material distribution version, authoritative scheduler order | Exact caches whose misses affect performance only |
| Committed/processed-result watermarks, reorder windows, and genuine in-flight ownership | Completed transient proposals at a coherent snapshot boundary |
| Required contact warm starts, pending impulses, grid modes, or solver state when they affect the next tick | Solver scratch only if discarding it demonstrably leaves continuation within the supported contract |

Saving only mass and particle transforms is inadequate if the next tick depends on contact histories, hidden WIP, rate residuals, topology epochs, or a different reification seed. Conversely, retaining unlimited historical contact/voxel snapshots is unnecessary. Save the current sufficient state and bounded semantic history.

R435 governs ordered virtual/stored deltas: remove overwritten payloads and maintain a reconstructible current stack. Cold residency records must include ownership and state needed to resume, not only a rendered surface. Quantities moving across hot/cold boundaries are transfers between representations of the same owner, or an explicit ownership change, never an extra credit.

### Migration policy

Use R437/R450 generated codecs and migrations from the historical per-pack schema, in the ordered pack/digest framework. Migrate versions before changing the pack set. A removed material/profile with live quantity requires an explicit mapping or salvage decision; do not substitute whatever current catalog entry looks similar.

Version closure tables and deterministic shape distributions when they affect future mechanics. A migration may intentionally change behavior, but that creates a new replay epoch or compatible rule transition. Converting existing double balances to exact quanta records its baseline/rounding outcome; it cannot reconstruct previously lost precision.

### Network recommendation

Start with **authoritative server simulation plus snapshots and ordered material deltas**. Clients may interpolate derived visuals. Commands include sequence/epoch information, and applied transfers remain idempotent under retry. A snapshot establishes a baseline hash; deltas reference it, preserve order, and have explicit gap recovery. Relevance filters may omit visualization, not server-side physical custody.

R443's compact lossless representation may be reused as a payload when suitable. Reliability, schema negotiation, baseline selection, sequence watermarks, ownership generations, and resynchronization still need a protocol. Cross-device lockstep is a separate commitment requiring the full supported solver and arithmetic matrix.

Replay witnesses compare authoritative state every tick after save/load, not just end-of-run totals. Include different worker counts, a blocked outlet, a pending wake cascade, a lost commit acknowledgement, an obsolete retry outside the history window, and destroyed/recreated destinations.

## Increment plan

Each lane below is a **plain proposed work package**, without new SPT identifiers. Product implementation begins only after the owner chooses the relevant model/budget. Determinism, save continuation, and quantity witnesses accompany every lane rather than being deferred to the end.

| Lane, in dependency order | Deliverable | Decisive proposed witness |
|---|---|---|
| Accounting and declaration bridge | Exact bounded local quantities, wider/composed source totals using existing forms, one ownership contract, legacy stock/extraction migration baseline, generated schema/codec probe | Extract 1 g repeatedly from a planetary-scale source; reconcile material/layer/body and all holdings across reload without lost/double quantity; prove ranges and stale retry rejection |
| Sand end-to-end | One sand profile; bounded CPU Drucker–Prager MPM per R256, scoop/container, reachable typed inserter, one machine with WIP and explicit waste; optional PBD comparator requires selection | 100,000 fixed ticks through the complete chain with exact balances and identical CPU replay/save continuation; narrow-wall leak test and baseline repose/discharge data |
| Sleep and wake | Retain full state while skipping validated rest; support/gravity/port/boundary invalidation and deterministic overload handling | Undercut, open a buried outlet, move/rotate the bucket, and change gravity; required wake precedes altered mechanics and no state/mass is dropped at capacity |
| Conservative representation operators | Independent particle↔cell/bulk restriction/prolongation, ownership masks, flux/reaction interface, admissible packing | 100,000 round trips with exact quantity; isolated momentum target ≤ 10^-6 relative plus absolute/angular/COM/volume residuals; infeasible splits defer safely |
| Depth-tier pile | Active shell, transition band, validated supported interior, current-material boundary updates, hysteresis | No unaccounted transition jumps; repose within 2°, nontrivial discharge within 5% plus low-flow limits; ≥ 2× cost target versus always-active, with separate full-state-sleep comparison |
| Dry grain morphology | Rotational proxy and 2–4 sphere clump families; reproducible static prototypes/variants; calibrated interior closure | Measured packing/repose/shear/discharge and jam statistics across seeds; shape differences persist across reification without doubled mass or changed physical grain size |
| Transport network | Finite pipe/belt contents, physical residence time, joint capacity arbitration, mixtures and physical filtering, topology edits | Fork/join and full/empty graphs; 9:1 and 99:1 service tests if fairness is promised; partition-invariant transport timing, exact split/merge/destruction balances |
| Local liquid | DFSPH reference with PBF comparator, moving-wall reactions, local containers/jets, independent render resolution | Repeated fill/scoop/pour with exact quantity, bounded measured density/volume error, no wall leakage, reaction balance, and residual/cost curves |
| Liquid depth and pressure | Choose coupled coarse pressure/narrow-band detail or scoped hydraulic graph; keep each law explicit | Deep moving obstacle/outlet tests, pressure-head/continuity residuals, full-loop throughflow for hydraulic mode, conservative interface flux and topology changes |
| Suspension rheology | Bounded carrier-fluid/clump or calibrated suspension experiment; concentration/history/retention state | Reproduce selected thinning/thickening/jam curves within a chosen reference range; demonstrate total/constituent balance and cost relative to a closure |
| Backend, residency, and network scale | Expand already passing lanes to SIMD/GPU candidates, cold-region persistence, authoritative snapshot/delta protocol | Supported device/worker matrix, hot/cold round trips, loss/retry/reorder resync, cap exhaustion, and p95/p99 complete-time/memory budgets |

Dependencies: the first two lanes establish the reference; sleep precedes bulk reduction; transfer operators precede the depth-tier pile; baseline sand calibration precedes morphology, and morphology updates must revalidate the depth closure. Transport can proceed after the first full chain, parallel to deeper mechanics. Liquid work reuses accounting/ports but earns its own mechanical reference. Pressure and suspension remain separately selected extensions. Backend work may begin early on read-only/derived stages, but authority moves only after the corresponding gate.

## Owner decisions

| Choice | Recommendation | Consequence of another choice |
|---|---|---|
| End-to-end architecture | **Option A** with an explicit path to Option B when deep dynamics demand it | Option C lowers cost by making more behavior a declared cell/closure approximation |
| Exact mass quantum and planetary totals | **Trial 1 µg local sand balances; wider/composed exact deposit totals, double aggregates derived; prove grain-size/range/schema requirements first** | A 1 g quantum is simpler for bulk-only fixtures but cannot represent many literal grains; the legacy baseline needs an accepted reconciliation policy |
| Initial mechanical fidelity | **Retain R256's CPU MPM dense-sand default; compare bounded PBD only if the owner wants a cheaper handling alternative** | Choosing PBD changes the accepted default for that scope and needs measured yield/flow/reaction limits; DEM throughout increases contact-history and timestep cost |
| Meaning of deep interior | **Only validated supported equilibrium initially; preserve full state for first sleep** | Broad dynamic interiors require a real coarse mechanical law and stronger coupling |
| Hidden shape scope | **Static prototype families plus evolving orientation/history; calibrated bulk closure** | Universal true-shaped micrograins have a much larger cost; scalar proxies alone weaken shape-driven jamming |
| Matflow material scope | **Dry sand first, then consequential gravel/ore and local liquids; keep R256's snow work in its existing program** | Reordering the broader program or adding early slurry/gas is an owner choice; no mobile pore water is implied by the first wet feature |
| Pipe semantics | **Rate-limited finite buffered contents first, with physical residence time** | Instant segment mixing simplifies infrastructure; pressure mode adds pump/head/resistance decisions and a coupled solver |
| Typed material intake | **Compatibility filter at physically accessible matter; explicit separators for selective recovery** | Arbitrary extraction from mixed bulk would remove the intended separation gameplay |
| Determinism/networking | **Exact quantities and repeatable CPU continuation; server authority for networking** | Lockstep across CPU/GPU/device families requires a funded numerical compatibility contract |
| Performance and visible scale | **Select target hardware; use the experimental envelope to measure before committing production caps** | Higher active grain/shape/fluid counts trade against other game systems and wake capacity |
| Overload and interruption | **Defer optional actions before commit; preserve matter; validated fallback or slower authority for unavoidable work** | Silently dropping detail that carries mechanical obligations produces inconsistent behavior |
| Existing-save reconciliation | **Versioned exact baseline with explicit rounding/provenance and migration/salvage rules** | Historical gram-level loss cannot be repaired without additional evidence or a deliberate authored adjustment |

These are owner choices for subsequent implementation, not a request to approve unfinished research. No new declaration surface is assumed. A necessary kernel extension should return as a concrete schema/codegen gap with a bounded example.

## Unverified leads

The following were not established from adequate opened primary evidence; no implementation or performance claims are attached:

- Noita's exact chunk sizes, dirty rectangles, checkerboard scheduling, and multithreading details: the GDC session abstract was accessible, not the full technical talk.
- Oxygen Not Included's exact cell stencil, element-per-cell rules, and solver ordering: official patches establish maintenance cases, not the whole algorithm.
- Satisfactory's internal hydraulic equations, update ordering, and determinism guarantees.
- Teardown, Dwarf Fortress, Space Engineers, and Astroneer as examples of true granular or voxel-fluid simulation: visible destruction or resource handling alone does not establish the requested mechanics.
- Inaccessible ScienceDirect leads on newer conservative SPH splitting and hybrid/learned continuum methods; the report relies on the accessible papers/thesis instead.
- A general 3D interactive extension of the 2022 interlocking-suspension setup, and validated bulk closures derived from its shapes.
- Current universal performance or commercial integration suitability of a research solver beyond the specific opened license/project evidence.
- Current native VIXEN implementations of every stage described by the July dispatch contract; KFR/launcher integration details; the exact new R452 typed-query spelling before a generated example is verified.

## References

All public sources below were opened on **2026-10-09**. Reference keys are bibliography labels, not SPT identifiers. “Foundation” marks work before 2020. A live project page is evidence of the inspected current page, not a pinned dependency recommendation.

### Granular mechanics and microstructure

| Key | Source/date | Material actually inspected |
|---|---|---|
| [G1] | Macklin et al., Unified Particle Physics for Real-Time Applications, 2014 · foundation | Original paper PDF |
| [G2] | Klár et al., Drucker–Prager Elastoplasticity for Sand Animation, 2016 · foundation | Original paper PDF |
| [G3] | Hu et al., MLS-MPM with Displacement Discontinuity and Two-Way Rigid Body Coupling, 2018 · foundation | Author publication page |
| [G4] | Yue et al., Hybrid Grains, 2018 · foundation | Author project page; full large PDF not relied upon |
| [G5] | Wang et al., A Massively Parallel and Scalable Multi-GPU Material Point Method, 2020 | Author project page and reported timings |
| [G6] | Soltanbeigi et al., Influence of various DEM shape representation methods on packing and shearing of granular assemblies, 2021 | Publisher full-text HTML |
| [G7] | Jiang et al., Hybrid continuum–discrete simulation of granular impact dynamics, 2022 revision | Author manuscript HTML |
| [G8] | Eghbalian et al., A physics-informed deep neural network for surrogate modeling in classical elasto-plasticity, 2022 | Author manuscript; limited claims from stated demonstration |
| [G9] | Chantharayukhonthorn, A hybrid discrete and continuum framework for multiscale modeling, MIT dissertation, 2023 | Institutional record/abstract |
| [G10] | Zhang et al., Chrono DEM-Engine research, 2023 preprint | Author manuscript, clump/activation methods and example timings |
| [G11] | Nagy et al., Flow of asymmetric elongated particles, 2023 preprint | Author manuscript |
| [G12] | Gupta and Keyser, Adaptive Sampling for Interactive Simulation of Granular Material, 2025 | Publisher full-text HTML |
| [G13] | Liu and Xu, A GPU-based DEM framework for simulation of polyhedral particulate system, 2023 | Publisher abstract; full article access not assumed |
| [S1] | Blair and Ness, Shear thickening in dense suspensions driven by particle interlocking, 2022 | Publisher full text |
| [S2] | Singh et al., Shear thickening and jamming of dense suspensions: the “roll” of friction, 2020 | Author abstract; stress-activated rolling resistance results |

### Fluids and sparse infrastructure

| Key | Source/date | Material actually inspected |
|---|---|---|
| [F1] | Interactive Computer Graphics, SPlisHSPlasH, current project | Official repository/README |
| [F2] | Ihmsen et al., Implicit Incompressible SPH, 2013 online / 2014 journal · foundation | ETH publication page |
| [F3] | Bender/Koschier DFSPH, foundational method; current tutorial | Official author tutorial and equations |
| [F4] | Macklin and Müller, Position Based Fluids, 2013 · foundation | Original paper PDF |
| [F5] | Jiang, Schroeder, and Teran, The Affine Particle-In-Cell Method, 2017 · foundation | Original paper PDF |
| [F6] | Ferstl et al., Narrow Band FLIP for Liquid Simulations, 2016 · foundation | Original paper PDF |
| [F7] | Braun, Bender, and Thuerey, Adaptive Phase-Field FLIP for Very Large Scale Two-Phase Fluid Simulation, 2025 | Author project page |
| [F8] | Nealon and Price, adaptive particle refinement in SPH, 2024 preprint | Author manuscript |
| [F9] | Su et al., Real-time Height-field Simulation of Sand and Water Mixtures, 2023 | Original paper PDF |
| [F10] | Xiao et al., Volume-Preserving LBM–MPM Coupling, 2026 | Author project page |
| [F10-code] | LBM–MPM Air–Water–Sand example implementation, current | Author repository; documented 2D CUDA/Taichi examples |
| [F11] | FluidX3D, current project | Official repository, FAQ, and stated license limits |
| [F12a] | NVIDIA, Accelerating OpenVDB on GPUs with NanoVDB, 2020 | Author/vendor introduction |
| [F12b] | OpenVDB, NanoVDB.h documentation, current | Official project documentation |
| [F13] | Setaluri et al., SPGrid, 2014 · foundation | Author publication page |
| [F14] | NVIDIA, GVDB Voxels, current project | Official repository and requirements |
| [F15] | Bender, Westhofen, and Jeske, Consistent SPH Rigid–Fluid Coupling, 2023 | Author publication page |

### Transport, games, and determinism

| Key | Source/date | Material actually inspected |
|---|---|---|
| [T1] | Wube, FFF-416: Fluids 2.0, 21 June 2024 | Developer prototype description |
| [T2] | Wube, FFF-430: Drowning in Fluids, 27 September 2024 | Developer pre-launch revision |
| [T3] | Wube, Version 2.0.7 release notes, 21 October 2024 | Official release post |
| [T4] | Wube, FFF-442: Flip, Flow, and Fresh Paint, 12 June 2026 | Developer 2.1-era revision; distinct from 2.0 launch |
| [T5] | Factorio stable downloads, current | Official page; stable 2.0.77 at retrieval |
| [T6] | Factorio experimental downloads, current | Official page; experimental 2.1.21 at retrieval |
| [T7] | Wube, FFF-176: Belts optimization for 0.15, 3 February 2017 · foundation | Developer optimization post |
| [T8] | Coffee Stain, The Fluids Update, 10 November 2020 | Official Steam developer announcement archive |
| [T9] | US EPA, EPANET 2.2 User Manual, 2020 | Official full manual PDF |
| [T10] | Erin Catto, Determinism, 27 August 2024 | Box2D author post |
| [T11] | NVIDIA, CUDA 13.1 / CCCL 3.1 announcement, 4 December 2025 | Vendor description of scoped reproducible reductions |
| [T12] | Khronos, Vulkan Environment for SPIR-V, current | Official arithmetic/environment specification |
| [T13] | Petri Purho / Nolla Games, Exploring the Tech and Design of Noita, GDC 2019 · foundation | Session abstract/metadata only |
| [T14] | Klei, March 2026 Bug Fix Update, displayed 31 March 2026 | Official Steam announcements; not a solver implementation disclosure |

### Repository sources

The exact paths and line ranges used as evidence are in “What exists in our tree.” These links identify the pinned snapshots and the principal source documents.

- [Undertow inspected commit](https://github.com/Lint111/undertow/commit/b22dce29434295ad9813a4ddcbc19199d11d1fed).
- [Kernel inspected commit](https://github.com/Lint111/Yeroket-Fantasy/commit/c3fb3847979e3f9eef04783510c42b70d7058ae7).
- [VIXEN inspected commit](https://github.com/Lint111/VBVS--VIXEN/commit/bd8e3f2588fc20eeabdbf57b8bc6ae7d6bbd1360).
- [Undertow AGENTS.md](https://github.com/Lint111/undertow/blob/b22dce29434295ad9813a4ddcbc19199d11d1fed/AGENTS.md), [CLAUDE.md](https://github.com/Lint111/undertow/blob/b22dce29434295ad9813a4ddcbc19199d11d1fed/CLAUDE.md), [lane rules](https://github.com/Lint111/undertow/blob/b22dce29434295ad9813a4ddcbc19199d11d1fed/docs/notes/LANE-RULES.md), and [kernel authoring rules](https://github.com/Lint111/undertow/blob/b22dce29434295ad9813a4ddcbc19199d11d1fed/.claude/skills/kernel-authoring-rules/SKILL.md).
- [Owner rulings/register](https://github.com/Lint111/undertow/blob/b22dce29434295ad9813a4ddcbc19199d11d1fed/docs/in-progress.md).
- [Accepted R18 material/mechanism research](https://github.com/Lint111/undertow/blob/b22dce29434295ad9813a4ddcbc19199d11d1fed/reports/research/2026-09-27-R18/research-R18-material-and-mechanism-simulation.md); its declaration corrections are in R255/R256.
- [Original extraction design](https://github.com/Lint111/undertow/blob/b22dce29434295ad9813a4ddcbc19199d11d1fed/docs/archive/plans/2026-05-31-mass-extraction-design.md) and [materialization dispatch](https://github.com/Lint111/undertow/blob/b22dce29434295ad9813a4ddcbc19199d11d1fed/docs/design/2026-09-10-materialization-dispatch.md).
- [Current Mass](https://github.com/Lint111/undertow/blob/b22dce29434295ad9813a4ddcbc19199d11d1fed/core/src/Undertow.Content.Core.Declarations/StarSystem/Mass.cs), [Extraction](https://github.com/Lint111/undertow/blob/b22dce29434295ad9813a4ddcbc19199d11d1fed/core/src/Undertow.Generation/StarSystem/Extraction/Extraction.cs), [economy extraction](https://github.com/Lint111/undertow/blob/b22dce29434295ad9813a4ddcbc19199d11d1fed/core/src/Undertow.Content.Core/Systems/Economy/ExtractionSystem.cs), and [resource content](https://github.com/Lint111/undertow/blob/b22dce29434295ad9813a4ddcbc19199d11d1fed/core/content/core/resources/resources.utdl).
- [VIXEN voxel-field physics research](https://github.com/Lint111/VBVS--VIXEN/blob/bd8e3f2588fc20eeabdbf57b8bc6ae7d6bbd1360/VIXEN/Vixen-Docs/03-Research/Voxel-Field-Physics-Research-2026-07.md) and [dispatch contract](https://github.com/Lint111/VBVS--VIXEN/blob/bd8e3f2588fc20eeabdbf57b8bc6ae7d6bbd1360/VIXEN/Vixen-Docs/01-Architecture/Kernel-Physics-Dispatch-Contract-Spec-2026-07.md).
- [Kernel schema markers](https://github.com/Lint111/Yeroket-Fantasy/blob/c3fb3847979e3f9eef04783510c42b70d7058ae7/Packages/com.yeroket.utility.kernel-framework/Runtime/Schema/SchemaTypes.cs), [frontier runtime](https://github.com/Lint111/Yeroket-Fantasy/blob/c3fb3847979e3f9eef04783510c42b70d7058ae7/Packages/com.yeroket.utility.kernel-framework/Runtime/Dispatcher/FrontierPhase.cs), and [reported bounds/Dozen witnesses](https://github.com/Lint111/Yeroket-Fantasy/blob/c3fb3847979e3f9eef04783510c42b70d7058ae7/reports/boundscore-run3.md).

[G1]: https://matthias-research.github.io/pages/publications/flex.pdf
[G2]: https://www.math.ucdavis.edu/~jteran/papers/KGPSJT16.pdf
[G3]: https://yuanming.taichi.graphics/publication/2018-mlsmpm/
[G4]: https://www.cs.columbia.edu/~smith/hybrid_grains/
[G5]: https://sites.google.com/view/siggraph2020-multigpu
[G6]: https://link.springer.com/article/10.1007/s10035-020-01078-y
[G7]: https://arxiv.org/html/2108.02080v2
[G8]: https://arxiv.org/html/2204.12088v1
[G9]: https://dspace.mit.edu/entities/publication/ead494f1-1f34-44ca-a90f-38a0c8698ef9
[G10]: https://arxiv.org/html/2311.04648v2
[G11]: https://arxiv.org/html/2312.05050v1
[G12]: https://onlinelibrary.wiley.com/doi/full/10.1002/cav.70062
[G13]: https://link.springer.com/article/10.1007/s10035-023-01321-2
[S1]: https://www.cambridge.org/core/journals/journal-of-fluid-mechanics/article/shear-thickening-in-dense-suspensions-driven-by-particle-interlocking/7D335FBDDC1067AECBE583476230FD3C
[S2]: https://arxiv.org/abs/2002.10996
[F1]: https://github.com/InteractiveComputerGraphics/SPlisHSPlasH
[F2]: https://cgl.ethz.ch/publications/papers/paperSol13b.php
[F3]: https://learn.physics-simulation.org/examples/dfsph.html
[F4]: https://matthias-research.github.io/pages/publications/pbf_sig_preprint.pdf
[F5]: https://www.cs.ucr.edu/~craigs/papers/2017-apic-jcp/paper.pdf
[F6]: https://www.cs.cit.tum.de/fileadmin/w00cfj/cg/Research/Publications/2016/NBFlip/nbflip.pdf
[F7]: https://ge.in.tum.de/2025/06/04/adaptive-phase-field-flip-for-very-large-scale-two-phase-fluid-simulation/
[F8]: https://arxiv.org/html/2409.11470v1
[F9]: https://kuiwuchn.github.io/RTWaterAndSand.pdf
[F10]: https://lwkobe.github.io/papers/XWYDL2026/
[F10-code]: https://github.com/DacianShaw/LBM-MPM-Air-Water-Sand
[F11]: https://github.com/ProjectPhysX/FluidX3D
[F12a]: https://developer.nvidia.com/blog/accelerating-openvdb-on-gpus-with-nanovdb
[F12b]: https://www.openvdb.org/documentation/doxygen/NanoVDB_8h.html
[F13]: https://graphics.cs.wisc.edu/Papers/2014/SABS14/
[F14]: https://github.com/NVIDIA/gvdb-voxels
[F15]: https://srjeske.de/publications/2023-vmv-sph-boundary-handling/
[T1]: https://factorio.com/blog/post/fff-416
[T2]: https://factorio.com/blog/post/fff-430
[T3]: https://forums.factorio.com/viewtopic.php?t=116184
[T4]: https://factorio.com/blog/post/fff-442
[T5]: https://factorio.com/download
[T6]: https://factorio.com/download/experimental
[T7]: https://www.factorio.com/blog/post/fff-176
[T8]: https://store.steampowered.com/news/posts/?appids=526870&enddate=1606152705&feed=steam_community_announcements
[T9]: https://19january2021snapshot.epa.gov/sites/static/files/2020-05/documents/epanet_userss_manual_2.2.0.pdf
[T10]: https://box2d.org/posts/2024/08/determinism/
[T11]: https://developer.nvidia.com/blog/nvidia-cuda-13-1-powers-next-gen-gpu-programming-with-nvidia-cuda-tile-and-performance-gains/
[T12]: https://docs.vulkan.org/spec/latest/appendices/spirvenv.html
[T13]: https://www.gdcvault.com/play/1025695/Exploring-the-Tech-and-Design
[T14]: https://steamcommunity.com/app/457140/announcements/

## SPT DISPOSITION

**No SPT writes, new IDs, status changes, or new declaration surfaces were made.** Existing identifiers below are cited only to locate governing work. This research does not close implementation tasks or claim a passed product witness.

| Existing identifier(s) | Disposition for this report |
|---|---|
| R256, R255, R254 | Preserve the accepted material portfolio and ordinary literal-field declarations; reconcile physical/acoustic material content. Matflow adds handling, depth, and morphology detail to that program. |
| R232, R238, R239 | Keep two existing data authorities under one derived schedule; verify complete publication/transfer dependencies before authoritative GPU work. |
| R213, D-0436 | Apply capacity/no-throw policy to the proposed layouts and outcomes without discarding material. The historical T-0817 closure is not reopened by this research. |
| R438 | All proposed model and transfer stages remain compiled/unrolled through existing lowering. |
| R441, R442, T-1450 | Reuse bounds/reuse work only within its exact contract; a new material-model approximation is not automatically covered by these optimizations. No completion claim for T-1450. |
| R443, R445, R447 | Static material data, dynamic state, generated outputs, and lossless authority remain the rendering/storage contract. |
| R451, R452 | Use existing frontier machinery and the selected typed-query direction; verify the concrete generated form in the implementation lane. |
| R435, R424 | Respect ordered stored/virtual deltas and hot/cold stream semantics; add a verified residency policy without conflating them. |
| R437, R450 | Exact quantity and solver-state changes require historical generated migrations and explicit loss handling. |
| R419 | Preserve the material/art direction and include transition visual witnesses. |

**Plain-text follow-up proposals:** implement the accounting bridge; prove one complete sand-handling chain; add state-preserving sleep; prove conservative representation operators; measure the depth-tier pile; calibrate dry morphology; add finite physical transport; compare local fluid solvers; select a pressure/deep-liquid law; research suspension rheology; expand backends/residency/networking after the corresponding gates. These are work descriptions, not newly minted tracked entries. R256's already named implementation groups still follow their existing lane-approval process.

## CONSOLIDATION ISSUES

1. **Accepted direction versus new scope.** R256 already favors MPM for dense sand and a portfolio of models. Retain that default. A PBD-first handling alternative or broad replacement needs an explicit owner choice; do not silently reopen the entire accepted research program.
2. **One physical material source.** Reconcile morphology, density/phase, closure, renderer, and acoustic fields with the existing proposed physical-material kind and resource binding. Do not create parallel resource, physics, rendering, and acoustic catalogs with separately authored copies.
3. **Current extraction/economy bridge.** The archival “storage later” statement is stale. Current coarse/local production and stock persistence must share one exact custody/depletion transition, with a destination reserved before depletion and all efficiency remainder represented.
4. **Astronomical precision and save history.** Current double mass cannot express tiny planetary debits. Exact local quantities require exact source depletion and wider/composed totals, with a documented historical baseline; no declaration change can reconstruct information already rounded away.
5. **Bulk validity and MPM sleep.** A surface band is not proof of deep equilibrium. MPM grid support, contact reactions, voids, moving walls, pressure, and consequential history constrain reduction. Unavoidable invalidation needs a safe declared mechanical outcome under capacity pressure.
6. **Existing dispatch/publication path.** The July contract is historical design evidence. R238/R239's existing worlds and generated plan remain authoritative; verify current bindings and R232's physics transfer path rather than inventing a new graph, registry, clock, or material manager.
7. **Transport semantics and migration.** Buffered transfers, pressure flow, instant segment mixing, and physical travel time are different laws. LOD, repartitioning, saves, and topology edits must preserve the selected one. Mixture filtering cannot become an undeclared separator.
8. **Backend and compression boundaries.** Existing narrow bounds witnesses do not prove whole-solver GPU parity. Derived render compression and network-payload reuse do not permit lossy holdings, an incomplete protocol, or hidden authority migration.
9. **Delivery environment.** The requested workstation worktree, external lane-rules path, and CodeGraph index were unavailable. Evidence was pinned through repository reads; KFR/launcher and complete native implementations were not audited. Only this report is proposed for the commit envelope, based on the inspected Undertow parent; no shared branch is advanced.
10. **Validation status.** This is a completed research/design report. Product code was unchanged; no build, game fixture, solver benchmark, or new conservation/parity test ran. The thresholds and lanes above are concrete acceptance proposals for subsequent implementation.


