# SIMD Materialization — Compiler-Pipeline Feasibility Report (2026-09)

**Status:** STOPPED before implementation. The current kernel compiler cannot represent the
runtime recipe VM's operand/stack/control semantics, and implementing those semantics in VIXEN
would violate the owner amendment that lowering must live in the kernel compiler pipeline.

**Repositories measured:** VIXEN ENGINE `lane-voxmut` at/above `716af94b`; Yeroket Fantasy
kernel-framework `lane-simdmat` at/above `09583af1`.

## 1. Decision

The lane-batched execution shape is viable for 90 of the 91 opcodes currently declared by
VIXEN. It is not valid to implement it yet.

The existing kernel pipeline owns a target-neutral SIMD4 widening stage
(`WidenedProgramBuilder` / `WidenedProgram`) and a strict-FP C++ SIMD4 pilot. That stage widens
one authored method body. It does **not** model a runtime instruction sequence, value/position
stack effects, stack-slot assignment, `InputMask` / `ParamMask` resolution, `Data0`–`Data7`
operand bindings, opcode dispatch, or position-stack control. Those semantics currently live in
VIXEN's handwritten `Recipe::evalRecipe` switch.

The canonical schema also does not own the full VIXEN opcode set:

- `ReadParam` and `ReadParamFloat3` are hand-mirrored in VIXEN because they are not marked as
  emitted core operations in the canonical enum.
- `DeclarePosition` and `InvokeRecipe` are VIXEN-only opcodes with no canonical kernel schema
  declaration.
- `InvokeRecipe` additionally cannot execute through `BulkMaterializationRequest`: the frozen
  request owns one instruction stream and one parameter array, but no registry or closed callee
  graph.

Consequently, an engine-side opcode table or lowering switch would be a second source of recipe
semantics. The amendment explicitly prohibits that workaround. The correct next move is a
compiler-model extension, followed by a new engine lane consuming its generated output.

## 2. Sources measured

VIXEN:

- `libraries/SVO/include/BulkMaterialization.h`
- `libraries/SVO/src/BulkMaterialization.cpp`
- `libraries/SVO/include/SdfBake.h`
- `libraries/SVO/include/Recipe/SdfRecipeEval.h`
- `libraries/SVO/include/Recipe/generated/SdfOpCodes.g.h`
- `libraries/SVO/include/Recipe/generated/SdfCoreKernels.g.hpp`
- `libraries/SVO/include/Recipe/RecipeParityCorpus.h`
- `libraries/SVO/tests/test_voxel_injection.cpp`
- `libraries/SVO/tests/test_voxel_injection_queue.cpp`
- `libraries/GaiaVoxelWorld/tests/benchmark_voxel_batch.cpp`
- `libraries/SVO/CMakeLists.txt`
- `Vixen-Docs/01-Architecture/Voxel-Mutation-Replacement-2026-09.md`

Yeroket kernel compiler:

- `SourceGenerator~/Transpiler/WidenedProgram.cs`
- `SourceGenerator~/Transpiler/WidenedProgramBuilder.cs`
- `SourceGenerator~/Transpiler/CppWidenedProgramRenderer.cs`
- `SourceGenerator~/Transpiler/MappingTables.cs`
- `SourceGenerator~/SDFNodeSourceGenerator.cs`
- `SourceGenerator~/Transpiler/RecipeContainerEmitter.cs`
- `Runtime/KernelCallableAttribute.cs`
- `CodegenTool~/Program.cs`
- `CodegenTool~/CompilationLoader.cs`
- `CodegenTool~/Tests/GaiaEmitterCliTests.cs`
- `Packages/com.utility.graph-framework/Runtime/VM/SDFInstruction.cs`
- `Packages/com.utility.sdf/Runtime/Kernels/SdfCoreKernels.cs`

## 3. Instruction-set classification

This classification is conservative. “Elementwise” requires identical operation ordering,
broadcast constants, no reassociation, and FP contraction disabled. “Masked” requires ordered
compare/select behavior matching scalar GLM semantics, rather than native `minps` / `maxps`
shortcuts whose NaN, tie, and signed-zero behavior differs. “Scalar fallback” remains inside a
lane batch but invokes the generated scalar kernel once per active lane.

| Class | Opcodes | Bit-identity constraint |
|---|---|---|
| Elementwise | `Plane` | Preserve `(x*n.x + y*n.y) + z*n.z`; no FMA or reassociation. |
| Elementwise | `Round`, `Onion` | Subtraction and absolute value only. |
| Elementwise | `Transform` | Preserve the generated cross-product statement order; no contraction. |
| Elementwise | `MirrorX`, `MirrorY`, `MirrorZ` | Component copy and scalar-compatible absolute value. |
| Elementwise | `RepeatInfinite` | Preserve the exact abs/add/divide/floor/subtract sequence. |
| Elementwise | `MathAdd`, `MathSub`, `MathMul`, `MathAbs`, `MathFrac`, `MathLerp`, `MathNegate` | Preserve scalar expression order; `MathFrac` is `x-floor(x)` and lerp must retain generated ordering. |
| Elementwise | `Displacement` | Preserve `sdf + disp*scale` with contraction disabled. |
| Lowered away | `Output`, `ComposeFloat3`, `Passthrough` | No runtime work after stack-slot lowering. |
| Broadcast / resolved at lowering | `PushParam`, `PushFloat3`, `ReadParam`, `ReadParamFloat3` | Broadcast constants and parameter snapshots; retain out-of-range zero fill. |
| Elementwise control | `RestorePos`, `DeclarePosition`, `DecomposeFloat3` | Resolve position/component stack slots statically. Bulk does not request the declared-position out value. |
| Elementwise | `Float3Add`, `Float3Sub`, `Float3MulComponentWise`, `Float3ScalarMul`, `Float3Dot` | SoA arithmetic; preserve dot reduction order and prohibit contraction. |
| Split by static operand | `PositionChannel` | Channels 0–2 are copies; channel 3 uses length and remains scalar-per-lane. |
| Masked | `Union`, `Subtract`, `Intersect`, `Xor` | Ordered compare/blend matching scalar min/max choice semantics. |
| Masked | `SmoothUnion`, `SmoothSubtract`, `SmoothIntersect`, `SmoothMax`, `SmoothUnionCubic`, `SmoothSubtractCubic`, `SmoothIntersectCubic` | Ordered masks plus exact polynomial ordering. |
| Masked | `TriangularPrism`, `Elongate` | Ordered min/max/clamp masks; no native min/max shortcut. |
| Masked | `MathSmoothstep`, `MathRemap`, `MathDiv`, `MathMin`, `MathMax`, `MathClamp`, `MathStep`, `MathSign`, `MathSaturate`, `Select` | Preserve zero-divisor, threshold, NaN, tie, and sign behavior. |
| Masked | `Float3Min`, `Float3Max` | Per-component ordered compare/select. |
| Scalar fallback | `Sphere`, `Box`, `BoxRounded`, `Capsule`, `Cylinder`, `Torus`, `Ellipsoid`, `HollowCylinder`, `TaperedCylinder`, `Panel`, `Plank`, `RoundedBox`, `CappedTorus`, `Cone`, `RoundCone`, `FakeRoundCone`, `Segment`, `Pyramid`, `HexPrism`, `Link` | These transitively use scalar `length` / `sqrt`; vector library results are not assumed bit-identical. |
| Scalar fallback | `Revolution` | Uses scalar 2D length. |
| Scalar fallback | `Twist`, `Bend` | Scalar `sin` / `cos` are retained per lane. |
| Scalar fallback | `RepeatLimited` | Scalar round/tie behavior is retained per lane. |
| Scalar fallback | `MathSin`, `MathCos`, `MathPow`, `MathSqrt`, `MathExp`, `MathLog`, `MathLog2` | Transcendental/library calls have no vector bit-identity guarantee. |
| Scalar fallback | `DistanceTo`, `Float3Normalize` | Length/sqrt/reciprocal remain scalar-per-lane. |
| **STOP** | `InvokeRecipe` | Requires a registry/callee graph that the frozen bulk request does not carry. |

This table covers all 91 VIXEN enumerators. Canonical opcode 110 (`CurlNoise3D`) is deliberately
not present in the VIXEN mirror and is therefore outside this request's stream.

## 4. Existing pipeline capability and exact gap

The reusable compiler machinery is real but narrower than the recipe VM:

- `WidenedType` defines `F32x4`, `I32x4`, `U32x4`, `Mask4`, and `F32x3x4`. There is no neutral
  8- or 16-lane representation.
- `WidenedProgramBuilder` widens locals, casts, calls, returns, and mask-convertible branches for
  an authored Roslyn body. It can accept consumer-provided initial lane bindings.
- Burst consumes that neutral program through `BurstWidenedProgramRenderer`.
- The native C++ pilot reuses the same neutral program, but currently renders only a single
  read/write float field, `F32x4` / `Mask4`, arithmetic, comparison, and `select` over SSE2.
- `RecipeContainerEmitter` emits the wire layout only. `EmitSdfOpCodeEnum` emits names and
  ordinals only. `[SdfCoreKernel]` is an empty marker and carries no instruction binding data.

The missing compiler model must express, for every recipe opcode:

1. input and output value kinds;
2. value-stack and position-stack effects;
3. each callable parameter's source: current position, stack slot, instruction data component,
   request parameter, or a `ParamMask`-selected stack value;
4. result placement and distance-scale behavior;
5. control behavior (`RestorePos`, `DeclarePosition`, and any future instruction control);
6. bit-identity class: widened, mask-lowered, scalar-per-lane, or rejected;
7. the canonical opcode ordinal plus a fail-closed consumer-extension mechanism.

Without those declarations, the compiler cannot derive the VIXEN interpreter's operand mapping
from `[SdfCoreKernel]` method bodies. Several graph nodes construct instruction data slots
manually, so parameter names and AST shape are not a reliable substitute.

## 5. Required compiler extension

The minimal acceptable implementation is a compiler-owned two-layer backend.

### `RecipeLoweringModel`

Add declared recipe instruction metadata beside the canonical opcode/callable schema. The model
joins opcode ordinal, `[SdfCoreKernel]` callable, stack/control descriptor, operand bindings, and
bit-identity class. Missing, duplicate, or unclassified entries fail generation. Mark
`ReadParam` / `ReadParamFloat3` as canonical core operations. Provide a declared extension seam
for consumer-only opcodes so `DeclarePosition` is not a handwritten VIXEN switch case.

`InvokeRecipe` needs an owner choice before admission:

- pre-flatten/unroll each request into a closed stream before enqueueing, preserving the frozen
  bulk contract; or
- extend the request with a registry/callee graph, which is forbidden by the current contract.

Until that choice is made, generated lowering must reject `InvokeRecipe` and the materializer must
not select an approximate path.

### `RecipeSimdEmitter`

For each mathematical callable, feed its authored body through `WidenedProgramBuilder`. Extend the
C++ renderer for widened returns, `F32x3x4`, masks, and the admitted intrinsic set. Emit scalar
per-lane thunks for classified library/transcendental operations. Do not independently reproduce
their formulas.

The emitter must generate both source-generator output and a
`CodegenTool --recipe-simd-cpp ... [--check]` artifact through the same model builder. The output
contains the runtime request lowerer and evaluator:

- a compact lowered record with preassigned value/position stack slots;
- broadcast instruction constants and resolved flat request-parameter indices;
- a generated execution thunk selected once per instruction during lowering;
- SIMD4 SoA value/position stacks;
- generated mask operations with scalar-compatible choice semantics;
- scalar-per-active-lane calls to the existing generated scalar kernels where required;
- fail-closed validation for unknown opcodes, bad stack shapes, unsupported control, and missing
  compiler metadata.

This is “generated runtime lowering”: source generation compiles the semantics once, while the
generated lowerer specializes each runtime-authored instruction stream once per materialization
request.

## 6. Intended engine consumption (public contract unchanged)

No change is required or permitted in `BulkMaterialization.h`.

The eventual engine integration should be private:

1. `CpuRecipeMaterializer` validates and lowers the request-owned instruction stream once.
2. Both `BakeSdfWorld` passes submit x-major batches of positions to the same lowered program.
3. Results are consumed in the existing scalar order, preserving occupancy, voxel insertion, and
   serialized ordering.
4. Cancellation is checked at the start of every lane batch and at the existing phase boundaries.
5. An internal scalar mode invokes the same real backend orchestration for parity and benchmark
   comparisons; it is not added to the public backend contract.

Suggested private seam:

- `detail::CpuRecipeEvalMode { Scalar, Lane4, Default }`
- `detail::materializeCpuRecipe(request, stopToken, mode)`

The existing compiler is SIMD4-only. Lane8/Lane16 should not be represented as native widths
until `WidenedType`, neutral tokens, renderers, runtime ABI, and parity tests are generalized.

## 7. Floating-point policy

The current SVO target invalidates a bit-identity proof. It applies `/fp:fast` on MSVC and
`-mavx2 -mfma -mf16c -ffast-math` on GCC/Clang; the configured compile database confirms those
flags reach `BulkMaterialization.cpp`.

The future scalar reference and generated lane evaluator must be isolated in strict translation
units with the same policy:

- GCC/Clang: `-fno-fast-math -ffp-contract=off` (and no reassociation/reciprocal shortcuts);
- MSVC: `/fp:strict` with contraction disabled for the relevant toolchain;
- identical rounding and FTZ/DAZ environment on both paths;
- no FMA contraction, even where the target globally enables FMA;
- exact within-lane source operation order;
- ordered compare/select in place of native SIMD min/max where scalar GLM choice semantics differ.

Strict flags must be verified in the actual compile commands. Applying them only to tests would
not prove the production implementation.

## 8. Required parity and cancellation gates

These tests were designed but not added because implementation stopped before the missing compiler
model was approved:

Kernel compiler:

- `RecipeSimdModel_AllCoreOpcodesHaveExactlyOneBinding`
- `RecipeSimdModel_MissingOrDuplicateBindingFailsClosed`
- `RecipeSimdModel_IncludesSdfCoreControlOps`
- `RecipeSimdEmitter_UsesWidenedProgramForMathBodies`
- `RecipeSimdEmitter_UnsupportedIntrinsicUsesScalarPerLane`
- `RecipeSimdCpp_GenerateCheckTamper`
- `RecipeSimdCpp_AllOpcodeDescriptorsMatchCanonicalOrdinals`
- `RecipeSimdCpp_ScalarVsSimdBits_AllVectorizedOps`
- `RecipeSimdCpp_ScalarFallbackBits_TranscendentalsAndSpecialFloats`
- `RecipeSimdCpp_TailsAndPaddingRemainUntouched`

VIXEN:

- `RecipeSimdParityTest.ScalarVsSimdBitIdentityAcrossParityCorpus`
- `BulkMaterializationIntegrationTest.ScalarVsSimdCanonicalHashParity`
- `BulkMaterializationIntegrationTest.SimdBatchCancellationProducesTerminalResult`

The first gate must compare float bit patterns, not tolerances. The real-backend gate must compare
`canonicalHash`, which includes every serialized stream, vector length/data, configuration field,
channel, and count. The existing fake-backend cancellation test is insufficient for batch-level
CPU cancellation, so a deterministic internal batch-progress test seam is required.

## 9. Benchmark status

No materialization benchmark was run. Running scalar timings before the compiler-owned evaluator
exists would not produce the required before/after comparison, and implementation stopped before
creating the benchmark target.

| Mode / width | Five Release rounds, pinned cores | Median | Canonical hash | Status |
|---|---:|---:|---:|---|
| Current scalar | Not run | — | — | Blocked before benchmark target creation. |
| Compiler SIMD4 | Not run | — | — | Compiler recipe lowering model does not exist. |
| Native lane8 | Not run | — | — | No `WidenedType` / renderer / ABI for width 8. |
| Native lane16 | Not run | — | — | No `WidenedType` / renderer / ABI for width 16. |

The eventual `benchmark_simd_materialization` should follow `benchmark_voxel_batch`'s Release,
pinned-core, five-round sorted-median format. One binary should run the same full request under
scalar and SIMD4 modes, include one lowering per materialization, report throughput and canonical
hash, and reject any mode whose hash differs. Auto-vectorization versus explicit intrinsics was
not compared; the existing compiler-owned C++ path is explicit SSE2 SIMD4 and is the only valid
starting candidate.

## 10. CPU/GPU shape and divergences

The desired common shape is sound: one instruction stream is specialized once, followed by many
independent lanes. GPU already assigns a sample to each shader invocation, while the proposed CPU
artifact uses four SoA lanes.

Current divergences that must remain top-level gates:

- CPU strict bit identity requires scalar-per-lane library math; GPU shader math is allowed by its
  existing tolerance harness and is not a CPU byte-identity oracle.
- The VIXEN GLSL core-kernel file is mechanically translated rather than emitted through the same
  current CLI artifact path.
- GPU nested-recipe generation can receive registry context and recursively inline a callee;
  `BulkMaterializationRequest` cannot.
- CPU `DeclarePosition` / `InvokeRecipe` and parameter control are not canonical compiler-owned
  operations today.
- The native compiler backend exposes SIMD4 only, not the GPU's scalable thread count or native
  CPU widths 8/16.

## 11. Validation performed

Read-only architecture and source measurement was performed in both clean worktrees. No source
implementation was written. No build or test command was run after the STOP finding because the
only committed artifact is this report.

The documented landed baseline remains 102 enabled tests:

| Suite | Documented enabled count | Post-report run |
|---|---:|---:|
| Gaia voxel world | 26 | Not run |
| Gaia coverage | 34 | Not run |
| SVO injection queue | 9 | Not run |
| SVO injection/materialization | 5 | Not run |
| SoA SDF serialization | 13 | Not run |
| SoA mip serialization | 6 | Not run |
| Shell octree/GPU | 9 | Not run |
| **Total** | **102** | **Not run** |

Final post-commit build count: **not run (documentation-only STOP report)**.

Final post-commit test count: **not run; no claim of 102/102 verification is made**.

## 12. Not delivered vs brief

- Compiler-owned recipe lowering model and generated request-time lowerer.
- Lane-batched `BakeSdfWorld` path.
- SIMD4 evaluator and scalar-per-lane fallback thunks.
- Scalar-vs-SIMD bit/canonical-hash parity tests, including the real backend.
- Real batch-level cancellation test.
- Strict-FP translation-unit build configuration and compile-command verification.
- Release before/after benchmark and lane-width table.
- Auto-vectorization versus explicit-intrinsics comparison.
- Lane8/Lane16 support or measurements.
- Kernel and engine build/test gates.

These items were intentionally not approximated. The blocker is the missing compiler-owned recipe
instruction semantics plus the unresolved frozen-contract behavior for `InvokeRecipe`; proceeding
inside VIXEN would contradict the owner amendment.
