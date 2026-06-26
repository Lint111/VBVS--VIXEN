#pragma once
#include "Recipe/SdfInstruction.h"
#include <string>
#include <vector>
#include <cstdint>

namespace Vixen::SVO::Recipe {

// Fixed trace-main template — concatenated after sdfCoreHlsl + sdfRecipe.
// Uses cbuffer + RWTexture2D; validated to compile via glslang HLSL frontend.
static constexpr const char* kTraceMain = R"(
// [[vk::push_constant]] maps cbuffer to Vulkan push constants (no descriptor binding).
// Without this, cbuffer PC : register(b0) aliases binding 0 with outImg -> pipeline validation error.
[[vk::push_constant]]
cbuffer PC {
    float3 camPos;  float _p0;
    float3 camDir;  float fov;
    float3 camUp;   float aspect;
    float3 camRight; int _p1;
};
RWTexture2D<float4> outImg : register(u0);

float3 gradN(float3 p) {
    float h = 1e-3;
    return normalize(float3(
        sdfRecipe(p + float3(h, 0, 0)) - sdfRecipe(p - float3(h, 0, 0)),
        sdfRecipe(p + float3(0, h, 0)) - sdfRecipe(p - float3(0, h, 0)),
        sdfRecipe(p + float3(0, 0, h)) - sdfRecipe(p - float3(0, 0, h))));
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint w, h;
    outImg.GetDimensions(w, h);
    if (id.x >= w || id.y >= h) return;

    float2 uv = (float2(id.xy) + 0.5) / float2(w, h) * 2.0 - 1.0;
    uv.x *= aspect;
    float tanF = tan(radians(fov * 0.5));
    float3 rd = normalize(camDir + uv.x * tanF * camRight + uv.y * tanF * camUp);

    float t = 0.0;
    float3 col = float3(0.02, 0.02, 0.05);
    for (int i = 0; i < 160; i++) {
        float3 p = camPos + rd * t;
        float d = sdfRecipe(p);
        if (d < 1e-3) {
            float3 n = gradN(p);
            float diff = saturate(dot(n, normalize(float3(0.4, 0.7, 0.5))));
            col = float3(0.2, 0.6, 0.3) * (0.2 + 0.8 * diff);
            break;
        }
        t += d;
        if (t > 200.0) break;
    }
    outImg[id.xy] = float4(col, 1.0);
}
)";

// Emit a specialised all-HLSL compute shader from an SdfInstruction program.
// The recipe is compiled in as literals (compile realization — no GPU interpreter).
// Stack simulation at emit time produces straight-line HLSL (no for-loop over instructions).
//
// Return value: sdfCoreHlsl + sdfRecipe (straight-line) + trace main ready for ShaderCompiler.
inline std::string EmitProceduralComputeShader(
    const SdfInstruction* prog,
    uint32_t count,
    const std::string& sdfCoreHlsl)
{
    std::vector<std::string> stk;
    std::string body;
    int n = 0;

    // ponytail: std::to_string for floats — sufficient for HLSL literals; no locale issues
    auto f = [](float v) {
        // Ensure a decimal point so HLSL sees it as a float literal.
        std::string s = std::to_string(v);
        if (s.find('.') == std::string::npos && s.find('e') == std::string::npos)
            s += ".0";
        return s;
    };

    for (uint32_t i = 0; i < count; ++i) {
        const SdfInstruction& in = prog[i];
        std::string t = "t" + std::to_string(n++);

        switch (static_cast<SdfOpCode>(in.opCode)) {
            case SdfOpCode::Sphere: {
                // data[0..2] = center xyz, data[3] = radius (mirrors SdfRecipeEval.h)
                body += "  float " + t + " = SdfCore_Sphere(p, float3("
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2])
                    + "), " + f(in.data[3]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::Union: {
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                body += "  float " + t + " = SdfCore_Union(" + a + ", " + b + ");\n";
                stk.push_back(t);
                break;
            }
            default:
                break;
        }
    }

    std::string recipe =
        "float sdfRecipe(float3 p) {\n"
        + body
        + "  return " + stk.back() + ";\n"
        "}\n";

    return sdfCoreHlsl + "\n" + recipe + "\n" + kTraceMain;
}

} // namespace Vixen::SVO::Recipe
