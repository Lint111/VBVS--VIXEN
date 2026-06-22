// Suppress <windows.h>'s min/max macros (pulled in transitively on the Windows build) so the
// std::max below isn't mangled into a syntax error. Must precede every include.
#define NOMINMAX

// cpu_castray_cmp_main.cpp — CPU castRay render of the SAME single shell + SAME camera
// as the GPU shader render (test_body_instance_raymarch_render), for a direct apples-to-
// apples brick-crack comparison.  NO Vulkan, NO GPU — pure SVO castRay.
//
// It deliberately mirrors the SHIPPED shader's per-instance placement convention (NOT the
// centred convention cpu_body_render uses), so the geometry lines up with /tmp/glsl_shader_near.png:
//   - one ShellOctree built with BuildShellOctree(6, materialId=1)  (the "octree 0" shell),
//   - octree LOCAL space is [0,n]^3 (n=64); the shell sphere fills it (centre n/2, radius n/2),
//   - the shader maps base-world [0,10]^3 -> local [0,1]^3 (localToWorld = scale(10)), and
//     de-instances the world ray as instOrigin=(rayOrigin-worldPos)/renderScale, so a base-world
//     point p maps to actual-world worldPos + p*renderScale.  Composing the two, world->local is:
//         local01 = 0.1 * (world - worldPos) / renderScale         (in [0,1])
//         local_n = local01 * n                                     (in [0,n], what castRay wants)
//     i.e. localPt = (n*0.1/renderScale) * (world - worldPos);  localDir = (n*0.1/renderScale)*dir.
//   The body therefore sits at actual-world centre worldPos + 0.5*10*renderScale, radius
//   0.5*10*renderScale — identical to the GPU test's framing.
//
// Output: /tmp/cpu_castray_near_cmp.png (512x512), to set beside /tmp/glsl_shader_near.png.

#include "ShellOctree.h"        // Vixen::SVO::BuildShellOctree, ShellOctree
#include "VoxelComponents.h"    // Material component

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

constexpr int   kShellDepth = 6;          // matches BodyOctreeSceneNode::kShellDepth
constexpr float kWorldGridSize = 10.0f;   // matches ShellOctreeGpu::Serialize localToWorld scale
constexpr float kBase = 0.05f;            // matches scene_instances.h kBaseRenderRadiusAu
constexpr int   kW = 512, kH = 512;

// getRayDir mirroring RayGeneration.glsl / cpu_body_render_main.cpp (Vulkan Y-flip).
glm::vec3 getRayDir(const glm::vec3& dir, const glm::vec3& up, const glm::vec3& right,
                    float fovRad, float aspect, float u, float v) {
    const float ndcX = u * 2.0f - 1.0f;
    const float ndcY = (1.0f - v) * 2.0f - 1.0f;
    const float t    = std::tan(fovRad * 0.5f);
    return glm::normalize(dir + right * (ndcX * t * aspect) + up * (ndcY * t));
}

} // namespace

int main() {
    std::printf("=== CPU castRay comparison render (single octree-0 shell, shader camera) ===\n");

    // --- One shell, materialId=1 (the "octree 0" / star shell BodyOctreeSceneNode builds). ---
    Vixen::SVO::ShellOctree shell = Vixen::SVO::BuildShellOctree(kShellDepth, /*materialId=*/1u);
    const float n = static_cast<float>(1 << kShellDepth);  // 64

    // --- Instance placement = the GPU test's single body: worldPos origin, renderScale=kBase*2. ---
    const glm::vec3 worldPos(0.0f, 0.0f, 0.0f);
    const float     renderScale = kBase * 2.0f;            // 0.10
    // world->local-[0,n] composite factor (see header derivation).
    const float     w2l = n * 0.1f / renderScale;          // 64*0.1/0.10 = 64

    // --- Camera: IDENTICAL to the GPU test (shader's true sphere centre + framing). ---
    const float     R     = 0.5f * kWorldGridSize * renderScale;          // 0.50 AU
    const glm::vec3 focus = worldPos + glm::vec3(R);                       // (0.5,0.5,0.5)
    const float     dist  = R * 4.0f;                                      // 2.0 AU back
    const glm::vec3 eye   = focus + glm::normalize(glm::vec3(0.3f, 0.25f, 1.0f)) * dist;
    const glm::vec3 dir   = glm::normalize(focus - eye);
    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(dir, worldUp));
    const glm::vec3 up    = glm::normalize(glm::cross(right, dir));
    const float     fovRad = glm::radians(45.0f);
    const float     aspect = static_cast<float>(kW) / static_cast<float>(kH);

    // --- Lighting: same Lambert+ambient as cpu_body_render_main.cpp. ---
    const glm::vec3 lightDir = glm::normalize(glm::vec3(-0.4f, 0.7f, 0.55f));
    const float     ambient  = 0.12f;
    // Shade with the shell's material colour (palette idx 1 = red {0.75,0.1,0.1}), to match
    // what the GPU shader shows for this shell (getMaterialColor(matID=1)).
    const glm::vec3 bodyColor(0.75f, 0.1f, 0.1f);

    std::vector<uint8_t> rgb(static_cast<size_t>(kW) * kH * 3, 0);
    int hitPixels = 0;

    for (int py = 0; py < kH; ++py) {
        for (int px = 0; px < kW; ++px) {
            const float u = (static_cast<float>(px) + 0.5f) / static_cast<float>(kW);
            const float v = (static_cast<float>(py) + 0.5f) / static_cast<float>(kH);
            const glm::vec3 rayDir = getRayDir(dir, up, right, fovRad, aspect, u, v);

            // World ray -> octree-local [0,n] (the frame castRay expects), shader convention.
            const glm::vec3 localOrigin = (eye - worldPos) * w2l;
            const glm::vec3 localDir    = rayDir * w2l;   // not renormalised — castRay is scale-tolerant

            const Vixen::SVO::ISVOStructure::RayHit hit =
                shell.octree->castRay(localOrigin, localDir, 0.0f, 1e30f);

            glm::vec3 out;
            if (hit.hit) {
                // Local surface normal ≈ normalize(hitPoint - centre) (rounder than face normal,
                // exactly as cpu_body_render_main.cpp does for shell hits).
                const glm::vec3 fromCentre = hit.hitPoint - glm::vec3(n * 0.5f);
                const glm::vec3 nrm = (glm::dot(fromCentre, fromCentre) > 1e-12f)
                                      ? glm::normalize(fromCentre) : glm::vec3(hit.normal);
                const float lambert = std::max(0.0f, glm::dot(nrm, lightDir));
                out = bodyColor * (ambient + (1.0f - ambient) * lambert);
                ++hitPixels;
            } else {
                out = glm::vec3(0.02f, 0.02f, 0.06f) * (1.0f - v * 0.4f);   // sky (matches shader)
            }

            const size_t idx = (static_cast<size_t>(py) * kW + px) * 3;
            for (int c = 0; c < 3; ++c) {
                const float g = std::pow(glm::clamp(out[c], 0.0f, 1.0f), 1.0f / 2.2f);  // gamma
                rgb[idx + c] = static_cast<uint8_t>(g * 255.0f + 0.5f);
            }
        }
    }

    const char* path = "/tmp/cpu_castray_near_cmp.png";
    const int ok = stbi_write_png(path, kW, kH, 3, rgb.data(), kW * 3);
    std::printf("[CMP ] %dx%d eye=(%.3f,%.3f,%.3f) focus=(%.3f,%.3f,%.3f) R=%.3f dist=%.3f fov=45 | "
                "castRay | body pixels=%d | %s -> %s\n",
                kW, kH, eye.x, eye.y, eye.z, focus.x, focus.y, focus.z, R, dist,
                hitPixels, ok ? "OK" : "WRITE-FAILED", path);
    return ok ? 0 : 1;
}
