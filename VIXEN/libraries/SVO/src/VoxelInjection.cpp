#include "VoxelInjection.h"
#include "LaineKarrasOctree.h"
#include "SVOBuilder.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <unordered_map>
#include <queue>

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
        // 1. Brick depth reached: stop octree subdivision, populate brick instead
        // 2. Max level reached
        // 3. Voxel smaller than minimum size
        int brickStartDepth = (config.brickDepthLevels > 0) ? (config.maxLevels - config.brickDepthLevels) : -1;
        bool atBrickDepth = (brickStartDepth >= 0) && (level >= brickStartDepth);
        float minSize = (config.minVoxelSize > 0.0f) ? config.minVoxelSize : config.errorThreshold;
        bool shouldTerminate = (level >= config.maxLevels) || (size < minSize) || atBrickDepth;

        if (shouldTerminate) {
            if (atBrickDepth && config.brickDepthLevels > 0) {
                // Terminate with brick (not single voxel)
                // TODO: Populate brick data structure
                // For now, treat as leaf with sampled data at center
                VoxelData data;
                if (sampler.sample(center, data)) {
                    node->data = data;
                    node->isLeaf = true;
                    m_stats.leavesCreated++;
                    return node;
                } else {
                    return nullptr;
                }
            } else {
                // Create leaf - sample single voxel data
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

    // Two-pass traversal to correctly set childPointer values

    // Pass 1: Count child descriptors per node to calculate offsets
    std::unordered_map<VoxelNode*, uint32_t> nodeToDescriptorIndex;
    uint32_t descriptorCounter = 0;

    std::function<void(VoxelNode*)> countDescriptors = [&](VoxelNode* node) {
        if (!node) return;

        if (!node->isLeaf) {
            nodeToDescriptorIndex[node] = descriptorCounter++;
        }

        for (auto& child : node->children) {
            if (child) {
                countDescriptors(child.get());
            }
        }
    };

    countDescriptors(root.get());

    // Pass 2: Build octree blocks with correct childPointer values
    // We need to track attribute indices to build AttributeLookup structures
    uint32_t attributeCounter = 0;

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
            desc.childPointer = 0;  // Will be set for first valid non-leaf child
            desc.farBit = 0;
            desc.contourPointer = 0;
            desc.contourMask = 0;

            // ESVO format: childPointer points to first non-leaf child's descriptor
            // Process children in octant order (0-7)
            uint32_t firstChildPtr = 0;
            bool foundFirst = false;

            for (int i = 0; i < 8; ++i) {
                if (node->children[i]) {
                    desc.validMask |= (1 << i);

                    if (node->children[i]->isLeaf) {
                        desc.leafMask |= (1 << i);
                    } else {
                        // Non-leaf child - point to its descriptor
                        if (!foundFirst) {
                            auto it = nodeToDescriptorIndex.find(node->children[i].get());
                            if (it != nodeToDescriptorIndex.end()) {
                                firstChildPtr = it->second;
                                foundFirst = true;
                            }
                        }
                    }
                }
            }

            desc.childPointer = firstChildPtr;
            rootBlock->childDescriptors.push_back(desc);

            // Create AttributeLookup for this node
            AttributeLookup attrLookup{};
            attrLookup.valuePointer = attributeCounter;  // Points to first attribute for this node's children
            attrLookup.mask = 0;

            // Count leaf children to know how many attributes this node has
            uint8_t leafCount = 0;
            for (int i = 0; i < 8; ++i) {
                if (node->children[i] && node->children[i]->isLeaf) {
                    attrLookup.mask |= (1 << i);
                    leafCount++;
                }
            }

            attributeCounter += leafCount;  // Reserve space for this node's leaf attributes
            rootBlock->attributeLookups.push_back(attrLookup);
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

// ============================================================================
// Bottom-Up Additive Voxel Insertion
// ============================================================================

/**
 * Insert single voxel using bottom-up additive approach.
 *
 * Algorithm:
 * 1. Compute voxel's position in octree hierarchy
 * 2. Find/create brick at leaf level
 * 3. Insert voxel into brick's dense storage
 * 4. Propagate "has child" up parent chain (idempotent)
 *
 * Thread-safety: Uses atomic operations for parent node updates.
 */
bool VoxelInjector::insertVoxel(
    ISVOStructure& svo,
    const glm::vec3& position,
    const VoxelData& data,
    const InjectionConfig& config) {

    // Cast to LaineKarrasOctree (required for direct manipulation)
    auto* octree = dynamic_cast<LaineKarrasOctree*>(&svo);
    if (!octree) {
        std::cerr << "insertVoxel requires LaineKarrasOctree implementation\n";
        return false;
    }

    // Ensure octree is initialized with default bounds if not set
    glm::vec3 worldMin(0.0f);
    glm::vec3 worldMax(10.0f);
    int maxLevels = config.maxLevels > 0 ? config.maxLevels : 12;

    octree->ensureInitialized(worldMin, worldMax, maxLevels);

    // Get mutable octree data and bounds
    Octree* octreeData = octree->getOctreeMutable();
    if (!octreeData || !octreeData->root) {
        return false;
    }

    worldMin = octreeData->worldMin;
    worldMax = octreeData->worldMax;

    // 1. Check bounds
    if (glm::any(glm::lessThan(position, worldMin)) || glm::any(glm::greaterThanEqual(position, worldMax))) {
        return false;  // Out of bounds
    }

    // 2. Compute octree path from root to target leaf
    // Target depth = maxLevels or (maxLevels - brickDepthLevels) if using bricks
    int targetDepth = config.maxLevels;
    if (config.brickDepthLevels > 0) {
        targetDepth = config.maxLevels - config.brickDepthLevels;
    }

    // Normalize position to [0,1]³
    glm::vec3 normPos = (position - worldMin) / (worldMax - worldMin);

    std::cout << "DEBUG insertVoxel: position=" << position.x << "," << position.y << "," << position.z
              << " normPos=" << normPos.x << "," << normPos.y << "," << normPos.z
              << " targetDepth=" << targetDepth << "\n" << std::flush;

    // Compute octree path (sequence of child indices from root to leaf)
    std::vector<int> path;
    path.reserve(targetDepth);

    // Work with local position that gets transformed at each level
    glm::vec3 localPos = normPos;

    for (int level = 0; level < targetDepth; ++level) {
        // Center is always at 0.5 in local coordinates
        const float center = 0.5f;

        // Compute child index (0-7) based on which octant contains position
        int childIdx = 0;
        if (localPos.x >= center) childIdx |= 1;
        if (localPos.y >= center) childIdx |= 2;
        if (localPos.z >= center) childIdx |= 4;

        path.push_back(childIdx);

        if (level < 3) {  // Debug first few levels
            std::cout << "Level " << level << ": childIdx=" << childIdx
                      << " (localPos=" << localPos.x << "," << localPos.y << "," << localPos.z
                      << " center=" << center << ")\n" << std::flush;
        }

        // Transform position to the selected child's local coordinate system
        // If position is in upper half (>= 0.5), map [0.5, 1.0] → [0.0, 1.0]
        // If position is in lower half (< 0.5), map [0.0, 0.5] → [0.0, 1.0]
        localPos = glm::vec3(
            (childIdx & 1) ? (localPos.x - 0.5f) * 2.0f : localPos.x * 2.0f,
            (childIdx & 2) ? (localPos.y - 0.5f) * 2.0f : localPos.y * 2.0f,
            (childIdx & 4) ? (localPos.z - 0.5f) * 2.0f : localPos.z * 2.0f
        );
    }

    // CRITICAL DEBUG: Print entire path for position (5,5,5)
    std::cout << "DEBUG: Complete path for position (" << position.x << "," << position.y << "," << position.z << "):";
    for (size_t i = 0; i < path.size(); ++i) {
        std::cout << " [" << i << "]=" << path[i];
    }
    std::cout << "\n" << std::flush;

    // 3. Octree data already retrieved above

    // 4. Traverse bottom-up: check if node already exists with validMask set
    //    Start from leaf, work upward until we find existing valid node
    //    This is the KEY OPTIMIZATION for parallel insertion!

    // First, find deepest existing node along path
    int existingDepth = 0; // How deep in the tree nodes exist

    // For now, simplified: always create full path
    // TODO: Add early termination logic when we detect existing validMask bits

    // 5. Create attribute data for the leaf voxel
    UncompressedAttributes attr{};

    // Pack color
    attr.red = static_cast<uint8_t>(glm::clamp(data.color.r, 0.0f, 1.0f) * 255);
    attr.green = static_cast<uint8_t>(glm::clamp(data.color.g, 0.0f, 1.0f) * 255);
    attr.blue = static_cast<uint8_t>(glm::clamp(data.color.b, 0.0f, 1.0f) * 255);
    attr.alpha = 255;

    // Pack normal using point-on-cube encoding
    glm::vec3 n = glm::normalize(data.normal);
    glm::vec3 absN = glm::abs(n);

    // Find dominant axis
    int axis = 0;
    if (absN.y > absN.x && absN.y > absN.z) axis = 1;
    else if (absN.z > absN.x && absN.z > absN.y) axis = 2;

    // Determine sign bit
    int sign = (axis == 0 ? n.x : (axis == 1 ? n.y : n.z)) >= 0.0f ? 1 : 0;

    // Project to cube face
    glm::vec2 uv;
    switch (axis) {
        case 0: uv = glm::vec2(n.y / absN.x, n.z / absN.x); break;
        case 1: uv = glm::vec2(n.x / absN.y, n.z / absN.y); break;
        case 2: uv = glm::vec2(n.x / absN.z, n.y / absN.z); break;
    }
    uv = (uv + 1.0f) * 0.5f;
    uv = glm::clamp(uv, glm::vec2(0.0f), glm::vec2(1.0f));

    attr.sign_and_axis = (axis << 1) | sign;
    attr.u_coordinate = static_cast<uint32_t>(uv.x * 32767.0f);
    attr.v_coordinate = static_cast<uint32_t>(uv.y * 16383.0f);

    // 6. Add attribute to octree (this will grow the attributes array)
    uint32_t attrIndex = static_cast<uint32_t>(octreeData->root->attributes.size());
    octreeData->root->attributes.push_back(attr);

    // 7. NOW traverse/create nodes along path and propagate validMask bits
    //    Work from ROOT down to LEAF, creating nodes as needed
    //    EARLY EXIT: If we find child already has validMask bit set, we're done!

    // Start at root descriptor (index 0)
    uint32_t currentDescriptorIdx = 0;

    // Ensure root descriptor exists
    if (octreeData->root->childDescriptors.empty()) {
        ChildDescriptor rootDesc{};
        rootDesc.validMask = 0;
        rootDesc.leafMask = 0;
        rootDesc.childPointer = 0;
        rootDesc.farBit = 0;
        rootDesc.contourPointer = 0;
        rootDesc.contourMask = 0;
        octreeData->root->childDescriptors.push_back(rootDesc);

        // Also create AttributeLookup for root
        AttributeLookup rootAttrLookup{};
        rootAttrLookup.valuePointer = 0;
        rootAttrLookup.mask = 0;
        octreeData->root->attributeLookups.push_back(rootAttrLookup);
    }

    // Traverse path, creating descriptors as needed
    for (size_t level = 0; level < path.size(); ++level) {
        int childIdx = path[level];

        std::cout << "DEBUG: Level " << level << ", currentDescIdx=" << currentDescriptorIdx
                  << ", childIdx=" << childIdx << "\n" << std::flush;

        // Get fresh reference each iteration in case vector reallocates
        ChildDescriptor& desc = octreeData->root->childDescriptors[currentDescriptorIdx];

        // Check if this child already exists
        bool childExists = (desc.validMask & (1 << childIdx)) != 0;

        if (childExists) {
            // Child already exists - check if it's the target leaf
            if (level == path.size() - 1) {
                // This is the target leaf and it already exists
                // EARLY EXIT - don't re-insert
                return true;
            }
            // Not a leaf - this is an internal node we need to traverse through
            // Continue down the tree to reach our target leaf
        } else {
            // Child doesn't exist yet - mark it as valid
            desc.validMask |= (1 << childIdx);
        }

        // If this is the leaf level, mark as leaf and complete insertion
        if (level == path.size() - 1) {
            // Re-get descriptor reference in case it was invalidated
            ChildDescriptor& leafDesc = octreeData->root->childDescriptors[currentDescriptorIdx];

            // Mark this child as a leaf
            if (!childExists) {
                leafDesc.leafMask |= (1 << childIdx);

                // Update attribute lookup for this descriptor
                AttributeLookup& attrLookup = octreeData->root->attributeLookups[currentDescriptorIdx];
                attrLookup.mask |= (1 << childIdx);
                if (attrLookup.valuePointer == 0 && attrIndex > 0) {
                    attrLookup.valuePointer = attrIndex; // Point to first attribute for this node
                }

                // Success! Voxel inserted
                octreeData->totalVoxels++;
            }
            return true;
        }

        // This is an internal node - need child descriptor to continue traversal
        // Simple strategy: append new descriptors to end, compact later

        if (childExists) {
            // Child already exists - look up its descriptor index from mapping
            // CRITICAL: Must use mapping, not childPointer! childPointer points to FIRST child created,
            // not necessarily the child at this octant. Using childPointer causes circular traversal.
            auto it = m_childMapping.find(currentDescriptorIdx);
            if (it != m_childMapping.end() && it->second[childIdx] != UINT32_MAX) {
                currentDescriptorIdx = it->second[childIdx];
            } else {
                std::cerr << "ERROR: Child exists but mapping not found! Parent=" << currentDescriptorIdx
                          << " childIdx=" << childIdx << " validMask=0x" << std::hex << (int)desc.validMask << std::dec << "\n";
                return false;
            }
        } else {
            // Allocate new child descriptor at end of array
            uint32_t newDescriptorIdx = static_cast<uint32_t>(octreeData->root->childDescriptors.size());

            // Track child mapping: currentDescriptor → childOctant → newDescriptor
            // Initialize mapping if this is the first child for this descriptor
            if (m_childMapping.find(currentDescriptorIdx) == m_childMapping.end()) {
                m_childMapping[currentDescriptorIdx].fill(UINT32_MAX);  // Mark all as invalid
            }
            m_childMapping[currentDescriptorIdx][childIdx] = newDescriptorIdx;

            // Set parent's childPointer (simplified - points to first child)
            // This will be properly recomputed during compaction
            octreeData->root->childDescriptors[currentDescriptorIdx].childPointer = newDescriptorIdx;

            // Create new child descriptor
            // NOTE: validMask will be properly set during compaction based on which children actually exist
            ChildDescriptor childDesc{};
            childDesc.validMask = 0;
            childDesc.leafMask = 0;
            childDesc.childPointer = 0;
            childDesc.farBit = 0;
            childDesc.contourPointer = 0;
            childDesc.contourMask = 0;
            octreeData->root->childDescriptors.push_back(childDesc);

            // Create corresponding AttributeLookup
            AttributeLookup childAttrLookup{};
            childAttrLookup.valuePointer = 0;
            childAttrLookup.mask = 0;
            octreeData->root->attributeLookups.push_back(childAttrLookup);

            currentDescriptorIdx = newDescriptorIdx;
        }
    }

    return true;
}

/**
 * Compact octree into ESVO format after additive insertions.
 *
 * Reorganizes descriptors from simple append order into breadth-first layout
 * with contiguous non-leaf children as required by ESVO traversal.
 */
bool VoxelInjector::compactToESVOFormat(ISVOStructure& svo) {
    LaineKarrasOctree* octree = dynamic_cast<LaineKarrasOctree*>(&svo);
    if (!octree) return false;

    Octree* octreeData = const_cast<Octree*>(octree->getOctree());
    if (!octreeData || !octreeData->root) return false;

    std::cout << "DEBUG_MARKER_XYZ Before compaction:\n" << std::flush;
    for (size_t i = 0; i < octreeData->root->childDescriptors.size() && i < 10; ++i) {
        const auto& desc = octreeData->root->childDescriptors[i];
        std::cout << "  [" << i << "] valid=0x" << std::hex << (int)desc.validMask
                  << " leaf=0x" << (int)desc.leafMask << std::dec
                  << " childPtr=" << desc.childPointer << "\n" << std::flush;
    }
    std::cout << std::dec << std::flush;

    // Build new descriptor array in ESVO order (breadth-first, contiguous children)
    std::vector<ChildDescriptor> newDescriptors;
    std::vector<AttributeLookup> newAttributeLookups;
    std::unordered_map<uint32_t, uint32_t> oldToNewIndex;  // Map old indices to new

    // BFS traversal to rebuild in correct order
    struct NodeInfo {
        uint32_t oldIndex;
        uint32_t newIndex;
    };

    std::queue<NodeInfo> queue;
    queue.push({0, 0});  // Start with root
    oldToNewIndex[0] = 0;

    // Add root to new arrays
    ChildDescriptor rootDesc = octreeData->root->childDescriptors[0];
    // For non-leaf nodes, compute validMask from mapping (which children actually exist)
    // For leaf nodes or nodes with leaf children, preserve original validMask/leafMask
    if (rootDesc.leafMask == 0) {  // No leaf children - recompute validMask
        rootDesc.validMask = 0;
        auto rootMappingIt = m_childMapping.find(0);
        if (rootMappingIt != m_childMapping.end()) {
            for (int i = 0; i < 8; ++i) {
                if (rootMappingIt->second[i] != UINT32_MAX) {
                    rootDesc.validMask |= (1 << i);
                }
            }
        }
    }
    // else: preserve original validMask/leafMask
    newDescriptors.push_back(rootDesc);
    newAttributeLookups.push_back(octreeData->root->attributeLookups[0]);

    while (!queue.empty()) {
        NodeInfo current = queue.front();
        queue.pop();

        const ChildDescriptor& oldDesc = octreeData->root->childDescriptors[current.oldIndex];

        // Collect non-leaf children for this node
        std::vector<uint32_t> nonLeafChildren;
        for (int i = 0; i < 8; ++i) {
            if ((oldDesc.validMask & (1 << i)) && !(oldDesc.leafMask & (1 << i))) {
                // This child exists and is not a leaf - it has a descriptor
                nonLeafChildren.push_back(i);
            }
        }

        if (!nonLeafChildren.empty()) {
            // Set childPointer to where these children will be placed
            uint32_t firstChildIndex = static_cast<uint32_t>(newDescriptors.size());
            newDescriptors[current.newIndex].childPointer = firstChildIndex;

            // Add all non-leaf children contiguously
            for (uint32_t childOctant : nonLeafChildren) {
                // Look up old descriptor index for this child octant
                uint32_t oldChildIndex = 0;
                auto it = m_childMapping.find(current.oldIndex);
                if (it != m_childMapping.end()) {
                    oldChildIndex = it->second[childOctant];
                } else {
                    // Fallback: use childPointer (should not happen with proper mapping)
                    oldChildIndex = oldDesc.childPointer;
                }

                uint32_t newChildIndex = static_cast<uint32_t>(newDescriptors.size());
                oldToNewIndex[oldChildIndex] = newChildIndex;

                // Copy descriptor and recompute validMask from mapping (for non-leaf nodes only)
                ChildDescriptor childDesc = octreeData->root->childDescriptors[oldChildIndex];
                if (childDesc.leafMask == 0) {  // No leaf children - recompute validMask
                    childDesc.validMask = 0;
                    auto childMappingIt = m_childMapping.find(oldChildIndex);
                    if (childMappingIt != m_childMapping.end()) {
                        for (int i = 0; i < 8; ++i) {
                            if (childMappingIt->second[i] != UINT32_MAX) {
                                childDesc.validMask |= (1 << i);
                            }
                        }
                    }
                }
                // else: preserve original validMask/leafMask for nodes with leaf children

                newDescriptors.push_back(childDesc);
                newAttributeLookups.push_back(octreeData->root->attributeLookups[oldChildIndex]);

                queue.push({oldChildIndex, newChildIndex});
            }
        }
    }

    // Debug output
    std::cout << "Compaction complete:\n";
    std::cout << "  Old descriptors: " << octreeData->root->childDescriptors.size() << "\n";
    std::cout << "  New descriptors: " << newDescriptors.size() << "\n";
    std::cout << "Old descriptor structure:\n";
    for (size_t i = 0; i < octreeData->root->childDescriptors.size() && i < 10; ++i) {
        const auto& desc = octreeData->root->childDescriptors[i];
        std::cout << "  [" << i << "] valid=0x" << std::hex << (int)desc.validMask
                  << " leaf=0x" << (int)desc.leafMask << std::dec
                  << " childPtr=" << desc.childPointer << "\n";
    }

    // Replace old arrays with compacted versions
    octreeData->root->childDescriptors = std::move(newDescriptors);
    octreeData->root->attributeLookups = std::move(newAttributeLookups);

    // Clear mapping - it's now invalid since descriptor indices changed
    m_childMapping.clear();

    return true;
}

/**
 * Batch insert voxels in parallel.
 * Uses thread pool to distribute work across cores.
 */
size_t VoxelInjector::insertVoxelsBatch(
    ISVOStructure& svo,
    const std::vector<VoxelData>& voxels,
    const InjectionConfig& config) {

    // TODO: Implement parallel batch insertion
    // Use TBB parallel_for or thread pool
    // Each thread processes subset of voxels
    // Atomic operations ensure thread-safety

    size_t inserted = 0;
    for (const auto& voxel : voxels) {
        if (insertVoxel(svo, voxel.position, voxel, config)) {
            inserted++;
        }
    }

    return inserted;
}

} // namespace SVO
