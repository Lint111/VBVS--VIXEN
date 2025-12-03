#include "pch.h"
#include "Compression/DXT1Compressor.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstring>

namespace  Vixen::VoxelData::Compression {

// ============================================================================
// DXT1ColorCompressor Implementation
// ============================================================================

void DXT1ColorCompressor::decodeColorHead(const uint32_t head, glm::vec3 ref[4]) {
    // ESVO encoding: RGB-565 packed with specific bit extraction
    // Uses exp2(-32) scaling to extract RGB-565 from bit-shifted values
    const float scale = 1.0f / 4294967296.0f;  // 1.0f / (1 << 32)

    // ref[0] from bits 0-15 (shifted to extract R, G, B)
    ref[0] = glm::vec3(
        static_cast<float>(head << 16) * scale,  // R: bits 11-15 -> float
        static_cast<float>(head << 21) * scale,  // G: bits 5-10 -> float
        static_cast<float>(head << 27) * scale   // B: bits 0-4 -> float
    );

    // ref[1] from bits 16-31
    ref[1] = glm::vec3(
        static_cast<float>(head) * scale,        // R: bits 27-31 -> float
        static_cast<float>(head << 5) * scale,   // G: bits 21-26 -> float
        static_cast<float>(head << 11) * scale   // B: bits 16-20 -> float
    );

    // Interpolated colors: 2/3 and 1/3 blends
    ref[2] = ref[0] * (2.0f / 3.0f) + ref[1] * (1.0f / 3.0f);
    ref[3] = ref[0] * (1.0f / 3.0f) + ref[1] * (2.0f / 3.0f);
}

uint32_t DXT1ColorCompressor::encodeColorHead(const glm::vec3& color0, const glm::vec3& color1) {
    // Encode two RGB colors into 32-bit header
    // Uses ESVO's iterative residual encoding for better precision

    uint32_t head = 0;

    // Encode color0 (ref[0]): B5 G6 R5 in bits 0-15
    head  = static_cast<uint32_t>(std::clamp(static_cast<int>(color0.b * 31.0f + 0.5f), 0, 31));
    head |= static_cast<uint32_t>(std::clamp(static_cast<int>(color0.g * 63.0f + 0.5f - static_cast<float>(head) * std::exp2(-5.0f)), 0, 63)) << 5;
    head |= static_cast<uint32_t>(std::clamp(static_cast<int>(color0.r * 31.0f + 0.5f - static_cast<float>(head) * std::exp2(-11.0f)), 0, 31)) << 11;

    // Encode color1 (ref[1]): B5 G6 R5 in bits 16-31
    head |= static_cast<uint32_t>(std::clamp(static_cast<int>(color1.b * 31.0f + 0.5f - static_cast<float>(head) * std::exp2(-16.0f)), 0, 31)) << 16;
    head |= static_cast<uint32_t>(std::clamp(static_cast<int>(color1.g * 63.0f + 0.5f - static_cast<float>(head) * std::exp2(-21.0f)), 0, 63)) << 21;
    head |= static_cast<uint32_t>(std::clamp(static_cast<int>(color1.r * 31.0f + 0.5f - static_cast<float>(head) * std::exp2(-27.0f)), 0, 31)) << 27;

    return head;
}

uint64_t DXT1ColorCompressor::encodeBlockTyped(
    const glm::vec3* colors,
    size_t validCount,
    const int32_t* indices
) const {
    if (validCount == 0) {
        return 0;
    }

    // Step 1: Find two colors with maximum distance (endpoints)
    int refIdx0 = 0, refIdx1 = 0;
    float maxDistSq = -std::numeric_limits<float>::max();

    for (size_t i = 0; i < validCount; ++i) {
        for (size_t j = i + 1; j < validCount; ++j) {
            glm::vec3 diff = colors[i] - colors[j];
            float distSq = glm::dot(diff, diff);
            if (distSq > maxDistSq) {
                refIdx0 = static_cast<int>(i);
                refIdx1 = static_cast<int>(j);
                maxDistSq = distSq;
            }
        }
    }

    // Step 2: Encode reference colors as RGB-565
    uint32_t head = encodeColorHead(colors[refIdx0], colors[refIdx1]);

    // Step 3: Decode reference colors for accurate matching
    glm::vec3 ref[4];
    decodeColorHead(head, ref);

    // Step 4: For each color, find best interpolation index
    uint32_t bits = 0;
    for (size_t i = 0; i < validCount; ++i) {
        int bestIdx = 0;
        float bestDistSq = std::numeric_limits<float>::max();

        for (int j = 0; j < 4; ++j) {
            glm::vec3 diff = colors[i] - ref[j];
            float distSq = glm::dot(diff, diff);
            if (distSq < bestDistSq) {
                bestIdx = j;
                bestDistSq = distSq;
            }
        }

        // Pack 2-bit index at appropriate position
        int texelIdx = indices ? indices[i] : static_cast<int>(i);
        bits |= static_cast<uint32_t>(bestIdx) << (texelIdx * 2);
    }

    // Pack header (32 bits) and indices (32 bits) into 64-bit block
    return static_cast<uint64_t>(head) | (static_cast<uint64_t>(bits) << 32);
}

void DXT1ColorCompressor::decodeBlockTyped(
    const uint64_t& block,
    glm::vec3* output
) const {
    // Extract header and bits
    uint32_t head = static_cast<uint32_t>(block);
    uint32_t bits = static_cast<uint32_t>(block >> 32);

    // Decode reference colors
    glm::vec3 ref[4];
    decodeColorHead(head, ref);

    // Decode each of 16 colors
    for (int i = 0; i < 16; ++i) {
        output[i] = ref[bits & 3];
        bits >>= 2;
    }
}

const char* DXT1ColorCompressor::getGLSLDecodeFunction() {
    return R"GLSL(
// Scale factor 1.0 / 2^32
const float DXT_SCALE = 1.0 / 4294967296.0;

// DXT1 color decode coefficients
const float DXT_COLOR_COEFS[4] = float[4](
    DXT_SCALE,
    0.0,
    2.0 * DXT_SCALE / 3.0,
    1.0 * DXT_SCALE / 3.0
);

// Decode DXT1 color block to RGB
// block: 64-bit compressed block (stored as uvec2)
// texelIdx: which of 16 texels to decode (0-15)
vec3 decodeDXT1Color(uvec2 block, int texelIdx) {
    uint head = block.x;
    uint bits = block.y;

    float c0 = DXT_COLOR_COEFS[(bits >> (texelIdx * 2)) & 3u];
    float c1 = DXT_SCALE - c0;

    return vec3(
        c0 * float(head << 16) + c1 * float(head),
        c0 * float(head << 21) + c1 * float(head << 5),
        c0 * float(head << 27) + c1 * float(head << 11)
    );
}
)GLSL";
}

// ============================================================================
// DXTNormalCompressor Implementation
// ============================================================================

uint32_t DXTNormalCompressor::encodeRawNormal(const glm::vec3& normal) {
    // Find dominant axis
    glm::vec3 a = glm::abs(normal);
    int axis = (a.x >= std::max(a.y, a.z)) ? 0 : (a.y >= a.z) ? 1 : 2;

    // Reorder to (t, u, v) where t is dominant
    glm::vec3 tuv;
    switch (axis) {
        case 0: tuv = normal; break;
        case 1: tuv = glm::vec3(normal.y, normal.z, normal.x); break;
        default: tuv = glm::vec3(normal.z, normal.x, normal.y); break;
    }

    // Pack: sign (1 bit), axis (2 bits), u (15 bits), v (14 bits)
    uint32_t result = 0;
    if (tuv.x < 0.0f) result |= 0x80000000u;
    result |= static_cast<uint32_t>(axis) << 29;

    float absT = std::abs(tuv.x);
    if (absT > 0.0f) {
        int u = std::clamp(static_cast<int>((tuv.y / absT) * 16383.0f), -0x4000, 0x3FFF);
        int v = std::clamp(static_cast<int>((tuv.z / absT) * 8191.0f), -0x2000, 0x1FFF);
        result |= static_cast<uint32_t>(u & 0x7FFF) << 14;
        result |= static_cast<uint32_t>(v & 0x3FFF);
    }

    return result;
}

glm::vec3 DXTNormalCompressor::decodeRawNormal(uint32_t value) {
    int32_t sign = static_cast<int32_t>(value) >> 31;
    int axis = (value >> 29) & 3;
    int32_t t = sign ^ 0x7FFFFFFF;
    int32_t u = static_cast<int32_t>(value << 3);
    int32_t v = static_cast<int32_t>(value << 18);

    switch (axis) {
        case 0: return glm::vec3(t, u, v);
        case 1: return glm::vec3(v, t, u);
        default: return glm::vec3(u, v, t);
    }
}

uint32_t DXTNormalCompressor::encodeNormalAxis(uint32_t headUV, const glm::vec3& axis, int shift) {
    // Find exponent based on max component
    float maxComp = std::max({std::abs(axis.x), std::abs(axis.y), std::abs(axis.z)});
    int exponent = 0;
    if (maxComp > 0.0f) {
        // Extract exponent from float representation
        uint32_t bits;
        std::memcpy(&bits, &maxComp, sizeof(bits));
        exponent = std::clamp(static_cast<int>(bits >> 23) + (-127 - 31 + 1 - 3 + 13), 0, 15);
    }

    glm::vec3 scaled = axis * std::exp2(static_cast<float>(-exponent + (13 - 31)));

    headUV |= static_cast<uint32_t>(exponent) << shift;
    headUV |= static_cast<uint32_t>((std::clamp(static_cast<int>(scaled.z + 8.5f - static_cast<float>(headUV) * std::exp2(-4.0f - shift)), 0, 15) ^ 8)) << (4 + shift);
    headUV |= static_cast<uint32_t>((std::clamp(static_cast<int>(scaled.y + 8.5f - static_cast<float>(headUV) * std::exp2(-8.0f - shift)), 0, 15) ^ 8)) << (8 + shift);
    headUV |= static_cast<uint32_t>((std::clamp(static_cast<int>(scaled.x + 8.5f - static_cast<float>(headUV) * std::exp2(-12.0f - shift)), 0, 15) ^ 8)) << (12 + shift);

    return headUV;
}

glm::vec3 DXTNormalCompressor::decodeNormalAxis(uint32_t headUV, int shift) {
    int32_t shifted = static_cast<int32_t>(headUV) << (16 - shift);
    int exponent = (shifted >> 16) & 15;
    float scale = std::exp2(static_cast<float>(exponent + (3 - 13)));

    return glm::vec3(
        static_cast<float>(shifted) * scale,
        static_cast<float>(shifted << 4) * scale,
        static_cast<float>(shifted << 8) * scale
    );
}

DXTNormalBlock DXTNormalCompressor::encodeBlockTyped(
    const glm::vec3* normals,
    size_t validCount,
    const int32_t* indices
) const {
    DXTNormalBlock result = {0, 0};
    if (validCount == 0) return result;

    // Step 1: Compute average normal as base
    glm::vec3 base = normals[0];
    for (size_t i = 1; i < validCount; ++i) {
        base += normals[i];
    }

    // Handle degenerate case (near-zero average)
    float baseLen = glm::length(base);
    if (baseLen < 0.1f * static_cast<float>(validCount)) {
        // Pick input closest to average
        int bestIdx = 0;
        float bestDot = glm::dot(normals[0], base);
        for (size_t i = 1; i < validCount; ++i) {
            float d = glm::dot(normals[i], base);
            if (d > bestDot) {
                bestIdx = static_cast<int>(i);
                bestDot = d;
            }
        }
        base = normals[bestIdx];
    }

    // Step 2: Encode base normal
    uint32_t headBase = encodeRawNormal(base);
    base = decodeRawNormal(headBase);

    // Step 3: Find normal furthest from base (U axis direction)
    int uIdx = 0;
    float uDot = glm::dot(normals[0], base);
    for (size_t i = 1; i < validCount; ++i) {
        float d = glm::dot(normals[i], base);
        if (d < uDot) {
            uIdx = static_cast<int>(i);
            uDot = d;
        }
    }

    // Encode U axis
    uint32_t headUV = encodeNormalAxis(0, normals[uIdx] * glm::length(base) - base, 0);
    glm::vec3 u = decodeNormalAxis(headUV, 0);

    // Step 4: Build reference normals using only U
    glm::vec3 uRef[4];
    for (int i = 0; i < 4; ++i) {
        uRef[i] = glm::normalize(base + u * NORMAL_COEFS[i]);
    }

    // Find worst approximated normal (V axis direction)
    int vIdx = 0, vLerpIdx = 0;
    float vDot = std::numeric_limits<float>::max();
    for (size_t i = 0; i < validCount; ++i) {
        int bestLerp = 0;
        float bestDot = glm::dot(normals[i], uRef[0]);
        for (int j = 1; j < 4; ++j) {
            float d = glm::dot(normals[i], uRef[j]);
            if (d > bestDot) {
                bestLerp = j;
                bestDot = d;
            }
        }
        if (bestDot < vDot) {
            vIdx = static_cast<int>(i);
            vLerpIdx = bestLerp;
            vDot = bestDot;
        }
    }

    // Encode V axis
    glm::vec3 tmp = base + u * NORMAL_COEFS[vLerpIdx];
    headUV = encodeNormalAxis(headUV, normals[vIdx] * glm::length(tmp) - tmp, 16);
    glm::vec3 v = decodeNormalAxis(headUV, 16);

    // Step 5: Build full 4x4 reference grid
    glm::vec3 uvRef[16];
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            uvRef[i + j * 4] = glm::normalize(base + u * NORMAL_COEFS[i] + v * NORMAL_COEFS[j]);
        }
    }

    // Step 6: Find best lerp factors for each input
    uint32_t bitsU = 0, bitsV = 0;
    for (size_t i = 0; i < validCount; ++i) {
        int bestLerp = 0;
        float bestDot = glm::dot(normals[i], uvRef[0]);
        for (int j = 1; j < 16; ++j) {
            float d = glm::dot(normals[i], uvRef[j]);
            if (d > bestDot) {
                bestLerp = j;
                bestDot = d;
            }
        }

        int texelIdx = indices ? indices[i] : static_cast<int>(i);
        bitsU |= static_cast<uint32_t>(bestLerp & 3) << (texelIdx * 2);
        bitsV |= static_cast<uint32_t>(bestLerp >> 2) << (texelIdx * 2);
    }

    // Pack result
    result.blockA = static_cast<uint64_t>(headBase) | (static_cast<uint64_t>(bitsU) << 32);
    result.blockB = static_cast<uint64_t>(headUV) | (static_cast<uint64_t>(bitsV) << 32);

    return result;
}

void DXTNormalCompressor::decodeBlockTyped(
    const DXTNormalBlock& block,
    glm::vec3* output
) const {
    uint32_t headBase = static_cast<uint32_t>(block.blockA);
    uint32_t headUV = static_cast<uint32_t>(block.blockB);
    uint32_t bitsU = static_cast<uint32_t>(block.blockA >> 32);
    uint32_t bitsV = static_cast<uint32_t>(block.blockB >> 32);

    glm::vec3 base = decodeRawNormal(headBase);
    glm::vec3 u = decodeNormalAxis(headUV, 0);
    glm::vec3 v = decodeNormalAxis(headUV, 16);

    for (int i = 0; i < 16; ++i) {
        float cu = NORMAL_COEFS[(bitsU >> (i * 2)) & 3];
        float cv = NORMAL_COEFS[(bitsV >> (i * 2)) & 3];
        output[i] = base + u * cu + v * cv;
    }
}

const char* DXTNormalCompressor::getGLSLDecodeFunction() {
    return R"GLSL(
// DXT normal decode coefficients
const float DXT_NORMAL_COEFS[4] = float[4](-1.0, -1.0/3.0, 1.0/3.0, 1.0);

// Decode raw normal from 32-bit encoding
vec3 decodeRawNormal(uint value) {
    int sign = int(value) >> 31;
    int axis = int((value >> 29) & 3u);
    int t = sign ^ 0x7FFFFFFF;
    int u = int(value << 3);
    int v = int(value << 18);

    if (axis == 0) return vec3(t, u, v);
    if (axis == 1) return vec3(v, t, u);
    return vec3(u, v, t);
}

// Decode normal axis from packed encoding
vec3 decodeNormalAxis(uint headUV, int shift) {
    int shifted = int(headUV) << (16 - shift);
    int exponent = (shifted >> 16) & 15;
    float scale = exp2(float(exponent + (3 - 13)));

    return vec3(
        float(shifted) * scale,
        float(shifted << 4) * scale,
        float(shifted << 8) * scale
    );
}

// Decode DXT normal block
// blockA, blockB: two 64-bit blocks (stored as uvec2 each)
// texelIdx: which of 16 texels to decode (0-15)
vec3 decodeDXTNormal(uvec2 blockA, uvec2 blockB, int texelIdx) {
    uint headBase = blockA.x;
    uint headUV = blockB.x;
    uint bitsU = blockA.y;
    uint bitsV = blockB.y;

    vec3 base = decodeRawNormal(headBase);
    vec3 u = decodeNormalAxis(headUV, 0);
    vec3 v = decodeNormalAxis(headUV, 16);

    float cu = DXT_NORMAL_COEFS[(bitsU >> (texelIdx * 2)) & 3u];
    float cv = DXT_NORMAL_COEFS[(bitsV >> (texelIdx * 2)) & 3u];

    return base + u * cu + v * cv;
}
)GLSL";
}

} // namespace VoxelData
