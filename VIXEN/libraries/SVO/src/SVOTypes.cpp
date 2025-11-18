#include "SVOTypes.h"
#include <cmath>
#include <algorithm>

namespace SVO {

// ============================================================================
// Contour Implementation
// ============================================================================

glm::vec3 Contour::getNormal() const {
    // Decode 6-bit signed components to [-1, 1]
    auto decode = [](uint32_t bits, int numBits) -> float {
        const int maxVal = (1 << (numBits - 1)) - 1;
        const int minVal = -(1 << (numBits - 1));
        int signed_val = static_cast<int>(bits);
        if (signed_val > maxVal) {
            signed_val = signed_val - (1 << numBits);
        }
        return static_cast<float>(signed_val) / static_cast<float>(maxVal);
    };

    glm::vec3 n;
    n.x = decode(nx, 6);
    n.y = decode(ny, 6);
    n.z = decode(nz, 6);

    return glm::normalize(n);
}

void Contour::getPlanes(const glm::vec3& normal, float& planeMin, float& planeMax) const {
    // Decode position (7-bit signed, range [-1, 1] in voxel space)
    const int maxVal = 63; // 2^6 - 1
    int signed_pos = static_cast<int>(position);
    if (signed_pos > maxVal) {
        signed_pos = signed_pos - 128;
    }
    float center = static_cast<float>(signed_pos) / static_cast<float>(maxVal);

    // Decode thickness (7-bit unsigned, range [0, 1] in voxel space)
    float thick = static_cast<float>(thickness) / 127.0f * 0.75f; // Scale factor from paper

    // Convert to plane positions
    // center is position along normal, thickness is full distance between planes
    planeMin = center - thick * 0.5f;
    planeMax = center + thick * 0.5f;
}

// ============================================================================
// UncompressedAttributes Implementation
// ============================================================================

glm::vec3 UncompressedAttributes::getNormal() const {
    // Decode normal stored as point on cube
    // sign_and_axis: 0-5 selects cube face (±X, ±Y, ±Z)
    // u_coordinate, v_coordinate: position on that face

    int axis = sign_and_axis >> 1;  // 0=X, 1=Y, 2=Z
    bool negative = (sign_and_axis & 1) != 0;

    // Decode coordinates to [-1, 1]
    float u = (static_cast<float>(u_coordinate) / 16383.0f) * 2.0f - 1.0f;
    float v = (static_cast<float>(v_coordinate) / 8191.0f) * 2.0f - 1.0f;

    glm::vec3 normal(0.0f);

    switch (axis) {
        case 0: // X axis
            normal.x = negative ? -1.0f : 1.0f;
            normal.y = u;
            normal.z = v;
            break;
        case 1: // Y axis
            normal.x = u;
            normal.y = negative ? -1.0f : 1.0f;
            normal.z = v;
            break;
        case 2: // Z axis
            normal.x = u;
            normal.y = v;
            normal.z = negative ? -1.0f : 1.0f;
            break;
    }

    return glm::normalize(normal);
}

// ============================================================================
// Utility Functions
// ============================================================================

namespace {

// Encode normal vector to point-on-cube format (32 bits)
uint32_t encodeNormal(const glm::vec3& n) {
    // Find dominant axis
    glm::vec3 absN = glm::abs(n);
    int axis;
    bool negative;

    if (absN.x >= absN.y && absN.x >= absN.z) {
        axis = 0;
        negative = n.x < 0.0f;
    } else if (absN.y >= absN.z) {
        axis = 1;
        negative = n.y < 0.0f;
    } else {
        axis = 2;
        negative = n.z < 0.0f;
    }

    // Project to cube face and encode coordinates
    float u, v;
    switch (axis) {
        case 0: // X face
            u = n.y / absN.x;
            v = n.z / absN.x;
            break;
        case 1: // Y face
            u = n.x / absN.y;
            v = n.z / absN.y;
            break;
        case 2: // Z face
            u = n.x / absN.z;
            v = n.y / absN.z;
            break;
    }

    // Map [-1,1] to integer ranges
    uint32_t u_int = static_cast<uint32_t>((u + 1.0f) * 0.5f * 16383.0f + 0.5f);
    uint32_t v_int = static_cast<uint32_t>((v + 1.0f) * 0.5f * 8191.0f + 0.5f);
    uint32_t sign_axis = (axis << 1) | (negative ? 1 : 0);

    // Pack into 32 bits
    return (sign_axis & 0x7) | ((u_int & 0x7FFF) << 3) | ((v_int & 0x3FFF) << 18);
}

// Encode contour from geometric parameters
Contour encodeContour(const glm::vec3& normal, float centerPos, float thickness) {
    Contour c{};

    // Encode normal (6 bits per component, signed)
    auto encode = [](float val, int numBits) -> uint32_t {
        const int maxVal = (1 << (numBits - 1)) - 1;
        int quantized = static_cast<int>(val * maxVal + 0.5f);
        quantized = std::clamp(quantized, -(1 << (numBits - 1)), maxVal);
        return static_cast<uint32_t>(quantized) & ((1 << numBits) - 1);
    };

    c.nx = encode(normal.x, 6);
    c.ny = encode(normal.y, 6);
    c.nz = encode(normal.z, 6);

    // Encode position (7 bits, signed, range [-1,1])
    int posQuantized = static_cast<int>(centerPos * 63.0f + 0.5f);
    posQuantized = std::clamp(posQuantized, -64, 63);
    c.position = static_cast<uint32_t>(posQuantized) & 0x7F;

    // Encode thickness (7 bits, unsigned, range [0,1])
    float scaledThickness = thickness / 0.75f; // Inverse of scale factor
    uint32_t thickQuantized = static_cast<uint32_t>(scaledThickness * 127.0f + 0.5f);
    c.thickness = std::min(thickQuantized, 127u);

    return c;
}

} // anonymous namespace

// ============================================================================
// Helper Functions
// ============================================================================

UncompressedAttributes makeAttributes(const glm::vec3& color, const glm::vec3& normal) {
    UncompressedAttributes attr{};

    // Encode color
    attr.red = static_cast<uint8_t>(std::clamp(color.r * 255.0f, 0.0f, 255.0f));
    attr.green = static_cast<uint8_t>(std::clamp(color.g * 255.0f, 0.0f, 255.0f));
    attr.blue = static_cast<uint8_t>(std::clamp(color.b * 255.0f, 0.0f, 255.0f));
    attr.alpha = 255;

    // Encode normal
    uint32_t packed = encodeNormal(glm::normalize(normal));
    attr.sign_and_axis = packed & 0x7;
    attr.u_coordinate = (packed >> 3) & 0x7FFF;
    attr.v_coordinate = (packed >> 18) & 0x3FFF;

    return attr;
}

Contour makeContour(const glm::vec3& normal, float centerPos, float thickness) {
    return encodeContour(glm::normalize(normal), centerPos, thickness);
}

glm::vec3 decodeContourNormal(const Contour& contour) {
    return contour.getNormal();
}

float decodeContourThickness(const Contour& contour) {
    // Decode thickness (7 bits, unsigned, range [0, 1] in voxel space)
    return static_cast<float>(contour.thickness) / 127.0f * 0.75f;
}

float decodeContourPosition(const Contour& contour) {
    // Decode position (7 bits, signed, range [-1, 1] in voxel space)
    const int maxVal = 63; // 2^6 - 1
    int signed_pos = static_cast<int>(contour.position);
    if (signed_pos > maxVal) {
        signed_pos = signed_pos - 128;
    }
    return static_cast<float>(signed_pos) / static_cast<float>(maxVal);
}

// Population count for 8-bit value
int popc8(uint8_t mask) {
    // Brian Kernighan's algorithm
    int count = 0;
    while (mask) {
        mask &= mask - 1;
        count++;
    }
    return count;
}

} // namespace SVO
