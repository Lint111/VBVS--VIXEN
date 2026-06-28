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
    // emit-time distScale stack: 1.0f for non-scaling transforms; M4b Transform pushes data[11]
    // At RestorePos: if |scale-1|>1e-4 emit a multiply to scale the TOS distance before popping.
    std::vector<float> distScaleSaveStk;

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
        assert(in.paramMask == 0 && "ParamMask!=0 deferred to P4");
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
            // --- Leaf primitives (no-position, pos-off=NO) — P2.4 M3b-1 ---
            case SdfOpCode::Capsule: {
                // data[0]=halfHeight, data[1]=radius
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_Capsule(" + curPos + ", "
                    + f(in.data[0]) + ", " + f(in.data[1]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::Cylinder: {
                // data[0]=halfHeight, data[1]=radius
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_Cylinder(" + curPos + ", "
                    + f(in.data[0]) + ", " + f(in.data[1]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::Torus: {
                // data[0]=majorRadius, data[1]=minorRadius
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_Torus(" + curPos + ", "
                    + f(in.data[0]) + ", " + f(in.data[1]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::BoxRounded: {
                // data[0..2]=halfExtents, data[3]=rounding
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_BoxRounded(" + curPos + ", float3("
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + "), "
                    + f(in.data[3]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::Plane: {
                // data[0..2]=normal, data[3]=distance
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_Plane(" + curPos + ", float3("
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + "), "
                    + f(in.data[3]) + ");\n";
                stk.push_back(t);
                break;
            }
            // --- Leaf primitives (position-offset, pos-off=YES) — P2.4 M3b-2 ---
            // Offset baked into position expression: curPos + " - float3(dx,dy,dz)"
            case SdfOpCode::Ellipsoid: {
                // data[0..2]=radii, data[4..6]=position
                std::string t = "t" + std::to_string(n++);
                std::string q = curPos + " - float3(" + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + ")";
                body += "  float " + t + " = SdfCore_Ellipsoid(" + q + ", float3("
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + "));\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::HollowCylinder: {
                // data[0]=halfLen, data[1]=outerR, data[2]=wall, data[4..6]=position
                std::string t = "t" + std::to_string(n++);
                std::string q = curPos + " - float3(" + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + ")";
                body += "  float " + t + " = SdfCore_HollowCylinder(" + q + ", "
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::TaperedCylinder: {
                // data[0]=height, data[1]=r1 (base), data[2]=r2 (top), data[4..6]=position
                std::string t = "t" + std::to_string(n++);
                std::string q = curPos + " - float3(" + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + ")";
                body += "  float " + t + " = SdfCore_TaperedCylinder(" + q + ", "
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::Cone: {
                // data[0]=sinAngle, data[1]=cosAngle, data[2]=height, data[4..6]=position
                std::string t = "t" + std::to_string(n++);
                std::string q = curPos + " - float3(" + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + ")";
                body += "  float " + t + " = SdfCore_Cone(" + q + ", float2("
                    + f(in.data[0]) + ", " + f(in.data[1]) + "), " + f(in.data[2]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::CappedTorus: {
                // data[0]=sinA, data[1]=cosA, data[2]=majorR, data[3]=minorR, data[4..6]=position
                std::string t = "t" + std::to_string(n++);
                std::string q = curPos + " - float3(" + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + ")";
                body += "  float " + t + " = SdfCore_CappedTorus(" + q + ", float2("
                    + f(in.data[0]) + ", " + f(in.data[1]) + "), " + f(in.data[2]) + ", " + f(in.data[3]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::Link: {
                // data[0]=halfLen, data[1]=majorR, data[2]=minorR, data[4..6]=position
                std::string t = "t" + std::to_string(n++);
                std::string q = curPos + " - float3(" + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + ")";
                body += "  float " + t + " = SdfCore_Link(" + q + ", "
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + ");\n";
                stk.push_back(t);
                break;
            }
            // Panel/Plank/RoundedBox: positioned BoxRounded (same math, opcode differs)
            case SdfOpCode::Panel: {
                // data[0..2]=halfExtents, data[3]=rounding, data[4..6]=position
                std::string t = "t" + std::to_string(n++);
                std::string q = curPos + " - float3(" + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + ")";
                body += "  float " + t + " = SdfCore_BoxRounded(" + q + ", float3("
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + "), "
                    + f(in.data[3]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::Plank: {
                // data[0..2]=halfExtents, data[3]=rounding, data[4..6]=position
                std::string t = "t" + std::to_string(n++);
                std::string q = curPos + " - float3(" + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + ")";
                body += "  float " + t + " = SdfCore_BoxRounded(" + q + ", float3("
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + "), "
                    + f(in.data[3]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::RoundedBox: {
                // data[0..2]=halfExtents, data[3]=rounding, data[4..6]=position
                std::string t = "t" + std::to_string(n++);
                std::string q = curPos + " - float3(" + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + ")";
                body += "  float " + t + " = SdfCore_BoxRounded(" + q + ", float3("
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + "), "
                    + f(in.data[3]) + ");\n";
                stk.push_back(t);
                break;
            }
            // --- Leaf primitives — P2.4 M3b-3: prism + cone family ---
            case SdfOpCode::RoundCone: {
                // data[0]=r1, data[1]=r2, data[2]=height, data[4..6]=position
                std::string t = "t" + std::to_string(n++);
                std::string q = curPos + " - float3(" + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + ")";
                body += "  float " + t + " = SdfCore_RoundCone(" + q + ", "
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::FakeRoundCone: {
                // data[0]=r1, data[1]=r2, data[2]=height, data[4..6]=position
                std::string t = "t" + std::to_string(n++);
                std::string q = curPos + " - float3(" + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + ")";
                body += "  float " + t + " = SdfCore_FakeRoundCone(" + q + ", "
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::Segment: {
                // data[0..2]=pointA, data[3]=radius, data[4..6]=pointB — samples curPos directly (no offset)
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_Segment(" + curPos + ", float3("
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + "), float3("
                    + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + "), "
                    + f(in.data[3]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::TriangularPrism: {
                // data[0]=h.x, data[1]=h.y, data[4..6]=position
                std::string t = "t" + std::to_string(n++);
                std::string q = curPos + " - float3(" + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + ")";
                body += "  float " + t + " = SdfCore_TriangularPrism(" + q + ", float2("
                    + f(in.data[0]) + ", " + f(in.data[1]) + "));\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::Pyramid: {
                // data[0]=height, data[4..6]=position
                std::string t = "t" + std::to_string(n++);
                std::string q = curPos + " - float3(" + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + ")";
                body += "  float " + t + " = SdfCore_Pyramid(" + q + ", " + f(in.data[0]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::HexPrism: {
                // data[0]=h.x (hex radius), data[1]=h.y (half-height), data[4..6]=position
                std::string t = "t" + std::to_string(n++);
                std::string q = curPos + " - float3(" + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + ")";
                body += "  float " + t + " = SdfCore_HexPrism(" + q + ", float2("
                    + f(in.data[0]) + ", " + f(in.data[1]) + "));\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MirrorX: {
                std::string pN = "pp" + std::to_string(n++);
                body += "  float3 " + pN + " = SdfCore_MirrorX(" + curPos + ");\n";
                posSaveStk.push_back(curPos); distScaleSaveStk.push_back(1.0f); curPos = pN;
                break;
            }
            case SdfOpCode::MirrorY: {
                std::string pN = "pp" + std::to_string(n++);
                body += "  float3 " + pN + " = SdfCore_MirrorY(" + curPos + ");\n";
                posSaveStk.push_back(curPos); distScaleSaveStk.push_back(1.0f); curPos = pN;
                break;
            }
            case SdfOpCode::MirrorZ: {
                std::string pN = "pp" + std::to_string(n++);
                body += "  float3 " + pN + " = SdfCore_MirrorZ(" + curPos + ");\n";
                posSaveStk.push_back(curPos); distScaleSaveStk.push_back(1.0f); curPos = pN;
                break;
            }
            case SdfOpCode::Elongate: {
                // data[0..2] = elongation h
                std::string pN = "pp" + std::to_string(n++);
                body += "  float3 " + pN + " = SdfCore_Elongate(" + curPos + ", float3("
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + "));\n";
                posSaveStk.push_back(curPos); distScaleSaveStk.push_back(1.0f); curPos = pN;
                break;
            }
            case SdfOpCode::Revolution: {
                // data[0]=offset, data[4..6]=center
                std::string pN = "pp" + std::to_string(n++);
                body += "  float3 " + pN + " = SdfCore_Revolution(" + curPos + ", float3("
                    + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + "), "
                    + f(in.data[0]) + ");\n";
                posSaveStk.push_back(curPos); distScaleSaveStk.push_back(1.0f); curPos = pN;
                break;
            }
            case SdfOpCode::Transform: {
                // data[0..2]=translation, data[4..7]=invRot xyzw, data[8..10]=invScale, data[11]=distScale
                std::string pN = "pp" + std::to_string(n++);
                body += "  float3 " + pN + " = SdfCore_Transform(" + curPos + ", float3("
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + "), float4("
                    + f(in.data[4]) + ", " + f(in.data[5]) + ", " + f(in.data[6]) + ", " + f(in.data[7]) + "), float3("
                    + f(in.data[8]) + ", " + f(in.data[9]) + ", " + f(in.data[10]) + "));\n";
                posSaveStk.push_back(curPos); distScaleSaveStk.push_back(in.data[11]); curPos = pN;
                break;
            }
            case SdfOpCode::Twist: {
                // data[0]=k
                std::string pN = "pp" + std::to_string(n++);
                body += "  float3 " + pN + " = SdfCore_Twist(" + curPos + ", " + f(in.data[0]) + ");\n";
                posSaveStk.push_back(curPos); distScaleSaveStk.push_back(1.0f); curPos = pN;
                break;
            }
            case SdfOpCode::Bend: {
                // data[0]=k
                std::string pN = "pp" + std::to_string(n++);
                body += "  float3 " + pN + " = SdfCore_Bend(" + curPos + ", " + f(in.data[0]) + ");\n";
                posSaveStk.push_back(curPos); distScaleSaveStk.push_back(1.0f); curPos = pN;
                break;
            }
            case SdfOpCode::RepeatInfinite: {
                // data[0..2]=spacing xyz
                std::string pN = "pp" + std::to_string(n++);
                body += "  float3 " + pN + " = SdfCore_RepeatInfinite(" + curPos + ", float3("
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + "));\n";
                posSaveStk.push_back(curPos); distScaleSaveStk.push_back(1.0f); curPos = pN;
                break;
            }
            case SdfOpCode::RepeatLimited: {
                // data[0]=spacing scalar, data[1..3]=limit xyz
                std::string pN = "pp" + std::to_string(n++);
                body += "  float3 " + pN + " = SdfCore_RepeatLimited(" + curPos + ", "
                    + f(in.data[0]) + ", float3("
                    + f(in.data[1]) + ", " + f(in.data[2]) + ", " + f(in.data[3]) + "));\n";
                posSaveStk.push_back(curPos); distScaleSaveStk.push_back(1.0f); curPos = pN;
                break;
            }
            case SdfOpCode::RestorePos: {
                assert(!posSaveStk.empty() && "RestorePos: emit-time position stack underflow");
                assert(!distScaleSaveStk.empty() && "RestorePos: emit-time distScale stack underflow");
                float scale = distScaleSaveStk.back(); distScaleSaveStk.pop_back();
                if (std::abs(scale - 1.0f) > 1e-4f) {
                    // M4b: Transform pushes a non-unit scale; emit the multiply into the TOS distance.
                    assert(!stk.empty() && "RestorePos: value stack empty when applying distScale");
                    std::string sN = "t" + std::to_string(n++);
                    body += "  float " + sN + " = " + stk.back() + " * " + f(scale) + ";\n";
                    stk.back() = sN;
                }
                curPos = posSaveStk.back(); posSaveStk.pop_back();
                break;
            }
            // ── M4c: value-math lane (emit mirrors eval) ─────────────────────────
            // Unary (pop-and-replace TOS)
            case SdfOpCode::MathSin: {
                assert(!stk.empty() && "MathSin: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathSin(" + a + ", "
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathCos: {
                assert(!stk.empty() && "MathCos: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathCos(" + a + ", "
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathSmoothstep: {
                assert(!stk.empty() && "MathSmoothstep: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathSmoothstep(" + a + ", "
                    + f(in.data[0]) + ", " + f(in.data[1]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathRemap: {
                assert(!stk.empty() && "MathRemap: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathRemap(" + a + ", "
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + ", " + f(in.data[3]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathClamp: {
                assert(!stk.empty() && "MathClamp: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathClamp(" + a + ", "
                    + f(in.data[0]) + ", " + f(in.data[1]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathAbs: {
                assert(!stk.empty() && "MathAbs: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathAbs(" + a + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathFrac: {
                assert(!stk.empty() && "MathFrac: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathFrac(" + a + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathPow: {
                assert(!stk.empty() && "MathPow: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathPow(" + a + ", " + f(in.data[0]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathSqrt: {
                assert(!stk.empty() && "MathSqrt: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathSqrt(" + a + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathNegate: {
                assert(!stk.empty() && "MathNegate: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathNegate(" + a + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathStep: {
                assert(!stk.empty() && "MathStep: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathStep(" + a + ", " + f(in.data[0]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathSign: {
                assert(!stk.empty() && "MathSign: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathSign(" + a + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathSaturate: {
                assert(!stk.empty() && "MathSaturate: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathSaturate(" + a + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathExp: {
                assert(!stk.empty() && "MathExp: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathExp(" + a + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathLog: {
                assert(!stk.empty() && "MathLog: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathLog(" + a + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathLog2: {
                assert(!stk.empty() && "MathLog2: emit-time value stack underflow");
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathLog2(" + a + ");\n";
                stk.push_back(t);
                break;
            }
            // Binary (pop b=top, a=new-top, replace with result)
            case SdfOpCode::MathAdd: {
                assert(stk.size() >= 2 && "MathAdd: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathAdd(" + a + ", " + b + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathSub: {            // non-commutative: a - b
                assert(stk.size() >= 2 && "MathSub: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathSub(" + a + ", " + b + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathMul: {
                assert(stk.size() >= 2 && "MathMul: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathMul(" + a + ", " + b + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathDiv: {
                assert(stk.size() >= 2 && "MathDiv: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathDiv(" + a + ", " + b + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathMin: {
                assert(stk.size() >= 2 && "MathMin: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathMin(" + a + ", " + b + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::MathMax: {
                assert(stk.size() >= 2 && "MathMax: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathMax(" + a + ", " + b + ");\n";
                stk.push_back(t);
                break;
            }
            // Ternary MathLerp: t_val=top, b=middle, a=bottom → lerp(a,b,t_val)
            case SdfOpCode::MathLerp: {
                assert(stk.size() >= 3 && "MathLerp: emit-time value stack underflow");
                std::string t_val = stk.back(); stk.pop_back();
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_MathLerp(" + a + ", " + b + ", " + t_val + ");\n";
                stk.push_back(t);
                break;
            }
            // Ternary Select: b=top, a=middle, cond=bottom → cond>threshold?a:b
            // N1: delegates to generated kernel SdfCore_Select
            case SdfOpCode::Select: {
                assert(stk.size() >= 3 && "Select: emit-time value stack underflow");
                std::string b = stk.back(); stk.pop_back();
                std::string a = stk.back(); stk.pop_back();
                std::string cond = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_Select(" + cond + ", " + a + ", " + b + ", " + f(in.data[0]) + ");\n";
                stk.push_back(t);
                break;
            }
            // Leaf: push position-derived value
            case SdfOpCode::PositionChannel: {
                // channel baked in data[0]: 0=x,1=y,2=z,3=length(xz)
                std::string t = "t" + std::to_string(n++);
                int ch = (int)in.data[0];
                std::string expr;
                switch (ch) {
                    case 0: expr = curPos + ".x"; break;
                    case 1: expr = curPos + ".y"; break;
                    case 2: expr = curPos + ".z"; break;
                    case 3: expr = "length(float2(" + curPos + ".x, " + curPos + ".z))"; break;
                    default: expr = curPos + ".y"; break;
                }
                body += "  float " + t + " = " + expr + ";\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::Displacement: {       // pop disp; push sdf + disp * scale
                // N1: delegates to generated kernel SdfCore_Displacement
                assert(stk.size() >= 2 && "Displacement: emit-time value stack underflow");
                std::string disp = stk.back(); stk.pop_back();
                std::string sdf = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_Displacement(" + sdf + ", " + disp + ", " + f(in.data[0]) + ");\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::DistanceTo: {          // push length(curPos - center)
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = length(" + curPos + " - float3("
                    + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + "));\n";
                stk.push_back(t);
                break;
            }
            // VM-control ops (hand-dispatched — no generated kernel equivalent)
            case SdfOpCode::Output: {              // passthrough; marks recipe end
                // no-op for emit (top of stack is the output value)
                break;
            }
            case SdfOpCode::PushParam: {           // push baked parameter value
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = " + f(in.data[0]) + ";\n";
                stk.push_back(t);
                break;
            }
            case SdfOpCode::PushFloat3: {          // push data[0..2] as x,y,z scalars
                std::string t = "t" + std::to_string(n++);
                body += "  float3 " + t + " = float3(" + f(in.data[0]) + ", " + f(in.data[1]) + ", " + f(in.data[2]) + ");\n";
                stk.push_back(t + ".x");
                stk.push_back(t + ".y");
                stk.push_back(t + ".z");
                break;
            }
            case SdfOpCode::ComposeFloat3: {       // no-op: 3 scalars on stack are already float3
                break;
            }
            case SdfOpCode::Passthrough: {         // pop 1, push 1 unchanged
                break;                             // top of stack is already the right value
            }
            case SdfOpCode::DecomposeFloat3: {     // pop float3 (vz,vy,vx), push one component
                assert(stk.size() >= 3 && "DecomposeFloat3: emit-time value stack underflow");
                std::string vz = stk.back(); stk.pop_back();
                std::string vy = stk.back(); stk.pop_back();
                std::string vx = stk.back(); stk.pop_back();
                int ch = (int)in.data[0]; // 0=x, 1=y, 2=z
                stk.push_back(ch == 0 ? vx : ch == 1 ? vy : vz);
                break;
            }
            // Float3 arithmetic — float3 is 3 consecutive scalars on string stack; x=deepest, z=top
            // Binary: pop b(bz,by,bx) then a(az,ay,ax), recompose float3, call kernel, push result.xyz
            case SdfOpCode::Float3Add: {
                assert(stk.size() >= 6 && "Float3Add: emit-time value stack underflow");
                std::string bz = stk.back(); stk.pop_back();
                std::string by = stk.back(); stk.pop_back();
                std::string bx = stk.back(); stk.pop_back();
                std::string az = stk.back(); stk.pop_back();
                std::string ay = stk.back(); stk.pop_back();
                std::string ax = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float3 " + t + " = SdfCore_Float3Add(float3(" + ax + ", " + ay + ", " + az + "), float3(" + bx + ", " + by + ", " + bz + "));\n";
                stk.push_back(t + ".x"); stk.push_back(t + ".y"); stk.push_back(t + ".z");
                break;
            }
            case SdfOpCode::Float3Sub: {           // non-commutative: a - b
                assert(stk.size() >= 6 && "Float3Sub: emit-time value stack underflow");
                std::string bz = stk.back(); stk.pop_back();
                std::string by = stk.back(); stk.pop_back();
                std::string bx = stk.back(); stk.pop_back();
                std::string az = stk.back(); stk.pop_back();
                std::string ay = stk.back(); stk.pop_back();
                std::string ax = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float3 " + t + " = SdfCore_Float3Sub(float3(" + ax + ", " + ay + ", " + az + "), float3(" + bx + ", " + by + ", " + bz + "));\n";
                stk.push_back(t + ".x"); stk.push_back(t + ".y"); stk.push_back(t + ".z");
                break;
            }
            case SdfOpCode::Float3MulComponentWise: {
                assert(stk.size() >= 6 && "Float3MulComponentWise: emit-time value stack underflow");
                std::string bz = stk.back(); stk.pop_back();
                std::string by = stk.back(); stk.pop_back();
                std::string bx = stk.back(); stk.pop_back();
                std::string az = stk.back(); stk.pop_back();
                std::string ay = stk.back(); stk.pop_back();
                std::string ax = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float3 " + t + " = SdfCore_Float3MulComponentWise(float3(" + ax + ", " + ay + ", " + az + "), float3(" + bx + ", " + by + ", " + bz + "));\n";
                stk.push_back(t + ".x"); stk.push_back(t + ".y"); stk.push_back(t + ".z");
                break;
            }
            case SdfOpCode::Float3Min: {
                assert(stk.size() >= 6 && "Float3Min: emit-time value stack underflow");
                std::string bz = stk.back(); stk.pop_back();
                std::string by = stk.back(); stk.pop_back();
                std::string bx = stk.back(); stk.pop_back();
                std::string az = stk.back(); stk.pop_back();
                std::string ay = stk.back(); stk.pop_back();
                std::string ax = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float3 " + t + " = SdfCore_Float3Min(float3(" + ax + ", " + ay + ", " + az + "), float3(" + bx + ", " + by + ", " + bz + "));\n";
                stk.push_back(t + ".x"); stk.push_back(t + ".y"); stk.push_back(t + ".z");
                break;
            }
            case SdfOpCode::Float3Max: {
                assert(stk.size() >= 6 && "Float3Max: emit-time value stack underflow");
                std::string bz = stk.back(); stk.pop_back();
                std::string by = stk.back(); stk.pop_back();
                std::string bx = stk.back(); stk.pop_back();
                std::string az = stk.back(); stk.pop_back();
                std::string ay = stk.back(); stk.pop_back();
                std::string ax = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float3 " + t + " = SdfCore_Float3Max(float3(" + ax + ", " + ay + ", " + az + "), float3(" + bx + ", " + by + ", " + bz + "));\n";
                stk.push_back(t + ".x"); stk.push_back(t + ".y"); stk.push_back(t + ".z");
                break;
            }
            // Float3ScalarMul: scalar=top, then vz,vy,vx → push result
            case SdfOpCode::Float3ScalarMul: {
                assert(stk.size() >= 4 && "Float3ScalarMul: emit-time value stack underflow");
                std::string s  = stk.back(); stk.pop_back();
                std::string vz = stk.back(); stk.pop_back();
                std::string vy = stk.back(); stk.pop_back();
                std::string vx = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float3 " + t + " = SdfCore_Float3ScalarMul(float3(" + vx + ", " + vy + ", " + vz + "), " + s + ");\n";
                stk.push_back(t + ".x"); stk.push_back(t + ".y"); stk.push_back(t + ".z");
                break;
            }
            // Float3Dot: pop b then a → push scalar
            case SdfOpCode::Float3Dot: {
                assert(stk.size() >= 6 && "Float3Dot: emit-time value stack underflow");
                std::string bz = stk.back(); stk.pop_back();
                std::string by = stk.back(); stk.pop_back();
                std::string bx = stk.back(); stk.pop_back();
                std::string az = stk.back(); stk.pop_back();
                std::string ay = stk.back(); stk.pop_back();
                std::string ax = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float " + t + " = SdfCore_Float3Dot(float3(" + ax + ", " + ay + ", " + az + "), float3(" + bx + ", " + by + ", " + bz + "));\n";
                stk.push_back(t);
                break;
            }
            // Float3Normalize: pop vz,vy,vx → push normalized float3
            case SdfOpCode::Float3Normalize: {
                assert(stk.size() >= 3 && "Float3Normalize: emit-time value stack underflow");
                std::string vz = stk.back(); stk.pop_back();
                std::string vy = stk.back(); stk.pop_back();
                std::string vx = stk.back(); stk.pop_back();
                std::string t = "t" + std::to_string(n++);
                body += "  float3 " + t + " = SdfCore_Float3Normalize(float3(" + vx + ", " + vy + ", " + vz + "));\n";
                stk.push_back(t + ".x"); stk.push_back(t + ".y"); stk.push_back(t + ".z");
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
