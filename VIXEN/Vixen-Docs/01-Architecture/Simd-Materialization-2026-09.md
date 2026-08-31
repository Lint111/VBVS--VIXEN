# SIMD Materialization — Run 3 Delivery Report

Date: 2026-08-31
Kernel commits: `939c2880`, `fdfed89f` (`lane-simdmat`)
Engine commits: `5f8dd399`, `27de0970` (`lane-voxmut`)
Status: delivered with SIMD4 as the selected CPU default; full-tree AppFlow drift check remains controller-gated by an existing nested-worktree path.

## 1. Result

`CpuRecipeMaterializer` now lowers each owned, closed recipe stream once and evaluates
`BakeSdfWorld` samples in four-lane SoA batches. The lowering and widening capability is owned by
the Yeroket kernel compiler: VIXEN declares its two consumer opcodes, consumes the generated C++
artifact, and owns only request orchestration and proof tests. `BulkMaterializationRequest` and
`IMaterializationBackend` are unchanged.

The resolution-64, five-round, pinned-core full-materialization benchmark measured a 4.65% median
time reduction (1.049x speedup) with the same canonical hash. SIMD4 is therefore the selected
default. SIMD8/16 were not attempted because Run 3 explicitly limits the neutral IR and native ABI
to SIMD4.

## 2. Opcode classification and coverage

`RecipeLoweringModel_CoversAll91ConsumerOpcodesExactlyOnce` proves 91 distinct names and ordinals.
Generation fails on malformed extension declarations, duplicate names/ordinals, missing callables,
missing classifications, widening-coherence failures, and classified-vector operations that ask
the neutral builder for scalar fallback.

| Class | Count | Opcodes | Bit-identity treatment |
|---|---:|---|---|
| Elementwise | 25 | `Plane`; `Round`, `Onion`; `Transform`; `MirrorX/Y/Z`; `RepeatInfinite`; `MathAdd/Sub/Mul/Abs/Frac/Lerp/Negate`; `Displacement`; `Float3Add/Sub/MulComponentWise/ScalarMul/Dot`; `RestorePos`, `DeclarePosition`, `DecomposeFloat3`, `PositionChannel` | Same source order through `WidenedProgram`; no reassociation or contraction. `PositionChannel` length uses scalar `sqrt` per lane. |
| Masked | 25 | `Union`, `Subtract`, `Intersect`, `Xor`; all seven smooth CSG variants; `Elongate`, `TriangularPrism`; `MathSmoothstep/Remap/Div/Min/Max/Clamp/Step/Sign/Saturate/Select`; `Float3Min/Max` | Ordered compare/select; no `_mm_min_ps`/`_mm_max_ps` shortcuts, preserving NaN/tie/signed-zero choice behavior. |
| Scalar-lane fallback | 33 | `Sphere`, `Box`, `BoxRounded`, `Capsule`, `Cylinder`, `Torus`, `Ellipsoid`, `HollowCylinder`, `TaperedCylinder`, `Panel`, `Plank`, `RoundedBox`, `CappedTorus`, `Cone`, `RoundCone`, `FakeRoundCone`, `Segment`, `Pyramid`, `HexPrism`, `Link`; `Revolution`, `Twist`, `Bend`, `RepeatLimited`; `MathSin/Cos/Pow/Sqrt/Exp/Log/Log2`; `DistanceTo`, `Float3Normalize` | Four-lane thunk calls the existing generated scalar kernel once per lane. No vector-library approximation. |
| Resolved at lowering | 4 | `PushParam`, `ReadParam`, `PushFloat3`, `ReadParamFloat3` | Constants broadcast; request parameters snapshot to flat scalar components with existing out-of-range zero fill. |
| Lowered away | 4 | `Output`, `ComposeFloat3`, `Passthrough`, `InvokeRecipe` | Marker/no-op instructions are removed. `InvokeRecipe` must first be recursively unrolled; a surviving invocation fails lowering. |
| STOPped | 0 | — | No opcode required an approximate default. |

Canonical opcode 110 (`CurlNoise3D`) is not part of the VIXEN 91-op consumer mirror and remains
outside this request stream.

## 3. Lowered-form design

The compiler model joins canonical opcode/callable symbols to execution class, lowering kind,
value/position stack effects, control behavior, callable alias, and lane-parameter bindings.
`ReadParam` and `ReadParamFloat3` are now canonical `[SdfCoreOp]` declarations. VIXEN-only controls
use a structured extension line:

`Name=ordinal|execution|lowering|vPop|vPush|pPop|pPush|control`

Thus `DeclarePosition` and `InvokeRecipe` enter through declared model data. The renderer generates
their names and control branches from that model rather than maintaining a VIXEN opcode switch.

For each request, generated `LoweredRecipeProgram::Lower`:

1. validates every stack transition against the shared 64-slot guards;
2. records prevalidated value/position stack bases per instruction;
3. copies/broadcasts instruction constants and resolves request parameter components;
4. resolves one generated execution thunk pointer per retained instruction; and
5. rejects unknown opcodes, open invocation streams, invalid final stack shape, and missing thunks.

The hot evaluator restores the recorded stack bases, invokes the thunk directly, and operates on
SIMD4 value and position stacks. It has no hot opcode/type-dispatch switch. Tail batches duplicate
the last valid lane only for evaluation and consume only the original lane count.

## 4. Shared neutral-program proof

Every admitted `[SdfCoreKernel]` body is formed as a standalone `KernelInfo`, checked by
`WideningCoherenceValidator.ValidateStandaloneCallable`, and passed to the existing
`WidenedProgramBuilder`.

- Elementwise/masked programs are rendered from that neutral program by
  `CppWidenedProgramRenderer`; a `BurstWidenedProgramRenderer` witness renders the same program
  during generation and fails the artifact if the shared neutral form diverges.
- `CppWidenedProgramRenderer` now admits widened returns, `F32x3x4`, masks, ordered math intrinsics,
  mixed uniform/lane float3 expressions, and scalar lane math helpers.
- `NeutralWideningGoldenTests` was extended for the C++ float3 vocabulary, ordered min, scalar
  square root, and sign-bit unary negation. Existing Burst goldens were not recaptured or bypassed.
- Unary vector negation XORs the IEEE sign bit; the exhaustive parity test caught and eliminated
  the incorrect `0 - x` behavior for `+0 -> -0`.

Both source-generator and `--recipe-simd-cpp ... [--check]` entry points call the same
`RecipeSimdEmitter` and model builder. The checked-in `RecipeSimd.g.hpp` passes the CLI drift gate.

## 5. Flattener relocation and invocation closure

The engine-authored `VoxelDocumentFlattener.h/.cpp` pair was deleted. The compiler artifact now
emits `FlattenVoxelDocument`, its stack/opcode validation, and `UnrollRecipeInstructions`.

Relocation accounting:

- moved: document flattening, layer-combine mapping, shared stack validation, recursive invocation
  closure, cycle/depth/unknown-callee failures;
- deleted: both authored engine flattener files and the SVO source-list entry;
- migrated consumers: editor document model, editor render test, AppFlow editor toggle test, and
  voxel-document flatten tests now include the generated pipeline artifact;
- migrated proof: all eight existing document decode/flatten tests pass unchanged in semantics;
- added proof: `RecipeNestedInvocation.CompilerOwnedUnrollMatchesRecursiveEvaluation` verifies
  open-stream rejection, recursive closure, no surviving invocation, and exact recursive/unrolled
  scalar results;
- landmine register: none; no engine flattener implementation remains.

Bulk requests continue to carry their closed stream in the frozen instruction vector. No registry
or callee graph was added to the public request.

## 6. Engine batching, cancellation, and floating point

`BakeSdfWorld` batches x-major positions four at a time in both occupancy and active-voxel passes.
Scalar callables retain a generic per-lane fallback; `CpuRecipeMaterializer` supplies the generated
SIMD4 evaluator. The second pass compacts active x positions without changing their order, so voxel
insertion and serialization order remain stable.

Cancellation is checked once at the start of every batch and at the existing phase boundaries.
`BulkMaterializationIntegrationTest.SimdBatchCancellationProducesTerminalResult` observes a real
in-flight queue job reach `Cancelled` and increments the cancelled counter.

FMA/FP policy is pinned on the production TU and scalar-reference/parity/benchmark TUs:

- GCC/Clang: `-fno-fast-math -ffp-contract=off` after the SVO target's historical fast flags;
- MSVC: `/fp:strict`;
- no vector transcendentals; no reassociation; ordered compare/select for min/max; and
- scalar and SIMD paths execute under the same runtime FTZ/DAZ environment.

The configured compile database confirmed the strict flags appear after `-ffast-math` for
`BulkMaterialization.cpp` and the proof targets.

## 7. Parity and cancellation gates

| Invariant | Result |
|---|---:|
| `RecipeSimdParity.AllCorpusProgramsAreBitIdenticalAcrossFourLanes` | PASS; every program in `RecipeParityCorpus`, four lanes, exact `bit_cast<uint32_t>` equality |
| `BulkMaterializationIntegrationTest.ScalarVsSimdCanonicalHashParity` | PASS; masked/scalar-fallback and parameter-resolved programs through real `CpuRecipeMaterializer` |
| `BulkMaterializationIntegrationTest.SimdBatchCancellationProducesTerminalResult` | PASS |
| `RecipeNestedInvocation.CompilerOwnedUnrollMatchesRecursiveEvaluation` | PASS |
| `VoxelDocumentDecode` / `VoxelDocumentFlatten` | 8/8 PASS |
| `recipe_simd_check` | PASS after regeneration |

## 8. Benchmark

Release build, box-queue pinned cores 0–7, resolution 64, identical sphere/box/smooth-union request,
five rounds per mode, sorted median, full bake + body-octree build + serialization:

| Method | Lane width | Rounds | Median ms | Canonical hash | Relative |
|---|---:|---:|---:|---:|---:|
| Scalar reference | 1 | 5 | 499.126 | 662401513665600963 | baseline |
| Compiler-lowered SIMD | 4 | 5 | 475.903 | 662401513665600963 | -4.65% time / 1.049x |
| SIMD8 | — | — | Not run | — | excluded by Run-3 ruling |
| SIMD16 | — | — | Not run | — | excluded by Run-3 ruling |

Chosen default: explicit compiler-emitted SIMD4. The recipe VM's dynamic instruction stream is not
a credible compiler auto-vectorization target; explicit SIMD4 is already the shared neutral IR's
approved native ABI and avoids a second dependency. No external SIMD library was added.

## 9. CPU/GPU structural alignment and divergences

Common shape: one closed instruction stream is specialized once, then the same callable bodies are
evaluated over many independent positions.

Remaining divergences:

- CPU packs four SoA lanes; GPU assigns one position to each shader invocation and scales through
  the dispatch grid.
- CPU library/transcendental classes call scalar generated kernels per lane for byte identity; GPU
  continues to use GLSL math and its existing tolerance-based proof.
- CPU snapshots parameter values into lowered records; GPU retains buffer/index parameter access.
- CPU unrolls invocation streams through the generated registry helper before lowering; GPU can
  recursively inline while emitting shader source with registry context.
- C++ and Burst share the neutral widened program. GLSL is still rendered by the established HLSL/
  GLSL emitter path, not by `CppWidenedProgramRenderer`.
- CPU FMA contraction is explicitly off for canonical bytes; GPU contraction policy remains the
  shader toolchain's policy.

## 10. Build and test evidence

Kernel committed tree:

- SourceGenerator and CodegenTool test projects: 0 warnings, 0 errors.
- focused recipe/neutral/schema tests: 11/11 pass; formatting follow-up: 4/4 pass.
- full source-generator suite: 315/315 pass.
- CodegenTool: zero new normalized failure names versus `/tmp/matqueue-kfail.txt`; one baseline name
  recovered in the post-commit run (`Emit_UnconvertedSystem_StillUsesBareMethodGroup`).

Engine committed tree/candidate:

- targeted SVO/materialization/parity/relocation/benchmark build: pass;
- historical 102-test set plus two new materialization tests: 104/104 pass;
- current shell CPU test plus recipe bit parity, invocation closure, and flattener tests: 21/21 pass;
- direct engine total reported here: 125/125 pass;
- full Release graph reached 550/554, then existing `appflow_check` resolved the external core schema
  to missing `/home/liory/Github/undertow/vixen/engine/core/codegen/view-schemas` in this nested
  worktree. This is environmental/controller-gated per dispatch; no custom runner or unrelated
  AppFlow path workaround was added.

## 11. Not delivered vs brief

- SIMD8 and SIMD16 were not implemented or measured; Run 3 explicitly ruled them future work and
  prohibited widening the neutral IR beyond SIMD4.
- A separate auto-vectorized evaluator was not landed. The runtime-authored VM stream requires
  lowering, while the approved compiler path already supplies explicit SIMD4 from the shared
  neutral program; adding a second evaluator would duplicate semantics.
- The full 554-step engine build cannot be claimed green in this nested worktree because the
  unrelated AppFlow drift target uses a missing external core-schema path. All feature targets,
  the new drift check, and required engine test sets passed; the controller must run the full graph
  from a checkout where the external core schema path is valid.
- No GPU evaluator rewrite was attempted; GPU continues to consume its established generated
  shader path over the same canonical callable source.
