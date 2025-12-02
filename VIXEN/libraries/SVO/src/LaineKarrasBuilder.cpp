#include "pch.h"
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
    // TODO: ISVOBuilder::BuildConfig needs proper definition
    BuildParams params;
    params.maxLevels = config.maxLevels;
    params.geometryErrorThreshold = config.errorThreshold;
    params.colorErrorThreshold = config.errorThreshold * 10.0f;
    // params.enableContours = config.enableContours;  // TODO: Add to BuildConfig
    // params.enableCompression = config.enableCompression;  // TODO: Add to BuildConfig
    return params;
}

InputMesh LaineKarrasBuilder::convertGeometry(const InputGeometry& geometry) {
    // TODO: ISVOBuilder::InputGeometry needs proper definition with positions/normals
    InputMesh mesh;
    // Placeholder - needs actual geometry data
    // mesh.vertices = geometry.positions;
    // mesh.normals = geometry.normals;
    return mesh;
}

} // namespace SVO
