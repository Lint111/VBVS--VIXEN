---
title: Inc4 P2.4 M2 — VM-emitter extension (design of record)
status: DESIGN 2026-06-27 — resolves P2.4 open design-point 1; ready to plan
tags: [architecture, voxel, sdf, recipe, kernel-framework, vm, codegen]
related:
  - "[[SDF-Recipe-Kernel-Codegen-Inc4-P2.4-Spec-2026-06]]"
  - "[[SDF-Recipe-Kernel-Codegen-Inc4-2026-06-Design]]"
---

# M2 — Extend VIXEN's recipe evaluators from leaf+binary to the full VM stack model

> **Goal.** Make VIXEN's CPU `evalRecipe` and straight-line HLSL `EmitProceduralComputeShader` mirror the
> Yeroket VM's full execution model, so M3/M4 can add the generic opcode catalogue as pure switch-case +
> generated-kernel work. The one structural gap is a **position stack**; everything else is new cases.

## 1. Reference model — the Yeroket VM (what VIXEN must mirror)

The C# VM (`com.utility.sdf/Runtime/Burst/SDFCompiledEvaluator.cs` + `SDFEvalContext.cs`) is a flat-array
stack machine. Registers that matter for generic recipes:

| Register | Type | Init | Role |
|---|---|---|---|
| `Pos` | `float3` | `worldPos` | current sample point — **mutated in place by domain transforms** |
| `Stack` / `Sp` | `float[64]` / int | empty | value stack; float3s stored as 3 interleaved floats |
| `PosStack` / `Psp` | `float3[64]` / int | empty | position save/restore stack for domain transforms |
| `DistScale` | `float` | `1` | `*=` on Transform (min-abs-scale, `Data2.w`); **hard-reset to 1 by RestorePos**; not stacked |
| `Parameters` | `float[]` | caller | runtime params (only used when `ParamMask`≠0 — see §4) |

**Instruction** = 132 B: `OpCode`, `InputMask` (binary: which inputs connected), `ParamMask` (bit N → pop
param N off the value stack instead of using the baked literal), then `Data0..7` (= `data[0..31]`).

**Per-category eval rules** (the exact arithmetic is the parity oracle):
- **Leaf primitive** (Sphere/Box/Torus/…): read `Pos`, compute signed distance, `Stack[Sp++] = d`. No
  pops (params from `Data` when `ParamMask`=0). Does not touch `PosStack`.
- **Binary CSG** (Union/Subtract/Intersect/Smooth*/Xor): `b = Stack[--Sp]; a = Stack[--Sp];
  Stack[Sp++] = op(a,b[,k])`. `k` from `Data0.z`. B is top, A is second (compiler emits A's subtree first).
- **Domain transform** (Transform/Twist/Bend/Elongate/Mirror{X,Y,Z}/Repeat{Infinite,Limited}/Revolution):
  `PosStack[Psp++] = Pos; Pos = f(Pos, Data…)`. Transform also `DistScale *= Data2.w`.
- **RestorePos**: `Pos = PosStack[--Psp]; DistScale = 1`.
- **Value/float3 math** (MathSin/Add/…/Lerp/Select/Displacement; Float3Add/…): value-stack ops (unary =
  peek+modify `Sp-1`; binary = pop B, modify `Sp-1`; float3 = 3-float groups).
- **VM control**: `Output` no-op (result = `Stack[Sp-1]`); `PushParam` push `Data0.x`; `ReadParam` push
  `Parameters[Data0.x]`; `PositionChannel` push a `Pos` component; `PushFloat3` push 3; `ComposeFloat3`
  no-op (3 scalars already on stack); `DecomposeFloat3`/`Passthrough` **no-op in the Burst VM**.

**Compiler ordering contract** (`SDFCompiledGraph.cs`): post-order for normal nodes (children → params
ascending-bit → node); for **domain** nodes: params → **domain instruction first** → children →
**RestorePos last**. So a transformed subtree is delimited purely structurally — no offsets. Nesting is
correct because each transform pushes/pops its own `PosStack` level. Result = `Stack[Sp-1]` after the last
instruction.

## 2. VIXEN current state (what we extend)

Both evaluators are ALREADY stack machines (only the position stack + the opcode set are missing):
- `SdfRecipeEval.h::evalRecipe` — `float stack[64]; sp`, loops instructions, switch on Sphere(push)/
  Union(pop2,push1), samples a FIXED `p`. Calls vendored `Yeroket::Sdf::Generated::SdfCore_{Sphere,Union}`.
- `SdfRecipeCodegen.h::EmitProceduralComputeShader` — emit-time name stack `std::vector<std::string> stk`,
  emits `float tN = SdfCore_…;` straight-line, assembles `sdfCoreHlsl + sdfRecipe + kTraceMain`. Params
  baked as float literals.
- `SdfInstruction` (`data[32]`, 132 B) + `kTraceMain` need **no change**.

## 3. The design — add a position stack, mirror the VM

**`evalRecipe` (CPU):**
```
glm::vec3 pos = p;                 glm::vec3 posStack[64]; int psp = 0;
float stack[64]; int sp = 0;       float distScale = 1.0f;
for each instr:
  leaf:            stack[sp++] = SdfCore_X(pos, <Data...>);          // sample `pos`, not the fixed p
  binary:          b=stack[--sp]; a=stack[--sp]; stack[sp++]=SdfCore_Op(a,b[,k]);
  domain xform:    posStack[psp++] = pos; pos = SdfCore_X(pos, <Data...>);  // (Transform: distScale *= Data2.w)
  RestorePos:      pos = posStack[--psp]; distScale = 1;
  value math:      value-stack ops (peek/pop/push)
return stack[sp-1];
```
**`EmitProceduralComputeShader` (HLSL emit):** mirror with a second emit-time stack of position-expression
names. The position stack unrolls to SSA temporaries (depth is statically known → no runtime stack/loop):
```
posStk = ["p"];   valStk = [];   // emit-time
  leaf:         emit `float tN = SdfCore_X(<posStk.back()>, <lit Data...>);`  valStk.push(tN)
  binary:       a=valStk.pop(); b=valStk.pop(); emit `float tN = SdfCore_Op(a,b[,k]);` valStk.push(tN)
  domain xform: emit `float3 pK = SdfCore_X(<posStk.back()>, <lit Data...>);` posStk.push(pK)
  RestorePos:   posStk.pop()                                          // back to the saved name
final: `return <valStk.back()>;`
```
This is the SAME stack discipline the C# compiler emitted the stream in — VIXEN's emit-time stacks replay
it. Both backends call the **same generated `SdfCore_*` kernels** (CPU = glm `.g.hpp`, HLSL = `.g.hlsl`),
so CPU/GPU parity is structural.

**Why the crux is solved:** domain transforms in a flat stream worried the spec. The C# compiler's
`transform → children → RestorePos` ordering + the statically-known stack depth means the emit-time
position stack reproduces it exactly with named temporaries — straight-line HLSL, no loops, no branches.

## 4. Design decisions

1. **`ParamMask` = 0 only (static/baked recipes) for P2.4.** VIXEN's recipes bake params as literals
   (compile realization); old serialized data is `ParamMask=0` too. Dynamic param subgraphs (`ParamMask`≠0,
   params popped off the value stack) are the runtime-parameterized path — **deferred to the GPU-VM / dynamic
   work (design-doc P4)**, not needed to render baked recipes. M2 reads operands from `Data` only.
2. **`DistScale`** — Transform's non-uniform-scale distance correction. The Burst VM accumulates it but does
   not auto-apply (leaves it to the consumer). M2 **mirrors whatever the C# HLSL/managed codegen does** (verify
   against `KernelHlslFunctionWriter`/the managed eval as the first M2 task) and pins it with conformance.
   First representative Transform uses **rigid (rotation+translation, scale=1 ⇒ DistScale=1)** to keep M2's
   parity unambiguous; non-uniform-scale correctness is verified-or-deferred to M4 with a note.
3. **`DecomposeFloat3` / `Passthrough` = no-op** (mirror the Burst VM — they have no `EvalBurst`).
4. **Excluded (unchanged from the P2.4 spec):** flow-control (Loop/Branch/Jump — not straight-line),
   blackboard/array, noise/FBM, marching-cubes, plant/turtle. Not generic and/or not unrollable.
5. **Parity oracle for M2 = analytic** (mirrors the existing `test_recipe_eval_parity` shape: `evalRecipe`
   result == an analytic formula at N sample points). Full **C#-reference-vector export** (run the C#
   evaluator via dotnet, commit distances) is the robust long-term oracle — adopt it in M3/M4 when opcode
   arithmetic gets too gnarly to re-derive analytically. (This is the design-doc's deferred "conformance
   export".)

## 5. M2 milestone scope (the structural proof)

M2 lands the **position-stack extension + one representative opcode per new structural lane**, so M3/M4 are
pure fill-in:
- A new **leaf primitive** (e.g. `Box`) — proves "sample `pos`, baked vec3 params".
- A new **binary CSG with a k param** (e.g. `SmoothUnion` or `Subtract`) — proves the param/`k` path.
- A **domain transform + RestorePos** (e.g. `MirrorX` or `Twist`, rigid) — proves the position stack on
  both evaluators.

Per opcode: Yeroket marks it `[KernelCallable]`+`[SdfCoreKernel]` + kernel math → regen (dotnet, §P2.4
resolved path) → re-vendor; VIXEN adds the `SdfOpCode` value + one `evalRecipe` case + one emit case.
**Gates:** CPU analytic parity (`evalRecipe`) for each; `EmitProceduralComputeShader` output compiles to
SPIR-V (existing `test_recipe_codegen` gate); and a **live lavapipe render gate** of a shape that needs the
position stack (e.g. a mirrored/twisted CSG of two primitives) — the authoritative proof the unrolled HLSL
matches the CPU eval. No-regression on the Sphere/Union recipe.

Then: **M3** = remaining primitives + all binary CSG + level-set modifiers (mechanical batch). **M4** =
remaining domain transforms + value/float3 math (+ the `UPDATE_GOLDENS=1` regen-write mode + non-uniform
DistScale). **M5** = broad live CSG composition render gate + full no-regression.

## 6. Risks

- **DistScale convention** (decision 2) — the one real parity subtlety; pin it in M2's first task by reading
  the C# codegen, not by guessing.
- **Enum-value ordering** stays a cross-repo binary contract (append-only; match the C# byte values).
- **Emit-time stack underflow/overflow** must be guarded (the C# compiler validates stack balance; VIXEN's
  emitter should assert `valStk`/`posStk` non-empty on pop and a non-empty `valStk` at `return`).
