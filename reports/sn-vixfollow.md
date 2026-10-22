# sn-vixfollow: render bridge S2 follow-ups

Branch `lane-vixfollow` (base 1a59a83b). Kernel worktree unchanged (f315389c8); no kernel change was needed.

## What changed
1. Accumulation policy comes from the record, not env.
   - `AccumulationHistoryNode.cpp:139-151`: reads `AccumulationSettings` (`maxFrames`, `resetOnMotion`,
     `reprojectionEnabled`). The node's env reads and `ResolveAccumulationPolicy()` are deleted.
   - `AccumulationHistoryNode.h:62,90`: `SetSettingsSource(const AccumulationSettings*)` (precedent:
     `SetCapacityTracker`, `SetSwapChainWrapper`). Executing without a source throws, so a missing wire fails loudly.
   - `BuildRenderGraph.cpp:1025,1093`: both `scene_radiance` and `accumulation_history` (the `SceneRadianceNode`
     alias shares the policy) get `&accumulationSettings_`.
   - The env override now lives only in the host's `RefreshResolvedLookSettings`, which folds it into the record.
2. Settings Fill derives record -> buffer node from the schema resource registry.
   - `GeneratedSystemStage.h:370-376,444`: `SchemaResourceRegistry::Source(identity)` and `sources_` map (throws if missing).
   - `BuildRenderGraph.cpp:914-933`: `addSettingsBuffer(kind)` finds the transfer row by `SchemaIdentity`, derives the
     node name from the kind, and provides the identity to `settingsResources_` (now also feeds the five other settings
     identities to generated systems).
   - `VulkanGraphApplication.cpp:851`: the string switch is gone; `VulkanGraphApplication.h` drops the six `*SettingsBuffer_` handles.
3. `test_b1_occlusion_ab` asserts generated constants.
   - `test_b1_occlusion_ab.cpp:103-106`: asserts `Push::proxyAabbCount::SIZE/OFFSET` against `sizeof(PushConstants)`; the
     hand `PushConstantsB2` mirror is deleted and the word is written at the generated offset. Root cause of the old red:
     the block is `alignas(16)` (112 B), not 96 B + trailing field.

## Witness (MEASURED; raw logs in `/home/liory/codeman-cases/undertow/reports/sn-vixfollow-evidence/`)
- VIXEN full build: 1297 steps, 0 errors.
- Targeted GPU job: 8/8 (6 GeneratedSystemStage, AccumulationConfigSdiParity, B1OcclusionAbTest which was red/uncompilable).
- ExposureTonemapParity: 32/32.
- RenderGraph label: base (op-rbs2r5 after) 15 failing of 1446; now the same 15 names of 1447 (diff empty). The extra test
  is B1OcclusionAbTest, now compiled and passing. Failing names: EditorDocumentRenderTest x3, AppFlowEditorToggleRenderTest.ToggleThenUndoRestoresRender,
  ProceduralRecipeRenderTest.Render* x10, vixen_text_shader_capture_producer (Timeout) - all pre-existing.
- Captures vs the rbs2 base evidence: Cornell 10/10 PNGs, lookdev 4/4 PNGs, planet C1.png/hits/lighting/meter all byte-identical.
- Comparison base is the op-rbs2r5 evidence (no separate base build).

## R412 checklist (added lines)
No EntityId, no `KindRef<All>`, no explicit `Id` member, no new attribute or declaration form, no ratchet growth,
`Program.Legacy.cs` untouched. Two added `throw std::runtime_error` guards (node: source unset; registry `Source`: identity
missing) are graph-build/host wiring guards.

## SPT DISPOSITION
No SPT task ids were assigned to this lane brief.

## CONSOLIDATION ISSUES
- `SceneRadianceNode` is an alias of `AccumulationHistoryNode`; each instance needs its own `SetSettingsSource` call, which is easy to miss
  (it caused the first capture failures). A generated settings-binding for nodes that read a record would remove the manual wiring.
