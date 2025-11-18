#include "VoxelInjection.h"
#include "LaineKarrasOctree.h"
#include "SVOBuilder.h"
#include <algorithm>
#include <chrono>
#include <iostream>

namespace SVO {

// ============================================================================
// VoxelInjector Implementation
// ============================================================================

/**
 * Inject sparse voxel data into SVO.
 * Builds octree by inserting individual voxels at appropriate levels.
 */
std::unique_ptr<ISVOStructure> VoxelInjector::inject(
    const SparseVoxelInput& input,
    const InjectionConfig& config) {

    m_stats = Stats{};
    auto startTime = std::chrono::high_resolution_clock::now();

    // Use shared_ptr to avoid expensive copies in lambdas
    auto voxelsPtr = std::make_shared<std::vector<VoxelData>>(input.voxels);
    glm::vec3 worldMinCopy = input.worldMin;
    glm::vec3 worldMaxCopy = input.worldMax;
    int resolutionCopy = input.resolution;

    // Create lambda sampler from sparse voxels
    auto sampler = std::make_unique<LambdaVoxelSampler>(
        // Sample function - lookup voxel in sparse list
        [voxelsPtr, worldMinCopy, worldMaxCopy, resolutionCopy](const glm::vec3& pos, VoxelData& data) -> bool {
            // Find closest voxel (simple linear search for now)
            // TODO: Use spatial hash or octree for faster lookup
            float minDist = std::numeric_limits<float>::max();
            const VoxelData* closest = nullptr;

            glm::vec3 voxelSize = (worldMaxCopy - worldMinCopy) / float(resolutionCopy);
            float searchRadius = glm::length(voxelSize) * 0.5f;

            for (const auto& voxel : *voxelsPtr) {
                float dist = glm::length(voxel.position - pos);
                if (dist < searchRadius && dist < minDist) {
                    minDist = dist;
                    closest = &voxel;
                }
            }

            if (closest) {
                data = *closest;
                return true;
            }
            return false;
        },
        // Bounds function
        [worldMinCopy, worldMaxCopy](glm::vec3& min, glm::vec3& max) {
            min = worldMinCopy;
            max = worldMaxCopy;
        },
        // Density estimator - check if region contains voxels
        [voxelsPtr](const glm::vec3& center, float size) -> float {
            // Check if any voxel is within this region
            float halfSize = size * 0.5f;
            for (const auto& voxel : *voxelsPtr) {
                glm::vec3 diff = glm::abs(voxel.position - center);
                if (diff.x <= halfSize && diff.y <= halfSize && diff.z <= halfSize) {
                    return 1.0f;  // Region contains voxels
                }
            }
            return 0.0f;  // Empty region
        }
    );

    // Build using sampler path
    auto result = inject(*sampler, config);

    auto endTime = std::chrono::high_resolution_clock::now();
    m_stats.buildTimeSeconds = std::chrono::duration<float>(endTime - startTime).count();

    return result;
}

/**
 * Inject dense voxel grid into SVO.
 * Builds octree by traversing grid in Morton order.
 */
std::unique_ptr<ISVOStructure> VoxelInjector::inject(
    const DenseVoxelInput& input,
    const InjectionConfig& config) {

    m_stats = Stats{};
    auto startTime = std::chrono::high_resolution_clock::now();

    // Use shared_ptr to avoid expensive copies in lambdas
    auto voxelsPtr = std::make_shared<std::vector<VoxelData>>(input.voxels);
    glm::vec3 worldMinCopy = input.worldMin;
    glm::vec3 worldMaxCopy = input.worldMax;
    glm::ivec3 resolutionCopy = input.resolution;

    // Create lambda sampler from dense grid
    auto sampler = std::make_unique<LambdaVoxelSampler>(
        // Sample function - lookup in grid
        [voxelsPtr, worldMinCopy, worldMaxCopy, resolutionCopy](const glm::vec3& pos, VoxelData& data) -> bool {
            // Convert world position to grid coordinates
            glm::vec3 normalized = (pos - worldMinCopy) / (worldMaxCopy - worldMinCopy);
            glm::ivec3 gridPos = glm::clamp(
                glm::ivec3(normalized * glm::vec3(resolutionCopy)),
                glm::ivec3(0),
                resolutionCopy - 1
            );

            // Lookup voxel
            size_t idx = gridPos.x + gridPos.y * resolutionCopy.x + gridPos.z * resolutionCopy.x * resolutionCopy.y;
            const VoxelData& voxel = (*voxelsPtr)[idx];

            if (voxel.isSolid()) {
                data = voxel;
                return true;
            }
            return false;
        },
        // Bounds function
        [worldMinCopy, worldMaxCopy](glm::vec3& min, glm::vec3& max) {
            min = worldMinCopy;
            max = worldMaxCopy;
        },
        // Density estimator - count solid voxels in region
        [voxelsPtr, worldMinCopy, worldMaxCopy, resolutionCopy](const glm::vec3& center, float size) -> float {
            // Sample region and estimate density
            glm::vec3 halfSize(size * 0.5f);
            glm::vec3 regionMin = center - halfSize;
            glm::vec3 regionMax = center + halfSize;

            // Convert to grid coordinates
            glm::vec3 normMin = (regionMin - worldMinCopy) / (worldMaxCopy - worldMinCopy);
            glm::vec3 normMax = (regionMax - worldMinCopy) / (worldMaxCopy - worldMinCopy);

            glm::ivec3 gridMin = glm::clamp(
                glm::ivec3(normMin * glm::vec3(resolutionCopy)),
                glm::ivec3(0),
                resolutionCopy - 1
            );
            glm::ivec3 gridMax = glm::clamp(
                glm::ivec3(normMax * glm::vec3(resolutionCopy)),
                glm::ivec3(0),
                resolutionCopy - 1
            );

            // Count solid voxels in region
            int totalVoxels = 0;
            int solidVoxels = 0;

            for (int z = gridMin.z; z <= gridMax.z; ++z) {
                for (int y = gridMin.y; y <= gridMax.y; ++y) {
                    for (int x = gridMin.x; x <= gridMax.x; ++x) {
                        totalVoxels++;
                        size_t idx = x + y * resolutionCopy.x + z * resolutionCopy.x * resolutionCopy.y;
                        if ((*voxelsPtr)[idx].isSolid()) {
                            solidVoxels++;
                        }
                    }
                }
            }

            return totalVoxels > 0 ? (float)solidVoxels / (float)totalVoxels : 0.0f;
        }
    );

    // Build using sampler path
    auto result = inject(*sampler, config);

    auto endTime = std::chrono::high_resolution_clock::now();
    m_stats.buildTimeSeconds = std::chrono::duration<float>(endTime - startTime).count();

    return result;
}

/**
 * Inject procedural voxels via sampler.
 * Core implementation - builds octree by recursive subdivision.
 */
std::unique_ptr<ISVOStructure> VoxelInjector::inject(
    const IVoxelSampler& sampler,
    const InjectionConfig& config) {

    m_stats = Stats{};
    auto startTime = std::chrono::high_resolution_clock::now();

    // Get bounds from sampler
    glm::vec3 worldMin, worldMax;
    sampler.getBounds(worldMin, worldMax);

    // Build octree using recursive subdivision
    auto svo = buildFromSampler(sampler, worldMin, worldMax, config);

    auto endTime = std::chrono::high_resolution_clock::now();
    m_stats.buildTimeSeconds = std::chrono::duration<float>(endTime - startTime).count();

    if (m_progressCallback) {
        m_progressCallback(1.0f, "Voxel injection complete");
    }

    return svo;
}

/**
 * Core recursive builder for voxel injection.
 * Subdivides octree based on sampler density estimates.
 */
std::unique_ptr<ISVOStructure> VoxelInjector::buildFromSampler(
    const IVoxelSampler& sampler,
    const glm::vec3& min,
    const glm::vec3& max,
    const InjectionConfig& config) {

    // Build context for recursive subdivision
    struct VoxelNode {
        glm::vec3 aabbMin;
        glm::vec3 aabbMax;
        int level;
        bool isLeaf;
        VoxelData data;
        std::unique_ptr<VoxelNode> children[8];
    };

    // Recursive subdivision function
    std::function<std::unique_ptr<VoxelNode>(const glm::vec3&, const glm::vec3&, int)> subdivide =
        [&](const glm::vec3& voxelMin, const glm::vec3& voxelMax, int level) -> std::unique_ptr<VoxelNode> {

        m_stats.voxelsProcessed++;

        // Progress callback
        if (m_progressCallback && m_stats.voxelsProcessed % 1000 == 0) {
            float progress = std::min(0.99f, (float)level / (float)config.maxLevels);
            m_progressCallback(progress, "Building from voxel data");
        }

        glm::vec3 center = (voxelMin + voxelMax) * 0.5f;
        float size = voxelMax.x - voxelMin.x;

        // Check density estimate
        float density = sampler.estimateDensity(center, size);

        // Early termination if empty
        if (density == 0.0f) {
            m_stats.emptyVoxelsCulled++;
            return nullptr;
        }

        auto node = std::make_unique<VoxelNode>();
        node->aabbMin = voxelMin;
        node->aabbMax = voxelMax;
        node->level = level;

        // Termination criteria
        // Stop at max level, or when voxel is smaller than minimum size
        float minSize = (config.minVoxelSize > 0.0f) ? config.minVoxelSize : config.errorThreshold;
        bool shouldTerminate = (level >= config.maxLevels) || (size < minSize);

        if (shouldTerminate) {
            // Create leaf - sample voxel data
            VoxelData data;
            if (sampler.sample(center, data)) {
                node->data = data;
                node->isLeaf = true;
                m_stats.leavesCreated++;
                return node;
            } else {
                return nullptr;  // No solid voxel at this location
            }
        }

        // Subdivide into 8 children
        node->isLeaf = false;
        glm::vec3 childSize = (voxelMax - voxelMin) * 0.5f;

        static const glm::vec3 childOffsets[8] = {
            {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f},
            {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 1.0f},
            {0.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}
        };

        for (int i = 0; i < 8; ++i) {
            glm::vec3 childMin = voxelMin + childOffsets[i] * childSize;
            glm::vec3 childMax = childMin + childSize;

            node->children[i] = subdivide(childMin, childMax, level + 1);
        }

        return node;
    };

    // Build root
    auto root = subdivide(min, max, 0);

    if (!root) {
        return nullptr;  // Empty octree
    }

    // Convert to LaineKarras octree structure
    auto octree = std::make_unique<Octree>();
    octree->worldMin = min;
    octree->worldMax = max;
    octree->maxLevels = config.maxLevels;

    auto rootBlock = std::make_unique<OctreeBlock>();

    // Traverse and build octree blocks
    std::function<void(VoxelNode*)> traverse = [&](VoxelNode* node) {
        if (!node) return;

        // Add attributes
        if (node->isLeaf) {
            UncompressedAttributes attr{};

            // Set color components
            attr.red = static_cast<uint8_t>(glm::clamp(node->data.color.r, 0.0f, 1.0f) * 255);
            attr.green = static_cast<uint8_t>(glm::clamp(node->data.color.g, 0.0f, 1.0f) * 255);
            attr.blue = static_cast<uint8_t>(glm::clamp(node->data.color.b, 0.0f, 1.0f) * 255);
            attr.alpha = 255;

            // Encode normal using point-on-cube
            glm::vec3 n = glm::normalize(node->data.normal);
            glm::vec3 absN = glm::abs(n);

            // Find dominant axis
            int axis = 0;
            if (absN.y > absN.x && absN.y > absN.z) axis = 1;
            else if (absN.z > absN.x && absN.z > absN.y) axis = 2;

            // Determine sign bit
            int sign = (axis == 0 ? n.x : (axis == 1 ? n.y : n.z)) >= 0.0f ? 1 : 0;

            // Project to cube face (remap to [0, 1])
            glm::vec2 uv;
            switch (axis) {
                case 0: uv = glm::vec2(n.y / absN.x, n.z / absN.x); break;
                case 1: uv = glm::vec2(n.x / absN.y, n.z / absN.y); break;
                case 2: uv = glm::vec2(n.x / absN.z, n.y / absN.z); break;
            }
            uv = (uv + 1.0f) * 0.5f;
            uv = glm::clamp(uv, glm::vec2(0.0f), glm::vec2(1.0f));

            // Pack into bitfields
            attr.sign_and_axis = (axis << 1) | sign;
            attr.u_coordinate = static_cast<uint32_t>(uv.x * 32767.0f);
            attr.v_coordinate = static_cast<uint32_t>(uv.y * 16383.0f);

            rootBlock->attributes.push_back(attr);
        } else {
            // Build child descriptor
            ChildDescriptor desc{};
            desc.validMask = 0;
            desc.leafMask = 0;

            for (int i = 0; i < 8; ++i) {
                if (node->children[i]) {
                    desc.validMask |= (1 << i);
                    if (node->children[i]->isLeaf) {
                        desc.leafMask |= (1 << i);
                    }
                }
            }

            rootBlock->childDescriptors.push_back(desc);
        }

        // Recurse
        for (auto& child : node->children) {
            if (child) {
                traverse(child.get());
            }
        }
    };

    traverse(root.get());

    octree->root = std::move(rootBlock);
    octree->totalVoxels = m_stats.leavesCreated;

    // Wrap in LaineKarrasOctree
    auto result = std::make_unique<LaineKarrasOctree>();
    result->setOctree(std::move(octree));

    return result;
}

/**
 * Merge voxel data into existing SVO.
 * TODO: Implement proper octree merging.
 */
bool VoxelInjector::merge(
    ISVOStructure& target,
    const SparseVoxelInput& input,
    const InjectionConfig& config) {

    // Build new SVO from input
    auto newSVO = inject(input, config);

    if (!newSVO) {
        return false;
    }

    // TODO: Implement octree merge
    // For now, just log that merge is not yet implemented
    std::cout << "Warning: VoxelInjector::merge() not yet implemented\n";

    return false;
}

bool VoxelInjector::merge(
    ISVOStructure& target,
    const IVoxelSampler& sampler,
    const InjectionConfig& config) {

    // Build new SVO from sampler
    auto newSVO = inject(sampler, config);

    if (!newSVO) {
        return false;
    }

    // TODO: Implement octree merge
    std::cout << "Warning: VoxelInjector::merge() not yet implemented\n";

    return false;
}

} // namespace SVO
