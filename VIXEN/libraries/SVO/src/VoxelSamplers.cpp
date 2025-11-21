#include "VoxelInjection.h"
#include <cmath>
#include <algorithm>

namespace SVO {
namespace Samplers {

// ============================================================================
// Simple 3D Noise Implementation (Perlin-like)
// ============================================================================

namespace {
    // Hash function for noise
    inline float hash(float n) {
        return std::fmod(std::sin(n) * 43758.5453f, 1.0f);
    }

    // 3D noise function
    float noise3D(const glm::vec3& p) {
        glm::vec3 i = glm::floor(p);
        glm::vec3 f = p - i;

        // Cubic interpolation
        f = f * f * (3.0f - 2.0f * f);

        float n = i.x + i.y * 57.0f + i.z * 113.0f;

        return glm::mix(
            glm::mix(
                glm::mix(hash(n + 0.0f), hash(n + 1.0f), f.x),
                glm::mix(hash(n + 57.0f), hash(n + 58.0f), f.x),
                f.y
            ),
            glm::mix(
                glm::mix(hash(n + 113.0f), hash(n + 114.0f), f.x),
                glm::mix(hash(n + 170.0f), hash(n + 171.0f), f.x),
                f.y
            ),
            f.z
        );
    }

    // Fractional Brownian Motion
    float fbm(const glm::vec3& p, int octaves, float lacunarity, float persistence) {
        float value = 0.0f;
        float amplitude = 1.0f;
        float frequency = 1.0f;

        for (int i = 0; i < octaves; ++i) {
            value += amplitude * noise3D(p * frequency);
            frequency *= lacunarity;
            amplitude *= persistence;
        }

        return value;
    }
}

// ============================================================================
// NoiseSampler Implementation
// ============================================================================

NoiseSampler::NoiseSampler(const Params& params)
    : m_params(params) {
}

bool NoiseSampler::sample(const glm::vec3& position, ::VoxelData::DynamicVoxelScalar & outData) const {
    // Apply frequency and offset
    glm::vec3 p = (position + m_params.offset) * m_params.frequency;

    // Generate noise value
    float noiseValue = fbm(p, m_params.octaves, m_params.lacunarity, m_params.persistence);
    noiseValue = noiseValue * 2.0f - 1.0f;  // Remap to [-1, 1]
    noiseValue *= m_params.amplitude;

    // Check if solid (below threshold = solid for terrain)
    if (noiseValue > m_params.threshold) {
        outData.position = position;
        outData.density = 1.0f;

        // Color based on height/noise value
        float t = glm::clamp((noiseValue - m_params.threshold) / m_params.amplitude, 0.0f, 1.0f);
        outData.color = glm::mix(glm::vec3(0.3f, 0.5f, 0.3f), glm::vec3(0.8f, 0.8f, 0.9f), t);

        // Estimate normal via gradient
        const float eps = 0.01f;
        float dx = fbm((position + glm::vec3(eps, 0, 0) + m_params.offset) * m_params.frequency,
                      m_params.octaves, m_params.lacunarity, m_params.persistence) - noiseValue;
        float dy = fbm((position + glm::vec3(0, eps, 0) + m_params.offset) * m_params.frequency,
                      m_params.octaves, m_params.lacunarity, m_params.persistence) - noiseValue;
        float dz = fbm((position + glm::vec3(0, 0, eps) + m_params.offset) * m_params.frequency,
                      m_params.octaves, m_params.lacunarity, m_params.persistence) - noiseValue;

        outData.normal = glm::normalize(glm::vec3(dx, dy, dz));
        outData.occlusion = 1.0f;

        return true;
    }

    return false;
}

void NoiseSampler::getBounds(glm::vec3& outMin, glm::vec3& outMax) const {
    // Infinite bounds for procedural noise
    outMin = glm::vec3(-1000.0f);
    outMax = glm::vec3(1000.0f);
}

float NoiseSampler::estimateDensity(const glm::vec3& center, float size) const {
    // Sample a few points to estimate density
    int samples = 8;
    int solidCount = 0;

    for (int i = 0; i < samples; ++i) {
        glm::vec3 offset(
            (i & 1) ? size * 0.5f : -size * 0.5f,
            (i & 2) ? size * 0.5f : -size * 0.5f,
            (i & 4) ? size * 0.5f : -size * 0.5f
        );

        glm::vec3 p = (center + offset + m_params.offset) * m_params.frequency;
        float noiseValue = fbm(p, m_params.octaves, m_params.lacunarity, m_params.persistence);
        noiseValue = noiseValue * 2.0f - 1.0f;
        noiseValue *= m_params.amplitude;

        if (noiseValue > m_params.threshold) {
            solidCount++;
        }
    }

    return (float)solidCount / (float)samples;
}

// ============================================================================
// SDFSampler Implementation
// ============================================================================

SDFSampler::SDFSampler(SDFFunc sdfFunc, const glm::vec3& min, const glm::vec3& max)
    : m_sdfFunc(std::move(sdfFunc))
    , m_min(min)
    , m_max(max) {
}

bool SDFSampler::sample(const glm::vec3& position, ::VoxelData::DynamicVoxelScalar& outData) const {
    float dist = m_sdfFunc(position);

    // Negative distance = inside surface
    if (dist < 0.0f) {
        outData.position = position;
        outData.density = 1.0f;
        outData.color = glm::vec3(0.7f, 0.7f, 0.7f);
        outData.normal = estimateNormal(position);
        outData.occlusion = 1.0f;
        return true;
    }

    return false;
}

void SDFSampler::getBounds(glm::vec3& outMin, glm::vec3& outMax) const {
    outMin = m_min;
    outMax = m_max;
}

glm::vec3 SDFSampler::estimateNormal(const glm::vec3& p) const {
    const float eps = 0.001f;

    float dx = m_sdfFunc(p + glm::vec3(eps, 0, 0)) - m_sdfFunc(p - glm::vec3(eps, 0, 0));
    float dy = m_sdfFunc(p + glm::vec3(0, eps, 0)) - m_sdfFunc(p - glm::vec3(0, eps, 0));
    float dz = m_sdfFunc(p + glm::vec3(0, 0, eps)) - m_sdfFunc(p - glm::vec3(0, 0, eps));

    return glm::normalize(glm::vec3(dx, dy, dz));
}

// ============================================================================
// HeightmapSampler Implementation
// ============================================================================

HeightmapSampler::HeightmapSampler(const Params& params)
    : m_params(params) {
}

bool HeightmapSampler::sample(const glm::vec3& position, ::VoxelData::DynamicVoxelScalar& outData) const {
    // Sample heightmap at XZ position
    float height = sampleHeight(position.x, position.z);

    // Check if position is below terrain surface
    if (position.y < height) {
        outData.position = position;
        outData.density = 1.0f;
        outData.color = m_params.baseColor;
        outData.normal = computeNormal(position.x, position.z);
        outData.occlusion = 1.0f;
        return true;
    }

    return false;
}

void HeightmapSampler::getBounds(glm::vec3& outMin, glm::vec3& outMax) const {
    outMin = glm::vec3(0.0f, m_params.minHeight, 0.0f);
    outMax = glm::vec3(
        m_params.width * m_params.horizontalScale,
        m_params.maxHeight,
        m_params.height * m_params.horizontalScale
    );
}

float HeightmapSampler::estimateDensity(const glm::vec3& center, float size) const {
    // Sample terrain height at region center
    float height = sampleHeight(center.x, center.z);

    float regionMin = center.y - size * 0.5f;
    float regionMax = center.y + size * 0.5f;

    // Fully below terrain
    if (regionMax < height) {
        return 1.0f;
    }

    // Fully above terrain
    if (regionMin > height) {
        return 0.0f;
    }

    // Intersects terrain surface
    return 0.5f;
}

float HeightmapSampler::sampleHeight(float x, float z) const {
    // Convert world coordinates to heightmap coordinates
    float u = x / m_params.horizontalScale;
    float v = z / m_params.horizontalScale;

    // Clamp to heightmap bounds
    int ix = glm::clamp(int(u), 0, m_params.width - 1);
    int iz = glm::clamp(int(v), 0, m_params.height - 1);

    // Lookup height value
    size_t idx = static_cast<size_t>(ix) + static_cast<size_t>(iz) * m_params.width;
    if (idx >= m_params.heights.size()) {
        return m_params.minHeight;
    }

    float normalizedHeight = m_params.heights[idx];
    return m_params.minHeight + normalizedHeight * (m_params.maxHeight - m_params.minHeight);
}

glm::vec3 HeightmapSampler::computeNormal(float x, float z) const {
    const float eps = 0.1f;

    // Sample heights at neighboring points
    float h0 = sampleHeight(x, z);
    float hx = sampleHeight(x + eps, z);
    float hz = sampleHeight(x, z + eps);

    // Compute tangent vectors
    glm::vec3 tx(eps, hx - h0, 0.0f);
    glm::vec3 tz(0.0f, hz - h0, eps);

    // Cross product for normal
    return glm::normalize(glm::cross(tx, tz));
}

} // namespace Samplers

// ============================================================================
// SDF Utility Functions
// ============================================================================

namespace SDF {

float sphere(const glm::vec3& p, float radius) {
    return glm::length(p) - radius;
}

float box(const glm::vec3& p, const glm::vec3& size) {
    glm::vec3 q = glm::abs(p) - size;
    return glm::length(glm::max(q, glm::vec3(0.0f))) +
           std::min(std::max(q.x, std::max(q.y, q.z)), 0.0f);
}

float torus(const glm::vec3& p, float majorRadius, float minorRadius) {
    glm::vec2 q(glm::length(glm::vec2(p.x, p.z)) - majorRadius, p.y);
    return glm::length(q) - minorRadius;
}

float cylinder(const glm::vec3& p, float radius, float height) {
    glm::vec2 d(glm::length(glm::vec2(p.x, p.z)) - radius, std::abs(p.y) - height);
    return std::min(std::max(d.x, d.y), 0.0f) + glm::length(glm::max(d, glm::vec2(0.0f)));
}

float unionOp(float d1, float d2) {
    return std::min(d1, d2);
}

float subtraction(float d1, float d2) {
    return std::max(-d1, d2);
}

float intersection(float d1, float d2) {
    return std::max(d1, d2);
}

float smoothUnion(float d1, float d2, float k) {
    float h = glm::clamp(0.5f + 0.5f * (d2 - d1) / k, 0.0f, 1.0f);
    return glm::mix(d2, d1, h) - k * h * (1.0f - h);
}

} // namespace SDF

} // namespace SVO
