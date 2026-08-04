#pragma once
#include "Recipe/RecipeRegistry.h"
#include "Recipe/SdfRecipeCodegenGlsl.h"
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

namespace Vixen::SVO::Recipe {

// W-KGLSL D2 — kernel-emitted recipe GLSL bridge (task #33). Opt-in override for
// EmitProceduralFieldFunctionGlsl's local field-function walk below: when
// VIXEN_S1_RECIPE_GLSL=<json> names a D1-extension handoff (schema 1, "functions":
// [{"name":"recipe_<id>", "text":"float sdfRecipe_0(vec3 p, float params[6]) {...}"}],
// keyed by real VIXEN recipeId per team-lead's ruling), the two specialized emitters below
// source a hot recipe's body from the kernel's Lower<T> -> GlslAstVisitor pipeline instead
// of this file's own walk — same signature, same SdfCoreKernels.glsl deps, proven
// byte-compileable in phases B/C. Loud fail-soft: a missing/unset env or an unmapped
// recipeId returns empty (never throws), so both call sites fall back to the local emitter
// and log which one won — never a silent divergence.
inline const std::map<uint32_t, std::string>& KernelEmittedRecipeGlslMap() {
    static const std::map<uint32_t, std::string> map = [] {
        std::map<uint32_t, std::string> m;
        const char* path = std::getenv("VIXEN_S1_RECIPE_GLSL");
        if (!path || !path[0]) return m;

        std::ifstream f(path);
        if (!f.good()) return m;
        nlohmann::json doc = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
        if (doc.is_discarded() || !doc.contains("functions") || !doc["functions"].is_array())
            return m;

        // name is "recipe_<id>" (D1 extension's keying); the id is authoritative, the
        // placeholder sdfRecipe_0 inside `text` gets rewritten to sdfRecipe_<id> below so the
        // returned body is a drop-in for what the local emitter would have produced.
        const std::regex nameRe(R"(recipe_(\d+))");
        const std::regex fnDeclRe(R"(sdfRecipe_0\b)");
        for (const auto& fn : doc["functions"]) {
            std::string name = fn.value("name", "");
            std::string text = fn.value("text", "");
            std::smatch nm;
            if (text.empty() || !std::regex_search(name, nm, nameRe)) continue;
            uint32_t recipeId = static_cast<uint32_t>(std::stoul(nm[1].str()));
            m[recipeId] = std::regex_replace(text, fnDeclRe, "sdfRecipe_" + std::to_string(recipeId));
        }
        return m;
    }();
    return map;
}

// Returns the kernel-emitted body for recipeId, or "" if unavailable (env unset / json
// missing / recipeId not in the D1-extension export) — callers fall back to
// EmitProceduralFieldFunctionGlsl on empty.
inline std::string TryGetKernelEmittedRecipeGlsl(uint32_t recipeId) {
    const auto& map = KernelEmittedRecipeGlslMap();
    auto it = map.find(recipeId);
    return it == map.end() ? std::string() : it->second;
}

// EmitSpecializedRecipeComputeShader — Recipe GPU Instance Bucketing Inc2 M2 (Task 5).
//
// GLSL sibling of SdfRecipeCodegen.h's EmitProceduralComputeShader (the existing HLSL
// "specialized single-recipe trace shader" precedent) — same idea (field function + a fixed
// trace main(), concatenated into ONE standalone compute shader with zero switch-dispatch
// overhead for any recipeId other than the one it was compiled for), but GLSL, and driven by
// this milestone's bucketed-dispatch data instead of a full-screen sweep:
//
//   - reuses EmitProceduralFieldFunctionGlsl for JUST this one recipe's bytecode (NOT
//     SpliceProceduralRecipesIntoSource's multi-recipe switch — this is deliberately the
//     smaller, simpler compile Task 5 calls for).
//   - main() is dispatched via vkCmdDispatchIndirect (Task 4) sized to THIS recipe's bucket's
//     screen-space coverage rect (RecipeInstanceBucketing.comp's mode==2 output) — one thread
//     per pixel WITHIN that rect (not the full screen), offset by the rect's own minX/minY
//     (looked up from the shared BucketMetaBuffer via this dispatch's recipeId, see below).
//   - loops ONLY this bucket's compacted member instance list (RecipeInstanceBucketing.comp's
//     bucketIndices[] slice for this recipeId) — never touches bodyInstances[] entries outside
//     this bucket, unlike the tier-0 switch's full-instance loop.
//   - per design doc §3.3 (resolved): writes HitRecord via a PLAIN (non-atomic)
//     read-compare-conditionally-overwrite (`if (hitT < hitRecords[idx].hitT) ...`), correct
//     because MultiDispatchNode's default autoBarriers_ serializes this dispatch against every
//     other bucket's dispatch touching the same HitRecord SSBO (write-after-write hazard,
//     FrameSyncScheduler::NeedsSync) — no atomics needed, see the design doc for the full proof.
//
// Recipe Bucketed-Dispatch Overhead Inc3 M1 (descriptor/push-constant overhead reduction):
// `BucketMembersBuffer` (binding 1) is now the FULL shared bucketIndices[] output from
// RecipeInstanceBucketing.comp (row-major, stride maxMembersPerBucket), not a per-bucket-aliased
// slice — every specialized dispatch, for every recipe, binds the SAME descriptor set pointing at
// the SAME buffer; the per-bucket row is selected by `pc.recipeId * pc.maxMembersPerBucket + m`
// inside the loop instead of by which buffer/descriptor-set was bound. Likewise `BucketMetaBuffer`
// (binding 3, NEW) replaces the old per-bucket `memberCount`/`rectMinX`/`rectMinY`/`boundRadius`/
// `stepRelaxation` push-constant fields — one shared SSBO, one entry per recipeId, indexed the
// same way. The push-constant block shrinks to the camera/screen fields (identical across every
// bucket in a frame) plus a single `recipeId` selector — the only thing that actually varies
// per-dispatch. This lets the CALLER (MultiDispatchNode's per-bucket DispatchPass loop) bind the
// shared descriptor set ONCE for the whole batch (Vulkan descriptor-set bindings persist across
// vkCmdBindPipeline/vkCmdDispatchIndirect within a command buffer as long as pipeline layouts stay
// compatible, which they do here — all N specialized pipelines share one VkPipelineLayout), instead
// of re-binding + rewriting a distinct descriptor set N times.
//
// Self-contained binding namespace starting at 0 (mirrors RecipeInstanceBucketing.comp's own
// "does not depend on the uber-shader splice chain" precedent) — this shader does NOT #include
// SceneBindings.glsl (that pulls in the ENTIRE tier-0 traversal machinery, including the very
// switch this specialized shader exists to avoid). It needs only: the shared compacted
// instance-index list + bodyInstances[] (for worldPos/recipeParams), the shared per-recipe bucket
// metadata, the camera basis (own push-constant block, same fields as SceneBindings.glsl's
// PushConstants for the ray-gen math), and the shared HitRecord SSBO (binding 18, the SAME binding
// the tier-0 path writes, so both paths composite into ONE shared per-pixel record).
//
// `sdfCoreKernelsGlsl` is the VERBATIM content of libraries/SVO/shaders/recipe/SdfCoreKernels.glsl
// (the SdfCore_* kernel set the emitted field function calls), passed in and textually inlined
// rather than #include-d — mirrors EmitProceduralComputeShader's (SdfRecipeCodegen.h) own
// sdfCoreHlsl-as-parameter convention exactly. This is required, not stylistic:
// ShaderManagement::ShaderCompiler::Compile takes ALREADY-PREPROCESSED source with no #include
// resolution (no includer/-I support), unlike the build-time glslc CLI compiles other shaders in
// this codebase use — the caller must therefore inline this shader's own dependency itself before
// calling Compile, exactly as EmitProceduralComputeShader's caller already does for the HLSL core.
// W2c (wavefront epoch): the standalone binding namespace + push block + getRayDir,
// shared VERBATIM by the march emitter below and EmitSpecializedRecipeShadeShader —
// one interface declaration, two kernel families (the CPU side binds/pushes the same
// way for both; see VulkanGraphApplication.cpp's PreTick orchestration).
inline constexpr const char* kSpecializedRecipeInterfaceGlsl = R"(
struct BodyInstance {
    vec3  worldPos;          // 0
    float renderScale;       // 12
    vec3  color;             // 16
    uint  octreeIndex;       // 28
    uint  providerKind;      // 32
    uint  recipeId;          // 36
    float recipeParams[6];   // 40..63
};
layout(std430, binding = 0) readonly buffer BodyInstanceBuffer { BodyInstance bodyInstances[]; };

// Inc3 M1: the FULL shared bucketIndices[] output from RecipeInstanceBucketing.comp (row-major,
// stride maxMembersPerBucket) — every recipe's compacted member list lives in ONE buffer, ONE
// descriptor set, bound once for the whole N-bucket dispatch batch. This dispatch's own row is
// selected inside main() via `pc.recipeId * pc.maxMembersPerBucket + m`, not by which buffer was
// bound (there is only ever one).
layout(std430, binding = 1) readonly buffer BucketMembersBuffer { uint bucketMembers[]; };

#ifndef HITRECORD_GLSL
#define HITRECORD_GLSL
#define HITRECORD_FLAG_HIT 0x1u
struct HitRecord {
    vec3  albedo;
    float roughness;
    vec3  worldNormal;
    float hitT;
    vec3  worldPos;
    uint  flags;
    uint  _pad0[3];
};
#endif
layout(std430, binding = 2) buffer HitRecordBuffer { HitRecord hitRecords[]; };

// Inc3 M1: per-recipe bucket metadata, ONE shared SSBO indexed by recipeId, replacing the old
// per-bucket memberCount/rectMinX/rectMinY/boundRadius/stepRelaxation push-constant fields.
// std430 layout — matches the CPU-side BucketMetaCpu mirror exactly (32 B/entry).
struct BucketMeta {
    uint  memberCount;
    uint  rectMinX;
    uint  rectMinY;
    float boundRadius;
    float stepRelaxation;
    uint  _pad[3];
};
layout(std430, binding = 3) readonly buffer BucketMetaBuffer { BucketMeta bucketMeta[]; };

layout(push_constant) uniform Push {
    vec3  cameraPos;
    float _p0;
    vec3  cameraDir;
    float fov;
    vec3  cameraUp;
    float aspect;
    vec3  cameraRight;
    float _p1;
    uint  screenWidth;   // FULL screen width (for HitRecord's row-major index, NOT the rect width)
    uint  screenHeight;  // FULL screen height
    uint  maxMembersPerBucket; // bucketMembers[] row stride (same for every recipe)
    uint  recipeId;      // THIS dispatch's bucket selector into BucketMetaBuffer/bucketMembers[]
} pc;

vec3 getRayDir(vec2 uv) {
    float tanHalfFov = tan(radians(pc.fov * 0.5));
    vec2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    return normalize(pc.cameraDir + pc.cameraRight * ndc.x * tanHalfFov * pc.aspect
                                   + pc.cameraUp    * ndc.y * tanHalfFov);
}

)";

inline std::string EmitSpecializedRecipeComputeShader(
    const RecipeRegistry::RecipeEntry& entry,
    uint32_t recipeId,
    const std::string& sdfCoreKernelsGlsl,
    bool emitGradientNormal = true)
{
    std::string fieldFn = TryGetKernelEmittedRecipeGlsl(recipeId);
    const bool kernelEmitted = !fieldFn.empty();
    if (!kernelEmitted) {
        fieldFn = EmitProceduralFieldFunctionGlsl(
            entry.bytecode.data(), static_cast<uint32_t>(entry.bytecode.size()), recipeId);
    }
    std::cout << "[RecipeBucketedDispatch] recipeId=" << recipeId << " field function: "
               << (kernelEmitted ? "kernel-emitted" : "local") << std::endl;

    // Matches EmitProceduralFieldFunctionGlsl's/UberShaderSplice.h's own float-literal guard --
    // every literal baked into GLSL source must carry a decimal point or GLSL parses it as an
    // int (the "1/6 integer-divides to 0" failure class).
    auto f = [](float v) {
        std::string s = std::to_string(v);
        if (s.find('.') == std::string::npos && s.find('e') == std::string::npos)
            s += ".0";
        return s;
    };

    std::ostringstream out;
    out << "#version 460\n\n";
    out << "// Recipe GPU Instance Bucketing Inc2 M2 Task 5 -- specialized single-recipe shader\n";
    out << "// for recipeId " << recipeId << ". Machine-generated by\n";
    out << "// EmitSpecializedRecipeComputeShader -- do not hand-edit.\n\n";
    out << "layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;\n\n";

    out << sdfCoreKernelsGlsl << "\n\n";
    out << fieldFn << "\n";

    // Standalone binding namespace (0..4), mirrors RecipeInstanceBucketing.comp's own
    // self-contained-shader precedent. bodyInstances[] is byte-identical to the shared
    // BodyInstanceGpu layout (binding 10 in SceneBindings.glsl) but declared fresh here at
    // binding 0, since this shader has no other reason to #include the shared chain.
    // (W2c: hoisted to kSpecializedRecipeInterfaceGlsl — shared with the shade emitter.)
    out << kSpecializedRecipeInterfaceGlsl;
    out << "void main() {\n";
    out << "    BucketMeta meta = bucketMeta[pc.recipeId];\n";
    out << "    ivec2 rectCoord = ivec2(gl_GlobalInvocationID.xy);\n";
    out << "    ivec2 pixelCoords = rectCoord + ivec2(int(meta.rectMinX), int(meta.rectMinY));\n";
    out << "    if (pixelCoords.x >= int(pc.screenWidth) || pixelCoords.y >= int(pc.screenHeight)) return;\n";
    out << "    if (pixelCoords.x < 0 || pixelCoords.y < 0) return;\n\n";
    out << "    vec2 uv = (vec2(pixelCoords) + 0.5) / vec2(float(pc.screenWidth), float(pc.screenHeight));\n";
    out << "    vec3 rayOrigin = pc.cameraPos;\n";
    out << "    vec3 rayDir    = getRayDir(uv);\n\n";
    out << "    bool  anyHit = false;\n";
    out << "    float bestT  = 1e30;\n";
    out << "    vec3  bestNormal = vec3(0.0, 1.0, 0.0);\n";
    out << "    vec3  bestColor  = vec3(1.0);\n";
    out << "    uint  bestInstIdx = 0u;\n";
    out << "    float bestEmission = 0.0;\n\n";
    out << "    uint bucketBase = pc.recipeId * pc.maxMembersPerBucket;\n";
    out << "    for (uint m = 0u; m < meta.memberCount; ++m) {\n";
    out << "        uint instIdx = bucketMembers[bucketBase + m];\n";
    out << "        BodyInstance inst = bodyInstances[instIdx];\n";
    out << "        // Bound-sphere reject center matches tier-0's getRecipeBoundSphere EXACTLY:\n";
    out << "        // this recipe's REGISTERED boundCenter, baked as a compile-time constant --\n";
    out << "        // worldPos is NEVER added to it (UberShaderSplice.h's getRecipeBoundSphere\n";
    out << "        // returns entry.boundCenter verbatim; TraceWorld.glsl's call site does not\n";
    out << "        // combine it with inst.worldPos either). Do not confuse this with\n";
    out << "        // RecipeInstanceBucketing.comp's OWN bucketing/coverage pass, which separately\n";
    out << "        // computes inst.worldPos + bound.center for its screen-space AABB projection --\n";
    out << "        // that is a different, bucketing-only convention. The field function itself\n";
    out << "        // (sdfRecipe_<id>) evaluates in WORLD space directly -- mirrors\n";
    out << "        // traceUberRecipeBody's evalRecipeField(recipeId, p, params) call, which passes\n";
    out << "        // the raw world-space march point with NO per-instance offset subtraction.\n";
    out << "        vec3 boundCenter = vec3(" << f(entry.boundCenter.x) << ", "
        << f(entry.boundCenter.y) << ", " << f(entry.boundCenter.z) << ");\n\n";
    out << "        vec3  oc = rayOrigin - boundCenter;\n";
    out << "        float b  = dot(oc, rayDir);\n";
    out << "        float c  = dot(oc, oc) - meta.boundRadius * meta.boundRadius;\n";
    out << "        float disc = b * b - c;\n";
    out << "        if (disc < 0.0) continue;\n";
    out << "        float sq = sqrt(disc);\n";
    out << "        float tNear = max(-b - sq, 0.0);\n";
    out << "        float tFar  = -b + sq;\n";
    out << "        if (tFar < 0.0 || tNear >= bestT) continue;\n\n";
    out << "        float t = tNear;\n";
    out << "        const int   MAX_STEPS = 128;\n";
    out << "        const float EPS = 1e-3;\n";
    out << "        for (int i = 0; i < MAX_STEPS; ++i) {\n";
    out << "            vec3  p = rayOrigin + rayDir * t;\n";
    out << "            float d = sdfRecipe_" << recipeId << "(p, inst.recipeParams);\n";
    out << "            if (d < EPS) {\n";
    out << "                if (t < bestT) {\n";
    // W2c: the 6-tap central-difference gradient is the ONLY recipe-spliced
    // MATERIAL computation in this kernel — the lean variant
    // (emitGradientNormal=false, used when the bucket-shade pass owns
    // material) skips it entirely and leaves the placeholder normal for the
    // shade kernel to overwrite on the final winner. Everything else in this
    // loop is traversal.
    if (emitGradientNormal) {
        out << "                    const float h = 1e-3;\n";
        out << "                    vec2 e = vec2(h, 0.0);\n";
        out << "                    float gx = sdfRecipe_" << recipeId << "(p + e.xyy, inst.recipeParams) - sdfRecipe_" << recipeId << "(p - e.xyy, inst.recipeParams);\n";
        out << "                    float gy = sdfRecipe_" << recipeId << "(p + e.yxy, inst.recipeParams) - sdfRecipe_" << recipeId << "(p - e.yxy, inst.recipeParams);\n";
        out << "                    float gz = sdfRecipe_" << recipeId << "(p + e.yyx, inst.recipeParams) - sdfRecipe_" << recipeId << "(p - e.yyx, inst.recipeParams);\n";
        out << "                    bestNormal = normalize(vec3(gx, gy, gz));\n";
    }
    out << "                    bestT = t;\n";
    out << "                    bestColor  = inst.color;\n";
    out << "                    bestInstIdx = instIdx;\n";
    out << "                    bestEmission = inst.recipeParams[3];\n";
    out << "                    anyHit = true;\n";
    out << "                }\n";
    out << "                break;\n";
    out << "            }\n";
    out << "            t += d * meta.stepRelaxation;\n";
    out << "            if (t > tFar) break;\n";
    out << "        }\n";
    out << "    }\n\n";
    out << "    if (!anyHit) return;\n\n";
    out << "    uint hitIdx = uint(pixelCoords.y) * pc.screenWidth + uint(pixelCoords.x);\n";
    out << "    // Design doc Sec3.3: plain (non-atomic) read-compare-conditionally-overwrite.\n";
    out << "    // Correct because MultiDispatchNode's default autoBarriers_ serializes this\n";
    out << "    // dispatch against every other pass touching HitRecord (write-after-write\n";
    out << "    // hazard), so no two buckets' dispatches can race on the same pixel.\n";
    out << "    if (bestT < hitRecords[hitIdx].hitT || hitRecords[hitIdx].flags == 0u) {\n";
    out << "        HitRecord rec;\n";
    out << "        rec.albedo = bestColor;\n";
    // W2c precursor fix: match the tier-0 megakernel's record convention exactly
    // (TraceWorld.glsl's procedural branch + BodyInstanceRayMarch.comp:327) — this
    // path used to write roughness=1.0 and a ZEROED _pad0, which (a) rendered hot
    // bodies with different roughness than the SAME recipe class on the tier-0
    // path, (b) broke pixel->instance->recipe resolution (the bucket-shade
    // ownership check and the Cornell diagnostics read _pad0[0] as instIdx), and
    // (c) dropped the winning instance's emission (recipeParams[3], the
    // SpatialReuseShade self-lit term). _pad0[2] stays 0 — the shadow wave's
    // visibility word (HitRecord.glsl's master ledger).
    out << "        rec.roughness = 0.5;\n";
    out << "        rec.worldNormal = bestNormal;\n";
    out << "        rec.hitT = bestT;\n";
    out << "        rec.worldPos = rayOrigin + rayDir * bestT;\n";
    out << "        rec.flags = HITRECORD_FLAG_HIT;\n";
    out << "        rec._pad0 = uint[3](bestInstIdx, floatBitsToUint(bestEmission), 0u);\n";
    out << "        hitRecords[hitIdx] = rec;\n";
    out << "    }\n";
    out << "}\n";

    return out.str();
}

// EmitSpecializedRecipeShadeShader — W2c (wavefront epoch): the per-recipe MATERIAL
// kernel, the bucket-shade half of the traversal/shade register split. Dispatched
// exactly like the march kernel (same indirect command, same rect, same interface —
// kSpecializedRecipeInterfaceGlsl — so the CPU side reuses one descriptor/push
// convention), it runs AFTER every march pass on the same MultiDispatchNode:
//   per rect pixel — bounds, hit gate, OWNERSHIP (the record's _pad0[0] instIdx must
//   belong to THIS recipe; real since the march writes bestInstIdx), then compute the
//   6-tap central-difference gradient at the record's worldPos with the WINNING
//   instance's params and write the material fields. The gradient expression is
//   TOKEN-IDENTICAL to the march's full variant (same h, same e-swizzles, same
//   normalize; worldPos == the march's own rayOrigin + rayDir*bestT), so
//   lean-march + this kernel reproduces the full march's records BIT-EXACTLY —
//   the W2c identity gate. The kernel writes ONLY the material fields
//   (albedo/roughness/worldNormal/_pad0[1] emission) — the boundary contract.
inline std::string EmitSpecializedRecipeShadeShader(
    const RecipeRegistry::RecipeEntry& entry,
    uint32_t recipeId,
    const std::string& sdfCoreKernelsGlsl)
{
    std::string fieldFn = TryGetKernelEmittedRecipeGlsl(recipeId);
    const bool kernelEmitted = !fieldFn.empty();
    if (!kernelEmitted) {
        fieldFn = EmitProceduralFieldFunctionGlsl(
            entry.bytecode.data(), static_cast<uint32_t>(entry.bytecode.size()), recipeId);
    }
    std::cout << "[BucketShade] recipeId=" << recipeId << " field function: "
               << (kernelEmitted ? "kernel-emitted" : "local") << std::endl;

    std::ostringstream out;
    out << "#version 460\n\n";
    out << "// W2c wavefront: specialized single-recipe MATERIAL (bucket-shade) kernel\n";
    out << "// for recipeId " << recipeId << ". Machine-generated by\n";
    out << "// EmitSpecializedRecipeShadeShader -- do not hand-edit.\n\n";
    out << "layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;\n\n";
    out << sdfCoreKernelsGlsl << "\n\n";
    out << fieldFn << "\n";
    out << kSpecializedRecipeInterfaceGlsl;
    out << "void main() {\n";
    out << "    BucketMeta meta = bucketMeta[pc.recipeId];\n";
    out << "    ivec2 rectCoord = ivec2(gl_GlobalInvocationID.xy);\n";
    out << "    ivec2 pixelCoords = rectCoord + ivec2(int(meta.rectMinX), int(meta.rectMinY));\n";
    out << "    if (pixelCoords.x >= int(pc.screenWidth) || pixelCoords.y >= int(pc.screenHeight)) return;\n";
    out << "    if (pixelCoords.x < 0 || pixelCoords.y < 0) return;\n\n";
    out << "    uint hitIdx = uint(pixelCoords.y) * pc.screenWidth + uint(pixelCoords.x);\n";
    out << "    HitRecord rec = hitRecords[hitIdx];\n";
    out << "    if ((rec.flags & HITRECORD_FLAG_HIT) == 0u) return;\n\n";
    out << "    uint instIdx = rec._pad0[0];\n";
    out << "    if (instIdx >= bodyInstances.length()) return;\n";
    out << "    BodyInstance inst = bodyInstances[instIdx];\n";
    out << "    if (inst.recipeId != pc.recipeId) return;\n\n";
    out << "    // The march's own gradient, verbatim, at the record's hit point (the same\n";
    out << "    // rayOrigin + rayDir*bestT float the march evaluated at) with the winning\n";
    out << "    // instance's params -- bit-equal normal by construction.\n";
    out << "    vec3 p = rec.worldPos;\n";
    out << "    const float h = 1e-3;\n";
    out << "    vec2 e = vec2(h, 0.0);\n";
    out << "    float gx = sdfRecipe_" << recipeId << "(p + e.xyy, inst.recipeParams) - sdfRecipe_" << recipeId << "(p - e.xyy, inst.recipeParams);\n";
    out << "    float gy = sdfRecipe_" << recipeId << "(p + e.yxy, inst.recipeParams) - sdfRecipe_" << recipeId << "(p - e.yxy, inst.recipeParams);\n";
    out << "    float gz = sdfRecipe_" << recipeId << "(p + e.yyx, inst.recipeParams) - sdfRecipe_" << recipeId << "(p - e.yyx, inst.recipeParams);\n\n";
    out << "    hitRecords[hitIdx].albedo      = inst.color;\n";
    out << "    hitRecords[hitIdx].roughness   = 0.5;\n";
    out << "    hitRecords[hitIdx].worldNormal = normalize(vec3(gx, gy, gz));\n";
    out << "    hitRecords[hitIdx]._pad0[1]    = floatBitsToUint(inst.recipeParams[3]);\n";
    out << "}\n";

    return out.str();
}

} // namespace Vixen::SVO::Recipe
