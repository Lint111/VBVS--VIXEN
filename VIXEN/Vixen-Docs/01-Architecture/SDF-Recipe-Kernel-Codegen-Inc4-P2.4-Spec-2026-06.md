---
title: Inc4 P2.4 — Opcode-catalogue extraction (Spec / design seed)
status: SPEC 2026-06-27 — decisions locked (user); needs a design pass before the no-placeholder plan
tags: [architecture, voxel, sdf, recipe, kernel-framework, codegen, opcode-catalogue, cross-repo]
related:
  - "[[SDF-Recipe-Kernel-Codegen-Inc4-2026-06-Design]]"
  - "[[SDF-Recipe-Kernel-Codegen-Inc4-P2.3-Plan-2026-06]]"
---

# P2.4 — Extract Yeroket's generic SDF opcode catalogue into the shared codegen

> **Not "author new opcodes."** Yeroket already has ~120 opcodes; P2.4 routes the GENERIC ones through
> the P1 AST-visitor codegen so any consumer (VIXEN) gets them, while game-specific/non-portable ones
> stay in Yeroket. This is the design doc's **D4 (layered opcodes)** + P1's deferred "opcode layering +
> per-consumer manifest", realized.

## Decisions (locked 2026-06-27, user)

- **Layering mechanism = a `[SdfCoreKernel]` marker attribute.** The Roslyn source-gen
  (`EmitCppEmitter`) filters `[KernelCallable]` methods by this marker before emitting; only marked
  kernels enter the generated C++/HLSL. Explicit opt-in, machine-enforced boundary. (Chosen over
  convention-only and enum-range partitioning.) Use the existing `CreateSyntaxProvider` predicate
  pattern (NOT `ForAttributeWithMetadataName` — it fails for same-compilation attributes).
- **Scope = the full generic+portable catalogue** (all ~75 generic opcodes), not a focused batch.

## Catalogue split (from the 2026-06-27 investigation)

Source of truth: `Yeroket-Fantasy/Packages/com.utility.graph-framework/Runtime/VM/SDFInstruction.cs`
(`SDFOpCode` byte enum, ~120 values; `SDFInstruction` = 132-byte blittable, `float data[32]`).

**INCLUDE — generic + portable (~75; mark `[KernelCallable]`+`[SdfCoreKernel]`):**
- **3D primitives** (math in `SDFPrimitives.cs`): Sphere✓, Box, BoxRounded/RoundedBox, Capsule, Cylinder,
  Plane, Torus, Ellipsoid, HollowCylinder, TaperedCylinder, Panel, Plank, CappedTorus, Cone, RoundCone,
  FakeRoundCone, Segment, BezierCurve, TriangularPrism, Pyramid, HexPrism, Link.
- **Binary CSG** (`SDFOperations.cs`): Union✓, SmoothUnion, Subtract, SmoothSubtract, Intersect,
  SmoothIntersect, Xor, SmoothMax, {Union,Subtract,Intersect}Cubic.
- **Level-set modifiers**: Round, Onion.
- **Domain transforms** (`SDFTransform.cs`): Transform, Elongate, Twist, Bend, MirrorX/Y/Z,
  RepeatInfinite, RepeatLimited, Revolution. ⚠ position-stack ops (see design point 1).
- **Scalar + float3 math** (~23): MathSin…Negate, Step/Sign/Saturate/Exp/Log/Log2, Select, Displacement,
  PositionChannel, DistanceTo; Float3 Add/Sub/MulCW/Min/Max/ScalarMul/Dot/Normalize. ⚠ value-stack ops.
- **VM control (no math body)**: Output, PushParam, ReadParam(+Float3), RestorePos, PushFloat3,
  ComposeFloat3, DecomposeFloat3, Passthrough. (Already partly handled by the stack emitter.)
- **2D primitives** (`SDF2DPrimitives.cs`): Circle2D, Segment2D, Bezier2D, Arc2D, RoundedBox2D. (Lower
  priority — only if a 2D→revolution path is wanted.)

**EXCLUDE — Yeroket-specific or NOT portable to straight-line HLSL (do NOT mark):**
- Plant/turtle/L-system: Gravity/Wind/AttractRepel, ObstacleDeflect, SurfaceSeek, GravityDroop,
  PhysicsRelax, SpaceColonizationStep, PipeModelAccum, CrownEnvelope, VoxelLightSample, TurtleForward/Turn,
  FoliagePoint, BranchRingVertex, CanopySphereNormal, PlantCapsuleSegmentDistance, PolySmoothMin, DepthBlend.
- Noise/weathering (depend on `FBM.` HLSL — on the generator deny-list): NoiseDeform, BoundedCarve,
  ChipDamage, Corrosion, Crack, PaperAging, WeatheringBlend, WoodGrain, Noise{Simplex,Perlin,Voronoi,FBM*}3D.
- Marching cubes (need `MCLookupTables.` — deny-listed): MCClassifyCell, MCEdgeInterpolate, MCTriCount.
- Blackboard/array runtime: BB{Read,Write,Read3,Write3,AccumAdd/Min/Max}, Array{Read,Read3,Write,Length}.
- **Flow control** — architecturally incompatible with the unrolling straight-line emitter:
  LoopBegin/End, BranchIfZero/NonZero, Jump, PushLoopCounter.
- ProfileExtrude (needs a runtime profile-point array not carried in the instruction).

## End-to-end touch-points (per included opcode)

1. **Yeroket**: add `[KernelCallable]`+`[SdfCoreKernel]` to the pure-math static (or wrap the existing
   `SDFPrimitives`/`SDFOperations`/`SDFTransform` static so the body is transpilable).
2. **Regen + vendor**: rebuild source-gen DLL → produce `SdfCoreKernels.g.hpp`/`.g.hlsl` → vendor into
   VIXEN `libraries/SVO/include/Recipe/generated/` + `libraries/SVO/shaders/recipe/`.
3. **VIXEN `SdfInstruction.h`**: add the `SdfOpCode` enum value at the EXACT C# byte position.
4. **VIXEN `SdfRecipeEval.h`** (`evalRecipe`): add the CPU case.
5. **VIXEN `SdfRecipeCodegen.h`** (`EmitProceduralComputeShader`): add the straight-line HLSL case.
6. **Conformance**: C# reference vectors → assert CPU + HLSL parity (the cross-language source of truth).

## Open design points — RESOLVE in the P2.4 design pass before writing the no-placeholder plan

1. **VM-emitter extension is the real work, not the switch cases.** Today `evalRecipe` +
   `EmitProceduralComputeShader` handle only leaf (Sphere) + binary (Union). The catalogue needs:
   - **domain transforms** (modify `p` for subsequent ops → position-stack: Transform/Twist/Bend/Mirror/Repeat),
   - **value math** (scalar/float3 value-stack),
   - **stack control** (RestorePos/Push/Compose/Decompose).
   Must first understand the C# VM's stack model (`SDFCompiledEvaluator.cs`) and extend BOTH VIXEN
   evaluators to mirror it. This is a design sub-task, likely its own early milestone.
2. **Regen path — RESOLVED 2026-06-27: dotnet-only, NO Unity.** Confirmed in the canonical Yeroket
   checkout. Commands: `cd Packages/com.yeroket.utility.kernel-framework/SourceGenerator~ && ~/.dotnet/dotnet
   build -c Release` (auto-deploys `RoslynAnalyzers/SDFNodeGenerator.dll` — commit it) → `~/.dotnet/dotnet
   test Tests/SDFNodeGenerator.Tests.csproj` (87/4; the 4 fails = pre-existing RefKind/ChainDispatch noise).
   The `.g.hpp`/`.g.hlsl` artifacts are produced by running the generator **in-process** via the test infra
   (`RunAndFindGeneratedAgainstMathRef` + `ExtractConst`) and `File.WriteAllText` — P0/P1 did exactly this
   (`git add --force`, `Generated/` is gitignored). **M3 will add an `UPDATE_GOLDENS=1` write-mode** to make
   regen-write a one-liner (deferred from M1 — M1 writes nothing, artifacts byte-identical). ⚠ The golden
   byte-identity tests run against the inline `RealSourceMirror` constant, NOT the real `SdfCoreKernels.cs` —
   so M3's regen-write must read from the real source + re-prepend the manual provenance comment block.
3. **`CppMappingTables` coverage.** Verify `math.rotate`/`math.conjugate`/`math.cross` etc. map to C++ before
   marking Transform/quaternion ops — unmapped calls fall back to verbatim C# and won't compile as C++.
4. **Multi-slot `data[]` encodings.** Per-opcode param packing (e.g. Transform uses slots 0–2 for
   translate+quat+scale) must match the C# `SDFInstruction` encoding in each VIXEN case.

## Proposed milestone shape (refine in the plan)

- **M1 — Layering mechanism ✅ DONE 2026-06-27**: `[SdfCoreKernel]` attribute + the `EmitCppEmitter`
  filter; Sphere/Union marked; regen byte-identical (proves the filter changes nothing yet). Yeroket
  commit `32207d64` on `feat/kernel-codegen-p2`; Opus-validated (filter load-bearing — tamper-confirmed:
  unmarking Union drops it from the regen → golden fails; 88/4-pre-existing; DLL committed with the filter).
- **M2 — VM-emitter extension**: extend `evalRecipe` + `EmitProceduralComputeShader` to the full VM
  stack model (domain-transform/value/stack-control), proven on a couple of representative new opcodes.
- **M3 — Primitives + CSG batch**: mark + regen + consume all generic primitives + binary CSG +
  modifiers; conformance vectors.
- **M4 — Domain transforms + math**: the position/value-stack opcodes; conformance.
- **M5 — Live render gate**: a non-trivial CSG composition (e.g. box ∩ sphere − cylinder, transformed)
  bakes + renders in VIXEN on lavapipe; no-regression on the existing Sphere/Union shapes.

## Progress

Cross-repo, two stacked branches (both KEPT, not merged): **Yeroket** `feat/kernel-codegen-p2` (off
`feat/kernel-codegen-p1`) for the generator/kernels; **VIXEN** `feat/sdf-recipe-codegen-p2` for consumption +
these docs.

- **Regen path confirmed dotnet-only** (2026-06-27) — see resolved design-point 2.
- **M1 DONE** (2026-06-27) — Yeroket `32207d64`; `[SdfCoreKernel]` marker + `EmitCppEmitter` filter, Sphere/
  Union marked, byte-identical, Opus-validated (tamper-confirmed load-bearing).
- **Next = M2** (VM-emitter extension) — the design-heavy core: understand the C# VM stack model
  (`SDFCompiledEvaluator.cs`) and extend VIXEN `evalRecipe` + `EmitProceduralComputeShader` from leaf+binary
  to domain-transform (position-stack) / value-math / stack-control. Needs the design pass (open point 1)
  before it can be a no-placeholder plan.

## Risks / gotchas (confirmed 2026-06-27)

- **Enum ordering is a cross-repo binary contract** — append only, never insert mid-group; VIXEN's
  `SdfOpCode` values must equal the C# byte values.
- **`float data[32]`** layout is load-bearing (132 B; `static_assert`) — do not change to `glm::vec4[8]`.
- **Canonical Yeroket checkout = `/home/liory/Github/Yeroket-Fantasy`** (the `/mnt/c/GitHub` one is stale).
- **Manual vendor copy has no automation** — worth a small script once the first batch lands.
