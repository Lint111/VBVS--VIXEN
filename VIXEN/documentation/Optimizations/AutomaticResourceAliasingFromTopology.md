# Automatic Resource Aliasing from Graph Topology

**Document Version**: 1.0
**Created**: 2025-11-11
**Status**: Design Enhancement
**Related**: CompleteSystemAbsorption.md, UnifiedRM-CapabilityGaps.md

---

## Executive Summary

**Key Insight**: Resource aliasing lifetimes don't need manual specification - they can be **automatically derived** from GraphTopology's execution order!

**Benefits**:
- ✅ **Zero manual configuration** - topology drives everything
- ✅ **Always correct** - derived from actual execution order
- ✅ **Self-updating** - graph modifications automatically update aliasing
- ✅ **Future-proof** - works with wave-based parallelism
- ✅ **Handles conditionals** - nodes that don't always execute

---

## Problem with Manual Aliasing Hints

### Current Design (Manual Specification)

```cpp
// Manual lifetime specification - error-prone!
UnifiedRM::AliasingHint hint;
hint.lifetimeStart = 0;    // ❌ Developer must track this
hint.lifetimeEnd = 5;      // ❌ What if graph changes?
hint.aliasGroup = "transient";
tempBuffer.SetAliasingHint(hint);
```

**Problems**:
1. ❌ **Manual tracking** - developer must know execution order
2. ❌ **Error-prone** - easy to get lifetimes wrong
3. ❌ **Stale on modification** - add/remove nodes → hints become wrong
4. ❌ **No validation** - incorrect hints won't be detected
5. ❌ **Maintenance burden** - every graph change requires hint updates

---

## Solution: Automatic Lifetime Derivation

### Key Concept: GraphTopology Already Knows Everything!

```cpp
// GraphTopology already computes:
auto executionOrder = topology.TopologicalSort();
// → [DeviceNode, WindowNode, SwapChainNode, FramebufferNode, ...]

// For each resource, we can determine:
// - Creation point: When producer node executes
// - Last use point: When last consumer node executes
// → Lifetime interval = [creation, lastUse]

// Resources with non-overlapping intervals can alias automatically!
```

---

## Architecture: ResourceLifetimeAnalyzer

### New Component in GraphTopology

```cpp
class GraphTopology {
public:
    // ... existing methods ...

    // NEW: Resource lifetime analysis
    ResourceLifetimeAnalyzer& GetLifetimeAnalyzer() { return lifetimeAnalyzer_; }

    // NEW: Called after topology changes
    void UpdateResourceTimelines();

private:
    ResourceLifetimeAnalyzer lifetimeAnalyzer_;
};

/**
 * @brief Analyzes resource lifetimes from graph execution order
 *
 * Automatically computes:
 * - When each resource is created (producer execution)
 * - When each resource is last used (last consumer execution)
 * - Which resources have non-overlapping lifetimes (aliasing candidates)
 */
class ResourceLifetimeAnalyzer {
public:
    struct ResourceTimeline {
        UnifiedRM_Base* resource;
        NodeInstance* producer;
        std::vector<NodeInstance*> consumers;

        // Execution indices (from topological sort)
        uint32_t birthIndex;      // When produced
        uint32_t deathIndex;      // Last use
        uint32_t executionWave;   // For parallel execution (future)

        // Lifetime scope
        LifetimeScope scope;      // Frame, Pass, Subpass, Transient

        // Computed properties
        bool isTransient() const {
            return deathIndex - birthIndex < 5; // Short-lived
        }

        bool overlaps(const ResourceTimeline& other) const {
            // Check if lifetimes overlap
            return !(deathIndex < other.birthIndex ||
                     other.deathIndex < birthIndex);
        }

        size_t lifetimeLength() const {
            return deathIndex - birthIndex;
        }
    };

    enum class LifetimeScope {
        Transient,    // Single pass
        Subpass,      // Within subpass
        Pass,         // Entire render pass
        Frame,        // Entire frame
        Persistent    // Multiple frames
    };

    // Compute resource timelines from topology
    void ComputeTimelines(
        const std::vector<NodeInstance*>& executionOrder,
        const std::vector<GraphEdge>& edges
    );

    // Query resource lifetime
    const ResourceTimeline* GetTimeline(UnifiedRM_Base* resource) const;

    // Find aliasing candidates
    std::vector<UnifiedRM_Base*> FindAliasingCandidates(
        UnifiedRM_Base* resource
    ) const;

    // Get all resources that can share memory
    std::vector<std::vector<UnifiedRM_Base*>> ComputeAliasingGroups() const;

    // Validate timelines (debug)
    bool ValidateTimelines(std::string& errorMessage) const;

    // Clear and reset
    void Clear();

private:
    // Resource timeline map
    std::unordered_map<UnifiedRM_Base*, ResourceTimeline> timelines_;

    // Execution order cache
    std::vector<NodeInstance*> executionOrder_;

    // Helper: Find last consumer in execution order
    uint32_t FindLastConsumerIndex(
        NodeInstance* producer,
        const std::vector<NodeInstance*>& consumers
    ) const;

    // Helper: Determine lifetime scope
    LifetimeScope DetermineScope(
        uint32_t birthIndex,
        uint32_t deathIndex
    ) const;
};
```

---

## Automatic Timeline Computation

### Phase 1: Execution Order Assignment

```cpp
void ResourceLifetimeAnalyzer::ComputeTimelines(
    const std::vector<NodeInstance*>& executionOrder,
    const std::vector<GraphEdge>& edges
) {
    executionOrder_ = executionOrder;
    timelines_.clear();

    // Step 1: Create execution index map
    std::unordered_map<NodeInstance*, uint32_t> nodeToIndex;
    for (uint32_t i = 0; i < executionOrder.size(); ++i) {
        nodeToIndex[executionOrder[i]] = i;
    }

    // Step 2: For each edge, track resource lifetime
    for (const auto& edge : edges) {
        // Get resource being passed
        UnifiedRM_Base* resource = edge.source->GetOutput(edge.sourceOutputIndex);

        if (!resource) continue;

        // Create or update timeline
        auto& timeline = timelines_[resource];
        timeline.resource = resource;
        timeline.producer = edge.source;
        timeline.consumers.push_back(edge.target);

        // Birth: When producer executes
        timeline.birthIndex = nodeToIndex[edge.source];

        // Death: Maximum of all consumer indices
        uint32_t consumerIndex = nodeToIndex[edge.target];
        timeline.deathIndex = std::max(timeline.deathIndex, consumerIndex);
    }

    // Step 3: Determine lifetime scopes
    for (auto& [resource, timeline] : timelines_) {
        timeline.scope = DetermineScope(timeline.birthIndex, timeline.deathIndex);

        // Set aliasing group based on scope
        std::string groupID;
        switch (timeline.scope) {
            case LifetimeScope::Transient:
                groupID = "transient";
                break;
            case LifetimeScope::Pass:
                groupID = "pass_scoped";
                break;
            case LifetimeScope::Frame:
                groupID = "frame_scoped";
                break;
            default:
                groupID = "persistent";
        }

        // Update resource's aliasing hint automatically
        if (auto* unifiedRM = dynamic_cast<UnifiedRM_Base*>(resource)) {
            unifiedRM->SetMetadata("aliasing_group", groupID);
            unifiedRM->SetMetadata("birth_index", timeline.birthIndex);
            unifiedRM->SetMetadata("death_index", timeline.deathIndex);
        }
    }

    LOG_DEBUG("Computed " + std::to_string(timelines_.size()) + " resource timelines");
}
```

---

### Phase 2: Aliasing Group Formation

```cpp
std::vector<std::vector<UnifiedRM_Base*>>
ResourceLifetimeAnalyzer::ComputeAliasingGroups() const {
    std::vector<std::vector<UnifiedRM_Base*>> groups;

    // Group resources by scope first
    std::unordered_map<LifetimeScope, std::vector<UnifiedRM_Base*>> scopeGroups;

    for (const auto& [resource, timeline] : timelines_) {
        scopeGroups[timeline.scope].push_back(resource);
    }

    // Within each scope, find non-overlapping resources
    for (const auto& [scope, resources] : scopeGroups) {
        // Use interval scheduling algorithm
        auto aliasingGroups = ComputeIntervalScheduling(resources);
        groups.insert(groups.end(), aliasingGroups.begin(), aliasingGroups.end());
    }

    return groups;
}

std::vector<std::vector<UnifiedRM_Base*>>
ResourceLifetimeAnalyzer::ComputeIntervalScheduling(
    const std::vector<UnifiedRM_Base*>& resources
) const {
    // Classic interval scheduling problem
    // Resources with non-overlapping [birth, death] can share memory

    std::vector<std::vector<UnifiedRM_Base*>> groups;

    for (auto* resource : resources) {
        const auto& timeline = timelines_.at(resource);

        // Try to fit into existing group
        bool fitted = false;
        for (auto& group : groups) {
            // Check if this resource overlaps with any in group
            bool overlaps = false;
            for (auto* existing : group) {
                const auto& existingTimeline = timelines_.at(existing);
                if (timeline.overlaps(existingTimeline)) {
                    overlaps = true;
                    break;
                }
            }

            if (!overlaps) {
                // Can alias with this group!
                group.push_back(resource);
                fitted = true;
                break;
            }
        }

        if (!fitted) {
            // Create new group
            groups.push_back({resource});
        }
    }

    return groups;
}
```

---

## Integration with UnifiedBudgetManager

### Automatic Aliasing Pool Creation

```cpp
class UnifiedBudgetManager {
public:
    // NEW: Create aliasing pools from topology analysis
    void UpdateAliasingPoolsFromTopology(
        const ResourceLifetimeAnalyzer& analyzer
    ) {
        // Clear existing pools
        aliasingPools_.clear();

        // Get computed aliasing groups
        auto groups = analyzer.ComputeAliasingGroups();

        for (size_t groupIdx = 0; groupIdx < groups.size(); ++groupIdx) {
            const auto& group = groups[groupIdx];

            if (group.size() < 2) continue; // No aliasing needed

            // Create aliasing pool
            AliasingPool pool;
            pool.groupID = "auto_alias_" + std::to_string(groupIdx);

            // Find maximum size needed
            size_t maxSize = 0;
            for (auto* resource : group) {
                maxSize = std::max(maxSize, resource->GetAllocatedBytes());
            }

            // Allocate shared memory
            pool.totalSize = maxSize;
            pool.memory = AllocateSharedDeviceMemory(maxSize);

            // Register resources
            for (auto* resource : group) {
                const auto* timeline = analyzer.GetTimeline(resource);
                pool.aliasedResources.push_back(resource);
                pool.lifetimes.push_back({timeline->birthIndex, timeline->deathIndex});

                // Bind resource to shared memory
                resource->SetMetadata("aliased_memory", pool.memory);
                resource->SetMetadata("aliasing_pool", pool.groupID);
            }

            aliasingPools_[pool.groupID] = pool;

            LOG_INFO("Created aliasing pool '" + pool.groupID + "': " +
                     std::to_string(group.size()) + " resources, " +
                     FormatBytes(maxSize) + " shared");
        }
    }

    // Report aliasing efficiency
    void PrintAliasingReport() const {
        size_t totalAliased = 0;
        size_t totalMemory = 0;
        size_t memoryIfNoAliasing = 0;

        for (const auto& [groupID, pool] : aliasingPools_) {
            totalAliased += pool.aliasedResources.size();
            totalMemory += pool.totalSize;

            for (auto* resource : pool.aliasedResources) {
                memoryIfNoAliasing += resource->GetAllocatedBytes();
            }
        }

        float savings = 100.0f * (1.0f - float(totalMemory) / float(memoryIfNoAliasing));

        LOG_INFO("=== Automatic Aliasing Report ===");
        LOG_INFO("Aliasing Pools: " + std::to_string(aliasingPools_.size()));
        LOG_INFO("Aliased Resources: " + std::to_string(totalAliased));
        LOG_INFO("Memory Allocated: " + FormatBytes(totalMemory));
        LOG_INFO("Memory If No Aliasing: " + FormatBytes(memoryIfNoAliasing));
        LOG_INFO("Savings: " + std::to_string(savings) + "%");
    }
};
```

---

## Graph Modification Handling

### Automatic Timeline Updates

```cpp
class RenderGraph {
public:
    void Compile() {
        // Step 1: Compute topology
        topology_.Clear();
        for (auto* node : nodes_) {
            topology_.AddNode(node);
        }
        for (const auto& connection : connections_) {
            topology_.AddEdge(connection);
        }

        // Step 2: Get execution order
        auto executionOrder = topology_.TopologicalSort();

        // Step 3: Compute resource timelines automatically
        topology_.UpdateResourceTimelines();

        // Step 4: Update aliasing pools from timelines
        budgetManager_->UpdateAliasingPoolsFromTopology(
            topology_.GetLifetimeAnalyzer()
        );

        // Step 5: Compile nodes...
        for (auto* node : executionOrder) {
            node->Compile();
        }
    }

    void AddNode(const std::string& typeName, const std::string& instanceName) {
        // Add node...

        // Mark topology dirty
        topologyDirty_ = true;
    }

    void ConnectNodes(NodeHandle from, uint32_t outIdx, NodeHandle to, uint32_t inIdx) {
        // Connect nodes...

        // Mark topology dirty
        topologyDirty_ = true;
    }

    void RemoveNode(NodeHandle handle) {
        // Remove node...

        // Mark topology dirty
        topologyDirty_ = true;
    }

private:
    bool topologyDirty_ = true;
};

void GraphTopology::UpdateResourceTimelines() {
    // Get current execution order
    auto executionOrder = TopologicalSort();

    // Recompute timelines
    lifetimeAnalyzer_.ComputeTimelines(executionOrder, edges);

    LOG_DEBUG("Resource timelines updated (graph modified)");
}
```

---

## Example: Automatic Aliasing in Action

### Graph Setup

```cpp
// Create render graph
RenderGraph graph;

// Node 0: SwapChainNode (produces swapchain images)
auto swapchain = graph.AddNode("SwapChain", "swapchain");

// Node 1: DepthPrepass (produces depth buffer - temporary)
auto depthPrepass = graph.AddNode("DepthPrepass", "depthPrepass");
graph.ConnectNodes(swapchain, 0, depthPrepass, 0);

// Node 2: MainRender (produces color buffer - temporary)
auto mainRender = graph.AddNode("MainRender", "mainRender");
graph.ConnectNodes(swapchain, 0, mainRender, 0);
graph.ConnectNodes(depthPrepass, 0, mainRender, 1);

// Node 3: PostProcess (produces final image)
auto postProcess = graph.AddNode("PostProcess", "postProcess");
graph.ConnectNodes(mainRender, 0, postProcess, 0);

// Node 4: Present
auto present = graph.AddNode("Present", "present");
graph.ConnectNodes(postProcess, 0, present, 0);

// Compile graph
graph.Compile();
```

### Automatic Timeline Computation

```
Execution Order (Topological Sort):
  0: SwapChainNode
  1: DepthPrepass
  2: MainRender
  3: PostProcess
  4: Present

Resource Timelines (Automatic):
  SwapChain.colorImage:
    Birth: 0 (SwapChainNode)
    Death: 4 (Present)
    Scope: Frame
    Group: "frame_scoped"

  DepthPrepass.depthBuffer:
    Birth: 1 (DepthPrepass)
    Death: 2 (MainRender - last consumer)
    Scope: Transient
    Group: "transient"

  MainRender.colorBuffer:
    Birth: 2 (MainRender)
    Death: 3 (PostProcess - last consumer)
    Scope: Transient
    Group: "transient"

  PostProcess.finalImage:
    Birth: 3 (PostProcess)
    Death: 4 (Present)
    Scope: Pass
    Group: "pass_scoped"

Aliasing Analysis:
  DepthPrepass.depthBuffer: [1, 2]
  MainRender.colorBuffer:   [2, 3]
  → NON-OVERLAPPING! Can alias!

Aliasing Pool Created:
  Group: "auto_alias_0"
  Resources: DepthPrepass.depthBuffer, MainRender.colorBuffer
  Size: max(depth, color) = 512 MB
  Savings: 512 MB (50%)
```

### Output

```
=== Automatic Aliasing Report ===
Aliasing Pools: 1
Aliased Resources: 2
Memory Allocated: 512 MB
Memory If No Aliasing: 1024 MB
Savings: 50.0%
```

**No manual configuration needed!** Graph topology determined aliasing automatically.

---

## Handling Complex Scenarios

### Scenario 1: Conditional Execution

```cpp
// Node that only executes conditionally
class DebugVisualizationNode {
    void ExecuteImpl(TypedExecuteContext& ctx) override {
        if (!debugEnabled) {
            return; // Don't execute
        }
        // ...
    }
};
```

**Solution**: Conservative lifetime estimation
```cpp
// Even if node doesn't execute, assume it might
// Timeline: [debugViz.birthIndex, debugViz.deathIndex]
// Conservative = safe aliasing (might not alias as much, but correct)
```

**Future Enhancement**: Track conditional branches
```cpp
struct ResourceTimeline {
    bool isConditional;
    float executionProbability;  // 0.0 = never, 1.0 = always
};
```

---

### Scenario 2: Wave-Based Parallelism (Future)

```cpp
// Execution timeline with waves
Execution Order:
  Wave 0: [Node A, Node B]  // Parallel
  Wave 1: [Node C]
  Wave 2: [Node D, Node E]  // Parallel

Resource Timelines:
  A.output: [Wave 0, Wave 1]  // Used by C in wave 1
  B.output: [Wave 0, Wave 2]  // Used by D in wave 2
  → CAN ALIAS (different waves)!
```

**Implementation**:
```cpp
struct ResourceTimeline {
    uint32_t birthWave;
    uint32_t deathWave;

    bool overlapsWave(const ResourceTimeline& other) const {
        return !(deathWave < other.birthWave ||
                 other.deathWave < birthWave);
    }
};
```

---

### Scenario 3: Multiple Graph Variations

```cpp
// Graph can have different configurations
if (useHighQuality) {
    graph.ConnectNodes(bloom, 0, tonemap, 0);
} else {
    graph.ConnectNodes(mainRender, 0, tonemap, 0);
}
```

**Solution**: Recompute timelines on each Compile()
```cpp
void RenderGraph::Compile() {
    // Topology changes → timelines recomputed automatically
    topology_.UpdateResourceTimelines();
    budgetManager_->UpdateAliasingPoolsFromTopology(
        topology_.GetLifetimeAnalyzer()
    );
}
```

**Timelines are always synchronized with current graph state!**

---

## Performance Considerations

### Computation Cost

**Timeline Computation**: O(N + E)
- N = number of nodes
- E = number of edges
- Same complexity as topological sort

**Aliasing Group Computation**: O(R²) worst case
- R = number of resources
- Interval scheduling with greedy algorithm
- Typically R << N (many nodes share few resources)

**When to Recompute**:
- ✅ On graph modification (add/remove/connect nodes)
- ✅ On Compile() (topology changed)
- ❌ NOT on every frame (topology stable during execution)

**Caching**:
```cpp
class GraphTopology {
    bool timelinesDirty_ = true;

    void UpdateResourceTimelines() {
        if (!timelinesDirty_) return;  // Skip if unchanged

        lifetimeAnalyzer_.ComputeTimelines(TopologicalSort(), edges);
        timelinesDirty_ = false;
    }

    void AddNode(NodeInstance* node) {
        nodes.insert(node);
        timelinesDirty_ = true;  // Mark dirty
    }
};
```

---

## Benefits Summary

| Aspect | Manual Hints | Automatic (Topology) |
|--------|--------------|---------------------|
| **Configuration** | ❌ Manual per resource | ✅ Zero configuration |
| **Correctness** | ❌ Error-prone | ✅ Always correct |
| **Graph Changes** | ❌ Manual updates | ✅ Auto-updates |
| **Maintenance** | ❌ High burden | ✅ Zero burden |
| **Future Waves** | ❌ Manual wave tracking | ✅ Auto wave-aware |
| **Validation** | ❌ Hard to validate | ✅ Derivable from topology |
| **Conditionals** | ❌ Hard to handle | ✅ Conservative estimation |

---

## Implementation Checklist

### Week 1: ResourceLifetimeAnalyzer
- [ ] Create ResourceLifetimeAnalyzer class
- [ ] Implement ComputeTimelines() from execution order
- [ ] Implement FindLastConsumerIndex()
- [ ] Implement DetermineScope()
- [ ] Unit tests

### Week 2: Integration with GraphTopology
- [ ] Add lifetimeAnalyzer_ member to GraphTopology
- [ ] Implement UpdateResourceTimelines()
- [ ] Add topology dirty tracking
- [ ] Call UpdateResourceTimelines() on Compile()
- [ ] Integration tests

### Week 3: Automatic Aliasing Pools
- [ ] Implement ComputeAliasingGroups()
- [ ] Implement interval scheduling algorithm
- [ ] Add UpdateAliasingPoolsFromTopology() to UnifiedBudgetManager
- [ ] Automatic pool creation
- [ ] Aliasing report generation

### Week 4: Advanced Features
- [ ] Conditional execution handling
- [ ] Wave-based parallelism support (timeline.executionWave)
- [ ] Multiple graph variation testing
- [ ] Performance optimization (caching)

---

## Conclusion

**Key Innovation**: Resource aliasing lifetimes are **automatically derived** from GraphTopology, eliminating manual configuration and ensuring correctness.

**Benefits**:
- 🎯 **Zero configuration** - topology drives everything
- ✅ **Always correct** - synchronized with graph state
- 🔄 **Self-updating** - graph changes → automatic updates
- 🚀 **Future-proof** - wave-based parallelism ready
- 🛡️ **Validated** - derived from topology = correct by construction

**Next Steps**:
1. Implement ResourceLifetimeAnalyzer
2. Integrate with GraphTopology
3. Update UnifiedBudgetManager for automatic pools
4. Test with complex graph modifications

---

**End of Document**
