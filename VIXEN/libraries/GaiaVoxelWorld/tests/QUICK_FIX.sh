#!/bin/bash
# Quick fix script for test compilation errors
# Run this from VIXEN root: bash libraries/GaiaVoxelWorld/tests/QUICK_FIX.sh

echo "Applying test compilation fixes..."

# Fix 1: Add missing includes to test_entity_brick_view.cpp
sed -i '5a #include <unordered_set>' libraries/GaiaVoxelWorld/tests/test_entity_brick_view.cpp

# Fix 2: Replace all  world.exists(entity) with world.exists(entity) in test_gaia_voxel_world.cpp
sed -i 's/entity\.valid()/world.exists(entity)/g' libraries/GaiaVoxelWorld/tests/test_gaia_voxel_world.cpp
sed -i 's/EXPECT_TRUE(world\.exists(entity))/EXPECT_TRUE(world.exists(entity))/g' libraries/GaiaVoxelWorld/tests/test_gaia_voxel_world.cpp

# Fix 3: Replace all  world.exists(entity) in test_voxel_injection_queue.cpp
sed -i 's/entity\.valid()/world.exists(entity)/g' libraries/GaiaVoxelWorld/tests/test_voxel_injection_queue.cpp

# Fix 4: Replace all  world.exists(entity) in test_entity_brick_view.cpp
sed -i 's/entity\.valid()/world.exists(entity)/g' libraries/GaiaVoxelWorld/tests/test_entity_brick_view.cpp

# Fix 5: Replace all  world.exists(entity) in test_voxel_injector.cpp (for any entity checks)
sed -i 's/entity\.valid()/world.exists(entity)/g' libraries/GaiaVoxelWorld/tests/test_voxel_injector.cpp

echo "✅ Applied fixes:"
echo "  - Added <unordered_set> include"
echo "  - Replaced  world.exists(entity) with world.exists(entity)"
echo ""
echo "Rebuild with:"
echo "  cmake --build build --config Debug --target GaiaVoxelWorld"
echo ""
echo "If MSVC PDB lock persists, see BUILD_WORKAROUND.md"
