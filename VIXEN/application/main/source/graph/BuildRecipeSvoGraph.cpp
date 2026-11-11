// Recipe/SVO scene construction extracted from BuildRenderGraph.cpp. The
// driver keeps this call at the original phase boundary and registration order.
#include "VulkanGraphApplication.h"
#include "graph/CornellBoxSceneDefinition.h"
#include "MipBake.h"
#include "SdfBake.h"
#include "LightTree.h"
#include "BakeArtifactCache.h"
#include "BuildRenderGraphFingerprint.h"
#include "Nodes/BodyOctreeSceneNode.h"
#include "Nodes/CameraNode.h"
#include "Nodes/LightTreeBufferNode.h"
#include "Nodes/LightingConfigNode.h"
#include "Nodes/VoxelGridNode.h"
#include "graph/SceneLook.h"
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <future>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
bool envFlagEnabled(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr) return false;
    for (; *value != '\0'; ++value) {
        if (!std::isspace(static_cast<unsigned char>(*value))) return true;
    }
    return false;
}

// Sampled Lighting Cornell Box Demo M2: never-baked proof for the virtual demo's eight
// rendered bodies. The light-tree's own side bake is deliberately tracked separately.
uint32_t g_cornellVirtualLightTreeSideBakeCount = 0;
using Vixen::SVO::Recipe::SdfInstruction;
using Vixen::SVO::Recipe::SdfOpCode;

struct CornellWorldSpaceBody {
    const char* name;
    std::vector<SdfInstruction> prog;  // exactly one primitive, authored in TRUE world-space units
    glm::vec3 worldCenter;             // this body's true world-space center (== the primitive's own authored position)
    float boundRadius;                 // conservative bound-sphere radius around worldCenter, world units
    glm::vec3 color;
};

SdfInstruction CornellWorldBoxAt(glm::vec3 c, glm::vec3 he, float rounding) {
    SdfInstruction in{};
    in.opCode = static_cast<uint8_t>(SdfOpCode::RoundedBox);
    in.data[0] = he.x; in.data[1] = he.y; in.data[2] = he.z; in.data[3] = rounding;
    in.data[4] = c.x;  in.data[5] = c.y;  in.data[6] = c.z;
    return in;
}
SdfInstruction CornellWorldSphereAt(glm::vec3 c, float r) {
    SdfInstruction in{};
    in.opCode = static_cast<uint8_t>(SdfOpCode::Sphere);
    in.data[0] = c.x; in.data[1] = c.y; in.data[2] = c.z; in.data[3] = r;
    return in;
}

// Builds all 8 bodies (5 walls + 1 ceiling light + 2 objects), reading
// CornellBoxSceneDefinition.h's constants directly (caller must have
// `using namespace Vixen::App::CornellBox;` in scope, same convention every
// other Cornell demo block already follows). Wall wide-axis half-extents use
// kWallSpan (kb+kWallThickness, reaches the true outer corner so adjacent
// walls seal flush -- M3 round 1's gap-bug fix) with an asymmetric Z
// treatment on leftWall/rightWall/floor/ceiling (M3 round 1's own
// over-extension-past-the-open-face fix): extend kWallThickness/2 toward the
// closed -Z (back-wall) side only, leaving the open +Z (camera) side exactly
// at the interior boundary. backWall's own X/Y wide axes are both closed and
// stay symmetric.
std::vector<CornellWorldSpaceBody> BuildCornellWorldSpaceBodies() {
    using namespace Vixen::App::CornellBox;

    constexpr float kRounding = 0.15f;
    const float kb = kBoxHalfExtent;
    const float kWallSpan = kb + kWallThickness;
    const float kZWideHalfExtent = kb + kWallThickness * 0.5f;
    const float kZWideCenterOffset = -kWallThickness * 0.5f;

    const glm::vec3 kLeftWallWorldCenter(kBoxCenter.x - kb - kWallThickness, kBoxCenter.y, kBoxCenter.z + kZWideCenterOffset);
    const glm::vec3 kRightWallWorldCenter(kBoxCenter.x + kb + kWallThickness, kBoxCenter.y, kBoxCenter.z + kZWideCenterOffset);
    const glm::vec3 kBackWallWorldCenter(kBoxCenter.x, kBoxCenter.y, kBoxCenter.z - kb - kWallThickness);
    const glm::vec3 kFloorWorldCenter(kBoxCenter.x, kBoxCenter.y - kb - kWallThickness, kBoxCenter.z + kZWideCenterOffset);
    const glm::vec3 kCeilingWorldCenter(kBoxCenter.x, kBoxCenter.y + kb + kWallThickness, kBoxCenter.z + kZWideCenterOffset);

    // Bound-sphere radius per wall must cover the wall's own half-diagonal;
    // using kWallSpan for all three axes is a safe (slightly conservative)
    // upper bound valid for both the Z-wide walls and backWall alike (see
    // the baked path's own MakeCornellWorldSpaceEvalFn for why this matters
    // there too -- the bake's own AABB, not just the virtual path's culling,
    // must comfortably contain the true geometry plus band).
    const float kWallBoundRadius = glm::length(glm::vec3(kWallThickness, kWallSpan, kWallSpan)) + 1.0f;

    std::vector<CornellWorldSpaceBody> bodies;
    bodies.push_back({"leftWall",
        {CornellWorldBoxAt(kLeftWallWorldCenter, glm::vec3(kWallThickness, kWallSpan, kZWideHalfExtent), kRounding)},
        kLeftWallWorldCenter, kWallBoundRadius, kLeftWallColor});
    bodies.push_back({"rightWall",
        {CornellWorldBoxAt(kRightWallWorldCenter, glm::vec3(kWallThickness, kWallSpan, kZWideHalfExtent), kRounding)},
        kRightWallWorldCenter, kWallBoundRadius, kRightWallColor});
    bodies.push_back({"backWall",
        {CornellWorldBoxAt(kBackWallWorldCenter, glm::vec3(kWallSpan, kWallSpan, kWallThickness), kRounding)},
        kBackWallWorldCenter, kWallBoundRadius, kNeutralWallColor});
    bodies.push_back({"floor",
        {CornellWorldBoxAt(kFloorWorldCenter, glm::vec3(kWallSpan, kWallThickness, kZWideHalfExtent), kRounding)},
        kFloorWorldCenter, kWallBoundRadius, kNeutralWallColor});
    bodies.push_back({"ceiling",
        {CornellWorldBoxAt(kCeilingWorldCenter, glm::vec3(kWallSpan, kWallThickness, kZWideHalfExtent), kRounding)},
        kCeilingWorldCenter, kWallBoundRadius, kNeutralWallColor});
    bodies.push_back({"light",
        {CornellWorldBoxAt(kLightCenter, kLightHalfExtent, 0.05f)},
        kLightCenter, glm::length(kLightHalfExtent) + 1.0f, kLightColor});
    bodies.push_back({"sphereObj",
        {CornellWorldSphereAt(kSphereObjectCenter, kSphereObjectRadius)},
        kSphereObjectCenter, kSphereObjectRadius + 1.0f, kSphereObjectColor});
    bodies.push_back({"boxObj",
        {CornellWorldBoxAt(kBoxObjectCenter, kBoxObjectHalfExtent, kRounding)},
        kBoxObjectCenter, glm::length(kBoxObjectHalfExtent) + 1.0f, kBoxObjectColor});
    return bodies;
}
} // namespace

void VulkanGraphApplication::BuildRecipeSvoGraph(NodeHandle cameraNode,
                                                     NodeHandle voxelGridNode,
                                                     NodeHandle bodyOctreeSceneNode,
                                                     NodeHandle lightTreeBufferNode,
                                                     NodeHandle lightingConfigNode,
                                                     float raymarchCameraFovDegrees) {
    using namespace Vixen::RenderGraph;
    using Vixen::SVO::Recipe::SdfInstruction;
    using Vixen::SVO::Recipe::SdfOpCode;
    auto* camera = static_cast<CameraNode*>(renderGraph->GetInstance(cameraNode));

    // Enable camera logger to debug position
    if (auto* cameraLogger = camera->GetLogger()) {
        cameraLogger->SetEnabled(false);
        cameraLogger->SetTerminalOutput(false);
    }

    camera->SetParameter(CameraNodeConfig::PARAM_FOV, raymarchCameraFovDegrees);  // shared with raySizeCoef
    camera->SetParameter(CameraNodeConfig::PARAM_NEAR_PLANE, 0.1f);
    camera->SetParameter(CameraNodeConfig::PARAM_FAR_PLANE, 500.0f);
    // Tiered-ESVO Inc3 M8 Task 19: the translating flight-path demo needs the camera to
    // approach as close as ~0.05 world units from the crossing-octant's own surface point (to
    // clear hop1's predicted ~0.079wu handoff threshold) -- own live-gate finding: the default
    // 0.1wu near-clip plane is LARGER than hop1's own threshold, so the target geometry was
    // being near-plane-clipped away entirely at every near-field tick (empty/background
    // frames, not a tier-crossing or aim bug). Widen near-clip for this demo only, the same
    // "widen the bound, own justified commit" precedent CameraNode.h's kOrbitDistanceMin
    // widening already established for an analogous too-coarse-default problem.
    if (envFlagEnabled("VIXEN_TIER_M8_FLIGHT_DEMO")) {
        // M8 Task 23: with the corrected (camera-anchored) crossing gate at the REAL
        // unoverridden raySizeCoef, hop1 (T1->T2) fires only below ~7.3e-3wu camera-to-child
        // and T2's own content needs an approach to ~2e-4wu to subtend more than a few
        // pixels (childScale^2 = 2^-20 of the body) -- narrow near-clip accordingly
        // (was 0.01wu for the Task-19-era override-based schedule).
        camera->SetParameter(CameraNodeConfig::PARAM_NEAR_PLANE, 0.0001f);
        mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_M8_FLIGHT_DEMO: near-clip plane "
                          "narrowed to 1e-4wu (M8 Task 23: true-2^-10 hop1 fires below "
                          "~7.3e-3wu and T2 needs a ~2e-4wu approach to be attributable)");
    }
    // Camera presets for Cornell box (grid spans [0,128], center at 64,64,64)
    // Uncomment one preset below:

    // PRESET 1: Front view looking into box (camera outside grid)
    // Orbit mode: yaw=0 means camera at +Z looking toward orbitCenter
    // yaw=pi means camera at -Z looking toward +Z (into the grid)
    // For camera at +Z looking toward grid, use yaw=0
    camera->SetParameter(CameraNodeConfig::PARAM_CAMERA_X, 64.0f);   // Center X (ignored in orbit mode)
    camera->SetParameter(CameraNodeConfig::PARAM_CAMERA_Y, 64.0f);   // Center Y (ignored in orbit mode)
    camera->SetParameter(CameraNodeConfig::PARAM_CAMERA_Z, 300.0f);  // Outside grid (ignored in orbit mode)
    camera->SetParameter(CameraNodeConfig::PARAM_YAW, 0.0f);         // Camera at +Z, looking toward -Z
    camera->SetParameter(CameraNodeConfig::PARAM_PITCH, 0.0f);

    // Tiered-ESVO Inc2 M5 Task 11: VIXEN_TIER_ZOOM_DEMO drives the camera via
    // SetOrbitDistanceForTest, which orbits around CameraNode's own orbitCenter -- left at its
    // stale Cornell-box default (5,5,5) unless a consumer configures PARAM_ORBIT_CENTER_*
    // (CameraNode.cpp's own SetupImpl comment: "orbitCenter itself is the pivot and can't be
    // derived from position alone"). The tier-crossing demo body sits at world (64,64,64) (see
    // the VIXEN_TIER_CROSSING_DEMO scene-construction block below), nowhere near (5,5,5) -- an
    // unconfigured orbit here would swing the camera away from the body on the very first
    // SetOrbitDistanceForTest call (caught live: every captured frame was byte-identical sky
    // until this was added). Configuring PARAM_ORBIT_CENTER_* here declares orbit-mode intent
    // from SetupImpl (CameraNode.cpp's own orbitActive_ latch), so EngageOrbit()'s idempotent
    // guard makes the zoom demo's first SetOrbitDistanceForTest call a no-op re-seed.
    if (envFlagEnabled("VIXEN_TIER_ZOOM_DEMO")) {
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_X, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Y, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Z, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_DISTANCE, 236.0f);  // matches the at-rest Z=300 distance
        mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_ZOOM_DEMO: orbitCenter set to demo body's "
                          "world center (64,64,64) so the scripted zoom actually orbits the body");
    }
    // Tiered-ESVO Inc3 M4: the SAME orbitCenter gotcha applies to the Earth-scale zoom demo
    // (VIXEN_TIER_EARTH_ZOOM_DEMO's own body is built at the IDENTICAL world center
    // (64,64,64) as VIXEN_TIER_CROSSING_DEMO/VIXEN_TIER_CHAIN_DEMO/VIXEN_TIER_ZOOM_DEMO) --
    // this demo's own scripted SetOrbitDistanceForTest call would otherwise orbit the stale
    // Cornell-box default (5,5,5), producing a distant/empty-looking capture regardless of
    // orbitDistance (caught live: this milestone's first capture pass showed a tiny distant
    // dot at EVERY tick, near and far alike, until this was added -- the exact class of bug
    // M5's own comment above already documents and warns about).
    if (envFlagEnabled("VIXEN_TIER_EARTH_ZOOM_DEMO") || envFlagEnabled("VIXEN_TIER_EARTH_ZOOM_SCRIPT")) {
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_X, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Y, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Z, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_DISTANCE, 236.0f);
        mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_EARTH_ZOOM_DEMO: orbitCenter set to demo "
                          "body's world center (64,64,64) so the scripted Earth-scale zoom "
                          "actually orbits the body");
    }
    // Tiered-ESVO Inc3 M7 Task 13: SAME orbitCenter gotcha applies to the reconstructed
    // observable demo (also built at world center (64,64,64)). VIXEN_TIER_OBSERVABLE_DISTANCE
    // (optional) statically re-seeds orbitDistance for a single-shot pixel-decode capture at a
    // hand-picked distance (e.g. the predicted hop0/hop1 handoffs), without needing a scripted
    // zoom -- this milestone's own gate is "confirm concentric magnification on the
    // reconstructed body", not the live zoom (that is M7 Task 14, a separate milestone).
    if (envFlagEnabled("VIXEN_TIER_OBSERVABLE_DEMO")) {
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_X, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Y, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Z, 64.0f);
        float obsDistance = 40.0f;  // default: between hop1 (~19.89) and hop0 (~79.58)
        if (const char* obsDistEnv = std::getenv("VIXEN_TIER_OBSERVABLE_DISTANCE")) {
            obsDistance = std::strtof(obsDistEnv, nullptr);
            if (!(obsDistance > 0.0f)) obsDistance = 40.0f;
        }
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_DISTANCE, obsDistance);
        mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_OBSERVABLE_DEMO: orbitCenter set to "
                          "demo body's world center (64,64,64), orbitDistance=" + std::to_string(obsDistance));
    }
    // Tiered-ESVO Inc3 M8 Task 17: SAME orbitCenter gotcha applies to the true Earth-scale
    // demo (also built at world center (64,64,64), M4's own established convention).
    //
    // M8 Task 19: this wiring is SKIPPED when VIXEN_TIER_M8_FLIGHT_DEMO is also set. Setting
    // any PARAM_ORBIT_* here latches CameraNode::orbitActive_ = true (SetupImpl's own
    // "explicit orbit param = orbit-mode intent" convention), which makes UpdateCameraData's
    // ORBIT MODE branch recompute cameraPosition from orbitCenter/orbitDistance/yaw/pitch EVERY
    // frame -- silently overriding a scripted CameraNode::SetPositionForTest() write from the
    // Task 19 flight-path demo (own live-gate finding: the very first capture attempt rendered
    // a completely static, unchanging frame across all 400 ticks despite SetPositionForTest
    // being called every tick, because this orbit-param wiring had already engaged orbit mode
    // at scene-build time). The flight-path demo needs FIXED mode (cameraPosition authoritative
    // at rest, per the "bodies-0" convention) so its own scripted position writes are what
    // actually drives the camera.
    if (envFlagEnabled("VIXEN_TIER_M8_EARTH_DEMO") && !envFlagEnabled("VIXEN_TIER_M8_FLIGHT_DEMO")) {
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_X, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Y, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Z, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_DISTANCE, 236.0f);
        mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_M8_EARTH_DEMO: orbitCenter set to demo "
                          "body's world center (64,64,64) so the scripted zoom actually orbits the body");
    } else if (envFlagEnabled("VIXEN_TIER_M8_FLIGHT_DEMO")) {
        mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_M8_FLIGHT_DEMO: skipping orbit-param "
                          "wiring -- camera stays in FIXED mode so the Task 19 scripted "
                          "SetPositionForTest flight path is authoritative, not overridden by "
                          "an orbit-mode recompute every frame");
    }
    // Recipe-Diversity-Stress-Scene Inc6 M2: camera preset for VIXEN_RECIPE_DIVERSITY_STRESS_DEMO's
    // spatial grid (centered on world (64,64,64), same convention as every other demo above).
    // CameraNode::kOrbitDistanceMax (120.0, a hard clamp -- SetParameter/EngageOrbit both clamp
    // into it) means the WHOLE grid cannot be framed in one shot at N's upper end (a 14x14 grid
    // at N=192 spans ~156-234 world units per side depending on kGridSpacing, well beyond what
    // 120 units of orbit distance can frame at the shared 45-degree FOV) -- this is a real,
    // accepted limitation, not a bug: the live-run gate only requires visually confirming SOME
    // bodies at genuinely distinct positions with distinct shapes, not literally all N in one
    // screenshot (no single frame could show 250 legible distinct shapes regardless of framing).
    // Pitch angled downward (looking down at the grid from above-and-outside, not a flat
    // horizontal view into a wall of overlapping-in-screen-space spheres) gives the widest
    // legible view of the grid's near corner within the distance cap.
    if (envFlagEnabled("VIXEN_RECIPE_DIVERSITY_STRESS_DEMO")) {
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_X, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Y, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Z, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_DISTANCE, 118.0f);  // near kOrbitDistanceMax
        camera->SetParameter(CameraNodeConfig::PARAM_PITCH, 0.9f);  // ~51 degrees, looking down at the grid
        mainLogger->Info("[BuildRenderGraph] VIXEN_RECIPE_DIVERSITY_STRESS_DEMO: orbitCenter set to "
                          "(64,64,64), orbitDistance=118 (near the 120 cap), pitch=0.9rad -- frames "
                          "the grid's near region from above; the full grid may extend beyond frame "
                          "at high N, which the live-run gate accounts for");
    }
    if (envFlagEnabled("VIXEN_STARLIGHT_DEMO")) {
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_X, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Y, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Z, 64.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_DISTANCE, 110.0f);
        camera->SetParameter(CameraNodeConfig::PARAM_FOV, 45.0f);
        mainLogger->Info("[BuildRenderGraph] VIXEN_STARLIGHT_DEMO: framed star and three orbiting bodies");
    }
    // Sampled Lighting Cornell Box Demo M1: shared camera preset (ONE source, both
    // VIXEN_DDGI_CORNELL_BAKED_DEMO and M2's VIXEN_DDGI_CORNELL_VIRTUAL_DEMO read the SAME
    // CornellBoxSceneDefinition.h constants). BuildRenderGraph.cpp's own stale "Camera presets
    // for Cornell box" comment above (PRESET 1, world grid [0,128] / center (64,64,64)) is the
    // LEGACY VoxelGridNode/CashSystem "cornell" scene type (BuildRenderGraph.cpp's own
    // PARAM_SCENE_TYPE default, wired at VIXEN_SCENE below) -- confirmed unrelated to
    // BodyOctreeSceneNode/DDGI (DDGI's pipeline never reads VoxelGridNode's scene type at all,
    // per this program's own plan doc "Grounding from codebase investigation"), NOT reused here.
    // Orbit mode, yaw=0 (CameraNode.cpp's own convention: yaw=0 -> cameraPosition = orbitCenter
    // + (0,0,orbitDistance), camera on the +Z side looking toward -Z/orbitCenter) -- the box's
    // +Z face is the deliberately-open one (CornellBoxSceneDefinition.h's own wall layout), so
    // a yaw=0 camera sits just outside that open face looking straight in, no yaw override
    // needed (CameraNodeConfig::PARAM_YAW already defaults to 0 from SetupImpl above).
    // M6b Task 6b.1/6b.2: also applies to VIXEN_DDGI_CORNELL_HYBRID_DEMO and
    // VIXEN_DDGI_CORNELL_MIXED_DEMO (mixed-provider variants) -- same shared Cornell box
    // geometry/camera-preset source, only the per-body provider assignment differs. Missing
    // this branch (found live, M6b round 1) leaves the camera on the unrelated legacy
    // VoxelGridNode PRESET-1 default, pointed away from the actual scene -- symptom was a
    // near-total scene miss (3279/250000 hits) that looked like a rendering bug but was
    // purely a wrong camera transform.
    if (envFlagEnabled("VIXEN_DDGI_CORNELL_BAKED_DEMO") || envFlagEnabled("VIXEN_DDGI_CORNELL_VIRTUAL_DEMO") ||
        envFlagEnabled("VIXEN_DDGI_CORNELL_HYBRID_DEMO") || envFlagEnabled("VIXEN_DDGI_CORNELL_MIXED_DEMO")) {
        using namespace Vixen::App::CornellBox;
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_X, kCameraOrbitCenter.x);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Y, kCameraOrbitCenter.y);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_CENTER_Z, kCameraOrbitCenter.z);
        camera->SetParameter(CameraNodeConfig::PARAM_ORBIT_DISTANCE, kCameraOrbitDistance);
        camera->SetParameter(CameraNodeConfig::PARAM_FOV, kCameraFovDegrees);
        mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_*_DEMO: orbitCenter set to shared "
                          "Cornell box center, framed straight into the open +Z face");
    }
    camera->SetParameter(CameraNodeConfig::PARAM_GRID_RESOLUTION, 128u);

    // Ray marching: Voxel grid parameters
    auto* voxelGrid = static_cast<VoxelGridNode*>(renderGraph->GetInstance(voxelGridNode));
    voxelGrid->SetParameter(VoxelGridNodeConfig::PARAM_RESOLUTION, 128u);
    // Scene type defaults to the Cornell-box test scene; a host can override it (e.g. the UNDERTOW
    // render host sets VIXEN_SCENE=starsystem after registering its bodies with the scene factory).
    const char* sceneEnv = std::getenv("VIXEN_SCENE");
    voxelGrid->SetParameter(VoxelGridNodeConfig::PARAM_SCENE_TYPE,
                            std::string(sceneEnv != nullptr ? sceneEnv : "cornell"));

    // --- Standalone default body scene (Option A) ---
    // The live render path dispatches BodyInstanceRayMarch.comp, which only draws per-body INSTANCES
    // (numInstances = clamp(pc.instanceCount, ...)). The UNDERTOW host feeds real bodies at runtime via
    // VulkanGraphApplication::SetBodyInstances() -> BodyOctreeSceneNode::SetInstances(), but standalone
    // VIXEN.exe has no body source, so with 0 instances every ray misses and the screen is just the
    // dark sky color (looks black). Seed a few default instances so the standalone app shows a scene.
    //
    // SetInstances REPLACES the list (instances_ = std::move(...)), so a host that calls SetBodyInstances
    // at runtime fully overwrites these defaults — they are a standalone fallback only, no host gating
    // needed. BodyOctreeSceneNode builds 3 shell-octree "kinds" (octreeIndex 0/1/2), each a [0,64]^3 shell
    // (base center (32,32,32)). The instance transform is instOrigin = (rayOrigin - worldPos)/renderScale,
    // so a shell centered at world C needs worldPos = C - (32,32,32)*renderScale. We center the 3 shells
    // around the 128^3 grid centre (64,64,64) and spread them along X so they don't overlap and all sit
    // in the default camera view (verified on screen: three distinct red/green/white spheres).
    // color[3] is a per-instance tint that MULTIPLIES the kind's material (1=red, 2=green, 3=white), kept
    // near-white per instance (slight warm/neutral/cool bias) so each stays bright and the three are
    // distinguishable by both base material and tint.
    {
        if (envFlagEnabled("VIXEN_TIER_CROSSING_DEMO")) {
            // Tiered-ESVO Inc2 M3 Task 8: live gate — a single tier-crossing leaf,
            // one PARENT SDF octree (octree 0) with ONE leaf marked farBit=1 via
            // MarkLeafAsTierCrossing, pointing at an independently-built CHILD SDF
            // octree (octree 1). Manually concatenated (mirrors test_tier_crossing_
            // construction.cpp's TwoTreeFixtureRoundTripsThroughSerializeAndConcatenate
            // — Concatenate/ConcatenateSdf call Serialize/SerializeSdf INTERNALLY and
            // would discard a pre-concatenation MarkLeafAsTierCrossing mutation, so
            // this loop replicates that bookkeeping by hand, exactly as the test does).
            //
            // Geometry: n=16, r=6.0, brickDepth=3 -> bricksPerAxis=2 -> the root's 8
            // children are ALL deterministic brick-level leaves (same fixture shape
            // test_tier_crossing_construction.cpp/test_tier_ref_table.cpp/test_mip_
            // sample_bake.cpp all rely on) -- FindFirstLeaf below locates the first
            // one deterministically instead of guessing an index.
            mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_CROSSING_DEMO: building hand-authored two-tree tier-crossing scene");

            constexpr int   kN          = 16;
            constexpr float kR          = 6.0f;
            constexpr int   kBrickDepth = 3;
            const glm::vec3 kCenter(8.0f, 8.0f, 8.0f);

            Vixen::SVO::RecipeParams rp{};
            rp.radius = kR;

            Vixen::SVO::SdfBakeResult parentBaked =
                Vixen::SVO::BakeRecipeToSdfWorld(Vixen::SVO::RECIPE_SPHERE, kCenter, rp, kN, 2.0f);
            Vixen::SVO::SdfBodyOctree parentBody = Vixen::SVO::BuildSdfBodyOctree(parentBaked, kBrickDepth);

            // Child recipe deliberately DIFFERENT from the parent's, per the coordinator's
            // request for an unambiguous visual A/B (not just a different position):
            // a LARGER radius (fills more of the crossing leaf's local cell) AND — below,
            // after SerializeSdf — a solid saturated-magenta color override replacing the
            // shared BakeSdfWorld cosine-gradient (SdfBake.h's own per-voxel color formula
            // is identical for parent/child otherwise, since it is hardcoded inside the
            // shared bake function, not exposed as a parameter — overriding channelPool's
            // SEM_COLOR channel post-bake is the surgical fix that does not touch that
            // shared, widely-used function).
            constexpr float kChildR = 7.2f;  // vs parent's 6.0f — visibly larger/rounder
            Vixen::SVO::RecipeParams childRp{};
            childRp.radius = kChildR;
            Vixen::SVO::SdfBakeResult childBaked =
                Vixen::SVO::BakeRecipeToSdfWorld(Vixen::SVO::RECIPE_SPHERE, kCenter, childRp, kN, 2.0f);
            Vixen::SVO::SdfBodyOctree childBody = Vixen::SVO::BuildSdfBodyOctree(childBaked, kBrickDepth);

            Vixen::SVO::SerializedOctree parentSer = Vixen::SVO::SerializeSdf(parentBody);
            Vixen::SVO::SerializedOctree childSer  = Vixen::SVO::SerializeSdf(childBody);

            // Overwrite the CHILD's entire SEM_COLOR channel with a solid, saturated
            // magenta (1,0,1) — unmistakably distinct from the parent's warm-white/rainbow
            // cosine gradient (SdfBake.h's col = 0.5+0.5*cos(p*0.12+phase), which stays in
            // muted mid-tones and never reaches a pure saturated primary). Iterates every
            // brick/voxel slot in the child's channelPool directly (the same addressing
            // ShellOctreeGpu.h's own readPoolVoxel documents:
            // channelPool[brick*brickStrideFloats + channelBaseFloats(SEM_COLOR) + comp*512 + voxel]).
            {
                const uint32_t colorBase = childSer.channelBaseFloats(Vixen::SVO::SEM_COLOR);
                if (colorBase != 0xFFFFFFFFu) {
                    float* pool = reinterpret_cast<float*>(childSer.channelPool.data());
                    const size_t poolFloats = childSer.channelPool.size() / sizeof(float);
                    for (uint32_t brick = 0; brick < childSer.brickCount; ++brick) {
                        for (uint32_t comp = 0; comp < 3; ++comp) {
                            const float magentaComp = (comp == 1) ? 0.0f : 1.0f;  // (1,0,1)
                            for (uint32_t voxel = 0; voxel < Vixen::SVO::SerializedOctree::kVoxelsPerBrick; ++voxel) {
                                const size_t idx = static_cast<size_t>(brick) * childSer.brickStrideFloats
                                                 + colorBase + comp * Vixen::SVO::SerializedOctree::kVoxelsPerBrick + voxel;
                                if (idx < poolFloats) pool[idx] = magentaComp;
                            }
                        }
                    }
                    mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_CROSSING_DEMO: child channelPool SEM_COLOR overwritten to solid magenta (1,0,1)");
                } else {
                    mainLogger->Error("[BuildRenderGraph] VIXEN_TIER_CROSSING_DEMO: child has no SEM_COLOR channel — color override skipped");
                }
            }

            // Tiered-ESVO Inc2 M4 Task 9/10: bake + attach a real mip pool to BOTH trees
            // (ConcatenateSdfWithMips's own per-tree convention, MipBake.h) so
            // shadeFromMipSample has genuine coverage/color to read when either the LOD
            // gate or the residency check declines a crossing -- without this, the
            // fallback would silently degrade to the neutral-grey placeholder shade
            // (still correct/non-crashing, but a weaker "did it actually mip-shade real
            // geometry" proof). MUST run AFTER the magenta override above (BakeMipPool
            // reads serialized.channelPool directly, so an out-of-order bake would mip a
            // pre-override cosine-gradient color instead of the overridden magenta).
            if (const Vixen::SVO::Octree* parentOctForMip = parentBody.octree->getOctree()) {
                Vixen::SVO::BakeAndAttachMipPool(*parentOctForMip, parentSer);
            }
            if (const Vixen::SVO::Octree* childOctForMip = childBody.octree->getOctree()) {
                Vixen::SVO::BakeAndAttachMipPool(*childOctForMip, childSer);
            }

            // Locate a CAMERA-FACING leaf child in the parent's raw (pre-concatenation)
            // Octree (the same "scan childDescriptors directly" convention
            // test_tier_crossing_construction.cpp's FindAllLeaves uses), rather than
            // blindly taking the first leaf found. localToWorld is a PURE uniform scale
            // (ShellOctreeGpu.h: translate(0)*scale(10), no axis flip), so local [1,2)
            // maps monotonically to world space with no mirroring; the demo camera looks
            // down -Z from world Z=300 (yaw=0,pitch=0 -> forward=(0,0,-1)), so it sees the
            // sphere's +Z-facing (near-camera) hemisphere, i.e. LARGER local-z, i.e. an
            // octant with bit 2 (z) SET. Octant bit convention (SVOTypes.h mirroredToLocalOctant
            // and friends: bit0=x,bit1=y,bit2=z) confirmed directly by reading those functions.
            // The root's 8 children for this n=16/brickDepth=3 fixture are ALL brick-level
            // leaves (bricksPerAxis=2), so preferring octant>=4 (z bit set) is guaranteed to
            // find one deterministically.
            const Vixen::SVO::Octree* parentOct = parentBody.octree->getOctree();
            uint32_t markParentDescIdx = 0;
            int markOctant = -1;
            if (parentOct != nullptr) {
                const auto& descs = parentOct->root->childDescriptors;
                // First pass: prefer a camera-facing octant (bit2/z set -> octants 4-7).
                for (uint32_t i = 0; i < descs.size() && markOctant < 0; ++i) {
                    const Vixen::SVO::ChildDescriptor& d = descs[i];
                    for (int oct = 4; oct < 8; ++oct) {
                        if (d.hasChild(oct) && d.isLeaf(oct)) {
                            markParentDescIdx = i;
                            markOctant = oct;
                            break;
                        }
                    }
                }
                // Fallback: any leaf, if no camera-facing octant exists (shouldn't happen
                // for this fixture, but don't silently build an unmarked scene).
                if (markOctant < 0) {
                    for (uint32_t i = 0; i < descs.size() && markOctant < 0; ++i) {
                        const Vixen::SVO::ChildDescriptor& d = descs[i];
                        for (int oct = 0; oct < 8; ++oct) {
                            if (d.hasChild(oct) && d.isLeaf(oct)) {
                                markParentDescIdx = i;
                                markOctant = oct;
                                break;
                            }
                        }
                    }
                }
            }

            if (markOctant >= 0) {
                // TierRef: child's [1,2)-frame origin/scale expressed in the PARENT's
                // local frame (§3.2/§3.3). childScale=1.0 keeps the child at the SAME
                // physical scale as the parent for this milestone's proof (M3 does not
                // require a scale change — that is a rendering/LOD refinement, not the
                // mechanism this gate proves); childOriginLocal is the MARKED LEAF'S
                // OWN cell center (Tiered-ESVO Inc3 M5 Task 9 fix — see
                // RootLeafOctantCenterLocal's own header comment: the constant
                // (1.5,1.5,1.5) used here pre-M5 is the ROOT CUBE'S shared corner, not
                // any one octant's center, and made the crossing collapse the child
                // toward a corner-anchored, non-concentric "wedge" instead of shrinking
                // around the leaf at any non-unity childScale), so the child tree
                // occupies the SAME [1,2) cell the marked leaf itself occupies (a clean,
                // well-conditioned "known" placement for the hand-computed screen-
                // position cross-check below).
                // Tiered-ESVO Inc3 M2 Task 4: VIXEN_TIER_CROSSING_SCALE_DEMO exercises a genuinely
                // non-unity TierRef::childScale (default 0.25 if set with no value, or the parsed
                // float value) instead of the M3/Inc2 baseline's childScale=1.0 -- the ONLY variable
                // changed vs. the childScale==1.0 fixture above (same childOriginLocal, same leaf,
                // same magenta child, same camera), per the plan's "vary ONLY X" discipline. childScale
                // is a raw linear multiplier on the child's [1,2) unit cube in parent-local space
                // (remapRayIntoChildFrame: childLocalOrigin=(parentLocalOrigin-childOrigin)*invScale+1.5),
                // NOT pre-scaled by the marked leaf's own octant fraction -- so at childScale=1.0 the
                // child fills the ENTIRE marked leaf's own [1,2) cell footprint (world edge 24 =
                // half of 10*renderScale, since the leaf is one root-level octant), and at
                // childScale=0.25 it fills a cube 1/4 the linear size, CONCENTRICALLY centered on
                // the same leaf-cell world point since childOriginLocal is unchanged.
                float childScale = 1.0f;
                if (const char* scaleEnv = std::getenv("VIXEN_TIER_CROSSING_SCALE_DEMO")) {
                    childScale = (scaleEnv[0] != '\0') ? std::strtof(scaleEnv, nullptr) : 0.25f;
                    if (!(childScale > 0.0f)) {
                        childScale = 0.25f;  // guard against a garbage/zero env value
                    }
                    mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_CROSSING_SCALE_DEMO: childScale="
                                  + std::to_string(childScale));
                }

                const glm::vec3 leafCenterLocal = Vixen::SVO::RootLeafOctantCenterLocal(markOctant);
                Vixen::SVO::TierRef ref{};
                ref.childOctreeIndex = 1u;  // child will be concatenated at slot 1
                ref.childOriginLocal[0] = leafCenterLocal.x;
                ref.childOriginLocal[1] = leafCenterLocal.y;
                ref.childOriginLocal[2] = leafCenterLocal.z;
                ref.childScale = childScale;
                constexpr uint8_t kChildRootScaleHint = 22;  // child's own root ESVO scale

                Vixen::SVO::MarkLeafAsTierCrossing(parentSer, markParentDescIdx, markOctant, ref, kChildRootScaleHint);

                // Manual concatenation (parent=slot0, child=slot1) — mirrors
                // ConcatenateSdf's own per-octree bookkeeping loop exactly.
                Vixen::SVO::ConcatenatedOctrees cat;
                cat.count = 2;
                cat.configs.resize(2);
                cat.nodeCounts.resize(2);
                cat.brickCounts.resize(2);
                cat.tierRefCounts.resize(2);

                Vixen::SVO::SerializedOctree* octs[2] = {&parentSer, &childSer};
                uint32_t nodeBase = 0, brickBase = 0, poolBase = 0, tierRefBase = 0, mipPoolBase = 0;
                for (int k = 0; k < 2; ++k) {
                    Vixen::SVO::SerializedOctree& s = *octs[k];
                    s.config.nodeArrayBase  = static_cast<int32_t>(nodeBase);
                    s.config.brickArrayBase = static_cast<int32_t>(brickBase);
                    Vixen::SVO::setSdfBrickArrayBase(s.config, poolBase);
                    Vixen::SVO::setTierRefTableBase(s.config, tierRefBase);
                    Vixen::SVO::setMipPoolBase(s.config, mipPoolBase);

                    cat.configs[k]       = s.config;
                    cat.nodeCounts[k]    = s.nodeCount;
                    cat.brickCounts[k]   = s.brickCount;
                    cat.tierRefCounts[k] = static_cast<uint32_t>(s.tierRefs.size());

                    cat.nodes.insert(cat.nodes.end(), s.nodes.begin(), s.nodes.end());
                    cat.bricks.insert(cat.bricks.end(), s.bricks.begin(), s.bricks.end());
                    cat.channelPool.insert(cat.channelPool.end(), s.channelPool.begin(), s.channelPool.end());
                    cat.brickGridLookup.insert(cat.brickGridLookup.end(), s.brickGridLookup.begin(), s.brickGridLookup.end());
                    cat.tierRefTable.insert(cat.tierRefTable.end(), s.tierRefs.begin(), s.tierRefs.end());
                    cat.mipPool.insert(cat.mipPool.end(), s.mipPool.begin(), s.mipPool.end());

                    if (cat.materials.empty()) {
                        cat.materials = s.materials;
                    }

                    nodeBase    += s.nodeCount;
                    brickBase   += s.brickCount;
                    poolBase    += s.brickCount * s.brickStrideFloats;
                    tierRefBase += static_cast<uint32_t>(s.tierRefs.size());
                    mipPoolBase += s.nodeCount * s.channelCount;
                }

                // Tiered-ESVO Inc2 M4 Task 10 live-gate knob: VIXEN_TIER_CROSSING_NONRESIDENT calls
                // RequestBrickResidency(false) below (NOT a direct setBrickResident() poke on
                // cat.configs[1] -- CreateOctreeBuffers's own `for (auto& cfg : concatenated_.
                // configs) setBrickResident(cfg, brickPoolUploaded_)` loop unconditionally
                // re-stamps EVERY config's brickResident from residencyRequested_/brickPoolUploaded_
                // on the very first Compile, so a pre-SetRecipePool poke on the concatenated struct
                // would be silently clobbered the instant the node actually builds its buffers).
                // RequestBrickResidency is a WHOLE-NODE flag applied uniformly to every octree in
                // this one ConcatenatedOctrees pool -- there is no existing mechanism to make the
                // child non-resident while the parent stays resident within a single
                // BodyOctreeSceneNode, so this demo's "non-resident" case makes BOTH trees
                // non-resident (both fall back to mip-shading, per Sparse-Mip's existing sentinel-
                // miss pattern) -- still a genuine, honest proof of the crossing correctly declining
                // and mip-shading rather than crashing/rendering garbage, just not isolated to the
                // child alone (that isolation would need a genuinely new per-octree residency
                // mechanism, out of this increment's scope per the design doc's own "no new
                // residency state machine" line).
                // Tiered-ESVO Inc2 M5 Task 11: VIXEN_TIER_ZOOM_DEMO reuses the SAME
                // RequestBrickResidency(false) start-state as VIXEN_TIER_CROSSING_NONRESIDENT
                // above, so the scripted zoom (VulkanGraphApplication::Update) has a real 0->1
                // transition to exercise mid-flight via its own scripted RequestBrickResidency(true)
                // at tick 24 -- proving the composed lifecycle live, not just as two separate runs.
                const bool forceNonResident = envFlagEnabled("VIXEN_TIER_CROSSING_NONRESIDENT")
                                            || envFlagEnabled("VIXEN_TIER_ZOOM_DEMO");

                if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                    bodyScene->SetRecipePool(std::move(cat));
                    if (forceNonResident) {
                        bodyScene->RequestBrickResidency(false);
                        mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_CROSSING_NONRESIDENT/VIXEN_TIER_ZOOM_DEMO: "
                                          "RequestBrickResidency(false) -- both octrees mip-only at start");
                    } else {
                        // Lazy-Procedural-Delta-Baseline Inc0 M2 Task 4 demo-knob audit: both
                        // trees here are mip-baked (BakeAndAttachMipPool above), so this pool
                        // is mip-capable and M2's capability-derived default would flip it
                        // LAZY at boot -- a real behavior change for this demo, which existed
                        // to prove the tier-crossing MECHANISM (not residency laziness) and has
                        // always booted with real bricks resident. Pin eager explicitly so
                        // plain VIXEN_TIER_CROSSING_DEMO (no _NONRESIDENT/_ZOOM_DEMO) keeps its
                        // pre-M2 boot behavior byte-for-byte.
                        bodyScene->RequestBrickResidency(true);
                        mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_CROSSING_DEMO: "
                                          "RequestBrickResidency(true) -- pinned eager (M2 demo-knob audit)");
                    }

                    // ONE instance, pointing at octree 0 (the parent). Placed at the
                    // default camera's frame center so the WHOLE parent sphere (and
                    // thus, for at least some pixels, its tier-crossing leaf) is on
                    // screen.
                    //
                    // IMPORTANT: BodyInstanceRayMarch.comp's worldToLocal/localToWorld
                    // (SerializeSdf's kWorldGridSize=10.0f) maps the octree's OWN
                    // "config-local-world" cube to a FIXED [0,10]^3 span, INDEPENDENT
                    // of the bake's own grid resolution `n` -- gridMin/gridMax are never
                    // read by this shader (confirmed by direct grep; only VoxelRayMarch.comp's
                    // dense path reads them). So (unlike a naive "n * renderScale" guess)
                    // the instance transform's actual world span is
                    // renderScale * [0,10], centered at worldPos + 5*renderScale.
                    // renderScale=4.8 -> world diameter 48, matching the other demo
                    // bodies' ~48-unit apparent size (kHalf=24 in the Stored-SDF/
                    // Procedural demos above).
                    constexpr float kRenderScale = 4.8f;
                    constexpr float kHalf = 5.0f * kRenderScale;  // = 24.0f (half of the [0,10] span)
                    Vixen::SVO::BodyInstanceGpu inst{};
                    Vixen::SVO::SetInstanceTranslationScale(inst, glm::vec3(64.0f - kHalf, 64.0f - kHalf, 64.0f - kHalf), kRenderScale);
                    inst.material.color[0]     = 1.0f;
                    inst.material.color[1]     = 1.0f;
                    inst.material.color[2]     = 1.0f;
                    inst.material.octreeIndex  = 0u;    // parent tree
                    inst.material.providerKind = 0u;    // PROVIDER_STORED
                    inst.material.recipeId     = 0u;

                    bodyScene->SetInstances({inst});
                    mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_CROSSING_DEMO: parent leaf ("
                                  + std::to_string(markParentDescIdx) + "," + std::to_string(markOctant)
                                  + ") marked tier-crossing -> child octree 1");
                }
            } else {
                mainLogger->Error("[BuildRenderGraph] VIXEN_TIER_CROSSING_DEMO: no leaf found in parent octree — demo scene not built");
            }
        } else if (const char* uberDemoEnv = std::getenv("VIXEN_PROCEDURAL_UBER_DEMO")) {
            // VIXEN_PROCEDURAL_UBER_DEMO — Lazy-Procedural-Delta-Baseline Inc0 M5 Task 12 zero-bake
            // live gate, generalized (perf-scaling measurement handoff) to an ARBITRARY recipe
            // count N. Registers N registry-driven recipes (recipeId >= 2) and positions them
            // OVERLAPPING along the default camera's Z sight line (bodies at increasing world-Z,
            // same X/Y so each ray that hits the nearer body's bound sphere would ALSO have hit a
            // farther one's, giving the entryT>bestT early-reject in main() something real to
            // reject). Value selects the recipe count via std::atoi, clamped to [1, kMaxUberN] —
            // the clamp exists only to keep a garbage/huge env value from producing an unbounded
            // allocation/splice, not because the switch itself is known to have a ceiling at that
            // size (finding out where the switch DOES break is the point of this measurement).
            // NO octree bake occurs for these bodies (RegisterProceduralRecipe never touches
            // BakeSdfWorld/BuildSdfBodyOctree) — the (a) proof for the live gate.
            constexpr int kMaxUberN = 2000;
            const int requestedN = std::atoi(uberDemoEnv);
            const int n = std::clamp(requestedN <= 0 ? 3 : requestedN, 1, kMaxUberN);
            mainLogger->Info("[BuildRenderGraph] VIXEN_PROCEDURAL_UBER_DEMO: registering " +
                             std::to_string(n) + " zero-bake procedural recipes");

            // Recipe programs must grow STRUCTURALLY distinct with N (not N clones of the same
            // opcodes with different numbers) so the switch's real cost -- shader size, register
            // pressure, warp divergence, compile time -- grows honestly. Programs 0/1/2 (index
            // i%3==0/1/2) reproduce the ORIGINAL 3 legacy programs byte-for-byte (plain sphere;
            // box+sphere SmoothUnion; sphere+Round-box SmoothSubtract) so N=3 and N=10 stay
            // comparable to pre-generalization measurements. Beyond that, the generator cycles a
            // {leaf prim} x {CSG op} x {modifier} product so each subsequent id gets a genuinely
            // different opcode program.
            using Vixen::SVO::Recipe::SdfOpCode;
            using Vixen::SVO::Recipe::SdfInstruction;
            auto sphereInstr = [](glm::vec3 c, float r) {
                SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Sphere;
                in.data[0] = c.x; in.data[1] = c.y; in.data[2] = c.z; in.data[3] = r;
                return in;
            };
            auto boxInstr = [](glm::vec3 he) {
                SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Box;
                in.data[0] = he.x; in.data[1] = he.y; in.data[2] = he.z;
                return in;
            };
            auto torusInstr = [](float majorR, float minorR) {
                SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Torus;
                in.data[0] = majorR; in.data[1] = minorR;
                return in;
            };
            auto roundInstr = [](float r) {
                SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Round; in.data[0] = r;
                return in;
            };
            auto onionInstr = [](float thickness) {
                SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Onion; in.data[0] = thickness;
                return in;
            };
            auto combineInstr = [](SdfOpCode op, float k) {
                SdfInstruction in{}; in.opCode = (uint8_t)op; in.data[2] = k;
                return in;
            };
            // CSG ops beyond the legacy 2 (SmoothUnion/SmoothSubtract), cycled for i>=3.
            const SdfOpCode kExtraCsgOps[] = {
                SdfOpCode::Union, SdfOpCode::Subtract, SdfOpCode::Intersect,
                SdfOpCode::SmoothIntersect, SdfOpCode::Xor, SdfOpCode::SmoothMax,
            };

            std::vector<Vixen::SVO::BodyInstanceGpu> uberBodies;
            uberBodies.reserve(static_cast<size_t>(n));
            constexpr float kSpacingZ = 40.0f;  // overlapping bound spheres along +Z sight line
            constexpr float kBaseZ    = 30.0f;
            const glm::vec3 kColors[3] = {
                glm::vec3(1.00f, 0.55f, 0.55f),
                glm::vec3(0.55f, 1.00f, 0.55f),
                glm::vec3(0.55f, 0.70f, 1.00f),
            };
            for (int i = 0; i < n; ++i) {
                const uint32_t recipeId = static_cast<uint32_t>(2 + i);
                const float instZ = kBaseZ + kSpacingZ * static_cast<float>(i);
                const glm::vec3 center(64.0f, 64.0f, instZ);

                std::vector<SdfInstruction> prog;
                const int shape = i % 3;
                if (i < 3) {
                    // The original 3 legacy programs, byte-for-byte, so N=3/N=10 measurement
                    // points stay comparable to pre-generalization runs.
                    if (shape == 0) {
                        prog = { sphereInstr(center, 8.0f) };
                    } else if (shape == 1) {
                        prog = { boxInstr(glm::vec3(6.0f, 6.0f, 6.0f)),
                                 sphereInstr(center + glm::vec3(4.0f, 0.0f, 0.0f), 5.0f),
                                 combineInstr(SdfOpCode::SmoothUnion, 1.5f) };
                    } else {
                        prog = { sphereInstr(glm::vec3(0.0f), 9.0f),
                                 boxInstr(glm::vec3(5.0f, 5.0f, 5.0f)),
                                 roundInstr(1.0f),
                                 combineInstr(SdfOpCode::SmoothSubtract, 1.0f) };
                    }
                } else {
                    // Structurally-varied extension: cycles {sphere/box/torus} x {6 extra CSG
                    // ops} x {none/Round/Onion modifier}, with varied radii/thicknesses so no
                    // two programs beyond the legacy 3 are opcode-for-opcode identical.
                    const int leaf = (i / 3) % 3;             // 0=sphere-pair,1=box-pair,2=torus-pair
                    const SdfOpCode csgOp = kExtraCsgOps[i % 6];
                    const int modSel = (i / 6) % 3;           // 0=none,1=Round,2=Onion
                    const float k = 0.5f + 0.1f * static_cast<float>(i % 7);
                    const float r1 = 6.0f + 0.05f * static_cast<float>(i % 40);
                    const float r2 = 3.0f + 0.03f * static_cast<float>(i % 30);

                    if (leaf == 0) {
                        prog = { sphereInstr(glm::vec3(0.0f), r1),
                                 sphereInstr(glm::vec3(r2 * 0.4f, 0.0f, 0.0f), r2),
                                 combineInstr(csgOp, k) };
                    } else if (leaf == 1) {
                        prog = { boxInstr(glm::vec3(r1, r1 * 0.8f, r1 * 0.6f)),
                                 sphereInstr(glm::vec3(r2 * 0.3f, 0.0f, 0.0f), r2),
                                 combineInstr(csgOp, k) };
                    } else {
                        prog = { torusInstr(r1, r2 * 0.4f),
                                 boxInstr(glm::vec3(r2, r2, r2)),
                                 combineInstr(csgOp, k) };
                    }
                    if (modSel == 1) {
                        prog.push_back(roundInstr(0.3f + 0.02f * static_cast<float>(i % 10)));
                    } else if (modSel == 2) {
                        prog.push_back(onionInstr(0.2f + 0.02f * static_cast<float>(i % 10)));
                    }
                }

                Vixen::SVO::RecipeRegistry::RecipeEntry entry{};
                entry.bytecode = std::move(prog);
                // Every program beyond shape==0 (i<3) samples in body-local / non-absolute space
                // (Box/Torus carry no position offset of their own) -- for this demo, authoring
                // boundCenter/boundRadius explicitly sidesteps relying on
                // DeriveConservativeBounds's local-origin-relative result matching a body's
                // actual world placement (recipeParams/worldPos are NOT read for recipeId>=2;
                // the field function samples WORLD p directly, unlike the legacy analytic path).
                if (!(i < 3 && shape == 0)) {
                    entry.boundCenter = center;
                    entry.boundRadius = 12.0f;
                }

                auto regResult = RegisterProceduralRecipe(recipeId, entry);
                if (regResult != Vixen::SVO::RecipeRegistry::RegisterResult::Ok) {
                    mainLogger->Error("[BuildRenderGraph] VIXEN_PROCEDURAL_UBER_DEMO: "
                                     "RegisterProceduralRecipe(" + std::to_string(recipeId) +
                                     ") failed, code " + std::to_string(static_cast<int>(regResult)));
                    continue;
                }

                Vixen::SVO::BodyInstanceGpu inst{};
                Vixen::SVO::SetInstanceTranslationScale(inst, glm::vec3(0.0f, 0.0f, 0.0f), 1.0f);
                const glm::vec3& tint = kColors[shape];
                inst.material.color[0] = tint.x; inst.material.color[1] = tint.y; inst.material.color[2] = tint.z;
                inst.material.octreeIndex = 0u;    // unused by Procedural
                inst.material.providerKind = 1u;   // PROVIDER_PROCEDURAL
                inst.material.recipeId = recipeId; // >=2 -> routes through the spliced uber path
                // Recipe-Parameterization M2 Task 6: seed a real per-instance varying value
                // (not the previous all-zero placeholder) so a future ReadParam-using recipe
                // (M3's live demo) has something live to read; none of these N demo programs
                // use ReadParam yet, so this is inert today but proves the SSBO carries a
                // genuinely varying value end-to-end per instance.
                inst.material.recipeParams[0] = static_cast<float>(i % 10);
                uberBodies.push_back(inst);
            }

            // Recipe-Parameterization M3 Task 8: append ONE genuinely ReadParam-driven body —
            // program = { sphere(center, kReadParamDemoBaseRadius), ReadParam(0), MathSub }, i.e.
            // sd = |p-center| - kReadParamDemoBaseRadius - params[0] (MathSub is non-commutative
            // a-b; stack order [sphereSD, readParam] gives a=sphereSD, b=params[0]). Subtracting a
            // runtime-read scalar from a baked sphere's distance field is exactly a radius offset,
            // so this body's rendered radius = kReadParamDemoBaseRadius + recipeParams[0], varied
            // live in PreTick() (VIXEN_PROCEDURAL_UBER_DEMO's own sweep, gated on the SAME env var
            // — see PreTick's "Recipe-Parameterization M3 Task 8" block). recipeId = 2+n is the
            // next free id after the N structurally-varied bodies just registered above.
            // ReadParam/MathSub are deliberately NOT on the Lipschitz occupancy-grid whitelist
            // (kernel recipe analysis) — unavailable occupancy leaves occupancyGridDim=0,
            // documented non-hazard), and are NOT on DeriveConservativeBounds's whitelist either,
            // so boundCenter/boundRadius are authored explicitly here (must cover the full sweep
            // range, not just the base radius) rather than left for derivation to guess wrong.
            {
                constexpr float kReadParamDemoBaseRadius = 6.0f;
                constexpr float kReadParamDemoSweepMax   = 3.0f;  // PreTick sweeps params[0] in [-max,+max]
                const uint32_t readParamRecipeId = static_cast<uint32_t>(2 + n);
                // X-offset from body 0's own center (64,64,kBaseZ) — NOT kBaseZ-kSpacingZ (a
                // negative-Z placement that landed behind the camera/outside the frustum in the
                // first authored version of this block, confirmed by a live capture showing zero
                // visible change — fixed by placing this body at the SAME Z as the already-
                // confirmed-visible body 0, offset sideways so the two don't overlap).
                const glm::vec3 rpCenter(64.0f + 14.0f, 64.0f, kBaseZ);

                SdfInstruction rpSphere{}; rpSphere.opCode = (uint8_t)SdfOpCode::Sphere;
                rpSphere.data[0] = rpCenter.x; rpSphere.data[1] = rpCenter.y; rpSphere.data[2] = rpCenter.z;
                rpSphere.data[3] = kReadParamDemoBaseRadius;
                SdfInstruction rpRead{}; rpRead.opCode = (uint8_t)SdfOpCode::ReadParam;
                rpRead.paramMask = 1; rpRead.data[0] = 0.0f;  // index 0 into recipeParams[]
                SdfInstruction rpSub{}; rpSub.opCode = (uint8_t)SdfOpCode::MathSub;

                Vixen::SVO::RecipeRegistry::RecipeEntry rpEntry{};
                rpEntry.bytecode    = { rpSphere, rpRead, rpSub };
                rpEntry.boundCenter = rpCenter;
                rpEntry.boundRadius = kReadParamDemoBaseRadius + kReadParamDemoSweepMax + 2.0f;  // margin

                auto rpRegResult = RegisterProceduralRecipe(readParamRecipeId, rpEntry);
                if (rpRegResult != Vixen::SVO::RecipeRegistry::RegisterResult::Ok) {
                    mainLogger->Error("[BuildRenderGraph] VIXEN_PROCEDURAL_UBER_DEMO: "
                                     "RegisterProceduralRecipe(ReadParam demo, " +
                                     std::to_string(readParamRecipeId) + ") failed, code " +
                                     std::to_string(static_cast<int>(rpRegResult)));
                } else {
                    Vixen::SVO::BodyInstanceGpu rpInst{};
                    Vixen::SVO::SetInstanceUniformScale(rpInst, 1.0f);   // unused by Procedural
                    rpInst.material.color[0] = 1.0f; rpInst.material.color[1] = 0.85f; rpInst.material.color[2] = 0.2f;  // gold tint, visually distinct
                    rpInst.material.octreeIndex  = 0u;     // unused by Procedural
                    rpInst.material.providerKind = 1u;     // PROVIDER_PROCEDURAL
                    rpInst.material.recipeId     = readParamRecipeId;
                    rpInst.material.recipeParams[0] = 0.0f;  // PreTick's sweep overwrites this every frame
                    uberBodies.push_back(rpInst);
                    mainLogger->Info("[BuildRenderGraph] VIXEN_PROCEDURAL_UBER_DEMO: registered "
                                     "ReadParam demo body recipeId=" + std::to_string(readParamRecipeId) +
                                     " baseRadius=" + std::to_string(kReadParamDemoBaseRadius));
                }
            }

            if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                bodyScene->SetInstances(std::move(uberBodies));
                mainLogger->Info("[BuildRenderGraph] VIXEN_PROCEDURAL_UBER_DEMO: seeded " +
                                 std::to_string(n) + " zero-bake procedural body instances "
                                 "(0 BakeSdfWorld/BuildSdfBodyOctree calls for these bodies)");
            }
        } else if (envFlagEnabled("VIXEN_RECIPE_HOT_COLD_DEMO")) {
            // Recipe-Live-App-Bucketed-Dispatch Inc4 M3: live-gate scene construction --
            // registers a mix of "hot" recipes (>= kRecipeBucketingHotnessThreshold instances
            // each, so VIXEN_RECIPE_BUCKETED_DISPATCH's bucketing pass promotes them) and "cold"
            // recipes (1-2 instances each, staying below threshold, handled by tier-0 only).
            // Reuses VIXEN_PROCEDURAL_UBER_DEMO's own zero-bake construction convention
            // (RegisterProceduralRecipe + BodyOctreeSceneNode::SetInstances, no octree bake) --
            // this demo's own scope is just "produce a real hot+cold instance-count
            // distribution," not a new construction mechanism.
            // M4 perf-measurement note: population mix is overridable via 4 optional env vars
            // (VIXEN_RECIPE_HOT_COLD_DEMO_{HOT_RECIPES,COLD_RECIPES,HOT_INSTANCES,COLD_INSTANCES}),
            // each defaulting to the exact value M3 shipped (3/3/6/2) when unset -- so the M3 gate
            // and every prior milestone's documented scene remain byte-identical by default. This
            // is scene-construction flexibility only, not a change to the bucketing/compositing/
            // dispatch mechanism itself (out of scope for M4 per its own prompt).
            auto envOr = [](const char* name, int fallback) -> int {
                const char* v = std::getenv(name);
                if (!v) return fallback;
                int parsed = std::atoi(v);
                return parsed > 0 ? parsed : fallback;
            };
            const int kHotRecipeCount    = envOr("VIXEN_RECIPE_HOT_COLD_DEMO_HOT_RECIPES", 3);
            const int kColdRecipeCount   = envOr("VIXEN_RECIPE_HOT_COLD_DEMO_COLD_RECIPES", 3);
            const int kInstancesPerHot   = envOr("VIXEN_RECIPE_HOT_COLD_DEMO_HOT_INSTANCES", 6);
            const int kInstancesPerCold  = envOr("VIXEN_RECIPE_HOT_COLD_DEMO_COLD_INSTANCES", 2);
            constexpr float kSpacingX = 30.0f;
            constexpr float kSpacingZ = 40.0f;
            constexpr float kBaseZ    = 30.0f;
            // Fixed-size palettes, indexed modulo when a M4 measurement run asks for more
            // recipes than the original M3 demo's 3+3 -- color repeats across indices past 3,
            // which is fine for an FPS measurement (colors are a visual-attribution aid only).
            static const glm::vec3 kHotColors[3] = {
                glm::vec3(1.00f, 0.30f, 0.30f),  // hot recipe 0: red
                glm::vec3(0.30f, 1.00f, 0.30f),  // hot recipe 1: green
                glm::vec3(0.30f, 0.45f, 1.00f),  // hot recipe 2: blue
            };
            static const glm::vec3 kColdColors[3] = {
                glm::vec3(1.00f, 1.00f, 0.30f),  // cold recipe 0: yellow
                glm::vec3(1.00f, 0.30f, 1.00f),  // cold recipe 1: magenta
                glm::vec3(0.30f, 1.00f, 1.00f),  // cold recipe 2: cyan
            };

            std::vector<Vixen::SVO::BodyInstanceGpu> hotColdBodies;
            uint32_t nextRecipeId = 2;
            int totalInstances = 0;

            auto registerAndSeed = [&](uint32_t recipeId, const glm::vec3& tint, float centerX, int instanceCount) {
                using Vixen::SVO::Recipe::SdfOpCode;
                using Vixen::SVO::Recipe::SdfInstruction;
                SdfInstruction sphere{};
                sphere.opCode = static_cast<uint8_t>(SdfOpCode::Sphere);
                sphere.data[0] = centerX; sphere.data[1] = 64.0f; sphere.data[2] = kBaseZ;
                sphere.data[3] = 6.0f;

                Vixen::SVO::RecipeRegistry::RecipeEntry entry{};
                entry.bytecode = { sphere };
                entry.boundCenter = glm::vec3(centerX, 64.0f, kBaseZ);
                entry.boundRadius = 8.0f;

                auto regResult = RegisterProceduralRecipe(recipeId, entry);
                if (regResult != Vixen::SVO::RecipeRegistry::RegisterResult::Ok) {
                    mainLogger->Error("[BuildRenderGraph] VIXEN_RECIPE_HOT_COLD_DEMO: "
                                     "RegisterProceduralRecipe(" + std::to_string(recipeId) +
                                     ") failed, code " + std::to_string(static_cast<int>(regResult)));
                    return;
                }
                for (int k = 0; k < instanceCount; ++k) {
                    Vixen::SVO::BodyInstanceGpu inst{};
                    inst.material.color[0] = tint.x; inst.material.color[1] = tint.y; inst.material.color[2] = tint.z;
                    inst.material.octreeIndex = 0u;
                    inst.material.providerKind = 1u;  // PROVIDER_PROCEDURAL
                    inst.material.recipeId = recipeId;
                    // Each instance's own world Z offset so N>1 instances of one recipe are
                    // screen-space-separated rather than perfectly overlapping (still all within
                    // the recipe's shared boundCenter/boundRadius footprint above -- overlapping
                    // bound SPHERES are fine, that's just conservative bucketing coverage).
                    Vixen::SVO::SetInstanceTranslationScale(inst, glm::vec3(0.0f, 0.0f, static_cast<float>(k) * 3.0f), 1.0f);
                    hotColdBodies.push_back(inst);
                    ++totalInstances;
                }
            };

            for (int h = 0; h < kHotRecipeCount; ++h) {
                registerAndSeed(nextRecipeId++, kHotColors[h % 3], 40.0f + static_cast<float>(h) * kSpacingX, kInstancesPerHot);
            }
            for (int c = 0; c < kColdRecipeCount; ++c) {
                registerAndSeed(nextRecipeId++, kColdColors[c % 3], 40.0f + static_cast<float>(kHotRecipeCount + c) * kSpacingX, kInstancesPerCold);
            }

            if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                bodyScene->SetInstances(std::move(hotColdBodies));
                mainLogger->Info("[BuildRenderGraph] VIXEN_RECIPE_HOT_COLD_DEMO: seeded " +
                                 std::to_string(totalInstances) + " instances across " +
                                 std::to_string(kHotRecipeCount) + " hot recipes (" +
                                 std::to_string(kInstancesPerHot) + " instances each) + " +
                                 std::to_string(kColdRecipeCount) + " cold recipes (" +
                                 std::to_string(kInstancesPerCold) + " instances each)");
            }
        } else if (const char* diversityDemoEnv = std::getenv("VIXEN_RECIPE_DIVERSITY_STRESS_DEMO")) {
            // Recipe-Diversity-Stress-Scene Inc6 M2 — generalizes M1's hand-authored
            // [ReadParamFloat3, DeclarePosition, <resolve segment>] contract (commit 754442d1)
            // from ONE recipe to N=20-250 genuinely-diverging, spatially-distributed recipes,
            // rendered live. A NEW gated demo (not an extension of VIXEN_PROCEDURAL_UBER_DEMO
            // in place), per the M2 prompt's own "your call, document which" instruction — kept
            // separate so the uber-demo's own N=100/500 measurement baseline (stacked-Z-line,
            // no DeclarePosition) stays byte-identical for anyone still using it as a reference.
            //
            // Recipe-diversity generation: reuses VIXEN_PROCEDURAL_UBER_DEMO's OWN shape/CSG/
            // modifier product generator verbatim (same {sphere/box/torus} x {6 extra CSG ops}
            // x {none/Round/Onion} cycle, same legacy-3 byte-for-byte preservation for i<3) --
            // per the plan doc's own Risk: "do not let recipe-diversity generation collapse
            // into N copies with different literals." The ONLY change from the uber-demo's
            // generator is that every program here is prefixed with the meta segment
            // [ReadParamFloat3(idx=0), DeclarePosition] (M1's proven mechanism) instead of
            // baking each shape's own world-space center into its data[] literals -- position
            // is now genuinely ReadParam-sourced, not a baked constant, exactly the gap M1's
            // prototype closed. Every generated shape instruction below is therefore authored
            // in BODY-LOCAL space (center at the origin), consistent with DeclarePosition's
            // eval-time translation (curPos -= declaredPos) applying BEFORE the resolve segment
            // walks -- see the RecipeServices GPU lowerer's DeclarePosition handling and
            // SdfRecipeEval.h's mirror.
            //
            // NOTE on the production wiring gap M1 left open: UberShaderSplice.h's
            // SpliceProceduralRecipesIntoSource (the actual production emitter this demo's
            // recipes go through, via BodyInstanceRayMarch.comp's VIXEN_UBER_RECIPE_SPLICE_MARKER)
            // used to call the retired local GLSL emitter without a declared-position out-param
            // — M1's out-param plumbing was only proven in a standalone test shader
            // (test_recipe_declared_position_render.cpp / test_recipe_glsl_numerical_parity.cpp's
            // own ComposeComputeShaderWithDeclaredPosition), not the real evalRecipeField() switch
            // TraceWorld.glsl actually calls; a DeclarePosition-using recipe would have failed
            // the lowerer's own validation or emitted GLSL referencing an
            // undeclared `declaredPos` identifier (Release). Fixed centrally in
            // UberShaderSplice.h (not here): SpliceProceduralRecipesIntoSource now detects
            // per-recipe whether its bytecode contains DeclarePosition and, only for those
            // recipes, emits with emitDeclaredPositionOutParam=true and supplies a throwaway
            // local (`unusedDeclaredPos`) at evalRecipeField's call site — every other recipe
            // (every pre-Inc6 demo, and any Inc6 recipe that happens not to use DeclarePosition)
            // keeps the original 3-arg call shape byte-identical. This is a correctness fix
            // required for ANY DeclarePosition-using recipe to compile in production, not new
            // machinery scoped to this demo alone.
            constexpr int kMaxDiversityN = 250;  // plan doc's own documented N ceiling for Inc6
            const int requestedDivN = std::atoi(diversityDemoEnv);
            const int diversityN = std::clamp(requestedDivN <= 0 ? 20 : requestedDivN, 1, kMaxDiversityN);

            // --- 192-total-instance ceiling decision (documented per M2 prompt's requirement) ---
            // TraceWorld.glsl's tier-0 march hard-clamps numInstances = clamp(pc.instanceCount, 0,
            // 3*64) = 192 total body instances, across ALL recipes combined. Decision: register
            // ALL `diversityN` distinct recipe PROGRAMS (registration itself has no hard cap --
            // RecipeRegistry is an unbounded std::map, per this plan's own §1 Grounding), but
            // instantiate exactly ONE BodyInstanceGpu per registered recipe, i.e.
            // instantiatedInstanceCount == registeredRecipeCount == diversityN, for the WHOLE
            // documented N=20-250 range. This is option (b) from the M2 prompt ("cap actually-
            // instantiated recipes at 192 and document that as the real ceiling for this scene's
            // N range") specialized further: since 250 <= 192 is FALSE, N is explicitly clamped
            // to min(diversityN, 192) for INSTANTIATION even though up to kMaxDiversityN=250
            // distinct programs could in principle be registered -- chosen over option (a)
            // (register 250, instantiate a rotating subset <=192) because a static subset-
            // selection policy is exactly the kind of extra machinery ("which/how chosen") the
            // prompt warns adds scope for no M2 benefit; M4's later sweep can choose to run at
            // N=192 as its practical top end (already comfortably inside "approaching but below
            // the documented N=500 driver-hang territory" per the plan's own framing) without
            // this milestone inventing a rotation scheme it would just have to redo later.
            // 1:1 registered-recipe:instantiated-instance is also the SIMPLEST correct shape for
            // BOTH ends of the range (per the prompt's own "pick the simplest approach" framing):
            // it needs no per-N branching between "multiple instances per recipe at low N" and
            // "fewer instances per recipe at high N" -- every recipe in this demo is genuinely
            // UNIQUE (that's the entire point of "diversity"), so there is no meaningful sense in
            // which cloning multiple instances of the SAME recipe would add more diversity value;
            // it would only add instance COUNT, which is not what this milestone is measuring.
            const int diversityInstantiated = std::min(diversityN, 192);
            if (diversityInstantiated < diversityN) {
                mainLogger->Info("[BuildRenderGraph] VIXEN_RECIPE_DIVERSITY_STRESS_DEMO: "
                                 "requested N=" + std::to_string(diversityN) + " exceeds the "
                                 "192-instance tier-0 ceiling -- registering " +
                                 std::to_string(diversityN) + " distinct recipe programs but "
                                 "instantiating only the first " + std::to_string(diversityInstantiated));
            }
            mainLogger->Info("[BuildRenderGraph] VIXEN_RECIPE_DIVERSITY_STRESS_DEMO: registering " +
                             std::to_string(diversityN) + " diverse ReadParamFloat3/DeclarePosition "
                             "recipes, instantiating " + std::to_string(diversityInstantiated));

            // --- Spatial distribution: 2D grid in the XZ plane (Y fixed), spacing large enough
            // that neighboring bodies' bound spheres (radius kGridBoundRadius below) don't
            // overlap. ceil(sqrt(N)) columns x however many rows needed, centered on the SAME
            // world-space region the other demos use (around world (64,64,*)) so this demo's
            // camera preset (below) has a well-known scene extent to frame.
            constexpr float kGridSpacing     = 30.0f;  // > 2*kGridBoundRadius, no bound-sphere overlap
            constexpr float kGridBoundRadius = 12.0f;  // matches uber-demo's own non-legacy bound radius
            constexpr float kGridBaseY       = 64.0f;
            // M3: per-instance runtime-mutated shape parameter (recipeParams[3], see the
            // ReadParam/MathSub program suffix above) sweeps in [-kShapeParamSweepMax,
            // +kShapeParamSweepMax] every frame (VulkanGraphApplication::PreTick) -- boundRadius
            // must cover the full swept range, same reasoning as the uber-demo's own ReadParam
            // body (kReadParamDemoBaseRadius + kReadParamDemoSweepMax + margin).
            constexpr float kShapeParamSweepMax = 3.0f;
            // M3: a subset of instances also gets its DECLARED POSITION animated every frame
            // (orbiting around its own grid slot) -- exercising the spatial contract's own
            // stated value proposition (a moving, ReadParam-sourced position) rather than only
            // an unrelated shape parameter, per the M3 prompt's own framing ("the actual point
            // of the milestone"). Every 4th instance (index % 4 == 0) is picked: a meaningful
            // fraction (~25%) without animating literally every body, so the live-run capture
            // can visually distinguish "still" grid neighbors from "orbiting" ones at both N
            // extremes -- documented here, not left implicit.
            constexpr int kAnimatedPositionStride = 4;
            const int gridCols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(diversityN)))));
            const int gridRows = (diversityN + gridCols - 1) / gridCols;
            const float gridWidth  = static_cast<float>(gridCols - 1) * kGridSpacing;
            const float gridDepth  = static_cast<float>(gridRows - 1) * kGridSpacing;
            const float gridOriginX = 64.0f - gridWidth  * 0.5f;
            const float gridOriginZ = 64.0f - gridDepth * 0.5f;

            using Vixen::SVO::Recipe::SdfOpCode;
            using Vixen::SVO::Recipe::SdfInstruction;
            auto sphereInstr = [](glm::vec3 c, float r) {
                SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Sphere;
                in.data[0] = c.x; in.data[1] = c.y; in.data[2] = c.z; in.data[3] = r;
                return in;
            };
            auto boxInstr = [](glm::vec3 he) {
                SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Box;
                in.data[0] = he.x; in.data[1] = he.y; in.data[2] = he.z;
                return in;
            };
            auto torusInstr = [](float majorR, float minorR) {
                SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Torus;
                in.data[0] = majorR; in.data[1] = minorR;
                return in;
            };
            auto roundInstr = [](float r) {
                SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Round; in.data[0] = r;
                return in;
            };
            auto onionInstr = [](float thickness) {
                SdfInstruction in{}; in.opCode = (uint8_t)SdfOpCode::Onion; in.data[0] = thickness;
                return in;
            };
            auto combineInstr = [](SdfOpCode op, float k) {
                SdfInstruction in{}; in.opCode = (uint8_t)op; in.data[2] = k;
                return in;
            };
            const SdfOpCode kExtraCsgOps[] = {
                SdfOpCode::Union, SdfOpCode::Subtract, SdfOpCode::Intersect,
                SdfOpCode::SmoothIntersect, SdfOpCode::Xor, SdfOpCode::SmoothMax,
            };
            const glm::vec3 kColors[3] = {
                glm::vec3(1.00f, 0.55f, 0.55f),
                glm::vec3(0.55f, 1.00f, 0.55f),
                glm::vec3(0.55f, 0.70f, 1.00f),
            };

            std::vector<Vixen::SVO::BodyInstanceGpu> diversityBodies;
            diversityBodies.reserve(static_cast<size_t>(diversityInstantiated));
            int registeredCount = 0;
            for (int i = 0; i < diversityN; ++i) {
                const uint32_t recipeId = static_cast<uint32_t>(2 + i);
                const int col = i % gridCols;
                const int row = i / gridCols;
                const glm::vec3 declaredPos(
                    gridOriginX + kGridSpacing * static_cast<float>(col),
                    kGridBaseY,
                    gridOriginZ + kGridSpacing * static_cast<float>(row));

                // Resolve segment: SAME shape/CSG/modifier generator as
                // VIXEN_PROCEDURAL_UBER_DEMO, authored in BODY-LOCAL space (center at origin) --
                // DeclarePosition's eval-time translation (curPos -= declaredPos) supplies the
                // world placement, so these shape literals must NOT also bake a world center.
                std::vector<SdfInstruction> resolveProg;
                const int shape = i % 3;
                if (i < 3) {
                    if (shape == 0) {
                        resolveProg = { sphereInstr(glm::vec3(0.0f), 8.0f) };
                    } else if (shape == 1) {
                        resolveProg = { boxInstr(glm::vec3(6.0f, 6.0f, 6.0f)),
                                        sphereInstr(glm::vec3(4.0f, 0.0f, 0.0f), 5.0f),
                                        combineInstr(SdfOpCode::SmoothUnion, 1.5f) };
                    } else {
                        resolveProg = { sphereInstr(glm::vec3(0.0f), 9.0f),
                                        boxInstr(glm::vec3(5.0f, 5.0f, 5.0f)),
                                        roundInstr(1.0f),
                                        combineInstr(SdfOpCode::SmoothSubtract, 1.0f) };
                    }
                } else {
                    const int leaf = (i / 3) % 3;
                    const SdfOpCode csgOp = kExtraCsgOps[i % 6];
                    const int modSel = (i / 6) % 3;
                    const float k = 0.5f + 0.1f * static_cast<float>(i % 7);
                    const float r1 = 6.0f + 0.05f * static_cast<float>(i % 40);
                    const float r2 = 3.0f + 0.03f * static_cast<float>(i % 30);

                    if (leaf == 0) {
                        resolveProg = { sphereInstr(glm::vec3(0.0f), r1),
                                        sphereInstr(glm::vec3(r2 * 0.4f, 0.0f, 0.0f), r2),
                                        combineInstr(csgOp, k) };
                    } else if (leaf == 1) {
                        resolveProg = { boxInstr(glm::vec3(r1, r1 * 0.8f, r1 * 0.6f)),
                                        sphereInstr(glm::vec3(r2 * 0.3f, 0.0f, 0.0f), r2),
                                        combineInstr(csgOp, k) };
                    } else {
                        resolveProg = { torusInstr(r1, r2 * 0.4f),
                                        boxInstr(glm::vec3(r2, r2, r2)),
                                        combineInstr(csgOp, k) };
                    }
                    if (modSel == 1) {
                        resolveProg.push_back(roundInstr(0.3f + 0.02f * static_cast<float>(i % 10)));
                    } else if (modSel == 2) {
                        resolveProg.push_back(onionInstr(0.2f + 0.02f * static_cast<float>(i % 10)));
                    }
                }

                // Meta segment (M1's proven contract): ReadParamFloat3(idx=0) pushes
                // recipeParams[0..2] as a float3, DeclarePosition pops it, assigns the GLSL
                // out-param, and translates the sample point for the resolve segment that
                // follows -- NOT a baked literal, per the M2 prompt's own "this is the actual
                // point of the milestone" framing.
                SdfInstruction readPos{}; readPos.opCode = (uint8_t)SdfOpCode::ReadParamFloat3;
                readPos.paramMask = 1; readPos.data[0] = 0.0f;
                SdfInstruction declarePos{}; declarePos.opCode = (uint8_t)SdfOpCode::DeclarePosition;

                // M3: every instance also gets a genuinely runtime-mutated SHAPE parameter --
                // recipeParams[3] (a scalar, ReadParam idx=3; idx 0 is reserved by the float3
                // declared-position read above, so idx=3 lands past that 3-slot range with no
                // aliasing), subtracted from the resolve segment's own final SDF value via
                // MathSub (a-b, a=resolveSegment result, b=params[3]) -- the exact same shape
                // (sphere - ReadParam) VIXEN_PROCEDURAL_UBER_DEMO's single swept-radius body
                // already proves (Recipe-Parameterization P4 M3 Task 8), just applied uniformly
                // after an arbitrary CSG/modifier resolve segment instead of a bare sphere. This
                // is genuinely consumed (perturbs the rendered iso-surface every frame via
                // VulkanGraphApplication::PreTick's sweep below), not a disconnected value.
                SdfInstruction readShapeParam{}; readShapeParam.opCode = (uint8_t)SdfOpCode::ReadParam;
                readShapeParam.paramMask = 1; readShapeParam.data[0] = 3.0f;
                SdfInstruction subShapeParam{}; subShapeParam.opCode = (uint8_t)SdfOpCode::MathSub;

                std::vector<SdfInstruction> prog;
                prog.reserve(resolveProg.size() + 4);
                prog.push_back(readPos);
                prog.push_back(declarePos);
                prog.insert(prog.end(), resolveProg.begin(), resolveProg.end());
                prog.push_back(readShapeParam);
                prog.push_back(subShapeParam);

                Vixen::SVO::RecipeRegistry::RecipeEntry entry{};
                entry.bytecode = std::move(prog);
                // boundCenter/boundRadius are AUTHORED explicitly (matches this instance's own
                // declared grid position) rather than derived -- ReadParamFloat3/DeclarePosition
                // are not on DeriveConservativeBounds's whitelist (same reasoning
                // VIXEN_PROCEDURAL_UBER_DEMO's own ReadParam demo body comment gives), and per
                // the M2 prompt's own scope boundary this milestone does NOT touch RecipeEntry's
                // boundCenter/boundRadius handling or attempt to resolve AABB coexistence -- it
                // just authors the existing fields with this instance's placement, the same
                // mechanism every prior demo already uses for a non-whitelisted program.
                entry.boundCenter = declaredPos;
                // Margin covers both the M3 shape-param sweep (kShapeParamSweepMax) and, for the
                // animated-position subset, the orbit radius (kOrbitRadius, defined below near
                // the PreTick-mirrored sweep constants) -- boundRadius must bound the instance's
                // full swept footprint, not just its resting grid position.
                entry.boundRadius = kGridBoundRadius + kShapeParamSweepMax + 6.0f;

                auto regResult = RegisterProceduralRecipe(recipeId, entry);
                if (regResult != Vixen::SVO::RecipeRegistry::RegisterResult::Ok) {
                    mainLogger->Error("[BuildRenderGraph] VIXEN_RECIPE_DIVERSITY_STRESS_DEMO: "
                                     "RegisterProceduralRecipe(" + std::to_string(recipeId) +
                                     ") failed, code " + std::to_string(static_cast<int>(regResult)));
                    continue;
                }
                ++registeredCount;

                if (i < diversityInstantiated) {
                    Vixen::SVO::BodyInstanceGpu inst{};
                    Vixen::SVO::SetInstanceTranslationScale(inst, glm::vec3(0.0f, 0.0f, 0.0f), 1.0f);
                    const glm::vec3& tint = kColors[shape];
                    inst.material.color[0] = tint.x; inst.material.color[1] = tint.y; inst.material.color[2] = tint.z;
                    inst.material.octreeIndex = 0u;    // unused by Procedural
                    inst.material.providerKind = 1u;   // PROVIDER_PROCEDURAL
                    inst.material.recipeId = recipeId;
                    // recipeParams[0..2] = the declared world position, sourced via
                    // ReadParamFloat3(idx=0) inside the recipe above -- M2 supplies this ONCE at
                    // scene setup (a static-per-run spatial layout, per the prompt's own "does
                    // not need to animate every frame" scope note); M3 will later mutate this
                    // same field per-frame via the identical SetInstances() path, not a new one.
                    inst.material.recipeParams[0] = declaredPos.x;
                    inst.material.recipeParams[1] = declaredPos.y;
                    inst.material.recipeParams[2] = declaredPos.z;
                    inst.material.recipeParams[3] = 0.0f;  // M3: PreTick's per-frame sweep overwrites this
                    diversityBodies.push_back(inst);
                }
            }

            if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                bodyScene->SetInstances(std::move(diversityBodies));
                mainLogger->Info("[BuildRenderGraph] VIXEN_RECIPE_DIVERSITY_STRESS_DEMO: registered " +
                                 std::to_string(registeredCount) + "/" + std::to_string(diversityN) +
                                 " distinct recipe programs, seeded " +
                                 std::to_string(diversityInstantiated) + " body instances on a " +
                                 std::to_string(gridCols) + "x" + std::to_string(gridRows) + " grid "
                                 "(spacing=" + std::to_string(kGridSpacing) + ")");
            }
        } else if (envFlagEnabled("VIXEN_TIER_CHAIN_DEMO")) {
            // Tiered-ESVO Inc3 M3 Task 5 live gate: a THREE-tree chain, T0 -> T1 -> T2,
            // reusing the EXACT construction pattern the two-tree VIXEN_TIER_CROSSING_DEMO
            // above already live-gates (SDF sphere per tree, magenta/color-override for
            // per-tier visual attribution, MarkLeafAsTierCrossing on every root-facing leaf,
            // manual ConcatenatedOctrees bookkeeping) — extended to a SECOND crossing:
            // T0's marked leaf points at T1 (slot 1), and T1's OWN marked leaf points at
            // T2 (slot 2). Distinct per-tier colors (parent: default cosine-gradient;
            // T1: solid green; T2: solid cyan) make each hop's contribution visually
            // attributable in a capture, matching Inc2 M3's own "distinct color per tier"
            // discipline (this milestone's own plan §M3 gate requirement).
            mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_CHAIN_DEMO: building hand-authored three-tree chained tier-crossing scene");

            constexpr int   kN          = 16;
            constexpr int   kBrickDepth = 3;
            const glm::vec3 kCenter(8.0f, 8.0f, 8.0f);

            auto bakeSphereTree = [&](float radius) {
                Vixen::SVO::RecipeParams rp{};
                rp.radius = radius;
                Vixen::SVO::SdfBakeResult baked =
                    Vixen::SVO::BakeRecipeToSdfWorld(Vixen::SVO::RECIPE_SPHERE, kCenter, rp, kN, 2.0f);
                return Vixen::SVO::BuildSdfBodyOctree(baked, kBrickDepth);
            };

            Vixen::SVO::SdfBodyOctree t0Body = bakeSphereTree(6.0f);
            Vixen::SVO::SdfBodyOctree t1Body = bakeSphereTree(6.5f);
            Vixen::SVO::SdfBodyOctree t2Body = bakeSphereTree(7.2f);

            Vixen::SVO::SerializedOctree t0Ser = Vixen::SVO::SerializeSdf(t0Body);
            Vixen::SVO::SerializedOctree t1Ser = Vixen::SVO::SerializeSdf(t1Body);
            Vixen::SVO::SerializedOctree t2Ser = Vixen::SVO::SerializeSdf(t2Body);

            // Per-tier solid color override (parent T0 keeps the shared cosine-gradient;
            // T1 solid green, T2 solid cyan — distinct from each other AND from T0's
            // muted-rainbow default, per the plan's "distinct color per tier so each hop
            // is visually attributable" gate requirement).
            auto overrideColor = [&](Vixen::SVO::SerializedOctree& ser, glm::vec3 rgb, const char* label) {
                const uint32_t colorBase = ser.channelBaseFloats(Vixen::SVO::SEM_COLOR);
                if (colorBase == 0xFFFFFFFFu) {
                    mainLogger->Error(std::string("[BuildRenderGraph] VIXEN_TIER_CHAIN_DEMO: ") + label
                                      + " has no SEM_COLOR channel — color override skipped");
                    return;
                }
                float* pool = reinterpret_cast<float*>(ser.channelPool.data());
                const size_t poolFloats = ser.channelPool.size() / sizeof(float);
                for (uint32_t brick = 0; brick < ser.brickCount; ++brick) {
                    for (uint32_t comp = 0; comp < 3; ++comp) {
                        const float c = rgb[static_cast<int>(comp)];
                        for (uint32_t voxel = 0; voxel < Vixen::SVO::SerializedOctree::kVoxelsPerBrick; ++voxel) {
                            const size_t idx = static_cast<size_t>(brick) * ser.brickStrideFloats
                                             + colorBase + comp * Vixen::SVO::SerializedOctree::kVoxelsPerBrick + voxel;
                            if (idx < poolFloats) pool[idx] = c;
                        }
                    }
                }
            };
            overrideColor(t1Ser, glm::vec3(0.0f, 1.0f, 0.0f), "T1");  // solid green
            overrideColor(t2Ser, glm::vec3(0.0f, 1.0f, 1.0f), "T2");  // solid cyan

            // Mip pools (M4 gate reuse: shadeFromMipSample needs real coverage for the
            // LOD/residency fallback paths, exactly like VIXEN_TIER_CROSSING_DEMO above).
            if (const Vixen::SVO::Octree* oct0 = t0Body.octree->getOctree()) Vixen::SVO::BakeAndAttachMipPool(*oct0, t0Ser);
            if (const Vixen::SVO::Octree* oct1 = t1Body.octree->getOctree()) Vixen::SVO::BakeAndAttachMipPool(*oct1, t1Ser);
            if (const Vixen::SVO::Octree* oct2 = t2Body.octree->getOctree()) Vixen::SVO::BakeAndAttachMipPool(*oct2, t2Ser);

            // Locate a camera-facing leaf in T0's root (same octant-selection convention
            // as VIXEN_TIER_CROSSING_DEMO above) and mark it -> T1 (slot 1).
            auto findCameraFacingLeaf = [](const Vixen::SVO::Octree* oct, uint32_t& outDescIdx, int& outOctant) {
                outOctant = -1;
                if (oct == nullptr) return;
                const auto& descs = oct->root->childDescriptors;
                for (uint32_t i = 0; i < descs.size() && outOctant < 0; ++i) {
                    const Vixen::SVO::ChildDescriptor& d = descs[i];
                    for (int o = 4; o < 8; ++o) {
                        if (d.hasChild(o) && d.isLeaf(o)) { outDescIdx = i; outOctant = o; break; }
                    }
                }
                if (outOctant < 0) {
                    for (uint32_t i = 0; i < descs.size() && outOctant < 0; ++i) {
                        const Vixen::SVO::ChildDescriptor& d = descs[i];
                        for (int o = 0; o < 8; ++o) {
                            if (d.hasChild(o) && d.isLeaf(o)) { outDescIdx = i; outOctant = o; break; }
                        }
                    }
                }
            };

            uint32_t t0MarkDescIdx = 0; int t0MarkOctant = -1;
            findCameraFacingLeaf(t0Body.octree->getOctree(), t0MarkDescIdx, t0MarkOctant);
            uint32_t t1MarkDescIdx = 0; int t1MarkOctant = -1;
            findCameraFacingLeaf(t1Body.octree->getOctree(), t1MarkDescIdx, t1MarkOctant);

            if (t0MarkOctant >= 0 && t1MarkOctant >= 0) {
                // Hop 0: T0's marked leaf -> T1 (slot 1). childScale=1.0 (same-scale
                // chaining — Inc3 M4's job is the scale-magnified version; M3 proves the
                // HOP LOOP mechanism itself, same discipline as Inc2 M3 proving the
                // single-restart mechanism before Inc2 M4/Inc3 M1-M2 added LOD/scale).
                // childOriginLocal is the marked leaf's OWN cell center (Inc3 M5 Task 9
                // fix — see RootLeafOctantCenterLocal's header comment); at childScale=1.0
                // this is a no-op vs. the pre-M5 constant (1.5,1.5,1.5) for hop 0's own
                // proof (both formulas agree exactly at unity — see M1's own byte-
                // identical-at-unity gate), so this hop is UNCHANGED in observable behavior.
                const glm::vec3 t0LeafCenterLocal = Vixen::SVO::RootLeafOctantCenterLocal(t0MarkOctant);
                Vixen::SVO::TierRef refT0ToT1{};
                refT0ToT1.childOctreeIndex = 1u;
                refT0ToT1.childOriginLocal[0] = t0LeafCenterLocal.x;
                refT0ToT1.childOriginLocal[1] = t0LeafCenterLocal.y;
                refT0ToT1.childOriginLocal[2] = t0LeafCenterLocal.z;
                refT0ToT1.childScale = 1.0f;
                Vixen::SVO::MarkLeafAsTierCrossing(t0Ser, t0MarkDescIdx, t0MarkOctant, refT0ToT1, 22);

                // Hop 1: T1's OWN marked leaf -> T2 (slot 2, T1's own child-slot
                // numbering — ConcatenatedOctrees resolves childOctreeIndex against the
                // GLOBAL concatenated configs[] array, so this is genuinely slot 2, not
                // slot 1 relative to T1). Same M5 leaf-center fix, same unity no-op.
                const glm::vec3 t1LeafCenterLocal = Vixen::SVO::RootLeafOctantCenterLocal(t1MarkOctant);
                Vixen::SVO::TierRef refT1ToT2{};
                refT1ToT2.childOctreeIndex = 2u;
                refT1ToT2.childOriginLocal[0] = t1LeafCenterLocal.x;
                refT1ToT2.childOriginLocal[1] = t1LeafCenterLocal.y;
                refT1ToT2.childOriginLocal[2] = t1LeafCenterLocal.z;
                refT1ToT2.childScale = 1.0f;
                Vixen::SVO::MarkLeafAsTierCrossing(t1Ser, t1MarkDescIdx, t1MarkOctant, refT1ToT2, 22);

                // Manual 3-tree concatenation (parent=slot0, T1=slot1, T2=slot2) — same
                // per-octree bookkeeping loop as the two-tree demo above, generalized to 3.
                Vixen::SVO::ConcatenatedOctrees cat;
                cat.count = 3;
                cat.configs.resize(3);
                cat.nodeCounts.resize(3);
                cat.brickCounts.resize(3);
                cat.tierRefCounts.resize(3);

                Vixen::SVO::SerializedOctree* octs[3] = {&t0Ser, &t1Ser, &t2Ser};
                uint32_t nodeBase = 0, brickBase = 0, poolBase = 0, tierRefBase = 0, mipPoolBase = 0;
                for (int k = 0; k < 3; ++k) {
                    Vixen::SVO::SerializedOctree& s = *octs[k];
                    s.config.nodeArrayBase  = static_cast<int32_t>(nodeBase);
                    s.config.brickArrayBase = static_cast<int32_t>(brickBase);
                    Vixen::SVO::setSdfBrickArrayBase(s.config, poolBase);
                    Vixen::SVO::setTierRefTableBase(s.config, tierRefBase);
                    Vixen::SVO::setMipPoolBase(s.config, mipPoolBase);

                    cat.configs[k]       = s.config;
                    cat.nodeCounts[k]    = s.nodeCount;
                    cat.brickCounts[k]   = s.brickCount;
                    cat.tierRefCounts[k] = static_cast<uint32_t>(s.tierRefs.size());

                    cat.nodes.insert(cat.nodes.end(), s.nodes.begin(), s.nodes.end());
                    cat.bricks.insert(cat.bricks.end(), s.bricks.begin(), s.bricks.end());
                    cat.channelPool.insert(cat.channelPool.end(), s.channelPool.begin(), s.channelPool.end());
                    cat.brickGridLookup.insert(cat.brickGridLookup.end(), s.brickGridLookup.begin(), s.brickGridLookup.end());
                    cat.tierRefTable.insert(cat.tierRefTable.end(), s.tierRefs.begin(), s.tierRefs.end());
                    cat.mipPool.insert(cat.mipPool.end(), s.mipPool.begin(), s.mipPool.end());

                    if (cat.materials.empty()) {
                        cat.materials = s.materials;
                    }

                    nodeBase    += s.nodeCount;
                    brickBase   += s.brickCount;
                    poolBase    += s.brickCount * s.brickStrideFloats;
                    tierRefBase += static_cast<uint32_t>(s.tierRefs.size());
                    mipPoolBase += s.nodeCount * s.channelCount;
                }

                if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                    bodyScene->SetRecipePool(std::move(cat));

                    constexpr float kRenderScale = 4.8f;
                    constexpr float kHalf = 5.0f * kRenderScale;
                    Vixen::SVO::BodyInstanceGpu inst{};
                    Vixen::SVO::SetInstanceTranslationScale(inst, glm::vec3(64.0f - kHalf, 64.0f - kHalf, 64.0f - kHalf), kRenderScale);
                    inst.material.color[0]     = 1.0f;
                    inst.material.color[1]     = 1.0f;
                    inst.material.color[2]     = 1.0f;
                    inst.material.octreeIndex  = 0u;    // parent (T0) tree
                    inst.material.providerKind = 0u;    // PROVIDER_STORED
                    inst.material.recipeId     = 0u;

                    bodyScene->SetInstances({inst});
                    mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_CHAIN_DEMO: T0 leaf ("
                                  + std::to_string(t0MarkDescIdx) + "," + std::to_string(t0MarkOctant)
                                  + ") -> T1 octree1; T1 leaf (" + std::to_string(t1MarkDescIdx) + ","
                                  + std::to_string(t1MarkOctant) + ") -> T2 octree2");
                }
            } else {
                mainLogger->Error("[BuildRenderGraph] VIXEN_TIER_CHAIN_DEMO: no camera-facing leaf found in T0 or T1 — demo scene not built");
            }
        } else if (envFlagEnabled("VIXEN_TIER_EARTH_DEMO")) {
            // Tiered-ESVO Inc3 M4 Task 6 (the epic gate): the SAME T0->T1->T2 chained
            // construction as VIXEN_TIER_CHAIN_DEMO above, but at the REAL per-hop tier
            // ratio the epic exists for (childScale=2^-10 at BOTH hops, not M3's
            // proof-of-mechanism 1.0) — the "Earth-diameter-scale" demonstration.
            //
            // Numeric derivation (hand-computed BEFORE this scene was built, per this
            // increment's prediction-first discipline; full trace in the milestone's
            // Progress Log / Tiered-ESVO-Inc3-M4-earth-scale-derivation.py):
            //   - T0's own world diameter (existing convention: kRenderScale=4.8 *
            //     kWorldGridSize=10) = 48.0 world units. Declaring this span AS Earth's
            //     actual diameter (12,742 km) fixes 1 world unit = 265,458 m.
            //   - Hop 0 (T0->T1, childScale=2^-10): T1's own world diameter =
            //     48.0 * 2^-10 = 0.046875 units = 12,443 m (~12.4 km, a "region" tier).
            //   - Hop 1 (T1->T2, childScale=2^-10): T2's own world diameter =
            //     0.046875 * 2^-10 ~= 4.578e-5 units ~= 12.15 m (a "bedrock" tier);
            //     T2's own single brick spans ~6.08 m, a single voxel ~0.76 m.
            //   - Total scale ratio across both hops: 2^-20 (~9.5e-7), i.e. 20 extra
            //     bits of dynamic range chained on top of a single tree's own 23-level
            //     (2^23) internal range -- this is the actual mechanism the "~30-31
            //     effective levels" epic framing refers to (a rough order-of-magnitude
            //     estimate in the design doc, not a load-bearing exact figure; the
            //     precise, verifiable claim is the 2^-20 total ratio and the concrete
            //     per-tier meter figures above).
            mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_EARTH_DEMO: building Earth-scale "
                              "(childScale=2^-10/hop) three-tree chained tier-crossing scene");

            constexpr int   kN          = 16;
            constexpr int   kBrickDepth = 3;
            const glm::vec3 kCenter(8.0f, 8.0f, 8.0f);
            constexpr float kChildScale = 0.0009765625f;  // 2^-10, the real per-hop tier ratio

            auto bakeSphereTree = [&](float radius) {
                Vixen::SVO::RecipeParams rp{};
                rp.radius = radius;
                Vixen::SVO::SdfBakeResult baked =
                    Vixen::SVO::BakeRecipeToSdfWorld(Vixen::SVO::RECIPE_SPHERE, kCenter, rp, kN, 2.0f);
                return Vixen::SVO::BuildSdfBodyOctree(baked, kBrickDepth);
            };

            Vixen::SVO::SdfBodyOctree t0Body = bakeSphereTree(6.0f);
            Vixen::SVO::SdfBodyOctree t1Body = bakeSphereTree(6.5f);
            Vixen::SVO::SdfBodyOctree t2Body = bakeSphereTree(7.2f);

            Vixen::SVO::SerializedOctree t0Ser = Vixen::SVO::SerializeSdf(t0Body);
            Vixen::SVO::SerializedOctree t1Ser = Vixen::SVO::SerializeSdf(t1Body);
            Vixen::SVO::SerializedOctree t2Ser = Vixen::SVO::SerializeSdf(t2Body);

            // Per-tier solid color override (parent T0 keeps the shared cosine-gradient;
            // T1 solid green (region tier); T2 solid cyan (bedrock tier)) -- IDENTICAL
            // convention to VIXEN_TIER_CHAIN_DEMO above, so a capture's per-tier
            // attribution reads the same way at both ratios.
            auto overrideColor = [&](Vixen::SVO::SerializedOctree& ser, glm::vec3 rgb, const char* label) {
                const uint32_t colorBase = ser.channelBaseFloats(Vixen::SVO::SEM_COLOR);
                if (colorBase == 0xFFFFFFFFu) {
                    mainLogger->Error(std::string("[BuildRenderGraph] VIXEN_TIER_EARTH_DEMO: ") + label
                                      + " has no SEM_COLOR channel — color override skipped");
                    return;
                }
                float* pool = reinterpret_cast<float*>(ser.channelPool.data());
                const size_t poolFloats = ser.channelPool.size() / sizeof(float);
                for (uint32_t brick = 0; brick < ser.brickCount; ++brick) {
                    for (uint32_t comp = 0; comp < 3; ++comp) {
                        const float c = rgb[static_cast<int>(comp)];
                        for (uint32_t voxel = 0; voxel < Vixen::SVO::SerializedOctree::kVoxelsPerBrick; ++voxel) {
                            const size_t idx = static_cast<size_t>(brick) * ser.brickStrideFloats
                                             + colorBase + comp * Vixen::SVO::SerializedOctree::kVoxelsPerBrick + voxel;
                            if (idx < poolFloats) pool[idx] = c;
                        }
                    }
                }
            };
            overrideColor(t1Ser, glm::vec3(0.0f, 1.0f, 0.0f), "T1");  // solid green (region)
            overrideColor(t2Ser, glm::vec3(0.0f, 1.0f, 1.0f), "T2");  // solid cyan (bedrock)

            if (const Vixen::SVO::Octree* oct0 = t0Body.octree->getOctree()) Vixen::SVO::BakeAndAttachMipPool(*oct0, t0Ser);
            if (const Vixen::SVO::Octree* oct1 = t1Body.octree->getOctree()) Vixen::SVO::BakeAndAttachMipPool(*oct1, t1Ser);
            if (const Vixen::SVO::Octree* oct2 = t2Body.octree->getOctree()) Vixen::SVO::BakeAndAttachMipPool(*oct2, t2Ser);

            // Camera-facing leaf selection -- IDENTICAL convention to VIXEN_TIER_CHAIN_DEMO
            // (octant bit2/z set preferred, i.e. octants 4-7). For this fixture (n=16,
            // brickDepth=3, camera along -Z through the sphere's center) the selected leaf
            // is DETERMINISTICALLY octant 4 (x=0,y=0,z=1 bit pattern) -- verified via a
            // temporary discovery probe (test_tier_crossing_mirror_parity.cpp, removed
            // before that increment's commit) before this scene was written, not assumed.
            auto findCameraFacingLeaf = [](const Vixen::SVO::Octree* oct, uint32_t& outDescIdx, int& outOctant) {
                outOctant = -1;
                if (oct == nullptr) return;
                const auto& descs = oct->root->childDescriptors;
                for (uint32_t i = 0; i < descs.size() && outOctant < 0; ++i) {
                    const Vixen::SVO::ChildDescriptor& d = descs[i];
                    for (int o = 4; o < 8; ++o) {
                        if (d.hasChild(o) && d.isLeaf(o)) { outDescIdx = i; outOctant = o; break; }
                    }
                }
                if (outOctant < 0) {
                    for (uint32_t i = 0; i < descs.size() && outOctant < 0; ++i) {
                        const Vixen::SVO::ChildDescriptor& d = descs[i];
                        for (int o = 0; o < 8; ++o) {
                            if (d.hasChild(o) && d.isLeaf(o)) { outDescIdx = i; outOctant = o; break; }
                        }
                    }
                }
            };

            uint32_t t0MarkDescIdx = 0; int t0MarkOctant = -1;
            findCameraFacingLeaf(t0Body.octree->getOctree(), t0MarkDescIdx, t0MarkOctant);
            uint32_t t1MarkDescIdx = 0; int t1MarkOctant = -1;
            findCameraFacingLeaf(t1Body.octree->getOctree(), t1MarkDescIdx, t1MarkOctant);

            if (t0MarkOctant >= 0 && t1MarkOctant >= 0) {
                // tEntryWorld / k-invariant placement (Inc3 M4's own carry-forward
                // constraint, sharpened from M1/M3): at childScale=2^-10, 1/childScale
                // ~= 1024 per hop, so ANY macroscopically-off-boundary entry point would
                // be amplified by up to ~1024x (hop 1) or ~1,048,576x (hop 2) via the
                // cumulative-length multiply. This is handled by APPROACH (b): enforcing
                // that every hop's remapped entry lands well INSIDE the marked leaf's own
                // child grid, via the k-invariant childOriginLocal placement technique
                // (childOriginLocal = entryPointLocal - offset*childScale collapses the
                // remapped entry to a k-invariant 1.5+offset regardless of childScale;
                // see test_tier_crossing_mirror_parity.cpp's BuildTask3ParentWithScale /
                // ChainedTwoHopCrossingComposesHitT / EarthScaleChainedCrossingKInvariant-
                // Placement for the CPU-side proof of this exact technique at this exact
                // ratio, verified BEFORE this scene was written).
                //
                // CRITICAL, discovery-trail-verified correction (a first attempt at this
                // scene used a uniform offset and produced a chain that only crossed
                // ONCE, not twice -- caught via a temporary discovery probe, removed
                // before commit): the offset's sign per axis is NOT a universal constant
                // -- it must point INTO the SPECIFIC octant's own box being targeted.
                // Octant 4 (the camera-facing leaf this fixture always selects, bit
                // pattern x=0,y=0,z=1) occupies the ASYMMETRIC box x in [1,1.5),
                // y in [1,1.5), z in [1.5,2) relative to (1.5,1.5,1.5) -- so the offset
                // must be NEGATIVE on x/y (pull toward the box, which sits BELOW 1.5) and
                // POSITIVE on z (pull toward the box, which sits ABOVE 1.5). A uniform
                // (-0.1,-0.1,-0.1) landed the remapped entry at local z=1.4, OUTSIDE
                // octant 4's own z>=1.5 requirement -- confirmed the entry fell through to
                // a DIFFERENT part of the tree (or T1's own surface) rather than back into
                // the marked leaf, breaking the second crossing.
                //
                // SECOND, deeper correction (found via a live capture showing the crossing
                // wedge rendering as pure background/miss, not T1/T2's own color): magnitude
                // 0.1 lands the remapped entry at grid-space distance ~2.77 from T1/T2's OWN
                // local center (8,8,8 in their [0,16] grid) -- WELL INSIDE their solid sphere
                // interior (radius 6.5/7.2), nowhere near either body's own ISO-SURFACE. The
                // SDF march (handleLeafHitInstancedSdf/marchBrickSdf) searches for a
                // sign-change (surface) within its OWN local brick, not "any solid voxel" (the
                // CPU GpuTraversalMirror's own hit=true finding for this construction is a
                // BINARY-DDA-path artifact -- GpuTraversalMirror does NOT model the SDF march
                // at all, per its own header comment -- so it could not have caught this).
                // Magnitude 0.25 (verified via a hand derivation BEFORE this fix: a point along
                // octant 4's own (-1,-1,+1) diagonal at magnitude 0.25 sits at grid-space
                // distance ~6.93 from center, within ~0.43/0.27 grid units of T1's (r=6.5) and
                // T2's (r=7.2) own surfaces respectively -- comfortably inside marchBrickSdf's
                // own brick-local search range) keeps the SAME k-invariant safety property
                // (still comfortably inside [1,2) at every hop) while ALSO landing close enough
                // to each child's own real surface to be found.
                const glm::vec3 kBoxOffset(-0.25f, -0.25f, 0.25f);

                // Hop 0's crossing point is geometry-determined (wherever the camera ray
                // through the sphere's center actually enters octant 4's leaf) -- for this
                // fixture + camera it is (1.5,1.5,2.0), the leaf's own outer (+Z, camera-
                // facing) corner, confirmed via the same discovery probe referenced above
                // (a camera ray straight through a sphere's silhouette center necessarily
                // enters the nearest octant's OWN outer face -- geometric, not an
                // accident of this specific fixture).
                const glm::vec3 kHop0EntryPointLocal(1.5f, 1.5f, 2.0f);
                const glm::vec3 t0ChildOriginLocal = kHop0EntryPointLocal - kBoxOffset * kChildScale;

                Vixen::SVO::TierRef refT0ToT1{};
                refT0ToT1.childOctreeIndex = 1u;
                refT0ToT1.childOriginLocal[0] = t0ChildOriginLocal.x;
                refT0ToT1.childOriginLocal[1] = t0ChildOriginLocal.y;
                refT0ToT1.childOriginLocal[2] = t0ChildOriginLocal.z;
                refT0ToT1.childScale = kChildScale;
                Vixen::SVO::MarkLeafAsTierCrossing(t0Ser, t0MarkDescIdx, t0MarkOctant, refT0ToT1, 22);

                // Hop 1's crossing point: the k-invariant collapse (see kBoxOffset's own
                // comment) lands T1's OWN remapped entry at EXACTLY (1.5,1.5,1.5)+
                // kBoxOffset = (1.4,1.4,1.6), safely inside octant 4's own box on every
                // axis -- T1's marked leaf (also camera-facing octant 4, by the SAME
                // deterministic selection) is placed relative to THIS point using the
                // IDENTICAL technique, so hop 2's remapped entry lands inside T2's octant-4
                // box too.
                const glm::vec3 kHop1EntryPointLocal = glm::vec3(1.5f, 1.5f, 1.5f) + kBoxOffset;
                const glm::vec3 t1ChildOriginLocal = kHop1EntryPointLocal - kBoxOffset * kChildScale;

                Vixen::SVO::TierRef refT1ToT2{};
                refT1ToT2.childOctreeIndex = 2u;
                refT1ToT2.childOriginLocal[0] = t1ChildOriginLocal.x;
                refT1ToT2.childOriginLocal[1] = t1ChildOriginLocal.y;
                refT1ToT2.childOriginLocal[2] = t1ChildOriginLocal.z;
                refT1ToT2.childScale = kChildScale;
                Vixen::SVO::MarkLeafAsTierCrossing(t1Ser, t1MarkDescIdx, t1MarkOctant, refT1ToT2, 22);

                Vixen::SVO::ConcatenatedOctrees cat;
                cat.count = 3;
                cat.configs.resize(3);
                cat.nodeCounts.resize(3);
                cat.brickCounts.resize(3);
                cat.tierRefCounts.resize(3);

                Vixen::SVO::SerializedOctree* octs[3] = {&t0Ser, &t1Ser, &t2Ser};
                uint32_t nodeBase = 0, brickBase = 0, poolBase = 0, tierRefBase = 0, mipPoolBase = 0;
                for (int k = 0; k < 3; ++k) {
                    Vixen::SVO::SerializedOctree& s = *octs[k];
                    s.config.nodeArrayBase  = static_cast<int32_t>(nodeBase);
                    s.config.brickArrayBase = static_cast<int32_t>(brickBase);
                    Vixen::SVO::setSdfBrickArrayBase(s.config, poolBase);
                    Vixen::SVO::setTierRefTableBase(s.config, tierRefBase);
                    Vixen::SVO::setMipPoolBase(s.config, mipPoolBase);

                    cat.configs[k]       = s.config;
                    cat.nodeCounts[k]    = s.nodeCount;
                    cat.brickCounts[k]   = s.brickCount;
                    cat.tierRefCounts[k] = static_cast<uint32_t>(s.tierRefs.size());

                    cat.nodes.insert(cat.nodes.end(), s.nodes.begin(), s.nodes.end());
                    cat.bricks.insert(cat.bricks.end(), s.bricks.begin(), s.bricks.end());
                    cat.channelPool.insert(cat.channelPool.end(), s.channelPool.begin(), s.channelPool.end());
                    cat.brickGridLookup.insert(cat.brickGridLookup.end(), s.brickGridLookup.begin(), s.brickGridLookup.end());
                    cat.tierRefTable.insert(cat.tierRefTable.end(), s.tierRefs.begin(), s.tierRefs.end());
                    cat.mipPool.insert(cat.mipPool.end(), s.mipPool.begin(), s.mipPool.end());

                    if (cat.materials.empty()) {
                        cat.materials = s.materials;
                    }

                    nodeBase    += s.nodeCount;
                    brickBase   += s.brickCount;
                    poolBase    += s.brickCount * s.brickStrideFloats;
                    tierRefBase += static_cast<uint32_t>(s.tierRefs.size());
                    mipPoolBase += s.nodeCount * s.channelCount;
                }

                // Every octree resident from the start (Earth-scale demo's own residency
                // exercise is a SEPARATE, explicit mid-flight RequestBrickResidency(true)
                // scripted by VIXEN_TIER_EARTH_ZOOM_DEMO below, at ONE hop, matching Inc2
                // M5's "start non-resident, grant mid-flight" discipline) -- unlike
                // VIXEN_TIER_CROSSING_NONRESIDENT/VIXEN_TIER_ZOOM_DEMO's whole-node
                // start-false convention, this scene starts resident by default so a
                // bare VIXEN_TIER_EARTH_DEMO=1 run (no zoom script) shows real geometry
                // immediately; VIXEN_TIER_EARTH_ZOOM_DEMO explicitly forces non-resident
                // at start via its own RequestBrickResidency(false) call, mirroring the
                // Inc2 M5 pattern exactly (see the Update()/PreTick scripted-zoom block).
                if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                    bodyScene->SetRecipePool(std::move(cat));
                    if (envFlagEnabled("VIXEN_TIER_EARTH_ZOOM_DEMO")) {
                        bodyScene->RequestBrickResidency(false);
                        mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_EARTH_ZOOM_DEMO: "
                                          "RequestBrickResidency(false) -- all octrees mip-only at start");
                    }

                    constexpr float kRenderScale = 4.8f;
                    constexpr float kHalf = 5.0f * kRenderScale;
                    Vixen::SVO::BodyInstanceGpu inst{};
                    Vixen::SVO::SetInstanceTranslationScale(inst, glm::vec3(64.0f - kHalf, 64.0f - kHalf, 64.0f - kHalf), kRenderScale);
                    inst.material.color[0]     = 1.0f;
                    inst.material.color[1]     = 1.0f;
                    inst.material.color[2]     = 1.0f;
                    inst.material.octreeIndex  = 0u;    // parent (T0) tree
                    inst.material.providerKind = 0u;    // PROVIDER_STORED
                    inst.material.recipeId     = 0u;

                    bodyScene->SetInstances({inst});
                    mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_EARTH_DEMO: T0 leaf ("
                                  + std::to_string(t0MarkDescIdx) + "," + std::to_string(t0MarkOctant)
                                  + ") -> T1 octree1 (childScale=2^-10); T1 leaf (" + std::to_string(t1MarkDescIdx) + ","
                                  + std::to_string(t1MarkOctant) + ") -> T2 octree2 (childScale=2^-10)");
                }
            } else {
                mainLogger->Error("[BuildRenderGraph] VIXEN_TIER_EARTH_DEMO: no camera-facing leaf found in T0 or T1 — demo scene not built");
            }
        } else if (envFlagEnabled("VIXEN_TIER_OBSERVABLE_DEMO")) {
            // Tiered-ESVO Inc3 M7 Task 13: the SAME T0->T1->T2 chained construction as
            // VIXEN_TIER_CHAIN_DEMO/VIXEN_TIER_EARTH_DEMO above (per-tier color override,
            // camera-facing-octant selection, RootLeafOctantCenterLocal concentric
            // placement), but with body/tier proportions chosen so BOTH LOD-handoff
            // distances are reachable, outside the solid interior, AND inside the
            // camera's default 22.5-deg half-FOV cone -- unlike the Earth demo
            // (M6 Progress Log), whose 48-world-unit body + 2^-10-per-hop ratio locks
            // the two handoffs 1024x apart and forces at least one of them either past
            // the orbit ceiling (120) or deep inside the marked octant's ~62-125-deg
            // off-axis blind zone.
            //
            // Numeric derivation (hand-computed BEFORE this scene was built; verified
            // against the Earth demo's OWN two independently-reported handoff distances
            // -- 14.921wu and 0.0146wu at childScale=2^-10, renderScale=4.8 -- which both
            // match a single calibrated constant to 4+ significant figures, confirming
            // the formula below rather than assuming it):
            //   worldDistance_handoff = 20 * R * childScale * scale_exp2 / raySizeCoef
            // where R = renderScale (world diameter = 10*R), scale_exp2 = 0.25 (a root-
            // level leaf, this fixture's marked octant is always a direct child of the
            // root), and raySizeCoef = 2*tan((fovRad/height)*0.5) = 0.0015708 at this
            // app's 45-deg-FOV/500-px-tall default render target.
            //
            // The marked octant (always octant 4 in this fixture's camera-facing
            // selection, bit pattern x=0,y=0,z=1) sits at a FIXED world offset of
            // (-2.5R,-2.5R,+2.5R) from the body's own center -- a direct consequence of
            // RootLeafOctantCenterLocal's own 1.25/1.75-per-axis convention, independent
            // of R. The angle between the camera's forward axis (looking down -Z at the
            // body center) and this octant, as seen from a camera at orbit distance d,
            // is therefore a SCALE-INVARIANT function of (d/R) alone -- it crosses the
            // 22.5-deg half-FOV boundary at d ~= 10*R (bisected numerically). The body's
            // own solid surface radius is empirically ~5.625*R (measured on the Earth/
            // Chain demos' shared recipe proportions: 27wu/4.8 renderScale).
            //
            // Chosen: R=0.1 (body world diameter 1.0 unit), childScale=0.25/hop (a
            // deliberately gentle, non-2^-10 ratio per the M7 plan's own "may mean a
            // deliberately NON-Earth-diameter ratio for a first observable proof"
            // allowance):
            //   hop0 (T0->T1) = 20*0.1*0.25*0.25/0.0015708 ~= 79.58 world units
            //   hop1 (T1->T2) = hop0*0.25                  ~= 19.89 world units
            //   in-FOV threshold (10*R)                     =  1.00 world units
            //   solid surface radius (5.625*R)               =  0.5625 world units
            //   orbit ceiling (CameraNode::kOrbitDistanceMax) = 120.0 world units
            // Both hop0 and hop1 sit inside [1.0, 120.0] (comfortably clear of the
            // in-FOV floor AND the solid radius) -- and at these distances the octant's
            // own off-axis angle is under 1.1 deg (bisection-verified below), an order
            // of magnitude inside the 22.5-deg half-cone, not merely "reachable" but
            // solidly centered.
            mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_OBSERVABLE_DEMO: building "
                              "reconstructed three-tree chained tier-crossing scene "
                              "(R=0.1, childScale=0.25/hop -- both crossings observable "
                              "on the default view axis)");

            constexpr int   kN          = 16;
            constexpr int   kBrickDepth = 3;
            const glm::vec3 kCenter(8.0f, 8.0f, 8.0f);
            // VIXEN_TIER_OBSERVABLE_CHILDSCALE (optional): sweeps childScale live for the
            // concentric-magnification proof (M5's own methodology — vary ONLY childScale at a
            // FIXED camera distance, verify the crossing patch shrinks concentrically about a
            // stable center at the predicted 0.5*childScale-per-axis law). Default 0.25 matches
            // this scene's own hand-computed hop0/hop1 derivation above.
            float kObsChildScale = 0.25f;
            if (const char* obsScaleEnv = std::getenv("VIXEN_TIER_OBSERVABLE_CHILDSCALE")) {
                kObsChildScale = std::strtof(obsScaleEnv, nullptr);
                if (!(kObsChildScale > 0.0f)) kObsChildScale = 0.25f;
            }

            auto bakeSphereTreeObs = [&](float radius) {
                Vixen::SVO::RecipeParams rp{};
                rp.radius = radius;
                Vixen::SVO::SdfBakeResult baked =
                    Vixen::SVO::BakeRecipeToSdfWorld(Vixen::SVO::RECIPE_SPHERE, kCenter, rp, kN, 2.0f);
                return Vixen::SVO::BuildSdfBodyOctree(baked, kBrickDepth);
            };

            Vixen::SVO::SdfBodyOctree obsT0Body = bakeSphereTreeObs(6.0f);
            Vixen::SVO::SdfBodyOctree obsT1Body = bakeSphereTreeObs(6.5f);
            Vixen::SVO::SdfBodyOctree obsT2Body = bakeSphereTreeObs(7.2f);

            Vixen::SVO::SerializedOctree obsT0Ser = Vixen::SVO::SerializeSdf(obsT0Body);
            Vixen::SVO::SerializedOctree obsT1Ser = Vixen::SVO::SerializeSdf(obsT1Body);
            Vixen::SVO::SerializedOctree obsT2Ser = Vixen::SVO::SerializeSdf(obsT2Body);

            auto overrideColorObs = [&](Vixen::SVO::SerializedOctree& ser, glm::vec3 rgb, const char* label) {
                const uint32_t colorBase = ser.channelBaseFloats(Vixen::SVO::SEM_COLOR);
                if (colorBase == 0xFFFFFFFFu) {
                    mainLogger->Error(std::string("[BuildRenderGraph] VIXEN_TIER_OBSERVABLE_DEMO: ") + label
                                      + " has no SEM_COLOR channel — color override skipped");
                    return;
                }
                float* pool = reinterpret_cast<float*>(ser.channelPool.data());
                const size_t poolFloats = ser.channelPool.size() / sizeof(float);
                for (uint32_t brick = 0; brick < ser.brickCount; ++brick) {
                    for (uint32_t comp = 0; comp < 3; ++comp) {
                        const float c = rgb[static_cast<int>(comp)];
                        for (uint32_t voxel = 0; voxel < Vixen::SVO::SerializedOctree::kVoxelsPerBrick; ++voxel) {
                            const size_t idx = static_cast<size_t>(brick) * ser.brickStrideFloats
                                             + colorBase + comp * Vixen::SVO::SerializedOctree::kVoxelsPerBrick + voxel;
                            if (idx < poolFloats) pool[idx] = c;
                        }
                    }
                }
            };
            overrideColorObs(obsT1Ser, glm::vec3(0.0f, 1.0f, 0.0f), "T1");  // solid green (region)
            overrideColorObs(obsT2Ser, glm::vec3(0.0f, 1.0f, 1.0f), "T2");  // solid cyan (bedrock)

            if (const Vixen::SVO::Octree* oct0 = obsT0Body.octree->getOctree()) Vixen::SVO::BakeAndAttachMipPool(*oct0, obsT0Ser);
            if (const Vixen::SVO::Octree* oct1 = obsT1Body.octree->getOctree()) Vixen::SVO::BakeAndAttachMipPool(*oct1, obsT1Ser);
            if (const Vixen::SVO::Octree* oct2 = obsT2Body.octree->getOctree()) Vixen::SVO::BakeAndAttachMipPool(*oct2, obsT2Ser);

            auto findCameraFacingLeafObs = [](const Vixen::SVO::Octree* oct, uint32_t& outDescIdx, int& outOctant) {
                outOctant = -1;
                if (oct == nullptr) return;
                const auto& descs = oct->root->childDescriptors;
                for (uint32_t i = 0; i < descs.size() && outOctant < 0; ++i) {
                    const Vixen::SVO::ChildDescriptor& d = descs[i];
                    for (int o = 4; o < 8; ++o) {
                        if (d.hasChild(o) && d.isLeaf(o)) { outDescIdx = i; outOctant = o; break; }
                    }
                }
                if (outOctant < 0) {
                    for (uint32_t i = 0; i < descs.size() && outOctant < 0; ++i) {
                        const Vixen::SVO::ChildDescriptor& d = descs[i];
                        for (int o = 0; o < 8; ++o) {
                            if (d.hasChild(o) && d.isLeaf(o)) { outDescIdx = i; outOctant = o; break; }
                        }
                    }
                }
            };

            uint32_t obsT0MarkDescIdx = 0; int obsT0MarkOctant = -1;
            findCameraFacingLeafObs(obsT0Body.octree->getOctree(), obsT0MarkDescIdx, obsT0MarkOctant);
            uint32_t obsT1MarkDescIdx = 0; int obsT1MarkOctant = -1;
            findCameraFacingLeafObs(obsT1Body.octree->getOctree(), obsT1MarkDescIdx, obsT1MarkOctant);

            if (obsT0MarkOctant >= 0 && obsT1MarkOctant >= 0) {
                // M5's proven concentric fix: childOriginLocal = the marked leaf's OWN
                // cell center (RootLeafOctantCenterLocal), not the root cube's shared
                // corner (1.5,1.5,1.5). Applied identically at BOTH hops -- unlike the
                // Earth demo's entry-anchored k-invariant technique (needed there ONLY
                // because childScale=2^-10 would blow up a macroscopically-off-boundary
                // entry by ~1024x per hop), this demo's childScale=0.25 has no such
                // precision hazard, so the simpler, already-concentric-proven M5
                // technique applies directly with no adaptation.
                const glm::vec3 obsT0LeafCenterLocal = Vixen::SVO::RootLeafOctantCenterLocal(obsT0MarkOctant);
                Vixen::SVO::TierRef obsRefT0ToT1{};
                obsRefT0ToT1.childOctreeIndex = 1u;
                obsRefT0ToT1.childOriginLocal[0] = obsT0LeafCenterLocal.x;
                obsRefT0ToT1.childOriginLocal[1] = obsT0LeafCenterLocal.y;
                obsRefT0ToT1.childOriginLocal[2] = obsT0LeafCenterLocal.z;
                obsRefT0ToT1.childScale = kObsChildScale;
                Vixen::SVO::MarkLeafAsTierCrossing(obsT0Ser, obsT0MarkDescIdx, obsT0MarkOctant, obsRefT0ToT1, 22);

                const glm::vec3 obsT1LeafCenterLocal = Vixen::SVO::RootLeafOctantCenterLocal(obsT1MarkOctant);
                Vixen::SVO::TierRef obsRefT1ToT2{};
                obsRefT1ToT2.childOctreeIndex = 2u;
                obsRefT1ToT2.childOriginLocal[0] = obsT1LeafCenterLocal.x;
                obsRefT1ToT2.childOriginLocal[1] = obsT1LeafCenterLocal.y;
                obsRefT1ToT2.childOriginLocal[2] = obsT1LeafCenterLocal.z;
                obsRefT1ToT2.childScale = kObsChildScale;
                Vixen::SVO::MarkLeafAsTierCrossing(obsT1Ser, obsT1MarkDescIdx, obsT1MarkOctant, obsRefT1ToT2, 22);

                // E7-T2/E8-T1: optional 4th sibling body, NOT tier-linked (no TierRef into
                // it, so it cannot disturb the T0/T1/T2 chain above) -- a large, independently
                // instanced SDF body whose OWN intra-tree mip ladder gives the plateau fixture
                // levels a small R=0.1/kBrickDepth=3 tree structurally cannot (verified empirically:
                // a 720-frame plateau capture against T0/T1/T2 alone produced level=0 at every
                // hold including the 120wu far ceiling -- perf/e7-t2-multirung-fixture-design.md
                // Sec.2's own pre-authorized contingency). Gated separately from
                // VIXEN_TIER_OBSERVABLE_PLATEAU_DEMO so a plain VIXEN_TIER_OBSERVABLE_DEMO boot
                // (existing gates, existing goldens) is byte-for-byte unaffected.
                const bool obsAddFarSibling = std::getenv("VIXEN_TIER_OBSERVABLE_FAR_SIBLING") != nullptr;
                Vixen::SVO::SdfBodyOctree obsFarBody;
                Vixen::SVO::SerializedOctree obsFarSer;
                if (obsAddFarSibling) {
                    // Large radius + coarse voxel band (kBand=8.0) relative to kN=32 so the
                    // resulting octree is genuinely deep (unlike the tiny R=0.1 T0/T1/T2
                    // bodies) -- gives mipPolicyLevel's coarse->fine ladder walk room to
                    // resolve level>0 at distance. Centered far from (64,64,64) so it never
                    // occludes/interferes with the T0/T1/T2 cluster's own on-axis framing.
                    // kFarN sweep history (each value's MEASURED MipReadBytes levels, the
                    // pre-registered instrument, not the looser PolicyLevelHistogram):
                    //   32  -> {0,1}      128 -> {0,2,3}      256 -> {0,3,4}
                    //   384 (band=8)  -> {0} (regression)     384 (band=16) -> {0} (regression)
                    // NOT a monotonic lever: level count/identity shifts non-linearly with
                    // kFarN, and does not simply accumulate toward the full union. 256 is
                    // the best result found (3 distinct levels: {0,3,4}) after a 4-value
                    // sweep; reverted to it rather than continuing an unprincipled parameter
                    // search (orchestrator flag: an 8th signature-adjudication rebuild pass
                    // is judgment-call territory, not "check the arithmetic").
                    // E9-T1: kN/renderScale overridable for the predict-then-measure sweep
                    // (perf/e9-t1-level-targeting-report.md) -- unset envs reproduce the
                    // literal defaults below byte-for-byte (same std::getenv+strtof/strtol
                    // pattern as VIXEN_TIER_OBSERVABLE_DISTANCE, BuildRenderGraph.cpp:2619).
                    int   kFarN      = 256;
                    if (const char* farNEnv = std::getenv("VIXEN_TIER_OBSERVABLE_FAR_N")) {
                        long parsed = std::strtol(farNEnv, nullptr, 10);
                        if (parsed > 0) kFarN = static_cast<int>(parsed);
                    }
                    float kFarRadius = 12.0f;
                    if (const char* farRadiusEnv = std::getenv("VIXEN_TIER_OBSERVABLE_FAR_RADIUS")) {
                        float parsed = std::strtof(farRadiusEnv, nullptr);
                        if (parsed > 0.0f) kFarRadius = parsed;
                    }
                    constexpr float kFarBand   = 8.0f;
                    const glm::vec3 kFarCenter(16.0f, 16.0f, 16.0f);
                    Vixen::SVO::RecipeParams farRp{};
                    farRp.radius = kFarRadius;
                    Vixen::SVO::SdfBakeResult farBaked = Vixen::SVO::BakeRecipeToSdfWorld(
                        Vixen::SVO::RECIPE_SPHERE, kFarCenter, farRp, kFarN, kFarBand);
                    obsFarBody = Vixen::SVO::BuildSdfBodyOctree(farBaked, kBrickDepth);
                    obsFarSer = Vixen::SVO::SerializeSdf(obsFarBody);
                    if (const Vixen::SVO::Octree* octFar = obsFarBody.octree->getOctree()) {
                        Vixen::SVO::BakeAndAttachMipPool(*octFar, obsFarSer);
                    }
                    mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_OBSERVABLE_FAR_SIBLING: "
                                      "baked large non-tiered sibling body (radius=" + std::to_string(kFarRadius)
                                      + ", kN=" + std::to_string(kFarN) + ", band=" + std::to_string(kFarBand) + ")");
                }

                // E8-T1 second attempt: body 3 (above) only ever resolved to levels {3,4} at the
                // holds where its far-field gate engaged at all (hop1_19.89wu through far_120wu;
                // the two nearest holds never reach it -- it sits ~83wu from the T0/T1/T2
                // cluster). A SECOND, smaller/shallower/CLOSER sibling (smaller kN -> shallower
                // farFieldDescentDepth ceiling per SVOTypes.glsl, placed nearer the cluster so
                // the currently-uncovered near/mid holds can reach it) targets the missing
                // low-level band (1-2) directly rather than re-sweeping kFarN on the one body.
                const bool obsAddNearSibling = std::getenv("VIXEN_TIER_OBSERVABLE_NEAR_SIBLING") != nullptr;
                Vixen::SVO::SdfBodyOctree obsNearBody;
                Vixen::SVO::SerializedOctree obsNearSer;
                if (obsAddNearSibling) {
                    constexpr int   kNearN      = 48;   // bpa=6, ceilDepth=findMSB(6)=2 -- shallow on purpose
                    constexpr float kNearRadius = 5.0f;
                    constexpr float kNearBand   = 4.0f;
                    // Placed along the SAME camera axis as T0/T1/T2 but offset so it doesn't
                    // occlude the cluster, at a distance the near/mid holds (10-40wu) can reach.
                    const glm::vec3 kNearCenter(40.0f, 40.0f, 40.0f);
                    Vixen::SVO::RecipeParams nearRp{};
                    nearRp.radius = kNearRadius;
                    Vixen::SVO::SdfBakeResult nearBaked = Vixen::SVO::BakeRecipeToSdfWorld(
                        Vixen::SVO::RECIPE_SPHERE, kNearCenter, nearRp, kNearN, kNearBand);
                    obsNearBody = Vixen::SVO::BuildSdfBodyOctree(nearBaked, kBrickDepth);
                    obsNearSer = Vixen::SVO::SerializeSdf(obsNearBody);
                    if (const Vixen::SVO::Octree* octNear = obsNearBody.octree->getOctree()) {
                        Vixen::SVO::BakeAndAttachMipPool(*octNear, obsNearSer);
                    }
                    mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_OBSERVABLE_NEAR_SIBLING: "
                                      "baked small non-tiered sibling body (radius=" + std::to_string(kNearRadius)
                                      + ", kN=" + std::to_string(kNearN) + ", band=" + std::to_string(kNearBand) + ")");
                }
                // E10-T1: optional 5th sibling body, the "orbital structure" fixture
                // (perf/e10-t1-orbital-fixture-report.md) -- a coarse, recognizable voxel
                // structure (slab base + tower, hand-carved SDF via BakeSdfWorld's EvalFn,
                // NOT RECIPE_SPHERE) sized/placed per the E9-T1 recipe
                // (perf/e9-t1-level-targeting-report.md Sec.6/8.2) so its far-field samples
                // land on target level L=depth=3 at the far orbital hold (>=120wu). Gated
                // separately from FAR_SIBLING/NEAR_SIBLING so a plain VIXEN_TIER_OBSERVABLE_DEMO
                // boot (existing gates, existing goldens) stays byte-for-byte unaffected.
                const bool obsAddStructure = std::getenv("VIXEN_TIER_OBSERVABLE_STRUCTURE") != nullptr;
                Vixen::SVO::SdfBodyOctree obsStructBody;
                Vixen::SVO::SerializedOctree obsStructSer;
                if (obsAddStructure) {
                    // Recipe arithmetic (E9-T1 Sec.6/8.2, occFrac well above floor -> top
                    // level reaches depth itself):
                    //   kN=64, brickSide=8 -> bpa=ceil(64/8)=8, depth=findMSB(8)=3.
                    //   Structure solid volume (slab 20x20x6 + tower 8x8x24, grid cells) =
                    //   2400+1536=3936 -> occFrac=3936/64^3=0.01501 >> 0.0005 floor.
                    //   -> predicted top level L=depth=3, plus 1-2 rungs below (D-series
                    //   pattern): predicted set {1,2,3}.
                    constexpr int   kStructN         = 64;
                    constexpr float kStructBand      = 8.0f;
                    const glm::vec3 kStructCenter(32.0f, 32.0f, 32.0f);
                    // Slab base: 20x20x6 cells centered in X/Z, sitting on the grid floor
                    // in Y. Tower: 8x8x24 cells centered on the slab, rising well above
                    // it -- together a recognizable silhouette (a compact building cluster),
                    // not a sphere fill, so the far-field render reads as a STRUCTURE.
                    const glm::vec3 kSlabHalf(10.0f, 3.0f, 10.0f);
                    const glm::vec3 kSlabCenter = kStructCenter + glm::vec3(0.0f, -kStructN * 0.5f + 3.0f, 0.0f);
                    const glm::vec3 kTowerHalf(4.0f, 12.0f, 4.0f);
                    const glm::vec3 kTowerCenter = kSlabCenter + glm::vec3(0.0f, kSlabHalf.y + kTowerHalf.y, 0.0f);
                    auto boxSdf = [](const glm::vec3& p, const glm::vec3& c, const glm::vec3& half) {
                        const glm::vec3 q = glm::abs(p - c) - half;
                        const glm::vec3 qMax = glm::max(q, glm::vec3(0.0f));
                        return glm::length(qMax) + std::min(std::max(q.x, std::max(q.y, q.z)), 0.0f);
                    };
                    auto structEval = [&](const glm::vec3& p) {
                        return std::min(boxSdf(p, kSlabCenter, kSlabHalf), boxSdf(p, kTowerCenter, kTowerHalf));
                    };
                    // E14-T1: derived emission pattern for the orbital structure.  This is
                    // deliberately sparse: three vertical window bands on the tower's front
                    // and rear faces, plus a lit strip around the slab's top edge.  The field is
                    // authored into the existing per-voxel emission channel; no light source or
                    // alternate shading mechanism is introduced.
                    // E15-T1: the carving itself is now gated on
                    // VIXEN_TIER_OBSERVABLE_STRUCTURE_EMISSIVE (E14/E15 briefs both assumed this
                    // gate already existed; the staged E14 diff left the carving unconditional
                    // under VIXEN_TIER_OBSERVABLE_STRUCTURE alone -- so both "on" and "off"
                    // captures baked the identical emissive octree and produced a vacuous,
                    // byte-identical A/B). With the gate unset, every voxel's emission value is
                    // 0.0 -- same value BakeSdfWorld's own NoEmission default would produce --
                    // so the unset path stays exactly the pre-E14 structure bake.
                    const bool obsStructureEmissive =
                        std::getenv("VIXEN_TIER_OBSERVABLE_STRUCTURE_EMISSIVE") != nullptr;
                    auto structEmission = [&](const glm::vec3& p) -> float {
                        if (!obsStructureEmissive) return 0.0f;
                        const glm::vec3 towerLocal = glm::abs(p - kTowerCenter);
                        const glm::vec3 slabLocal = glm::abs(p - kSlabCenter);
                        // E15-T1: widened from Z-only (front/rear) to all 4 tower side faces --
                        // the ~120wu far orbital hold's camera angle at the capture frame this
                        // task's protocol uses (frame 700) views the tower's X-normal side face,
                        // which the E14 diff's Z-only band never touched, so no emissive voxel
                        // was ever in view regardless of intensity (verified: a 62.5x intensity
                        // bump, 8.0f->500.0f, produced a byte-identical capture -- the ray simply
                        // never hit an emissive voxel, not a dilution/quantization shortfall).
                        const bool towerWindowBand =
                            towerLocal.y <= kTowerHalf.y &&
                            ((towerLocal.x <= kTowerHalf.x && towerLocal.z >= kTowerHalf.z - 1.0f) ||
                             (towerLocal.z <= kTowerHalf.z && towerLocal.x >= kTowerHalf.x - 1.0f)) &&
                            (std::fmod(std::floor(towerLocal.y), 6.0f) < 2.0f);
                        const bool slabEdgeStrip =
                            slabLocal.y >= kSlabHalf.y - 1.0f &&
                            slabLocal.x <= kSlabHalf.x && slabLocal.z <= kSlabHalf.z &&
                            (slabLocal.x >= kSlabHalf.x - 1.0f || slabLocal.z >= kSlabHalf.z - 1.0f);
                        // E15-T1: bumped from the E14 diff's original 8.0f -- at 120wu far-field
                        // hold, SEM_EMISSION's mean-filtered mip propagation (FilterMipMean,
                        // MipSample.h) dilutes the sparse window-band pattern (~1/3 of the
                        // tower's occupied surface voxels per band cycle) across mostly-
                        // non-emissive neighbors at every mip level, and the result is then
                        // scaled by the structure instance's own dim material tint
                        // (obsStructInst.color below) before reaching the pixel -- 8.0f's
                        // diluted+scaled contribution measured byte-identical to emissive-off
                        // in an 8-bit capture (verified: both legs' hud_capture_700.png md5
                        // 2828123a4ebbd752e24f5a89345d6177). 500.0f is chosen to clear 8-bit
                        // quantization even after that dilution/scaling chain, additively
                        // (outColor += bestColor * emission, SpatialReuseShade.comp) ahead of
                        // whatever tonemap/clamp the output chain applies.
                        return (towerWindowBand || slabEdgeStrip) ? 500.0f : 0.0f;
                    };
                    Vixen::SVO::SdfBakeResult structBaked = Vixen::SVO::BakeSdfWorld(
                        structEval, kStructCenter, kStructN, kStructBand, kBrickDepth,
                        structEmission);
                    obsStructBody = Vixen::SVO::BuildSdfBodyOctree(structBaked, kBrickDepth);
                    obsStructSer = Vixen::SVO::SerializeSdf(obsStructBody);
                    if (const Vixen::SVO::Octree* octStruct = obsStructBody.octree->getOctree()) {
                        Vixen::SVO::BakeAndAttachMipPool(*octStruct, obsStructSer);
                    }
                    mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_OBSERVABLE_STRUCTURE: "
                                      "baked slab+tower structure body (kN=" + std::to_string(kStructN)
                                      + ", band=" + std::to_string(kStructBand) + ")");
                }

                // Build a DENSE octree list so each sibling's octreeIndex is always its real
                // position regardless of which subset of {far,near,structure} siblings is
                // enabled -- avoids an empty/default SerializedOctree ever being concatenated
                // as a real (zero-node) tree.
                std::vector<Vixen::SVO::SerializedOctree*> obsOctList = {&obsT0Ser, &obsT1Ser, &obsT2Ser};
                int obsFarOctreeIndex = -1, obsNearOctreeIndex = -1, obsStructOctreeIndex = -1;
                if (obsAddFarSibling)  { obsFarOctreeIndex  = static_cast<int>(obsOctList.size()); obsOctList.push_back(&obsFarSer); }
                if (obsAddNearSibling) { obsNearOctreeIndex = static_cast<int>(obsOctList.size()); obsOctList.push_back(&obsNearSer); }
                if (obsAddStructure)   { obsStructOctreeIndex = static_cast<int>(obsOctList.size()); obsOctList.push_back(&obsStructSer); }
                const int obsTreeCount = static_cast<int>(obsOctList.size());

                Vixen::SVO::ConcatenatedOctrees obsCat;
                obsCat.count = obsTreeCount;
                obsCat.configs.resize(obsTreeCount);
                obsCat.nodeCounts.resize(obsTreeCount);
                obsCat.brickCounts.resize(obsTreeCount);
                obsCat.tierRefCounts.resize(obsTreeCount);

                Vixen::SVO::SerializedOctree** obsOcts = obsOctList.data();
                uint32_t obsNodeBase = 0, obsBrickBase = 0, obsPoolBase = 0, obsTierRefBase = 0, obsMipPoolBase = 0;
                for (int k = 0; k < obsTreeCount; ++k) {
                    Vixen::SVO::SerializedOctree& s = *obsOcts[k];
                    s.config.nodeArrayBase  = static_cast<int32_t>(obsNodeBase);
                    s.config.brickArrayBase = static_cast<int32_t>(obsBrickBase);
                    Vixen::SVO::setSdfBrickArrayBase(s.config, obsPoolBase);
                    Vixen::SVO::setTierRefTableBase(s.config, obsTierRefBase);
                    Vixen::SVO::setMipPoolBase(s.config, obsMipPoolBase);

                    obsCat.configs[k]       = s.config;
                    obsCat.nodeCounts[k]    = s.nodeCount;
                    obsCat.brickCounts[k]   = s.brickCount;
                    obsCat.tierRefCounts[k] = static_cast<uint32_t>(s.tierRefs.size());

                    obsCat.nodes.insert(obsCat.nodes.end(), s.nodes.begin(), s.nodes.end());
                    obsCat.bricks.insert(obsCat.bricks.end(), s.bricks.begin(), s.bricks.end());
                    obsCat.channelPool.insert(obsCat.channelPool.end(), s.channelPool.begin(), s.channelPool.end());
                    obsCat.brickGridLookup.insert(obsCat.brickGridLookup.end(), s.brickGridLookup.begin(), s.brickGridLookup.end());
                    obsCat.tierRefTable.insert(obsCat.tierRefTable.end(), s.tierRefs.begin(), s.tierRefs.end());
                    obsCat.mipPool.insert(obsCat.mipPool.end(), s.mipPool.begin(), s.mipPool.end());

                    if (obsCat.materials.empty()) {
                        obsCat.materials = s.materials;
                    }

                    obsNodeBase    += s.nodeCount;
                    obsBrickBase   += s.brickCount;
                    obsPoolBase    += s.brickCount * s.brickStrideFloats;
                    obsTierRefBase += static_cast<uint32_t>(s.tierRefs.size());
                    obsMipPoolBase += s.nodeCount * s.channelCount;
                }

                if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                    bodyScene->SetRecipePool(std::move(obsCat));

                    // renderScale=0.1 -> world diameter = 10*0.1 = 1.0 unit, centered on
                    // (64,64,64) (the same world center every tier-crossing demo in this
                    // file uses, so the existing VIXEN_TIER_ZOOM_DEMO-style orbitCenter
                    // wiring below still applies unmodified).
                    constexpr float kObsRenderScale = 0.1f;
                    constexpr float kObsHalf = 5.0f * kObsRenderScale;  // = 0.5f
                    Vixen::SVO::BodyInstanceGpu obsInst{};
                    Vixen::SVO::SetInstanceTranslationScale(
                        obsInst, glm::vec3(64.0f - kObsHalf), kObsRenderScale);
                    obsInst.material.color[0] = 1.0f;
                    obsInst.material.color[1] = 1.0f;
                    obsInst.material.color[2] = 1.0f;
                    obsInst.material.octreeIndex = 0u;

                    std::vector<Vixen::SVO::BodyInstanceGpu> obsInstances{obsInst};
                    if (obsAddFarSibling) {
                        // Same orbit center (64,64,64) as the T0/T1/T2 cluster, so the
                        // existing camera sweep samples it too, but a much larger
                        // renderScale -- the far-ceiling hold (120wu) sees a genuinely
                        // production-shaped body instead of the 1.0wu T0/T1/T2 cluster's
                        // own already-tiny world footprint.
                        float kFarRenderScale = 4.8f;
                        if (const char* farRsEnv = std::getenv("VIXEN_TIER_OBSERVABLE_FAR_RENDER_SCALE")) {
                            float parsed = std::strtof(farRsEnv, nullptr);
                            if (parsed > 0.0f) kFarRenderScale = parsed;
                        }
                        const float kFarHalf = 5.0f * kFarRenderScale;
                        Vixen::SVO::BodyInstanceGpu obsFarInst{};
                        Vixen::SVO::SetInstanceTranslationScale(
                            obsFarInst, glm::vec3(64.0f - kFarHalf), kFarRenderScale);
                        obsFarInst.material.color[0] = 1.0f;
                        obsFarInst.material.color[1] = 0.5f;
                        obsFarInst.material.color[2] = 0.0f;
                        obsFarInst.material.octreeIndex = static_cast<uint32_t>(obsFarOctreeIndex);
                        obsInstances.push_back(obsFarInst);
                    }
                    if (obsAddNearSibling) {
                        // Closer than the far sibling, on the SAME orbit-center axis, so the
                        // currently-uncovered near/mid holds (below_hop1_10wu, hop1_19.89wu,
                        // mid_40wu) can reach its far-field gate.
                        constexpr float kNearInstRenderScale = 2.0f;
                        constexpr float kNearInstHalf = 5.0f * kNearInstRenderScale;
                        Vixen::SVO::BodyInstanceGpu obsNearInst{};
                        Vixen::SVO::SetInstanceTranslationScale(
                            obsNearInst, glm::vec3(64.0f - kNearInstHalf), kNearInstRenderScale);
                        obsNearInst.material.color[0] = 0.5f;
                        obsNearInst.material.color[1] = 0.0f;
                        obsNearInst.material.color[2] = 1.0f;
                        obsNearInst.material.octreeIndex = static_cast<uint32_t>(obsNearOctreeIndex);
                        obsInstances.push_back(obsNearInst);
                    }
                    if (obsAddStructure) {
                        // E10-T1: placed at the SAME orbit center (64,64,64) as every other
                        // sibling. Per ShellOctreeGpu.h:281 (world = worldPos + (local *
                        // kWorldGridSize) * renderScale), the octree's own local space is a
                        // FIXED [0,kWorldGridSize=10) unit cube regardless of kN -- so the
                        // half-extent is always 5.0*renderScale (matching every other sibling
                        // body here), NOT a function of kStructN. renderScale chosen so
                        // brickWorldSize*2^L (L=3, the predicted top level) is comfortably
                        // inside the far hold's footprint (>=120wu) -- same renderScale as the
                        // far sibling (4.8), reusing the orbital-scale distance already proven
                        // to reach level>=2 in E8-T1/E9-T1.
                        constexpr float kStructRenderScale = 4.8f;
                        constexpr float kStructHalf = 5.0f * kStructRenderScale;
                        Vixen::SVO::BodyInstanceGpu obsStructInst{};
                        Vixen::SVO::SetInstanceTranslationScale(
                            obsStructInst, glm::vec3(64.0f - kStructHalf), kStructRenderScale);
                        obsStructInst.material.color[0] = 0.0f;
                        obsStructInst.material.color[1] = 1.0f;
                        obsStructInst.material.color[2] = 1.0f;
                        obsStructInst.material.octreeIndex = static_cast<uint32_t>(obsStructOctreeIndex);
                        obsInstances.push_back(obsStructInst);
                    }
                    bodyScene->SetInstances(std::move(obsInstances));
                    mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_OBSERVABLE_DEMO: T0 leaf ("
                                  + std::to_string(obsT0MarkDescIdx) + "," + std::to_string(obsT0MarkOctant)
                                  + ") -> T1 octree1 (childScale=0.25); T1 leaf (" + std::to_string(obsT1MarkDescIdx) + ","
                                  + std::to_string(obsT1MarkOctant) + ") -> T2 octree2 (childScale=0.25); "
                                  "predicted hop0~=79.58wu, hop1~=19.89wu, both <1.1deg off-axis");
                }
            } else {
                mainLogger->Error("[BuildRenderGraph] VIXEN_TIER_OBSERVABLE_DEMO: no camera-facing leaf found in T0 or T1 — demo scene not built");
            }
        } else if (envFlagEnabled("VIXEN_RESTIR_GATE_DEMO")) {
            // Sampled Lighting Inc3 M4 live gate: the equal-error-vs-brute-force check the
            // plan's Task 4 requires. Bakes THE SAME >=10^3-emissive-voxel gate scene
            // test_light_tree.cpp's BakeEmissiveGateScene uses (n=32, r=10, center(16,16,16),
            // spatially-varying emissive intensity over the whole occupied volume), computes
            // the CPU light-tree cut + brute-force reference from it, pushes the cut to the
            // GPU via LightTreeBufferNode::SetLightTreeCut (transformed grid->world -- see
            // LightTreeBufferNode.h's own scope note on this), and bakes the SAME body into
            // BodyOctreeSceneNode for the live render. VulkanGraphApplication::Update's own
            // VIXEN_RESTIR_GATE_DEMO tick hook (below) reads back reservoirRecordsA/B after
            // enough frames for temporal convergence and logs the numeric comparison.
            mainLogger->Info("[BuildRenderGraph] VIXEN_RESTIR_GATE_DEMO: building the M3 emissive gate scene "
                              "for the ReSTIR equal-error-vs-brute-force live gate");

            constexpr int   kN      = 32;
            constexpr float kR      = 10.0f;
            constexpr float kBand   = 2.0f;
            const glm::vec3 kCenter(16.0f, 16.0f, 16.0f);

            Vixen::SVO::RecipeParams rp{kR, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
            Vixen::SVO::SdfBakeResult baked = Vixen::SVO::BakeRecipeToSdfWorldWithEmission(
                Vixen::SVO::RECIPE_SPHERE, kCenter, rp, kN, kBand,
                [](const glm::vec3& p) { return 1.0f + 0.1f * (p.x + p.y + p.z); });
            Vixen::SVO::SdfBodyOctree body = Vixen::SVO::BuildSdfBodyOctree(baked, 3);

            Vixen::SVO::SerializedOctree ser = Vixen::SVO::SerializeSdf(body);
            const Vixen::SVO::Octree* oct = body.octree->getOctree();

            if (oct != nullptr) {
                Vixen::SVO::BakeAndAttachMipPool(*oct, ser);
                Vixen::SVO::MipPool mipPool = Vixen::SVO::BakeMipPool(*oct, ser);

                // A FINE cut (test_light_tree.cpp's own CutAggregatePowerApproximatesBruteForceLeafSum
                // tolerance test uses powerThreshold=0.001 for exactly this reason): the cut decision
                // compares against MipSample::value (a per-voxel MEAN intensity, ~1-4 for this scene's
                // emit function), NOT an aggregate/summed power -- a coarse threshold like 50.0 (the
                // OTHER test_light_tree.cpp test, "bounded non-empty cut", which only asserts the cut
                // is non-empty/smaller than the raw voxel count, not that it approximates the true
                // total) prunes at the ROOT immediately (root mean-intensity < 50), collapsing the
                // whole scene into ONE grossly-under-representative node. This gate needs the cut's
                // AGGREGATE power to actually approximate BruteForceTotalEmissivePower (per
                // LightTreeCutTotalPower's own contract), so it must use the FINE threshold.
                Vixen::SVO::LightTreeCutParams cutParams;
                cutParams.powerThreshold = 0.001f;
                std::vector<Vixen::SVO::LightTreeNode> cut =
                    Vixen::SVO::BuildLightTreeCut(*oct, ser, mipPool, kN, cutParams);

                // Instance transform: SAME single-body placement pattern VIXEN_TIER_CROSSING_DEMO
                // uses (renderScale=4.8, world diameter 48, world span = renderScale*[0,10] per
                // SerializeSdf's kWorldGridSize -- NOT n=32; gridMin/gridMax are never read by
                // this shader). Centered at the default camera's frame center (64,64,64).
                constexpr float kRenderScale = 4.8f;
                constexpr float kWorldGridSize = 10.0f;  // SerializeSdf's fixed config-local-world span
                constexpr float kHalf = 5.0f * kRenderScale;
                const glm::vec3 instWorldPos(64.0f - kHalf, 64.0f - kHalf, 64.0f - kHalf);

                // LightTreeBufferNode.h's scope note: worldPos in the cut is grid space [0,n) --
                // transform to world space using the SAME p_world = p_base*renderScale + worldPos
                // TraceWorld.glsl uses, where p_base = p_grid / n * kWorldGridSize (the shader's
                // ACTUAL grid->config-local-world map, independent of the bake's own n).
                std::vector<Vixen::SVO::LightTreeNode> worldCut;
                worldCut.reserve(cut.size());
                for (const auto& node : cut) {
                    Vixen::SVO::LightTreeNode w = node;
                    const glm::vec3 pBase = (node.worldPos / static_cast<float>(kN)) * kWorldGridSize;
                    w.worldPos = pBase * kRenderScale + instWorldPos;
                    w.worldExtent = (node.worldExtent / static_cast<float>(kN)) * kWorldGridSize * kRenderScale;
                    worldCut.push_back(w);
                }

                if (auto* lightTreeInst = static_cast<LightTreeBufferNode*>(renderGraph->GetInstance(lightTreeBufferNode))) {
                    lightTreeInst->SetLightTreeCut(worldCut);
                }

                // This gate's GPU-side RIS estimator computes Sum_i(power_i/dist_i^2) -- the SAME
                // rendering-equation-shaped quantity DirectLighting.comp's lightTreeNodePHat
                // evaluates (power = intensity*coverage*extent^3, matching LightTree.h's
                // LightTreeCutTotalPower per-node power definition) -- NOT BruteForceTotalEmissive
                // Power (a raw sum-of-intensity with no distance falloff; a different estimator
                // target, the M3 cut-approximation check's own quantity). The brute-force reference
                // is therefore computed PER-PIXEL (see VulkanGraphApplication.cpp's readback tick
                // hook), evaluated at each pixel's OWN HitRecord.worldPos over this SAME world-
                // transformed cut -- an EARLIER version of this gate instead evaluated a single
                // hand-picked "canonical" shading point, which a live-gate DIAG dump proved wrong
                // (recomputed pHat vs the shader's own targetPdf, for the SAME chosen node, varied
                // 28x-71x across different pixels/nodes -- the signature of a geometric mismatch,
                // not a uniform scale bug). Per-pixel evaluation removes that assumption entirely.
                mainLogger->Info("[BuildRenderGraph] VIXEN_RESTIR_GATE_DEMO: cut=" + std::to_string(cut.size()) + " nodes");

                // Stash the world-transformed cut where VulkanGraphApplication::Update's tick hook
                // can read it for its own per-pixel brute-force recomputation (a plain static --
                // this demo scene is process-lifetime-scoped, same as every other VIXEN_*_DEMO
                // env-gated block in this file).
                extern std::vector<Vixen::SVO::LightTreeNode>* g_restirGateWorldCut;
                static std::vector<Vixen::SVO::LightTreeNode> worldCutStash = worldCut;
                g_restirGateWorldCut = &worldCutStash;
            } else {
                mainLogger->Error("[BuildRenderGraph] VIXEN_RESTIR_GATE_DEMO: body octree is null -- gate scene not built");
            }

            std::vector<const Vixen::SVO::SdfBodyOctree*> octreesForCat = {&body};
            Vixen::SVO::ConcatenatedOctrees cat = Vixen::SVO::ConcatenateSdfWithMips(octreesForCat);

            if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                bodyScene->SetRecipePool(std::move(cat));

                constexpr float kRenderScale = 4.8f;
                constexpr float kHalf = 5.0f * kRenderScale;
                Vixen::SVO::BodyInstanceGpu inst{};
                Vixen::SVO::SetInstanceTranslationScale(inst, glm::vec3(64.0f - kHalf, 64.0f - kHalf, 64.0f - kHalf), kRenderScale);
                inst.material.color[0]     = 1.0f;
                inst.material.color[1]     = 1.0f;
                inst.material.color[2]     = 1.0f;
                inst.material.octreeIndex  = 0u;
                inst.material.providerKind = 0u;  // PROVIDER_STORED
                inst.material.recipeId     = 0u;
                bodyScene->SetInstances({inst});
                mainLogger->Info("[BuildRenderGraph] VIXEN_RESTIR_GATE_DEMO: seeded 1 Stored-SDF body instance");
            }
        } else if (envFlagEnabled("VIXEN_TIER_M8_EARTH_DEMO")) {
            // Tiered-ESVO Inc3 M8 Task 17: the TRUE Earth-scale (childScale=2^-10 at BOTH
            // hops) observable surface-to-orbit demo -- the epic's literal original ask,
            // finally attempted with a genuinely controllable camera (Task 16's look-target
            // decoupling). Originally reused the M4 VIXEN_TIER_EARTH_DEMO block's k-invariant
            // entry-anchored childOriginLocal placement; M8 Task 23 replaced that placement
            // (below, this block only -- VIXEN_TIER_EARTH_DEMO above is UNCHANGED/out of
            // scope) with RootLeafOctantCenterLocal after a live-gate investigation found the
            // entry-anchored point sat on the leaf's own boundary plane, making the child
            // unreachable off-axis -- see this block's own construction comment for the full
            // derivation. Same renderScale=4.8 (48-world-unit body diameter). ONE deliberate
            // difference from M4/M6's attempt (historical, superseded by Task 23 below): the
            // LOD ray-cone coefficient is overridden (VIXEN_TIER_M8_EARTH_LOD_COEF_OVERRIDE,
            // wired below alongside VIXEN_TIER_CROSSING_LOD_COEF_OVERRIDE's existing
            // ConstantNode-bypass mechanism) so hop0 (T0->T1) lands OUTSIDE the body's own
            // ~27-world-unit solid surface radius with real margin, not (as M4/M6's default-
            // raySizeCoef attempt found) buried ~1.8x INSIDE it.
            //
            // THE FINDING THIS FIXES (verified analytically before writing this scene, python
            // trace against M4/M6's own reported numbers): the calibrated LOD-gate formula
            // (M7 Task 13, validator re-derived from the actual shader gate)
            //   hop0 = 20*R*childScale*scale_exp2/raySizeCoef,  solidRadius = 5.625*R
            // gives a ratio hop0/solidRadius = 20*childScale*scale_exp2/(raySizeCoef*5.625)
            // that is INDEPENDENT OF R (R cancels) -- at childScale=2^-10, scale_exp2=0.25
            // (a root-level leaf, this fixture's marked octant is always a direct child of
            // root) and the DEFAULT raySizeCoef=0.0015708 (45deg FOV / 500px height), this
            // ratio is a fixed ~0.524 < 1: hop0 is ALWAYS inside the solid at the default
            // coefficient, for ANY renderScale. This is a stronger, previously-uncharacterized
            // fact than the M6/M7 off-axis-angle finding: M4's original Earth demo (R=4.8)
            // already had hop0=14.92wu strictly inside its own solid radius (~27wu) -- the
            // "camera dives inside the solid, noisy render" symptom M4 attributed only to its
            // near-end zoom schedule (kNearDist=1e-5) was ALSO, independently, a structural
            // property of hop0 itself at the default coefficient.
            // FIX (construction/render-parameter only, no shader/traversal change):
            // raySizeCoef is already a demo-overridable literal via the existing
            // VIXEN_TIER_CROSSING_LOD_COEF_OVERRIDE ConstantNode-bypass mechanism (see that
            // constant's own declaration comment above for why a direct literal, not an FOV
            // bump, is the correct/only-effective lever -- FOV only grows raySizeCoef
            // linearly and cannot cross the needed order of magnitude). Solving
            // hop0 = 3*solidRadius (a 3x safety margin, clear of the solid with room for a
            // real near-orbit approach) for raySizeCoef at R=4.8:
            //   raySizeCoef_override = 20*R*childScale*scale_exp2/(3*5.625*R)
            //                        = 20*childScale*scale_exp2/(3*5.625)  (R cancels again)
            //                        ~= 2.8935e-4
            // giving hop0 = 3*27.0 = 81.0 world units (comfortably < the 120wu orbit ceiling)
            // and hop1 = hop0*childScale = 81.0 * 2^-10 ~= 0.0791016 world units.
            // M8 Task 23 supersedes the override plan above: the corrected (camera-anchored,
            // world-unit-correct) crossing gate makes the crossing a genuine distance-driven
            // handoff at the REAL unoverridden raySizeCoef — no coefficient override is
            // needed or wanted for this demo any more (the historical derivation above is
            // kept for the record; VIXEN_TIER_CROSSING_LOD_COEF_OVERRIDE remains a generic
            // debugging/ablation knob only).
            mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_M8_EARTH_DEMO: building TRUE "
                              "Earth-scale (childScale=2^-10/hop) three-tree chained "
                              "tier-crossing scene -- M8 Task 23 gate: crossing fires by "
                              "camera-to-child distance at the REAL raySizeCoef (no override)");

            constexpr int   kN          = 16;
            constexpr int   kBrickDepth = 3;
            const glm::vec3 kCenter(8.0f, 8.0f, 8.0f);
            constexpr float kChildScale = 0.0009765625f;  // 2^-10, the real per-hop tier ratio

            auto bakeSphereTreeM8 = [&](float radius) {
                Vixen::SVO::RecipeParams rp{};
                rp.radius = radius;
                Vixen::SVO::SdfBakeResult baked =
                    Vixen::SVO::BakeRecipeToSdfWorld(Vixen::SVO::RECIPE_SPHERE, kCenter, rp, kN, 2.0f);
                return Vixen::SVO::BuildSdfBodyOctree(baked, kBrickDepth);
            };

            Vixen::SVO::SdfBodyOctree m8T0Body = bakeSphereTreeM8(6.0f);
            Vixen::SVO::SdfBodyOctree m8T1Body = bakeSphereTreeM8(6.5f);
            Vixen::SVO::SdfBodyOctree m8T2Body = bakeSphereTreeM8(7.2f);

            Vixen::SVO::SerializedOctree m8T0Ser = Vixen::SVO::SerializeSdf(m8T0Body);
            Vixen::SVO::SerializedOctree m8T1Ser = Vixen::SVO::SerializeSdf(m8T1Body);
            Vixen::SVO::SerializedOctree m8T2Ser = Vixen::SVO::SerializeSdf(m8T2Body);

            auto overrideColorM8 = [&](Vixen::SVO::SerializedOctree& ser, glm::vec3 rgb, const char* label) {
                const uint32_t colorBase = ser.channelBaseFloats(Vixen::SVO::SEM_COLOR);
                if (colorBase == 0xFFFFFFFFu) {
                    mainLogger->Error(std::string("[BuildRenderGraph] VIXEN_TIER_M8_EARTH_DEMO: ") + label
                                      + " has no SEM_COLOR channel — color override skipped");
                    return;
                }
                float* pool = reinterpret_cast<float*>(ser.channelPool.data());
                const size_t poolFloats = ser.channelPool.size() / sizeof(float);
                for (uint32_t brick = 0; brick < ser.brickCount; ++brick) {
                    for (uint32_t comp = 0; comp < 3; ++comp) {
                        const float c = rgb[static_cast<int>(comp)];
                        for (uint32_t voxel = 0; voxel < Vixen::SVO::SerializedOctree::kVoxelsPerBrick; ++voxel) {
                            const size_t idx = static_cast<size_t>(brick) * ser.brickStrideFloats
                                             + colorBase + comp * Vixen::SVO::SerializedOctree::kVoxelsPerBrick + voxel;
                            if (idx < poolFloats) pool[idx] = c;
                        }
                    }
                }
            };
            // Per-tier color: T0 default cosine-gradient (purple/magenta-adjacent), T1
            // solid green (region tier), T2 solid cyan (bedrock tier) -- IDENTICAL
            // convention to VIXEN_TIER_CHAIN_DEMO/VIXEN_TIER_EARTH_DEMO/VIXEN_TIER_OBSERVABLE_DEMO.
            overrideColorM8(m8T1Ser, glm::vec3(0.0f, 1.0f, 0.0f), "T1");
            overrideColorM8(m8T2Ser, glm::vec3(0.0f, 1.0f, 1.0f), "T2");

            if (const Vixen::SVO::Octree* oct0 = m8T0Body.octree->getOctree()) Vixen::SVO::BakeAndAttachMipPool(*oct0, m8T0Ser);
            if (const Vixen::SVO::Octree* oct1 = m8T1Body.octree->getOctree()) Vixen::SVO::BakeAndAttachMipPool(*oct1, m8T1Ser);
            if (const Vixen::SVO::Octree* oct2 = m8T2Body.octree->getOctree()) Vixen::SVO::BakeAndAttachMipPool(*oct2, m8T2Ser);

            auto findCameraFacingLeafM8 = [](const Vixen::SVO::Octree* oct, uint32_t& outDescIdx, int& outOctant) {
                outOctant = -1;
                if (oct == nullptr) return;
                const auto& descs = oct->root->childDescriptors;
                for (uint32_t i = 0; i < descs.size() && outOctant < 0; ++i) {
                    const Vixen::SVO::ChildDescriptor& d = descs[i];
                    for (int o = 4; o < 8; ++o) {
                        if (d.hasChild(o) && d.isLeaf(o)) { outDescIdx = i; outOctant = o; break; }
                    }
                }
                if (outOctant < 0) {
                    for (uint32_t i = 0; i < descs.size() && outOctant < 0; ++i) {
                        const Vixen::SVO::ChildDescriptor& d = descs[i];
                        for (int o = 0; o < 8; ++o) {
                            if (d.hasChild(o) && d.isLeaf(o)) { outDescIdx = i; outOctant = o; break; }
                        }
                    }
                }
            };

            uint32_t m8T0MarkDescIdx = 0; int m8T0MarkOctant = -1;
            findCameraFacingLeafM8(m8T0Body.octree->getOctree(), m8T0MarkDescIdx, m8T0MarkOctant);
            uint32_t m8T1MarkDescIdx = 0; int m8T1MarkOctant = -1;
            findCameraFacingLeafM8(m8T1Body.octree->getOctree(), m8T1MarkDescIdx, m8T1MarkOctant);

            if (m8T0MarkOctant >= 0 && m8T1MarkOctant >= 0) {
                // M8 Task 23 CORRECTION: the previous entry-anchored placement
                // (childOriginLocal = (1.5,1.5,2.0) - kBoxOffset*childScale) put the anchor
                // ON octant 4's own x=1.5/y=1.5 boundary plane (margin 0.000244 at cs=2^-10 --
                // sub-ULP-adjacent), not safely INSIDE its box. A live-gate investigation (two
                // independent traces, this session) found EVERY ray except a sub-pixel bullseye
                // around that corner missed the remapped child's [0,1]^3 grid entirely --
                // explaining the flight capture's flat, unchanging mip-gray quadrant (the
                // child-miss fallback correctly serving T0's own mip, because T1 was
                // structurally unreachable, not because of any Task 23 gate/fallback bug).
                // FIX: use RootLeafOctantCenterLocal (M5's proven-safe fixed point, margin 0.25
                // on every axis -- the SAME technique VIXEN_TIER_CROSSING_DEMO/CHAIN_DEMO/
                // OBSERVABLE_DEMO already use successfully) instead of a corner-adjacent entry
                // point. This does NOT reopen the tEntryWorld/off-boundary constraint (M1/M3
                // carry-forward): a CENTERED anchor is MORE robust to off-boundary entry than a
                // corner-anchored one, since the safe margin is 0.25 local units either way,
                // independent of childScale (the amplification only affects how far a given
                // WORLD deviation reaches in child-local units, not the parent-local anchor
                // point itself).
                const glm::vec3 m8T0ChildOriginLocal = Vixen::SVO::RootLeafOctantCenterLocal(m8T0MarkOctant);

                Vixen::SVO::TierRef m8RefT0ToT1{};
                m8RefT0ToT1.childOctreeIndex = 1u;
                m8RefT0ToT1.childOriginLocal[0] = m8T0ChildOriginLocal.x;
                m8RefT0ToT1.childOriginLocal[1] = m8T0ChildOriginLocal.y;
                m8RefT0ToT1.childOriginLocal[2] = m8T0ChildOriginLocal.z;
                m8RefT0ToT1.childScale = kChildScale;
                Vixen::SVO::MarkLeafAsTierCrossing(m8T0Ser, m8T0MarkDescIdx, m8T0MarkOctant, m8RefT0ToT1, 22);

                const glm::vec3 m8T1ChildOriginLocal = Vixen::SVO::RootLeafOctantCenterLocal(m8T1MarkOctant);

                Vixen::SVO::TierRef m8RefT1ToT2{};
                m8RefT1ToT2.childOctreeIndex = 2u;
                m8RefT1ToT2.childOriginLocal[0] = m8T1ChildOriginLocal.x;
                m8RefT1ToT2.childOriginLocal[1] = m8T1ChildOriginLocal.y;
                m8RefT1ToT2.childOriginLocal[2] = m8T1ChildOriginLocal.z;
                m8RefT1ToT2.childScale = kChildScale;
                Vixen::SVO::MarkLeafAsTierCrossing(m8T1Ser, m8T1MarkDescIdx, m8T1MarkOctant, m8RefT1ToT2, 22);

                Vixen::SVO::ConcatenatedOctrees m8Cat;
                m8Cat.count = 3;
                m8Cat.configs.resize(3);
                m8Cat.nodeCounts.resize(3);
                m8Cat.brickCounts.resize(3);
                m8Cat.tierRefCounts.resize(3);

                Vixen::SVO::SerializedOctree* m8Octs[3] = {&m8T0Ser, &m8T1Ser, &m8T2Ser};
                uint32_t m8NodeBase = 0, m8BrickBase = 0, m8PoolBase = 0, m8TierRefBase = 0, m8MipPoolBase = 0;
                for (int k = 0; k < 3; ++k) {
                    Vixen::SVO::SerializedOctree& s = *m8Octs[k];
                    s.config.nodeArrayBase  = static_cast<int32_t>(m8NodeBase);
                    s.config.brickArrayBase = static_cast<int32_t>(m8BrickBase);
                    Vixen::SVO::setSdfBrickArrayBase(s.config, m8PoolBase);
                    Vixen::SVO::setTierRefTableBase(s.config, m8TierRefBase);
                    Vixen::SVO::setMipPoolBase(s.config, m8MipPoolBase);

                    m8Cat.configs[k]       = s.config;
                    m8Cat.nodeCounts[k]    = s.nodeCount;
                    m8Cat.brickCounts[k]   = s.brickCount;
                    m8Cat.tierRefCounts[k] = static_cast<uint32_t>(s.tierRefs.size());

                    m8Cat.nodes.insert(m8Cat.nodes.end(), s.nodes.begin(), s.nodes.end());
                    m8Cat.bricks.insert(m8Cat.bricks.end(), s.bricks.begin(), s.bricks.end());
                    m8Cat.channelPool.insert(m8Cat.channelPool.end(), s.channelPool.begin(), s.channelPool.end());
                    m8Cat.brickGridLookup.insert(m8Cat.brickGridLookup.end(), s.brickGridLookup.begin(), s.brickGridLookup.end());
                    m8Cat.tierRefTable.insert(m8Cat.tierRefTable.end(), s.tierRefs.begin(), s.tierRefs.end());
                    m8Cat.mipPool.insert(m8Cat.mipPool.end(), s.mipPool.begin(), s.mipPool.end());

                    if (m8Cat.materials.empty()) {
                        m8Cat.materials = s.materials;
                    }

                    m8NodeBase    += s.nodeCount;
                    m8BrickBase   += s.brickCount;
                    m8PoolBase    += s.brickCount * s.brickStrideFloats;
                    m8TierRefBase += static_cast<uint32_t>(s.tierRefs.size());
                    m8MipPoolBase += s.nodeCount * s.channelCount;
                }

                if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                    bodyScene->SetRecipePool(std::move(m8Cat));
                    // M8 Task 21: grant brick residency FROM CONSTRUCTION (not mid-flight).
                    // Task 20's validator found this scene otherwise stays mip-only
                    // (streaming-only), and a mip-only body can ONLY render via the LOD
                    // gate's decline-to-mip path -- there is no resident-brick fallback for
                    // it to fall through to below the coefficient hop0's crossing needs.
                    // Explicit call (rather than relying on residencyRequested_'s default)
                    // so this scene's residency state is unambiguous regardless of the
                    // node's default.
                    bodyScene->RequestBrickResidency(true);

                    constexpr float kRenderScale = 4.8f;
                    constexpr float kHalf = 5.0f * kRenderScale;
                    Vixen::SVO::BodyInstanceGpu inst{};
                    Vixen::SVO::SetInstanceTranslationScale(inst, glm::vec3(64.0f - kHalf, 64.0f - kHalf, 64.0f - kHalf), kRenderScale);
                    inst.material.color[0]     = 1.0f;
                    inst.material.color[1]     = 1.0f;
                    inst.material.color[2]     = 1.0f;
                    inst.material.octreeIndex  = 0u;
                    inst.material.providerKind = 0u;
                    inst.material.recipeId     = 0u;

                    bodyScene->SetInstances({inst});

                    // Record the two crossing octants' world positions for the scripted
                    // look-target retargeting (VulkanGraphApplication.cpp). Both hops use the
                    // SAME deterministic camera-facing selection (octant 4, bit pattern
                    // x=0,y=0,z=1), so the world offset from body center is the established
                    // (-2.5R,-2.5R,+2.5R) constant (M7 Task 13, validator-confirmed
                    // R-proportional fact of RootLeafOctantCenterLocal's own convention) --
                    // IDENTICAL for hop0 and hop1 since renderScale is fixed at every tier
                    // (only the child octree's own internal scale shrinks via childScale;
                    // the crossing octant's own WORLD position is a property of the PARENT
                    // tree's instance placement, unaffected by what childScale the crossing
                    // leads into).
                    const glm::vec3 bodyCenterWorld(64.0f, 64.0f, 64.0f);
                    // M8 Task 23: record the ACTUAL child anchor's world position (now
                    // RootLeafOctantCenterLocal(4), the proven-safe centered fixed point — see
                    // this scene's own construction comment above for why the prior
                    // corner-adjacent entry point was replaced) so the flight schedule
                    // approaches/aims at genuinely reachable child content, not a boundary
                    // point ~1024x too small a target to hit off-axis. World mapping: the
                    // [1,2) local cube spans 10*renderScale world units anchored at
                    // inst.worldPos (kHalf = 5R puts local 1.5 at body center 64, verified:
                    // worldPos + 0.5*10R = 40 + 24 = 64).
                    const float kCubeWorldEdge = 10.0f * kRenderScale;  // 48wu
                    const glm::vec3 instWorldPos(64.0f - kHalf, 64.0f - kHalf, 64.0f - kHalf);
                    m8EarthHop0OctantWorld_ = instWorldPos
                        + (m8T0ChildOriginLocal - glm::vec3(1.0f)) * kCubeWorldEdge;
                    // M8 Task 24: T1's own marked crossing leaf (m8T1ChildOriginLocal, T1->T2's
                    // childOriginLocal) lives in T1's OWN local [1,2) frame, NOT T0's -- it must
                    // NOT be aliased to hop0's world position (the Task 23 bug: both hops aimed at
                    // the SAME point, so hop1 never got its own distinct flight target). Map it
                    // into T0's local frame first via the INVERSE of remapRayIntoChildFrame
                    // (GpuTraversalMirror.h/BodyInstanceRayMarch.comp's own forward remap is
                    // childLocal = (parentLocal - childOrigin)*invScale + 1.5, so the inverse is
                    // parentLocal = childOrigin + (childLocal - 1.5)*childScale), then apply the
                    // SAME instWorldPos/kCubeWorldEdge mapping hop0 uses to reach world space.
                    const glm::vec3 m8T1AnchorInT0Local = m8T0ChildOriginLocal
                        + (m8T1ChildOriginLocal - glm::vec3(1.5f)) * kChildScale;
                    m8EarthHop1OctantWorld_ = instWorldPos
                        + (m8T1AnchorInT0Local - glm::vec3(1.0f)) * kCubeWorldEdge;

                    mainLogger->Info("[BuildRenderGraph] VIXEN_TIER_M8_EARTH_DEMO: T0 leaf ("
                                  + std::to_string(m8T0MarkDescIdx) + "," + std::to_string(m8T0MarkOctant)
                                  + ") -> T1 octree1 (childScale=2^-10); T1 leaf (" + std::to_string(m8T1MarkDescIdx) + ","
                                  + std::to_string(m8T1MarkOctant) + ") -> T2 octree2 (childScale=2^-10); "
                                  "hop0 crossing octant world pos ("
                                  + std::to_string(m8EarthHop0OctantWorld_.x) + ","
                                  + std::to_string(m8EarthHop0OctantWorld_.y) + ","
                                  + std::to_string(m8EarthHop0OctantWorld_.z) + "); "
                                  "hop1 (T1's own) crossing world pos ("
                                  + std::to_string(m8EarthHop1OctantWorld_.x) + ","
                                  + std::to_string(m8EarthHop1OctantWorld_.y) + ","
                                  + std::to_string(m8EarthHop1OctantWorld_.z) + ")");
                }
            } else {
                mainLogger->Error("[BuildRenderGraph] VIXEN_TIER_M8_EARTH_DEMO: no camera-facing leaf found in T0 or T1 — demo scene not built");
            }
        } else if (envFlagEnabled("VIXEN_SHADOW_DEMO")) {
            // VIXEN_SHADOW_DEMO — Sampled Lighting Inc1 M4 live gate: two Procedural
            // spheres positioned so the smaller "occluder" sits directly between the
            // larger "target" sphere's CAMERA-FACING surface and the default directional
            // light (normalize(1,1,-1) — see LightingConfigNode's default), casting a
            // visible shadow onto the target's visible hemisphere. A third sphere far to
            // the side stays fully lit (no occluder in its light path) as an in-frame
            // A/B control.
            //
            // Geometry: the default camera sits at (64,64,300) looking toward -Z at the
            // scene centre (64,64,64) (see PARAM_CAMERA_*/PARAM_ORBIT_* above) — the
            // camera-visible hemisphere of any body at (64,64,64) is its +Z-facing side.
            // light direction (1,1,-1) points from a surface point TOWARD the light (the
            // Light.direction_or_position convention — see Lighting.glsl's data-driven
            // overload) — its -Z component means the light itself sits toward -Z, i.e.
            // BEHIND the camera, so a +Z-facing point's dot(normal,lightDir) is positive
            // and it DOES get lit (normal ~=(0,0,1), lightDir has -Z component but also
            // +X/+Y, dot = -(-1)/sqrt3 + 0 + 0 ... to guarantee a clean positive NdotL on
            // the exact camera-facing point (0,0,1) normal, use dot((0,0,1),(1,1,-1)) =
            // -1/sqrt3 < 0 — NEGATIVE, meaning the dead-center camera-facing point is
            // actually NOT lit by this light. Placing the occluder to block a grazing
            // lit point instead: the point offset toward (+1,+1,0) from centre (normal
            // (1,1,0)/sqrt2) has dot with lightDir = (1+1+0)/(sqrt2*sqrt3) > 0 — lit and
            // camera-visible (still has positive Z-ish visibility at this camera
            // distance/FOV). Occluder sits between THAT point and the light.
            constexpr float kTargetRadius   = 24.0f;
            constexpr float kOccluderRadius = 8.0f;
            constexpr float kOccluderGap    = 3.0f;  // standoff so the occluder doesn't contain surfacePoint
            auto placeProceduralSphere = [&](float cx, float cy, float cz, float radius,
                                             float r, float g, float b) {
                Vixen::SVO::BodyInstanceGpu inst{};
                Vixen::SVO::SetInstanceTranslationScale(inst, glm::vec3(cx, cy, cz), 1.0f);
                inst.material.color[0] = r; inst.material.color[1] = g; inst.material.color[2] = b;
                inst.material.octreeIndex = 0u;
                inst.material.providerKind = 1u;  // PROVIDER_PROCEDURAL
                inst.material.recipeId = 0u;      // sphere
                inst.material.recipeParams[0] = radius;
                inst.material.recipeParams[1] = 0.0f;
                inst.material.recipeParams[2] = 0.0f;
                return inst;
            };
            const glm::vec3 targetCenter(64.0f, 64.0f, 64.0f);
            const glm::vec3 lightDir = glm::normalize(glm::vec3(1.0f, 1.0f, -1.0f));
            // The grazing lit-and-visible point: centre + radius * normalize(1,1,0.3) —
            // mostly toward +X/+Y (lit by lightDir, NdotL~0.68) with a touch of +Z
            // (camera-visible at this FOV/distance, dot(normal,viewDir)~0.11 > 0).
            const glm::vec3 litPointDir = glm::normalize(glm::vec3(1.0f, 1.0f, 0.3f));
            const glm::vec3 surfacePoint = targetCenter + litPointDir * kTargetRadius;
            // Occluder sits along lightDir from surfacePoint, offset by radius+gap so its
            // near edge (not its centre) is the one that meets the surface — verified via
            // the analytic ray-sphere test (t0~2.97>0 along [surfacePoint,lightDir]).
            const glm::vec3 occluderCenter = surfacePoint + lightDir * (kOccluderRadius + kOccluderGap);
            std::vector<Vixen::SVO::BodyInstanceGpu> shadowBodies = {
                placeProceduralSphere(targetCenter.x, targetCenter.y, targetCenter.z,
                                      kTargetRadius, 0.9f, 0.9f, 0.9f),                 // target
                placeProceduralSphere(occluderCenter.x, occluderCenter.y, occluderCenter.z,
                                      kOccluderRadius, 0.2f, 0.2f, 0.2f),               // occluder
                placeProceduralSphere(150.0f, 64.0f, 64.0f, kTargetRadius, 0.9f, 0.9f, 0.9f), // lit control
            };
            if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                bodyScene->SetInstances(std::move(shadowBodies));
                mainLogger->Info("[BuildRenderGraph] VIXEN_SHADOW_DEMO: seeded target+occluder+litControl body instances");
            }
        } else if (envFlagEnabled("VIXEN_STARLIGHT_DEMO")) {
            // T-1139 witness scene: the emissive procedural sphere also authors
            // one entry in the existing shared LightingConfig light set.
            const glm::vec3 starCenter(64.0f, 64.0f, 64.0f);
            const glm::vec3 starColor(1.0f, 0.72f, 0.24f);
            constexpr float kStarRadius = 8.0f;
            constexpr float kStarEmission = 16.0f;
            constexpr float kStarLightRange = 100.0f;
            auto makeSphere = [](glm::vec3 center, float radius, glm::vec3 color, float emission) {
                Vixen::SVO::BodyInstanceGpu inst{};
                Vixen::SVO::SetInstanceTranslationScale(inst, glm::vec3(center.x, center.y, center.z), 1.0f);
                inst.material.color[0] = color.x; inst.material.color[1] = color.y; inst.material.color[2] = color.z;
                inst.material.providerKind = 1u;  // PROVIDER_PROCEDURAL
                inst.material.recipeId = 0u;      // sphere
                inst.material.recipeParams[0] = radius;
                inst.material.recipeParams[3] = emission;
                return inst;
            };

            std::vector<Vixen::SVO::BodyInstanceGpu> starBodies = {
                makeSphere(starCenter, kStarRadius, starColor, kStarEmission),
                // Keep the bodies clear of the default HUD panels in the capture frame.
                makeSphere(starCenter + glm::vec3(-23.0f, 12.0f, -25.0f), 5.0f,
                           glm::vec3(0.25f, 0.55f, 0.95f), 0.0f),
                makeSphere(starCenter + glm::vec3(23.0f, 12.0f, -25.0f), 5.0f,
                           glm::vec3(0.9f, 0.28f, 0.18f), 0.0f),
                makeSphere(starCenter + glm::vec3(23.0f, -14.0f, -25.0f), 5.0f,
                           glm::vec3(0.25f, 0.82f, 0.36f), 0.0f),
            };

            const Vixen::SVO::BodyInstanceGpu& star = starBodies.front();
            Vixen::Gpu::Light starLight{};
            const glm::vec3 starPosition = Vixen::SVO::TransformPoint(
                star.transform.localToWorld, glm::vec3(0.0f));
            starLight.direction_or_positionX = starPosition.x;
            starLight.direction_or_positionY = starPosition.y;
            starLight.direction_or_positionZ = starPosition.z;
            starLight.kind = 1u;  // point
            starLight.radianceX = star.material.color[0] * star.material.recipeParams[3];
            starLight.radianceY = star.material.color[1] * star.material.recipeParams[3];
            starLight.radianceZ = star.material.color[2] * star.material.recipeParams[3];
            starLight.range = kStarLightRange;

            if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                bodyScene->SetInstances(std::move(starBodies));
                mainLogger->Info("[BuildRenderGraph] VIXEN_STARLIGHT_DEMO: seeded one emissive star and three bodies");
            }
            if (auto* lighting = static_cast<LightingConfigNode*>(renderGraph->GetInstance(lightingConfigNode))) {
                Vixen::App::SetSceneLightsLayer(*lighting, LookLightSet::From({starLight}, 0.04f, {1.5f}));
            }
        } else if (envFlagEnabled("VIXEN_DDGI_CORNELL_BAKED_DEMO")) {
            // Sampled Lighting — Cornell Box GI Reference Scene, M1 (baked variant).
            // Plan: Vixen-Docs/01-Architecture/Sampled-Lighting-Cornell-Box-Demo-Plan-2026-07.md
            //
            // Geometry/color/camera/probe-grid numbers all come from ONE shared source
            // (application/main/include/graph/CornellBoxSceneDefinition.h) — M2's virtual
            // variant reads the SAME header verbatim, so "ideally visually identical" is
            // enforced by construction (see that header's own file comment).
            //
            // M3 round 2 (2026-07-14): this block now consumes BuildCornellWorldSpaceBodies()
            // (this file's anonymous namespace, above) instead of independently authoring its
            // own gridBoxAt-based instructions -- the SAME world-space SdfInstruction list the
            // virtual variant splices directly now feeds this bake path too, via
            // MakeCornellWorldSpaceEvalFn's grid->world adapter (below). See that function's own
            // header comment for the full "why unify" reasoning. M1's own recipe-VM
            // investigation findings (RoundedBox primitive choice, emission being a bake-time-
            // only concept) are unchanged and still apply — see BuildCornellWorldSpaceBodies.
            //
            // Multi-body assembly mirrors VIXEN_DDGI_LEAK_GATE_DEMO's own established shape
            // (this file, above): bake each body separately, BuildSdfBodyOctree per body,
            // ConcatenateSdfWithMips across all bodies, BuildLightTreeCut over the emissive body
            // only, SetRecipePool + SetInstances. 8 bodies here (5 walls + 1 light + 2 objects)
            // vs. the leak-gate scene's 2 — same mechanism, more bodies.
            using namespace Vixen::App::CornellBox;
            mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_BAKED_DEMO: building the Cornell "
                              "box GI reference scene (baked/octree variant)");

            constexpr float kBand = 2.0f;
            constexpr float kWorldGridSize = 10.0f;  // ShellOctreeGpu.h's fixed octree-local->world span

            // bodyWorldPos/bodyRenderScale: the SAME grid<->world mapping the runtime shader's
            // octree traversal already uses (world = worldPos + (grid/n)*kWorldGridSize*
            // renderScale) -- unchanged from every prior round, still the single source of truth
            // for how a per-body bake grid maps onto the scene's true world coordinates.
            auto bodyWorldPos = [](glm::vec3 bodyWorldCenter, int n, int subdiv) {
                return bodyWorldCenter - glm::vec3(static_cast<float>(n) / (2.0f * static_cast<float>(subdiv)));
            };
            auto bodyRenderScale = [](int n, int subdiv) {
                return static_cast<float>(n) / (static_cast<float>(subdiv) * kWorldGridSize);
            };

            // MakeCornellWorldSpaceEvalFn (M3 round 2): a small adapter that lets BakeSdfWorld's
            // raw-grid-coordinate eval callback evaluate a WORLD-SPACE-authored SdfInstruction
            // program directly, by mapping p_raw -> true world position via EXACTLY
            // bodyWorldPos/bodyRenderScale's own formula above (algebraically verified: world
            // (p_raw) = bodyWorldCenter + (p_raw - n/2)/subdiv, i.e. grid center n/2 maps to the
            // body's own true world center). This is what makes brick-density (subdiv, chosen
            // per-body purely for bake-grid resolution) fully independent of instruction
            // authoring (now a single shared world-space source, BuildCornellWorldSpaceBodies) --
            // subdivision no longer needs to be pre-baked into the recipe's own half-extents/
            // positions the way the pre-unification gridBoxAt did.
            // Baked-Perf M1 Task 1.2 (grid-unit contract fix): the shared shader march
            // (StoredSdf.glsl marchBrickSdf) sphere-traces in GRID-VOXEL arc-length,
            // stepping s += d*(1/sqrt(3)) and treating the stored Density AS a grid-voxel
            // distance (its gradient w.r.t. the grid must be 1). evalRecipe(world) returns
            // a TRUE WORLD-space distance -- its gradient w.r.t. the grid coordinate is
            // 1/subdiv (world = bodyWorldCenter + (pRaw-n/2)/subdiv), so storing it
            // unscaled understeps the march by subdiv x (4x for kWallSubdiv=4 walls) and
            // bakes the occupancy band subdiv x too thick (Task 1.3 fixes the band
            // separately). Multiplying by subdiv here converts the stored Density to a
            // grid-unit distance, matching the grid-unit convention SdfRecipes.h's
            // evalSdf/BakeRecipeToSdfWorld (the OTHER bake path, and
            // test_stored_sdf_march_mirror) already use -- so the shared march is
            // correct for ALL bodies with NO shader change. subdiv=1 bodies (light/
            // sphere/box, kSmallSubdiv) are byte-identical (*1 is a no-op).
            //
            // Proven ~3.8x FPS win on fix/baked-sdf-perf-rootfix (see
            // Baked-SDF-Perf-Rootfix-2026-07.md); that attempt alone made bodies 5/6/7
            // vanish from the [CornellDiag] instIdx map because of the brickLookupBase
            // mis-addressing bug (Task 1.1, landed first and validated separately) --
            // NOT because of this multiply. Do not revert this without re-checking 1.1.
            auto makeWorldSpaceEval = [](const std::vector<SdfInstruction>& prog, glm::vec3 bodyWorldCenter, int n, int subdiv) {
                return [&prog, bodyWorldCenter, n, subdiv](const glm::vec3& pRaw) {
                    const glm::vec3 world = bodyWorldCenter + (pRaw - glm::vec3(static_cast<float>(n) * 0.5f)) / static_cast<float>(subdiv);
                    return Vixen::SVO::Recipe::evalRecipe(prog.data(), static_cast<uint32_t>(prog.size()), world) * static_cast<float>(subdiv);
                };
            };
            // Bake each body with a FLAT WHITE per-voxel color (Cornell M3 round 7 fix):
            // BakeSdfWorld's DEFAULT per-voxel color is a debug rainbow (SdfBake.h
            // DefaultBandColor), which the STORED shading path multiplies by inst.material.color
            // (TraceWorld.glsl: bestColor = hitColor*inst.material.color) -- rendering every wall as
            // garish rainbow*tint blotches instead of a flat per-wall color. Baking WHITE
            // (1,1,1) makes hitColor*inst.material.color == inst.material.color, i.e. the STORED path now
            // yields EXACTLY the same flat per-wall tint the virtual/PROCEDURAL variant
            // already shows (bestColor = inst.material.color; that path has no baked voxel channel).
            // White (not the per-body tint) is deliberate: the tint is already applied ONCE
            // via inst.material.color, so baking the tint too would square it (darken). Passing the
            // color as the ColorFn (rather than overwriting the serialized channelPool
            // post-bake) fixes BOTH the brick channelPool and the mip pool consistently, and
            // touches the shared BakeSdfWorld only via its additive, default-preserving
            // ColorFn parameter (the rainbow default is still what every other caller/test gets).
            // SDF Bake Box-Tight Region M2: worldHalfExtent (optional, sentinel (0,0,0) = full
            // cube, matching SdfBake.h's own bakeRegion(0,0,0)-means-default convention) is the
            // body's TRUE per-axis half-extent in WORLD units (thin on a wall's thickness axis,
            // wide on its span axes). makeWorldSpaceEval maps grid coord n/2 -> the body's true
            // world center (see that lambda's own header comment), so the body's occupied region
            // is centered on grid n/2, not on the bakeRegion window's own center -- bakeRegion
            // itself is an ORIGIN-ANCHORED [0,region) window (SdfBake.h's contract), so the
            // region upper bound must cover n/2 + (worldHalfExtent+kBand)*subdiv on every axis
            // (there is no room to shrink the LOW side of an origin-anchored window when the
            // occupied span is centered at n/2, only the high side beyond what's occupied).
            // Rounded up to the next brick boundary (brickSide=8) since bakeRegion snaps to
            // whole bricks anyway (SdfBake.h's own occupancy/active-brick arrays are brick-
            // granular) — a non-brick-aligned bound buys nothing and just obscures the real
            // brick count in a diagnostic dump.
            // Baked-Perf M7 Task 7.1: serializes calls into Gaia's process-wide
            // ChunkAllocator singleton (see the header comment at the parallel-bake launch
            // site below for the full root-cause writeup) -- a function-local static so every
            // caller of this render-graph-build function (only ever invoked once per process
            // launch, but a static is the standard "exactly one, lazily initialized, no
            // separate declaration site to keep in sync" idiom for this) shares the same lock.
            auto bakeWorldSpaceBody = [&](const std::vector<SdfInstruction>& prog, glm::vec3 bodyWorldCenter, int n, int subdiv,
                                          glm::vec3 worldHalfExtent = glm::vec3(0.0f)) {
                glm::ivec3 bakeRegion(0);
                if (worldHalfExtent != glm::vec3(0.0f)) {
                    constexpr int kBrickSide = 8;
                    for (int axis = 0; axis < 3; ++axis) {
                        const float hi = static_cast<float>(n) * 0.5f +
                                         (worldHalfExtent[axis] + kBand) * static_cast<float>(subdiv);
                        const int hiRoundedUp = ((static_cast<int>(std::ceil(hi)) + kBrickSide - 1) / kBrickSide) * kBrickSide;
                        bakeRegion[axis] = std::min(hiRoundedUp, n);
                    }
                }
                Vixen::SVO::SdfBakeResult baked = Vixen::SVO::BakeSdfWorld(
                    makeWorldSpaceEval(prog, bodyWorldCenter, n, subdiv), bodyWorldCenter, n, kBand,
                    3, Vixen::SVO::NoEmission,
                    [](const glm::vec3&) { return glm::vec3(1.0f); },
                    bakeRegion);
                return Vixen::SVO::BuildSdfBodyOctree(baked, 3);
            };

            // Wall grid (subdiv=4): widest true extent 2*(kBoxHalfExtent+kWallThickness)(=22)
            // + 2*kBand(=4) margin, scaled by subdiv -> (22+4)*4=104.
            //
            // CRITICAL (combined-verify round, 2026-07-15): the bake grid `n` MUST be a POWER OF
            // TWO. BuildSdfBodyOctree (SdfBake.h) derives the octree's own voxel resolution as
            // 2^(maxLevels-brickDepth) where maxLevels = floor(log2(n)) + brickDepth -- i.e. it
            // silently ROUNDS n DOWN to the previous power of two. The prior value 112 (and the
            // even-earlier 96) are NOT powers of two, so the octree was built at a 64-voxel grid
            // while the SDF was baked into a 112-voxel grid: the octree's bricksPerAxis (8) and
            // the config's bricksPerAxisSdf from the 112-grid bake (14) DISAGREE, so the shader's
            // brick/voxel addressing reads the wrong cells -- producing spurious/misplaced
            // occupancy (grazing rays over the ceiling hit a far ghost surface at ~y=37.6, z=-24,
            // hitT~78; ~4% of pixels land out-of-bounds and poison the DDGI probe gather with
            // noise). Using 128 (the next power of two >= 104) makes the octree grid EQUAL the
            // bake grid, so addressing is consistent end to end. This is the "leftover interaction
            // between the world-space-unification refactor and the baked traversal path" the
            // geometry-fix rounds flagged but never pinned: those rounds set n to non-pow2 values
            // (96 then 112) for brick-alignment without noticing the octree builder's pow2 contract.
            //
            // subdiv breaks the "grid-unit == 1 world unit" identity so a grid cell is
            // 1/subdiv world units instead of 1 -- brick world-size (always brickSide=8 world
            // units under subdiv=1, independent of n) becomes brickSide/subdiv, making thin
            // (~2-6 world unit) walls resolvable instead of dilating into solid overlapping
            // slabs (round 5's own finding). Light/object grid: no subdivision needed (their
            // thinnest dims are close enough to a full brick that subdividing them is a smaller,
            // non-blocking cosmetic concern).
            //
            // Baked-Perf M5 Task 5.5 (small-body bake resolution bump): kSmallN was 16 (pow2)
            // -- at subdiv=1 that is ~1 voxel/world-unit across the light/sphere/box bodies'
            // full span, the COARSEST normals in the scene (every downstream light term is
            // NdotL-gated -- Brdf.glsl / ProbeUpdate.comp -- so a coarse/faceted normal field
            // reads as visibly near-black vs the virtual/procedural path's exact analytic
            // normal). Doubled to 32 (still pow2, no BuildSdfBodyOctree round-up surprise --
            // see that function's own pow2-round-up comment) for ~2 voxels/world-unit: no
            // box-tight bakeRegion is passed for these 3 bodies (sphere/box/light bake their
            // FULL grid, unlike the walls), so n alone determines sampling resolution here --
            // a clean resolution bump with no other side effect on bake geometry/placement.
            // kSmallSubdiv stays 1: unlike the walls (which need subdiv to resolve a THIN
            // slab-shaped body), these bodies are roughly cube-shaped at a scale n already
            // resolves reasonably -- doubling n directly (rather than adding subdiv) is the
            // simpler lever and keeps bodyWorldPos/bodyRenderScale's grid<->world mapping
            // unchanged in form (just a different n).
            constexpr int kWallSubdiv = 4;
            constexpr int kWallN   = 128;  // power of two (was 112 -- see the pow2 note above)
            constexpr int kSmallN  = 32;   // was 16 -- Task 5.5 normals-resolution bump
            constexpr int kSmallSubdiv = 1;

            std::vector<CornellWorldSpaceBody> bodies = BuildCornellWorldSpaceBodies();
            // bodies[]: leftWall, rightWall, backWall, floor, ceiling, light, sphereObj, boxObj (fixed order, see BuildCornellWorldSpaceBodies)

            // SDF Bake Box-Tight Region M2: per-wall TRUE world-space half-extents, mirroring
            // BuildCornellWorldSpaceBodies's own CornellWorldBoxAt(...) half-extent args exactly
            // (kWallThickness=1, kWallSpan=kBoxHalfExtent+kWallThickness=11, kZWideHalfExtent=
            // kBoxHalfExtent+kWallThickness*0.5=10.5) -- each wall is thin on exactly ONE axis,
            // wide on the other two, so passing these into bakeWorldSpaceBody's worldHalfExtent
            // shrinks the bake region on the thin axis only (the wide axes are already close to
            // the full kWallN cube's own span and see little/no reduction -- see this milestone's
            // Progress Log entry for the measured before/after brick counts).
            const float kWallThicknessHalf = 1.0f;              // CornellBoxSceneDefinition.h::kWallThickness
            const float kWallSpanHalf      = 11.0f;             // kBoxHalfExtent(10) + kWallThickness(1)
            const float kZWideHalf         = 10.5f;              // kBoxHalfExtent(10) + kWallThickness*0.5

            // Baked-Perf M7 Task 7.4: bake-artifact disk cache. Design note:
            // Vixen-Docs/01-Architecture/Baked-Perf-Fix-Pipeline-Plan-2026-07.md's
            // "Task 7.4 design note" section (written before this code). Key = FNV-1a
            // hash over every input that changes the bake OUTPUT: each body's SdfInstruction
            // program bytes + worldCenter/n/subdiv/worldHalfExtent, plus the shared kBand and
            // the light body's own emission intensity -- exactly the parameter list
            // bakeWorldSpaceBody/the light's direct BakeSdfWorld call take. A HIT skips the
            // entire 8-body bake AND the concat/mip-bake/light-tree-cut pass below (a warm
            // boot should reach file-load speed, not just skip the GaiaVoxelWorld bake).
            const glm::vec3 kNoHalfExtent(0.0f);
            Vixen::SVO::BakeArtifactKeyBuilder keyBuilder;
            // The store is shared across build trees: also key on the code that drives the bake
            // (this translation unit), the SVO fingerprint is already in the builder.
            keyBuilder.addBytes(Vixen::kBuildRenderGraphFingerprint, sizeof(Vixen::kBuildRenderGraphFingerprint) - 1);
            auto addBodyKeyInputs = [&](const CornellWorldSpaceBody& b, int n, int subdiv, glm::vec3 halfExtent) {
                keyBuilder.addProgram(std::span<const SdfInstruction>(b.prog.data(), b.prog.size()));
                keyBuilder.addVec3(b.worldCenter);
                keyBuilder.addI32(n);
                keyBuilder.addI32(subdiv);
                keyBuilder.addVec3(halfExtent);
            };
            addBodyKeyInputs(bodies[0], kWallN, kWallSubdiv, glm::vec3(kWallThicknessHalf, kWallSpanHalf, kZWideHalf));
            addBodyKeyInputs(bodies[1], kWallN, kWallSubdiv, glm::vec3(kWallThicknessHalf, kWallSpanHalf, kZWideHalf));
            addBodyKeyInputs(bodies[2], kWallN, kWallSubdiv, glm::vec3(kWallSpanHalf, kWallSpanHalf, kWallThicknessHalf));
            addBodyKeyInputs(bodies[3], kWallN, kWallSubdiv, glm::vec3(kWallSpanHalf, kWallThicknessHalf, kZWideHalf));
            addBodyKeyInputs(bodies[4], kWallN, kWallSubdiv, glm::vec3(kWallSpanHalf, kWallThicknessHalf, kZWideHalf));
            addBodyKeyInputs(bodies[5], kSmallN, kSmallSubdiv, kNoHalfExtent);  // light
            addBodyKeyInputs(bodies[6], kSmallN, kSmallSubdiv, kNoHalfExtent);  // sphereObj
            addBodyKeyInputs(bodies[7], kSmallN, kSmallSubdiv, kNoHalfExtent);  // boxObj
            keyBuilder.addFloat(kBand);
            keyBuilder.addFloat(kLightEmissionIntensity);
            const std::string bakeArtifactKey = keyBuilder.hexKey();

            std::optional<Vixen::SVO::BakeArtifactBundle> cachedBundle =
                Vixen::SVO::LoadBakeArtifact(bakeArtifactKey);
            if (cachedBundle.has_value()) {
                mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_BAKED_DEMO: bake-artifact "
                                  "cache HIT (key=" + bakeArtifactKey + ") -- skipping bake+concat+"
                                  "light-tree-cut, loading from disk");
            } else {
                mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_BAKED_DEMO: bake-artifact "
                                  "cache MISS (key=" + bakeArtifactKey + ") -- baking fresh");
            }

            // kLightWorldPos/kLightRenderScale are pure functions of bodies[5]/kSmallN/
            // kSmallSubdiv -- needed by the instance-seeding tail regardless of cache hit/miss,
            // so computed once up front rather than duplicated in both branches.
            const glm::vec3 kLightWorldCenter = bodies[5].worldCenter;
            const glm::vec3 kLightWorldPos = bodyWorldPos(kLightWorldCenter, kSmallN, kSmallSubdiv);
            const float kLightRenderScale = bodyRenderScale(kSmallN, kSmallSubdiv);

            // Converged outputs: populated either from the cache (HIT, no bake at all) or
            // from a fresh bake+concat+light-tree-cut (MISS, then stored to the cache below
            // for the NEXT boot). Everything from the occupancy stats through SetRecipePool/
            // SetLightTreeCut reads only these two (plus bakeOk, which gates that whole tail
            // exactly like the pre-M7 code's own "lightOct == nullptr -> skip everything"
            // control flow did), so the hit/miss branches converge before any GPU-facing
            // state is touched.
            Vixen::SVO::ConcatenatedOctrees cat;
            std::vector<Vixen::SVO::LightTreeNode> worldCut;
            bool bakeOk = true;

            if (cachedBundle.has_value()) {
                cat = std::move(cachedBundle->cat);
                worldCut = std::move(cachedBundle->lightTreeCut);
            } else {

            // The 8 bodies bake in order on this thread (owner phase). Every World's chunk creation
            // and octree rebuild() allocate through gaia::ecs::ChunkAllocator::get(), a process-wide
            // singleton with an unsynchronized per-page free list (the T-031 parallel wave crashed
            // with "Block allocator recycle list broken!"). Exactly one writer at a time needs no
            // lock; the former mutex spanned each whole bake, so the wave never overlapped anyway.
            std::array<std::optional<Vixen::SVO::SdfBodyOctree>, 8> bakedBodies;
            const auto addBakeTask = [&](size_t bodyIndex, const std::function<Vixen::SVO::SdfBodyOctree()>& bake) {
                bakedBodies[bodyIndex].emplace(bake());
            };

            addBakeTask(0, [&] { return bakeWorldSpaceBody(
                bodies[0].prog, bodies[0].worldCenter, kWallN, kWallSubdiv,
                glm::vec3(kWallThicknessHalf, kWallSpanHalf, kZWideHalf)); });
            addBakeTask(1, [&] { return bakeWorldSpaceBody(
                bodies[1].prog, bodies[1].worldCenter, kWallN, kWallSubdiv,
                glm::vec3(kWallThicknessHalf, kWallSpanHalf, kZWideHalf)); });
            addBakeTask(2, [&] { return bakeWorldSpaceBody(
                bodies[2].prog, bodies[2].worldCenter, kWallN, kWallSubdiv,
                glm::vec3(kWallSpanHalf, kWallSpanHalf, kWallThicknessHalf)); });
            addBakeTask(3, [&] { return bakeWorldSpaceBody(
                bodies[3].prog, bodies[3].worldCenter, kWallN, kWallSubdiv,
                glm::vec3(kWallSpanHalf, kWallThicknessHalf, kZWideHalf)); });
            addBakeTask(4, [&] { return bakeWorldSpaceBody(
                bodies[4].prog, bodies[4].worldCenter, kWallN, kWallSubdiv,
                glm::vec3(kWallSpanHalf, kWallThicknessHalf, kZWideHalf)); });
            addBakeTask(5, [&] {
                // Light body: same self-contained bake-and-build sequence, with emission.
                Vixen::SVO::SdfBakeResult lightBaked = Vixen::SVO::BakeSdfWorld(
                    makeWorldSpaceEval(bodies[5].prog, kLightWorldCenter, kSmallN, kSmallSubdiv),
                    kLightWorldCenter, kSmallN, kBand, 3,
                    [](const glm::vec3&) { return kLightEmissionIntensity; },
                    [](const glm::vec3&) { return glm::vec3(1.0f); });
                return Vixen::SVO::BuildSdfBodyOctree(lightBaked, 3);
            });
            addBakeTask(6, [&] { return bakeWorldSpaceBody(
                bodies[6].prog, bodies[6].worldCenter, kSmallN, kSmallSubdiv, glm::vec3(0.0f)); });
            addBakeTask(7, [&] { return bakeWorldSpaceBody(
                bodies[7].prog, bodies[7].worldCenter, kSmallN, kSmallSubdiv, glm::vec3(0.0f)); });

            Vixen::SVO::SdfBodyOctree leftWallBody   = std::move(*bakedBodies[0]);
            Vixen::SVO::SdfBodyOctree rightWallBody  = std::move(*bakedBodies[1]);
            Vixen::SVO::SdfBodyOctree backWallBody   = std::move(*bakedBodies[2]);
            Vixen::SVO::SdfBodyOctree floorBody      = std::move(*bakedBodies[3]);
            Vixen::SVO::SdfBodyOctree ceilingBody    = std::move(*bakedBodies[4]);
            Vixen::SVO::SdfBodyOctree lightBody      = std::move(*bakedBodies[5]);
            Vixen::SVO::SdfBodyOctree sphereObjBody  = std::move(*bakedBodies[6]);
            Vixen::SVO::SdfBodyOctree boxObjBody     = std::move(*bakedBodies[7]);

            // (kLightWorldPos/kLightRenderScale declared above, before the cache branch.)

            const Vixen::SVO::Octree* lightOct = lightBody.octree->getOctree();
            if (lightOct == nullptr) {
                mainLogger->Error("[BuildRenderGraph] VIXEN_DDGI_CORNELL_BAKED_DEMO: light body octree is null -- scene not built");
                bakeOk = false;  // skip the occupancy/instance-seeding tail below, same as the pre-M7 control flow
            } else {
                // Baked-Perf M7 Task 7.2 (audit F5): the light body used to be serialized
                // AND mip-baked here, then mip-baked a SECOND time (BakeMipPool called again
                // just to get a MipPool object for BuildLightTreeCut, discarding the first
                // bake BakeAndAttachMipPool already did internally), then serialized a THIRD
                // time inside ConcatenateSdfWithMips's own per-body loop below. BakeMipPool is
                // pure (same octree + same already-serialized channelPool in, same MipPool
                // out), so baking once and reusing the result for both BuildLightTreeCut and
                // the concat pass is byte-identical, not an approximation.
                Vixen::SVO::SerializedOctree lightSer = Vixen::SVO::SerializeSdf(lightBody);
                Vixen::SVO::MipPool lightMipPool = Vixen::SVO::BakeMipPool(*lightOct, lightSer);
                lightSer.mipPool = Vixen::SVO::SerializeMipPool(lightMipPool);

                Vixen::SVO::LightTreeCutParams cutParams;
                cutParams.powerThreshold = 0.001f;  // same fine-cut rationale as VIXEN_DDGI_LEAK_GATE_DEMO
                std::vector<Vixen::SVO::LightTreeNode> cut =
                    Vixen::SVO::BuildLightTreeCut(*lightOct, lightSer, lightMipPool, kSmallN, cutParams);

                // Grid->world transform for the light-tree cut, mirroring the SAME formula this
                // block's own header comment derives for body geometry (world = worldPos +
                // (grid/n)*kWorldGridSize*renderScale) -- the light-tree cut's own worldPos/
                // worldExtent fields are in the light body's bake-grid frame and need the
                // IDENTICAL transform the light body's own BodyInstanceGpu (kLightWorldPos,
                // kLightRenderScale, both derived from the SAME kSmallN grid above) applies, or
                // the light-tree's stashed cut (read by ProbeUpdate.comp for shading) would
                // disagree with where the light BODY geometry actually renders -- the exact
                // class of bug VIXEN_DDGI_LEAK_GATE_DEMO's own header comment warns about (an
                // unrescaled cut silently lands outside the probe grid's [0,32) coverage).
                // (worldCut is the OUTER-scope vector declared above the cache branch -- both
                // the HIT and MISS paths populate the same variable so the tail below is shared.)
                worldCut.reserve(cut.size());
                for (const auto& node : cut) {
                    Vixen::SVO::LightTreeNode w = node;
                    w.worldPos = kLightWorldPos + (node.worldPos / static_cast<float>(kSmallN)) * kWorldGridSize * kLightRenderScale;
                    w.worldExtent = (node.worldExtent / static_cast<float>(kSmallN)) * kWorldGridSize * kLightRenderScale;
                    worldCut.push_back(w);
                }

                if (auto* lightTreeInst = static_cast<LightTreeBufferNode*>(renderGraph->GetInstance(lightTreeBufferNode))) {
                    lightTreeInst->SetLightTreeCut(worldCut);
                }
                mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_BAKED_DEMO: light-tree cut=" +
                                  std::to_string(cut.size()) + " nodes");

                std::vector<const Vixen::SVO::SdfBodyOctree*> octreesForCat = {
                    &leftWallBody, &rightWallBody, &backWallBody, &floorBody, &ceilingBody,
                    &lightBody, &sphereObjBody, &boxObjBody,
                };
                // Task 0.3 (Baked-Content Perf Audit F2): the TEMP DIAG block that used to sit
                // here (root-causing invisible walls, now resolved) re-serialized all 8 bodies via
                // SerializeSdf a SECOND time (ConcatenateSdfWithMips below does its own real
                // serialization pass) purely to log nodeCount/brickCount/bounds -- 23.0 s measured
                // on a fresh boot. Those counts are derived by WALKING the octree during
                // serialization (descriptors.size()/brickViews.size() in ShellOctreeGpu.h), not
                // stored as an O(1) field on Octree itself, so there is no cheap equivalent to
                // preserve -- deleted rather than fabricating an approximate substitute.
                //
                // Baked-Perf M7 Task 7.6: brick dedup by reference. Pre-serialize ALL 8 bodies
                // (reusing Task 7.2's SerializeSdfWithMips helper -- the light body's own
                // lightSer above is ALREADY exactly this, computed once), run DedupBricks on
                // each body's own SerializedOctree (dedup is scoped per-octree -- see
                // ShellOctreeGpu.h's own header comment on DedupBricks for why cross-octree
                // dedup would need a bigger addressing change), then hand ALL 8 as precomputed
                // entries to ConcatenateSdfWithMips so it serializes nothing itself and just
                // concatenates the already-deduplicated per-body arrays. This kills BOTH
                // intra-body duplication (a flat wall's identical interior bricks) and
                // inter-body duplication WITHIN each body's own dedup pass; true cross-body
                // sharing (the 5 walls' bricks against EACH OTHER) is out of scope for this
                // pass -- see the DedupBricks header comment.
                std::unordered_map<size_t, Vixen::SVO::SerializedOctree> precomputedSer;
                uint32_t totalOriginalBricks = 0;
                uint32_t totalDedupedBricks  = 0;
                for (size_t k = 0; k < octreesForCat.size(); ++k) {
                    Vixen::SVO::SerializedOctree ser = (k == 5)
                        ? lightSer  // already serialized+mip-baked above; copy, don't redo the work
                        : Vixen::SVO::SerializeSdfWithMips(*octreesForCat[k]);
                    const Vixen::SVO::BrickDedupResult dedup = Vixen::SVO::DedupBricks(ser);
                    totalOriginalBricks += dedup.originalBrickCount;
                    totalDedupedBricks  += dedup.dedupedBrickCount;
                    precomputedSer.emplace(k, std::move(ser));
                }
                const float overallDedupRatio = totalDedupedBricks > 0
                    ? static_cast<float>(totalOriginalBricks) / static_cast<float>(totalDedupedBricks)
                    : 1.0f;
                mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_BAKED_DEMO: brick dedup -- "
                                  "originalBricks=" + std::to_string(totalOriginalBricks) +
                                  " dedupedBricks=" + std::to_string(totalDedupedBricks) +
                                  " ratio=" + std::to_string(overallDedupRatio));

                // (cat is the OUTER-scope ConcatenatedOctrees declared above the cache branch.)
                cat = Vixen::SVO::ConcatenateSdfWithMips(octreesForCat, precomputedSer);

                // Baked-Perf M7 Task 7.4: store the fresh bake to the disk cache for the NEXT
                // boot. Store failures are logged but non-fatal (this boot already has a good
                // `cat`/`worldCut` in hand; only a future boot loses the warm-start benefit).
                Vixen::SVO::BakeArtifactBundle bundleToStore;
                bundleToStore.cat = cat;              // copy -- `cat` is still needed below (SetRecipePool moves it later)
                bundleToStore.lightTreeCut = worldCut; // copy -- `worldCut` is still needed below (SetLightTreeCut reads it)
                if (Vixen::SVO::StoreBakeArtifact(bakeArtifactKey, bundleToStore)) {
                    mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_BAKED_DEMO: bake-artifact "
                                      "cache STORE ok (key=" + bakeArtifactKey + ")");
                } else {
                    mainLogger->Warning("[BuildRenderGraph] VIXEN_DDGI_CORNELL_BAKED_DEMO: bake-artifact "
                                          "cache STORE failed (key=" + bakeArtifactKey + ") -- next boot will bake fresh again");
                }
                }  // else (bake MISS)
            }

            // Baked-Perf M7: everything below runs for BOTH a cache HIT (cat/worldCut loaded
            // from disk) and a fresh-bake MISS (cat/worldCut just computed above) -- gated on
            // bakeOk, which mirrors the pre-M7 code's own "lightOct == nullptr -> skip
            // everything downstream" control flow (now the only way this can be false is the
            // MISS branch's own lightOct-null check, since a cache HIT has no octree to fail).
            if (bakeOk) {
                // Baked-Perf M7 Task 7.5: per-body occupancy stats -- occupied bricks /
                // bounding-volume bricks (bpa^3), and voxel fill ratio (occupied voxels /
                // (brickCount*512)) within allocated bricks. This is the data M8 needs to
                // decide, per-body, whether a dense 3D texture (which only wins where fill
                // ratio is near 1.0) is worth it -- a genuinely sparse body loses both
                // inter-brick sparseness and the sparse-mip hierarchy to a dense texture.
                // Reads ONLY `cat` (bricksPerAxis via cat.configs[k], occupied counts via
                // cat.brickCounts[k]/cat.occupiedVoxelCounts[k]) so it works identically on a
                // cache HIT (no live octree objects exist in that path) and a MISS.
                // Log line format: [CornellDiag] mirrors this demo's existing diagnostic
                // block naming convention (instIdx map, OOB counter) rather than inventing
                // a new tag.
                {
                    static const char* kBodyNames[8] = {
                        "leftWall", "rightWall", "backWall", "floor", "ceiling",
                        "light", "sphereObj", "boxObj",
                    };
                    std::ostringstream occJson;
                    occJson << "{\n";
                    for (size_t k = 0; k < 8; ++k) {
                        const uint64_t bpa = cat.configs[k].bricksPerAxis;
                        const uint64_t boundingVolumeBricks = bpa * bpa * bpa;
                        const uint64_t occupiedBricks = cat.brickCounts[k];
                        const uint64_t occupiedVoxels = cat.occupiedVoxelCounts[k];
                        const uint64_t voxelsInOccupiedBricks =
                            occupiedBricks * Vixen::SVO::SerializedOctree::kVoxelsPerBrick;
                        const double brickOccupancyRatio = boundingVolumeBricks > 0
                            ? static_cast<double>(occupiedBricks) / static_cast<double>(boundingVolumeBricks) : 0.0;
                        const double voxelFillRatio = voxelsInOccupiedBricks > 0
                            ? static_cast<double>(occupiedVoxels) / static_cast<double>(voxelsInOccupiedBricks) : 0.0;
                        mainLogger->Info("[CornellDiag] occupancy body=" + std::string(kBodyNames[k]) +
                                          " occupiedBricks=" + std::to_string(occupiedBricks) +
                                          " boundingVolumeBricks=" + std::to_string(boundingVolumeBricks) +
                                          " brickOccupancyRatio=" + std::to_string(brickOccupancyRatio) +
                                          " occupiedVoxels=" + std::to_string(occupiedVoxels) +
                                          " voxelFillRatio=" + std::to_string(voxelFillRatio));
                        occJson << "  \"" << kBodyNames[k] << "\": {"
                                << "\"occupiedBricks\": " << occupiedBricks << ", "
                                << "\"boundingVolumeBricks\": " << boundingVolumeBricks << ", "
                                << "\"brickOccupancyRatio\": " << brickOccupancyRatio << ", "
                                << "\"occupiedVoxels\": " << occupiedVoxels << ", "
                                << "\"voxelFillRatio\": " << voxelFillRatio << "}"
                                << (k + 1 < 8 ? ",\n" : "\n");
                    }
                    occJson << "}\n";
                    // Env-var-driven output path (VIXEN_OCCUPANCY_JSON), matching the existing
                    // VIXEN_PERF_CSV/VIXEN_HUD_CAPTURE_DIR convention (PerfCsvWriter.cpp) rather
                    // than a cwd-relative literal -- the app's cwd at launch (VIXEN\, per every
                    // temp_bench\*.bat's own `cd /d %VIXEN_ROOT%\VIXEN`) is NOT the worktree root
                    // temp_bench\ lives under, so a bare "temp_bench/occupancy.json" silently
                    // misses (caught live: this file did not exist after the first bake run).
                    // Falls back to the historical relative path if the env var is unset, so a
                    // bare manual launch from the repo root still gets a file, just not
                    // necessarily where the bench scripts expect it.
                    const char* occPathEnv = std::getenv("VIXEN_OCCUPANCY_JSON");
                    const std::string occPath = occPathEnv ? occPathEnv : "temp_bench/occupancy.json";
                    std::ofstream occFile(occPath);
                    if (occFile) {
                        occFile << occJson.str();
                    } else {
                        mainLogger->Warning("[BuildRenderGraph] VIXEN_DDGI_CORNELL_BAKED_DEMO: "
                                              "could not write occupancy JSON to " + occPath);
                    }
                }

                // M11.2: emissionIntensity defaults to 0.0 (every non-emissive body) and rides
                // in recipeParams[3] -- STORED instances never touch recipeParams (PROVIDER_STORED
                // is never read by TraceWorld.glsl's recipeParams accesses, all gated on
                // PROVIDER_PROCEDURAL), so the slot is genuinely free. See TraceWorld.glsl's
                // WorldHit.emission field for the shader-side read.
                auto makeInstance = [&](uint32_t octreeIdx, glm::vec3 color, glm::vec3 worldPos,
                                        float renderScale, float emissionIntensity = 0.0f) {
                    Vixen::SVO::BodyInstanceGpu inst{};
                    Vixen::SVO::SetInstanceTranslationScale(inst, glm::vec3(worldPos.x, worldPos.y, worldPos.z), renderScale);
                    inst.material.color[0] = color.x; inst.material.color[1] = color.y; inst.material.color[2] = color.z;
                    inst.material.octreeIndex = octreeIdx;
                    inst.material.providerKind = 0u;  // PROVIDER_STORED
                    inst.material.recipeId = 0u;
                    inst.material.recipeParams[3] = emissionIntensity;
                    return inst;
                };
                const float kWallRenderScale  = bodyRenderScale(kWallN, kWallSubdiv);
                const float kSmallRenderScale = bodyRenderScale(kSmallN, kSmallSubdiv);
                std::vector<Vixen::SVO::BodyInstanceGpu> instances = {
                    makeInstance(0u, bodies[0].color, bodyWorldPos(bodies[0].worldCenter, kWallN, kWallSubdiv),  kWallRenderScale),   // leftWall
                    makeInstance(1u, bodies[1].color, bodyWorldPos(bodies[1].worldCenter, kWallN, kWallSubdiv),  kWallRenderScale),   // rightWall
                    makeInstance(2u, bodies[2].color, bodyWorldPos(bodies[2].worldCenter, kWallN, kWallSubdiv),  kWallRenderScale),   // backWall
                    makeInstance(3u, bodies[3].color, bodyWorldPos(bodies[3].worldCenter, kWallN, kWallSubdiv),  kWallRenderScale),   // floor
                    makeInstance(4u, bodies[4].color, bodyWorldPos(bodies[4].worldCenter, kWallN, kWallSubdiv),  kWallRenderScale),   // ceiling
                    makeInstance(5u, bodies[5].color, kLightWorldPos,                                            kLightRenderScale, kLightEmissionIntensity),  // light
                    makeInstance(6u, bodies[6].color, bodyWorldPos(bodies[6].worldCenter, kSmallN, kSmallSubdiv), kSmallRenderScale), // sphereObj
                    makeInstance(7u, bodies[7].color, bodyWorldPos(bodies[7].worldCenter, kSmallN, kSmallSubdiv), kSmallRenderScale), // boxObj
                };

                if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                    bodyScene->SetRecipePool(std::move(cat));
                    // Force EAGER brick residency so the primary visible hit brick-marches the
                    // real trilinear SDF surface (handleLeafHitInstancedSdf) instead of the coarse
                    // mip-fallback (shadeFromMipSample) -- root cause of the "fragmented Cornell box"
                    // this demo was reported broken with (combined-verify round, 2026-07-15).
                    //
                    // WHY this is required (two stacked causes, both found this round):
                    //  (1) Every body is mip-capable, so DeriveResidencyDefault boots the scene LAZY
                    //      (brickResident=0). Under LAZY the primary hit resolves via mip-fallback at
                    //      coarse leaf-NODE granularity -- grey base color, flat up-normal, leaf-entry
                    //      t. A coarse-grid HitRecord readback showed this renders geometrically
                    //      INCOHERENT hits: wrong body per screen region, worldPos.z never below ~18.8
                    //      (rays never reach the interior/back wall), no coherent wall planes.
                    //  (2) The prior M3-round-4 "RequestBrickResidency ruled out" note was wrong for a
                    //      subtle reason: VulkanGraphApplication::UpdateBodySceneResidency() runs a
                    //      per-FRAME frustum/resolvability heuristic (tuned for the orbit-scale
                    //      tiered-ESVO demos) that calls RequestBrickResidency(anyInstanceWantsBricks)
                    //      every tick and STOMPED this grant back to false for this fixed close box
                    //      (its walls fail the pixel-resolvability threshold) -- so the earlier trial's
                    //      residency never actually stuck. That heuristic now early-returns for this
                    //      demo (see UpdateBodySceneResidency), leaving this call the exclusive driver,
                    //      exactly like VIXEN_TIER_ZOOM_DEMO's own scripted residency ownership.
                    bodyScene->RequestBrickResidency(true);
                    bodyScene->SetInstances(std::move(instances));
                    mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_BAKED_DEMO: seeded 8 body "
                                      "instances (5 walls + 1 ceiling light + 2 objects)");
                }
            }
            // Force-enable the DDGI probe grid for this demo -- ProbeGridConfigNode.cpp's own
            // MakeDefaultProbeGridConfig defaults probeGridEnabled=0 and only reads
            // VIXEN_PROBE_GRID_CONFIG_ENABLED independently later in the SAME process (its own
            // SetupImpl, not called from here) -- setting it here means this demo's whole point
            // (visually inspect DDGI GI quality) works from ONE env var, without the operator
            // needing to separately remember VIXEN_PROBE_GRID_CONFIG_ENABLED=1 (an easy miss that
            // would silently render the box with probeGridEnabled=0 -- no bounce lighting at all,
            // a false-negative-looking "it doesn't work" report). Mirrors
            // VIXEN_DDGI_LEAK_GATE_DEMO's own reliance on that lever (that gate's own comment,
            // this file ~line 3016, documents the SAME dependency but relies on the operator
            // setting it manually since that gate is a numeric/CPU-readback proof, not a live
            // visual demo). _putenv_s (not setenv) for MSVC CRT portability.
#if defined(_WIN32)
            _putenv_s("VIXEN_PROBE_GRID_CONFIG_ENABLED", "1");
#else
            setenv("VIXEN_PROBE_GRID_CONFIG_ENABLED", "1", 1);
#endif
            mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_BAKED_DEMO: force-enabled "
                              "VIXEN_PROBE_GRID_CONFIG_ENABLED=1 (probe grid must be on for this "
                              "demo's own point -- visible bounce lighting -- to be visible at all)");
        } else if (envFlagEnabled("VIXEN_DDGI_CORNELL_VIRTUAL_DEMO")) {
            // Sampled Lighting — Cornell Box GI Reference Scene, M2 (virtual/zero-bake variant).
            // Plan: Vixen-Docs/01-Architecture/Sampled-Lighting-Cornell-Box-Demo-Plan-2026-07.md
            //
            // SAME geometry/color/camera/probe-grid numbers as M1's baked variant (ONE shared
            // source, application/main/include/graph/CornellBoxSceneDefinition.h) -- "ideally
            // visually identical" enforced by construction. Content-representation backend is
            // the ONLY thing that differs: every one of the 8 bodies here renders through the
            // zero-bake GPU-direct path (RegisterProceduralRecipe -> PROVIDER_PROCEDURAL
            // BodyInstanceGpu, the same live mechanism VIXEN_PROCEDURAL_UBER_DEMO above already
            // proves works end-to-end, extended here to a hand-authored multi-body scene instead
            // of a generated stress corpus) -- ZERO BakeRecipeInstructionsToSdfWorld/
            // BuildSdfBodyOctree calls for any of them: static inspection of this whole block
            // confirms neither function is ever called for the 8-body registration loop below
            // (only RegisterProceduralRecipe), and g_cornellVirtualLightTreeSideBakeCount (this
            // file's top-level namespace, mirrors test_baked_vs_virtual_parity.cpp's own
            // g_bakeCallCount call-counter technique) proves at runtime that the ONLY bake call
            // anywhere in this block is the light-tree's own explicitly-scoped side bake below.
            //
            // M1's own recipe-VM investigation answers (load-bearing here, see M1's block above
            // for the full reasoning):
            //   (a) Box primitive: RoundedBox (data[0..2]=halfExtents, data[3]=rounding,
            //       data[4..6]=position) -- same primitive M1 used for the baked variant, now
            //       registered live via RecipeRegistry instead of baked directly.
            //   (b) Emission: the recipe-VM/RecipeEntry has NO emission opcode or material-tag
            //       field (confirmed again here: BodyInstanceGpu carries only worldPos/
            //       renderScale/color/octreeIndex/providerKind/recipeId/recipeParams -- no
            //       emission field anywhere, ShellOctreeGpu.h:349-357). More fundamentally,
            //       BuildLightTreeCut (LightTree.h) structurally REQUIRES an already-baked
            //       Octree+SerializedOctree+MipPool with a SEM_EMISSION voxel channel -- the
            //       light-tree mechanism that feeds DDGI's bounce-lighting gather is inherently
            //       baked-content-only, independent of how the body's own VISIBLE geometry is
            //       represented. This is a real architectural boundary, not a gap this milestone
            //       can route around: the light-tree cut is consumed by ProbeUpdate.comp as a
            //       flat world-space SSBO (LightTreeBufferNode::SetLightTreeCut), decoupled from
            //       whichever provider (Stored/Procedural) renders the light body's own pixels.
            //       Resolution: the light BODY's VISIBLE geometry still renders through the
            //       zero-bake procedural path (a real registered recipe, PROVIDER_PROCEDURAL,
            //       zero bake calls, same as every other body here) -- but the light-TREE CUT
            //       that feeds DDGI's gather is computed from a small, explicitly-scoped side
            //       bake of ONLY the light body's own geometry+emission (mirrors M1's
            //       BakeRecipeInstructionsToSdfWorldWithEmission call almost exactly, since this
            //       IS the same mechanism M1 already used for its own light-tree cut -- the only
            //       difference is this bake's result is discarded after the light-tree cut is
            //       derived, never fed into SetRecipePool/rendered). This bake is NOT counted
            //       against the zero-bake proof below (the proof's own scope is "the 8 bodies'
            //       RENDERED representation," matching the plan's own "never-baked proof for the
            //       virtual variant" gate wording) -- it is called out explicitly in both the
            //       log line and the Progress Log so this distinction is never silently assumed.
            using namespace Vixen::App::CornellBox;
            mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_VIRTUAL_DEMO: building the Cornell "
                              "box GI reference scene (virtual/zero-bake variant)");

            using Vixen::SVO::Recipe::SdfInstruction;

            // M3 round 2 (2026-07-14): geometry now comes from BuildCornellWorldSpaceBodies()
            // (this file's anonymous namespace, above) -- the SAME world-space SdfInstruction
            // list the baked variant's bake path also consumes (via a grid->world eval adapter),
            // instead of this block independently re-authoring its own worldBoxAt/worldSphereAt
            // calls from the shared CONSTANTS. See BuildCornellWorldSpaceBodies's own header
            // comment for the full "why unify" reasoning. The virtual/Procedural path's field
            // function already samples WORLD p directly (recipeId>=2 convention,
            // VIXEN_PROCEDURAL_UBER_DEMO's own "unused: field samples world p directly" comment),
            // so these world-space-authored instructions splice in completely unchanged -- no
            // adapter needed on this side, unlike the baked path.
            std::vector<CornellWorldSpaceBody> worldBodies = BuildCornellWorldSpaceBodies();

            // Recipe id allocation: 2..9 (0/1 reserved for the legacy analytic RECIPE_SPHERE/
            // RECIPE_DISPLACED_SPHERE path, same convention VIXEN_PROCEDURAL_UBER_DEMO's own
            // "recipeId = 2 + i" comment documents). Bound sphere authored explicitly for every
            // entry (same reasoning VIXEN_PROCEDURAL_UBER_DEMO's own comment gives: every
            // program here samples WORLD p directly via a position-carrying primitive, so
            // DeriveConservativeBounds' derivation would be redundant with an authored bound
            // anyway -- authoring it directly also sidesteps ever depending on derivation
            // succeeding for a program shape that might change later). worldBodies[]'s own
            // boundRadius field (computed in BuildCornellWorldSpaceBodies, shared with the
            // baked path's own bake-band sizing) is reused directly here rather than
            // re-derived.
            std::vector<Vixen::SVO::BodyInstanceGpu> virtualBodies;
            virtualBodies.reserve(worldBodies.size());
            bool allRegistered = true;
            for (size_t i = 0; i < worldBodies.size(); ++i) {
                const CornellWorldSpaceBody& b = worldBodies[i];
                const uint32_t recipeId = static_cast<uint32_t>(2 + i);

                Vixen::SVO::RecipeRegistry::RecipeEntry entry{};
                entry.bytecode = b.prog;
                entry.boundCenter = b.worldCenter;
                entry.boundRadius = b.boundRadius;

                auto regResult = RegisterProceduralRecipe(recipeId, entry);
                if (regResult != Vixen::SVO::RecipeRegistry::RegisterResult::Ok) {
                    mainLogger->Error(std::string("[BuildRenderGraph] VIXEN_DDGI_CORNELL_VIRTUAL_DEMO: "
                                     "RegisterProceduralRecipe(") + b.name + ") failed, code " +
                                     std::to_string(static_cast<int>(regResult)));
                    allRegistered = false;
                    continue;
                }

                Vixen::SVO::BodyInstanceGpu inst{};
                Vixen::SVO::SetInstanceTranslationScale(inst, glm::vec3(0.0f, 0.0f, 0.0f), 1.0f);
                inst.material.color[0] = b.color.x; inst.material.color[1] = b.color.y; inst.material.color[2] = b.color.z;
                inst.material.octreeIndex = 0u;    // unused by Procedural
                inst.material.providerKind = 1u;   // PROVIDER_PROCEDURAL
                inst.material.recipeId = recipeId;
                // M11.2: recipeParams[3..5] are unconditionally zero-initialized above and
                // never written elsewhere in this loop (every body here "samples world p
                // directly", per this block's own header comment) -- genuinely spare, so the
                // light body's emission intensity rides in recipeParams[3] rather than a new
                // BodyInstance field. Every other body stays 0.0 (non-emissive).
                if (std::string(b.name) == "light") {
                    inst.material.recipeParams[3] = kLightEmissionIntensity;
                }
                virtualBodies.push_back(inst);
            }

            if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                bodyScene->SetInstances(std::move(virtualBodies));
                mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_VIRTUAL_DEMO: seeded " +
                                  std::to_string(worldBodies.size()) + " zero-bake procedural body "
                                  "instances (5 walls + 1 ceiling light + 2 objects), allRegistered=" +
                                  (allRegistered ? std::string("true") : std::string("false")));
            }

            // Light-tree cut: a SEPARATE, explicitly-scoped side bake of ONLY the light body's
            // geometry+emission (see this block's own header comment on why this is required and
            // why it is not counted against the zero-bake proof). Mirrors M1's own
            // BakeRecipeInstructionsToSdfWorldWithEmission call almost verbatim -- same recipe
            // shape (a single RoundedBox), same emission lambda, same light-tree-cut pipeline --
            // the ONLY difference is this bake's SdfBodyOctree is used solely to derive the
            // light-tree cut and is never fed to SetRecipePool (the light body's own visible
            // pixels come entirely from the procedural recipe registered above, recipeId=7).
            {
                constexpr int kLightBakeN = 16;
                constexpr float kBand = 2.0f;
                const glm::vec3 kLightBakeCenter(static_cast<float>(kLightBakeN) * 0.5f);
                std::vector<SdfInstruction> lightLocalProg = {
                    CornellWorldBoxAt(kLightBakeCenter, kLightHalfExtent, 0.05f)  // authored at grid-local center, not world center
                };
                ++g_cornellVirtualLightTreeSideBakeCount;
                Vixen::SVO::SdfBakeResult lightBaked = Vixen::SVO::BakeRecipeInstructionsToSdfWorldWithEmission(
                    lightLocalProg.data(), static_cast<uint32_t>(lightLocalProg.size()), kLightBakeCenter,
                    kLightBakeN, kBand,
                    [](const glm::vec3&) { return kLightEmissionIntensity; });
                Vixen::SVO::SdfBodyOctree lightBody = Vixen::SVO::BuildSdfBodyOctree(lightBaked, 3);

                const Vixen::SVO::Octree* lightOct = lightBody.octree->getOctree();
                if (lightOct == nullptr) {
                    mainLogger->Error("[BuildRenderGraph] VIXEN_DDGI_CORNELL_VIRTUAL_DEMO: light-tree "
                                       "side-bake octree is null -- no bounce lighting from the ceiling light");
                } else {
                    Vixen::SVO::SerializedOctree lightSer = Vixen::SVO::SerializeSdf(lightBody);
                    Vixen::SVO::BakeAndAttachMipPool(*lightOct, lightSer);
                    Vixen::SVO::MipPool lightMipPool = Vixen::SVO::BakeMipPool(*lightOct, lightSer);

                    Vixen::SVO::LightTreeCutParams cutParams;
                    cutParams.powerThreshold = 0.001f;  // same fine-cut rationale as M1's baked variant
                    std::vector<Vixen::SVO::LightTreeNode> cut =
                        Vixen::SVO::BuildLightTreeCut(*lightOct, lightSer, lightMipPool, kLightBakeN, cutParams);

                    // Grid->world transform: this side bake's grid is centered on kLightBakeCenter
                    // (grid-local), and the recipe itself is authored at that SAME grid-local
                    // center (worldBoxAt(kLightBakeCenter, ...) above) -- so grid (kLightBakeN/2,
                    // kLightBakeN/2, kLightBakeN/2) maps to world kLightCenter. worldPos/
                    // renderScale below encode exactly that mapping, mirroring M1's own
                    // kLightWorldPos/kLightRenderScale derivation (bodyWorldPos/bodyRenderScale)
                    // but specialized to this side bake's kSmallSubdiv=1, kSmallN=kLightBakeN case.
                    constexpr float kWorldGridSize = 10.0f;  // ShellOctreeGpu.h's fixed octree-local->world span
                    const float lightRenderScale = static_cast<float>(kLightBakeN) / kWorldGridSize;
                    const glm::vec3 lightWorldPos = kLightCenter - glm::vec3(static_cast<float>(kLightBakeN) * 0.5f);

                    std::vector<Vixen::SVO::LightTreeNode> worldCut;
                    worldCut.reserve(cut.size());
                    for (const auto& node : cut) {
                        Vixen::SVO::LightTreeNode w = node;
                        w.worldPos = lightWorldPos + (node.worldPos / static_cast<float>(kLightBakeN)) * kWorldGridSize * lightRenderScale;
                        w.worldExtent = (node.worldExtent / static_cast<float>(kLightBakeN)) * kWorldGridSize * lightRenderScale;
                        worldCut.push_back(w);
                    }

                    if (auto* lightTreeInst = static_cast<LightTreeBufferNode*>(renderGraph->GetInstance(lightTreeBufferNode))) {
                        lightTreeInst->SetLightTreeCut(worldCut);
                    }
                    mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_VIRTUAL_DEMO: light-tree "
                                      "side-bake cut=" + std::to_string(cut.size()) + " nodes (side bake "
                                      "count=" + std::to_string(g_cornellVirtualLightTreeSideBakeCount) +
                                      ", NOT counted against the zero-bake render-path proof)");
                }
            }

            // Force-enable the DDGI probe grid -- same reasoning as M1's baked variant above.
#if defined(_WIN32)
            _putenv_s("VIXEN_PROBE_GRID_CONFIG_ENABLED", "1");
#else
            setenv("VIXEN_PROBE_GRID_CONFIG_ENABLED", "1", 1);
#endif
            mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_VIRTUAL_DEMO: force-enabled "
                              "VIXEN_PROBE_GRID_CONFIG_ENABLED=1 (probe grid must be on for this "
                              "demo's own point -- visible bounce lighting -- to be visible at all)");
        } else if (envFlagEnabled("VIXEN_DDGI_CORNELL_HYBRID_DEMO")) {
            // Sampled Lighting — Cornell Box GI Reference Scene, M6b Task 6b.1 (hybrid
            // provider variant, "hole in the wall"). Plan: Baked-Perf-Fix-Pipeline-Plan-2026-07.md
            // Milestone M6b.
            //
            // Structurally this is M2's virtual/zero-bake variant (7 bodies PROVIDER_PROCEDURAL,
            // zero bake calls) with exactly ONE body -- rightWall -- flipped to PROVIDER_STORED,
            // whose bake is a MODIFIED shape (the rightWall RoundedBox minus a Cylinder, so light
            // can pass through a visible hole). This needs zero new engine machinery: the CSG
            // Subtract opcode (SdfOpCode::Subtract) and BakeSdfWorld (the same bake path M1's
            // baked variant already uses per-body) already support an arbitrary instruction
            // program -- this block just authors a 2-instruction program (RoundedBox, Cylinder,
            // Subtract) instead of M1's 1-instruction (RoundedBox only) program for that one body.
            //
            // Geometry/color/camera SAME shared source as every other Cornell variant
            // (CornellBoxSceneDefinition.h via BuildCornellWorldSpaceBodies()) so the hole is the
            // ONLY visual difference from the virtual/baked baselines.
            using namespace Vixen::App::CornellBox;
            using Vixen::SVO::Recipe::SdfInstruction;
            mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_HYBRID_DEMO: building the Cornell "
                              "box GI reference scene (hybrid variant: rightWall PROVIDER_STORED with "
                              "a baked hole, other 7 bodies PROVIDER_PROCEDURAL)");

            std::vector<CornellWorldSpaceBody> worldBodies = BuildCornellWorldSpaceBodies();
            // worldBodies[]: leftWall(0), rightWall(1), backWall(2), floor(3), ceiling(4), light(5),
            // sphereObj(6), boxObj(7) -- fixed order, see BuildCornellWorldSpaceBodies.
            constexpr size_t kHoleWallIdx = 1;  // rightWall

            // --- rightWall: baked, MODIFIED shape (wall minus a through-hole cylinder) ---
            //
            // SdfOpCode::Cylinder (SdfRecipeEval.h / generated/SdfCoreKernels.g.hpp
            // SdfCore_Cylinder) is NOT a position/axis-carrying primitive like RoundedBox/Sphere --
            // it evaluates directly against the current `pos` with its bore axis HARD-FIXED to
            // local Y (SdfCore_Cylinder: radial test on p.x/p.z, height test on p.y) and has no
            // data[4..6] center field at all. To place and orient it, wrap it in a
            // SdfOpCode::Transform/RestorePos domain-transform pair (the same push/pop position-
            // stack mechanism Twist/Bend/RepeatInfinite use -- SdfRecipeEval.h's Transform case):
            // Transform pushes `pos` into the cylinder's local frame via
            // pos' = invRot*(pos-translation)*invScale (SdfCore_Transform), the Cylinder opcode
            // then evaluates in that local frame, and RestorePos pops back to world space
            // afterward for the following Subtract to combine correctly with the wall's own
            // world-space RoundedBox distance.
            //
            // rightWall is thin on X (CornellWorldBoxAt(kRightWallWorldCenter, vec3(kWallThickness,
            // kWallSpan, kZWideHalfExtent), ...) -- BuildCornellWorldSpaceBodies), so the hole must
            // bore through world X to be a see-through hole rather than a shallow dimple. Since the
            // cylinder's own bore axis is fixed to LOCAL Y, invRot must rotate world X onto local Y:
            // a +90 degree rotation about Z maps (1,0,0) -> (0,1,0) exactly (verified numerically:
            // quaternion (qv=(0,0,sin45),qw=cos45) applied to SdfCore_Transform's rotation formula
            // sends world X to local Y with zero residual on X/Z). translation = the wall's own
            // world center (Transform's data[0..2]), so the cylinder is centered on the wall.
            const float kHoleRadius = 3.0f;                    // world units -- clearly visible, well inside the wall's span
            const float kHoleHalfLen = kWallThickness * 2.0f;  // bores fully through the wall's thin (X) axis with margin
            const glm::vec3& kHoleWallCenter = worldBodies[kHoleWallIdx].worldCenter;

            SdfInstruction transformIn{};
            transformIn.opCode = static_cast<uint8_t>(SdfOpCode::Transform);
            transformIn.data[0] = kHoleWallCenter.x;  // translation (Data0.xyz)
            transformIn.data[1] = kHoleWallCenter.y;
            transformIn.data[2] = kHoleWallCenter.z;
            // invRot xyzw: +90deg about Z, world X -> local Y (see derivation above)
            transformIn.data[4] = 0.0f;
            transformIn.data[5] = 0.0f;
            transformIn.data[6] = 0.70710678f;   // sin(45deg)
            transformIn.data[7] = 0.70710678f;   // cos(45deg)
            transformIn.data[8]  = 1.0f;  // invScale (no scaling)
            transformIn.data[9]  = 1.0f;
            transformIn.data[10] = 1.0f;
            transformIn.data[11] = 1.0f;  // distScale (rotation/translation only -- no distance rescale)

            SdfInstruction holeCylinder{};
            holeCylinder.opCode = static_cast<uint8_t>(SdfOpCode::Cylinder);
            holeCylinder.data[0] = kHoleHalfLen;  // halfHeight (bore axis, local Y)
            holeCylinder.data[1] = kHoleRadius;   // radius

            SdfInstruction restorePosOp{};
            restorePosOp.opCode = static_cast<uint8_t>(SdfOpCode::RestorePos);

            SdfInstruction subtractOp{};
            subtractOp.opCode = static_cast<uint8_t>(SdfOpCode::Subtract);

            std::vector<SdfInstruction> holedWallProg = worldBodies[kHoleWallIdx].prog;  // [RoundedBox]
            holedWallProg.push_back(transformIn);
            holedWallProg.push_back(holeCylinder);
            holedWallProg.push_back(restorePosOp);
            holedWallProg.push_back(subtractOp);
            // stack after: RoundedBox(wall, world space), Cylinder(hole, transformed into world
            // space by RestorePos) -> Subtract -> wall minus hole.

            constexpr float kBand = 2.0f;
            constexpr float kWorldGridSize = 10.0f;  // ShellOctreeGpu.h's fixed octree-local->world span
            constexpr int kWallSubdiv = 4;
            constexpr int kWallN = 128;  // power of two -- see M1 baked variant's own pow2 note

            auto bodyWorldPos = [](glm::vec3 bodyWorldCenter, int n, int subdiv) {
                return bodyWorldCenter - glm::vec3(static_cast<float>(n) / (2.0f * static_cast<float>(subdiv)));
            };
            auto bodyRenderScale = [](int n, int subdiv) {
                return static_cast<float>(n) / (static_cast<float>(subdiv) * kWorldGridSize);
            };
            auto makeWorldSpaceEval = [](const std::vector<SdfInstruction>& prog, glm::vec3 bodyWorldCenter, int n, int subdiv) {
                return [&prog, bodyWorldCenter, n, subdiv](const glm::vec3& pRaw) {
                    const glm::vec3 world = bodyWorldCenter + (pRaw - glm::vec3(static_cast<float>(n) * 0.5f)) / static_cast<float>(subdiv);
                    return Vixen::SVO::Recipe::evalRecipe(prog.data(), static_cast<uint32_t>(prog.size()), world) * static_cast<float>(subdiv);
                };
            };

            Vixen::SVO::SdfBakeResult holedWallBaked = Vixen::SVO::BakeSdfWorld(
                makeWorldSpaceEval(holedWallProg, worldBodies[kHoleWallIdx].worldCenter, kWallN, kWallSubdiv),
                worldBodies[kHoleWallIdx].worldCenter, kWallN, kBand, 3, Vixen::SVO::NoEmission,
                [](const glm::vec3&) { return glm::vec3(1.0f); });
            Vixen::SVO::SdfBodyOctree holedWallBody = Vixen::SVO::BuildSdfBodyOctree(holedWallBaked, 3);

            const Vixen::SVO::Octree* holedWallOct = holedWallBody.octree->getOctree();
            if (holedWallOct == nullptr) {
                mainLogger->Error("[BuildRenderGraph] VIXEN_DDGI_CORNELL_HYBRID_DEMO: holed rightWall "
                                   "octree is null -- scene not built");
            } else {
                std::vector<const Vixen::SVO::SdfBodyOctree*> octreesForCat = { &holedWallBody };
                Vixen::SVO::ConcatenatedOctrees cat = Vixen::SVO::ConcatenateSdfWithMips(octreesForCat);

                if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                    bodyScene->SetRecipePool(std::move(cat));
                    bodyScene->RequestBrickResidency(true);  // same eager-residency requirement as M1's baked variant

                    // --- Assemble all 8 instances: rightWall STORED (octreeIndex 0, the only
                    // entry in cat), the other 7 PROCEDURAL ---
                    std::vector<Vixen::SVO::BodyInstanceGpu> instances;
                    instances.reserve(worldBodies.size());
                    bool allRegistered = true;
                    for (size_t i = 0; i < worldBodies.size(); ++i) {
                        const CornellWorldSpaceBody& b = worldBodies[i];
                        Vixen::SVO::BodyInstanceGpu inst{};
                        inst.material.color[0] = b.color.x; inst.material.color[1] = b.color.y; inst.material.color[2] = b.color.z;

                        if (i == kHoleWallIdx) {
                            const glm::vec3 wp = bodyWorldPos(b.worldCenter, kWallN, kWallSubdiv);
                            Vixen::SVO::SetInstanceTranslationScale(inst, glm::vec3(wp.x, wp.y, wp.z), bodyRenderScale(kWallN, kWallSubdiv));
                            inst.material.octreeIndex = 0u;
                            inst.material.providerKind = 0u;  // PROVIDER_STORED
                            inst.material.recipeId = 0u;
                        } else {
                            const uint32_t recipeId = static_cast<uint32_t>(2 + i);  // 2..9, mirrors M2's own convention
                            Vixen::SVO::RecipeRegistry::RecipeEntry entry{};
                            entry.bytecode = b.prog;
                            entry.boundCenter = b.worldCenter;
                            entry.boundRadius = b.boundRadius;
                            auto regResult = RegisterProceduralRecipe(recipeId, entry);
                            if (regResult != Vixen::SVO::RecipeRegistry::RegisterResult::Ok) {
                                mainLogger->Error(std::string("[BuildRenderGraph] VIXEN_DDGI_CORNELL_HYBRID_DEMO: "
                                                 "RegisterProceduralRecipe(") + b.name + ") failed, code " +
                                                 std::to_string(static_cast<int>(regResult)));
                                allRegistered = false;
                            }
                            Vixen::SVO::SetInstanceTranslationScale(inst, glm::vec3(0.0f, 0.0f, 0.0f), 1.0f);
                            inst.material.octreeIndex = 0u;    // unused by Procedural
                            inst.material.providerKind = 1u;   // PROVIDER_PROCEDURAL
                            inst.material.recipeId = recipeId;
                        }
                        instances.push_back(inst);
                    }
                    bodyScene->SetInstances(std::move(instances));
                    mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_HYBRID_DEMO: seeded 8 body "
                                      "instances (rightWall PROVIDER_STORED w/ hole, 7 PROVIDER_PROCEDURAL), "
                                      "allRegistered=" + (allRegistered ? std::string("true") : std::string("false")));
                }
            }

            // Light-tree cut: SAME side-bake mechanism as M2's virtual variant (light-tree cut is
            // structurally baked-content-only regardless of how bodies render -- see that block's
            // own header comment). The light BODY's visible pixels render via PROVIDER_PROCEDURAL
            // above like every other non-holed body; this bake feeds ONLY the DDGI light-tree cut.
            {
                constexpr int kLightBakeN = 16;
                const glm::vec3 kLightBakeCenter(static_cast<float>(kLightBakeN) * 0.5f);
                std::vector<SdfInstruction> lightLocalProg = {
                    CornellWorldBoxAt(kLightBakeCenter, kLightHalfExtent, 0.05f)
                };
                Vixen::SVO::SdfBakeResult lightBaked = Vixen::SVO::BakeRecipeInstructionsToSdfWorldWithEmission(
                    lightLocalProg.data(), static_cast<uint32_t>(lightLocalProg.size()), kLightBakeCenter,
                    kLightBakeN, kBand,
                    [](const glm::vec3&) { return kLightEmissionIntensity; });
                Vixen::SVO::SdfBodyOctree lightBody = Vixen::SVO::BuildSdfBodyOctree(lightBaked, 3);

                const Vixen::SVO::Octree* lightOct = lightBody.octree->getOctree();
                if (lightOct == nullptr) {
                    mainLogger->Error("[BuildRenderGraph] VIXEN_DDGI_CORNELL_HYBRID_DEMO: light-tree "
                                       "side-bake octree is null -- no bounce lighting from the ceiling light");
                } else {
                    Vixen::SVO::SerializedOctree lightSer = Vixen::SVO::SerializeSdf(lightBody);
                    Vixen::SVO::BakeAndAttachMipPool(*lightOct, lightSer);
                    Vixen::SVO::MipPool lightMipPool = Vixen::SVO::BakeMipPool(*lightOct, lightSer);

                    Vixen::SVO::LightTreeCutParams cutParams;
                    cutParams.powerThreshold = 0.001f;
                    std::vector<Vixen::SVO::LightTreeNode> cut =
                        Vixen::SVO::BuildLightTreeCut(*lightOct, lightSer, lightMipPool, kLightBakeN, cutParams);

                    const float lightRenderScale = static_cast<float>(kLightBakeN) / kWorldGridSize;
                    const glm::vec3 lightWorldPos = kLightCenter - glm::vec3(static_cast<float>(kLightBakeN) * 0.5f);

                    std::vector<Vixen::SVO::LightTreeNode> worldCut;
                    worldCut.reserve(cut.size());
                    for (const auto& node : cut) {
                        Vixen::SVO::LightTreeNode w = node;
                        w.worldPos = lightWorldPos + (node.worldPos / static_cast<float>(kLightBakeN)) * kWorldGridSize * lightRenderScale;
                        w.worldExtent = (node.worldExtent / static_cast<float>(kLightBakeN)) * kWorldGridSize * lightRenderScale;
                        worldCut.push_back(w);
                    }

                    if (auto* lightTreeInst = static_cast<LightTreeBufferNode*>(renderGraph->GetInstance(lightTreeBufferNode))) {
                        lightTreeInst->SetLightTreeCut(worldCut);
                    }
                    mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_HYBRID_DEMO: light-tree "
                                      "side-bake cut=" + std::to_string(cut.size()) + " nodes");
                }
            }

#if defined(_WIN32)
            _putenv_s("VIXEN_PROBE_GRID_CONFIG_ENABLED", "1");
#else
            setenv("VIXEN_PROBE_GRID_CONFIG_ENABLED", "1", 1);
#endif
            mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_HYBRID_DEMO: force-enabled "
                              "VIXEN_PROBE_GRID_CONFIG_ENABLED=1");
        } else if (envFlagEnabled("VIXEN_DDGI_CORNELL_MIXED_DEMO")) {
            // Sampled Lighting — Cornell Box GI Reference Scene, M6b Task 6b.2 (mixed-provider
            // split). Plan: Baked-Perf-Fix-Pipeline-Plan-2026-07.md Milestone M6b.
            //
            // Two configs, selected by the env var's VALUE:
            //   VIXEN_DDGI_CORNELL_MIXED_DEMO=walls_stored   -- 5 walls PROVIDER_STORED (baked,
            //     UNMODIFIED shape, unlike 6b.1's hole), light+sphereObj+boxObj PROVIDER_PROCEDURAL.
            //   VIXEN_DDGI_CORNELL_MIXED_DEMO=objects_stored (or any other/empty value) -- the
            //     INVERSE: light+sphereObj+boxObj PROVIDER_STORED, 5 walls PROVIDER_PROCEDURAL.
            //
            // Mechanically this is 6b.1's hybrid pattern generalized to an arbitrary index SET
            // instead of a single hard-coded body: bake every body in the STORED set (mirroring
            // M1's bakeWorldSpaceBody/light-emission bake), register every body in the
            // PROCEDURAL set (mirroring M2's RegisterProceduralRecipe loop), concatenate only the
            // STORED subset via ConcatenateSdfWithMips (never all 8 -- an empty STORED set would
            // pass an empty vector, which ConcatenateSdfWithMips accepts). The light-tree cut is
            // ALWAYS derived from its own explicitly-scoped side bake regardless of which set the
            // light body's VISIBLE geometry is in (same "light-tree cut is baked-content-only"
            // architectural boundary M2's own header comment establishes) -- so, unusually, the
            // walls_stored config's light body appears in BOTH bake and procedural registration
            // for different purposes (register for pixels, side-bake for the DDGI cut), matching
            // M2's own light handling exactly.
            using namespace Vixen::App::CornellBox;
            using Vixen::SVO::Recipe::SdfInstruction;
            const char* mixedMode = std::getenv("VIXEN_DDGI_CORNELL_MIXED_DEMO");
            const bool wallsStored = (std::string(mixedMode) == "walls_stored");
            mainLogger->Info(std::string("[BuildRenderGraph] VIXEN_DDGI_CORNELL_MIXED_DEMO: building the "
                              "Cornell box GI reference scene (mixed-provider variant: ") +
                              (wallsStored ? "walls STORED, objects+light PROCEDURAL)" :
                                             "objects+light STORED, walls PROCEDURAL)"));

            std::vector<CornellWorldSpaceBody> worldBodies = BuildCornellWorldSpaceBodies();
            // worldBodies[]: leftWall(0), rightWall(1), backWall(2), floor(3), ceiling(4), light(5),
            // sphereObj(6), boxObj(7) -- fixed order, see BuildCornellWorldSpaceBodies.
            const std::vector<size_t> wallIdx    = {0, 1, 2, 3, 4};
            const std::vector<size_t> objectIdx  = {5, 6, 7};  // light, sphereObj, boxObj
            const std::vector<size_t>& storedIdx     = wallsStored ? wallIdx   : objectIdx;
            const std::vector<size_t>& proceduralIdx = wallsStored ? objectIdx : wallIdx;

            constexpr float kBand = 2.0f;
            constexpr float kWorldGridSize = 10.0f;  // ShellOctreeGpu.h's fixed octree-local->world span
            constexpr int kWallSubdiv = 4;
            constexpr int kWallN = 128;   // power of two -- see M1 baked variant's own pow2 note
            constexpr int kSmallN = 32;
            constexpr int kSmallSubdiv = 1;

            auto bodyWorldPos = [](glm::vec3 bodyWorldCenter, int n, int subdiv) {
                return bodyWorldCenter - glm::vec3(static_cast<float>(n) / (2.0f * static_cast<float>(subdiv)));
            };
            auto bodyRenderScale = [](int n, int subdiv) {
                return static_cast<float>(n) / (static_cast<float>(subdiv) * kWorldGridSize);
            };
            auto makeWorldSpaceEval = [](const std::vector<SdfInstruction>& prog, glm::vec3 bodyWorldCenter, int n, int subdiv) {
                return [&prog, bodyWorldCenter, n, subdiv](const glm::vec3& pRaw) {
                    const glm::vec3 world = bodyWorldCenter + (pRaw - glm::vec3(static_cast<float>(n) * 0.5f)) / static_cast<float>(subdiv);
                    return Vixen::SVO::Recipe::evalRecipe(prog.data(), static_cast<uint32_t>(prog.size()), world) * static_cast<float>(subdiv);
                };
            };
            // n/subdiv per body: walls need kWallN/kWallSubdiv (thin-slab resolution), light/
            // sphere/box need kSmallN/kSmallSubdiv (cube-ish bodies) -- same split M1's baked
            // variant uses, generalized to whichever bodies land in the STORED set here.
            auto gridParamsFor = [&](size_t i) {
                return (i <= 4) ? std::make_pair(kWallN, kWallSubdiv) : std::make_pair(kSmallN, kSmallSubdiv);
            };

            std::vector<Vixen::SVO::SdfBodyOctree> storedOctrees;  // owns storage for octreesForCat below
            storedOctrees.reserve(storedIdx.size());
            std::vector<uint32_t> storedOctreeIndexOf(worldBodies.size(), 0xFFFFFFFFu);  // body idx -> octreesForCat slot
            for (size_t i : storedIdx) {
                const CornellWorldSpaceBody& b = worldBodies[i];
                auto [n, subdiv] = gridParamsFor(i);
                Vixen::SVO::SdfBakeResult baked = Vixen::SVO::BakeSdfWorld(
                    makeWorldSpaceEval(b.prog, b.worldCenter, n, subdiv), b.worldCenter, n, kBand, 3,
                    Vixen::SVO::NoEmission, [](const glm::vec3&) { return glm::vec3(1.0f); });
                storedOctreeIndexOf[i] = static_cast<uint32_t>(storedOctrees.size());
                storedOctrees.push_back(Vixen::SVO::BuildSdfBodyOctree(baked, 3));
            }

            std::vector<const Vixen::SVO::SdfBodyOctree*> octreesForCat;
            octreesForCat.reserve(storedOctrees.size());
            for (const auto& oct : storedOctrees) octreesForCat.push_back(&oct);
            Vixen::SVO::ConcatenatedOctrees cat = Vixen::SVO::ConcatenateSdfWithMips(octreesForCat);

            if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                bodyScene->SetRecipePool(std::move(cat));
                bodyScene->RequestBrickResidency(true);  // same eager-residency requirement as M1/6b.1

                std::vector<Vixen::SVO::BodyInstanceGpu> instances;
                instances.reserve(worldBodies.size());
                bool allRegistered = true;
                std::vector<bool> isStored(worldBodies.size(), false);
                for (size_t i : storedIdx) isStored[i] = true;

                for (size_t i = 0; i < worldBodies.size(); ++i) {
                    const CornellWorldSpaceBody& b = worldBodies[i];
                    Vixen::SVO::BodyInstanceGpu inst{};
                    inst.material.color[0] = b.color.x; inst.material.color[1] = b.color.y; inst.material.color[2] = b.color.z;

                    if (isStored[i]) {
                        auto [n, subdiv] = gridParamsFor(i);
                        const glm::vec3 wp = bodyWorldPos(b.worldCenter, n, subdiv);
                        Vixen::SVO::SetInstanceTranslationScale(inst, glm::vec3(wp.x, wp.y, wp.z), bodyRenderScale(n, subdiv));
                        inst.material.octreeIndex = storedOctreeIndexOf[i];
                        inst.material.providerKind = 0u;  // PROVIDER_STORED
                        inst.material.recipeId = 0u;
                    } else {
                        const uint32_t recipeId = static_cast<uint32_t>(2 + i);  // 2..9, mirrors M2's own convention
                        Vixen::SVO::RecipeRegistry::RecipeEntry entry{};
                        entry.bytecode = b.prog;
                        entry.boundCenter = b.worldCenter;
                        entry.boundRadius = b.boundRadius;
                        auto regResult = RegisterProceduralRecipe(recipeId, entry);
                        if (regResult != Vixen::SVO::RecipeRegistry::RegisterResult::Ok) {
                            mainLogger->Error(std::string("[BuildRenderGraph] VIXEN_DDGI_CORNELL_MIXED_DEMO: "
                                             "RegisterProceduralRecipe(") + b.name + ") failed, code " +
                                             std::to_string(static_cast<int>(regResult)));
                            allRegistered = false;
                        }
                        Vixen::SVO::SetInstanceTranslationScale(inst, glm::vec3(0.0f, 0.0f, 0.0f), 1.0f);
                        inst.material.octreeIndex = 0u;    // unused by Procedural
                        inst.material.providerKind = 1u;   // PROVIDER_PROCEDURAL
                        inst.material.recipeId = recipeId;
                    }
                    instances.push_back(inst);
                }
                bodyScene->SetInstances(std::move(instances));
                mainLogger->Info(std::string("[BuildRenderGraph] VIXEN_DDGI_CORNELL_MIXED_DEMO: seeded 8 body "
                                  "instances (") + std::to_string(storedIdx.size()) + " PROVIDER_STORED, " +
                                  std::to_string(proceduralIdx.size()) + " PROVIDER_PROCEDURAL), allRegistered=" +
                                  (allRegistered ? std::string("true") : std::string("false")));
            }

            // Light-tree cut: SAME side-bake mechanism as 6b.1/M2 regardless of which set the
            // light body's visible geometry is in -- structurally baked-content-only (see M2's
            // own header comment).
            {
                constexpr int kLightBakeN = 16;
                const glm::vec3 kLightBakeCenter(static_cast<float>(kLightBakeN) * 0.5f);
                std::vector<SdfInstruction> lightLocalProg = {
                    CornellWorldBoxAt(kLightBakeCenter, kLightHalfExtent, 0.05f)
                };
                Vixen::SVO::SdfBakeResult lightBaked = Vixen::SVO::BakeRecipeInstructionsToSdfWorldWithEmission(
                    lightLocalProg.data(), static_cast<uint32_t>(lightLocalProg.size()), kLightBakeCenter,
                    kLightBakeN, kBand,
                    [](const glm::vec3&) { return kLightEmissionIntensity; });
                Vixen::SVO::SdfBodyOctree lightBody = Vixen::SVO::BuildSdfBodyOctree(lightBaked, 3);

                const Vixen::SVO::Octree* lightOct = lightBody.octree->getOctree();
                if (lightOct == nullptr) {
                    mainLogger->Error("[BuildRenderGraph] VIXEN_DDGI_CORNELL_MIXED_DEMO: light-tree "
                                       "side-bake octree is null -- no bounce lighting from the ceiling light");
                } else {
                    Vixen::SVO::SerializedOctree lightSer = Vixen::SVO::SerializeSdf(lightBody);
                    Vixen::SVO::BakeAndAttachMipPool(*lightOct, lightSer);
                    Vixen::SVO::MipPool lightMipPool = Vixen::SVO::BakeMipPool(*lightOct, lightSer);

                    Vixen::SVO::LightTreeCutParams cutParams;
                    cutParams.powerThreshold = 0.001f;
                    std::vector<Vixen::SVO::LightTreeNode> cut =
                        Vixen::SVO::BuildLightTreeCut(*lightOct, lightSer, lightMipPool, kLightBakeN, cutParams);

                    const float lightRenderScale = static_cast<float>(kLightBakeN) / kWorldGridSize;
                    const glm::vec3 lightWorldPos = kLightCenter - glm::vec3(static_cast<float>(kLightBakeN) * 0.5f);

                    std::vector<Vixen::SVO::LightTreeNode> worldCut;
                    worldCut.reserve(cut.size());
                    for (const auto& node : cut) {
                        Vixen::SVO::LightTreeNode w = node;
                        w.worldPos = lightWorldPos + (node.worldPos / static_cast<float>(kLightBakeN)) * kWorldGridSize * lightRenderScale;
                        w.worldExtent = (node.worldExtent / static_cast<float>(kLightBakeN)) * kWorldGridSize * lightRenderScale;
                        worldCut.push_back(w);
                    }

                    if (auto* lightTreeInst = static_cast<LightTreeBufferNode*>(renderGraph->GetInstance(lightTreeBufferNode))) {
                        lightTreeInst->SetLightTreeCut(worldCut);
                    }
                    mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_MIXED_DEMO: light-tree "
                                      "side-bake cut=" + std::to_string(cut.size()) + " nodes");
                }
            }

#if defined(_WIN32)
            _putenv_s("VIXEN_PROBE_GRID_CONFIG_ENABLED", "1");
#else
            setenv("VIXEN_PROBE_GRID_CONFIG_ENABLED", "1", 1);
#endif
            mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_CORNELL_MIXED_DEMO: force-enabled "
                              "VIXEN_PROBE_GRID_CONFIG_ENABLED=1");
        } else if (envFlagEnabled("VIXEN_DDGI_LEAK_GATE_DEMO") || envFlagEnabled("VIXEN_DDGI_EDIT_LOOP_DEMO")) {
            // Sampled Lighting Inc4 M6 reuses this EXACT scene (geometry, probe placement,
            // near/far indices) for the edit-loop responsiveness gate when
            // VIXEN_DDGI_EDIT_LOOP_DEMO=1 -- same "don't invent a new mechanism" discipline
            // M4's own gate used relative to VIXEN_RESTIR_GATE_DEMO. The only behavioral
            // difference (isEditLoopMode below): the light-tree cut is built EMPTY at scene-
            // construction time (the source starts "off") and the REAL cut is stashed via
            // g_ddgiEditLoopWorldCut for VulkanGraphApplication.cpp's readback hook to flip in
            // live at a chosen tick -- a genuine mid-run scene-content edit, not a restart.
            const bool isEditLoopMode = envFlagEnabled("VIXEN_DDGI_EDIT_LOOP_DEMO");

            // Sampled Lighting Inc4 M4 live gate: the leak-test scene the plan's Task 4
            // requires -- thin-wall occluder geometry between an emissive source and a
            // probe/observation point, proving the Chebyshev-tested gather does NOT leak
            // light through the wall while an ablation (Chebyshev test disabled) DOES.
            // Mirrors VIXEN_RESTIR_GATE_DEMO's own shape (emissive body -> light-tree cut
            // -> world-transform -> stash for the CPU readback hook) but ALSO bakes a
            // second, non-emissive body (the thin wall) and configures ProbeGridConfig +
            // the DDGILeakGateDebug SSBO so ProbeUpdate.comp's M4 self-test gather has a
            // concrete near/far probe pair + shading point to exercise.
            //
            // Geometry (world space) -- MUST live inside ProbeGridConfigNode's own default
            // grid coverage: origin (0,0,0), spacing 4, count 8x8x8 -> world [0,32) on every
            // axis (ProbeGridConfigNode has no live setter this milestone, see below). A first
            // attempt at this scene placed the bodies at the default camera's (64,64,64)
            // framing (VIXEN_RESTIR_GATE_DEMO's own convention) -- a live-gate run caught that
            // this put the near/far probes OUTSIDE the grid's [0,32) coverage entirely
            // (silently producing a probeIndex >= probeCount workgroup no-op, both readbacks
            // reading zero with no way to tell "correctly rejected" from "never ran" -- a false
            // negative-control result). Rescaled below (kRenderScale=1.0, not 4.8) so both
            // bodies AND the near/far probes/shading point land inside [0,32):
            //   emissive source: a small emissive sphere at grid (10,16,16) -> world X~=5.1.
            //   thin wall: a box occluder at grid (22,16,16) -> world X~=8.9, spanning world
            //     X~=[8.56,9.19] (thin -- 1 grid-unit half-extent -> ~0.31 world units either
            //     side), full Y/Z span (a real wall, not a small block seen around).
            //   near probe: grid (1,4,4) -> world (4,16,16) -- before the source (world X~=5.1),
            //     no occluder between it and the source.
            //   far probe / shading point: grid (3,4,4) -> world (12,16,16) -- past the wall's
            //     far face (world X~=9.19), occluded from the source. The gather samples the
            //     NEAR probe's atlas entry AT this far shading point -- Chebyshev-tested
            //     visibility must reject it (the near probe's own stored ray-hit distance
            //     toward the wall is much closer than the near-probe-to-far-point test
            //     distance), while the ablation (test forced to 1) must NOT reject it, proving
            //     the scene leaks without the mechanism.
            mainLogger->Info(std::string("[BuildRenderGraph] ") +
                              (isEditLoopMode ? "VIXEN_DDGI_EDIT_LOOP_DEMO: building the M6 "
                                                "edit-loop scene (source starts OFF)"
                                              : "VIXEN_DDGI_LEAK_GATE_DEMO: building the M4 "
                                                "thin-wall leak-test gate scene"));

            constexpr int   kN    = 32;
            constexpr float kBand = 2.0f;
            const glm::vec3 kSourceCenter(10.0f, 16.0f, 16.0f);   // grid-space center (kN=32 grid), world-mapped below
            constexpr float kSourceRadius = 8.0f;  // sized comparably to VIXEN_RESTIR_GATE_DEMO's own proven r=10/n=32 scene

            Vixen::SVO::RecipeParams sourceParams{kSourceRadius, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
            Vixen::SVO::SdfBakeResult sourceBaked = Vixen::SVO::BakeRecipeToSdfWorldWithEmission(
                Vixen::SVO::RECIPE_SPHERE, kSourceCenter, sourceParams, kN, kBand,
                [](const glm::vec3&) { return 4.0f; });  // uniform emissive intensity
            Vixen::SVO::SdfBodyOctree sourceBody = Vixen::SVO::BuildSdfBodyOctree(sourceBaked, 3);

            // Thin wall: a genuine box SDF (no RECIPE_BOX exists in SdfRecipes.h -- BakeSdfWorld's
            // templated `eval` accepts ANY lambda, so a box distance function is authored directly
            // here rather than approximating one out of RECIPE_SPHERE/RECIPE_DISPLACED_SPHERE).
            // Grid-space half-extents: thin along X (2 grid units -> genuinely thin at this bake's
            // world scale below), full-span along Y/Z (a real wall, not a small block the source
            // could simply be seen around).
            const glm::vec3 kWallCenter(22.0f, 16.0f, 16.0f);
            const glm::vec3 kWallHalfExtent(1.0f, 15.0f, 15.0f);
            auto wallSdf = [&](const glm::vec3& p) {
                glm::vec3 q = glm::abs(p - kWallCenter) - kWallHalfExtent;
                glm::vec3 qPos = glm::max(q, glm::vec3(0.0f));
                float outside = glm::length(qPos);
                float inside = std::min(std::max(q.x, std::max(q.y, q.z)), 0.0f);
                return outside + inside;
            };
            // SDF Bake Box-Tight Region M2: this body was investigated as this plan's own
            // second named validation case, but a box-tight bakeRegion computed honestly for
            // its ACTUAL geometry is a no-op here -- NOT applied, deliberately, not an
            // oversight. M1's bakeRegion is an ORIGIN-ANCHORED [0,region) window (SdfBake.h);
            // it can only trim occupied bricks off the HIGH end of a grid axis. This wall's
            // thin axis (X, half-extent 1.0 + kBand 2.0 = 3.0) is occupied at grid X~[19,25)
            // because kWallCenter.x=22 sits near the HIGH end of the kN=32 grid, not centered
            // at n/2=16 -- so the occupied range's own high edge (25, rounded to the brick
            // boundary at 32) already equals the FULL grid width; there is no low-side region
            // to trim away. The other two axes (Y/Z, half-extent 15) are occupied almost the
            // entire [0,32) span by design ("a real wall, not a small block", see this block's
            // own header comment) -- also no room to shrink. Recentering kWallCenter to n/2
            // would change this body's WORLD position (both bodies here share ONE grid->world
            // transform, sourceWorldPos/kRenderScale below -- there is no per-body compensating
            // offset the way Cornell's bodyWorldPos/bodyRenderScale gives each wall its own
            // placement), which would desync the near/far probe indices this scene's geometry
            // was carefully hand-tuned against (see the header comment above) -- out of this
            // milestone's scope. Verified by computing bakeRegion per axis for this body's real
            // half-extents: all three axes round up to the full kN=32, i.e. zero brick
            // reduction, confirmed by hand and left unapplied rather than landing dead plumbing
            // with no measured effect. Cornell's walls (bakeWorldSpaceBody above) DO benefit --
            // their bodies are each individually world-placed (bodyWorldPos/bodyRenderScale per
            // body), so recentering the bake grid on each wall's own true center was already the
            // existing convention, not a new risk.
            Vixen::SVO::SdfBakeResult wallBaked = Vixen::SVO::BakeSdfWorld(wallSdf, kWallCenter, kN, kBand, 3);
            Vixen::SVO::SdfBodyOctree wallBody = Vixen::SVO::BuildSdfBodyOctree(wallBaked, 3);

            const Vixen::SVO::Octree* sourceOct = sourceBody.octree->getOctree();
            if (sourceOct != nullptr) {
                Vixen::SVO::SerializedOctree sourceSer = Vixen::SVO::SerializeSdf(sourceBody);
                Vixen::SVO::BakeAndAttachMipPool(*sourceOct, sourceSer);
                Vixen::SVO::MipPool sourceMipPool = Vixen::SVO::BakeMipPool(*sourceOct, sourceSer);

                Vixen::SVO::LightTreeCutParams cutParams;
                cutParams.powerThreshold = 0.001f;  // fine cut -- same rationale as VIXEN_RESTIR_GATE_DEMO
                std::vector<Vixen::SVO::LightTreeNode> cut =
                    Vixen::SVO::BuildLightTreeCut(*sourceOct, sourceSer, sourceMipPool, kN, cutParams);
                for (size_t di = 0; di < cut.size() && di < 5; ++di) {
                    const auto& n = cut[di];
                    mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_LEAK_GATE_DEMO: DIAG rawGridCutNode[" + std::to_string(di) +
                                      "] gridPos=(" + std::to_string(n.worldPos.x) + "," + std::to_string(n.worldPos.y) + "," + std::to_string(n.worldPos.z) +
                                      ") gridExtent=" + std::to_string(n.worldExtent));
                }

                // Body-placement/world-transform: SAME shape as VIXEN_RESTIR_GATE_DEMO's own
                // gate scene (renderScale + world-grid-size + a fixed worldPos origin), but
                // renderScale=1.0 (NOT 4.8) -- the whole point of this rescale is keeping both
                // bodies AND the near/far probes inside ProbeGridConfigNode's default [0,32)
                // grid coverage (see this block's own header comment on why the first attempt's
                // renderScale=4.8/world-center placement put the probes outside the grid
                // entirely). World diameter = kWorldGridSize*kRenderScale = 10; sourceWorldPos
                // chosen so grid (0,0,0) maps to world (2,8,8) -- keeps the whole 10-unit body
                // span + the near/far probes' Y/Z=16 comfortably inside [0,32) on every axis.
                constexpr float kRenderScale = 1.0f;
                constexpr float kWorldGridSize = 10.0f;
                const glm::vec3 sourceWorldPos(2.0f, 8.0f, 8.0f);

                std::vector<Vixen::SVO::LightTreeNode> worldCut;
                worldCut.reserve(cut.size());
                for (const auto& node : cut) {
                    Vixen::SVO::LightTreeNode w = node;
                    const glm::vec3 pBase = (node.worldPos / static_cast<float>(kN)) * kWorldGridSize;
                    w.worldPos = pBase * kRenderScale + sourceWorldPos;
                    w.worldExtent = (node.worldExtent / static_cast<float>(kN)) * kWorldGridSize * kRenderScale;
                    worldCut.push_back(w);
                }

                if (auto* lightTreeInst = static_cast<LightTreeBufferNode*>(renderGraph->GetInstance(lightTreeBufferNode))) {
                    // Edit-loop mode: start with an EMPTY cut (source "off" -- zero light-tree
                    // content for the probe-update pass to sample) and stash the REAL cut for
                    // VulkanGraphApplication.cpp's readback hook to flip in live at a chosen
                    // tick. Leak-gate mode (default): apply the real cut immediately, unchanged
                    // from M4.
                    if (isEditLoopMode) {
                        lightTreeInst->SetLightTreeCut({});
                        extern std::vector<Vixen::SVO::LightTreeNode>* g_ddgiEditLoopWorldCut;
                        static std::vector<Vixen::SVO::LightTreeNode> editLoopCutStash = worldCut;
                        g_ddgiEditLoopWorldCut = &editLoopCutStash;
                    } else {
                        lightTreeInst->SetLightTreeCut(worldCut);
                    }
                }
                mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_LEAK_GATE_DEMO: cut=" + std::to_string(cut.size()) + " nodes");
                for (size_t di = 0; di < worldCut.size() && di < 5; ++di) {
                    const auto& n = worldCut[di];
                    mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_LEAK_GATE_DEMO: DIAG cutNode[" + std::to_string(di) +
                                      "] worldPos=(" + std::to_string(n.worldPos.x) + "," + std::to_string(n.worldPos.y) + "," + std::to_string(n.worldPos.z) +
                                      ") worldExtent=" + std::to_string(n.worldExtent) +
                                      " intensity=" + std::to_string(n.intensity) + " coverage=" + std::to_string(n.coverage));
                }

                // Instance placement: source body + wall body, both mapped via the SAME
                // grid->world transform (kRenderScale/kWorldGridSize/sourceWorldPos above) so
                // both bodies' grid-space centers/extents land at consistent world positions
                // relative to each other -- source at grid (10,16,16) -> world X~=5.1, wall at
                // grid (22,16,16) -> world X~=8.9 (see this block's own header comment for the
                // full derivation). Both bodies share ONE octree pool slot each (octreeIndex
                // 0/1) via
                // ConcatenateSdfWithMips below, same multi-body pattern VIXEN_TIER_OBSERVABLE_DEMO
                // uses (BAKE separate octrees, concatenate, place with distinct BodyInstanceGpu
                // entries).
                std::vector<const Vixen::SVO::SdfBodyOctree*> octreesForCat = {&sourceBody, &wallBody};
                Vixen::SVO::ConcatenatedOctrees cat = Vixen::SVO::ConcatenateSdfWithMips(octreesForCat);

                if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                    Vixen::SVO::BodyInstanceGpu sourceInst{};
                    Vixen::SVO::SetInstanceTranslationScale(sourceInst, sourceWorldPos, kRenderScale);
                    sourceInst.material.color[0] = 1.0f;
                    sourceInst.material.color[1] = 1.0f;
                    sourceInst.material.color[2] = 1.0f;
                    sourceInst.material.octreeIndex = 0u;

                    Vixen::SVO::BodyInstanceGpu wallInst{};
                    Vixen::SVO::SetInstanceTranslationScale(wallInst, sourceWorldPos, kRenderScale);
                    wallInst.material.color[0] = 0.7f;
                    wallInst.material.color[1] = 0.7f;
                    wallInst.material.color[2] = 0.7f;
                    wallInst.material.octreeIndex = 1u;

                    bodyScene->SetRecipePool(std::move(cat));
                    bodyScene->SetInstances({sourceInst, wallInst});
                    mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_LEAK_GATE_DEMO: seeded emissive-source + thin-wall body instances");
                }

                // ProbeGridConfig: ProbeGridConfigNode has no live setter this milestone
                // (MakeDefaultProbeGridConfig is a file-local default, flipped on via
                // VIXEN_PROBE_GRID_CONFIG_ENABLED=1 -- see that file) -- the gate reuses that
                // SAME default grid (origin (0,0,0), spacing 4, count 8x8x8, world coverage
                // [0,32) on every axis -- MUST match this block's own geometry, hence the
                // kRenderScale=1.0/sourceWorldPos=(2,8,8) rescale above) and picks near/far
                // probe INDICES that land at valid, in-range grid nodes near the source/wall/
                // far-point positions, avoiding a new live setter for a one-off gate (mirrors
                // VIXEN_RESTIR_GATE_DEMO's own "reuse existing plumbing, don't add a new
                // mechanism" discipline). World position of probe (px,py,pz) = (px*4,py*4,pz*4).
                // Near probe at grid (2,3,3) -> world (8,12,12): distance ~3.2 from the source's
                // own world center/radius (5.125,13,13)/2.5 -- outside the source's own solid
                // geometry (a probe placed INSIDE the emissive sphere's solid volume gave a
                // degenerate all-zero TraceWorld result, a live-gate finding from an earlier
                // pass with a smaller kSourceRadius=3 where the near probe sat inside the
                // sphere -- kSourceRadius was widened to 8 AND the near probe moved outside it),
                // and before the wall's near face (world X~=8.56), no occluder in the way. Far
                // probe/shading point at grid (3,3,3) -> world (12,12,12): past the wall's far
                // face (world X~=9.19), occluded from the source. Both indices are well inside
                // [0, countX*countY*countZ=512) -- an EARLIER live-gate bug this geometry
                // rescale also fixed was an out-of-range index silently no-op'ing the gather
                // workgroup (see this block's own header comment).
                constexpr uint32_t kNearProbeX = 2u, kFarProbeX = 3u, kProbeY = 3u, kProbeZ = 3u;
                constexpr uint32_t kDefaultCountX = 8u, kDefaultCountY = 8u;
                const uint32_t nearProbeIndex = kNearProbeX + kProbeY * kDefaultCountX + kProbeZ * kDefaultCountX * kDefaultCountY;
                const uint32_t farProbeIndex  = kFarProbeX  + kProbeY * kDefaultCountX + kProbeZ * kDefaultCountX * kDefaultCountY;
                const glm::vec3 farShadingPos(static_cast<float>(kFarProbeX) * 4.0f,
                                               static_cast<float>(kProbeY) * 4.0f,
                                               static_cast<float>(kProbeZ) * 4.0f);

                ddgiLeakGateNearProbeIndex_ = nearProbeIndex;
                ddgiLeakGateFarProbeIndex_  = farProbeIndex;
                ddgiLeakGateFarShadingPos_  = farShadingPos;
                mainLogger->Info("[BuildRenderGraph] VIXEN_DDGI_LEAK_GATE_DEMO: nearProbeIndex=" +
                                  std::to_string(nearProbeIndex) + " farProbeIndex=" + std::to_string(farProbeIndex) +
                                  " farShadingPos=(" + std::to_string(farShadingPos.x) + "," +
                                  std::to_string(farShadingPos.y) + "," + std::to_string(farShadingPos.z) + ")");
            } else {
                mainLogger->Error("[BuildRenderGraph] VIXEN_DDGI_LEAK_GATE_DEMO: source body octree is null -- gate scene not built");
            }
        } else if (envFlagEnabled("VIXEN_BRICKMAP_SCENE")) {
            // W-BRICKMAP Slice 2 round 3 RETARGET: the default (env-unset) fallback below is
            // Procedural (providerKind=1, no octree at all -- see its own comment), so an A/B
            // against it is a no-op by construction. The coarse-grid DDA backend now serves
            // FORMAT_STORED_SDF (round-3 root-cause finding: brickGridLookup/brickLookupBase
            // are only populated by the SDF serialization path, ShellOctreeGpu.h's
            // SerializeSdf/ConcatenateSdf -- a FORMAT_BINARY octree's brickLookup binding is a
            // 1-byte placeholder, so round 2's FORMAT_BINARY-only scope-down read it out of
            // bounds). BodyOctreeSceneNode::EnsureOctreesBuilt bakes 3 Stored-SDF octrees when
            // EITHER VIXEN_STORED_SDF_DEMO or VIXEN_BRICKMAP_SCENE is set (same bake, shared) --
            // this branch just points instances at them (providerKind=0/PROVIDER_STORED,
            // formatId==FORMAT_STORED_SDF), reusing VIXEN_STORED_SDF_DEMO's placeStored geometry
            // convention verbatim (kSdfN=64 grid, renderScale=0.75, same transform math).
            // Gated on its own scene env var (VIXEN_BRICKMAP_SCENE), separate from
            // the VIXEN_COMPOSED_TRAVERSAL A/B override -- an A/B run needs the SAME
            // scene on both composed and ESVO-forced runs, varying only traversal.
            // BATCH-18/POST-CLOSURE CORRECTION: kHalf below used to be 32*renderScale (a
            // "grid-[0,64] scales directly by renderScale" mental model) -- that is NOT what
            // the actual transform composes (BodyOctreeSceneNode::EnsureRtQueryTlasBuilt /
            // TraceWorld.glsl): world = worldPos + renderScale * (octreeConfig.localToWorld *
            // local[0,1]^3), and localToWorld is a FIXED scale(kWorldGridSize=10) unrelated to
            // the 64-grid resolution -- confirmed by the OTHER demo block in this same file
            // (VIXEN_DDGI_LEAK_GATE_DEMO, ~line 2825), which already uses the correct
            // `kHalf = 5.0f * kRenderScale` form (= 0.5*kWorldGridSize*renderScale) and whose
            // own comment derives it from an [RtTlasInst] corner-transform dump. The body's TRUE
            // world half-extent is renderScale*kWorldGridSize/2 = 0.75*10/2 = 3.75wu (full
            // extent 7.5wu), not 32*0.75=24wu. The old 24wu offset shifted every body ~20.25wu
            // off its intended center per axis (0.5*kWorldGridSize*(32/5 - 1) = 20.25) -- nearly
            // 3 body-diameters, well past "visible": VALUE FIXED, not just the comment. Example
            // (center (64,64,64)): old worldPos (40,40,40) -> new worldPos (60.25,60.25,60.25).
            // This moves every VIXEN_BRICKMAP_SCENE body -- the flag-off identity hash
            // (0bbf6e47..., batch 19) is now STALE; re-baseline required (see ledger).
            constexpr float kRenderScale = 0.75f;
            // Deep-field-mip-policy regime-3 divergence placement. The stored-SDF
            // brick is the mip-policy leaf here: kWorldGridSize/8*renderScale =
            // 0.9375 world units. At the smoke window's measured 500 px height,
            // the live RaySizeCoefNode formula gives coef=0.001570797 and bias=0,
            // so level 2 (leaf*4) begins at 2387.32 world units. Keep the default
            // scene untouched; the FAR gate adds a 10% margin and also moves the
            // existing kind-5 body behind the sparse shell for the composite leg.
            const bool sparseBodyEnabled = envFlagEnabled("VIXEN_SPARSE_BODY");
            const bool sparseBodyFarEnabled = sparseBodyEnabled &&
                                              envFlagEnabled("VIXEN_SPARSE_BODY_FAR");
            // Batch 47 / NEBULA: a second gate on top of VIXEN_SPARSE_BODY.
            // The historical near and FAR placements remain byte-identical
            // when this flag is absent.
            const bool sparseBodyNebulaEnabled = sparseBodyEnabled &&
                                                  envFlagEnabled("VIXEN_SPARSE_BODY_NEBULA");
            // Batch 50 / KI-047 residual: a THIRD independent gate on top of
            // VIXEN_SPARSE_BODY. Sol's V1.3 finding (sol-b49-validation.md) was
            // that the regime-3 composite blend genuinely EXECUTES at
            // residualT=0.015625 (BodyInstanceRayMarch.comp:255's gate passes),
            // but frames stayed byte-identical because TraceWorld.glsl's same-pass
            // second-nearest candidate (secondColor, :642/:672) is never populated
            // -- the 3 seeded near bodies (D~612/800/1200wu) sit on distinct,
            // non-overlapping camera rays. This flag adds a 7th body directly
            // BEHIND the existing D~612wu body (octreeIdx 3, world (200,64,-297))
            // on the SAME ray, so a ray through the near body's silhouette also
            // finds the far one as TraceWorld's real "lost isCloserHit but still a
            // hit" second candidate. Historical near/FAR/NEBULA placements are
            // untouched when this flag is unset (byte-identical, criterion C1).
            const bool sparseBodyOverlapEnabled = sparseBodyEnabled &&
                                                   envFlagEnabled("VIXEN_SPARSE_BODY_OVERLAP");
            constexpr float kSmokeFrameHeight = 500.0f;
            constexpr float kCameraFovDegrees = 45.0f;
            constexpr float kPi = 3.14159265358979323846f;
            const float sparseRaySizeCoef = 2.0f * std::tan(
                (kCameraFovDegrees * kPi / 180.0f / kSmokeFrameHeight) * 0.5f);
            const float sparseLevel2Footprint = (10.0f / 8.0f) * kRenderScale * 4.0f;
            const float sparseLevel2Distance = sparseLevel2Footprint / sparseRaySizeCoef;
            const float sparseFarDistance = sparseLevel2Distance * 1.10f;
            // NEBULA derivation (batch 47): the local bake remains 8^3 bricks,
            // so one brick is (10/8)*scale world units and a level-2 node spans
            // 4 bricks. With scale=128, level 2 starts at
            //   ((10/8)*128*4) / 0.001570797 = 407,436wu;
            // the 10% placement margin is 448,180wu. The same scale makes the
            // kind-6 shell diameter (52/64)*10*128 = 1,040wu, or only 1.48px
            // at that distance. Scaling the instance scales both the object and
            // its cells, so this ratio cannot improve; the measured bar outcome
            // is reported rather than hidden in scene authoring.
            constexpr float kSparseNebulaRenderScale = 128.0f;
            const float sparseNebulaLevel2Footprint = (10.0f / 8.0f) *
                                                       kSparseNebulaRenderScale * 4.0f;
            const float sparseNebulaLevel2Distance = sparseNebulaLevel2Footprint / sparseRaySizeCoef;
            const float sparseNebulaDistance = sparseNebulaLevel2Distance * 1.10f;
            const glm::vec3 sparseFarCamera(64.0f, 64.0f, 300.0f);
            const glm::vec3 sparseFarDirection = glm::normalize(glm::vec3(220.0f, 0.0f, -1170.0f));
            const glm::vec3 sparseFarCenter = sparseFarCamera + sparseFarDirection * sparseFarDistance;
            const glm::vec3 behindSparseFarCenter = sparseFarCamera + sparseFarDirection * (sparseFarDistance + 400.0f);
            const glm::vec3 sparseNebulaCenter = sparseFarCamera + sparseFarDirection * sparseNebulaDistance;
            // Keep the kind-5 sphere behind the nebula's ~520wu front shell
            // surface on the shared camera ray. The old FAR separation remains
            // +400wu; the scaled control uses +700wu.
            const glm::vec3 behindSparseNebulaCenter =
                sparseFarCamera + sparseFarDirection * (sparseNebulaDistance + 700.0f);
            const bool sparseBodyFarPlacementEnabled = sparseBodyFarEnabled || sparseBodyNebulaEnabled;
            // VIXEN_SPARSE_BODY_OVERLAP placement: same ray as the round-12 D~612wu
            // body (octreeIdx 3, world center (200,64,-297); camera at
            // (64,64,300) looking -Z per round-12's convention). Extending
            // 150wu further along that exact camera->near-body direction lands
            // the new body's CENTER on the near body's own ray, at D~762wu --
            // a genuine near/behind overlap pair, not a parallel/offset one.
            //
            // ⚠ FIRST ATTEMPT (both bodies at kRenderScale=0.75) measured a
            // byte-identical frame vs the non-overlap baseline: the far body's
            // angular radius (0.282deg at D~762wu) was SMALLER than the near
            // body's (0.351deg at D~612wu) and both are centered on the exact
            // same ray direction, so the far body's silhouette sat entirely
            // INSIDE the near body's -- every ray that could hit the far body
            // hit the near one first, and TraceWorld's "lost isCloserHit but
            // still a real hit" branch (the only thing that populates
            // secondColor) never fired for ANY pixel. Fix: give the BEHIND
            // body a larger renderScale (kOverlapBehindRenderScale=1.5, vs the
            // near body's 0.75) so its silhouette's angular radius
            // (~0.53deg) exceeds the near body's and visibly rims out around
            // it -- guaranteeing rays exist that miss the near body's disc but
            // hit the far body's, the genuine "second candidate" case.
            constexpr glm::vec3 kOverlapNearBodyCenter(200.0f, 64.0f, -297.0f);
            const glm::vec3 overlapRayDir =
                glm::normalize(kOverlapNearBodyCenter - sparseFarCamera);
            constexpr float kOverlapBehindExtraDistance = 150.0f;
            const glm::vec3 overlapBehindCenter =
                kOverlapNearBodyCenter + overlapRayDir * kOverlapBehindExtraDistance;
            constexpr float kOverlapBehindRenderScale = 1.5f;  // 2x the near body's 0.75

            auto placeStoredSdf = [&](float cx, float cy, float cz,
                                       float r, float g, float b,
                                       uint32_t octreeIdx,
                                       float renderScale) {
                Vixen::SVO::BodyInstanceGpu inst{};
                Vixen::SVO::SetInstanceTranslationScale(inst, glm::vec3(cx - 5.0f * renderScale, cy - 5.0f * renderScale, cz - 5.0f * renderScale), renderScale);
                inst.material.color[0]     = r;
                inst.material.color[1]     = g;
                inst.material.color[2]     = b;
                inst.material.octreeIndex  = octreeIdx;
                inst.material.providerKind = 0u;  // PROVIDER_STORED: octree path (formatId==FORMAT_STORED_SDF here)
                inst.material.recipeId     = 0u;
                return inst;
            };
            // Round 12 (far-field default-coef parity, plan ledger 2026-08-04
            // "ROUND 12"): the 3 near bodies above sit at D≈507-539wu camera
            // distance -- below the TRUE default-coef tier-crossing distance
            // (596.75wu, closed by arithmetic in Batch 11) -- so this scene could
            // never fire the far-field gate at default coef. Add a 4th body past
            // it. Camera geometry for this scene is FIXED (no PARAM_ORBIT_* is
            // set for VIXEN_BRICKMAP_SCENE): position (64,64,300), yaw=0/pitch=0,
            // which looks toward -Z (BuildRenderGraph.cpp's PRESET-1 comment) --
            // so "far" means SMALLER z, not larger (an earlier version of this
            // edit placed the body at z=+800, behind the camera and invisible;
            // confirmed by a zero-pixel-diff boot against the pre-edit 3-body
            // baseline). x=200,z=-297 -> forward dist 597wu, lateral offset
            // 136wu, D=612.3wu > 597wu crossing, and comfortably inside the
            // 45-deg-FOV cone at that depth (max lateral ~247wu, ~111wu margin)
            // and clear of the HUD band (x69-332/y286-312). octreeIdx=3 is
            // BodyOctreeSceneNode::EnsureOctreesBuilt's VIXEN_BRICKMAP_SCENE-only
            // 4th Stored-SDF bake (kind 3, plain sphere). Near bodies are
            // untouched -- in-frame internal control for the near-region parity leg.
            // ROUND-18 STEP 5 (multi-distance close): 2 more in-cone far bodies, WELL past
            // the 597wu tier-crossing, D~800/D~1200, margins >30% off the FOV cone edge
            // (half-FOV ~22.48deg derived from round-12's own 247wu@597wu figure). Camera
            // fixed at (64,64,300) looking -Z, so "far" = smaller z (round-12 convention).
            // D~800: fwd=780wu, lateral=150wu -> max_lateral=322.8wu, margin=53.5%, D=794.3wu.
            // D~1200: fwd=1170wu, lateral=220wu -> max_lateral=484.2wu, margin=54.6%, D=1190.5wu.
            // octreeIdx 4/5 are EnsureOctreesBuilt's round-18 kinds (plain sphere, same recipe).
            std::vector<Vixen::SVO::BodyInstanceGpu> brickmapBodies = {
                placeStoredSdf( 14.0f,  64.0f,   64.0f, 1.00f, 0.95f, 0.85f, 0u, kRenderScale),
                placeStoredSdf( 64.0f,  64.0f,   64.0f, 0.55f, 0.75f, 1.00f, 1u, kRenderScale),
                placeStoredSdf(114.0f,  64.0f,   64.0f, 0.85f, 0.90f, 1.00f, 2u, kRenderScale),
                placeStoredSdf(200.0f,  64.0f, -297.0f, 1.00f, 0.60f, 0.20f, 3u, kRenderScale),
                placeStoredSdf(214.0f,  64.0f, -480.0f, 0.90f, 0.40f, 0.90f, 4u, kRenderScale),
                placeStoredSdf(sparseBodyFarPlacementEnabled
                                   ? (sparseBodyNebulaEnabled ? behindSparseNebulaCenter.x : behindSparseFarCenter.x)
                                   : 284.0f,
                               64.0f,
                               sparseBodyFarPlacementEnabled
                                   ? (sparseBodyNebulaEnabled ? behindSparseNebulaCenter.z : behindSparseFarCenter.z)
                                   : -870.0f,
                               0.40f, 0.90f, 0.60f, 5u, kRenderScale),
            };
            // Deep-field-mip-policy regime-3 divergence scene (2026-08-08),
            // env-gated (VIXEN_SPARSE_BODY=1, default off -- the hard identity gate):
            // the kind-6 scattered shell is near at the historical D~675wu placement
            // unless VIXEN_SPARSE_BODY_FAR is also enabled. FAR moves it to the
            // derived level-2+ distance and moves kind 5 behind it on the same
            // in-cone ray, preserving actual sparse-shell-over-background layering.
            if (sparseBodyEnabled) {
                const glm::vec3 sparseCenter = sparseBodyNebulaEnabled
                    ? sparseNebulaCenter
                    : sparseBodyFarEnabled
                        ? sparseFarCenter
                        : glm::vec3(191.5f, 64.0f, -363.0f);
                brickmapBodies.push_back(
                    placeStoredSdf(sparseCenter.x, sparseCenter.y, sparseCenter.z,
                                   0.20f, 0.90f, 0.95f, 6u,
                                   sparseBodyNebulaEnabled ? kSparseNebulaRenderScale : kRenderScale));
            }
            // VIXEN_SPARSE_BODY_OVERLAP: a 7th body, BRIGHT (color at/near 1.0 --
            // quantization bar per sol-b49-validation.md V1.3: residualT=0.015625
            // needs behindColor >= ~0.251 to move even one 8-bit LSB; this uses
            // 1.0 -- flat white -- well clear of that bar), placed directly
            // behind the D~612wu body on the SAME camera ray (see
            // overlapBehindCenter derivation above). octreeIdx 7 is
            // EnsureOctreesBuilt's batch-50 kind (plain sphere, same recipe as
            // the other round-18/sparse-shell kinds).
            if (sparseBodyOverlapEnabled) {
                brickmapBodies.push_back(
                    placeStoredSdf(overlapBehindCenter.x, overlapBehindCenter.y, overlapBehindCenter.z,
                                   1.00f, 1.00f, 1.00f, 7u, kOverlapBehindRenderScale));
            }
            if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                bodyScene->SetInstances(std::move(brickmapBodies));
                mainLogger->Info("[BuildRenderGraph] VIXEN_BRICKMAP_SCENE: seeded " +
                                  std::to_string(bodyScene->GetInstances().size()) +
                                  " FORMAT_STORED_SDF body instances "
                                  "(round 12: +1 far body D~612wu; round 18: +2 more D~800/D~1200wu; "
                                  "VIXEN_SPARSE_BODY: +1 scattered-shell occluder; FAR=" +
                                  (sparseBodyNebulaEnabled ? "nebula-level2+" :
                                   sparseBodyFarEnabled ? "level2+" : "off") +
                                  "; OVERLAP=" + (sparseBodyOverlapEnabled ? "on" : "off") + ")");
            }
        } else if (envFlagEnabled("VIXEN_STORED_SDF_DEMO")) {
            // VIXEN_STORED_SDF_DEMO — Stored-SDF bodies (Increment 2, M5 Task 10).
            // EnsureOctreesBuilt has baked 3 SdfBodyOctrees (kinds 0/1/2) via ConcatenateSdf,
            // setting configs[k].formatId = STORED_SDF and populating the sdfBricks /
            // brickGridLookup buffers (bindings 11/12). Instances use providerKind=0 (STORED)
            // and octreeIndex=0/1/2 to select the per-kind OctreeConfig.
            //
            // Transform convention (binary-shell / marchStoredSdf AABB), BATCH-18/POST-CLOSURE
            // CORRECTED: the world span of the instance transform is renderScale *
            // octreeConfig.localToWorld * local[0,1]^3, and localToWorld is a FIXED
            // scale(kWorldGridSize=10) INDEPENDENT of the 64-grid resolution -- so kHalf is
            // 0.5*kWorldGridSize*renderScale = 5*renderScale, NOT 32*renderScale (that formula
            // wrongly assumed the [0,64] grid-voxel index scales directly by renderScale; see
            // the matching correction + derivation at the VIXEN_BRICKMAP_SCENE block above,
            // and the already-correct `5.0f * kRenderScale` reference form at the
            // VIXEN_DDGI_LEAK_GATE_DEMO block, ~line 2825).
            //   renderScale = 0.75       — the world half-extent of the [0,10] localToWorld span
            //   kHalf       = 5 * 0.75 = 3.75   (TRUE half-extent; full extent 7.5wu)
            //   worldPos    = center - 3.75
            //     → de-instance transform: instOrigin = (rayOrigin - worldPos) / renderScale
            //       maps a ray at world center to the octree's local center.
            //
            // Body centers in world space (same spread as the Procedural seed so the
            // default camera (X=64, Z=300, looking -Z) frames all three):
            //   left   center = (14, 64, 64)  → worldPos = (14-3.75, 64-3.75, 64-3.75) = (10.25, 60.25, 60.25)
            //   centre center = (64, 64, 64)  → worldPos = (64-3.75, 64-3.75, 64-3.75) = (60.25, 60.25, 60.25)
            //   right  center = (114,64, 64)  → worldPos = (114-3.75,64-3.75, 64-3.75) = (110.25,60.25, 60.25)
            // (previously, with the wrong kHalf=24: (-10,40,40) / (40,40,40) / (90,40,40) --
            // every body was ~20.25wu off its intended center per axis. VALUE FIXED.)
            constexpr float kRenderScale = 0.75f;
            constexpr float kHalf        = 5.0f * kRenderScale;  // = 3.75f -- TRUE half of the [0,10] localToWorld span

            auto placeStored = [&](float cx, float cy, float cz,
                                   float r, float g, float b,
                                   uint32_t octreeIdx) {
                Vixen::SVO::BodyInstanceGpu inst{};
                Vixen::SVO::SetInstanceTranslationScale(inst, glm::vec3(cx - kHalf, cy - kHalf, cz - kHalf), kRenderScale);
                inst.material.color[0]     = r;
                inst.material.color[1]     = g;
                inst.material.color[2]     = b;
                inst.material.octreeIndex  = octreeIdx;    // selects configs[k] (incl. formatId)
                inst.material.providerKind = 0u;           // PROVIDER_STORED: octree/Stored path
                inst.material.recipeId     = 0u;           // unused by Stored path
                return inst;
            };
            std::vector<Vixen::SVO::BodyInstanceGpu> storedBodies = {
                placeStored( 14.0f, 64.0f, 64.0f, 1.00f, 0.95f, 0.85f, 0u),  // left   — smooth sphere   (kind 0, red)
                placeStored( 64.0f, 64.0f, 64.0f, 0.55f, 0.75f, 1.00f, 1u),  // centre — displaced sphere (kind 1, green)
                placeStored(114.0f, 64.0f, 64.0f, 0.85f, 0.90f, 1.00f, 2u),  // right  — smooth sphere   (kind 2, white)
            };
            if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                bodyScene->SetInstances(std::move(storedBodies));
                mainLogger->Info("[BuildRenderGraph] VIXEN_STORED_SDF_DEMO: seeded 3 Stored-SDF body instances");
            }
        } else {
            // Default — Procedural SDF bodies (Increment 1): true smooth spheres, no octree.
            // worldPos = world centre; recipeParams = (radius, displaceAmp, displaceFreq).
            // Radius 24 matches the prior Stored shells' on-screen size (kHalf=24), so the
            // default camera frames all three. providerKind=1 selects the Procedural path.
            constexpr float kRadius = 24.0f;
            auto placeProcedural = [&](float cx, float cy, float cz,
                                       float r, float g, float b,
                                       uint32_t recipeId, float amp, float freq) {
                Vixen::SVO::BodyInstanceGpu inst{};
                Vixen::SVO::SetInstanceTranslationScale(inst, glm::vec3(cx, cy, cz), 1.0f);
                inst.material.color[0]    = r;
                inst.material.color[1]    = g;
                inst.material.color[2]    = b;
                inst.material.octreeIndex = 0u;              // unused by Procedural
                inst.material.providerKind = 1u;             // PROVIDER_PROCEDURAL
                inst.material.recipeId     = recipeId;       // 0 = sphere, 1 = displaced sphere
                inst.material.recipeParams[0] = kRadius;
                inst.material.recipeParams[1] = amp;
                inst.material.recipeParams[2] = freq;
                return inst;
            };
            std::vector<Vixen::SVO::BodyInstanceGpu> defaultBodies = {
                placeProcedural( 14.0f, 64.0f, 64.0f, 1.00f, 0.95f, 0.85f, 0u, 0.0f, 0.0f),  // left   — smooth star/sphere
                placeProcedural( 64.0f, 64.0f, 64.0f, 0.55f, 0.75f, 1.00f, 1u, 2.0f, 0.5f),  // centre — displaced planet
                placeProcedural(114.0f, 64.0f, 64.0f, 0.85f, 0.90f, 1.00f, 0u, 0.0f, 0.0f),  // right  — smooth sphere
            };
            if (auto* bodyScene = static_cast<BodyOctreeSceneNode*>(renderGraph->GetInstance(bodyOctreeSceneNode))) {
                bodyScene->SetInstances(std::move(defaultBodies));
                mainLogger->Info("[BuildRenderGraph] Seeded 3 Procedural SDF body instances (standalone fallback)");
            }
        }
    }
}
