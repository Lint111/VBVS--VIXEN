# Partial Cleanup Strategy: Reference Counting Approach

## Problem Statement

When cleaning a node `A` that depends on `B` and `C`:
- Should we clean `B`? Only if **no other nodes** depend on it
- Should we clean `C`? Only if **no other nodes** depend on it

**Invalid to enforce tree constraint** because real render graphs form DAGs:
```
Device ──┬──> Buffer ──┬──> ShadowPass
         │             └──> MainPass
         └──> Texture ──┬──> MainPass  ← Diamond dependency
                        └──> UIPass
```

## Solution: Dependent Reference Counting

Track **how many nodes depend on each node** during graph compilation.

### Algorithm

**1. Build Dependent Count Map:**
```cpp
std::unordered_map<NodeInstance*, size_t> dependentCounts;

for (auto& node : instances) {
    for (auto* dependency : node->GetDependencies()) {
        dependentCounts[dependency]++;
    }
}
```

**2. Partial Cleanup Logic:**
```cpp
void RenderGraph::CleanupSubgraph(NodeInstance* root) {
    std::set<NodeInstance*> toClean;
    std::queue<NodeInstance*> queue;
    queue.push(root);
    
    while (!queue.empty()) {
        auto* node = queue.front();
        queue.pop();
        
        toClean.insert(node);
        
        // Check each dependency
        for (auto* dep : node->GetDependencies()) {
            // Decrement dependent count
            dependentCounts[dep]--;
            
            // If no other nodes depend on it, mark for cleanup
            if (dependentCounts[dep] == 0) {
                queue.push(dep);
            }
        }
    }
    
    // Execute cleanup for marked nodes
    for (auto* node : toClean) {
        ExecuteCleanupFor(node);
    }
}
```

### Example Walkthrough

**Graph:**
```
Device (refCount=3)
  ├──> Buffer (refCount=2)
  │      ├──> ShadowPass (refCount=0)
  │      └──> MainPass (refCount=0)
  └──> Texture (refCount=2)
         ├──> MainPass (refCount=0)  ← Already counted above
         └──> UIPass (refCount=0)
```

**Scenario 1: Clean MainPass**
```
CleanupSubgraph(MainPass):
1. Mark MainPass for cleanup
2. Check dependencies:
   - Buffer: refCount 2 → 1 (still used by ShadowPass) → DON'T clean
   - Texture: refCount 2 → 1 (still used by UIPass) → DON'T clean
3. Result: Clean ONLY MainPass
```

**Scenario 2: Clean MainPass + ShadowPass**
```
CleanupSubgraph({MainPass, ShadowPass}):
1. Mark MainPass, ShadowPass for cleanup
2. Check MainPass dependencies:
   - Buffer: refCount 2 → 1
   - Texture: refCount 2 → 1
3. Check ShadowPass dependencies:
   - Buffer: refCount 1 → 0 (no more users!) → Mark Buffer for cleanup
4. Check Buffer dependencies:
   - Device: refCount 3 → 2 (still used by Texture) → DON'T clean
5. Result: Clean MainPass, ShadowPass, Buffer (but NOT Texture or Device)
```

**Scenario 3: Full Cleanup (All Leaf Nodes)**
```
CleanupSubgraph({MainPass, ShadowPass, UIPass}):
→ All leaves cleaned
→ Buffer refCount → 0 → cleaned
→ Texture refCount → 0 → cleaned
→ Device refCount → 0 → cleaned
✅ Complete graph cleanup
```

## API Design

```cpp
class RenderGraph {
public:
    // Full cleanup (current behavior)
    void ExecuteCleanup();
    
    // Partial cleanup starting from one node
    void CleanupSubgraph(const std::string& rootNodeName);
    
    // Partial cleanup starting from multiple nodes
    void CleanupSubgraph(const std::vector<std::string>& rootNodeNames);
    
    // Preview what would be cleaned (dry-run)
    std::vector<std::string> GetCleanupScope(const std::string& rootNodeName) const;
    
private:
    // Compute dependent reference counts
    void ComputeDependentCounts();
    
    // Current counts (updated during partial cleanup)
    std::unordered_map<NodeInstance*, size_t> dependentCounts;
};
```

## Usage Examples

### Recompile SwapChain After Resize

```cpp
// Window resized → Need to recreate swapchain + framebuffers

// 1. Clean affected subgraph
graph.CleanupSubgraph({"SwapChainNode", "FramebufferNode"});

// 2. Recompile (resources recreated automatically)
graph.RecompileNodes({"SwapChainNode", "FramebufferNode"});

// Device, RenderPass, Pipeline untouched!
```

### Shader Hot-Reload

```cpp
// Shader file changed → Recreate pipeline

// 1. Preview what would be affected
auto affected = graph.GetCleanupScope("GraphicsPipelineNode");
// Returns: ["GraphicsPipelineNode"] (device, descriptors untouched)

// 2. Clean + recompile pipeline only
graph.CleanupSubgraph("GraphicsPipelineNode");
graph.RecompileNodes({"GraphicsPipelineNode"});
```

### Dynamic Light Addition

```cpp
// Add new shadow map dynamically

// 1. Add nodes
auto shadowNode = graph.AddNode("ShadowMapPass", "Shadow_Light5");
graph.ConnectNodes(deviceNode, 0, shadowNode, 0);

// 2. Compile only new node (existing graph untouched)
graph.CompileNode("Shadow_Light5");

// No cleanup needed!
```

### Dynamic Light Removal

```cpp
// Remove shadow map

// 1. Clean specific node + unused dependencies
graph.CleanupSubgraph("Shadow_Light5");

// 2. Remove from graph topology
graph.RemoveNode("Shadow_Light5");

// Shared resources (Device, CommandPool) preserved
```

## Edge Cases

### Circular Dependencies (Shouldn't Exist)

Cleanup algorithm assumes DAG. If cycles exist:
```cpp
// During graph compilation
if (topology.HasCycles()) {
    throw std::runtime_error("Circular dependencies detected");
}
```

### Shared Root Resources

```
Device ──┬──> PoolA ──> PassA
         └──> PoolB ──> PassB

CleanupSubgraph(PassA):
→ PassA cleaned
→ PoolA cleaned (refCount 1 → 0)
→ Device NOT cleaned (refCount 2 → 1, PoolB still uses it)
✅ Correct behavior
```

## Implementation Priority

**Phase 1: Full Cleanup (DONE)**
- `ExecuteCleanup()` destroys entire graph
- Reference counting not needed

**Phase 2: Partial Cleanup (NEXT)**
- Implement `ComputeDependentCounts()`
- Implement `CleanupSubgraph()`
- Add dry-run `GetCleanupScope()`

**Phase 3: Selective Recompilation**
- Track dirty nodes
- Recompile only affected subgraph
- Integrate with EventBus (WindowResize, ShaderReload events)

## Recommendation

**Don't restrict graph topology to trees.** Instead:
1. Allow arbitrary DAGs (required for real rendering)
2. Implement reference counting for safe partial cleanup
3. Use `GetCleanupScope()` to preview before destructive operations
4. Validate DAG properties (no cycles) during compilation

This gives you **flexibility + safety** without artificial constraints.
