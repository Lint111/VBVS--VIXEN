import numpy as np
np.set_printoptions(precision=6, suppress=True)

# ---------------------------------------------------------------------------
# Fixture: ray moving purely along +x in world space, so all points on the
# ray have the same y,z as the origin. This keeps the algebra 1-D and lets
# us sanity check every intermediate quantity against ground truth.
# ---------------------------------------------------------------------------

S_parent = 4.0
parent_origin_world = np.array([0.0, 0.0, 0.0])
def parent_local_to_world(p_local): return parent_origin_world + p_local * S_parent
def parent_world_to_local(p_world): return (p_world - parent_origin_world) / S_parent

ray_origin_world = np.array([-2.0, 0.5*S_parent, 0.5*S_parent])  # so local y,z = 0.5
ray_dir_world = np.array([1.0, 0.0, 0.0])  # unit length

rayOriginLocal = parent_world_to_local(ray_origin_world)
rayDirLocal = ray_dir_world / S_parent
print("rayOriginLocal:", rayOriginLocal, "rayDirLocal:", rayDirLocal)

# Crossing point on the ray at local x=0.5 -> world x = 0.5*S_parent = 2.0
crossing_local = np.array([0.5, 0.5, 0.5])
t_local = (crossing_local[0] - rayOriginLocal[0]) / rayDirLocal[0]
print("t_local =", t_local)
tierCrossWorldT = t_local
crossing_point_world_geometric = ray_origin_world + tierCrossWorldT*ray_dir_world
crossing_point_world_from_local = parent_local_to_world(crossing_local)
print("check:", crossing_point_world_geometric, crossing_point_world_from_local)
assert np.allclose(crossing_point_world_geometric, crossing_point_world_from_local)
print("tierCrossWorldT = true world distance to crossing:", tierCrossWorldT)
print()

# --- child, childScale=0.5, ray still along +x, so childOriginLocal only
# needs to differ in x to keep the ray inside a sensible child mapping;
# but let's just put it centered under the crossing so childLocalOrigin
# naturally starts at the child's local center for a clean picture.
childScale = 0.5
childOriginLocal = np.array([0.5, 0.5, 0.5])  # same point as crossing_local (x also)
invScale = 1.0/childScale
parentLocalOrigin = crossing_local
parentLocalDir = rayDirLocal
childLocalOrigin = (parentLocalOrigin - childOriginLocal)*invScale + np.array([1.5,1.5,1.5])
childLocalDir = parentLocalDir*invScale
print("childLocalOrigin:", childLocalOrigin, "childLocalDir:", childLocalDir, "|childLocalDir|=", np.linalg.norm(childLocalDir))
print()

S_child = 3.0
child_origin_world = np.array([100.0,100.0,100.0])
def child_local_to_world(p): return child_origin_world + (p-np.array([1.0,1.0,1.0]))*S_child
def child_world_to_local(p): return (p-child_origin_world)/S_child + np.array([1.0,1.0,1.0])

childRayOriginWorld = child_local_to_world(childLocalOrigin)
childRayDirWorld = childLocalDir*S_child
print("childRayOriginWorld:", childRayOriginWorld, "childRayDirWorld:", childRayDirWorld, "|childRayDirWorld|=", np.linalg.norm(childRayDirWorld))

recov_origin = child_world_to_local(childRayOriginWorld)
recov_dir = childRayDirWorld/S_child
assert np.allclose(recov_origin, childLocalOrigin)
assert np.allclose(recov_dir, childLocalDir)
print("Confirmed round-trip recovers same local ray for ANY S_child.")
print()

# Child hit point: move purely along local x again (childLocalDir has only
# x-component nonzero since parentLocalDir/rayDirLocal only had x nonzero).
s_child = 0.3   # child's OWN internal parametric hit distance ("s")
child_hit_local = childLocalOrigin + s_child*childLocalDir
child_hit_world = child_local_to_world(child_hit_local)
print("child_hit_local:", child_hit_local, "child_hit_world:", child_hit_world)

true_world_dist_from_crossing = np.linalg.norm(child_hit_world - childRayOriginWorld)
print("s_child (child's reported hitT)     =", s_child)
print("TRUE world dist from crossing to hit=", true_world_dist_from_crossing)
print("ratio s_child/true_world_dist =", s_child/true_world_dist_from_crossing)
print()
print("S_child =", S_child, " S_parent =", S_parent, " childScale =", childScale)
print("S_parent*childScale =", S_parent*childScale)
print()
# Test the hypothesis: true_world_dist = s_child * S_child  (child's own
# internal "t" times child's own local->world scale -- exactly how an
# ORDINARY top-level instance's hitT works, standalone, with NO reference
# to childScale or S_parent at all)
print("s_child * S_child =", s_child*S_child, " (compare to true_world_dist)")

print()
print("=== Verifying the correction factor ===")
print("true_world_dist / s_child =", true_world_dist_from_crossing/s_child)
print("|childRayDirWorld| =", np.linalg.norm(childRayDirWorld))
print("This equals |childRayDirWorld|, i.e.:")
print("   true_world_dist = s_child * |childRayDirWorld|")
print()
print("Now: |childRayDirWorld| = |childLocalDir * S_child| = |childLocalDir| * S_child")
print("     |childLocalDir| = |parentLocalDir| * invScale = |parentLocalDir| / childScale")
print("     ==> |childRayDirWorld| = |parentLocalDir| * S_child / childScale")
print()
print("BUT the wrapper has NO access to S_child as a scalar it can cheaply read out")
print("of context at the composition site in a scale-free way EXCEPT via")
print("|childRayDirWorld| itself, which it CAN compute (it's a local vec3 the")
print("wrapper already builds at line 1033). So the general, robust, ship-ready")
print("correction is:")
print()
print("   hitT_world_correct = tierCrossWorldT + childHit_s * length(childRayDirWorld)")
print()
print("Sanity: does this reduce to today's (childScale==1, and further, an")
print("ordinary same-scale crossing where S_child snaps to 1 local-unit==1")
print("world-unit along parentLocalDir) shipped formula (plain addition) when")
print("childScale==1 AND parentLocalDir is unit-length AND S_child==S_parent?")
childScale_unity = 1.0
childLocalDir_unity = rayDirLocal * (1.0/childScale_unity)
# For Inc2's actual demo, the child octree's localToWorld is constructed so
# that the child's cube occupies EXACTLY the same world-space footprint as
# the parent leaf cell (that's the whole point of childScale==1: no
# magnification). That means S_child (child local->world uniform scale)
# must equal S_parent * (parent leaf's local edge length in root-local units)
# for a leaf at some depth -- but at the TOP of the tree with the leaf being
# the whole root cube analog in Inc2's actual demo fixture, S_child == S_parent.
S_child_unity = S_parent
childRayDirWorld_unity = childLocalDir_unity * S_child_unity
print("|childRayDirWorld| at childScale=1, S_child=S_parent:", np.linalg.norm(childRayDirWorld_unity))
print("(parentLocalDir magnitude was rayDirLocal =", np.linalg.norm(rayDirLocal), " -- NOT 1, since it's world_dir/S_parent)")

print()
print("=== Does a pure childScale multiply/divide (no S_child) ever work? ===")
print("hitT_correct/s_child = |childRayDirWorld| = |parentLocalDir|*S_child/childScale")
print("This depends on S_child (child octree's OWN localToWorld scale), which is")
print("INDEPENDENT per-octree config data, not derivable from childScale alone.")
print("=> A pure '* childScale' or '/ childScale' scalar CANNOT be correct in")
print("general -- it would only be correct in the special case S_child==S_parent")
print("(i.e. parent and child share the same base local->world scale, which Inc2's")
print("shipped demo happens to satisfy, hiding the bug).")
print()
print("Conclusion: the composition-site fix must multiply by length(childRayDirWorld)")
print("(a vector already computed in the wrapper), NOT by a naive childScale factor.")
