# ===========================================================================
# Critical Nodes Tests - Priority 3
# ===========================================================================
# These test suites validate critical node classes (infrastructure + sync):
# - DeviceNode: Vulkan device initialization, queue families
# - WindowNode: Window creation, surface management
# - CommandPoolNode: Command pool creation, buffer allocation
# - SwapChainNode: Swapchain creation, image acquisition, present modes
# - FrameSyncNode: Fences and semaphores for frame synchronization
#
# Unit Tests: Configuration validation, slot metadata, type checking
# Integration Tests: Actual Vulkan resource creation (requires full SDK)
#
# Compatible with VULKAN_TRIMMED_BUILD for unit tests.
# Integration tests require full Vulkan SDK.
#
# ===========================================================================
# Milestone 2 PDB consolidation (2026-07-14): 41 targets grouped into 15
# executables (10 merged groups + 5 kept standalone). See
# Vixen-Docs/04-Development/RenderGraph-Test-PDB-Consolidation-Plan-2026-07.md
# for the full grouping rationale and collision analysis. Every merged
# group's constituent files were individually verified for: no file-scope
# (non-anonymous-namespace) struct/symbol collisions, at most one hand-rolled
# main()/RUN_ALL_TESTS() among the group's files (all bare gtest_main), and
# identical POST_BUILD/custom-command/compile-definition requirements (a
# strict, cheap-to-apply union where a subset of the group needs an extra
# conditional lib/define). gtest_discover_tests() DISCOVERY_MODE/
# DISCOVERY_TIMEOUT is preserved unchanged on every resulting group that
# needs it.
# ===========================================================================

# Common libraries for RenderGraph tests. Start with GoogleTest and RenderGraph
set(RENDERGRAPH_TEST_COMMON_LIBS
    GTest::gtest_main
    RenderGraph
)

# Optionally append other known dependencies if their targets exist in the build
if(TARGET VulkanResources)
    list(APPEND RENDERGRAPH_TEST_COMMON_LIBS VulkanResources)
endif()
if(TARGET GaiaVoxelWorld)
    list(APPEND RENDERGRAPH_TEST_COMMON_LIBS GaiaVoxelWorld)
endif()
if(TARGET Logger)
    list(APPEND RENDERGRAPH_TEST_COMMON_LIBS Logger)
endif()
if(TARGET EventBus)
    list(APPEND RENDERGRAPH_TEST_COMMON_LIBS EventBus)
endif()
if(TARGET ResourceManagement)
    list(APPEND RENDERGRAPH_TEST_COMMON_LIBS ResourceManagement)
endif()
if(TARGET ShaderManagement)
    list(APPEND RENDERGRAPH_TEST_COMMON_LIBS ShaderManagement)
endif()

# ===========================================================================
# Group 1: test_rendergraph_criticalnodes_infra1 — bare link surface, default
# discovery. DeviceNode / WindowNode / CommandPoolNode / SwapChainNode /
# FrameSyncNode / RenderTargetNode / ComputeDispatchNode / BlendMode.
# ===========================================================================
add_executable(test_rendergraph_criticalnodes_infra1
    Nodes/test_device_node.cpp
    Nodes/test_window_node.cpp
    Nodes/test_command_pool_node.cpp
    Nodes/test_swap_chain_node.cpp
    Nodes/test_frame_sync_node.cpp
    Nodes/test_render_target_node.cpp
    Nodes/test_compute_dispatch_node.cpp
    Nodes/test_blit_node.cpp
    Nodes/test_blend_mode.cpp
)
target_link_libraries(test_rendergraph_criticalnodes_infra1 PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
set_target_properties(test_rendergraph_criticalnodes_infra1 PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_rendergraph_criticalnodes_infra1)
message(STATUS "[RenderGraph Tests] Added: test_rendergraph_criticalnodes_infra1 (merged: device/window/command_pool/swap_chain/frame_sync/render_target/compute_dispatch/blit/blend_mode)")

# ===========================================================================
# Group 2: test_rendergraph_criticalnodes_infra2 — bare link surface plus two
# cheap conditional unions (AccelerationStructureNode's optional CashSystem
# link, BodyOctreeLifetime's optional SVO link + VIXEN_WSL_DZN_ICD define —
# both no-ops when their target/var is absent, harmless to apply to the
# whole group). Default discovery.
# ===========================================================================
add_executable(test_rendergraph_criticalnodes_infra2
    Nodes/test_pick_ray.cpp
    Nodes/test_instance_buffer_node.cpp
    Nodes/test_pick_id_target_node.cpp
    Nodes/test_dynamic_instance_buffer_node.cpp
    Nodes/test_mvp_uniform_node.cpp
    Nodes/test_acceleration_structure_node.cpp
    Nodes/test_body_octree_lifetime.cpp
)
target_link_libraries(test_rendergraph_criticalnodes_infra2 PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
if(TARGET CashSystem)
    target_link_libraries(test_rendergraph_criticalnodes_infra2 PRIVATE CashSystem)
endif()
if(TARGET SVO)
    target_link_libraries(test_rendergraph_criticalnodes_infra2 PRIVATE SVO)
endif()
if(VIXEN_WSL_DZN_ICD)
    target_compile_definitions(test_rendergraph_criticalnodes_infra2 PRIVATE VIXEN_WSL_DZN_ICD="${VIXEN_WSL_DZN_ICD}")
endif()
set_target_properties(test_rendergraph_criticalnodes_infra2 PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_rendergraph_criticalnodes_infra2)
message(STATUS "[RenderGraph Tests] Added: test_rendergraph_criticalnodes_infra2 (merged: pick_ray/instance_buffer/pick_id_target/dynamic_instance_buffer/mvp_uniform/acceleration_structure/body_octree_lifetime)")

# ===========================================================================
# Group 3 + 4: PushConstantGathererNode / DescriptorResourceGathererNode each
# hand-roll their own int main()+RUN_ALL_TESTS() — at most one such file per
# binary (a second would be LNK2005 duplicate-main, the exact collision
# Milestone 1 found). Paired each with one plain SelectionSet/SelectionResolve
# unit-test file (no main() of their own, no risky file-scope symbols) rather
# than growing Group 1/2 with the extra ShaderManagementTestFixtures lib.
# ===========================================================================
add_executable(test_rendergraph_criticalnodes_pushconstant
    Nodes/test_push_constant_gatherer_node.cpp
    Selection/test_selection_set.cpp
)
target_link_libraries(test_rendergraph_criticalnodes_pushconstant PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS} ShaderManagementTestFixtures)
set_target_properties(test_rendergraph_criticalnodes_pushconstant PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_rendergraph_criticalnodes_pushconstant)
message(STATUS "[RenderGraph Tests] Added: test_rendergraph_criticalnodes_pushconstant (merged: push_constant_gatherer_node/selection_set)")

add_executable(test_rendergraph_criticalnodes_descriptorgatherer
    Nodes/test_descriptor_resource_gatherer_node.cpp
    Selection/test_selection_resolve.cpp
)
target_link_libraries(test_rendergraph_criticalnodes_descriptorgatherer PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS} ShaderManagementTestFixtures)
set_target_properties(test_rendergraph_criticalnodes_descriptorgatherer PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_rendergraph_criticalnodes_descriptorgatherer)
message(STATUS "[RenderGraph Tests] Added: test_rendergraph_criticalnodes_descriptorgatherer (merged: descriptor_resource_gatherer_node/selection_resolve)")

# ===========================================================================
# BodyInstanceRayMarch.comp REAL-SHADER render-to-PNG test.
# ===========================================================================
# Compiles the SHIPPED ray-march compute shader to SPIR-V at build time with the
# bundled glslc, then renders the SP2 body scene through it (Dozen preferred, lavapipe
# fallback — see test_body_octree_lifetime.cpp's file header) and dumps
# /tmp/glsl_shader_near.png. Same device-selection + validation-layer contract as
# test_body_octree_lifetime (an unrecognized device is hard-asserted against before
# any vkQueueSubmit). The PNG is directly comparable to the CPU castRay reference
# (cpu_body_render NEAR view) to settle the brick-crack Q.
# NOTE: do NOT gate on if(TARGET SVO) — at the point tests/CMakeLists.txt includes
# this file the SVO target is not yet visible in this directory scope (subdirectory
# ordering). SVO symbols (ShellOctree/BodyInstanceGpu, header-only + lib) reach this
# target transitively via RenderGraph in RENDERGRAPH_TEST_COMMON_LIBS, exactly like
# test_body_octree_lifetime above (whose own `if(TARGET SVO)` link is likewise a no-op).

# --- Compile BodyInstanceRayMarch.comp -> SPIR-V with an environment-appropriate glslc ---
# VIXEN_SHADER_SOURCE_DIR is <VIXEN>/shaders; the auto-provisioned SDK sits beside it.
# These are GPU-render tests that need a glslc RUNNABLE ON THE CURRENT PLATFORM. The
# auto-provisioned LunarG SDK is a LINUX build — its bin/glslc is an ELF binary with NO
# extension. On a Windows/MSVC configure the WSL-provisioned tree is still visible (shared
# /mnt/c checkout), and cmd.exe cannot execute a Linux ELF ("... is not recognized ...").
#
# Two traps this resolution avoids:
#   1. A bare `if(EXISTS bin/glslc)` gate: the Linux ELF file EXISTS on Windows, so the gate
#      wrongly passes and the whole ninja build fails at the exec step.
#   2. `find_program(NAMES glslc)` on Windows still MATCHES the extensionless Linux ELF (Windows
#      find_program tries the exact name too, not only .exe), so it picks the wrong-OS binary.
# Fix: make the search OS-aware. On Windows require glslc.exe and look in a real system SDK
# (VULKAN_SDK / PATH) FIRST — the WSL Linux tree can never supply a .exe. On Linux use the
# extensionless glslc from the provisioned tree. Then gate on whether a platform-runnable glslc
# was actually found, so the tests build+run wherever one exists and cleanly skip where none does.
set(_brm_shader_dir "${VIXEN_SHADER_SOURCE_DIR}")
if(WIN32)
    # Windows: only a .exe is runnable. Prefer a system SDK; never the WSL-provisioned ELF.
    set(_glslc_names glslc.exe)
    set(_glslc_hints "")
    if(DEFINED ENV{VULKAN_SDK})
        list(APPEND _glslc_hints "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
    endif()
else()
    # Linux/WSL: the auto-provisioned SDK's extensionless glslc (versioned via ProvisionVulkan.cmake,
    # with a legacy relative fallback when the provisioning vars are unset), then a system SDK.
    set(_glslc_names glslc)
    set(_glslc_hints "")
    if(DEFINED VIXEN_VULKAN_CACHE_DIR AND DEFINED VIXEN_VULKAN_SDK_VERSION)
        list(APPEND _glslc_hints "${VIXEN_VULKAN_CACHE_DIR}/${VIXEN_VULKAN_SDK_VERSION}/x86_64/bin")
    endif()
    list(APPEND _glslc_hints "${_brm_shader_dir}/../.vulkan-sdk/1.4.350.1/x86_64/bin")
    if(DEFINED ENV{VULKAN_SDK})
        list(APPEND _glslc_hints "$ENV{VULKAN_SDK}/bin")
    endif()
endif()
find_program(VIXEN_GLSLC NAMES ${_glslc_names} HINTS ${_glslc_hints})
if(VIXEN_GLSLC)
set(_brm_src "${_brm_shader_dir}/BodyInstanceRayMarch.comp")
set(_brm_spv "${CMAKE_CURRENT_BINARY_DIR}/BodyInstanceRayMarch.spv")

# DEPEND on the .comp AND every .glsl it may #include — otherwise editing an include
# (e.g. StoredSdf.glsl / ESVOTraversal.glsl) leaves the .spv stale ("ninja: no work to do").
file(GLOB _brm_includes CONFIGURE_DEPENDS "${_brm_shader_dir}/*.glsl")

add_custom_command(
    OUTPUT  ${_brm_spv}
    COMMAND ${VIXEN_GLSLC}
            -fshader-stage=compute
            -I ${_brm_shader_dir}
            -I ${CMAKE_SOURCE_DIR}/libraries/SVO/shaders
            --target-env=vulkan1.3
            # Baked-perf-pipeline M2 (audit D1, Task 2.2): test_body_instance_occlusion_reject.cpp
            # and test_tier_crossing_lod_residency.cpp (both share this SPV, see
            # body_instance_raymarch_spv's DEPENDS below) read back binding 14
            # (InstanceIterDebugBuffer) and assert on real per-instance iteration counts --
            # VIXEN_GPU_TRACE_HOOKS must be defined here so those stores are present in this
            # SPV, matching the "tests compile the hooks-ON shader variant explicitly"
            # approach (production's BuildRenderGraph.cpp compiles hooks-OFF by default; see
            # SceneBindings.glsl's InstanceIterDebugBuffer comment). This is the ONLY SPV this
            # custom command produces, and it is test-only infrastructure never loaded by the
            # live app, so defining it unconditionally here does not affect production defaults.
            -DVIXEN_GPU_TRACE_HOOKS=1
            ${_brm_src}
            -o ${_brm_spv}
    DEPENDS ${_brm_src} ${_brm_includes}
    COMMENT "Compiling BodyInstanceRayMarch.comp -> SPIR-V (bundled glslc, VIXEN_GPU_TRACE_HOOKS=1 for instanceIterCount readback tests)"
    VERBATIM)
add_custom_target(body_instance_raymarch_spv DEPENDS ${_brm_spv})

# Raster-proxy B1 M2/M4: a SECOND SPV variant with VIXEN_B1_OCCLUSION_CULL defined —
# the depthDistanceImage declaration+store (binding 36) is #ifdef-gated in the shader
# (production injects the define only when the env flag is set). ONLY gpurender1's
# fixture binds an image at 36 (its DepthDistanceImageMatchesHitRecords test needs the
# B1-ON variant); every other consumer keeps the PLAIN spv above — binding a descriptor
# the shader doesn't declare, or declaring a binding nothing binds, are both errors
# (the binding-8 lesson), so each bundle gets exactly the variant its fixture matches.
set(_brm_spv_b1 "${CMAKE_CURRENT_BINARY_DIR}/BodyInstanceRayMarch_b1.spv")
add_custom_command(
    OUTPUT  ${_brm_spv_b1}
    COMMAND ${VIXEN_GLSLC}
            -fshader-stage=compute
            -I ${_brm_shader_dir}
            -I ${CMAKE_SOURCE_DIR}/libraries/SVO/shaders
            --target-env=vulkan1.3
            -DVIXEN_GPU_TRACE_HOOKS=1
            -DVIXEN_B1_OCCLUSION_CULL=1
            ${_brm_src}
            -o ${_brm_spv_b1}
    DEPENDS ${_brm_src} ${_brm_includes}
    COMMENT "Compiling BodyInstanceRayMarch.comp -> SPIR-V (B1-ON variant, binding 36 declared)"
    VERBATIM)
add_custom_target(body_instance_raymarch_spv_b1 DEPENDS ${_brm_spv_b1})

# Raster-proxy B2 measured path: B1 remains enabled and the compact proxy
# interval/candidate buffer is added at binding 42.  The A/B fixture uses this
# third variant for its baseline / B1 / B1+B2 single-variable gate.
set(_brm_spv_b2 "${CMAKE_CURRENT_BINARY_DIR}/BodyInstanceRayMarch_b2.spv")
add_custom_command(
    OUTPUT  ${_brm_spv_b2}
    COMMAND ${VIXEN_GLSLC}
            -fshader-stage=compute
            -I ${_brm_shader_dir}
            -I ${CMAKE_SOURCE_DIR}/libraries/SVO/shaders
            --target-env=vulkan1.3
            -DVIXEN_GPU_TRACE_HOOKS=1
            -DVIXEN_B1_OCCLUSION_CULL=1
            -DVIXEN_B2_PROXY_PREPASS=1
            ${_brm_src}
            -o ${_brm_spv_b2}
    DEPENDS ${_brm_src} ${_brm_includes}
    COMMENT "Compiling BodyInstanceRayMarch.comp -> SPIR-V (B1+B2 variant, binding 42 declared)"
    VERBATIM)
add_custom_target(body_instance_raymarch_spv_b2 DEPENDS ${_brm_spv_b2})

# ===========================================================================
# Group 5: test_rendergraph_criticalnodes_gpurender1 — real-shader GPU render
# tests sharing body_instance_raymarch_spv, SVO + (conditional) stb, single
# GLSL_RAYMARCH_SPV compile-def, PRE_TEST discovery (120s timeout — GPU-init
# discovery flake, see file headers below).
#
# NOTE (found only by an actual Windows/MSVC link, not static review, exactly
# the Milestone-1 lesson): test_body_instance_raymarch_render.cpp,
# test_recipe_pool_render.cpp, and test_mip_fallback_render.cpp EACH
# `#define STB_IMAGE_WRITE_IMPLEMENTATION` to instantiate stb_image_write.h's
# implementation — at most ONE such file may exist per binary, or every
# stbi_write_* symbol is LNK2005 multiply-defined. test_hitrecord_readback.cpp
# does not write PNGs (no stb dependency at all) and is the only file in the
# original 4-way group safe to pair with an STB-impl file. Split into:
# BodyInstanceRayMarchRender+HitRecordReadback (paired), RecipePoolRender
# (standalone STB-impl), MipFallbackRender (standalone STB-impl).
# ===========================================================================
add_executable(test_rendergraph_criticalnodes_gpurender1
    Nodes/test_body_instance_raymarch_render.cpp
    Nodes/test_hitrecord_readback.cpp
)
add_dependencies(test_rendergraph_criticalnodes_gpurender1 body_instance_raymarch_spv body_instance_raymarch_spv_b1)
target_link_libraries(test_rendergraph_criticalnodes_gpurender1 PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
if(TARGET SVO)
    target_link_libraries(test_rendergraph_criticalnodes_gpurender1 PRIVATE SVO)
endif()
if(TARGET stb)
    target_link_libraries(test_rendergraph_criticalnodes_gpurender1 PRIVATE stb)
else()
    # stb is an INTERFACE header dep; if the target isn't visible here, add its include dir.
    target_include_directories(test_rendergraph_criticalnodes_gpurender1 PRIVATE
        "${stb_SOURCE_DIR}")
endif()
# Per-SOURCE SPV variants (B1 M4, found by native validation VUID-07988, masked on Dozen):
# the render fixture binds the depth image at 36 → B1-ON variant; the hitrecord fixture's
# own pipeline layout does NOT declare 36 → plain variant. One bundle, each TU gets exactly
# the variant its fixture matches — same doctrine as the per-bundle split above.
set_source_files_properties(Nodes/test_body_instance_raymarch_render.cpp PROPERTIES
    COMPILE_DEFINITIONS GLSL_RAYMARCH_SPV="${_brm_spv_b1}")
set_source_files_properties(Nodes/test_hitrecord_readback.cpp PROPERTIES
    COMPILE_DEFINITIONS GLSL_RAYMARCH_SPV="${_brm_spv}")
if(VIXEN_WSL_DZN_ICD)
    target_compile_definitions(test_rendergraph_criticalnodes_gpurender1 PRIVATE VIXEN_WSL_DZN_ICD="${VIXEN_WSL_DZN_ICD}")
endif()

set_target_properties(test_rendergraph_criticalnodes_gpurender1 PROPERTIES FOLDER "Tests/RenderGraph Tests")
# DISCOVERY_MODE PRE_TEST: defer the --gtest_list_tests invocation to ctest run-time,
# not POST_BUILD. This prevents the Vulkan-init timeout from making the build "FAILED"
# (the known MSB3073 / 5s discovery timeout flake — see friction log 2026-06-13).
gtest_discover_tests(test_rendergraph_criticalnodes_gpurender1
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120)

message(STATUS "[RenderGraph Tests] Added: test_rendergraph_criticalnodes_gpurender1 (merged: body_instance_raymarch_render/hitrecord_readback)")

# RecipePoolRender — kept standalone: has its own STB_IMAGE_WRITE_IMPLEMENTATION
# (see NOTE above); no remaining non-STB-impl partner left after pairing
# HitRecordReadback with BodyInstanceRayMarchRender above.
add_executable(test_recipe_pool_render
    Nodes/test_recipe_pool_render.cpp
)
add_dependencies(test_recipe_pool_render body_instance_raymarch_spv)
target_link_libraries(test_recipe_pool_render PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
if(TARGET SVO)
    target_link_libraries(test_recipe_pool_render PRIVATE SVO)
endif()
if(TARGET stb)
    target_link_libraries(test_recipe_pool_render PRIVATE stb)
else()
    target_include_directories(test_recipe_pool_render PRIVATE
        "${stb_SOURCE_DIR}")
endif()
target_compile_definitions(test_recipe_pool_render PRIVATE
    GLSL_RAYMARCH_SPV="${_brm_spv}")
if(VIXEN_WSL_DZN_ICD)
    target_compile_definitions(test_recipe_pool_render PRIVATE VIXEN_WSL_DZN_ICD="${VIXEN_WSL_DZN_ICD}")
endif()
set_target_properties(test_recipe_pool_render PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_recipe_pool_render
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120)
message(STATUS "[RenderGraph Tests] Added: test_recipe_pool_render (I4.1 pool render gate; standalone, own STB_IMAGE_WRITE_IMPLEMENTATION)")

# MipFallbackRender — kept standalone: has its own STB_IMAGE_WRITE_IMPLEMENTATION
# (see NOTE above).
add_executable(test_mip_fallback_render
    Nodes/test_mip_fallback_render.cpp
)
add_dependencies(test_mip_fallback_render body_instance_raymarch_spv)
target_link_libraries(test_mip_fallback_render PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
if(TARGET SVO)
    target_link_libraries(test_mip_fallback_render PRIVATE SVO)
endif()
if(TARGET stb)
    target_link_libraries(test_mip_fallback_render PRIVATE stb)
else()
    target_include_directories(test_mip_fallback_render PRIVATE
        "${stb_SOURCE_DIR}")
endif()
target_compile_definitions(test_mip_fallback_render PRIVATE
    GLSL_RAYMARCH_SPV="${_brm_spv}")
if(VIXEN_WSL_DZN_ICD)
    target_compile_definitions(test_mip_fallback_render PRIVATE VIXEN_WSL_DZN_ICD="${VIXEN_WSL_DZN_ICD}")
endif()
set_target_properties(test_mip_fallback_render PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_mip_fallback_render
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120)
message(STATUS "[RenderGraph Tests] Added: test_mip_fallback_render (Sparse-Mip ESVO LOD Inc1 M3; standalone, own STB_IMAGE_WRITE_IMPLEMENTATION)")

# ===========================================================================
# Sampled Lighting Inc0 M6 Task 14 — baked-vs-virtual geometry parity gate.
# Kept STANDALONE: hand-rolled int main()+RUN_ALL_TESTS() (can't coexist with
# any other main()-defining file in this directory), no body_instance_raymarch_spv
# dependency (compiles its shader variants at TEST RUNTIME instead), and its
# own 3-compile-def set (BODY_INSTANCE_RAYMARCH_COMP_PATH/VIXEN_SHADERS_DIR/
# VIXEN_SVO_SHADERS_DIR) distinct from GLSL_RAYMARCH_SPV.
# ===========================================================================
add_executable(test_baked_vs_virtual_parity
    Nodes/test_baked_vs_virtual_parity.cpp
)
target_link_libraries(test_baked_vs_virtual_parity PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
if(TARGET SVO)
    target_link_libraries(test_baked_vs_virtual_parity PRIVATE SVO)
endif()
if(TARGET ShaderManagement)
    target_link_libraries(test_baked_vs_virtual_parity PRIVATE ShaderManagement)
endif()
if(TARGET stb)
    target_link_libraries(test_baked_vs_virtual_parity PRIVATE stb)
else()
    target_include_directories(test_baked_vs_virtual_parity PRIVATE
        "${CMAKE_BINARY_DIR}/_deps/stb-src")
endif()
target_compile_definitions(test_baked_vs_virtual_parity PRIVATE
    BODY_INSTANCE_RAYMARCH_COMP_PATH="${CMAKE_SOURCE_DIR}/shaders/BodyInstanceRayMarch.comp"
    VIXEN_SHADERS_DIR="${CMAKE_SOURCE_DIR}/shaders"
    VIXEN_SVO_SHADERS_DIR="${CMAKE_SOURCE_DIR}/libraries/SVO/shaders"
)
if(VIXEN_WSL_DZN_ICD)
    target_compile_definitions(test_baked_vs_virtual_parity PRIVATE VIXEN_WSL_DZN_ICD="${VIXEN_WSL_DZN_ICD}")
endif()
set_target_properties(test_baked_vs_virtual_parity PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_baked_vs_virtual_parity
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120)
message(STATUS "[RenderGraph Tests] Added: test_baked_vs_virtual_parity (Lazy-Procedural-Delta-Baseline Inc0 M6 Task 14)")

# ===========================================================================
# Group 6a: test_rendergraph_criticalnodes_gpurender2 — more real-shader GPU
# render/dispatch tests sharing body_instance_raymarch_spv + SVO +
# GLSL_RAYMARCH_SPV + PRE_TEST discovery. TierCrossingLodResidency's
# `if(TARGET GaiaVoxelWorld)` link is a pre-existing no-op here (GaiaVoxelWorld
# is already unconditionally in RENDERGRAPH_TEST_COMMON_LIBS when present —
# see the conditional append block above), so folding it into this group adds
# zero real new surface.
#
# NOTE (found only by an actual Windows/MSVC link, not static review, exactly
# the Milestone-1 lesson): test_editor_document_render.cpp AND
# test_recipe_authoring_gate.cpp EACH `#define STB_IMAGE_WRITE_IMPLEMENTATION`
# — at most ONE such file may exist per binary (LNK2005 otherwise, same
# constraint as the gpurender1 split above). Split into two groups so each
# keeps exactly one STB-impl file: this group pairs EditorDocumentRender with
# the two files that don't write PNGs (BodyInstanceOcclusionReject,
# TierCrossingLodResidency); RecipeAuthoringGate goes in gpurender2b below
# with ShadowCorrectness.
# ===========================================================================
add_executable(test_rendergraph_criticalnodes_gpurender2
    Nodes/test_body_instance_occlusion_reject.cpp
    Nodes/test_editor_document_render.cpp
    Nodes/test_tier_crossing_lod_residency.cpp
)
add_dependencies(test_rendergraph_criticalnodes_gpurender2 body_instance_raymarch_spv)
target_link_libraries(test_rendergraph_criticalnodes_gpurender2 PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
if(TARGET SVO)
    target_link_libraries(test_rendergraph_criticalnodes_gpurender2 PRIVATE SVO)
endif()
if(TARGET stb)
    target_link_libraries(test_rendergraph_criticalnodes_gpurender2 PRIVATE stb)
else()
    target_include_directories(test_rendergraph_criticalnodes_gpurender2 PRIVATE
        "${stb_SOURCE_DIR}")
endif()
target_compile_definitions(test_rendergraph_criticalnodes_gpurender2 PRIVATE
    GLSL_RAYMARCH_SPV="${_brm_spv}"
    VXD_GOLDEN_PATH="${VIXEN_ROOT}/BuiltAssets/documents/sample_tri_layer.vxd")
if(VIXEN_WSL_DZN_ICD)
    target_compile_definitions(test_rendergraph_criticalnodes_gpurender2 PRIVATE VIXEN_WSL_DZN_ICD="${VIXEN_WSL_DZN_ICD}")
endif()
set_target_properties(test_rendergraph_criticalnodes_gpurender2 PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_rendergraph_criticalnodes_gpurender2
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120)

# Row B / 0eb.5: editor preview consumes the direct flattened RecipeEntry while the VRC1
# serializer remains the export/persistence seam. Headless byte parity covers both all-layer and
# enabled-mask paths without requiring a Vulkan device.
add_executable(test_editor_document_model_preview
    Nodes/test_editor_document_model_preview.cpp
)
target_compile_features(test_editor_document_model_preview PRIVATE cxx_std_23)
target_link_libraries(test_editor_document_model_preview PRIVATE GTest::gtest_main glm::glm)
if(TARGET SVO)
    target_link_libraries(test_editor_document_model_preview PRIVATE SVO)
endif()
target_include_directories(test_editor_document_model_preview PRIVATE
    ${CMAKE_SOURCE_DIR}/application/editor/include
    ${CMAKE_SOURCE_DIR}/libraries/SVO/include
)
target_compile_definitions(test_editor_document_model_preview PRIVATE
    VXD_GOLDEN_PATH="${VIXEN_ROOT}/BuiltAssets/documents/sample_tri_layer.vxd")
set_target_properties(test_editor_document_model_preview PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_editor_document_model_preview)
message(STATUS "[RenderGraph Tests] Added: test_editor_document_model_preview (direct flatten/VRC1 parity)")

# ===========================================================================
# Group 6b: test_rendergraph_criticalnodes_gpurender2b — RecipeAuthoringGate
# (own STB_IMAGE_WRITE_IMPLEMENTATION, see NOTE above) paired with
# ShadowCorrectness (no stb dependency) — the one remaining non-STB-impl file
# from the original 5-target group. Same body_instance_raymarch_spv + SVO +
# GLSL_RAYMARCH_SPV + PRE_TEST contract.
# ===========================================================================
add_executable(test_rendergraph_criticalnodes_gpurender2b
    Nodes/test_recipe_authoring_gate.cpp
    Nodes/test_shadow_correctness.cpp
)
add_dependencies(test_rendergraph_criticalnodes_gpurender2b body_instance_raymarch_spv)
target_link_libraries(test_rendergraph_criticalnodes_gpurender2b PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
if(TARGET SVO)
    target_link_libraries(test_rendergraph_criticalnodes_gpurender2b PRIVATE SVO)
endif()
if(TARGET stb)
    target_link_libraries(test_rendergraph_criticalnodes_gpurender2b PRIVATE stb)
else()
    target_include_directories(test_rendergraph_criticalnodes_gpurender2b PRIVATE
        "${stb_SOURCE_DIR}")
endif()
target_compile_definitions(test_rendergraph_criticalnodes_gpurender2b PRIVATE
    GLSL_RAYMARCH_SPV="${_brm_spv}"
    # ShadowCorrectness fix (2026-09-01): compiles SpatialReuseShade.comp and
    # ShadowVisibilityWave.comp at TEST RUNTIME via ShaderManagement (already in
    # RENDERGRAPH_TEST_COMMON_LIBS -- see that list's own `if(TARGET ShaderManagement)` append
    # above), using the same VIXEN_SHADER_SOURCE_DIR-based AddIncludePath() pattern as
    # test_sampling_compile_gate.cpp and BuildRenderGraph.cpp's shader registrations, so
    # #include "SceneBindings.glsl" / "Generated/*.glsl" resolve against the real shader tree.
    VIXEN_SHADER_SOURCE_DIR="${VIXEN_SHADER_SOURCE_DIR}")
if(VIXEN_WSL_DZN_ICD)
    target_compile_definitions(test_rendergraph_criticalnodes_gpurender2b PRIVATE VIXEN_WSL_DZN_ICD="${VIXEN_WSL_DZN_ICD}")
endif()
set_target_properties(test_rendergraph_criticalnodes_gpurender2b PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_rendergraph_criticalnodes_gpurender2b
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120)
message(STATUS "[RenderGraph Tests] Added: test_rendergraph_criticalnodes_gpurender2b (merged: recipe_authoring_gate/shadow_correctness)")

# ===========================================================================
# AppFlow Inc-2 M4 — headless GPU render-gate. Kept STANDALONE: the only
# target in this file that links AppFlow (offline-only lib) — no other target
# needs it, so merging would add unique surface to a group for no shared
# benefit. Links AppFlow alongside RenderGraph + SVO; the GPU dependency
# lives in this test, not in the AppFlow lib.
# ===========================================================================
add_executable(test_appflow_editor_toggle_render
    Nodes/test_appflow_editor_toggle_render.cpp
)
add_dependencies(test_appflow_editor_toggle_render body_instance_raymarch_spv)
target_link_libraries(test_appflow_editor_toggle_render PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
if(TARGET SVO)
    target_link_libraries(test_appflow_editor_toggle_render PRIVATE SVO)
endif()
if(TARGET AppFlow)
    target_link_libraries(test_appflow_editor_toggle_render PRIVATE AppFlow)
endif()
if(TARGET stb)
    target_link_libraries(test_appflow_editor_toggle_render PRIVATE stb)
else()
    target_include_directories(test_appflow_editor_toggle_render PRIVATE
        "${stb_SOURCE_DIR}")
endif()
target_compile_definitions(test_appflow_editor_toggle_render PRIVATE
    GLSL_RAYMARCH_SPV="${_brm_spv}"
    VXD_GOLDEN_PATH="${VIXEN_ROOT}/BuiltAssets/documents/sample_tri_layer.vxd")
if(VIXEN_WSL_DZN_ICD)
    target_compile_definitions(test_appflow_editor_toggle_render PRIVATE VIXEN_WSL_DZN_ICD="${VIXEN_WSL_DZN_ICD}")
endif()
set_target_properties(test_appflow_editor_toggle_render PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_appflow_editor_toggle_render
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120)
message(STATUS "[RenderGraph Tests] Added: test_appflow_editor_toggle_render (AppFlow Inc-2 M4 GPU render-gate)")

# ===========================================================================
# Group 7: test_rendergraph_criticalnodes_sdiparity — CPU-only SPIR-V
# reflection drift-guards (no lavapipe/no GPU dispatch), all reusing the same
# compiled body_instance_raymarch_spv + identical GLSL_RAYMARCH_SPV
# compile-def, all PRE_TEST discovery, no hand-rolled main() in any member.
# OctreeConfigSdiParity's `if(TARGET SVO)` link is a cheap union (SVO already
# reaches every target here transitively via RenderGraph). ReservoirConfig
# has no shader to reflect against (pure struct-size/offsetof) and no
# body_instance_raymarch_spv dependency of its own — a harmless superset dep
# once merged into a group that already depends on it.
# OctreeConfigSdiParity / LightingConfigSdiParity / HitRecordSdiParity /
# ShadowConfigSdiParity / AccumulationConfigSdiParity /
# PrevCameraConfigSdiParity / ReservoirConfigLayout.
# ===========================================================================
add_executable(test_rendergraph_criticalnodes_sdiparity
    Nodes/test_octree_config_sdi_parity.cpp
    Nodes/test_lightingconfig_sdi_parity.cpp
    Nodes/test_hitrecord_sdi_parity.cpp
    Nodes/test_shadowconfig_sdi_parity.cpp
    Nodes/test_accumulationconfig_sdi_parity.cpp
    Nodes/test_prevcameraconfig_sdi_parity.cpp
    Nodes/test_reservoirconfig_layout.cpp
)
add_dependencies(test_rendergraph_criticalnodes_sdiparity body_instance_raymarch_spv)
target_link_libraries(test_rendergraph_criticalnodes_sdiparity PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
if(TARGET SVO)
    target_link_libraries(test_rendergraph_criticalnodes_sdiparity PRIVATE SVO)
endif()
target_compile_definitions(test_rendergraph_criticalnodes_sdiparity PRIVATE
    GLSL_RAYMARCH_SPV="${_brm_spv}")
set_target_properties(test_rendergraph_criticalnodes_sdiparity PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_rendergraph_criticalnodes_sdiparity
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120)
message(STATUS "[RenderGraph Tests] Added: test_rendergraph_criticalnodes_sdiparity (merged: octree_config/lightingconfig/hitrecord/shadowconfig/accumulationconfig/prevcameraconfig sdi_parity + reservoirconfig_layout)")

else()
    message(STATUS "[RenderGraph Tests] SKIPPED test_body_instance_raymarch_render — no glslc runnable on this platform found (searched ${_glslc_hints} + PATH)")
endif()

# ===========================================================================
# Group 8: test_rendergraph_criticalnodes_windowedcapture — pure file-I/O
# (stb_image) PNG-assertion tests, no Vulkan/GPU, deliberately registered
# OUTSIDE the glslc-gated `if(VIXEN_GLSLC)` block above so they build+run on
# the Windows/MSVC side regardless of glslc availability, matching where the
# windowed apps that produce their input PNGs themselves build and run.
# AppFlow Inc-2b M3 (EditorToggleUndoCapture) + View Contract Inc-2 Task 5
# (HudRenderCapture) — same bare-plus-stb link surface, default discovery.
# ===========================================================================
add_executable(test_rendergraph_criticalnodes_windowedcapture
    Nodes/test_editor_toggle_undo_capture.cpp
    Nodes/test_hud_render_capture.cpp
)
target_link_libraries(test_rendergraph_criticalnodes_windowedcapture PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
if(TARGET stb)
    target_link_libraries(test_rendergraph_criticalnodes_windowedcapture PRIVATE stb)
else()
    target_include_directories(test_rendergraph_criticalnodes_windowedcapture PRIVATE
        "${stb_SOURCE_DIR}")
endif()
set_target_properties(test_rendergraph_criticalnodes_windowedcapture PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_rendergraph_criticalnodes_windowedcapture)
message(STATUS "[RenderGraph Tests] Added: test_rendergraph_criticalnodes_windowedcapture (merged: editor_toggle_undo_capture/hud_render_capture)")

# ===========================================================================
# P2.2 M2 — Procedural recipe live compute render (compile realization).
# Kept STANDALONE: its own `if(TARGET ShaderManagement)` gate block (separate
# from the glslc-gated block above) and its own POST_BUILD TBB DLL copy step
# — no other target in this file needs that POST_BUILD, so merging would
# force it onto unrelated tests for no shared benefit.
# ===========================================================================
# Emits an all-HLSL compute shader from SdfInstruction[], compiles it via
# ShaderCompiler (HLSL→SPIR-V at test run time), dispatches with a
# minimal 1-binding (storage-image) + push-constant compute harness.
# No pre-compiled .spv needed (ShaderCompiler handles it at runtime).
# Same device-selection contract as test_body_instance_raymarch_render.
if(TARGET ShaderManagement)
# NOTE: SVO target is not visible at this include scope (subdirectory ordering),
# same as test_body_instance_raymarch_render. SVO headers + ShaderCompiler reach
# the test transitively via RENDERGRAPH_TEST_COMMON_LIBS (which includes ShaderManagement).

add_executable(test_procedural_recipe_render
    Nodes/test_procedural_recipe_render.cpp
)

target_link_libraries(test_procedural_recipe_render PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
# Optional explicit SVO link (no-op at this scope, transitive via RENDERGRAPH_TEST_COMMON_LIBS).
if(TARGET SVO)
    target_link_libraries(test_procedural_recipe_render PRIVATE SVO)
endif()

if(TARGET stb)
    target_link_libraries(test_procedural_recipe_render PRIVATE stb)
else()
    target_include_directories(test_procedural_recipe_render PRIVATE
        "${stb_SOURCE_DIR}")
endif()

# SDF_CORE_KERNELS_HLSL_PATH: same path as used by test_recipe_codegen.
target_compile_definitions(test_procedural_recipe_render PRIVATE
    SDF_CORE_KERNELS_HLSL_PATH="${CMAKE_SOURCE_DIR}/libraries/SVO/shaders/recipe/SdfCoreKernels.g.hlsl"
)
if(VIXEN_WSL_DZN_ICD)
    target_compile_definitions(test_procedural_recipe_render PRIVATE VIXEN_WSL_DZN_ICD="${VIXEN_WSL_DZN_ICD}")
endif()

if(TARGET TBB::tbb)
    add_custom_command(TARGET test_procedural_recipe_render POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb>
            $<TARGET_FILE_DIR:test_procedural_recipe_render>
        COMMENT "Copying TBB DLL for test_procedural_recipe_render")
endif()

set_target_properties(test_procedural_recipe_render PROPERTIES FOLDER "Tests/RenderGraph Tests")

# PRE_TEST discovery to avoid Vulkan-init timeout during build (same as raymarch render test).
gtest_discover_tests(test_procedural_recipe_render
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120)

message(STATUS "[RenderGraph Tests] Added: test_procedural_recipe_render (P2.2 M2 live procedural compute)")
else()
    message(STATUS "[RenderGraph Tests] SKIPPED test_procedural_recipe_render — ShaderManagement not available")
endif()

# ===========================================================================
# Surface-Shell ESVO cache — ShellRevalidateNode GPU dispatch live-gate.
# Kept STANDALONE: its own separate `if(VIXEN_GLSLC)` gate block with its own
# custom-command target (shell_derive_spv, compiling ShellDerive.comp — a
# DIFFERENT shader than body_instance_raymarch_spv's BodyInstanceRayMarch.comp),
# so it cannot share a merged group with any body_instance_raymarch_spv-
# dependent target above.
# ===========================================================================
# Compiles the SHIPPED shaders/ShellDerive.comp to SPIR-V at build time with the
# environment-appropriate glslc (same gate as body_instance_raymarch_spv above — skipped
# when no platform-runnable glslc is found), then: (a) asserts the GPU dispatch's shellFlags[]
# classification matches the CPU Vixen::SVO::DeriveShell oracle bit-for-bit, and (b) assembles a
# real ComputePassStep pair with disjoint Resource* accesses and asserts BuildPassGroupSchedule
# bakes ZERO entry barriers between them (double-buffer parallelism proof).
if(VIXEN_GLSLC)
set(_shellderive_src "${_brm_shader_dir}/ShellDerive.comp")
set(_shellderive_spv "${CMAKE_CURRENT_BINARY_DIR}/ShellDerive.spv")
file(GLOB _shellderive_includes CONFIGURE_DEPENDS "${_brm_shader_dir}/*.glsl" "${_brm_shader_dir}/Generated/*.glsl")

add_custom_command(
    OUTPUT  ${_shellderive_spv}
    COMMAND ${VIXEN_GLSLC}
            -fshader-stage=compute
            -I ${_brm_shader_dir}
            --target-env=vulkan1.2
            ${_shellderive_src}
            -o ${_shellderive_spv}
    DEPENDS ${_shellderive_src} ${_shellderive_includes}
    COMMENT "Compiling ShellDerive.comp -> SPIR-V (bundled glslc)"
    VERBATIM)
add_custom_target(shell_derive_spv DEPENDS ${_shellderive_spv})

add_executable(test_shell_revalidate_node
    Nodes/test_shell_revalidate_node.cpp
)
add_dependencies(test_shell_revalidate_node shell_derive_spv)
target_link_libraries(test_shell_revalidate_node PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
if(TARGET SVO)
    target_link_libraries(test_shell_revalidate_node PRIVATE SVO)
endif()
target_compile_definitions(test_shell_revalidate_node PRIVATE
    SHELLDERIVE_SPV="${_shellderive_spv}")
if(VIXEN_WSL_DZN_ICD)
    target_compile_definitions(test_shell_revalidate_node PRIVATE VIXEN_WSL_DZN_ICD="${VIXEN_WSL_DZN_ICD}")
endif()

set_target_properties(test_shell_revalidate_node PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_shell_revalidate_node
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120)

message(STATUS "[RenderGraph Tests] Added: test_shell_revalidate_node (Surface-Shell GPU dispatch live-gate)")
else()
    message(STATUS "[RenderGraph Tests] SKIPPED test_shell_revalidate_node — no glslc runnable on this platform found")
endif()

# ===========================================================================
# Recipe GPU Instance Bucketing Inc2 M1 — bucketing pre-pass live-run gate.
# Kept STANDALONE, mirroring the ShellDerive.comp block above: its own separate
# `if(VIXEN_GLSLC)` gate block with its own custom-command target
# (recipe_instance_bucketing_spv, compiling shaders/RecipeInstanceBucketing.comp — a
# standalone shader with its own binding namespace starting at 0, no dependency on
# BodyInstanceRayMarch.comp's uber-shader splice chain), so it cannot share a merged
# group with any body_instance_raymarch_spv-dependent target above.
# ===========================================================================
if(VIXEN_GLSLC)
set(_recipebucketing_src "${_brm_shader_dir}/RecipeInstanceBucketing.comp")
set(_recipebucketing_spv "${CMAKE_CURRENT_BINARY_DIR}/RecipeInstanceBucketing.spv")

add_custom_command(
    OUTPUT  ${_recipebucketing_spv}
    COMMAND ${VIXEN_GLSLC}
            -fshader-stage=compute
            --target-env=vulkan1.3
            ${_recipebucketing_src}
            -o ${_recipebucketing_spv}
    DEPENDS ${_recipebucketing_src}
    COMMENT "Compiling RecipeInstanceBucketing.comp -> SPIR-V (bundled glslc)"
    VERBATIM)
add_custom_target(recipe_instance_bucketing_spv DEPENDS ${_recipebucketing_spv})

add_executable(test_recipe_instance_bucketing
    Nodes/test_recipe_instance_bucketing.cpp
)
add_dependencies(test_recipe_instance_bucketing recipe_instance_bucketing_spv)
target_link_libraries(test_recipe_instance_bucketing PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
if(TARGET SVO)
    target_link_libraries(test_recipe_instance_bucketing PRIVATE SVO)
endif()
target_compile_definitions(test_recipe_instance_bucketing PRIVATE
    RECIPE_BUCKETING_SPV="${_recipebucketing_spv}")
if(VIXEN_WSL_DZN_ICD)
    target_compile_definitions(test_recipe_instance_bucketing PRIVATE VIXEN_WSL_DZN_ICD="${VIXEN_WSL_DZN_ICD}")
endif()

set_target_properties(test_recipe_instance_bucketing PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_recipe_instance_bucketing
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120)

message(STATUS "[RenderGraph Tests] Added: test_recipe_instance_bucketing (Recipe GPU Instance Bucketing Inc2 M1 live-run gate)")
else()
    message(STATUS "[RenderGraph Tests] SKIPPED test_recipe_instance_bucketing — no glslc runnable on this platform found")
endif()

# ===========================================================================
# Recipe GPU Instance Bucketing Inc2 M2 — indirect dispatch + specialized pipeline live-run gate.
# Shares recipe_instance_bucketing_spv (the M1 bucketing shader, now with M2's added mode==2
# finalize pass) — kept inside the SAME if(VIXEN_GLSLC) block since it depends on that target.
# The specialized single-recipe shader (Task 5) is NOT build-time glslc-compiled: it's emitted
# and compiled AT TEST RUNTIME via ShaderManagement::ShaderCompiler (mirrors
# test_procedural_recipe_render.cpp's own runtime-compile pattern), so this target needs no
# second add_custom_command — only the vendored SdfCoreKernels.glsl file PATH (inlined into the
# generated source at runtime, not #include-d — see SpecializedRecipeShaderGlsl.h's header
# comment for why ShaderCompiler::Compile cannot resolve #include directives).
# ===========================================================================
if(VIXEN_GLSLC)
add_executable(test_recipe_bucketed_indirect_dispatch
    Nodes/test_recipe_bucketed_indirect_dispatch.cpp
)
add_dependencies(test_recipe_bucketed_indirect_dispatch recipe_instance_bucketing_spv)
target_link_libraries(test_recipe_bucketed_indirect_dispatch PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
if(TARGET SVO)
    target_link_libraries(test_recipe_bucketed_indirect_dispatch PRIVATE SVO)
endif()
target_compile_definitions(test_recipe_bucketed_indirect_dispatch PRIVATE
    RECIPE_BUCKETING_SPV="${_recipebucketing_spv}"
    SDF_CORE_KERNELS_GLSL_PATH="${CMAKE_SOURCE_DIR}/libraries/SVO/shaders/recipe/SdfCoreKernels.glsl")
if(VIXEN_WSL_DZN_ICD)
    target_compile_definitions(test_recipe_bucketed_indirect_dispatch PRIVATE VIXEN_WSL_DZN_ICD="${VIXEN_WSL_DZN_ICD}")
endif()

set_target_properties(test_recipe_bucketed_indirect_dispatch PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_recipe_bucketed_indirect_dispatch
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120)

message(STATUS "[RenderGraph Tests] Added: test_recipe_bucketed_indirect_dispatch (Recipe GPU Instance Bucketing Inc2 M2 live-run gate)")
else()
    message(STATUS "[RenderGraph Tests] SKIPPED test_recipe_bucketed_indirect_dispatch — no glslc runnable on this platform found")
endif()

# ===========================================================================
# Recipe GPU Instance Bucketing Inc2 M3 — multi-recipe cross-bucket compositing live-run gate.
# Shares recipe_instance_bucketing_spv (M1's bucketing shader) exactly like the M2 target above —
# kept inside the SAME if(VIXEN_GLSLC) block since it depends on that custom-command target. Both
# the specialized single-recipe shaders (Task 5, looped for 2 hot recipes) AND the standalone
# cold-path shader (this milestone's own tier-0-equivalent, see the .cpp file's header comment for
# why the REAL BodyInstanceRayMarch.comp is out of scope here) are emitted/compiled AT TEST
# RUNTIME via ShaderManagement::ShaderCompiler — mirrors M2's own runtime-compile pattern, no
# second add_custom_command needed beyond the shared SdfCoreKernels.glsl path.
# ===========================================================================
if(VIXEN_GLSLC)
add_executable(test_recipe_multi_bucket_compositing
    Nodes/test_recipe_multi_bucket_compositing.cpp
)
add_dependencies(test_recipe_multi_bucket_compositing recipe_instance_bucketing_spv)
target_link_libraries(test_recipe_multi_bucket_compositing PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
if(TARGET SVO)
    target_link_libraries(test_recipe_multi_bucket_compositing PRIVATE SVO)
endif()
target_compile_definitions(test_recipe_multi_bucket_compositing PRIVATE
    RECIPE_BUCKETING_SPV="${_recipebucketing_spv}"
    SDF_CORE_KERNELS_GLSL_PATH="${CMAKE_SOURCE_DIR}/libraries/SVO/shaders/recipe/SdfCoreKernels.glsl")
if(VIXEN_WSL_DZN_ICD)
    target_compile_definitions(test_recipe_multi_bucket_compositing PRIVATE VIXEN_WSL_DZN_ICD="${VIXEN_WSL_DZN_ICD}")
endif()

set_target_properties(test_recipe_multi_bucket_compositing PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_recipe_multi_bucket_compositing
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120)

message(STATUS "[RenderGraph Tests] Added: test_recipe_multi_bucket_compositing (Recipe GPU Instance Bucketing Inc2 M3 live-run gate)")
else()
    message(STATUS "[RenderGraph Tests] SKIPPED test_recipe_multi_bucket_compositing — no glslc runnable on this platform found")
endif()

# ===========================================================================
# Recipe GPU Instance Bucketing Inc2 M4 — performance measurement live-run gate (Task 9).
# Shares recipe_instance_bucketing_spv exactly like the M2/M3 targets above — kept inside the
# SAME if(VIXEN_GLSLC) block since it depends on that custom-command target. N specialized
# single-recipe shaders (Task 5, looped up to N=100) AND the cold-path stand-in shader are
# emitted/compiled AT TEST RUNTIME via ShaderManagement::ShaderCompiler, same as M2/M3.
# Kept STANDALONE (not merged into gpurender groups above): this is a PERFORMANCE gate with its
# own steady-state timing loop (kSteadyIters repeats per N), and pairing it with a PNG/STB-impl
# target would let unrelated build failures block a perf capture, or vice versa.
# ===========================================================================
if(VIXEN_GLSLC)
add_executable(test_recipe_bucketing_perf
    Nodes/test_recipe_bucketing_perf.cpp
)
add_dependencies(test_recipe_bucketing_perf recipe_instance_bucketing_spv)
target_link_libraries(test_recipe_bucketing_perf PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
if(TARGET SVO)
    target_link_libraries(test_recipe_bucketing_perf PRIVATE SVO)
endif()
target_compile_definitions(test_recipe_bucketing_perf PRIVATE
    RECIPE_BUCKETING_SPV="${_recipebucketing_spv}"
    SDF_CORE_KERNELS_GLSL_PATH="${CMAKE_SOURCE_DIR}/libraries/SVO/shaders/recipe/SdfCoreKernels.glsl")
if(VIXEN_WSL_DZN_ICD)
    target_compile_definitions(test_recipe_bucketing_perf PRIVATE VIXEN_WSL_DZN_ICD="${VIXEN_WSL_DZN_ICD}")
endif()

set_target_properties(test_recipe_bucketing_perf PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_recipe_bucketing_perf
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120)

message(STATUS "[RenderGraph Tests] Added: test_recipe_bucketing_perf (Recipe GPU Instance Bucketing Inc2 M4 performance live-run gate)")
else()
    message(STATUS "[RenderGraph Tests] SKIPPED test_recipe_bucketing_perf — no glslc runnable on this platform found")
endif()

# ===========================================================================
# Recipe Bucketed-Dispatch Overhead Inc3 M0 — switch-cost isolation gating spike (Task 1).
# Does NOT depend on recipe_instance_bucketing_spv (this milestone is about the tier-0 SWITCH
# shader's own scaling, not the bucketing mechanism) — only needs SdfCoreKernels.glsl's path
# (read at runtime, same textual-inline convention M2/M3/M4 use since ShaderCompiler::Compile
# has no #include resolution) and a runtime-compiled synthetic switch-shader per test case, via
# ShaderManagement::ShaderCompiler. Kept inside the SAME if(VIXEN_GLSLC) guard as its siblings
# for consistency (glslc availability is this platform's general "can we compile GLSL at all"
# gate, even though this target's own shaders are all runtime-compiled, not build-time glslc).
# ===========================================================================
if(VIXEN_GLSLC)
add_executable(test_switch_cost_isolation
    Nodes/test_switch_cost_isolation.cpp
)
target_link_libraries(test_switch_cost_isolation PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
if(TARGET SVO)
    target_link_libraries(test_switch_cost_isolation PRIVATE SVO)
endif()
target_compile_definitions(test_switch_cost_isolation PRIVATE
    SDF_CORE_KERNELS_GLSL_PATH="${CMAKE_SOURCE_DIR}/libraries/SVO/shaders/recipe/SdfCoreKernels.glsl")
if(VIXEN_WSL_DZN_ICD)
    target_compile_definitions(test_switch_cost_isolation PRIVATE VIXEN_WSL_DZN_ICD="${VIXEN_WSL_DZN_ICD}")
endif()

set_target_properties(test_switch_cost_isolation PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_switch_cost_isolation
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120)

message(STATUS "[RenderGraph Tests] Added: test_switch_cost_isolation (Recipe Bucketed-Dispatch Overhead Inc3 M0 gating spike)")
else()
    message(STATUS "[RenderGraph Tests] SKIPPED test_switch_cost_isolation — no glslc runnable on this platform found")
endif()

# ---------------------------------------------------------------------------
# Raster-proxy B1 M2: HiZDownsample CPU mirror (gpu-shader-debug discipline).
# Device-less — pure reduce-math unit tests against Nodes/HiZDownsampleMirror.h,
# the 1:1 mirror of shaders/HiZDownsample.comp.
# ---------------------------------------------------------------------------
add_executable(test_hiz_downsample_mirror
    Nodes/test_hiz_downsample_mirror.cpp
)
target_link_libraries(test_hiz_downsample_mirror PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
set_target_properties(test_hiz_downsample_mirror PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_hiz_downsample_mirror)
message(STATUS "[RenderGraph Tests] Added: test_hiz_downsample_mirror (Raster-proxy B1 M2 CPU mirror)")

# ---------------------------------------------------------------------------
# Raster-proxy B2: compact union interval + ordered 192-bit candidate-mask
# CPU mirror. Device-less and intentionally separate from the B1 mirrors so
# its RED/GREEN gate remains focused on the new per-pixel contract.
# ---------------------------------------------------------------------------
add_executable(test_proxy_interval_prepass_mirror
    Nodes/test_proxy_interval_prepass_mirror.cpp
)
target_link_libraries(test_proxy_interval_prepass_mirror PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
set_target_properties(test_proxy_interval_prepass_mirror PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_proxy_interval_prepass_mirror)
message(STATUS "[RenderGraph Tests] Added: test_proxy_interval_prepass_mirror (Raster-proxy B2 CPU mirror)")

# glslc-validate the mirrored shader at build time (same VIXEN_GLSLC resolved
# above for the march SPV): building the mirror test proves the .comp compiles.
if(VIXEN_GLSLC)
    set(_hiz_src "${_brm_shader_dir}/HiZDownsample.comp")
    set(_hiz_spv "${CMAKE_CURRENT_BINARY_DIR}/HiZDownsample.spv")
    add_custom_command(
        OUTPUT  ${_hiz_spv}
        COMMAND ${VIXEN_GLSLC}
                -fshader-stage=compute
                --target-env=vulkan1.3
                ${_hiz_src}
                -o ${_hiz_spv}
        DEPENDS ${_hiz_src}
        COMMENT "Compiling HiZDownsample.comp -> SPIR-V (B1 M2 mirror-parity validation)"
        VERBATIM)
    add_custom_target(hiz_downsample_spv DEPENDS ${_hiz_spv})
    add_dependencies(test_hiz_downsample_mirror hiz_downsample_spv)
endif()

# ---------------------------------------------------------------------------
# Raster-proxy B1 M3: InstanceOcclusionCull CPU mirror (gpu-shader-debug
# discipline). Device-less — per-word cull math (AABB compose / reproject /
# tile test / conservatism escapes / OR-compose) against
# Nodes/InstanceOcclusionCullMirror.h, the 1:1 mirror of
# shaders/InstanceOcclusionCull.comp.
# ---------------------------------------------------------------------------
add_executable(test_instance_occlusion_cull_mirror
    Nodes/test_instance_occlusion_cull_mirror.cpp
)
target_link_libraries(test_instance_occlusion_cull_mirror PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
set_target_properties(test_instance_occlusion_cull_mirror PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_instance_occlusion_cull_mirror)
message(STATUS "[RenderGraph Tests] Added: test_instance_occlusion_cull_mirror (Raster-proxy B1 M3 CPU mirror)")

# ---------------------------------------------------------------------------
# Raster-proxy B1 M3 device gate: dispatch the SHIPPED InstanceOcclusionCull.comp
# against synthetic buffers; B1 camera-region skip-mask words must equal the CPU mirror's output
# on identical inputs. Same glslc/device-selection contract as
# test_recipe_instance_bucketing above.
# ---------------------------------------------------------------------------
if(VIXEN_GLSLC)
    set(_cull_src "${_brm_shader_dir}/InstanceOcclusionCull.comp")
    set(_cull_spv "${CMAKE_CURRENT_BINARY_DIR}/InstanceOcclusionCull.spv")
    add_custom_command(
        OUTPUT  ${_cull_spv}
        COMMAND ${VIXEN_GLSLC}
                -fshader-stage=compute
                -I ${_brm_shader_dir}
                --target-env=vulkan1.3
                ${_cull_src}
                -o ${_cull_spv}
        DEPENDS ${_cull_src} "${_brm_shader_dir}/Generated/OctreeConfig.glsl"
        COMMENT "Compiling InstanceOcclusionCull.comp -> SPIR-V (B1 M3)"
        VERBATIM)
    add_custom_target(instance_occlusion_cull_spv DEPENDS ${_cull_spv})

    set(_proxy_interval_vert_src "${_brm_shader_dir}/ProxyIntervalPrepass.vert")
    set(_proxy_interval_frag_src "${_brm_shader_dir}/ProxyIntervalPrepass.frag")
    set(_proxy_interval_comp_src "${_brm_shader_dir}/ProxyIntervalPrepass.comp")
    set(_proxy_interval_vert_spv "${CMAKE_CURRENT_BINARY_DIR}/ProxyIntervalPrepass.vert.spv")
    set(_proxy_interval_frag_spv "${CMAKE_CURRENT_BINARY_DIR}/ProxyIntervalPrepass.frag.spv")
    set(_proxy_interval_comp_spv "${CMAKE_CURRENT_BINARY_DIR}/ProxyIntervalPrepass.comp.spv")
    add_custom_command(
        OUTPUT ${_proxy_interval_vert_spv}
        COMMAND ${VIXEN_GLSLC}
                -fshader-stage=vertex
                -I ${_brm_shader_dir}
                --target-env=vulkan1.3
                ${_proxy_interval_vert_src}
                -o ${_proxy_interval_vert_spv}
        DEPENDS ${_proxy_interval_vert_src} "${_brm_shader_dir}/Generated/OctreeConfig.glsl"
        COMMENT "Compiling ProxyIntervalPrepass.vert -> SPIR-V (B2 device parity)"
        VERBATIM)
    add_custom_command(
        OUTPUT ${_proxy_interval_frag_spv}
        COMMAND ${VIXEN_GLSLC}
                -fshader-stage=fragment
                --target-env=vulkan1.3
                ${_proxy_interval_frag_src}
                -o ${_proxy_interval_frag_spv}
        DEPENDS ${_proxy_interval_frag_src}
        COMMENT "Compiling ProxyIntervalPrepass.frag -> SPIR-V (B2 device parity)"
        VERBATIM)
    add_custom_command(
        OUTPUT ${_proxy_interval_comp_spv}
        COMMAND ${VIXEN_GLSLC}
                -fshader-stage=compute
                -I ${_brm_shader_dir}
                --target-env=vulkan1.3
                ${_proxy_interval_comp_src}
                -o ${_proxy_interval_comp_spv}
        DEPENDS ${_proxy_interval_comp_src} "${_brm_shader_dir}/Generated/OctreeConfig.glsl"
        COMMENT "Compiling ProxyIntervalPrepass.comp -> SPIR-V (B2 compute-writer parity)"
        VERBATIM)
    add_custom_target(proxy_interval_prepass_spv
        DEPENDS ${_proxy_interval_vert_spv} ${_proxy_interval_frag_spv}
                ${_proxy_interval_comp_spv})

    add_executable(test_proxy_interval_prepass_device
        Nodes/test_proxy_interval_prepass_device.cpp
    )
    add_dependencies(test_proxy_interval_prepass_device proxy_interval_prepass_spv)
    target_link_libraries(test_proxy_interval_prepass_device PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
    if(TARGET SVO)
        target_link_libraries(test_proxy_interval_prepass_device PRIVATE SVO)
    endif()
    target_compile_definitions(test_proxy_interval_prepass_device PRIVATE
        PROXY_INTERVAL_VERTEX_SPV="${_proxy_interval_vert_spv}"
        PROXY_INTERVAL_FRAGMENT_SPV="${_proxy_interval_frag_spv}"
        PROXY_INTERVAL_COMPUTE_SPV="${_proxy_interval_comp_spv}")
    if(VIXEN_WSL_DZN_ICD)
        target_compile_definitions(test_proxy_interval_prepass_device PRIVATE
            VIXEN_WSL_DZN_ICD="${VIXEN_WSL_DZN_ICD}")
    endif()
    set_target_properties(test_proxy_interval_prepass_device
        PROPERTIES FOLDER "Tests/RenderGraph Tests")
    gtest_discover_tests(test_proxy_interval_prepass_device
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 120)
    message(STATUS "[RenderGraph Tests] Added: test_proxy_interval_prepass_device (Raster-proxy B2 device parity)")

    set(_shadow_ray_trace_src "${_brm_shader_dir}/ShadowRayTrace.comp")
    set(_shadow_ray_trace_spv "${CMAKE_CURRENT_BINARY_DIR}/ShadowRayTrace.spv")
    add_custom_command(
        OUTPUT  ${_shadow_ray_trace_spv}
        COMMAND ${VIXEN_GLSLC}
                -fshader-stage=compute
                -I ${_brm_shader_dir}
                -I ${CMAKE_SOURCE_DIR}/libraries/SVO/shaders
                --target-env=vulkan1.3
                ${_shadow_ray_trace_src}
                -o ${_shadow_ray_trace_spv}
        DEPENDS ${_shadow_ray_trace_src} ${_brm_includes}
        COMMENT "Compiling ShadowRayTrace.comp -> SPIR-V (B1 shadow-mask regression)"
        VERBATIM)
    add_custom_target(shadow_ray_trace_spv DEPENDS ${_shadow_ray_trace_spv})

    add_executable(test_instance_occlusion_cull_device
        Nodes/test_instance_occlusion_cull_device.cpp
    )
    add_dependencies(test_instance_occlusion_cull_device instance_occlusion_cull_spv)
    target_link_libraries(test_instance_occlusion_cull_device PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
    if(TARGET SVO)
        target_link_libraries(test_instance_occlusion_cull_device PRIVATE SVO)
    endif()
    target_compile_definitions(test_instance_occlusion_cull_device PRIVATE
        INSTANCE_OCCLUSION_CULL_SPV="${_cull_spv}")
    if(VIXEN_WSL_DZN_ICD)
        target_compile_definitions(test_instance_occlusion_cull_device PRIVATE VIXEN_WSL_DZN_ICD="${VIXEN_WSL_DZN_ICD}")
    endif()
    set_target_properties(test_instance_occlusion_cull_device PROPERTIES FOLDER "Tests/RenderGraph Tests")
    gtest_discover_tests(test_instance_occlusion_cull_device
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 120)
    message(STATUS "[RenderGraph Tests] Added: test_instance_occlusion_cull_device (Raster-proxy B1 M3 device gate)")
else()
    message(STATUS "[RenderGraph Tests] SKIPPED test_instance_occlusion_cull_device — no glslc runnable on this platform found")
endif()

# ---------------------------------------------------------------------------
# Raster-proxy B1 M4 A/B gate: runs the FULL march(A) -> HiZ -> cull -> march(B)
# chain on one synthetic scene and asserts both the exclusion mechanism AND the
# measured win (>=40% total-iteration reduction, byte-identical pixels). Reuses
# the B1-ON march SPV (body_instance_raymarch_spv_b1, binding 36 declared) plus
# the M2/M3 HiZ + cull SPVs already built above. Same glslc/device-selection
# contract as test_instance_occlusion_cull_device above.
# ---------------------------------------------------------------------------
if(VIXEN_GLSLC)
    add_executable(test_b1_occlusion_ab
        Nodes/test_b1_occlusion_ab.cpp
    )
    add_dependencies(test_b1_occlusion_ab
        body_instance_raymarch_spv_b1
        body_instance_raymarch_spv_b2
        hiz_downsample_spv
        instance_occlusion_cull_spv
        shadow_ray_trace_spv)
    target_link_libraries(test_b1_occlusion_ab PRIVATE ${RENDERGRAPH_TEST_COMMON_LIBS})
    if(TARGET SVO)
        target_link_libraries(test_b1_occlusion_ab PRIVATE SVO)
    endif()
    target_compile_definitions(test_b1_occlusion_ab PRIVATE
        GLSL_RAYMARCH_SPV="${_brm_spv_b1}"
        GLSL_RAYMARCH_B2_SPV="${_brm_spv_b2}"
        HIZ_DOWNSAMPLE_SPV="${_hiz_spv}"
        INSTANCE_OCCLUSION_CULL_SPV="${_cull_spv}"
        SHADOW_RAY_TRACE_SPV="${_shadow_ray_trace_spv}")
    if(VIXEN_WSL_DZN_ICD)
        target_compile_definitions(test_b1_occlusion_ab PRIVATE VIXEN_WSL_DZN_ICD="${VIXEN_WSL_DZN_ICD}")
    endif()
    set_target_properties(test_b1_occlusion_ab PROPERTIES FOLDER "Tests/RenderGraph Tests")
    gtest_discover_tests(test_b1_occlusion_ab
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 120)
    message(STATUS "[RenderGraph Tests] Added: test_b1_occlusion_ab (Raster-proxy B1 M4 A/B gate)")
else()
    message(STATUS "[RenderGraph Tests] SKIPPED test_b1_occlusion_ab — no glslc runnable on this platform found")
endif()
