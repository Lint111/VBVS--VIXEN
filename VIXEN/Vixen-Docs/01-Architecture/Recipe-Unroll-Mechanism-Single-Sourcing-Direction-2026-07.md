# Recipe Unroll Mechanism Single-Sourcing — Direction (2026-07-16)

> **Status: FUTURE DIRECTION, not yet scoped into milestones.** Captured after a grounded research
> pass into the current codebase's actual coupling (2026-07-16, three rounds — see Research
> Provenance below). Not started — no design doc, no implementation plan. This doc exists so the
> idea and its research grounding aren't lost. Supersedes an earlier draft of this doc (a
> "registration framework" framing) once the idea was clarified through discussion into the sharper
> form below.

## The idea, precisely stated (final form, after iteration)

Today, per-opcode **math** (`SdfCore_Sphere`, `SdfCore_Union`, `SdfCore_Twist`, etc.) is already
single-sourced as C# `[KernelCallable]` methods and transpiled automatically to C++/HLSL by
Yeroket's shared `CppAstVisitor`/`HLSLVisitor` pipeline. That part of the framework works exactly as
intended. What's hand-duplicated instead — confirmed by direct code inspection, see Research
Finding 2 below — is the **mechanism that eliminates orchestration and produces a formalized
output**: today that mechanism is two independent, hand-written C++ emitters
(`SdfRecipeCodegenGlsl.h`, `SdfRecipeCodegen.h`), each re-implementing the identical stack-walking
unroll algorithm from scratch, once per target language.

**Important correction, established through discussion — orchestration and formalized output are
NOT two composable callable kinds layered together. They are two mutually exclusive EXECUTION
STRATEGIES for the same recipe:**

- **Orchestration** = the runtime interpreter path (`SdfRecipeEval.h::evalRecipe`): given a list of
  recipe steps (bytecode), EXECUTE them at runtime — VM state and all, live stack pushes/pops,
  opcode dispatch, position/distScale bookkeeping happening fresh on every call. This is what "a
  recipe" fundamentally IS: a list of steps meant to be performed.
- **Formalized output** = the recipe's steps, taken as a whole, PRE-ROLLED at codegen time into one
  self-contained, focused program — callable as a single method with a clear, typed
  input/output signature, with the VM logic (stacks, opcode dispatch, bookkeeping) not present
  *and shared*, but entirely ABSENT from the result. Orchestration doesn't get pulled into the
  formalized output as a component — it's compiled AWAY. What remains is pure straight-line
  computation with no VM state left to reference.

**So the idea is NOT "make orchestration into a callable."** Orchestration is a process, not a
reusable unit — its entire purpose is to consume itself and produce a formalized output that no
longer needs it. The actual, correctly-scoped idea: **the MECHANISM that performs this
recipe→formalized-output unrolling should be single-sourced and reusable across target languages**,
the same way the underlying math primitives already are — instead of today's two independent,
hand-written, per-language unroll algorithms that happen to produce the right shape (a formalized,
self-contained, clearly-typed callable) but do so via duplicated, bespoke C++ code rather than one
shared mechanism.

**The payoff**: today's unrolled emitters are already correctly PRODUCING formalized outputs (this
part of the design is right) — the gap is only that the unrolling MECHANISM itself is duplicated
per language. Single-sourcing that mechanism would mean GLSL, HLSL, and any future target fall out
of one shared unroll pipeline, rather than each needing its own hand-written emitter that walks the
same bytecode with the same stack-simulation logic independently.

## Research Finding 1 — the math layer is already shared and working (no gap here)

`SdfCore_Sphere`, `SdfCore_Union`, `SdfCore_Twist`, `SdfCore_Transform`, etc. are `[KernelCallable]`
C# methods in `SdfCoreKernels.cs` (canonical Yeroket source), transpiled by a real Roslyn
`IIncrementalGenerator` (`SDFNodeSourceGenerator.cs:343-361`'s `kernelCallableProvider`) via
`CppAstVisitor.cs`/`HLSLVisitor.cs` — confirmed to require only SYNTAX-tree walking (no semantic
model / whole-project resolution), meaning the transpiler works on isolated method bodies, not
whole-compilation context. This machinery already, correctly, produces the vendored `.g.hpp`/
`.g.hlsl` primitive kernel files this repo uses. **Nothing about this layer needs to change.**

## Research Finding 2 — the un-shared orchestration layer, with concrete examples

Confirmed via direct line-by-line comparison of `SdfRecipeCodegenGlsl.h` (GLSL) vs.
`SdfRecipeCodegen.h` (HLSL) for three representative opcodes:

**Sphere** (leaf case — minimal orchestration): both emitters (`SdfRecipeCodegenGlsl.h:66-74`,
`SdfRecipeCodegen.h:104-112`) call the shared `SdfCore_Sphere(...)`, then independently allocate a
temp variable name and `push_back` it onto an emit-time value stack (`std::vector<std::string>
stk`). Line-for-line identical structure between the two files, differing only in `vec3(` vs.
`float3(`.

**Union** (2-operand case): both emitters (`SdfRecipeCodegenGlsl.h:83-91`,
`SdfRecipeCodegen.h:121-129`) contain an `assert(stk.size() >= 2)` underflow guard, two
`pop_back()`s (binding operand order — `b` then `a`, encoding the VM's stack-machine semantics),
call the shared `SdfCore_Union(a, b)`, then push a fresh temp. The assert/pop/push bookkeeping has
no `[KernelCallable]`/`[VMKernel]` counterpart anywhere — it's inline C++, duplicated verbatim
(same variable names, same assert message) between the two emitter files.

**Twist/Transform + RestorePos** (position-stack + distScale-stack case): both emitters
(`SdfRecipeCodegenGlsl.h:430-453` push-side, `:471-484` pop-side; byte-identical structure in
`SdfRecipeCodegen.h:468-522`) call the shared `SdfCore_Transform`/`SdfCore_Twist`, then independently
rebind a `curPos` variable, push the old position + a distScale value onto TWO separate emit-time
stacks, and — on `RestorePos` — pop both stacks and CONDITIONALLY synthesize an extra multiply
statement if the popped distScale isn't ≈1.0 (`SdfRecipeCodegenGlsl.h:471-484`'s `if (std::abs(scale
- 1.0f) > 1e-4f)` branch). This conditional-rescale-on-pop control flow is pure emit-time logic with
literally no `SdfCore_*` counterpart — confirmed the distScale value itself
(`SdfCoreKernels.cs:111`'s own comment) is explicitly documented as "pushed to distScaleStack by
VIXEN (not a kernel param)," i.e., the C# author already treats this as VM-level bookkeeping outside
any kernel's own math, by design.

**Conclusion**: the orchestration layer (stack push/pop/arity/rebind/conditional-rescale) is real
and structurally identical between the two hand-written emitters — but per the correction above,
this is NOT something that should become its own callable. It's the RUNTIME-EXECUTION-shaped
bookkeeping that the unroll mechanism must walk through and ELIMINATE when producing a formalized
output, exactly as both emitters already do (their whole job is turning "N steps of VM bookkeeping"
into "zero VM bookkeeping, one straight-line function"). The actual gap this finding demonstrates:
that elimination logic — walking the bytecode, simulating the stacks AT EMIT TIME, and emitting only
the surviving straight-line math-call sequence — is hand-written twice, independently, once per
target language, rather than being one shared mechanism the two emitters both invoke.

## Research Finding 3 — no first-class `Kernel` type exists anywhere (from the prior research round)

Neither VIXEN C++ nor Yeroket C# has a `struct`/`class` representing "one compiled emit-unit:
signature + body + target language + source recipe." A "kernel" today is purely an implicit,
unnamed byproduct — a raw `std::string` identified only by naming convention
(`sdfRecipe_<recipeId>(vec3 p, float params[6])`). `RecipeRegistry::RecipeEntry` registers a
recipe's bytecode INPUT, with no field for what it compiles TO. This still holds under the final
framing: an "implicit/derived KernelCallable" for a composed recipe would still need SOME identity
(signature, target language) even if it's synthesized rather than separately registered — this
doesn't disappear just because composition itself becomes callable-shaped.

## What this means for scope

The concrete target, if this is ever picked up: single-source the UNROLL MECHANISM (the emit-time
bytecode walk + stack simulation + straight-line-code emission) as one shared piece of logic,
parameterized by target language, rather than as two independent hand-written C++ files
(`SdfRecipeCodegenGlsl.h`, `SdfRecipeCodegen.h`) that each re-implement the identical walk. The
existing per-opcode `SdfCore_*` math calls stay exactly as they are today (already shared, already
correct). What changes is only WHO performs the "walk the bytecode, simulate the stacks, emit a
formalized output" process — one shared mechanism instead of two hand-copies of it.

## Explicitly NOT yet answered — needs real design work, not assumed

- **What does "single-sourcing the unroll mechanism" concretely look like in C++, given the
  mechanism itself (not just the math) needs to change per target language (GLSL `vec3` vs. HLSL
  `float3` literal/type syntax)?** The two existing emitters are ~85-90% structurally identical
  (Research Finding 2/earlier rounds) with the difference concentrated in literal/type-name
  spelling — this suggests the shared mechanism could be a single templated/parameterized walker
  with a small per-language "syntax adapter" (how to spell a vec3 literal, a function call, an
  assignment) rather than needing the FULL emitter duplicated. This is plausible from the evidence
  but not yet designed or prototyped.
- **Should this reuse Yeroket's Roslyn-based transpiler pipeline (cross-engine, C#-sourced) or stay
  a VIXEN-local C++ mechanism (single-sourced within VIXEN only, not cross-engine)?** The existing
  `CppAstVisitor`/`HLSLVisitor` transpile C# METHOD BODIES (the math primitives) — they don't walk
  `SdfInstruction[]` bytecode at all today (bytecode is a VIXEN/runtime concept, not something that
  exists in the C# source the Roslyn visitors parse). Reusing that pipeline for the UNROLL mechanism
  itself (not just the math it calls into) would mean either representing recipe bytecode in C#
  source form somehow, or accepting this stays a separate, VIXEN-local unification (a shared C++
  walker, not touching the Roslyn/C# side at all) — these are two substantially different-scoped
  efforts and the research so far doesn't establish which is right.
- **Is a second, non-SDF domain actually needed to justify this?** Same caveat as before: this is a
  feasibility/cleanliness finding (real, hand-duplicated logic exists) not a demonstrated need from
  a second concrete consumer. Worth resolving before investing further, since a single-domain
  codebase can also just accept "two ~900-line hand-parallel files, kept in sync by discipline" as
  its cost of doing business if no second target/domain is actually coming.

## Research Provenance

Three research rounds, each correcting/narrowing the prior one based on user clarification — kept
here so a future reader understands why the framing evolved:
1. Round 1: confirmed the existing GLSL/HLSL recipe emitters are ~85-90% hand-duplicated, opcode
   dispatch hardcoded in 5-6 independent places, no first-class `Kernel` type anywhere (Finding 3).
2. Round 2 (initial "should recipes synthesize into KernelCallables" framing): investigated whether
   a WHOLE composed recipe could be synthesized as C# source fed through the existing Roslyn-based
   transpiler — established this is the HARDER, still-open question in the "explicitly not yet
   answered" section above, not the idea's actual core.
3. Round 3 (user clarified "recipe sub-elements... each step should be its own self-contained
   callable"): investigated whether per-opcode orchestration (not just math) is already shared.
   Confirmed NOT shared (Finding 2) — the orchestration/bookkeeping layer is real and duplicated.
4. Round 4 (final correction — user clarified "orchestration works on recipes and not on formalised
   outputs... orchestration takes a list of recipe steps and performs them at runtime, a formalised
   output is said recipe steps pre-rolled into a self-contained and focused program... skipping VM
   logic"): corrected the framing. Orchestration and formalized output are NOT two callable kinds to
   compose — they're two mutually exclusive execution strategies, and orchestration is a process
   that's meant to be COMPILED AWAY, not turned into a reusable callable itself. The real, correctly
   -scoped gap is that the MECHANISM performing this elimination (walk bytecode → simulate stacks
   at emit time → emit formalized output) is hand-duplicated per target language, not that
   orchestration itself needs to become callable.

## Relationship to other in-flight directions

- Builds on the same research substrate as
  [[Recipe-Single-Dispatch-Unrolled-Selection-Direction-2026-07]] (both stem from the 2026-07-16
  Increment 2 M4 closeout's follow-on ideas) but is orthogonal — that direction is about
  dispatch/selection cost between already-unrolled recipes; this one is about the codegen
  mechanism that PRODUCES the unrolled functions in the first place.
- Relates to [[kernel-codegen-framework-direction]] (Inc4, the shipped shared C++/HLSL primitive
  transpiler this idea would extend) and [[Recipe-Parameterization-Plan-2026-07]] (whose
  GLSL/HLSL `ReadParam` drift is a concrete, already-happened instance of the duplication-causes-
  real-bugs risk this idea would close at the root).
