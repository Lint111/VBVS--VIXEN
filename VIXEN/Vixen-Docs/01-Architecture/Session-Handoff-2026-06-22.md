---
title: Session Handoff — 2026-06-22 (Auto-Sync FrameGraph P3→P5 complete + HUD fix)
aliases: [handoff 2026-06-22, auto-sync done handoff]
tags: [handoff, session, rendergraph, synchronization, AR21, framegraph]
created: 2026-06-22
related:
  - "[[Auto-Sync-FrameGraph-Inc1-Design-2026-06]]"
  - "[[Session-Handoff-2026-06-21]]"
---

# Session Handoff — 2026-06-22

## TL;DR — what's on `main` now (tip `e715baa5`, pushed, build green)

The **Auto-Sync FrameGraph epic (AR#21) is COMPLETE through P5 — Tiers 1+2 are done and merged to `main`.** Plus the standalone app now boots, renders a 3-body scene, composites a HUD, and closes cleanly, all at **0 synchronization-validation errors**.

Everything below is merged to `main` and pushed. All feature branches are retained but fully merged.

## What shipped this session (in order, each merged to main)

| Work | Merge commit | Summary |
|---|---|---|
| **P3** (Tier-1 barrier replay) | `7d28ae52` | `ComputeDispatchNode` replays the baked `barrier2` schedule; `synchronization2` enabled via the **capability graph**; +5 pre-existing app-rot fixes so `VIXEN.exe` boots/renders/closes (ConstantNode self-alloc, instanceCount int32, RmlUi teardown order, UISelectionProvider registration, default 3-body scene seed). |
| **P4** (generic `PassGroupNode`) | `c782a1fc` | A **generic pass-assembly node**: assembles an arbitrary ordered list of compute+graphics passes into ONE command buffer + ONE submit, auto-baking intra-pass barriers by reusing the P2 `BuildScheduleFromTimelines`. Demo `VIXEN_AUTOSYNC_DEMO` (compute→compute→render→present gradient). Found+fixed **2 core engine bugs**: post-compile callback fired before `SetState(Compiled)`; `ComputePipelineNode` keyed pipelines on the descriptor-*interface* hash (same-interface compute shaders aliased one pipeline) → now keyed on SPIR-V code. `MultiDispatchNode` confirmed dead code (left untouched). |
| **P5a** (Tier-2 foundations) | `3a919250` | `FrameSyncNode` gains a per-loop **timeline semaphore** + per-frame **`frameBase`** (output slots; runtime-neutral). Real swapchain `Resource*` wired into `FrameSyncScheduler::Build` → image-barrier/tagging machinery engages. |
| **P5b** (Tier-2 timeline edges) | `d6ba7356` | Submits consume baked timeline edges via **`vkQueueSubmit2`** (deduped signals); generic `ComputeStageNode` + `VIXEN_FANIN_DEMO` proves **multi-submit fan-in** (2 producers→1 consumer, timeline-ONLY, live split render); **CORE scheduler fix**: bake timeline edges only for **declared-`AccessKind`** hazards (removes phantom edges to non-submitting metadata nodes); live composite compute→UI migrated off the binary handoff onto the timeline edge; WSI timeline-law assert. |
| **HUD render fix** | `e715baa5` | `hud.rcss` never sized `<body>` → document collapsed to 0×0 → the `position:absolute` HUD drew off-screen. Fix: `body { position:absolute; inset:0 }`. CSS-only; live-verified. |

P1+P2 (the declarative access model + the pure `FrameSyncScheduler`) were merged before this session (`6258b93e`).

## Auto-sync architecture (as built, on main)

- **declare → schedule → replay.** Nodes/slots declare `AccessKind` (`Core/BarrierTypes.h` → `{stage2, access2, layout}` via `ResolveAccess`); the pure `FrameSyncScheduler` (`Core/FrameSyncScheduler.{h,cpp}`, `FrameSyncSchedule.h`) bakes a structured schedule at compile (`BuildScheduleFromTimelines`): per-`SubmitGroup` `entryBarriers` (Tier-1) + inter-group `SyncEdge`s with `waitEdges`/`signalEdges` (Tier-2) + `timelineValuesPerFrame`; submitting nodes replay at execute.
- **Edge baking rule (P5b):** a timeline `SyncEdge` bakes only when BOTH endpoints carry a declared `AccessKind` (untyped passthrough/metadata accesses are excluded — they were creating phantom edges to non-submitting nodes). One edge per producer→consumer hazard; `timelineOffset = producer.groupId`.
- **Timeline protocol (P5b):** a producer signals its group's value (`groupId + frameBase`) ONCE per submit (dedupe via `std::set` — all of a producer's edges share its offset, so per-edge signaling double-signals → `VUID-VkSubmitInfo2-semaphore-03882`); a consumer waits each producer's value. `frameBase` (from `FrameSyncNode::TIMELINE_FRAME_BASE`) advances by `timelineValuesPerFrame` once/frame BEFORE consumers read it, so all nodes in a frame resolve the same value (else deadlock). WSI acquire/present stay **binary** (timeline is illegal there — asserted).
- **Hybrid:** intra-group = `vkCmdPipelineBarrier2`; inter-group internal = one timeline semaphore per loop; swapchain-adjacent = binary `imageAvailable`/`renderComplete`.
- **Live composite path** (the standalone `VIXEN.exe` default graph): `ComputeDispatchNode` (BodyInstanceRayMarch body-octree → swapchain, leaves it GENERAL) → `UIRenderNode` (composite HUD, LOAD GENERAL → PRESENT_SRC) → `PresentNode`. compute→UI is now ordered by the **timeline edge** (the `renderComplete→compositeWait` *connection* is kept purely as a topology ordering edge — its binary semaphore is inert; removing the connection inverts the topo sort).

## How to run / verify (WSL → Windows)

- **Build:** `cmd.exe /c _ninja_preset_build.bat` FOREGROUND (`timeout: 600000`; exceeds the 120s Bash default). Test exes under `build-ninja/libraries/RenderGraph/tests/`.
- **Demos** (env vars must be set INSIDE cmd.exe; WSL bash env doesn't reach the .exe):
  - `cmd.exe /c "set VIXEN_AUTOSYNC_DEMO=1&& set VIXEN_VULKAN_VALIDATION=1&& C:\cpp\VBVS--VIXEN\VIXEN\binaries\VIXEN.exe"` → P4 compute→compute→render gradient.
  - `cmd.exe /c "set VIXEN_FANIN_DEMO=1&& set VIXEN_VULKAN_VALIDATION=1&& ...VIXEN.exe"` → P5b multi-submit fan-in split (left=A, right=B).
  - No env (default) → live composite: 3-body scene + HUD overlay.
  - `taskkill /F /IM VIXEN.exe` to reap.
- **Default app does ~90s GaiaVoxelWorld gen** before the render loop — be patient on live runs (~110s).
- **Screenshots from WSL:** `CopyFromScreen` via PowerShell after foregrounding the VIXEN window (PrintWindow returns black for the Vulkan swapchain). The window may receive a WSLg `WindowCloseEvent` ~37s in (environmental).

## Hard-won learnings (apply next session)

1. **The live run is the authoritative gate for GPU/render/sync work** — memory `[[live-verification-authoritative-for-gpu-work]]`. Multiple thorough Opus static reviews PASSED bugs that only a live run caught this session: duplicate timeline signals (VUID-03882), phantom scheduler edges → present stall, a topo-order inversion → layout VUIDs, a compute-pipeline interface-hash aliasing. **Instrument-and-run to root-cause; don't trust static reasoning for these.** A `frameBase`/value mismatch is a HANG, invisible to build/unit/static review.
2. **Build-env caution:** a debug agent had flipped `ENABLE_COVERAGE=ON` in the local `build-ninja` CMake cache; `--coverage` corrupts objects on this MSVC toolchain (→ `test_timer`/`test_scene_generators` link-fail with "corrupt COFF"). If you see that, reconfigure `cmake --preset vixen-ninja -DENABLE_COVERAGE=OFF` + full rebuild. Repo default is OFF (not committed).
3. **Test gap:** the UI smoke test only checks the RmlUi doc parses, never layout — which is why the 0×0-body HUD bug shipped. Consider a layout assertion if UI work continues.

## Open follow-ups (all OPTIONAL, all separate from the now-complete auto-sync epic)

- **Render-quality:** shell-octree **surface artifacts/speckling** in `BodyInstanceRayMarch.comp` (visible on the spheres) — a shader-quality issue. Camera-config rot (orbit defaults `{5,5,5}`/dist 30 disconnected from `PARAM_CAMERA (64,64,300)` — frames OK empirically).
- **HUD data:** `SetHudData`/`SetHudView` are never called by the app (only `test_ui_hud_smoke`), so the HUD shows default values ("tick 0 · bodies 0"); wire a host to feed live data if a functional HUD is wanted.
- **P6 polish (optional):** more fan-in scenarios / generalize multi-submit composition beyond the smoke demo. Tier-3 inter-loop cadence sync is still future (cross-loop edges are modeled, not resolved — skip-deadlock hazard documented in the design spec).

## Pointers
- Epic memory: `[[auto-sync-framegraph-epic]]`. Design spec + per-phase plans (P1, P2, P3, P4, P5a, P5b) in `VIXEN/Vixen-Docs/01-Architecture/Auto-Sync-FrameGraph-Inc1-*`. Each plan carries a Milestone Map + Progress Log (durable resume memory).
- User rules in play: `[[prefer-pure-fully-correct-solutions]]` (no band-aids), `[[live-verification-authoritative-for-gpu-work]]`, `[[offer-to-bless-needless-permission-prompts]]`.
- Execution model used: the **post-brainstorm-context-manager** milestone pipeline (fresh implementer + Opus validator per milestone; controller thin; live gates controller-driven).
