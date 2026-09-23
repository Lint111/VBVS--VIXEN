#!/usr/bin/env bash
set -euo pipefail

SPT_SCOPE="/home/liory/projects/architecture-federation/workspaces/yeroket-stack/.spt"
SPT_CLI="/home/liory/projects/SPT/dist/src/cli.js"

spt() {
  node "$SPT_CLI" "$@" --scope "$SPT_SCOPE"
}

add_id() {
  local output id
  output="$(spt add "$@")"
  printf '%s\n' "$output" >&2
  id="$(printf '%s\n' "$output" | awk 'NR == 1 { print $1 }')"
  if [[ ! "$id" =~ ^[A-Z]-[0-9]+$ ]]; then
    printf 'Could not read an SPT id from add output: %s\n' "$output" >&2
    return 1
  fi
  printf '%s\n' "$id"
}

add_closed_task() {
  local title="$1" sha="$2" id
  shift 2
  id="$(add_id task "$title" --repo vixen "$@")"
  spt close "$id" --commit "vixen:$sha"
}

# Attach the audit and governing cross-repository evidence to the existing audit item.
spt link T-0525 file vixen:reports/vixendocs.md --reason "Verified VIXEN documentation audit and import list."
spt link T-0525 file vixen:reports/vixendocs-apply.sh --reason "Read-only SPT transfer script produced by the audit."
spt link T-0525 file undertow:docs/standing-rules.md --reason "R106/R106.1/R155/R155.1/R162.4 govern current VIXEN classification."
spt link T-0525 file undertow:reports/vixensplit.md --reason "Current VIXEN framework/application split measurement."
spt link S-0007 file vixen:VIXEN/Vixen-Docs/04-Development/Known-Issues.md --reason "Original source for the 27 imported VIXEN issue tasks."
spt link T-0001 file undertow:perf/e7-t1-stencil-report.md --reason "KI-051 was discovered during the E7 stencil re-gate."
spt link T-0002 file undertow:perf/e4-t1-virtual-census-report.md --reason "KI-050 was discovered during the E4 virtual-source census."
spt link T-0003 file undertow:perf/e5-t1-mip-regime-report.md --reason "KI-049 references the E5 frozen boot matrix."
spt link T-0455 file undertow:reports/vixensplit.md --reason "Source report for the measured VIXEN framework/application split."
spt link T-0458 file undertow:docs/standing-rules.md --reason "R155/R155.1 govern the shared-tag provider pin tracked by this task."

# Deep-Field items that are live after the delivered stencil and wholesale waves.
deepfield_epoch="$(add_id epoch "VIXEN Deep-Field residency and compositing follow-ons" \
  --repo vixen \
  --doc vixen:VIXEN/Vixen-Docs/01-Architecture/Deep-Field-Mip-Accessor-Policy-2026-08.md \
  --doc vixen:VIXEN/Vixen-Docs/Deep-Field-Residency-Unification-2026-08.md)"

tile_story="$(add_id story "E11 sparse-tile payoff and graduation gate" \
  --repo vixen --parent "$deepfield_epoch" \
  --doc vixen:VIXEN/Vixen-Docs/Deep-Field-Policy-Stencil-Grouping-2026-08.md)"
add_id task "Measure tile reduction on a sparse tile-uniform acceptance scene" \
  --repo vixen --parent "$tile_story" \
  --doc vixen:VIXEN/Vixen-Docs/Deep-Field-Policy-Stencil-Grouping-2026-08.md \
  --file vixen:VIXEN/shaders/BodyInstanceRayMarch.comp \
  --file undertow:perf/e11-t1-stencil-perf-report.md \
  --body "E11 is implemented and remains opt-in after a 63-67% dense-scene slowdown. The current rig lacks the sparse tile-uniform scene needed to establish payoff and decide whether to keep the feature gated."

cosmic_story="$(add_id story "Regime-3 cosmic quality and mip consumption" \
  --repo vixen --parent "$deepfield_epoch" \
  --doc vixen:VIXEN/Vixen-Docs/01-Architecture/Deep-Field-Mip-Accessor-Policy-2026-08.md)"
add_id task "Probe walkCov and implement cross-instance residual-transmittance compositing" \
  --repo vixen --parent "$cosmic_story" \
  --doc vixen:VIXEN/Vixen-Docs/01-Architecture/Deep-Field-Mip-Accessor-Policy-2026-08.md \
  --file vixen:VIXEN/shaders/SceneBindings.glsl \
  --file undertow:docs/plans/2026-08-04-wavefront-recipe-shading.md \
  --file undertow:docs/superpowers/specs/2026-08-08-deep-field-mip-policy-design.md \
  --body "The current per-instance cosmic walk commits an opaque hit and discards residual transmittance. Audit walkCov/walkSampledLevel before relying on the coverage signal; the nebula-over-galaxy composite remains the acceptance bar."
add_id task "Consume baked anisotropic coarse mips and evaluate sky-sphere cache/hysteresis" \
  --repo vixen --parent "$cosmic_story" \
  --doc vixen:VIXEN/Vixen-Docs/01-Architecture/Deep-Field-Mip-Accessor-Policy-2026-08.md \
  --file vixen:VIXEN/libraries/SVO/include/MipAnisoPool.h \
  --file undertow:docs/superpowers/specs/2026-08-08-deep-field-mip-policy-design.md \
  --body "Anisotropic mip data is baked, but the regime-3 traversal does not yet consume it. The source document also lists sky-sphere caching and hysteresis as follow-ons."

residency_story="$(add_id story "Residency accounting falsification probe" \
  --repo vixen --parent "$deepfield_epoch" \
  --doc vixen:VIXEN/Vixen-Docs/Deep-Field-Residency-Unification-2026-08.md)"
add_id task "Measure long-session upload growth for repeated residency-boundary crossings" \
  --repo vixen --parent "$residency_story" \
  --doc vixen:VIXEN/Vixen-Docs/Deep-Field-Residency-Unification-2026-08.md \
  --file vixen:VIXEN/libraries/SVO/include/ResidencyTrigger.h \
  --file vixen:VIXEN/application/main/include/PerfCsvWriter.h \
  --body "The shared classifier and byte counters are present. The design's repeated frustum/resolvability-boundary long-session growth probe remains unevidenced; do not introduce an LRU until this measurement establishes unbounded growth."

# The VIXEN multi-channel plan is active, with a concrete cross-repository contract failure.
recipe_epoch="$(add_id epoch "VIXEN domain-agnostic multi-channel recipe output" \
  --repo vixen \
  --doc vixen:VIXEN/Vixen-Docs/01-Architecture/Domain-Agnostic-MultiChannel-Recipe-Output-Direction-2026-07.md \
  --doc vixen:VIXEN/Vixen-Docs/01-Architecture/Domain-Agnostic-MultiChannel-Recipe-Output-Plan-2026-07.md)"
recipe_story="$(add_id story "VIXEN consumer schema and generator verification" \
  --repo vixen --parent "$recipe_epoch" \
  --doc vixen:VIXEN/Vixen-Docs/01-Architecture/Domain-Agnostic-MultiChannel-Recipe-Output-Plan-2026-07.md)"
add_id task "Align recipe-lowering extension declarations with the current kernel parser" \
  --repo vixen --parent "$recipe_story" \
  --doc vixen:VIXEN/Vixen-Docs/01-Architecture/Domain-Agnostic-MultiChannel-Recipe-Output-Plan-2026-07.md \
  --file vixen:VIXEN/codegen/recipe-lowering.extensions \
  --file yeroket:Packages/com.yeroket.utility.kernel-framework/SourceGenerator~/Transpiler/RecipeLoweringModel.cs \
  --file yeroket:Packages/com.utility.graph-framework/Runtime/VM/SDFInstruction.cs \
  --file yeroket:Packages/com.utility.sdf/Runtime/Kernels/SdfCoreKernels.cs \
  --body "The unchanged VIXEN declarations have eight fields; the current kernel parser requires eleven, so recipe_simd_check exits before output comparison. Decide the schema owner/compatibility rule, update the appropriate source, then rerun the queued check."
add_id task "Reconcile the Increment 1 completion claim and deferred Unity consumer gate" \
  --repo vixen --parent "$recipe_story" \
  --doc vixen:VIXEN/Vixen-Docs/01-Architecture/Domain-Agnostic-MultiChannel-Recipe-Output-Direction-2026-07.md \
  --doc vixen:VIXEN/Vixen-Docs/01-Architecture/Domain-Agnostic-MultiChannel-Recipe-Output-Plan-2026-07.md \
  --file yeroket:Packages/com.yeroket.utility.kernel-framework/CodegenTool~/Program.cs \
  --file yeroket:Packages/com.yeroket.utility.kernel-framework/SourceGenerator~/Transpiler/RecipeLoweringModel.cs \
  --body "The direction document still says pre-plan while the plan records Increment 1 complete at 6fa9cb2c; that SHA is absent from the current Yeroket object database. Re-establish the landed state and the deferred Unity consumer check before promoting later increments."

# Implemented items not yet represented in the SPT.
add_closed_task "AppFlow native framework Inc1/Inc2/Inc2b and scripted editor undo/redo" \
  d4ce37033cc5b821817689fc09b12613e0f8647a \
  --cites D-0315 \
  --doc vixen:VIXEN/Vixen-Docs/01-Architecture/AppFlow-Framework-Inc1-Plan-2026-07.md \
  --doc vixen:VIXEN/Vixen-Docs/01-Architecture/AppFlow-Framework-Inc2-Plan-2026-07.md \
  --doc vixen:VIXEN/Vixen-Docs/01-Architecture/AppFlow-Framework-Inc2b-Plan-2026-07.md \
  --file vixen:VIXEN/libraries/AppFlow/include/AppFlowRuntime.h \
  --file yeroket:Packages/com.yeroket.utility.kernel-framework/CodegenTool~/Program.cs \
  --body "Native AppFlow Inc1/2/2b code and the scripted windowed editor gate are present. Inc3 selector/editor-mode and managed-host work stays deferred under the current VIXEN split."

add_closed_task "E6-T1 shared CellFootprintRegime classifier and CPU residency twin" \
  e335130dcbceebf683f05d4008f35aead12d24d3 \
  --alias E6-T1 \
  --doc vixen:VIXEN/Vixen-Docs/Deep-Field-Residency-Unification-2026-08.md \
  --file vixen:VIXEN/libraries/SVO/include/CellFootprintRegime.h \
  --file vixen:VIXEN/shaders/SceneBindings.glsl

add_closed_task "E7-T1 policy stencil and E8-T1 multirung far-field fixture" \
  188f5cb91a9c1bcfb486aa40cbdf30aff53e4adb \
  --alias E7-T1 \
  --doc vixen:VIXEN/Vixen-Docs/00-Index/Quick-Lookup.md \
  --file vixen:VIXEN/shaders/SceneBindings.glsl \
  --file undertow:perf/e7-t1-stencil-report.md \
  --file undertow:perf/e8-t1-multirung-report.md \
  --body "E8's far-field capture requires VIXEN_COMPOSED_TRAVERSAL together with the RTQuery traversal gate."

add_closed_task "E9/E10 far-field level targeting and orbital structure fixture" \
  4b7195e9740a9237147a2be6b1e851f4bec25272 \
  --alias E9-E10 \
  --doc vixen:VIXEN/Vixen-Docs/00-Index/Quick-Lookup.md \
  --file vixen:VIXEN/shaders/TraceWorld.glsl \
  --file undertow:perf/e9-t1-level-targeting-report.md \
  --file undertow:perf/e10-t1-orbital-fixture-report.md \
  --file undertow:perf/e10-t2-rung-calibration-report.md

add_closed_task "E11-T1 policy-stencil tile reduction and evaluator skip" \
  c12eaeba82fc986731575149bbaf9e3cf92e8f2b \
  --alias E11-T1 \
  --doc vixen:VIXEN/Vixen-Docs/Deep-Field-Policy-Stencil-Grouping-2026-08.md \
  --file vixen:VIXEN/shaders/BodyInstanceRayMarch.comp \
  --file vixen:VIXEN/shaders/ShadowVisibilityWave.comp \
  --file undertow:perf/e11-t1-stencil-perf-report.md \
  --body "Implemented behind VIXEN_POLICY_STENCIL_TILES. The dense Cornell measurement is negative (+63-67% frame time); the sparse-scene payoff remains an open task."

add_closed_task "Resolve KI-052 policy-stencil tile-buffer RAW hazard (E12-T1)" \
  868298e2e9cb2d9e6e827d53ddae69056b744b6b \
  --alias KI-052 \
  --doc vixen:VIXEN/Vixen-Docs/04-Development/Known-Issues.md \
  --file vixen:VIXEN/libraries/RenderGraph/include/Data/Nodes/ComputeDispatchNodeConfig.h \
  --file vixen:VIXEN/application/main/source/graph/BuildRenderGraph.cpp \
  --file undertow:perf/e12-t1-ki052-report.md

add_closed_task "E21-S1 reserve and populate wholesale channel-pool capacity" \
  231e272ec94c1d7eb8a1be466167b40e7655ad9f \
  --alias E21-S1 \
  --doc vixen:VIXEN/Vixen-Docs/Deep-Field-Wholesale-Admission-2026-08.md \
  --file vixen:VIXEN/libraries/SVO/include/WholesaleAvailability.h

add_closed_task "E22-S2 atomically admit channel pool and brick lookup with reuse ledger" \
  73f202440cc45f265a6088efe8e2c0d3393d5544 \
  --alias E22-S2 \
  --doc vixen:VIXEN/Vixen-Docs/Deep-Field-Wholesale-Admission-2026-08.md \
  --file vixen:VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp

add_closed_task "E23-S3 suppress fine-byte upload on mip-only wholesale legs" \
  27828493de9f3ac2ba630e2080c0e8d097d611f6 \
  --alias E23-S3 \
  --doc vixen:VIXEN/Vixen-Docs/Deep-Field-Wholesale-Admission-2026-08.md \
  --file vixen:VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp

add_closed_task "E24-S4 gate tier-reference and occupancy payload readiness" \
  c780e70584fbdb28dba48898a672ebfb132170bb \
  --alias E24-S4 \
  --doc vixen:VIXEN/Vixen-Docs/Deep-Field-Wholesale-Admission-2026-08.md \
  --file vixen:VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp

add_closed_task "E25-S5 wholesale capacity arena and placeholder-backed allocation reduction" \
  3334cd4ed6a45b277ed62d0ab9b50430fa88d95a \
  --alias E25-S5 \
  --doc vixen:VIXEN/Vixen-Docs/Deep-Field-Wholesale-Admission-2026-08.md \
  --file vixen:VIXEN/libraries/RenderGraph/src/Nodes/BodyOctreeSceneNode.cpp \
  --body "The delivered arena path reports a 31.5% allocation reduction."

add_closed_task "HDR1 scene-linear radiance/history seam and exposure tonemap witness" \
  bd159652a734dae4c754f130c36717c6df6456ab \
  --alias HDR1 \
  --doc vixen:VIXEN/Vixen-Docs/Deep-Field-HDR-Exposure-2026-08.md \
  --file vixen:VIXEN/shaders/ExposureTonemap.comp \
  --file vixen:VIXEN/libraries/RenderGraph/include/Nodes/SceneRadianceNode.h

add_closed_task "HDR2 deterministic GPU exposure meter" \
  f92761bb5cb02d6deeb013f27ff0113499dcaa87 \
  --alias HDR2 \
  --doc vixen:VIXEN/Vixen-Docs/Deep-Field-HDR-Exposure-2026-08.md \
  --file vixen:VIXEN/shaders/ExposureTonemap.comp \
  --file vixen:VIXEN/libraries/RenderGraph/include/Nodes/ExposureMeterNode.h

add_closed_task "September Main-Wave and voxel materialization reconciliation" \
  e335130dcbceebf683f05d4008f35aead12d24d3 \
  --doc vixen:VIXEN/Vixen-Docs/01-Architecture/Main-Wave-Reconciliation-2026-09.md \
  --file vixen:VIXEN/application/main/source/graph/BuildRenderGraph.cpp \
  --file yeroket:Packages/com.yeroket.utility.kernel-framework/CodegenTool~/Program.cs \
  --file undertow:vixen/codegen/CMakeLists.txt \
  --file undertow:vixen/render/Generated/ViewSectionEnum.g.h

add_closed_task "SIMD4 recipe materialization with Yeroket kernel emitter" \
  5f8dd399787dd86b336b0d29a8e9c1bc1075dc21 \
  --doc vixen:VIXEN/Vixen-Docs/01-Architecture/Simd-Materialization-2026-09.md \
  --file vixen:VIXEN/libraries/SVO/include/Recipe/generated/RecipeSimd.g.hpp \
  --file yeroket:Packages/com.yeroket.utility.kernel-framework/SourceGenerator~/Transpiler/RecipeSimdEmitter.cs \
  --file yeroket:Packages/com.yeroket.utility.kernel-framework/CodegenTool~/Program.cs \
  --file yeroket:Packages/com.utility.graph-framework/Runtime/VM/SDFInstruction.cs \
  --file yeroket:Packages/com.utility.sdf/Runtime/Kernels/SdfCoreKernels.cs \
  --body "Yeroket kernel commits 939c2880 and fdfed89f are separately verified ancestors of Yeroket origin/main."

add_closed_task "CPU-first voxel mutation replacement and multicore owned-page assembly" \
  fb6b1590dcccb859b0152fdbb00bf106cdb2e65b \
  --doc vixen:VIXEN/Vixen-Docs/01-Architecture/Voxel-Mutation-Replacement-2026-09.md \
  --file vixen:VIXEN/libraries/GaiaVoxelWorld/include/GaiaVoxelWorld.h \
  --file vixen:VIXEN/libraries/GaiaVoxelWorld/src/GaiaVoxelWorld.cpp \
  --body "GPU backend and paged-pool generation swap remain follow-ons. Keep KI-027/T-0017 open because the compatibility VoxelInjector remains."

spt verify
