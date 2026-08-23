# Descriptor-Gatherer Reverse Coverage Check — Direction (2026-07-17)

> **Status: FUTURE DIRECTION, not yet scoped into milestones.** Captured after a grounded research
> pass following a real bug found during
> [[Recipe-Live-App-Bucketed-Dispatch-Inc4-Plan-2026-07]]'s M1 (binding 35 unbound in production,
> caught by the M1 Opus validator via live validation-layer errors, not any build-time check). Not
> started — no design doc, no implementation plan.

## The bug that motivated this

M1 of the Inc4 live-wiring increment added a new SSBO binding (35, `InstanceSkipMaskBuffer`) to
`SceneBindings.glsl`, correctly used it in the shader, but never wired it into
`BuildRenderGraph.cpp`'s hand-maintained `descriptorGatherer` connection list. Result: an unbound
`STORAGE_BUFFER` descriptor at `vkCmdDispatch` time on every default-scene frame — caught only because
the M1 validator actually ran the live app with validation layers on, not by any build-time signal.
The 11 hand-rolled GTest harnesses that DID catch related issues bypass the production
reflection/gatherer path entirely (they build their own descriptor sets), so they couldn't have caught
this class of bug either.

## The idea, as the user framed it

Could VIXEN have "official shader variant handling in the shader library" — feature-based toggles on
shaders serving as predicates for RenderGraph node connections, so the graph-construction code could
know at build time which bindings a compiled shader variant actually needs?

## What the grounding research found (2026-07-17) — narrower and smaller than the framing suggested

**The premise "shader variants aren't formalized" is correct, but that's not actually the load-bearing
gap.** A full SPIR-V reflection pipeline already runs live, in production, on every graph compile:

- `ShaderLibraryNode` (wired live at `BuildRenderGraph.cpp:376`) drives `ShaderBundleBuilder::
  PerformBuild`, which invokes `SpirvReflector`/`ReflectDescriptorLayout` on the real compiled SPIR-V
  (`ShaderBundleBuilder.cpp:668-736`) — **this directly answers the "is the shader library node used
  at runtime" question: yes, it is the thing that performs reflection on every compile, not a dormant
  or offline-only tool.**
- The resulting `descriptorLayout->bindings` (every binding the compiled shader actually declares) is
  already in scope, at runtime-of-graph-construction, inside
  `DescriptorResourceGathererNode::CompileImpl` (`DescriptorResourceGathererNode.cpp:50-182`) — the
  SAME function that has the wired-slot list (`ctx.InVariadicCount()`/`ctx.InVariadicSlot(i)`) in
  scope simultaneously.
- **That function already validates ONE direction**: every wired slot must match a real, correctly-
  typed shader binding, or gets marked `Invalid` (`ValidateSingleSlotAgainstShader`, lines 337-353).
  **It never validates the reverse direction**: every shader-declared binding must have a
  corresponding wired slot. This exact gap is precisely what would have caught the binding-35 bug at
  build time — the data needed already exists in scope, in the exact function that would need to check
  it, computed by infrastructure that already runs on every build.

**Separately confirmed, narrower findings that shape scope:**
- There is no unified "shader variant system" today. `LOD_ENABLED` looks like a toggle but is
  permanently `#define`d with zero C++-side control (a dead-code-looking always-on flag, not a real
  variant axis). The real variant mechanisms (`VIXEN_GPU_TRACE_HOOKS`, `VIXEN_UBER_RECIPE_SPLICED`)
  are ad-hoc textual `#define`-injection hacks (`BuildRenderGraph.cpp`'s
  `ReadShaderSourceWithTraceHooksGate` prepends a `#define` line before compilation), not a formalized
  system with predicates any node-connection logic could query.
- A codegen tool (`SpirvInterfaceGenerator`) already parses reflection data and emits C++ binding
  aliases (`generated/sdi/*Names.g.h`) on every build — but its own doc comment states its purpose is
  IDE autocomplete/type-safety for code that CHOOSES to use the aliases, not an enforced manifest.
  `BuildRenderGraph.cpp` uses these aliases for SOME bindings and bare integer literals for others in
  the SAME wiring block (one binding is explicitly commented `"hardcoded; no SDI regen yet"`) — this
  confirms the generated header already silently drifts from what's actually wired, today, as
  standing practice, not just in the binding-35 incident.
- Because shader `#define` variants are applied BEFORE compilation and reflection runs AFTER
  compilation, a reverse-coverage check added to `DescriptorResourceGathererNode` would automatically
  be variant-correct with zero new integration work between the preprocessor-variant system and the
  reflection system — reflection already sees whatever variant was actually compiled.

## The actual, narrower opportunity

Not "build an official shader-variant system" — **add the missing reverse-direction loop to
`DescriptorResourceGathererNode::CompileImpl`**: for every binding in `layoutSpec->bindings`, confirm
a corresponding wired slot exists; fail the build (or at minimum log loudly) if not. This uses data
already computed by infrastructure that already runs on every build, inside the exact function that
already does the "easy half" of this check — not a new reflection pipeline, not a new variant system,
not new codegen.

## Explicitly NOT yet answered — needs real investigation before scoping an implementation

- **Is every reflected binding actually SUPPOSED to be graph-wired?** `resourceArray_` is sized by
  `maxBinding+1` (`DescriptorResourceGathererNode.cpp:145`) and some entries may legitimately be
  populated some other way, not through the gatherer's own wiring list — this needs verification
  before assuming a naive "every binding needs a wire" coverage loop is correct. A false-positive
  build failure on a legitimately-unwired-but-fine binding would be worse than the silent gap it
  replaces.
- **What's the right failure mode?** Hard build failure (blocks all progress until fixed, strongest
  guarantee) vs. a loud warning/log (safer to introduce without risk of blocking unrelated work, weaker
  guarantee) — not decided.
- **Should this consume the raw reflection struct (`descriptorLayout->bindings`, always live and
  complete) or the generated SDI header (`generated/sdi/*Names.g.h`, optional/opt-in today)?** The
  research's own answer leans toward the raw reflection data, since it's the one already live inside
  the function that would need the check — but this wasn't fully explored as a design decision.
- **Should `BuildRenderGraph.cpp`'s existing bare-integer-literal bindings be migrated to the generated
  SDI aliases as part of this work, or is that a separate cleanup?** The `"hardcoded; no SDI regen
  yet"` comment suggests this drift is already a known, tolerated state — worth deciding whether fixing
  the coverage-check gap should also clean this up, or stay narrowly scoped to the new check itself.

## Relationship to other in-flight work

- Directly motivated by [[Recipe-Live-App-Bucketed-Dispatch-Inc4-Plan-2026-07]]'s M1 fix round (binding
  35's missing gatherer wiring) but is NOT part of that increment's scope — a separate, small,
  build-tooling improvement.
- Independent of the recipe-codegen-focused direction docs from the same session
  ([[Recipe-Unroll-Mechanism-Single-Sourcing-Direction-2026-07]],
  [[Recipe-Spatial-Contract-Two-Pass-Culling-Direction-2026-07]]) — this is about RenderGraph
  descriptor-wiring correctness generally, not recipe-specific codegen.

## Suggested first step, if picked up

Investigate the "is every reflected binding supposed to be wired" open question first (read
`DescriptorResourceGathererNode.cpp`'s full binding-population logic, not just the two functions this
research already read, to confirm there's no other legitimate path that populates `resourceArray_`
entries outside the gatherer's own explicit wiring calls) — this determines whether a naive coverage
loop is safe to add or would need an exclusion mechanism for legitimately-unwired-but-fine bindings.
