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
