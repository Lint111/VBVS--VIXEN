// cpu_body_render_main.cpp — headless, pure-CPU render of the SP2 body scene to PNG.
//
// =============================================================================
// WHAT THIS IS (and is NOT)
// =============================================================================
// A standalone visual-verification tool that mirrors, ON THE CPU, exactly what the
// GPU shader BodyInstanceRayMarch.comp draws — but using the SVO library's proven
// CPU LaineKarrasOctree::castRay / castRayWithLOD traversal (the same code path
// test_cornell_box.cpp and test_shell_octree.cpp exercise).
//
//   *** NO Vulkan. NO GPU. NO VkDevice. ***  It cannot touch the WSL/Dozen GPU.
//   It is 100% safe to compile and RUN. That is the whole point: a GPU-free way to
//   eyeball whether the body renderer produces round, lit, per-kind-coloured bodies.
//
// It builds the ≤3 per-kind shell octrees exactly as BodyOctreeSceneNode does
// (BuildShellOctree(kShellDepth, kind+1), kShellDepth = 6), lays out a representative
// body scene with vixen/render's BuildBodyInstances rule, constructs a camera ray per
// pixel the way getRayDir() does in the shader, transforms the world ray into each
// instance's octree-local frame, casts, keeps the nearest world-space hit across all
// instances, and Lambert-shades it tinted by the per-kind colour.
//
// Output: two PNGs — a FAR view (whole system, bodies small → coarse LOD) and a NEAR
// view (one body fills the frame → fine detail / shell visible).
//
// =============================================================================
// COORDINATE / TRANSFORM FIDELITY TO THE GLSL PATH
// =============================================================================
// The GPU shader transforms the world ray as (BodyInstanceRayMarch.comp ~L605-607):
//     instOrigin = (rayOrigin - worldPos) / renderScale       (point)
//     instDir    =  rayDir              / renderScale          (direction)
// and then the octree's own worldToLocal maps that into the octree's [0,1]^3 grid.
//
// On the CPU the LaineKarrasOctree built by BuildShellOctree lives in [0,n]^3 world
// coords (n = 2^kShellDepth = 64): the unit sphere is centred at (n/2) with radius
// (n/2) (see ShellOctree.h / test_shell_octree.cpp). To make that octree's sphere
// occupy a world ball of radius `renderScale` centred at `worldPos` — i.e. to put it
// where the shader puts it — we use the local↔world similarity:
//     s = renderScale / (n/2)                 // world units per octree-local unit
//     localPt  = (worldPt  - worldPos) / s + (n/2)
//     localDir =  worldDir / s
// castRay returns hit.hitPoint in octree-local [0,n] coords; we map it back to world
// (worldHit = worldPos + (hitPoint - n/2) * s) and rank instances by the TRUE world
// distance |worldHit - cameraPos|, so the cross-instance nearest-hit test compares
// like-for-like (mirroring the shader's hitT-in-world-units invariant).
//
// The hit entity carries the Material component (= kind+1), so we recover the kind
// directly and shade with that kind's colour — equivalent to, and a touch crisper
// than, the shader's "neutral-grey * inst.color" tint for LOD-coarse hits.
// =============================================================================

#include "ShellOctree.h"        // Vixen::SVO::BuildShellOctree, ShellOctree
#include "SVOLOD.h"             // Vixen::SVO::LODParameters
#include "VoxelComponents.h"    // Material component

#include "scene_instances.h"    // undertow::vixen::BuildBodyInstances, BodyInstance
#include "star_scene.h"         // undertow::vixen::Body

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <glm/glm.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace {

// --- Shader-mirrored constants -------------------------------------------------
constexpr int   kShellDepth          = 6;     // BodyOctreeSceneNode::kShellDepth
constexpr int   kKindCount           = 3;     // star / planet / moon
constexpr float kRaymarchCameraFovDeg = 45.0f; // kRaymarchCameraFovDegrees

// Per-kind shade colour (matches scene_instances.h kColorStar/Planet/Moon and the
// shader's per-instance colour). Indexed by kind = materialId - 1.
constexpr glm::vec3 kKindColor[kKindCount] = {
    glm::vec3(1.00f, 0.95f, 0.60f),   // 0 star  — warm
    glm::vec3(0.25f, 0.50f, 0.90f),   // 1 planet — blue
    glm::vec3(0.60f, 0.60f, 0.60f),   // 2 moon  — gray
};

// --- A built, placed body instance (octree handle + world placement) ----------
struct SceneInstance {
    const Vixen::SVO::ShellOctree* octree;   // one of the ≤3 per-kind shells (non-owning)
    Vixen::GaiaVoxel::GaiaVoxelWorld* world; // its world (to read Material on hit)
    glm::vec3 worldPos;                      // AU
    float     renderScale;                   // world-space blob radius (AU)
    glm::vec3 color;                         // per-kind tint
};

// --- Camera (mirrors BodyInstanceRayMarch.comp getRayDir) ----------------------
struct Camera {
    glm::vec3 pos;
    glm::vec3 dir;     // normalized forward
    glm::vec3 up;      // normalized
    glm::vec3 right;   // normalized
    float     fovRad;  // vertical-ish fov (shader uses a single fov + aspect)
    float     aspect;  // width / height
};

// getRayDir(uv): the shader builds, for uv in [0,1]^2,
//   ndc = uv*2 - 1; rayDir = normalize(dir + right*ndc.x*tan(fov/2)*aspect
//                                          + up*ndc.y*tan(fov/2))
// (RayGeneration.glsl). We reproduce that here. Note GLSL image y grows downward;
// we flip v so the PNG is upright.
glm::vec3 getRayDir(const Camera& cam, float u, float v) {
    const float ndcX = u * 2.0f - 1.0f;
    const float ndcY = (1.0f - v) * 2.0f - 1.0f;   // flip so +y is up in the image
    const float t    = std::tan(cam.fovRad * 0.5f);
    const glm::vec3 d = cam.dir
                      + cam.right * (ndcX * t * cam.aspect)
                      + cam.up    * (ndcY * t);
    return glm::normalize(d);
}

Camera makeCameraLookingAt(const glm::vec3& eye, const glm::vec3& target,
                           float fovDeg, float aspect) {
    Camera cam;
    cam.pos    = eye;
    cam.dir    = glm::normalize(target - eye);
    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    cam.right  = glm::normalize(glm::cross(cam.dir, worldUp));
    cam.up     = glm::normalize(glm::cross(cam.right, cam.dir));
    cam.fovRad = glm::radians(fovDeg);
    cam.aspect = aspect;
    return cam;
}

// --- One instance cast: world ray → octree-local [0,n] frame → castRay ---------
//
// Returns true on hit; fills worldHitPoint, worldNormal, materialId, and the
// world-space distance from the camera (for the cross-instance nearest test).
bool castInstance(const SceneInstance& inst, const glm::vec3& rayOrigin,
                  const glm::vec3& rayDir, bool useLod, const Vixen::SVO::LODParameters& lod,
                  glm::vec3& worldHitPoint, glm::vec3& worldNormal,
                  uint32_t& materialId, float& worldDist) {
    const float n    = static_cast<float>(1 << kShellDepth);   // 64
    const float half = n * 0.5f;                                // sphere centre/radius in local
    const float s    = inst.renderScale / half;                // world units per local unit

    // World → octree-local [0,n] (the frame BuildShellOctree's castRay expects).
    const glm::vec3 localOrigin = (rayOrigin - inst.worldPos) / s + glm::vec3(half);
    const glm::vec3 localDir    = rayDir / s;   // not renormalised — castRay is scale-tolerant

    Vixen::SVO::ISVOStructure::RayHit hit =
        useLod ? inst.octree->octree->castRayWithLOD(localOrigin, localDir, lod, 0.0f, 1e30f)
               : inst.octree->octree->castRay(localOrigin, localDir, 0.0f, 1e30f);
    if (!hit.hit) return false;

    // Local hit → world.
    worldHitPoint = inst.worldPos + (hit.hitPoint - glm::vec3(half)) * s;
    worldDist     = glm::length(worldHitPoint - rayOrigin);

    // Normal: BuildShellOctree builds a hollow unit sphere centred at `half`, so the
    // local surface normal ≈ normalize(hitPoint - centre). This reads far rounder than
    // the per-face brick normal (which the shader also discards for LOD hits). A pure
    // similarity (uniform scale + translate) preserves directions, so the world normal
    // is the same vector.
    const glm::vec3 fromCentre = hit.hitPoint - glm::vec3(half);
    worldNormal = (glm::dot(fromCentre, fromCentre) > 1e-12f)
                  ? glm::normalize(fromCentre)
                  : glm::vec3(hit.normal);   // degenerate fallback: face normal

    // Material (= kind+1) from the hit entity.
    materialId = 0u;
    if (inst.world->exists(hit.entity)) {
        auto m = inst.world->getComponentValue<Vixen::GaiaVoxel::Material>(hit.entity);
        if (m.has_value()) materialId = m.value();
    }
    return true;
}

// --- Render one frame ----------------------------------------------------------
struct RenderStats { int hitPixels = 0; };

RenderStats renderFrame(const std::vector<SceneInstance>& scene, const Camera& cam,
                        int width, int height, bool useLod,
                        std::vector<uint8_t>& rgb /*out, width*height*3*/) {
    rgb.assign(static_cast<size_t>(width) * height * 3, 0);

    // Screen-space LOD params from the camera, exactly like the shader/SVOLOD.h.
    const Vixen::SVO::LODParameters lod =
        Vixen::SVO::LODParameters::fromCamera(cam.fovRad, height);

    // Fixed key light + small ambient (the shader's computeLighting is a Lambert+ambient).
    const glm::vec3 lightDir = glm::normalize(glm::vec3(-0.4f, 0.7f, 0.55f));
    const float     ambient  = 0.12f;

    RenderStats stats;
    for (int py = 0; py < height; ++py) {
        for (int px = 0; px < width; ++px) {
            const float u = (static_cast<float>(px) + 0.5f) / static_cast<float>(width);
            const float v = (static_cast<float>(py) + 0.5f) / static_cast<float>(height);
            const glm::vec3 rayDir = getRayDir(cam, u, v);

            // Nearest world-space hit across all instances.
            bool      anyHit  = false;
            float     bestDist = std::numeric_limits<float>::max();
            glm::vec3 bestColor(0.0f);
            glm::vec3 bestNormal(0.0f, 1.0f, 0.0f);

            for (const SceneInstance& inst : scene) {
                glm::vec3 wHit, wNrm;
                uint32_t  matId;
                float     wDist;
                if (castInstance(inst, cam.pos, rayDir, useLod, lod, wHit, wNrm, matId, wDist)) {
                    if (wDist < bestDist) {
                        bestDist  = wDist;
                        bestNormal = wNrm;
                        // Tint by the hit kind's colour (materialId = kind+1); fall back
                        // to the instance's own colour if the entity lacked a Material.
                        const int kind = (matId >= 1u && matId <= static_cast<uint32_t>(kKindCount))
                                         ? static_cast<int>(matId) - 1 : -1;
                        bestColor = (kind >= 0) ? kKindColor[kind] : inst.color;
                        anyHit    = true;
                    }
                }
            }

            glm::vec3 out;
            if (anyHit) {
                const float lambert = std::max(0.0f, glm::dot(bestNormal, lightDir));
                out = bestColor * (ambient + (1.0f - ambient) * lambert);
                ++stats.hitPixels;
            } else {
                // Dark background, faint vertical gradient (echoes the shader sky).
                out = glm::vec3(0.02f, 0.02f, 0.06f) * (1.0f - v * 0.4f);
            }

            const size_t idx = (static_cast<size_t>(py) * width + px) * 3;
            for (int c = 0; c < 3; ++c) {
                const float g = std::pow(glm::clamp(out[c], 0.0f, 1.0f), 1.0f / 2.2f); // gamma
                rgb[idx + c] = static_cast<uint8_t>(g * 255.0f + 0.5f);
            }
        }
    }
    return stats;
}

// --- Scene centroid (look-at target) ------------------------------------------
glm::vec3 centroid(const std::vector<SceneInstance>& scene) {
    glm::vec3 c(0.0f);
    if (scene.empty()) return c;
    for (const auto& s : scene) c += s.worldPos;
    return c / static_cast<float>(scene.size());
}

} // namespace

int main() {
    using namespace undertow::vixen;

    std::printf("=== CPU body-scene renderer (pure castRay, no Vulkan) ===\n");

    // -------------------------------------------------------------------------
    // 1. Build the ≤3 per-kind shell octrees exactly as BodyOctreeSceneNode does:
    //    BuildShellOctree(kShellDepth, materialId = kind+1).
    // -------------------------------------------------------------------------
    std::array<Vixen::SVO::ShellOctree, kKindCount> shells = {
        Vixen::SVO::BuildShellOctree(kShellDepth, /*materialId star  */ 1u),
        Vixen::SVO::BuildShellOctree(kShellDepth, /*materialId planet*/ 2u),
        Vixen::SVO::BuildShellOctree(kShellDepth, /*materialId moon  */ 3u),
    };
    std::printf("Built %d per-kind shell octrees (depth=%d, n=%d cells/axis).\n",
                kKindCount, kShellDepth, 1 << kShellDepth);

    // -------------------------------------------------------------------------
    // 2. A representative body scene: a star at the origin, three planets at
    //    distinct AU offsets, and a moon near the middle planet. Spread so several
    //    are visible. Then run the SHIPPED instance rule (BuildBodyInstances).
    // -------------------------------------------------------------------------
    const double kSun = 1.989e30, kEarth = 5.972e24, kJup = 1.898e27, kLuna = 7.35e22;
    std::vector<Body> bodies = {
        { 0.0,  0.0,  0.0, kSun,  0 },   // star at origin
        { 1.6,  0.0,  0.3, kEarth, 1 },  // inner planet
        { 0.2,  0.0, -2.4, kJup,   1 },  // outer planet (different axis)
        {-2.6,  0.4,  0.6, kEarth, 1 },  // a third planet, off-plane
        { 1.6,  0.18, 0.3 + 0.42, kLuna, 2 }, // moon hugging the inner planet
    };
    const std::vector<BodyInstance> insts = BuildBodyInstances(bodies, DeriveBaseRenderRadiusAu(bodies));

    // Bind each logical instance to its built octree + world.
    std::vector<SceneInstance> scene;
    scene.reserve(insts.size());
    for (const BodyInstance& bi : insts) {
        const int kind = (bi.kind < static_cast<uint32_t>(kKindCount))
                         ? static_cast<int>(bi.kind) : 0;
        SceneInstance s;
        s.octree      = &shells[kind];
        s.world       = shells[kind].world.get();
        s.worldPos    = glm::vec3(bi.worldPos[0], bi.worldPos[1], bi.worldPos[2]);
        s.renderScale = bi.renderScale;
        s.color       = glm::vec3(bi.color[0], bi.color[1], bi.color[2]);
        scene.push_back(s);
    }
    std::printf("Scene: %zu instances (1 star, 3 planets, 1 moon).\n", scene.size());

    constexpr int kW = 512, kH = 512;
    const float aspect = static_cast<float>(kW) / static_cast<float>(kH);
    const glm::vec3 target = centroid(scene);

    int totalOk = 0;

    // -------------------------------------------------------------------------
    // 3a. FAR view — frame the whole system from a distance (bodies small → the
    //     screen-space LOD ramp resolves them at coarse voxels). castRayWithLOD.
    // -------------------------------------------------------------------------
    {
        // Distance that fits the spread: bounding radius of the body positions + margin.
        float maxR = 1.0f;
        for (const auto& s : scene) maxR = std::max(maxR, glm::length(s.worldPos - target));
        const float dist = maxR * 3.2f + 1.5f;
        const glm::vec3 eye = target + glm::normalize(glm::vec3(0.55f, 0.42f, 1.0f)) * dist;
        const Camera cam = makeCameraLookingAt(eye, target, kRaymarchCameraFovDeg, aspect);

        std::vector<uint8_t> rgb;
        const RenderStats st = renderFrame(scene, cam, kW, kH, /*useLod=*/true, rgb);
        const char* path = "/tmp/body_render_far.png";
        const int ok = stbi_write_png(path, kW, kH, 3, rgb.data(), kW * 3);
        totalOk += (ok != 0);
        std::printf("[FAR ] %dx%d  eye=(%.2f,%.2f,%.2f) target=(%.2f,%.2f,%.2f) dist=%.2f AU "
                    "| LOD on | body pixels=%d | %s -> %s\n",
                    kW, kH, eye.x, eye.y, eye.z, target.x, target.y, target.z, dist,
                    st.hitPixels, ok ? "OK" : "WRITE-FAILED", path);
    }

    // -------------------------------------------------------------------------
    // 3b. NEAR view — camera close to ONE body (the inner planet) so it fills the
    //     frame and the shell/fine detail is visible. Full-detail castRay.
    // -------------------------------------------------------------------------
    {
        const SceneInstance& focus = scene[1];   // inner planet
        const glm::vec3 ftgt = focus.worldPos;
        // Sit a few body-radii away so it nearly fills the 45° frame.
        const float dist = focus.renderScale * 4.0f;
        const glm::vec3 eye = ftgt + glm::normalize(glm::vec3(0.3f, 0.25f, 1.0f)) * dist;
        const Camera cam = makeCameraLookingAt(eye, ftgt, kRaymarchCameraFovDeg, aspect);

        std::vector<uint8_t> rgb;
        const RenderStats st = renderFrame(scene, cam, kW, kH, /*useLod=*/false, rgb);
        const char* path = "/tmp/body_render_near.png";
        const int ok = stbi_write_png(path, kW, kH, 3, rgb.data(), kW * 3);
        totalOk += (ok != 0);
        std::printf("[NEAR] %dx%d  eye=(%.2f,%.2f,%.2f) focus=planet@(%.2f,%.2f,%.2f) "
                    "scale=%.3f AU dist=%.3f AU | full detail | body pixels=%d | %s -> %s\n",
                    kW, kH, eye.x, eye.y, eye.z, ftgt.x, ftgt.y, ftgt.z,
                    focus.renderScale, dist, st.hitPixels, ok ? "OK" : "WRITE-FAILED", path);
    }

    std::printf("Wrote %d/2 PNG(s).\n", totalOk);
    return (totalOk == 2) ? 0 : 1;
}
