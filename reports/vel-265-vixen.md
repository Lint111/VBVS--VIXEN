# vel-265-vixen (red-state batch, 5 items)

| id | title | verdict | evidence |
|---|---|---|---|
| T-4640 | GPU timestamp pool hard-coded; CollectResults silently reports 0 for a slot-less stage | FIXED (3d43120b) | The pool was already raised to 32 by T-4637. This commit removes the silent part. `GPUPerformanceLogger` now logs an Error (what: pass name; where: slots used/max; fix: raise `maxQueryConsumers` in DeviceNode.cpp) instead of a bare Warning. It adds `HasQuerySlot()`. `PerfCsvWriter` reports once per column that its 0 means unmeasured. MEASURED: `GPUPerformanceLogger.cpp.o` and `PerfCsvWriter.cpp.o` compile via the queue with no warnings. NOT MEASURED: no GPU run, and no test that forces slot exhaustion. The "hard-coded constant" part is unchanged (32 stays in DeviceNode.cpp); the pool size is not derived from the pass count. |
| T-4659 | frame_targets_schema_check fails against current undertow tool | STALE-ON-BRANCH (laptop `lane-vixenpin`, VIXEN 2cc6097b; not fetchable here, so unverified) | Reproduced on b60a1bd6: `CoarseLeverageKinds.cs:27:68: PIPE005: .Message<TDelivery, TSave>() takes a delivery marker and a save marker`. The cause is a kernel pin older than the undertow sources, not the check. The controller's landable list records the fix as `lane-vixenpin` (pin -> kernel 67d3a5cf, "PIPE005 gone"), which is blocked on `lane-dealineage`. Not redone: re-pinning here would duplicate that lane. |
| T-1148 | Renderer app graph includes the starfield background pass | STALE | `BuildRenderGraph.cpp:841` adds `sky_sphere` (SkySphereNode, T-1138). The scene look turns the starfield on (`PlanetSceneDefinition.h:71`, `SceneLook.h:128`). `test_headless_starfield_graph` does the pixel comparison. Commits 0f53e3f0 (opt-in procedural starfield) and 8f6e990a (scene-declared sky). The test was not run here: its target needs the full VixenApp build, which the PIPE005 red above blocks. |
| T-1133 | Renderer consumes native view snapshot each frame into body instances + HUD | NEEDS-LANE | VIXEN has the seam (`VulkanGraphApplication::SetBodyInstances`, HudView push). The generated `UndertowBodies` view accessor is only used by the builder test; nothing maps its rows to `BodyInstanceGpu`. The host that owns the snapshot is in undertow/launcher (the old `vixen/app` was deleted in T-1333), so the mapping lives in another repo. Brief: in the launcher/undertow host, each frame read `UndertowBodiesSection` rows, build `BodyInstanceGpu` (recipe, position, scale) and call `SetBodyInstances`; witness: overlay instance count == view rows, and a moving row moves. |
| T-1147 | Bodies drawn from the view output | NEEDS-LANE (same as T-1133) | Blocked by T-1133. Also needs a render recipe lookup (star/planet/moon/jovian in render_recipes.utdl, an undertow file) and a capture witness. |

## LANDABLE NOW
VIXEN 3d43120b (compiled only, no GPU test).

## CONSOLIDATION ISSUES
- The VIXEN full build and every `*_check` stay red (PIPE005) until `lane-vixenpin` and `lane-dealineage` land, so nothing in VIXEN can be fully witnessed.
- The query pool size is a constant in DeviceNode.cpp. It should be derived from the registered timed passes, or `AllocateQuerySlot` should fail the build/boot in strict mode.

## SPT DISPOSITION
CLOSE: T-4640 (see caveat above), T-1148 (stale, evidence above). OPEN: T-4659 (pending lane-vixenpin), T-1133, T-1147.
