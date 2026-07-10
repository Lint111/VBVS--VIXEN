cmake_minimum_required(VERSION 3.15)
project(SVOTests)

# Include Google Test helpers (provided by FetchContent in the top-level CMake)
include(GoogleTest)

# ===========================================================================
# Basic SVO Builder Tests
# ===========================================================================
# Tests mesh-based octree construction, triangle filtering, SAT

add_executable(test_svo_builder
    test_svo_builder.cpp
)

if(TARGET SVO)
    target_link_libraries(test_svo_builder PRIVATE GTest::gtest_main SVO)
else()
    target_link_libraries(test_svo_builder PRIVATE GTest::gtest_main)
endif()

# Visual Studio solution folder organization
set_target_properties(test_svo_builder PROPERTIES FOLDER "Tests/SVO Tests")

# Copy TBB DLLs to test executable directory for runtime
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_svo_builder POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb>
            $<TARGET_FILE_DIR:test_svo_builder>
        COMMENT "Copying TBB DLL for test_svo_builder"
    )
endif()

gtest_discover_tests(test_svo_builder)

# ===========================================================================
# Voxel Injection Tests
# ===========================================================================
# Tests voxel-based octree construction, procedural samplers

add_executable(test_voxel_injection
    test_voxel_injection.cpp
)

if(TARGET SVO)
    target_link_libraries(test_voxel_injection PRIVATE GTest::gtest_main SVO GaiaVoxelWorld)
else()
    target_link_libraries(test_voxel_injection PRIVATE GTest::gtest_main)
endif()

set_target_properties(test_voxel_injection PROPERTIES FOLDER "Tests/SVO Tests")

# Copy TBB DLLs to test executable directory for runtime
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_voxel_injection POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb>
            $<TARGET_FILE_DIR:test_voxel_injection>
        COMMENT "Copying TBB DLL for test_voxel_injection"
    )
endif()

gtest_discover_tests(test_voxel_injection)

# ===========================================================================
# Data Structures Tests
# ===========================================================================
# Tests ChildDescriptor, Contour encoding/decoding, attributes

add_executable(test_svo_types
    test_svo_types.cpp
)

if(TARGET SVO)
    target_link_libraries(test_svo_types PRIVATE GTest::gtest_main SVO)
else()
    target_link_libraries(test_svo_types PRIVATE GTest::gtest_main)
endif()

set_target_properties(test_svo_types PROPERTIES FOLDER "Tests/SVO Tests")

# Copy TBB DLLs to test executable directory for runtime
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_svo_types POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb>
            $<TARGET_FILE_DIR:test_svo_types>
        COMMENT "Copying TBB DLL for test_svo_types"
    )
endif()

gtest_discover_tests(test_svo_types)

add_executable(test_sdf_recipes
    test_sdf_recipes.cpp
)
if(TARGET SVO)
    target_link_libraries(test_sdf_recipes PRIVATE GTest::gtest_main SVO)
else()
    target_link_libraries(test_sdf_recipes PRIVATE GTest::gtest_main)
endif()
gtest_discover_tests(test_sdf_recipes)

# ===========================================================================
# SoA-SDF Serialize Tests  (Inc2 M2 — SoA-SDF brick + grid-lookup + descriptor)
# ===========================================================================
add_executable(test_soa_sdf_serialize test_soa_sdf_serialize.cpp)
if(TARGET SVO)
    target_link_libraries(test_soa_sdf_serialize PRIVATE
        GTest::gtest_main SVO GaiaVoxelWorld VoxelComponents VoxelData)
else()
    target_link_libraries(test_soa_sdf_serialize PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_soa_sdf_serialize PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_soa_sdf_serialize POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_soa_sdf_serialize>
        COMMENT "Copying TBB DLL for test_soa_sdf_serialize")
endif()
gtest_discover_tests(test_soa_sdf_serialize)

# ===========================================================================
# SoA-Mip Serialize Tests  (Sparse-Mip ESVO LOD Inc1 M1 Task 3 — MipPool
# round-trip through SerializedOctree::mipPool + ConcatenatedOctrees::mipPool)
# ===========================================================================
add_executable(test_soa_mip_serialize test_soa_mip_serialize.cpp)
if(TARGET SVO)
    target_link_libraries(test_soa_mip_serialize PRIVATE
        GTest::gtest_main SVO GaiaVoxelWorld VoxelComponents VoxelData)
else()
    target_link_libraries(test_soa_mip_serialize PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_soa_mip_serialize PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_soa_mip_serialize POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_soa_mip_serialize>
        COMMENT "Copying TBB DLL for test_soa_mip_serialize")
endif()
gtest_discover_tests(test_soa_mip_serialize)

# ===========================================================================
# TierRefTable Tests  (Tiered ESVO Inc2 M1 Tasks 2-3 — TierRefTable
# concatenation bookkeeping on ConcatenatedOctrees + OctreeConfig's new
# tierRefTableBase field, mirroring test_soa_mip_serialize's mipPool pattern)
# ===========================================================================
add_executable(test_tier_ref_table test_tier_ref_table.cpp)
if(TARGET SVO)
    target_link_libraries(test_tier_ref_table PRIVATE
        GTest::gtest_main SVO GaiaVoxelWorld VoxelComponents VoxelData)
else()
    target_link_libraries(test_tier_ref_table PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_tier_ref_table PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_tier_ref_table POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_tier_ref_table>
        COMMENT "Copying TBB DLL for test_tier_ref_table")
endif()
gtest_discover_tests(test_tier_ref_table)

# ===========================================================================
# Tier-Crossing Construction Tests  (Tiered ESVO Inc2 M2 Tasks 4-5 —
# MarkLeafAsTierCrossing's farBit=1 construction path + the two-tree
# hand-authored round-trip fixture proving a marked leaf's TierRef resolves
# to the child tree's real ConcatenatedOctrees slot)
# ===========================================================================
add_executable(test_tier_crossing_construction test_tier_crossing_construction.cpp)
if(TARGET SVO)
    target_link_libraries(test_tier_crossing_construction PRIVATE
        GTest::gtest_main SVO GaiaVoxelWorld VoxelComponents VoxelData)
else()
    target_link_libraries(test_tier_crossing_construction PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_tier_crossing_construction PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_tier_crossing_construction POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_tier_crossing_construction>
        COMMENT "Copying TBB DLL for test_tier_crossing_construction")
endif()
gtest_discover_tests(test_tier_crossing_construction)

# ===========================================================================
# Tier-Crossing Mirror Parity Tests  (Tiered ESVO Inc2 M4 Task 10 sync — a
# cheap win flagged by the M3 Opus validator addendum: no automated parity
# test previously exercised GpuTraversalMirror's crossing-restart path at
# all. Proves the mirror's own castRay()/RegisterTierCrossingChild composes
# correctly with a resident vs. non-resident child config.)
# ===========================================================================
add_executable(test_tier_crossing_mirror_parity test_tier_crossing_mirror_parity.cpp)
if(TARGET SVO)
    target_link_libraries(test_tier_crossing_mirror_parity PRIVATE
        GTest::gtest_main SVO GaiaVoxelWorld VoxelComponents VoxelData)
else()
    target_link_libraries(test_tier_crossing_mirror_parity PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_tier_crossing_mirror_parity PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_tier_crossing_mirror_parity POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_tier_crossing_mirror_parity>
        COMMENT "Copying TBB DLL for test_tier_crossing_mirror_parity")
endif()
gtest_discover_tests(test_tier_crossing_mirror_parity)

# ===========================================================================
# Stored-SDF March Mirror Tests  (gpu-shader-debug — CPU mirror of StoredSdf.glsl
# marchBrickSdf + BodyInstanceRayMarch.comp handleLeafHitInstancedSdf, reproducing
# the GPU's brick-aligned holes on the CPU for root-cause debugging)
# ===========================================================================
add_executable(test_stored_sdf_march_mirror test_stored_sdf_march_mirror.cpp)
if(TARGET SVO)
    target_link_libraries(test_stored_sdf_march_mirror PRIVATE
        GTest::gtest_main SVO GaiaVoxelWorld VoxelComponents VoxelData)
else()
    target_link_libraries(test_stored_sdf_march_mirror PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_stored_sdf_march_mirror PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_stored_sdf_march_mirror POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_stored_sdf_march_mirror>
        COMMENT "Copying TBB DLL for test_stored_sdf_march_mirror")
endif()
gtest_discover_tests(test_stored_sdf_march_mirror)

# ===========================================================================
# Channel Format Tests  (Inc3 M1 — VIXEN channel vocabulary + OctreeConfig table)
# ===========================================================================
add_executable(test_channel_format test_channel_format.cpp)
if(TARGET SVO)
    target_link_libraries(test_channel_format PRIVATE
        GTest::gtest_main SVO)
else()
    target_link_libraries(test_channel_format PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_channel_format PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_channel_format POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_channel_format>
        COMMENT "Copying TBB DLL for test_channel_format")
endif()
gtest_discover_tests(test_channel_format)

# ===========================================================================
# Mip Sample Filter Tests  (Sparse-Mip ESVO LOD Inc1 M1 Task 1 — pure math,
# no octree needed: weighted-mean color/roughness + min-magnitude SDF filter)
# ===========================================================================
add_executable(test_mip_sample_filter test_mip_sample_filter.cpp)
if(TARGET SVO)
    target_link_libraries(test_mip_sample_filter PRIVATE
        GTest::gtest_main SVO)
else()
    target_link_libraries(test_mip_sample_filter PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_mip_sample_filter PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_mip_sample_filter POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_mip_sample_filter>
        COMMENT "Copying TBB DLL for test_mip_sample_filter")
endif()
gtest_discover_tests(test_mip_sample_filter)

# ===========================================================================
# Mip Sample Bake Tests  (Sparse-Mip ESVO LOD Inc1 M1 Task 2 — bottom-up
# bake-time fill of MipPool from an already-built Octree + SerializeSdf output)
# ===========================================================================
add_executable(test_mip_sample_bake test_mip_sample_bake.cpp)
if(TARGET SVO)
    target_link_libraries(test_mip_sample_bake PRIVATE
        GTest::gtest_main SVO GaiaVoxelWorld VoxelComponents VoxelData)
else()
    target_link_libraries(test_mip_sample_bake PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_mip_sample_bake PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_mip_sample_bake POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_mip_sample_bake>
        COMMENT "Copying TBB DLL for test_mip_sample_bake")
endif()
gtest_discover_tests(test_mip_sample_bake)

# ===========================================================================
# Resolvable-Level Formula Tests  (Sparse-Mip ESVO LOD Inc1 M4a Task 10 part 1
# — pure math, no octree/GPU needed: minResolvableLevel(distance, fovRadians,
# screenHeightPx, leafSize_m, pxThreshold), both distance- and FOV-driven axes)
# ===========================================================================
add_executable(test_resolvable_level test_resolvable_level.cpp)
if(TARGET SVO)
    target_link_libraries(test_resolvable_level PRIVATE
        GTest::gtest_main SVO)
else()
    target_link_libraries(test_resolvable_level PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_resolvable_level PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_resolvable_level POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_resolvable_level>
        COMMENT "Copying TBB DLL for test_resolvable_level")
endif()
gtest_discover_tests(test_resolvable_level)

# ===========================================================================
# Frustum Cull Tests  (Sparse-Mip ESVO LOD Inc1 M4b Task 10 part 2, frustum piece
# — pure math, no octree/GPU needed: BuildFrustum/SphereIntersectsFrustum, plus
# the residency hysteresis-pad constant)
# ===========================================================================
add_executable(test_frustum_cull test_frustum_cull.cpp)
if(TARGET SVO)
    target_link_libraries(test_frustum_cull PRIVATE
        GTest::gtest_main SVO)
else()
    target_link_libraries(test_frustum_cull PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_frustum_cull PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_frustum_cull POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_frustum_cull>
        COMMENT "Copying TBB DLL for test_frustum_cull")
endif()
gtest_discover_tests(test_frustum_cull)

# ===========================================================================
# TierAddress Tests  (Tiered ESVO Inc1 M1 Task 1 — pure type/math, no
# octree/GPU needed: hop-chain construction + shared-prefix comparison)
# ===========================================================================
add_executable(test_tier_address test_tier_address.cpp)
if(TARGET SVO)
    target_link_libraries(test_tier_address PRIVATE
        GTest::gtest_main SVO)
else()
    target_link_libraries(test_tier_address PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_tier_address PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_tier_address POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_tier_address>
        COMMENT "Copying TBB DLL for test_tier_address")
endif()
gtest_discover_tests(test_tier_address)

# ===========================================================================
# Tier Math Tests  (Tiered ESVO Inc1 M1 Task 2 — pure math, no octree/GPU
# needed: tier-index -> linear scale-range table, T0/T1/T2/System/Galaxy)
# ===========================================================================
add_executable(test_tier_math test_tier_math.cpp)
if(TARGET SVO)
    target_link_libraries(test_tier_math PRIVATE
        GTest::gtest_main SVO)
else()
    target_link_libraries(test_tier_math PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_tier_math PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_tier_math POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_tier_math>
        COMMENT "Copying TBB DLL for test_tier_math")
endif()
gtest_discover_tests(test_tier_math)

# ===========================================================================
# TierRef Tests  (Tiered ESVO Inc2 M1 Task 1 — pure data/layout, no
# octree/GPU needed: field round-trip + std430-safe byte layout proof for the
# tier-crossing-leaf indirect payload)
# ===========================================================================
add_executable(test_tier_ref test_tier_ref.cpp)
if(TARGET SVO)
    target_link_libraries(test_tier_ref PRIVATE
        GTest::gtest_main SVO)
else()
    target_link_libraries(test_tier_ref PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_tier_ref PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_tier_ref POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_tier_ref>
        COMMENT "Copying TBB DLL for test_tier_ref")
endif()
gtest_discover_tests(test_tier_ref)

# ===========================================================================
# Tier Direction Tests  (Tiered ESVO Inc1 M2 Task 3 — pure math, no
# octree/GPU needed: shared-prefix-composed direction/distance between two
# TierAddress values, via synthetic per-hop TierHopFrame fixtures)
# ===========================================================================
add_executable(test_tier_direction test_tier_direction.cpp)
if(TARGET SVO)
    target_link_libraries(test_tier_direction PRIVATE
        GTest::gtest_main SVO)
else()
    target_link_libraries(test_tier_direction PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_tier_direction PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_tier_direction POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_tier_direction>
        COMMENT "Copying TBB DLL for test_tier_direction")
endif()
gtest_discover_tests(test_tier_direction)

# ===========================================================================
# Tier Magnitude Tests  (Tiered ESVO Inc1 M2 Task 4 — pure math, no
# octree/GPU needed: apparent-magnitude falloff + optional light-delay
# staleness no-op stub)
# ===========================================================================
add_executable(test_tier_magnitude test_tier_magnitude.cpp)
if(TARGET SVO)
    target_link_libraries(test_tier_magnitude PRIVATE
        GTest::gtest_main SVO)
else()
    target_link_libraries(test_tier_magnitude PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_tier_magnitude PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_tier_magnitude POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_tier_magnitude>
        COMMENT "Copying TBB DLL for test_tier_magnitude")
endif()
gtest_discover_tests(test_tier_magnitude)

# ===========================================================================
# Instance Sort Tests  (Sparse-Mip ESVO LOD Inc1 M4b Task 10 part 2, occlusion piece
# — pure math, no octree/GPU needed: SortInstancesFrontToBack)
# ===========================================================================
add_executable(test_instance_sort test_instance_sort.cpp)
if(TARGET SVO)
    target_link_libraries(test_instance_sort PRIVATE
        GTest::gtest_main SVO)
else()
    target_link_libraries(test_instance_sort PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_instance_sort PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_instance_sort POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_instance_sort>
        COMMENT "Copying TBB DLL for test_instance_sort")
endif()
gtest_discover_tests(test_instance_sort)

# ===========================================================================
# Residency Trigger Tests  (Sparse-Mip ESVO LOD Inc1 M4c Task 10 part 3 —
# pure math, no octree/GPU needed: InstanceWantsBrickResidency, the combined
# M4a+M4b trigger, three scenarios — distance/zoom/orientation-driven)
# ===========================================================================
add_executable(test_residency_trigger test_residency_trigger.cpp)
if(TARGET SVO)
    target_link_libraries(test_residency_trigger PRIVATE
        GTest::gtest_main SVO)
else()
    target_link_libraries(test_residency_trigger PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_residency_trigger PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_residency_trigger POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_residency_trigger>
        COMMENT "Copying TBB DLL for test_residency_trigger")
endif()
gtest_discover_tests(test_residency_trigger)

# ===========================================================================
# CPU-Side Residency Occlusion Gate Tests  (Sparse-Mip ESVO LOD Inc2 M3 — deferred
# from Inc1 M4b — pure math, no octree/GPU needed: RaySphereNearestHit,
# IsOccludedByResidentTrees; three-body camera->occluder->target line-up)
# ===========================================================================
add_executable(test_occlusion_gate test_occlusion_gate.cpp)
if(TARGET SVO)
    target_link_libraries(test_occlusion_gate PRIVATE
        GTest::gtest_main SVO)
else()
    target_link_libraries(test_occlusion_gate PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_occlusion_gate PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_occlusion_gate POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_occlusion_gate>
        COMMENT "Copying TBB DLL for test_occlusion_gate")
endif()
gtest_discover_tests(test_occlusion_gate)

# ===========================================================================
# SDF Bake Tests  (Inc2 M1 — recipe → narrow-band SDF voxels + body octree)
# ===========================================================================
add_executable(test_sdf_bake test_sdf_bake.cpp)
if(TARGET SVO)
    target_link_libraries(test_sdf_bake PRIVATE
        GTest::gtest_main SVO GaiaVoxelWorld VoxelComponents VoxelData)
else()
    target_link_libraries(test_sdf_bake PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_sdf_bake PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_sdf_bake POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_sdf_bake>
        COMMENT "Copying TBB DLL for test_sdf_bake")
endif()
gtest_discover_tests(test_sdf_bake)

# ===========================================================================
# Shell Derive Tests (Surface-Shell ESVO cache — CPU classifier source of truth)
# ===========================================================================
add_executable(test_shell_derive test_shell_derive.cpp)
if(TARGET SVO)
    target_link_libraries(test_shell_derive PRIVATE
        GTest::gtest_main SVO GaiaVoxelWorld VoxelComponents VoxelData)
else()
    target_link_libraries(test_shell_derive PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_shell_derive PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_shell_derive POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_shell_derive>
        COMMENT "Copying TBB DLL for test_shell_derive")
endif()
gtest_discover_tests(test_shell_derive)

# ===========================================================================
# Recipe Codegen Tests (P2.2 M1 — instruction→HLSL emitter + SPIR-V compile gate)
# ===========================================================================
add_executable(test_recipe_codegen test_recipe_codegen.cpp)
if(TARGET SVO AND TARGET ShaderManagement)
    target_link_libraries(test_recipe_codegen PRIVATE
        GTest::gtest_main SVO ShaderManagement)
elseif(TARGET SVO)
    target_link_libraries(test_recipe_codegen PRIVATE
        GTest::gtest_main SVO)
else()
    target_link_libraries(test_recipe_codegen PRIVATE GTest::gtest_main)
endif()
target_compile_definitions(test_recipe_codegen PRIVATE
    SDF_CORE_KERNELS_HLSL_PATH="${CMAKE_SOURCE_DIR}/libraries/SVO/shaders/recipe/SdfCoreKernels.g.hlsl"
)
set_target_properties(test_recipe_codegen PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_recipe_codegen POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_recipe_codegen>
        COMMENT "Copying TBB DLL for test_recipe_codegen")
endif()
gtest_discover_tests(test_recipe_codegen)

# ===========================================================================
# SdfCore GLSL name-set drift-guard (Lazy-Procedural-Delta-Baseline Inc1 M4 Task 7)
# ===========================================================================
add_executable(test_sdf_core_glsl test_sdf_core_glsl.cpp)
target_link_libraries(test_sdf_core_glsl PRIVATE GTest::gtest_main)
target_compile_definitions(test_sdf_core_glsl PRIVATE
    SDF_CORE_KERNELS_HLSL_PATH="${CMAKE_SOURCE_DIR}/libraries/SVO/shaders/recipe/SdfCoreKernels.g.hlsl"
    SDF_CORE_KERNELS_GLSL_PATH="${CMAKE_SOURCE_DIR}/libraries/SVO/shaders/recipe/SdfCoreKernels.glsl"
)
set_target_properties(test_sdf_core_glsl PROPERTIES FOLDER "Tests/SVO Tests")
gtest_discover_tests(test_sdf_core_glsl)

# ===========================================================================
# GLSL field-function emitter smoke tests (Inc1 M4 Task 8)
# ===========================================================================
add_executable(test_recipe_codegen_glsl test_recipe_codegen_glsl.cpp)
if(TARGET SVO)
    target_link_libraries(test_recipe_codegen_glsl PRIVATE GTest::gtest_main SVO)
else()
    target_link_libraries(test_recipe_codegen_glsl PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_recipe_codegen_glsl PROPERTIES FOLDER "Tests/SVO Tests")
gtest_discover_tests(test_recipe_codegen_glsl)

# ===========================================================================
# Recipe GLSL Numerical Parity Tests (Lazy-Procedural-Delta-Baseline Inc1 M4
# Task 9 — GLSL field-function emitter vs CPU evalRecipe, across the full
# ParityCorpus. CPU-eval + glslang-compile run on every machine; the actual
# GPU dispatch+compare only runs on a REAL (non-lavapipe, non-Dozen) GPU —
# the fixture SetUp() GTEST_SKIPs otherwise. See file header for the
# Windows-native hand-off command.)
# ===========================================================================
add_executable(test_recipe_glsl_numerical_parity test_recipe_glsl_numerical_parity.cpp)
if(TARGET SVO AND TARGET ShaderManagement AND TARGET VulkanResources)
    target_link_libraries(test_recipe_glsl_numerical_parity PRIVATE
        GTest::gtest_main SVO ShaderManagement VulkanResources)
elseif(TARGET SVO AND TARGET ShaderManagement)
    target_link_libraries(test_recipe_glsl_numerical_parity PRIVATE
        GTest::gtest_main SVO ShaderManagement)
else()
    target_link_libraries(test_recipe_glsl_numerical_parity PRIVATE GTest::gtest_main)
endif()
target_compile_definitions(test_recipe_glsl_numerical_parity PRIVATE
    SDF_CORE_KERNELS_GLSL_PATH="${CMAKE_SOURCE_DIR}/libraries/SVO/shaders/recipe/SdfCoreKernels.glsl"
)
set_target_properties(test_recipe_glsl_numerical_parity PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_recipe_glsl_numerical_parity POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_recipe_glsl_numerical_parity>
        COMMENT "Copying TBB DLL for test_recipe_glsl_numerical_parity")
endif()
gtest_discover_tests(test_recipe_glsl_numerical_parity)

# ===========================================================================
# Recipe Bake Parity Tests (P2.1 M1 — recipe-sphere bake == analytic-sphere bake)
# ===========================================================================
add_executable(test_recipe_bake test_recipe_bake.cpp)
if(TARGET SVO)
    target_link_libraries(test_recipe_bake PRIVATE
        GTest::gtest_main SVO GaiaVoxelWorld VoxelComponents VoxelData)
else()
    target_link_libraries(test_recipe_bake PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_recipe_bake PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_recipe_bake POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_recipe_bake>
        COMMENT "Copying TBB DLL for test_recipe_bake")
endif()
gtest_discover_tests(test_recipe_bake)

# ===========================================================================
# Recipe Bake Center Test (Inc2a Task 1 — proves BakeRecipeInstructionsToSdfWorld
# honors `center`, mirroring evalSdf's existing p - center convention)
# ===========================================================================
add_executable(test_recipe_bake_center test_recipe_bake_center.cpp)
if(TARGET SVO)
    target_link_libraries(test_recipe_bake_center PRIVATE
        GTest::gtest_main SVO GaiaVoxelWorld VoxelComponents VoxelData)
else()
    target_link_libraries(test_recipe_bake_center PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_recipe_bake_center PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_recipe_bake_center POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_recipe_bake_center>
        COMMENT "Copying TBB DLL for test_recipe_bake_center")
endif()
gtest_discover_tests(test_recipe_bake_center)

# ===========================================================================
# Recipe Eval Parity Tests (P0 T4 — CPU stack-VM parity vs analytic golden)
# ===========================================================================
add_executable(test_recipe_eval_parity test_recipe_eval_parity.cpp)
if(TARGET SVO)
    target_link_libraries(test_recipe_eval_parity PRIVATE GTest::gtest_main SVO)
else()
    target_link_libraries(test_recipe_eval_parity PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_recipe_eval_parity PROPERTIES FOLDER "Tests/SVO Tests")
gtest_discover_tests(test_recipe_eval_parity)

# ===========================================================================
# Recipe Container Parity Test (I1.6 — C# writer blob round-trips via generated C++ reader)
# Fixture bytes come from C# RecipeContainer.Serialize via the committed Yeroket dumper.
# ===========================================================================
add_executable(test_recipe_container_parity test_recipe_container_parity.cpp)
if(TARGET SVO)
    target_link_libraries(test_recipe_container_parity PRIVATE GTest::gtest_main SVO)
else()
    target_link_libraries(test_recipe_container_parity PRIVATE GTest::gtest_main)
endif()
target_compile_definitions(test_recipe_container_parity PRIVATE
    RECIPE_CONTAINER_FIXTURE_PATH="${CMAKE_SOURCE_DIR}/libraries/SVO/tests/fixtures/recipe-container/sphere26.vrc"
)
set_target_properties(test_recipe_container_parity PROPERTIES FOLDER "Tests/SVO Tests")
gtest_discover_tests(test_recipe_container_parity)

# ===========================================================================
# Recipe Registry Tests (I2.1+I2.2 — RecipeRegistry core + StackOverflow guard)
# ===========================================================================
add_executable(test_recipe_registry test_recipe_registry.cpp)
if(TARGET SVO)
    target_link_libraries(test_recipe_registry PRIVATE GTest::gtest_main SVO)
else()
    target_link_libraries(test_recipe_registry PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_recipe_registry PROPERTIES FOLDER "Tests/SVO Tests")
gtest_discover_tests(test_recipe_registry)

# ===========================================================================
# Recipe Bounds Tests (Lazy-Procedural-Delta-Baseline Inc0 M5 Task 10 —
# whitelist-restricted bound derivation + RecipeEntry bounds-metadata validation)
# ===========================================================================
add_executable(test_recipe_bounds test_recipe_bounds.cpp)
if(TARGET SVO)
    target_link_libraries(test_recipe_bounds PRIVATE GTest::gtest_main SVO)
else()
    target_link_libraries(test_recipe_bounds PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_recipe_bounds PROPERTIES FOLDER "Tests/SVO Tests")
gtest_discover_tests(test_recipe_bounds)

# ===========================================================================
# Recipe Manifest Tests (I2.3 — RecipeManifest model + validation)
# ===========================================================================
add_executable(test_recipe_manifest test_recipe_manifest.cpp)
if(TARGET SVO)
    target_link_libraries(test_recipe_manifest PRIVATE GTest::gtest_main SVO)
else()
    target_link_libraries(test_recipe_manifest PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_recipe_manifest PROPERTIES FOLDER "Tests/SVO Tests")
gtest_discover_tests(test_recipe_manifest)

# ===========================================================================
# Recipe Ingest Tests (I2.4 — IngestBlob consumes the vendored reader)
# ===========================================================================
add_executable(test_recipe_ingest test_recipe_ingest.cpp)
if(TARGET SVO)
    target_link_libraries(test_recipe_ingest PRIVATE GTest::gtest_main SVO)
else()
    target_link_libraries(test_recipe_ingest PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_recipe_ingest PROPERTIES FOLDER "Tests/SVO Tests")
gtest_discover_tests(test_recipe_ingest)

# ===========================================================================
# Recipe Boot Ingest Tests (Spec B I3/Task 6 — host packed buffer -> registry+bake, fail-loud)
# ===========================================================================
add_executable(test_recipe_boot_ingest test_recipe_boot_ingest.cpp)
if(TARGET SVO)
    target_link_libraries(test_recipe_boot_ingest PRIVATE GTest::gtest_main SVO)
else()
    target_link_libraries(test_recipe_boot_ingest PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_recipe_boot_ingest PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_recipe_boot_ingest POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_recipe_boot_ingest>
        COMMENT "Copying TBB DLL for test_recipe_boot_ingest")
endif()
gtest_discover_tests(test_recipe_boot_ingest)

# ===========================================================================
# Recipe Pack Loader Tests (I2.5 — manifest+blobs->registry, fail-loud)
# ===========================================================================
add_executable(test_recipe_pack_loader test_recipe_pack_loader.cpp)
if(TARGET SVO)
    target_link_libraries(test_recipe_pack_loader PRIVATE GTest::gtest_main SVO)
else()
    target_link_libraries(test_recipe_pack_loader PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_recipe_pack_loader PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_recipe_pack_loader POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_recipe_pack_loader>
        COMMENT "Copying TBB DLL for test_recipe_pack_loader")
endif()
gtest_discover_tests(test_recipe_pack_loader)

# ===========================================================================
# Voxel Document Flatten Tests (Voxel-Authoring Inc1 M3 — VoxelDocumentFlattener,
# document -> VRC1 recipe blob, consumable unchanged by the existing registry/pool path)
# ===========================================================================
add_executable(test_voxel_document_flatten test_voxel_document_flatten.cpp)
if(TARGET SVO)
    target_link_libraries(test_voxel_document_flatten PRIVATE GTest::gtest_main SVO)
else()
    target_link_libraries(test_voxel_document_flatten PRIVATE GTest::gtest_main)
endif()
target_compile_definitions(test_voxel_document_flatten PRIVATE
    SAMPLE_TRI_LAYER_VXD_PATH="${CMAKE_SOURCE_DIR}/BuiltAssets/documents/sample_tri_layer.vxd"
)
set_target_properties(test_voxel_document_flatten PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_voxel_document_flatten POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_voxel_document_flatten>
        COMMENT "Copying TBB DLL for test_voxel_document_flatten")
endif()
gtest_discover_tests(test_voxel_document_flatten)

# ===========================================================================
# Octree Pool Tests (I3.1 — dynamic-N pool + I3.2 config-size formula)
# ===========================================================================
add_executable(test_octree_pool test_octree_pool.cpp)
if(TARGET SVO)
    target_link_libraries(test_octree_pool PRIVATE
        GTest::gtest_main SVO GaiaVoxelWorld VoxelComponents VoxelData)
else()
    target_link_libraries(test_octree_pool PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_octree_pool PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_octree_pool POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_octree_pool>
        COMMENT "Copying TBB DLL for test_octree_pool")
endif()
gtest_discover_tests(test_octree_pool)

# ===========================================================================
# Recipe Baker Tests (I3.3 — BakeRegistryToPool, memory-budgeted fail-loud)
# ===========================================================================
add_executable(test_recipe_baker test_recipe_baker.cpp)
if(TARGET SVO)
    target_link_libraries(test_recipe_baker PRIVATE
        GTest::gtest_main SVO GaiaVoxelWorld VoxelComponents VoxelData)
else()
    target_link_libraries(test_recipe_baker PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_recipe_baker PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_recipe_baker POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_recipe_baker>
        COMMENT "Copying TBB DLL for test_recipe_baker")
endif()
gtest_discover_tests(test_recipe_baker)

# ===========================================================================
# Generation-Cost Benchmark (Lazy-Procedural-Delta-Baseline Inc0 M3, Task 6)
# ===========================================================================
add_executable(test_generation_cost_benchmark test_generation_cost_benchmark.cpp)
if(TARGET SVO)
    target_link_libraries(test_generation_cost_benchmark PRIVATE
        GTest::gtest_main SVO GaiaVoxelWorld VoxelComponents VoxelData)
else()
    target_link_libraries(test_generation_cost_benchmark PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_generation_cost_benchmark PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_generation_cost_benchmark POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_generation_cost_benchmark>
        COMMENT "Copying TBB DLL for test_generation_cost_benchmark")
endif()
gtest_discover_tests(test_generation_cost_benchmark)

add_executable(test_residency_default test_residency_default.cpp)
if(TARGET SVO)
    target_link_libraries(test_residency_default PRIVATE
        GTest::gtest_main SVO GaiaVoxelWorld VoxelComponents VoxelData)
else()
    target_link_libraries(test_residency_default PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_residency_default PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_residency_default POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_residency_default>
        COMMENT "Copying TBB DLL for test_residency_default")
endif()
gtest_discover_tests(test_residency_default)

# ===========================================================================
# Sampler Tests
# ===========================================================================
# Tests NoiseSampler, SDFSampler, HeightmapSampler

add_executable(test_samplers
    test_samplers.cpp
)

if(TARGET SVO)
    target_link_libraries(test_samplers PRIVATE GTest::gtest_main SVO)
else()
    target_link_libraries(test_samplers PRIVATE GTest::gtest_main)
endif()

set_target_properties(test_samplers PROPERTIES FOLDER "Tests/SVO Tests")

# Copy TBB DLLs to test executable directory for runtime
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_samplers POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb>
            $<TARGET_FILE_DIR:test_samplers>
        COMMENT "Copying TBB DLL for test_samplers"
    )
endif()

gtest_discover_tests(test_samplers)

# ===========================================================================
# Octree Query Interface Tests
# ===========================================================================
# Tests voxelExists, getVoxelData, getChildMask, castRay, castRayLOD

add_executable(test_octree_queries
    test_octree_queries.cpp
)

if(TARGET SVO)
    target_link_libraries(test_octree_queries PRIVATE GTest::gtest_main SVO GaiaVoxelWorld VoxelComponents)
else()
    target_link_libraries(test_octree_queries PRIVATE GTest::gtest_main)
endif()

set_target_properties(test_octree_queries PROPERTIES FOLDER "Tests/SVO Tests")

# Copy TBB DLLs to test executable directory for runtime
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_octree_queries POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb>
            $<TARGET_FILE_DIR:test_octree_queries>
        COMMENT "Copying TBB DLL for test_octree_queries"
    )
endif()

gtest_discover_tests(test_octree_queries)

# ===========================================================================
# BrickView Tests (Converted from Legacy BrickStorage)
# ===========================================================================
# Tests AttributeRegistry + BrickView integration:
# - Brick allocation and indexing
# - Multi-attribute access (Float, Uint32, Vec3)
# - 3D coordinate API (setAt3D / getAt3D)
# - Pointer-based access (zero-cost path)
# - Index-based access (AttributeIndex for O(1) lookup)
# - Data isolation between bricks
# - Backward compatibility (string-based lookup)

add_executable(test_brick_view
    test_brick_view.cpp
)

if(TARGET VoxelData)
    target_link_libraries(test_brick_view PRIVATE GTest::gtest_main VoxelData)
else()
    target_link_libraries(test_brick_view PRIVATE GTest::gtest_main)
endif()

set_target_properties(test_brick_view PROPERTIES FOLDER "Tests/VoxelData Tests")

# Copy TBB DLLs to test executable directory for runtime
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_brick_view POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb>
            $<TARGET_FILE_DIR:test_brick_view>
        COMMENT "Copying TBB DLL for test_brick_view"
    )
endif()

gtest_discover_tests(test_brick_view)

# ===========================================================================
# EntityBrickView Tests (Spatial View Pattern)
# ===========================================================================
# Tests entity-based brick view pattern (SVO spatial indexing concern):
# - Zero-copy entity span access over 8³ brick regions
# - Linear and 3D coordinate entity access
# - Component access (density, color, normal, material)
# - Span iteration (std::span over 512 entities)
# - Utility functions (countSolidVoxels, isEmpty, isFull)
# - Memory efficiency validation (4 KB entity refs vs 70 KB data copies)

add_executable(test_entity_brick_view
    test_entity_brick_view.cpp
)

target_link_libraries(test_entity_brick_view PRIVATE
    GTest::gtest_main
    GaiaVoxelWorld
    VoxelData
)

set_target_properties(test_entity_brick_view PROPERTIES FOLDER "Tests/SVO Tests")

# Copy TBB DLLs to test executable directory for runtime
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_entity_brick_view POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb>
            $<TARGET_FILE_DIR:test_entity_brick_view>
        COMMENT "Copying TBB DLL for test_entity_brick_view"
    )
endif()

gtest_discover_tests(test_entity_brick_view)

# ===========================================================================
# Comprehensive Ray Casting Tests
# ===========================================================================
# Tests axis-aligned rays, diagonal rays, inside/outside grid, miss cases,
# multiple voxel traversal, dense volumes, edge cases, stress testing,
# performance characteristics, and Cornell box scene rendering

add_executable(test_ray_casting_comprehensive
    test_ray_casting_comprehensive.cpp
)

if(TARGET SVO)
    target_link_libraries(test_ray_casting_comprehensive PRIVATE GTest::gtest_main SVO)
else()
    target_link_libraries(test_ray_casting_comprehensive PRIVATE GTest::gtest_main)
endif()

set_target_properties(test_ray_casting_comprehensive PROPERTIES FOLDER "Tests/SVO Tests")

# Copy TBB DLLs to test executable directory for runtime
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_ray_casting_comprehensive POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb>
            $<TARGET_FILE_DIR:test_ray_casting_comprehensive>
        COMMENT "Copying TBB DLL for test_ray_casting_comprehensive"
    )
endif()

gtest_discover_tests(test_ray_casting_comprehensive)

# ===========================================================================
# Brick Creation Tests
# ===========================================================================
# Tests that bricks are properly allocated and populated during voxel injection,
# and that ray casting finds and traverses them (using GaiaVoxelWorld API)

add_executable(test_brick_creation
    test_brick_creation.cpp
)

target_link_libraries(test_brick_creation PRIVATE
    GTest::gtest_main
    SVO
    GaiaVoxelWorld
    VoxelComponents
)

set_target_properties(test_brick_creation PROPERTIES FOLDER "Tests/SVO Tests")

# Copy TBB DLLs to test executable directory for runtime
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_brick_creation POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb>
            $<TARGET_FILE_DIR:test_brick_creation>
        COMMENT "Copying TBB DLL for test_brick_creation"
    )
endif()

gtest_discover_tests(test_brick_creation)

# ===========================================================================
# AttributeRegistry Integration Tests
# ===========================================================================
# Tests LaineKarrasOctree integration with AttributeRegistry (using GaiaVoxelWorld API).
# Validates key attribute lookup, multi-attribute access, BrickView pointer access,
# type safety, and entity-to-octree integration.

add_executable(test_attribute_registry_integration
    test_attribute_registry_integration.cpp
)

target_link_libraries(test_attribute_registry_integration PRIVATE
    GTest::gtest_main
    SVO
    VoxelData
    GaiaVoxelWorld
    VoxelComponents
)

set_target_properties(test_attribute_registry_integration PROPERTIES FOLDER "Tests/SVO Tests")

# Copy TBB DLLs to test executable directory for runtime
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_attribute_registry_integration POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb>
            $<TARGET_FILE_DIR:test_attribute_registry_integration>
        COMMENT "Copying TBB DLL for test_attribute_registry_integration"
    )
endif()

gtest_discover_tests(test_attribute_registry_integration)

# ===========================================================================
# Brick Traversal Tests
# ===========================================================================
# Tests brick DDA traversal within LaineKarrasOctree. Validates brick-to-leaf
# transitions, brick misses with grid continuation, multi-brick traversal,
# brick boundary conditions, and axis-parallel rays through bricks.

add_executable(test_brick_traversal
    test_brick_traversal.cpp
)

if(TARGET SVO)
    target_link_libraries(test_brick_traversal PRIVATE GTest::gtest_main SVO)
else()
    target_link_libraries(test_brick_traversal PRIVATE GTest::gtest_main)
endif()

set_target_properties(test_brick_traversal PROPERTIES FOLDER "Tests/SVO Tests")

# Copy TBB DLLs to test executable directory for runtime
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_brick_traversal POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb>
            $<TARGET_FILE_DIR:test_brick_traversal>
        COMMENT "Copying TBB DLL for test_brick_traversal"
    )
endif()

gtest_discover_tests(test_brick_traversal)

# ===========================================================================
# Rebuild Hierarchy Tests
# ===========================================================================
# Tests rebuild() hierarchical structure construction (Phase 3)

add_executable(test_rebuild_hierarchy
    test_rebuild_hierarchy.cpp
)

target_link_libraries(test_rebuild_hierarchy PRIVATE
    GTest::gtest_main
    SVO
    GaiaVoxelWorld
    VoxelComponents
)

set_target_properties(test_rebuild_hierarchy PROPERTIES FOLDER "Tests/SVO Tests")

# Copy TBB DLLs to test executable directory for runtime
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_rebuild_hierarchy POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb>
            $<TARGET_FILE_DIR:test_rebuild_hierarchy>
        COMMENT "Copying TBB DLL for test_rebuild_hierarchy"
    )
endif()

gtest_discover_tests(test_rebuild_hierarchy)

# ===========================================================================
# Cornell Box Tests
# ===========================================================================
# Tests Cornell Box scene construction and ray casting using GaiaVoxelWorld.
# Validates material color retrieval, normal vectors, and entity-based hits.

add_executable(test_cornell_box
    test_cornell_box.cpp
)

target_link_libraries(test_cornell_box PRIVATE
    GTest::gtest_main
    SVO
    GaiaVoxelWorld
    VoxelComponents
    VoxelData
)

set_target_properties(test_cornell_box PROPERTIES FOLDER "Tests/SVO Tests")

# Copy TBB DLLs to test executable directory for runtime
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_cornell_box POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb>
            $<TARGET_FILE_DIR:test_cornell_box>
        COMMENT "Copying TBB DLL for test_cornell_box"
    )
endif()

gtest_discover_tests(test_cornell_box)

# ===========================================================================
# Screen-Space LOD Tests (Phase B.2)
# ===========================================================================
# Tests LODParameters structure and screen-space adaptive ray termination.
# Based on ESVO paper Section 4.4 and Raycast.inl line 181.

add_executable(test_lod
    test_lod.cpp
)

target_link_libraries(test_lod PRIVATE
    GTest::gtest_main
    SVO
    GaiaVoxelWorld
    VoxelComponents
    VoxelData
)

set_target_properties(test_lod PROPERTIES FOLDER "Tests/SVO Tests")

# Copy TBB DLLs to test executable directory for runtime
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_lod POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb>
            $<TARGET_FILE_DIR:test_lod>
        COMMENT "Copying TBB DLL for test_lod"
    )
endif()

gtest_discover_tests(test_lod)

# ===========================================================================
# Shell Octree Tests  (SP1 — surface-shell voxelizer + body octree)
# ===========================================================================
add_executable(test_shell_octree test_shell_octree.cpp)
if(TARGET SVO)
    target_link_libraries(test_shell_octree PRIVATE
        GTest::gtest_main SVO GaiaVoxelWorld VoxelComponents VoxelData)
else()
    target_link_libraries(test_shell_octree PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_shell_octree PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_shell_octree POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_shell_octree>
        COMMENT "Copying TBB DLL for test_shell_octree")
endif()
gtest_discover_tests(test_shell_octree)

# ===========================================================================
# Shell Octree GPU Serialization Tests  (SP2 Task 5a — CPU byte format)
# ===========================================================================
add_executable(test_shell_octree_gpu test_shell_octree_gpu.cpp)
if(TARGET SVO)
    target_link_libraries(test_shell_octree_gpu PRIVATE
        GTest::gtest_main SVO GaiaVoxelWorld VoxelComponents VoxelData)
else()
    target_link_libraries(test_shell_octree_gpu PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_shell_octree_gpu PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_shell_octree_gpu POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_shell_octree_gpu>
        COMMENT "Copying TBB DLL for test_shell_octree_gpu")
endif()
gtest_discover_tests(test_shell_octree_gpu)

# ===========================================================================
# GPU Parity Tests  (castRay vs the GpuTraversalMirror oracle — NO Vulkan/GPU)
# ===========================================================================
# Drives LaineKarrasOctree::castRay to bit-for-bit parity with a faithful 1:1 C++
# mirror of the GPU body shader (GpuTraversalMirror.h), run against the EXACT byte
# buffers the shader consumes (ShellOctreeGpu::Serialize). Headless / gtest-only.
add_executable(test_gpu_parity test_gpu_parity.cpp)
if(TARGET SVO)
    target_link_libraries(test_gpu_parity PRIVATE
        GTest::gtest_main SVO GaiaVoxelWorld VoxelComponents VoxelData)
else()
    target_link_libraries(test_gpu_parity PRIVATE GTest::gtest_main)
endif()
set_target_properties(test_gpu_parity PROPERTIES FOLDER "Tests/SVO Tests")
if(TARGET TBB::tbb)
    add_custom_command(TARGET test_gpu_parity POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:test_gpu_parity>
        COMMENT "Copying TBB DLL for test_gpu_parity")
endif()
gtest_discover_tests(test_gpu_parity)

# ===========================================================================
# CPU Body Render Tool  (headless visual check — pure castRay, NO Vulkan/GPU)
# ===========================================================================
# Standalone `main` that mirrors BodyInstanceRayMarch.comp ON THE CPU using the
# proven LaineKarrasOctree::castRay / castRayWithLOD traversal, rendering the SP2
# body scene to two PNGs (/tmp/body_render_far.png + _near.png). It builds the same
# ≤3 per-kind shell octrees as BodyOctreeSceneNode and reuses vixen/render's
# BuildBodyInstances rule. It touches NO Vulkan — safe to run anywhere.
#
# Not a gtest target (it writes images + prints a report), so no gtest_discover_tests.
# Gate on the UNDERTOW consumer headers (render/scene_instances.h, render/star_scene.h): they
# only exist when VIXEN is embedded in the UNDERTOW repo (render/ as a sibling submodule, five
# levels up). In a standalone VIXEN checkout they are absent, so this consumer-integration tool
# cannot build — skip it rather than failing the whole build.
if(TARGET SVO AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../render/star_scene.h")
    add_executable(cpu_body_render cpu_body_render_main.cpp)
    target_link_libraries(cpu_body_render PRIVATE
        SVO GaiaVoxelWorld VoxelComponents VoxelData)
    # glm comes transitively from SVO (PUBLIC). stb provides stb_image_write.h.
    if(TARGET stb)
        target_link_libraries(cpu_body_render PRIVATE stb)
    endif()
    target_include_directories(cpu_body_render PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../render)
    set_target_properties(cpu_body_render PROPERTIES FOLDER "Tools/SVO")
    if(TARGET TBB::tbb)
        add_custom_command(TARGET cpu_body_render POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:cpu_body_render>
            COMMENT "Copying TBB DLL for cpu_body_render")
    endif()
else()
    message(STATUS "[SVO Tests] SKIPPED cpu_body_render — UNDERTOW consumer headers (render/) absent (standalone VIXEN build)")
endif()

# ===========================================================================
# CPU castRay COMPARISON render  (same shell + same camera as the GPU shader)
# ===========================================================================
# Renders the SAME single octree-0 shell at the SAME shader camera as
# test_body_instance_raymarch_render, via pure CPU castRay, to
# /tmp/cpu_castray_near_cmp.png — the direct apples-to-apples brick-crack
# comparison against /tmp/glsl_shader_near.png. NO Vulkan. Not a gtest target.
if(TARGET SVO)
    add_executable(cpu_castray_cmp cpu_castray_cmp_main.cpp)
    target_link_libraries(cpu_castray_cmp PRIVATE
        SVO GaiaVoxelWorld VoxelComponents VoxelData)
    if(TARGET stb)
        target_link_libraries(cpu_castray_cmp PRIVATE stb)
    endif()
    set_target_properties(cpu_castray_cmp PROPERTIES FOLDER "Tools/SVO")
    if(TARGET TBB::tbb)
        add_custom_command(TARGET cpu_castray_cmp POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:cpu_castray_cmp>
            COMMENT "Copying TBB DLL for cpu_castray_cmp")
    endif()
endif()

# ===========================================================================
# CPU Body Render Diagnostics  (artifact isolation — pure castRay, NO Vulkan)
# ===========================================================================
# 5 diagnostic variant PNGs: shell/solid/thickband × depth-6/depth-8.
# Same NEAR camera as cpu_body_render. Not a gtest target.
# Same UNDERTOW-consumer-header gate as cpu_body_render above.
if(TARGET SVO AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../render/star_scene.h")
    add_executable(cpu_body_render_diag cpu_body_render_diag.cpp)
    target_link_libraries(cpu_body_render_diag PRIVATE
        SVO GaiaVoxelWorld VoxelComponents VoxelData)
    if(TARGET stb)
        target_link_libraries(cpu_body_render_diag PRIVATE stb)
    endif()
    target_include_directories(cpu_body_render_diag PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../render)
    set_target_properties(cpu_body_render_diag PROPERTIES FOLDER "Tools/SVO")
    if(TARGET TBB::tbb)
        add_custom_command(TARGET cpu_body_render_diag POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:TBB::tbb> $<TARGET_FILE_DIR:cpu_body_render_diag>
            COMMENT "Copying TBB DLL for cpu_body_render_diag")
    endif()
else()
    message(STATUS "[SVO Tests] SKIPPED cpu_body_render_diag — UNDERTOW consumer headers (render/) absent (standalone VIXEN build)")
endif()
