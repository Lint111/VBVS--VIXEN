#pragma once
#include "Recipe/SdfInstruction.h"
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

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

    // emit-time position stack: mirrors pos/posStack in evalRecipe (C# VM ctx.Pos/PosStack)
    std::string curPos = "p";
    std::vector<std::string> posSaveStk;

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
        switch (static_cast<SdfOpCode>(in.opCode)) {
            case SdfOpCode::Sphere: {
                // data[0..2] = center xyz, data[3] = radius (mirrors SdfRecipeEval.h)
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_Sphere(" + curPos + ", float3("
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2])
                    + "), " + f(in.data[3]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::Box: {
                // data[0..2] = halfExtents xyz (mirrors SdfRecipeEval.h)
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_Box(" + curPos + ", float3("
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + "));\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::Union: {
                assert(stk.size() >= 2 && "Union: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_Union(" + a + ", " + b + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::SmoothUnion: {
                // data[2] = k (Data0.z), mirrors evalRecipe
                assert(stk.size() >= 2 && "SmoothUnion: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_SmoothUnion(" + a + ", " + b
                    + ", " + f(in.data[2]) + ");\n";
                stk.push_back(t);
                break;
            }
            // --- Binary CSG (non-smooth) ---
            case SdfOpCode::Subtract: {           // non-commutative: A minus B
                assert(stk.size() >= 2 && "Subtract: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_Subtract(" + a + ", " + b + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::Intersect: {
                assert(stk.size() >= 2 && "Intersect: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_Intersect(" + a + ", " + b + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::Xor: {
                assert(stk.size() >= 2 && "Xor: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_Xor(" + a + ", " + b + ");\n";
                stk.push_back(t);
                break;
            }
            // --- Binary CSG (smooth linear): k = data[2] ---
            case SdfOpCode::SmoothSubtract: {     // non-commutative
                assert(stk.size() >= 2 && "SmoothSubtract: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_SmoothSubtract(" + a + ", " + b
                    + ", " + f(in.data[2]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::SmoothIntersect: {
                assert(stk.size() >= 2 && "SmoothIntersect: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_SmoothIntersect(" + a + ", " + b
                    + ", " + f(in.data[2]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::SmoothMax: {
                assert(stk.size() >= 2 && "SmoothMax: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_SmoothMax(" + a + ", " + b
                    + ", " + f(in.data[2]) + ");\n";
                stk.push_back(t);
                break;
            }
            // --- Binary CSG (smooth cubic): k = data[2] ---
            case SdfOpCode::SmoothUnionCubic: {
                assert(stk.size() >= 2 && "SmoothUnionCubic: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_SmoothUnionCubic(" + a + ", " + b
                    + ", " + f(in.data[2]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::SmoothSubtractCubic: { // non-commutative
                assert(stk.size() >= 2 && "SmoothSubtractCubic: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_SmoothSubtractCubic(" + a + ", " + b
                    + ", " + f(in.data[2]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::SmoothIntersectCubic: {
                assert(stk.size() >= 2 && "SmoothIntersectCubic: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_SmoothIntersectCubic(" + a + ", " + b
                    + ", " + f(in.data[2]) + ");\n";
                stk.push_back(t);
                break;
            }
            // --- Unary modifiers: pop-and-replace (TOS), radius/thickness = data[0] ---
            case SdfOpCode::Round: {
                assert(!stk.empty() && "Round: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_Round(" + a + ", " + f(in.data[0]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::Onion: {
                assert(!stk.empty() && "Onion: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_Onion(" + a + ", " + f(in.data[0]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MirrorX: {
                std::string pN = "pp" + std::to_string(n++);
                body += "  float3 " + pN + " = SdfCore_MirrorX(" + curPos + ");\n";
                posSaveStk.push_back(curPos); curPos = pN;
                break;
            }
            case SdfOpCode::RestorePos: {
                assert(!posSaveStk.empty() && "RestorePos: emit-time position stack underflow");
                curPos = posSaveStk.back(); posSaveStk.pop_back();
                break;
            }
            default:
                break;
        }
    }

    assert(!stk.empty() && "EmitProceduralComputeShader: empty value stack at return");
    std::string recipe =
        "float sdfRecipe(float3 p) {\n"
        + body
        + "  return " + stk.back() + ";\n"
        "}\n";

    return sdfCoreHlsl + "\n" + recipe + "\n" + kTraceMain;
}

} // namespace Vixen::SVO::Recipe
