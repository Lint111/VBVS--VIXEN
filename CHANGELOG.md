# Changelog

All notable changes to VIXEN will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **Sparse-Mip ESVO LOD** (Inc1 + Inc2) — per-level filtered mip samples on the ESVO so a
  distant/non-resident subtree still shades correctly, plus partial/streamed brick-pool uploads
  gated by screen-space resolvability, frustum containment, and a CPU-side occlusion test against
  already-resident trees. Measured ~170-220x bandwidth reduction at the mechanism's all-or-nothing
  extreme endpoint, and a realistic 3.2x/68.8% reduction on a mixed near/far scene through the live
  residency trigger. See `Vixen-Docs/01-Architecture/Sparse-Mip-ESVO-LOD-{Direction,Inc1-Plan,
  Inc2-Plan}-2026-07.md`.
- **Tiered ESVO — observer-relative addressing, Inc1** — `TierAddress` (a short hop-chain identity
  spanning voxel-cm to galaxy scale across 5 tiers) and a `SkyProjectionNode` that composites
  direction+magnitude sky points over the existing voxel render, evaluated at the address level with
  no ray-marching. First slice of the nested-tree/observer-addressing epic supporting an
  observation-post fleet-detection mechanic; the tier-crossing traversal-restart machinery remains a
  future increment. See `Vixen-Docs/01-Architecture/Tiered-ESVO-{Observer-Addressing-Design,
  Inc1-Plan}-2026-07.md`.
- **Tiered ESVO — tier-crossing traversal, Inc2 (surface-to-orbit)** — `farBit==1` ESVO leaves now
  reference a child tree (`TierRef`/`TierRefTable`, GPU binding 15) and the ray-march shader restarts
  traversal inside the child tree's own frame with a fresh stack (same-physical-scale
  `childScale==1.0` scope), with screen-space LOD and child-residency early-outs that decline the
  crossing to the parent's mip sample. Live-proven on real hardware: a continuous, seamless
  surface-to-orbit zoom through a real tier crossing (LOD handoff observed within 1 tick of the
  hand-computed prediction) including a mid-flight residency transition. N-tier chaining and
  scale-magnified tiers deferred with documented prerequisites (per-child-scale hitT normalization,
  LOD-gate generalization). See `Vixen-Docs/01-Architecture/Tiered-ESVO-{Observer-Addressing-Design,
  Inc2-Plan}-2026-07.md`.
- **Tiered ESVO — scale-magnified + chained crossings + a live true-scale surface-to-orbit flight,
  Inc3** — the tier-crossing mechanism is generalized off the `childScale==1.0` restriction: child
  hit-t is normalized into the parent's world-t unit by `length(childRayDirWorld)` (reduces
  byte-exactly to Inc2's plain addition at unity), and the traversal wrapper becomes a bounded
  `MAX_TIER_HOPS` hop loop (parked chain, one live stack at a time) so a `farBit==1` leaf reached
  *inside* a child tree is followed — enabling T0→T1→T2 chains. Concentric scale-magnification is
  live-proven across `childScale ∈ {1.0, 0.5, 0.25, 0.125}` and a 3-tree chain renders both crossings
  on real hardware. The tier-crossing LOD gate was subsequently rebuilt from first principles
  (superseding the original `>= childScale*scale_exp2` screen-space formula, which shared its one
  coefficient with the ordinary non-crossing gate and could never resolve a true 2^-10 crossing without
  starving the body's own visibility): the gate now compares a genuinely camera-anchored,
  world-unit-correct distance — proven by hand-derivation to compose correctly through *arbitrary* hop
  depth, with the ordinary gate provably untouched (every non-crossing scene renders byte-identical).
  `CameraNode` gained an additive look-target (`PARAM_LOOK_TARGET_*`/`SetLookTargetForTest`, defaults to
  the existing orbit-center so every scene is unaffected when unset) and a translating-position
  capability (`SetPositionForTest`), enabling a camera that can fly a real trajectory through a crossing
  rather than only orbit a fixed center. **The epic's original ask is now live-proven in full**: a
  continuous ground-to-orbit flight through TWO true `childScale=2^-10` tier crossings on one
  trajectory, each genuinely color-attributable (real per-voxel SDF color at its own hand-predicted
  distance, not an LOD-decline placeholder), with zero hand-tuned per-scene constant — dynamically
  correct at any resolution/FOV/scale by construction. `GpuTraversalMirror.h` stays in lockstep (the
  LOD-gate rewrite doesn't need porting — the CPU oracle runs exclusively with LOD disabled, an
  established precedent). See
  `Vixen-Docs/01-Architecture/{Tiered-ESVO-Inc3-Plan,Tiered-ESVO-Observer-Addressing-Design}-2026-07.md`.
- **Sampled Lighting, Inc1 (shadow rays)** — the ESVO march is now factored into a shared
  `TraceWorld`/`TraceWorldShadow` shader seam (`shaders/TraceWorld.glsl`), the primary pass writes a
  per-pixel `HitRecord` (albedo/normal/roughness/hitT/worldPos, SSBO@binding-17), and shading casts a
  real shadow ray per light through `TraceWorldShadow`, gated by a drift-guarded `ShadowConfig`
  `[GpuStruct]` (`enabled`/`raysPerLight`/`maxShadowDistance`/`biasEpsilon`, SSBO@binding-18) — the
  world now occludes its own light for the first time. `ShadowConfig.enabled=0` reproduces the
  pre-shadow render byte-identically, an A/B lever for future regression checks. Measured shadow-ray
  cost through the ESVO traversal: ~240 ns/ray (1080p, real GPU; method and caveats in
  `gate-artifacts/inc1-m5-shadowray-cost.txt`) — the first empirical number future ray-budget
  decisions (ReSTIR ray count, DDGI probe budget, the frame-time split) will be sized from. The
  direct-lighting shading currently runs inline in the march pass rather than as the separate
  `DirectLighting.comp` pass the design describes (`ComputeStageNode`'s 3-hazard-slot cap; tracked as
  a prerequisite for Inc3 ReSTIR, see Known Issues KI-018). See
  `Vixen-Docs/01-Architecture/Sampled-Lighting-{Design,Inc1-Plan}-2026-07.md`.
- **Sampled Lighting, Inc2 (temporal accumulation)** — a persistent (not ring-buffered) history
  storage image (`AccumulationHistoryNode`, binding-20) plus a drift-guarded `AccumulationConfig`
  `[GpuStruct]` (binding-19) blend each frame's shaded result into a converging 1/N EWMA average at
  the accumulate seam in `BodyInstanceRayMarch.comp`, driving variance to zero on a static camera —
  the prerequisite every later stochastic layer (ReSTIR, DDGI, specular) leans on for a presentable
  image. Camera motion no longer forces a whole-frame reset: per-pixel reprojection through the
  previous frame's view-projection matrix (`PrevCameraConfig`, binding-21) plus a three-part
  validation (bounds, motion-magnitude, color-consistency) lets accumulation continue while orbiting,
  falling back to the current frame only where reprojected history fails validation (disocclusions,
  edges). `AccumulationConfig.enabled=0` reproduces the Inc1 baseline byte-identically, held through
  every milestone. Measured cost: ~1.2 ms/frame full-frame delta at 1080p for the static EWMA path
  (~0.6 ns/pixel over the full frame — method, caveats, and signal-quality discussion in
  `gate-artifacts/inc2-m5-accumulation-cost.txt`). Body-motion (rigid instance-transform)
  reprojection is explicitly deferred to a later increment; procedural deformation is out of scope
  entirely. Two known issues filed: KI-020 (the color-consistency reject is sound here but will
  fight Inc3's stochastic sampling — a geometric worldPos/depth reject is now a required Inc3
  prerequisite) and KI-021 (a pre-existing, unrelated `VIXEN_RESIZE_AT_FRAME` mid-run-resize access
  violation, newly surfaced by this program's first resize-exercising gate). See
  `Vixen-Docs/01-Architecture/Sampled-Lighting-{Design,Inc2-Plan}-2026-07.md`.
- **Sampled Lighting, Inc3 (ReSTIR direct illumination)** — unbiased ReSTIR DI for many-light
  scenes, sourced from the scene's own emissive voxels (not an app-authored light list). A new
  scalar-RGB emissive semantic channel rides the existing sparse-mip averaging, and a bounded
  top-down CUT through the emissive mip pyramid (`LightTree.h`) turns a million glowing voxels
  into a handful of coarse light-tree candidates — "a light-tree for free," reusing the LOD
  machinery. Direct lighting now runs as its own `DirectLightingNode`/`DirectLighting.comp`
  compute-graph pass (finally separated from the march, closing KI-018 — the split needed a
  standalone `BlitNode`, a generic WSI-free `IMAGE_WRITE` sync slot, and array-hazard-tracking
  surgery in the RenderGraph auto-sync core along the way), consuming `HitRecord` + a new
  drift-guarded `ReservoirConfig` `[GpuStruct]`. RIS draws weighted candidates from the light-tree
  cut; a per-pixel reservoir (ping-pong buffer, binding 25/26) carries them through temporal reuse
  (reprojected via a new worldPos/depth companion history buffer — closing KI-023, the geometric
  reproject reject that replaces Inc2's color-consistency check, which would otherwise fight
  ReSTIR's own noise) and spatial neighbor reuse (`SpatialReuseShade.comp`, MIS-weighted). CPU-mirror
  tests prove the reservoir math's unbiasedness identities (weight normalization, MIS-combine,
  RIS-converges-to-independent-brute-force-MC) before any GPU rendering; live equal-error-vs-
  brute-force gates hold pre-spatial 0.044% and post-spatial 0.15% relative error. Unbiased weights
  only in this landing — the 35–65× biased mode is scaffolded (`ReservoirConfig.biasedModeEnabled`)
  but not implemented, deliberately deferred and flagged on the design doc's bias ledger, not
  silently dropped. `ReservoirConfig.reservoirEnabled=0` reproduces the pre-ReSTIR render
  byte-identically, held through every milestone. Measured cost: ~5.4 ms/frame full-frame delta at
  1080p on a ≥10³-emissive-voxel scene (method, caveats, and signal-quality discussion in
  `gate-artifacts/inc3-m7-restir-cost.txt`). Also reconciled a sibling program's algorithm change
  (Tiered-ESVO's world-unit-correct tier-crossing LOD gate) into this program's own shader-split
  layout, and fixed a real post-merge regression where a RenderGraph library restructuring
  (per-node OBJECT-lib linkage) silently dropped a shader-source-path compile definition, breaking
  app boot. Spectral (temperature→blackbody) emission, biased-mode ReSTIR, and the denoiser all
  remain deliberate, tracked fast-follows. See
  `Vixen-Docs/01-Architecture/Sampled-Lighting-{Design,Inc3-Plan}-2026-07.md`.

### Fixed
- **CameraNode silently overriding every scene's configured camera** — `ExecuteImpl` recomputed the
  camera position from stale orbit-mode defaults every frame, so every standalone body scene rendered
  with the camera aimed at the old Cornell-box pivot (body-less frames). The configured
  `PARAM_CAMERA_*` pose is now authoritative at rest; orbit engages only on real interaction
  (drag/wheel/WASD), snap-free, with an explicit latch for orbit-configured consumers (editor).
- **Missing `TRANSFER_DST_BIT` on the brick/config GPU buffers** — the first live post-compile
  residency grant surfaced a real `VUID-vkCmdCopyBuffer-dstBuffer-00120`; both buffers now declare
  the usage flag at creation (root-cause fix; sibling sweep confirmed no other affected buffers).
- **Tier-crossing chained-hop hit-t double-count (Inc3 M3)** — the per-hop cumulative direction-length
  factor was multiplied (`*=`) when it should be assigned (`=`), because `childRayDirWorld` already
  compounds all prior hops' scaling; the bug double-counted every hop past the first (a 2-hop chain at
  childScale=0.5 measured 8× where the correct value is 4×). Fixed in shader and CPU mirror; guarded by
  a chained hit-t parity test against the `(1/childScale)^hop` closed form.
- **Tier-crossing child magnification anchored to the wrong point (Inc3 M5)** — every demo placed the
  child at the root cube's shared corner `(1.5,1.5,1.5)` instead of the marked leaf's own octant center,
  making that corner a scale-invariant fixed point: the child collapsed into a one-sided "wedge" that
  barely shrank (~1.24× at childScale=0.25) instead of magnifying concentrically (4×). Fixed with an
  octant-center-aware placement helper; the traversal/remap math was correct and untouched. This also
  reconciled an earlier mis-measurement that had reported a working 3.93× magnification on the broken
  render.
- **CPU traversal-mirror empty child tier-ref table (Inc3 M3)** — the `GpuTraversalMirror.h` oracle did
  not carry each child tree's own tier-ref table, so a *second* chained crossing silently degraded to a
  wrong brick read; fixed by giving each child link its own table slice (the GPU shader was unaffected —
  it indexes one concatenated table by per-config base offset).
- **Tier-crossing LOD gate structurally could not resolve a true-scale crossing (Inc3 M8)** — the gate
  compared a node's own chord-floored `tv_max` (which does not shrink as the camera approaches, only as
  the tree is descended) against a threshold reachable only by shrinking the single `raySizeCoef` field
  shared with the ordinary non-crossing gate — which simultaneously starved the body's own base
  visibility. Root-caused (an octree-depth "fix" was proven algebraically impossible: chord and
  `scale_exp2` both halve per level and cancel identically from the gate) and replaced with a genuinely
  camera-anchored, world-unit-correct distance requiring no shared coefficient and no per-scene
  override; proven correct through arbitrary hop depth and live-verified at the true `childScale=2^-10`
  ratio on real hardware.
- **`VIXEN/binaries/VIXEN.exe` was a stale, build-system-disconnected copy** — the source-tree,
  gitignored `binaries/` directory (which every hand-rolled capture `.bat` script runs by relative path)
  was never refreshed by CMake; only the real build output (`CMAKE_BINARY_DIR/binaries`) was guaranteed
  fresh. A `POST_BUILD` step now mirrors the executable into `VIXEN/binaries/` on every build.
- **`FrameCapture.cpp` stale include after the Profiler-library consolidation** — a leftover
  `#include "Profiler/FrameCapture.h"` broke a clean build once the header moved to
  `application/main/include/`; corrected to the bare path (the only straggler, verified repo-wide).

### Documentation
- Rewrote the repository `README.md` and `VIXEN/README.md` to reflect the pivot from voxel
  ray-tracing research platform to reusable, moddable game render engine (for *Undertow*) with the
  SDF/Recipe/CSG procedural-content codegen system. De-duplicated the two READMEs (root = overview,
  engine = build/layout quick reference).
- Corrected `VIXEN/DOCUMENTATION_INDEX.md` (removed false "Phase K/L COMPLETE" status) and refreshed
  the root `DOCUMENTATION_INDEX.md`; both now point to `Vixen-Docs/` as canonical.
- `libraries/README.md` now documents all 14 libraries (was 6).

### Changed
- Archived finished/superseded docs (July 2026 cleanup): session transcripts, feature proposals,
  MCP-dev notes, and completed Sprint4–6.5 logs moved to `Vixen-Docs/_archive/2026-07/`; root
  planning/review docs and the resolved deletion-incident post-mortems moved to
  `VIXEN/archive/2026-07-cleanup/`.

### Removed
- Deleted loose build spew from the repo root: `bash.exe.stackdump`, stray `comp.spv` / `rchit.spv`.
- Untracked regeneratable SPIR-V build copies under `generated/` and stale Python bytecode; the
  source-referenced SDI headers (`generated/sdi/*.h`) and runtime shader assets (`shaders/*.spv`)
  remain tracked. Added `.gitignore` rules for `*.stackdump`, `generated/*.spv`, and `__pycache__/`.

## [0.1.0] - 2025-11-10

### Overview
First public release of VIXEN - Vulkan Interactive eXample Engine. This release represents the completion of Phase G, featuring a production-quality graph-based rendering architecture with compile-time type safety and comprehensive infrastructure systems.

**Status**: Phase G Complete (SlotRole System & Descriptor Binding Refactor)
**Research**: Phase H (Voxel Infrastructure) in progress on separate branch

### Added

#### Core Architecture
- **Graph-Based Rendering System** - Node-based render graph with directed acyclic graph (DAG) execution
- **Typed Node API** - `TypedNode<Config>` with compile-time `In()`/`Out()` slot validation
- **Resource Variant System** - 29+ Vulkan types with macro-based registry, zero-overhead abstractions
- **Event-Driven Invalidation** - EventBus with cascade recompilation (WindowResize → SwapChainInvalidated → Framebuffer rebuild)
- **Protected API Enforcement** - Nodes use high-level typed API, graph manages low-level wiring

#### Infrastructure Systems
- **Persistent Cache System** - 9 cachers with async save/load (SamplerCacher, ShaderModuleCacher, PipelineCacher, etc.)
- **Testing Framework** - 40% coverage, 10 GoogleTest suites, VS Code integration with LCOV visualization
- **Logging System** - ILoggable interface with LOG_TRACE/DEBUG/INFO/WARNING/ERROR macros
- **Lifecycle Hooks** - 14 total hooks (6 graph phases + 8 node phases) for fine-grained control
- **Multi-Rate Loop System** - Fixed timestep accumulator (per-frame, fixed 60Hz, fixed 120Hz)
- **Frame-in-Flight Synchronization** - CPU-GPU pacing with two-tier sync (fences + semaphores)

#### Shader Management (Phases 0-5)
- **SPIRV Reflection** - Automatic descriptor layout generation from shader reflection
- **SDI Generation** - Type-safe UBO struct definitions with content-hash UUID system
- **Data-Driven Pipelines** - Zero hardcoded shader assumptions, all from reflection
- **Descriptor Automation** - Pool sizing, layout creation, binding from SPIRV metadata
- **Push Constants** - Automatic extraction and propagation
- **Vertex Format Extraction** - Dynamic vertex input from SPIRV reflection

#### Node Catalog (19+ Nodes)
- **Core**: WindowNode, DeviceNode, SwapChainNode, RenderPassNode, FramebufferNode
- **Pipeline**: GraphicsPipelineNode, ComputePipelineNode, DescriptorSetNode
- **Resources**: CommandPoolNode, VertexBufferNode, DepthBufferNode, TextureLoaderNode, ShaderLibraryNode
- **Execution**: GeometryRenderNode, ComputeDispatchNode, PresentNode, FrameSyncNode
- **Utility**: ConstantNode, LoopBridgeNode, BoolOpNode

#### Build System
- **CMake Modular Architecture** - 7 libraries (Logger, VulkanResources, EventBus, ShaderManagement, ResourceManagement, RenderGraph, CashSystem)
- **Build Optimizations** - Ccache/sccache support, precompiled headers, Ninja generator, unity builds
- **Testing Infrastructure** - GoogleTest integration, CTest support, coverage reporting
- **Trimmed Build Mode** - Header-only Vulkan build without full SDK

#### Documentation
- **Memory Bank** - 6 files documenting project context, patterns, progress (~200 pages)
- **Architecture Documentation** - 30+ files covering graph system (~800 pages)
- **Component Documentation** - ShaderManagement, CashSystem, EventBus (~330 pages)
- **Standards** - C++23 coding guidelines, communication style, smart pointer guide
- **Documentation Index** - Comprehensive index of 90+ files organized by topic

### Changed

#### Phase G: SlotRole System & Descriptor Binding Refactor
- **SlotRole Bitwise Flags** - Combined `Dependency | Execute` roles for flexible descriptor binding
- **Deferred Descriptor Binding** - Execute phase instead of Compile phase
- **DescriptorSetNode Generalization** - Removed hardcoded MVP/rotation/UBO logic, reduced from ~230 lines to ~80 lines
- **NodeFlags Enum** - Consolidated state management pattern (replaces scattered bool flags)
- **Per-Frame Descriptor Sets** - Generalized binding infrastructure

#### Previous Phase Completions
- **Phase 0.1-0.7**: Synchronization infrastructure (per-frame resources, frame-in-flight, command buffers, loops, present fences)
- **Phase A**: Persistent cache with lazy deserialization
- **Phase B**: Encapsulation via INodeWiring interface
- **Phase C**: Event processing validation
- **Phase F**: Bundle-first organization refactor

### Technical Details

#### Performance Characteristics
- **Node Capacity**: 100-200 nodes per graph
- **Build Time**: Clean 60-90s, Incremental 5-10s (with optimizations)
- **Cache Performance**: CACHE HIT confirmed for shaders, pipelines, samplers
- **Frame Rate**: Target 60 FPS with 4 frames in flight
- **Test Coverage**: 40% coverage across 10 test suites

#### Platform Support
- **OS**: Windows 10/11 (x64)
- **Compiler**: MSVC (Visual Studio 2022+, C++23 required)
- **Graphics API**: Vulkan 1.4.321.1
- **Build System**: CMake 3.21+

#### Design Patterns
- Typed Node Pattern - Compile-time slot validation
- Resource Variant Pattern - Zero-overhead type safety
- Graph-Owns-Resources Pattern - Clear lifetime management
- EventBus Invalidation Pattern - Decoupled node communication
- Handle-Based Access Pattern - O(1) node lookups
- Cleanup Dependency Pattern - Auto-detected dependency-ordered destruction
- Two-Semaphore Synchronization Pattern - GPU-GPU sync without CPU stalls
- Split SDI Architecture Pattern - Generic interface sharing with shader-specific convenience

### Fixed
- Zero Vulkan validation errors
- Descriptor binding issues (moved from Compile to Execute phase)
- Semaphore indexing per Vulkan validation guide
- MessageType collision bug (DeviceMetadataEvent vs CleanupRequestedMessage)
- SPIRV reflection vertex format extraction (removed hardcoded vec4 bug)
- Pipeline layout variable shadowing bug

### Security
- MIT License
- No sensitive data in repository
- RAII throughout (smart pointers, no raw new/delete)
- Const-correctness on member functions

### Known Limitations
- **Manual Descriptor Setup** - Some nodes still create descriptor layouts manually (automated in Phases 4-5)
- **Single-Threaded Execution** - No wave-based parallelism yet
- **No Memory Aliasing** - No transient resource optimization
- **Virtual Dispatch Overhead** - ~2-5ns per call (acceptable <200 nodes)

### Notes
- **Research Track**: Phase H (Voxel Infrastructure) in progress on `claude/phase-h-voxel-infrastructure` branch
- **Target Platform**: Windows-only acceptable for v0.1
- **Code Quality**: Zero warnings in RenderGraph library, professional codebase quality

---

## Future Roadmap

### Phase H: Voxel Infrastructure (In Progress - 60% Complete)
- Sparse voxel octree data structure
- Procedural scene generation (Cornell Box, Cave, Urban Grid)
- GPU upload integration
- VoxelRayMarch compute shader with DDA traversal

### Phase I-N: Research Execution (May 2026 Target)
- 4 ray tracing/marching pipelines (compute shader, fragment shader, hardware RT, hybrid)
- 180-configuration test matrix
- Automated testing framework
- Performance profiling system
- Academic paper submission

### Long-Term Extensions (August 2026 Target)
- Hybrid RTX surface-skin pipeline
- GigaVoxels streaming architecture
- Extended research (270 configurations)
- Journal publication

---

## Acknowledgments

Based on 24 research papers covering voxel rendering, ray tracing, sparse voxel octrees, and GPU optimization techniques.

### Technologies
- Vulkan - Khronos Group
- SPIRV-Reflect - Shader reflection library
- GoogleTest - Testing framework
- glslang - GLSL to SPIRV compiler

---

**Full documentation**: See [DOCUMENTATION_INDEX.md](DOCUMENTATION_INDEX.md) for complete guide to 90+ documentation files.

**Repository**: https://github.com/lioryaari/VIXEN

**License**: MIT License - Copyright (c) 2025 Lior Yaari
