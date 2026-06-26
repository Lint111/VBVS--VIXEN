# P2.2 — Live Procedural Render (compile realization) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development / executing-plans. Checkbox steps. **VIXEN-only**, branch `feat/sdf-recipe-codegen-p2` (continues the P2 line). Reuses P0's HLSL ingestion + the generated `SdfCore_*` HLSL.

**Goal:** Render a recipe **live** (no bake, no voxels) via the *compile realization*: emit a specialized all-HLSL compute shader from an `SdfInstruction[]` (recipe primitives/params unrolled as literals, calling the generated `SdfCore_*`), compile it through `ShaderCompiler` (HLSL→SPIR-V), sphere-trace it, and render on lavapipe — the "Procedural (compiled)" provider.

**Architecture:** A VIXEN-side **instruction→HLSL emitter** simulates the recipe's stack at emit time and produces straight-line HLSL (`float t0=SdfCore_Sphere(...); … return tN;`), concatenated with the vendored `SdfCoreKernels.g.hlsl` preamble and a fixed sphere-trace compute `main`. The recipe is compiled **into** the shader (params as literals) — specialized + fast; structural changes recompile. A standalone 1-binding (storage-image) compute harness dispatches it on lavapipe. No instruction SSBO, no GPU VM (that's deferred).

**Tech Stack:** C++23, GoogleTest, glslang (HLSL frontend), Vulkan 1.3 / lavapipe, glm.

## Global Constraints

- **All-HLSL compute** (one source language per glslang compile): `SdfCore_*` (HLSL) + emitted `sdfRecipe` (HLSL) + the trace `main` (HLSL), compiled via `ShaderCompiler` `sourceLanguage=HLSL` (the P0 path). No mixing GLSL.
- **Recipe compiled in as literals** (compile realization) — no instruction buffer; params are emitted constants. Param/structure change → recompile (acceptable; the GPU VM that avoids recompile is deferred).
- **⚠ lavapipe compute gotcha:** storage-image writes need **Vulkan 1.3** + the **`shaderStorageImageWriteWithoutFormat`** device feature enabled, else the write silently no-ops. Mirror the existing render test's device creation (it enables this).
- **Reuse, don't perturb:** the existing GLSL stored/binary render path + P2.1 bake path are untouched. P2.2 is additive (new emitter + new standalone compute test).
- **Live gate authoritative:** lavapipe offscreen + controller reads the PNG.
- **Repo:** VIXEN worktree `/mnt/c/cpp/VBVS--VIXEN/.claude/worktrees/sdf-recipe-codegen-p0`, branch `feat/sdf-recipe-codegen-p2`. Build `cmake --preset vixen-wsl` (reuse existing `build-wsl`; ctest discovery is stale — run binaries directly).

## File Structure

**Create:**
- `VIXEN/libraries/SVO/include/Recipe/SdfRecipeCodegen.h` — header-only emitter: `EmitProceduralComputeShader(prog, count, sdfCoreHlsl) -> std::string`.
- `VIXEN/libraries/SVO/tests/test_recipe_codegen.cpp` — M1: emit straight-line HLSL + compile to valid SPIR-V (ShaderCompiler HLSL).
- `VIXEN/libraries/RenderGraph/tests/Nodes/test_procedural_recipe_render.cpp` — M2: standalone 1-binding compute, dispatch on lavapipe, readback, PNG.

**Modify:** `VIXEN/libraries/SVO/tests/CMakeLists.txt` (register `test_recipe_codegen`, link `ShaderManagement` + pass the `SDF_CORE_KERNELS_HLSL_PATH` compile-def as P0's test does); `VIXEN/libraries/RenderGraph/tests/.../CMakeLists.txt` (register `test_procedural_recipe_render`).

---

## Milestone Map

> Persisted for the context-manager pipeline (2026-06-26). Two milestones, sequential. VIXEN-only, branch `feat/sdf-recipe-codegen-p2`.

- [ ] **M1 `[VIXEN]` — Instruction→HLSL emitter + compile gate (Task 1).** CPU/compile, low-risk. Gate: `test_recipe_codegen` green — emits straight-line `sdfRecipe` for sphere∪sphere + the full shader compiles to valid SPIR-V via the HLSL path. Implementer **Sonnet**.
- [ ] **M2 `[VIXEN]` — Standalone compute sphere-trace render (Tasks 2–3).** Gate: `RenderProceduralRecipe` renders a recipe SOLID on lavapipe (bodyPixels > threshold) + controller/validator reads the PNG (smooth procedural peanut); existing render tests untouched. Implementer **Sonnet**.

Validators: **Opus** per milestone (M2 reads the PNG + tamper-checks). Controller: Opus, thin.

## Progress Log

_(controller appends one line per completed milestone)_

---

## Task 1 [M1]: Instruction→HLSL emitter + compile gate

**Files:** Create `VIXEN/libraries/SVO/include/Recipe/SdfRecipeCodegen.h`, `VIXEN/libraries/SVO/tests/test_recipe_codegen.cpp`; modify `…/SVO/tests/CMakeLists.txt`.

**Interfaces:**
- Consumes: `Recipe::SdfInstruction`/`SdfOpCode` + the opcode semantics mirrored from `Recipe::evalRecipe` (Sphere: `SdfCore_Sphere(p, center, radius)`; Union: `SdfCore_Union(a,b)`).
- Produces: `std::string Vixen::SVO::Recipe::EmitProceduralComputeShader(const SdfInstruction* prog, uint32_t count, const std::string& sdfCoreHlsl)`.

- [ ] **Step 1: Write the emitter** (header-only). Generate `sdfRecipe` by **simulating the stack at emit time** (mirror `evalRecipe`): keep a `std::vector<std::string>` of temp names; `Sphere` → emit `float tN = SdfCore_Sphere(p, float3(cx,cy,cz), r);` push `tN`; `Union` → pop b,a, emit `float tM = SdfCore_Union(a,b);` push `tM`; final `return <top>;`. Then assemble: `sdfCoreHlsl` + the generated `sdfRecipe` + the fixed trace `main` template:
```hlsl
// (concatenated after sdfCoreHlsl + sdfRecipe)
cbuffer PC : register(b0) { float3 camPos; float _p0; float3 camDir; float fov;
                            float3 camUp; float aspect; float3 camRight; int _p1; };
RWTexture2D<float4> outImg : register(u0);
float3 gradN(float3 p){ float h=1e-3;
  return normalize(float3(
    sdfRecipe(p+float3(h,0,0))-sdfRecipe(p-float3(h,0,0)),
    sdfRecipe(p+float3(0,h,0))-sdfRecipe(p-float3(0,h,0)),
    sdfRecipe(p+float3(0,0,h))-sdfRecipe(p-float3(0,0,h)))); }
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID){
  uint w,h; outImg.GetDimensions(w,h); if(id.x>=w||id.y>=h) return;
  float2 uv=(float2(id.xy)+0.5)/float2(w,h)*2.0-1.0; uv.x*=aspect;
  float tanF=tan(radians(fov*0.5));
  float3 rd=normalize(camDir+uv.x*tanF*camRight+uv.y*tanF*camUp);
  float t=0.0; float3 col=float3(0.02,0.02,0.05);
  for(int i=0;i<160;i++){ float3 p=camPos+rd*t; float d=sdfRecipe(p);
    if(d<1e-3){ float3 n=gradN(p); float diff=saturate(dot(n,normalize(float3(0.4,0.7,0.5))));
      col=float3(0.2,0.6,0.3)*(0.2+0.8*diff); break; }
    t+=d; if(t>200.0) break; }
  outImg[id.xy]=float4(col,1.0);
}
```
  Header sketch:
```cpp
#pragma once
#include "Recipe/SdfInstruction.h"
#include <string>#include <vector>#include <cstdint>
namespace Vixen::SVO::Recipe {
inline std::string EmitProceduralComputeShader(const SdfInstruction* prog, uint32_t count, const std::string& sdfCoreHlsl){
    std::vector<std::string> stk; std::string body; int n=0;
    auto f=[](float v){ return std::to_string(v); };
    for(uint32_t i=0;i<count;++i){ const auto& in=prog[i]; std::string t="t"+std::to_string(n++);
      switch(static_cast<SdfOpCode>(in.opCode)){
        case SdfOpCode::Sphere: body+="  float "+t+" = SdfCore_Sphere(p, float3("+f(in.data[0])+","+f(in.data[1])+","+f(in.data[2])+"), "+f(in.data[3])+");\n"; stk.push_back(t); break;
        case SdfOpCode::Union: { auto b=stk.back(); stk.pop_back(); auto a=stk.back(); stk.pop_back();
          body+="  float "+t+" = SdfCore_Union("+a+", "+b+");\n"; stk.push_back(t);} break;
        default: break; } }
    std::string recipe="float sdfRecipe(float3 p){\n"+body+"  return "+stk.back()+";\n}\n";
    return sdfCoreHlsl + "\n" + recipe + "\n" + /* trace-main template above */ kTraceMain;
}
}
```
- [ ] **Step 2: Write the compile-gate test** — read the vendored `SdfCoreKernels.g.hlsl` (via the `SDF_CORE_KERNELS_HLSL_PATH` compile-def, as `test_hlsl_ingestion.cpp` does), emit the shader for a sphere∪sphere recipe, compile with `ShaderCompiler` (`sourceLanguage=HLSL`, `validateSpirv=true`):
```cpp
TEST(RecipeCodegen, EmitsCompilableProceduralShader){
  std::string core = ReadFile(SDF_CORE_KERNELS_HLSL_PATH);
  Recipe::SdfInstruction prog[] = { sphere({-1,0,0},1), sphere({1,0,0},1), unionOp() };
  std::string src = Recipe::EmitProceduralComputeShader(prog, 3, core);
  // straight-line emitted (not a loop/interpreter):
  EXPECT_NE(src.find("SdfCore_Sphere(p, float3(-1"), std::string::npos);
  EXPECT_NE(src.find("SdfCore_Union(t0, t1)"), std::string::npos);
  ShaderManagement::ShaderCompiler c; ShaderManagement::CompilationOptions o;
  o.sourceLanguage = ShaderManagement::CompilationOptions::SourceLanguage::HLSL; o.validateSpirv=true;
  auto out = c.Compile(ShaderManagement::ShaderStage::Compute, src, "main", o);
  ASSERT_TRUE(out.success) << out.GetFullLog(); EXPECT_FALSE(out.spirv.empty());
}
```
- [ ] **Step 3: Run → iterate to green.** `cmake --build build-wsl --target test_recipe_codegen && ./build-wsl/libraries/SVO/tests/test_recipe_codegen --gtest_brief=1`. If glslang's HLSL frontend rejects any construct (e.g. `RWTexture2D`/`GetDimensions`/`cbuffer`), adjust the trace-main to the glslang-HLSL-accepted form until it compiles to valid SPIR-V.
- [ ] **Step 4: Commit** `feat(recipe): instruction→HLSL procedural emitter + compile gate (P2.2 M1)`.

## Task 2 [M2]: Standalone compute sphere-trace render harness

**Files:** Create `VIXEN/libraries/RenderGraph/tests/Nodes/test_procedural_recipe_render.cpp`; modify its CMakeLists.

- [ ] **Step 1: Build the fixture** — reuse the device/instance/pool/lavapipe bring-up from `test_body_instance_raymarch_render.cpp` (copy the fixture setup incl. **`shaderStorageImageWriteWithoutFormat` feature enable + Vulkan 1.3** — without it the storage-image write silently no-ops), and its `FindMemoryType`/image-create/host-buffer/readback helpers. **Do NOT reuse the 11-binding octree descriptor layout** — create a minimal one: **binding 0 = `RWTexture2D` storage image (R8G8B8A8_UNORM)** + a **push-constant range** (the camera block matching the emitter's `cbuffer PC` — keep the 76-byte layout + a `MakeCamera` copy).
- [ ] **Step 2: Write the render test** — emit the shader (`EmitProceduralComputeShader` for a sphere∪sphere peanut in world space, e.g. spheres at ±2 on X, r≈2.5), compile via `ShaderCompiler` HLSL → `vkCreateShaderModule` → compute pipeline (raw Vulkan, mirror the render test's module/pipeline creation), bind the storage image, push the camera (frame on the recipe's center/extent), dispatch `ceil(W/8)×ceil(H/8)`, barrier, copy image→host buffer, write `/tmp/glsl_recipe_procedural.png`. Assert `bodyPixels > 20000` (a body rendered). **Controller reads the PNG** to confirm a SMOOTH procedural two-lobe (peanut) — distinct from P2.1's voxel-baked one (this one is analytic, no brick stepping).
- [ ] **Step 3: Build + run on lavapipe.** `cmake --build build-wsl --target test_procedural_recipe_render`, run the binary with the lavapipe env (`VK_ICD_FILENAMES=…/lvp_icd.json VK_LAYER_PATH=…`). Expect PASS + the PNG.

## Task 3 [M2]: No-regression + commit

- [ ] **Step 1:** Existing render tests untouched: run `*RenderStoredSdfBodiesNoHoles*` + `*RenderRecipeBakedBody*` (lavapipe) → green (P2.2 is additive; it added no shared-state changes).
- [ ] **Step 2:** `test_recipe_codegen` + `test_recipe_bake` green.
- [ ] **Step 3: Commit** `feat(recipe): live procedural compute render on lavapipe — compile realization (P2.2 M2)`.

## Self-Review

**Coverage:** emitter → T1; live compute render → T2; no-regression → T3. The compile-realization claim (recipe→HLSL→SPIR-V→live sphere-trace) is proven by T1 (compiles) + T2 (renders). ✓
**Placeholders:** the emitter + its test are concrete; the trace-main HLSL template is given verbatim (adjust only if glslang rejects a construct, T1 Step 3); M2's dispatch is copy-adapt from the render test with the exact reuse points named. No "TODO". ✓
**Type consistency:** `EmitProceduralComputeShader`, `SdfInstruction`/`SdfOpCode`, the `cbuffer PC`↔push-constant layout, binding 0 = storage image — consistent T1↔T2. ✓
**Risk:** glslang HLSL frontend on the compute `main` (RWTexture2D/GetDimensions/cbuffer) — de-risked by T1's compile gate before any GPU work; the lavapipe storage-image-feature gotcha is called out in Global Constraints + T2 Step 1.

## Execution Handoff

Run via post-brainstorm-context-manager (2 milestones). M1 Sonnet+Opus (emit + compile gate, tamper-check the straight-line emit). M2 Sonnet+Opus (lavapipe live render — validator AND controller read the PNG).
