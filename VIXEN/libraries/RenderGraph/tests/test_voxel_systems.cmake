# ===========================================================================
# Voxel Systems - Consolidated Test Configuration (Phase H)
# ===========================================================================
# This file consolidates all voxel-related system tests:
# - Voxel Octree: Construction, compression, serialization
# - Scene Generators: Procedural scene generation with density targets
# - Voxel Traversal: Ray-AABB intersection and DDA traversal
#
# Compatible with VULKAN_TRIMMED_BUILD (headers only, no Vulkan required).
#
# PDB-consolidation (Milestone 1, 2026-07): originally 2 separate gtest executables,
# each independently re-collating RenderGraph's ~150-160MB of /Z7 embedded debug
# info into its own .pdb. Merged into 1 grouped executable — same test coverage,
# far fewer redundant PDB collations. See
# Vixen-Docs/04-Development/RenderGraph-Test-PDB-Consolidation-Plan-2026-07.md.
# ===========================================================================

# Voxel Octree Tests removed — the deprecated SparseVoxelOctree class (and its
# Data/test_voxel_octree.cpp) was deleted; the live voxel path is LaineKarrasOctree.

# ---------------------------------------------------------------------------
# Scene Generators (Phase H.2.5) + Voxel Traversal (Phase H.4.5)
# ---------------------------------------------------------------------------
# - Scene Generators: procedural scene generation — Cornell Box (10% density ±5%),
#   Cave System (50% density ±5%), Urban Grid (90% density ±5%), reproducibility and
#   spatial coherence.
# - Voxel Traversal: Ray-AABB intersection (Williams et al. 2005), DDA voxel
#   traversal (Amanatides & Woo 1987), empty space skipping optimization.

add_executable(test_rendergraph_voxelsystems
    Data/test_scene_generators.cpp
    Data/test_voxel_traversal.cpp
)

# Allow tests to include library headers with clean paths: #include "RenderGraph/..."
target_include_directories(test_rendergraph_voxelsystems PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include  # RenderGraph's own headers
)

target_link_libraries(test_rendergraph_voxelsystems PRIVATE
    GTest::gtest_main
    RenderGraph
)

# Visual Studio solution folder organization
set_target_properties(test_rendergraph_voxelsystems PROPERTIES FOLDER "Tests/RenderGraph Tests")
gtest_discover_tests(test_rendergraph_voxelsystems)

message(STATUS "[RenderGraph Tests] Added: test_rendergraph_voxelsystems (SceneGenerators/VoxelTraversal)")
