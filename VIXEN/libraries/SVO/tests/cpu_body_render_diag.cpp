// cpu_body_render_diag.cpp — Diagnostic variants for the CPU body render tool.
//
// Produces 5 PNG variants to isolate the cause of the brick-grid / gap artifacts
// observed in body_render_near.png. All variants use the EXACT same NEAR camera
// from cpu_body_render_main.cpp (inner planet close-up, full-detail castRay).
//
//   /tmp/diag_shell_d6.png    — baseline shell depth 6 (reproduces the artifact)
//   /tmp/diag_solid_d6.png    — solid-sphere depth 6 (all interior voxels filled)
//   /tmp/diag_thickband_d6.png — shell depth 6 with ±2.0-voxel band (wider)
//   /tmp/diag_shell_d8.png    — shell depth 8, 256² (higher resolution reference)
//   /tmp/diag_solid_d8.png    — solid depth 8, 256² (clean high-res reference)
//
// Build: cpu_body_render_diag target (added to CMakeLists.txt beside cpu_body_render).
// NO Vulkan, NO GPU, pure castRay only.

#include "ShellOctree.h"        // Vixen::SVO::BuildShellOctree, ShellOctree
#include "ShellVoxelizer.h"     // ShellVoxels
#include "SVOLOD.h"             // LODParameters
#include "VoxelComponents.h"    // Material

#include "scene_instances.h"    // undertow::vixen::BuildBodyInstances, BodyInstance
#include "star_scene.h"         // undertow::vixen::Body

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Constants (match cpu_body_render_main.cpp exactly)
// ---------------------------------------------------------------------------
constexpr int   kShellDepth           = 6;
constexpr int   kKindCount            = 3;
constexpr float kRaymarchCameraFovDeg = 45.0f;

constexpr glm::vec3 kKindColor[kKindCount] = {
    glm::vec3(1.00f, 0.95f, 0.60f),   // star
    glm::vec3(0.25f, 0.50f, 0.90f),   // planet
    glm::vec3(0.60f, 0.60f, 0.60f),   // moon
};

// ---------------------------------------------------------------------------
// Solid-sphere voxelizer: emit every voxel whose centre is within radius 1.0
// in the [-1,1] normalised space (i.e. all interior + surface cells).
// ---------------------------------------------------------------------------
inline std::vector<glm::ivec3> SolidSphereVoxels(int depth) {
    const int n = 1 << depth;
    const double inv = 2.0 / n;   // cell size in [-1,1] units
    std::vector<glm::ivec3> out;
    out.reserve(static_cast<size_t>(n) * n * n / 2);  // rough pre-alloc
    for (int z = 0; z < n; ++z)
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                const glm::dvec3 p(
                    (x + 0.5) * inv - 1.0,
                    (y + 0.5) * inv - 1.0,
                    (z + 0.5) * inv - 1.0);
                if (glm::dot(p, p) <= 1.0)   // inside the unit sphere
                    out.push_back({x, y, z});
            }
    return out;
}

// ---------------------------------------------------------------------------
// Thicker-band shell: ±2.0·cellSize instead of √3/2·cellSize
// ---------------------------------------------------------------------------
inline std::vector<glm::ivec3> ThickBandShellVoxels(int depth) {
    const int n = 1 << depth;
    const double inv = 2.0 / n;
    const double band = inv * 2.0;   // ±2 voxels wide
    std::vector<glm::ivec3> out;
    for (int z = 0; z < n; ++z)
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                const glm::dvec3 p(
                    (x + 0.5) * inv - 1.0,
                    (y + 0.5) * inv - 1.0,
                    (z + 0.5) * inv - 1.0);
                const double r = glm::length(p);
                if (r >= 1.0 - band && r <= 1.0 + band)
                    out.push_back({x, y, z});
            }
    return out;
}

// ---------------------------------------------------------------------------
// Generic octree builder from an arbitrary voxel list
// ---------------------------------------------------------------------------
Vixen::SVO::ShellOctree BuildOctreeFromVoxels(
        int depth, uint32_t materialId,
        const std::vector<glm::ivec3>& cells) {
    Vixen::SVO::ShellOctree result;

    result.registry = std::make_unique<Vixen::VoxelData::AttributeRegistry>();
    result.registry->registerKey("density", Vixen::VoxelData::AttributeType::Float, 1.0f);
    result.registry->addAttribute("color", Vixen::VoxelData::AttributeType::Vec3, glm::vec3(1.0f));

    result.world = std::make_unique<Vixen::GaiaVoxel::GaiaVoxelWorld>();

    for (const glm::ivec3& cell : cells) {
        const glm::vec3 pos(cell);
        const Vixen::GaiaVoxel::ComponentQueryRequest components[] = {
            Vixen::GaiaVoxel::Density{1.0f},
            Vixen::GaiaVoxel::Color{glm::vec3(1.0f, 1.0f, 1.0f)},
            Vixen::GaiaVoxel::Material{materialId},
        };
        result.world->createVoxel(Vixen::GaiaVoxel::VoxelCreationRequest{pos, components});
    }

    const int n = 1 << depth;
    constexpr int kBrickDepthLevels = 3;
    const int maxLevels = depth + kBrickDepthLevels;

    result.octree = std::make_unique<Vixen::SVO::LaineKarrasOctree>(
        *result.world, result.registry.get(), maxLevels, kBrickDepthLevels);
    result.octree->rebuild(*result.world,
        glm::vec3(0.0f), glm::vec3(static_cast<float>(n)));

    return result;
}

// ---------------------------------------------------------------------------
// Camera (identical to cpu_body_render_main.cpp)
// ---------------------------------------------------------------------------
struct Camera {
    glm::vec3 pos, dir, up, right;
    float fovRad, aspect;
};

glm::vec3 getRayDir(const Camera& cam, float u, float v) {
    const float ndcX = u * 2.0f - 1.0f;
    const float ndcY = (1.0f - v) * 2.0f - 1.0f;
    const float t    = std::tan(cam.fovRad * 0.5f);
    return glm::normalize(cam.dir
                        + cam.right * (ndcX * t * cam.aspect)
                        + cam.up    * (ndcY * t));
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

// ---------------------------------------------------------------------------
// Single-octree render (the diagnostic variants all focus on ONE body)
// ---------------------------------------------------------------------------
struct RenderStats {
    int hitPixels   = 0;
    double renderMs = 0.0;
};

struct SingleOctreeScene {
    const Vixen::SVO::ShellOctree* octree;
    Vixen::GaiaVoxel::GaiaVoxelWorld* world;
    glm::vec3 worldPos;
    float     renderScale;
    glm::vec3 color;
};

bool castSingleOctree(const SingleOctreeScene& inst,
                      const glm::vec3& rayOrigin,
                      const glm::vec3& rayDir,
                      int depth,
                      glm::vec3& worldHitPoint,
                      glm::vec3& worldNormal,
                      uint32_t&  materialId,
                      float&     worldDist) {
    const float n    = static_cast<float>(1 << depth);
    const float half = n * 0.5f;
    const float s    = inst.renderScale / half;

    const glm::vec3 localOrigin = (rayOrigin - inst.worldPos) / s + glm::vec3(half);
    const glm::vec3 localDir    = rayDir / s;

    Vixen::SVO::ISVOStructure::RayHit hit =
        inst.octree->octree->castRay(localOrigin, localDir, 0.0f, 1e30f);
    if (!hit.hit) return false;

    worldHitPoint = inst.worldPos + (hit.hitPoint - glm::vec3(half)) * s;
    worldDist     = glm::length(worldHitPoint - rayOrigin);

    const glm::vec3 fromCentre = hit.hitPoint - glm::vec3(half);
    worldNormal = (glm::dot(fromCentre, fromCentre) > 1e-12f)
                  ? glm::normalize(fromCentre)
                  : glm::vec3(hit.normal);

    materialId = 0u;
    if (inst.world->exists(hit.entity)) {
        auto m = inst.world->getComponentValue<Vixen::GaiaVoxel::Material>(hit.entity);
        if (m.has_value()) materialId = m.value();
    }
    return true;
}

RenderStats renderSingleBody(const SingleOctreeScene& inst, int depth,
                             const Camera& cam, int width, int height,
                             const char* path) {
    std::vector<uint8_t> rgb(static_cast<size_t>(width) * height * 3, 0);

    const glm::vec3 lightDir = glm::normalize(glm::vec3(-0.4f, 0.7f, 0.55f));
    constexpr float ambient  = 0.12f;

    RenderStats stats;
    const auto t0 = std::chrono::high_resolution_clock::now();

    for (int py = 0; py < height; ++py) {
        for (int px = 0; px < width; ++px) {
            const float u = (static_cast<float>(px) + 0.5f) / static_cast<float>(width);
            const float v = (static_cast<float>(py) + 0.5f) / static_cast<float>(height);
            const glm::vec3 rayDir = getRayDir(cam, u, v);

            glm::vec3 wHit, wNrm;
            uint32_t  matId;
            float     wDist;
            glm::vec3 out;

            if (castSingleOctree(inst, cam.pos, rayDir, depth, wHit, wNrm, matId, wDist)) {
                ++stats.hitPixels;
                const int kind = (matId >= 1u && matId <= static_cast<uint32_t>(kKindCount))
                                 ? static_cast<int>(matId) - 1 : -1;
                const glm::vec3 color = (kind >= 0) ? kKindColor[kind] : inst.color;
                const float lambert   = std::max(0.0f, glm::dot(wNrm, lightDir));
                out = color * (ambient + (1.0f - ambient) * lambert);
            } else {
                out = glm::vec3(0.02f, 0.02f, 0.06f) * (1.0f - v * 0.4f);
            }

            const size_t idx = (static_cast<size_t>(py) * width + px) * 3;
            for (int c = 0; c < 3; ++c) {
                const float g = std::pow(glm::clamp(out[c], 0.0f, 1.0f), 1.0f / 2.2f);
                rgb[idx + c] = static_cast<uint8_t>(g * 255.0f + 0.5f);
            }
        }
    }

    const auto t1 = std::chrono::high_resolution_clock::now();
    stats.renderMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    const int ok = stbi_write_png(path, width, height, 3, rgb.data(), width * 3);
    std::printf("  %dx%d | hit=%d | %.1f ms | %s => %s\n",
                width, height, stats.hitPixels, stats.renderMs,
                ok ? "OK" : "WRITE-FAILED", path);
    return stats;
}

} // anonymous namespace

int main() {
    using namespace undertow::vixen;

    std::printf("=== CPU body render DIAGNOSTICS (pure castRay, no Vulkan) ===\n");
    std::printf("All variants: single body (inner planet kind), NEAR camera, full-detail castRay.\n\n");

    // -------------------------------------------------------------------------
    // Build the same body scene as the main tool to extract the EXACT NEAR camera
    // parameters (inner planet at scene[1], worldPos + renderScale).
    // -------------------------------------------------------------------------
    const double kSun = 1.989e30, kEarth = 5.972e24, kJup = 1.898e27, kLuna = 7.35e22;
    const std::vector<Body> bodies = {
        { 0.0,  0.0,  0.0, kSun,   0 },
        { 1.6,  0.0,  0.3, kEarth, 1 },
        { 0.2,  0.0, -2.4, kJup,   1 },
        {-2.6,  0.4,  0.6, kEarth, 1 },
        { 1.6,  0.18, 0.3 + 0.42, kLuna, 2 },
    };
    const std::vector<BodyInstance> insts = BuildBodyInstances(bodies);

    // scene[1] = inner planet (index 1 mirrors cpu_body_render_main.cpp line 337)
    const BodyInstance& focusBi = insts[1];
    const glm::vec3 ftgt(focusBi.worldPos[0], focusBi.worldPos[1], focusBi.worldPos[2]);
    const float renderScale = focusBi.renderScale;

    std::printf("Focus body: kind=%u  worldPos=(%.4f,%.4f,%.4f)  renderScale=%.6f AU\n",
                focusBi.kind, ftgt.x, ftgt.y, ftgt.z, renderScale);

    // NEAR camera — identical to cpu_body_render_main.cpp section 3b
    constexpr float kW512 = 512.0f, kH512 = 512.0f;
    const float dist512 = renderScale * 4.0f;
    const glm::vec3 eye512 = ftgt + glm::normalize(glm::vec3(0.3f, 0.25f, 1.0f)) * dist512;
    const Camera cam512 = makeCameraLookingAt(eye512, ftgt, kRaymarchCameraFovDeg,
                                              kW512 / kH512);
    std::printf("NEAR camera: eye=(%.4f,%.4f,%.4f)  dist=%.6f AU\n\n",
                eye512.x, eye512.y, eye512.z, dist512);

    // 256² camera for depth-8 variants (same angle, same target)
    constexpr float kW256 = 256.0f, kH256 = 256.0f;
    const Camera cam256 = makeCameraLookingAt(eye512, ftgt, kRaymarchCameraFovDeg,
                                              kW256 / kH256);

    // Material id for the inner planet (kind=1 => materialId=2 in original tool).
    // We just pass 2u to all diagnostic octrees so the kind-color lookup works.
    constexpr uint32_t kPlanetMatId = 2u;

    // Helper: wrap a ShellOctree into the single-body scene descriptor
    auto makeScene = [&](const Vixen::SVO::ShellOctree& oct) -> SingleOctreeScene {
        SingleOctreeScene s;
        s.octree      = &oct;
        s.world       = oct.world.get();
        s.worldPos    = ftgt;
        s.renderScale = renderScale;
        s.color       = kKindColor[1];   // planet blue
        return s;
    };

    // -------------------------------------------------------------------------
    // VARIANT 1: Baseline shell depth 6 (reproduces the original artifact)
    // -------------------------------------------------------------------------
    std::printf("[1/5] Baseline shell, depth=%d, 512x512 → /tmp/diag_shell_d6.png\n", kShellDepth);
    {
        const auto cells = Vixen::SVO::ShellVoxels(kShellDepth);
        std::printf("      voxel count: %zu\n", cells.size());
        Vixen::SVO::ShellOctree oct = BuildOctreeFromVoxels(kShellDepth, kPlanetMatId, cells);
        renderSingleBody(makeScene(oct), kShellDepth, cam512, 512, 512,
                         "/tmp/diag_shell_d6.png");
    }

    // -------------------------------------------------------------------------
    // VARIANT 2: Solid sphere depth 6
    // -------------------------------------------------------------------------
    std::printf("[2/5] Solid sphere, depth=%d, 512x512 → /tmp/diag_solid_d6.png\n", kShellDepth);
    {
        const auto cells = SolidSphereVoxels(kShellDepth);
        std::printf("      voxel count: %zu\n", cells.size());
        Vixen::SVO::ShellOctree oct = BuildOctreeFromVoxels(kShellDepth, kPlanetMatId, cells);
        renderSingleBody(makeScene(oct), kShellDepth, cam512, 512, 512,
                         "/tmp/diag_solid_d6.png");
    }

    // -------------------------------------------------------------------------
    // VARIANT 3: Thicker-band shell depth 6
    // -------------------------------------------------------------------------
    std::printf("[3/5] Thick-band shell (±2.0 voxels), depth=%d, 512x512 → /tmp/diag_thickband_d6.png\n",
                kShellDepth);
    {
        const auto cells = ThickBandShellVoxels(kShellDepth);
        std::printf("      voxel count: %zu\n", cells.size());
        Vixen::SVO::ShellOctree oct = BuildOctreeFromVoxels(kShellDepth, kPlanetMatId, cells);
        renderSingleBody(makeScene(oct), kShellDepth, cam512, 512, 512,
                         "/tmp/diag_thickband_d6.png");
    }

    // -------------------------------------------------------------------------
    // VARIANT 4: Shell depth 8 at 256² (high-resolution)
    // -------------------------------------------------------------------------
    constexpr int kDepth8 = 8;
    std::printf("[4/5] Shell, depth=%d, 256x256 → /tmp/diag_shell_d8.png\n", kDepth8);
    {
        const auto cells = Vixen::SVO::ShellVoxels(kDepth8);
        std::printf("      voxel count: %zu\n", cells.size());
        Vixen::SVO::ShellOctree oct = BuildOctreeFromVoxels(kDepth8, kPlanetMatId, cells);
        renderSingleBody(makeScene(oct), kDepth8, cam256, 256, 256,
                         "/tmp/diag_shell_d8.png");
    }

    // -------------------------------------------------------------------------
    // VARIANT 5: Solid sphere depth 8 at 256²
    // -------------------------------------------------------------------------
    std::printf("[5/5] Solid sphere, depth=%d, 256x256 → /tmp/diag_solid_d8.png\n", kDepth8);
    {
        const auto cells = SolidSphereVoxels(kDepth8);
        std::printf("      voxel count: %zu\n", cells.size());
        Vixen::SVO::ShellOctree oct = BuildOctreeFromVoxels(kDepth8, kPlanetMatId, cells);
        renderSingleBody(makeScene(oct), kDepth8, cam256, 256, 256,
                         "/tmp/diag_solid_d8.png");
    }

    std::printf("\nDiagnostic renders complete.\n");
    return 0;
}
