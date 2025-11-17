#include "LaineKarrasOctree.h"

namespace SVO {

LaineKarrasBuilder::LaineKarrasBuilder()
    : m_impl(nullptr) {
}

LaineKarrasBuilder::~LaineKarrasBuilder() = default;

std::unique_ptr<ISVOStructure> LaineKarrasBuilder::build(
    const InputGeometry& geometry,
    const BuildConfig& config) {

    // Convert interface types to SVOBuilder types
    BuildParams params = convertConfig(config);
    InputMesh mesh = convertGeometry(geometry);

    // Create builder
    m_impl = std::make_unique<SVOBuilder>(params);

    // Set progress callback if provided
    if (m_progressCallback) {
        m_impl->setProgressCallback([this](float progress) {
            m_progressCallback(progress, "Building octree");
        });
    }

    // Build octree
    auto octree = m_impl->build(mesh);

    if (!octree) {
        return nullptr;
    }

    // Wrap in LaineKarrasOctree
    auto result = std::make_unique<LaineKarrasOctree>();
    result->setOctree(std::move(octree));

    return result;
}

BuildParams LaineKarrasBuilder::convertConfig(const BuildConfig& config) {
    BuildParams params;
    params.maxLevels = config.maxLevels;
    params.geometryErrorThreshold = config.errorThreshold;
    params.colorErrorThreshold = config.errorThreshold * 10.0f;
    params.enableContours = config.enableContours;
    params.enableCompression = config.enableCompression;
    return params;
}

InputMesh LaineKarrasBuilder::convertGeometry(const InputGeometry& geometry) {
    InputMesh mesh;

    // Copy vertex data
    mesh.vertices = geometry.positions;
    mesh.normals = geometry.normals;
    mesh.colors.resize(geometry.positions.size(), glm::vec3(1.0f));  // Default white
    mesh.uvs.resize(geometry.positions.size(), glm::vec2(0.0f));

    // Copy indices
    mesh.indices = geometry.indices;

    // Compute bounding box
    if (!geometry.positions.empty()) {
        mesh.minBounds = geometry.positions[0];
        mesh.maxBounds = geometry.positions[0];

        for (const auto& pos : geometry.positions) {
            mesh.minBounds = glm::min(mesh.minBounds, pos);
            mesh.maxBounds = glm::max(mesh.maxBounds, pos);
        }
    }

    return mesh;
}

} // namespace SVO
