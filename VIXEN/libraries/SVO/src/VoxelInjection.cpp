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

    // Create lambda sampler from sparse voxels
    auto sampler = std::make_unique<LambdaVoxelSampler>(
        // Sample function - lookup voxel in sparse list
        [&input](const glm::vec3& pos, VoxelData& data) -> bool {
            // Find closest voxel (simple linear search for now)
            // TODO: Use spatial hash or octree for faster lookup
            float minDist = std::numeric_limits<float>::max();
            const VoxelData* closest = nullptr;

            glm::vec3 voxelSize = (input.worldMax - input.worldMin) / float(input.resolution);
            float searchRadius = glm::length(voxelSize) * 0.5f;

            for (const auto& voxel : input.voxels) {
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
        [&input](glm::vec3& min, glm::vec3& max) {
            min = input.worldMin;
            max = input.worldMax;
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

    // Create lambda sampler from dense grid
    auto sampler = std::make_unique<LambdaVoxelSampler>(
        // Sample function - lookup in grid
        [&input](const glm::vec3& pos, VoxelData& data) -> bool {
            // Convert world position to grid coordinates
            glm::vec3 normalized = (pos - input.worldMin) / (input.worldMax - input.worldMin);
            glm::ivec3 gridPos = glm::clamp(
                glm::ivec3(normalized * glm::vec3(input.resolution)),
                glm::ivec3(0),
                input.resolution - 1
            );

            // Lookup voxel
            const VoxelData& voxel = input.at(gridPos.x, gridPos.y, gridPos.z);

            if (voxel.isSolid()) {
                data = voxel;
                return true;
            }
            return false;
        },
        // Bounds function
        [&input](glm::vec3& min, glm::vec3& max) {
            min = input.worldMin;
            max = input.worldMax;
        },
        // Density estimator - count solid voxels in region
        [&input](const glm::vec3& center, float size) -> float {
            // Sample region and estimate density
            glm::vec3 halfSize(size * 0.5f);
            glm::vec3 regionMin = center - halfSize;
            glm::vec3 regionMax = center + halfSize;

            // Convert to grid coordinates
            glm::vec3 normMin = (regionMin - input.worldMin) / (input.worldMax - input.worldMin);
            glm::vec3 normMax = (regionMax - input.worldMin) / (input.worldMax - input.worldMin);

            glm::ivec3 gridMin = glm::clamp(
                glm::ivec3(normMin * glm::vec3(input.resolution)),
                glm::ivec3(0),
                input.resolution - 1
            );
            glm::ivec3 gridMax = glm::clamp(
                glm::ivec3(normMax * glm::vec3(input.resolution)),
                glm::ivec3(0),
                input.resolution - 1
            );

            // Count solid voxels in region
            int totalVoxels = 0;
            int solidVoxels = 0;

            for (int z = gridMin.z; z <= gridMax.z; ++z) {
                for (int y = gridMin.y; y <= gridMax.y; ++y) {
                    for (int x = gridMin.x; x <= gridMax.x; ++x) {
                        totalVoxels++;
                        if (input.at(x, y, z).isSolid()) {
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
        bool shouldTerminate = (level >= config.maxLevels) || (density >= 0.99f && size < 0.001f);

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
            UncompressedAttributes attr;
            attr.color = (uint32_t(node->data.color.r * 255) << 0) |
                        (uint32_t(node->data.color.g * 255) << 8) |
                        (uint32_t(node->data.color.b * 255) << 16) |
                        (uint32_t(255) << 24);

            // Simple normal encoding
            glm::vec3 n = glm::normalize(node->data.normal);
            attr.normal = (uint32_t((n.x * 0.5f + 0.5f) * 1023) << 0) |
                         (uint32_t((n.y * 0.5f + 0.5f) * 1023) << 10) |
                         (uint32_t((n.z * 0.5f + 0.5f) * 1023) << 20);

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
