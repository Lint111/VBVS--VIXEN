import math

R = 4.8
bodyCenter = (64.0, 64.0, 64.0)
sphereRadiusLocal = 6.0   # T0's baked sphere radius, in the pre-renderScale unit-cube-ish space
# The recipe bakes into a kN=16 grid box of local half-size 8 (since local space grid spans
# some [-N/2, N/2]-ish box per BakeRecipeToSdfWorld/RECIPE_SPHERE convention); the actual WORLD
# sphere radius is sphereRadiusLocal * R (established pattern: renderScale multiplies local
# recipe-space units into world units directly, per BodyInstanceGpu.renderScale semantics).
sphereRadiusWorld = sphereRadiusLocal * R
print("sphere world radius:", sphereRadiusWorld, "(vs empirical solidRadius=5.625R=", 5.625*R, ")")

# The octant direction (unit vector, camera-facing +Z/-X/-Y octant as established):
dirVec = (-1/math.sqrt(3), -1/math.sqrt(3), 1/math.sqrt(3))

# The TRUE outer surface point of the sphere along this direction:
surfacePoint = tuple(bodyCenter[i] + dirVec[i]*sphereRadiusWorld for i in range(3))
print("sphere surface point along octant direction:", surfacePoint)

# The octant itself (a big coarse cell -- root-level leaf, covering 1/8 of the whole 48-unit
# body cube) -- does the octant's OWN geometry (the part of the sphere's SDF surface that
# falls within its cell bounds) touch/approach this same outer surface point? The camera-
# facing octant selection (`findCameraFacingLeafM8`, octants 4-7 preferred, i.e. the ones
# nearest +Z) is chosen SPECIFICALLY so its cell overlaps the sphere's own camera-facing
# surface region -- by construction this should be true (same logic Task 13/M7 already
# proved works for VIXEN_TIER_OBSERVABLE_DEMO at a gentler ratio).
#
# ==> the flight path should approach along dirVec toward the SPHERE SURFACE POINT (not the
# octant's cell-center world position, which is an octree bookkeeping artifact, not a real
# rendered-geometry location). Distance-to-target should be measured to this SURFACE point.

# far endpoint: same as before, far along dirVec for a full-body orbit view
farDistFromCenter = 120.0
farCamPos = tuple(bodyCenter[i] + dirVec[i]*farDistFromCenter for i in range(3))
farDistToSurface = farDistFromCenter - sphereRadiusWorld
print("far camera pos:", farCamPos, "dist-to-surface:", farDistToSurface)

# near endpoint: just OUTSIDE the sphere surface (small positive margin), i.e. camera radial
# dist from center = sphereRadiusWorld + smallEpsilon
epsilon = 0.05
nearDistFromCenter = sphereRadiusWorld + epsilon
nearCamPos = tuple(bodyCenter[i] + dirVec[i]*nearDistFromCenter for i in range(3))
nearDistToSurface = epsilon
print("near camera pos:", nearCamPos, "dist-to-surface:", nearDistToSurface)

childScale = 2**-10
scale_exp2 = 0.25
raySizeCoefOverride = 2.8935e-4
def hop_dist(cs, coef):
    return 20.0 * R * cs * scale_exp2 / coef
hop0 = hop_dist(childScale, raySizeCoefOverride)
hop1 = hop0 * childScale
print("hop0:", hop0, "hop1:", hop1)
print("hop1 reachable (near <= hop1 <= far)?", nearDistToSurface <= hop1 <= farDistToSurface)
print("hop0 reachable (near <= hop0 <= far)?", nearDistToSurface <= hop0 <= farDistToSurface)

logNear = math.log10(nearDistToSurface)
logFar = math.log10(farDistToSurface)
def t_for_dist(d):
    return (math.log10(d) - logNear) / (logFar - logNear)
kPhase1End = 400
print("predicted tick hop0:", t_for_dist(hop0)*kPhase1End)
print("predicted tick hop1:", t_for_dist(hop1)*kPhase1End)
