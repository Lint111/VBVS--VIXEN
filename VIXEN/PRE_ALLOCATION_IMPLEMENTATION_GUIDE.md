# Pre-Allocation Implementation Guide
**Date:** January 3, 2026  
**Audience:** Implementing engineers for Sprints 5-10

---

## Table of Contents
1. **Phase 2.1: EventBus Queue Pre-Allocation**
2. **Phase 2.2: DescriptorSetCache Pre-Calculation**
3. **Phase 2.3: Timeline FrameHistory Pre-Reservation**
4. **Phase 2.4: Multi-GPU Transfer Queue Pre-Sizing**
5. **Phase 2.5: CashSystem StagingBufferPool Pre-Warming**
6. **Testing Strategy**
7. **Validation Checklist**

---

## Phase 2.1: EventBus Queue Pre-Allocation

### Current Code Analysis
**File:** `libraries/EventBus/include/EventBus.h`

```cpp
// Current (allocates dynamically)
class EventBus {
    std::vector<Event> eventQueue;
    
    void Emit(const Event& event) {
        eventQueue.push_back(event);  // Reallocates if capacity exceeded
    }
    
    void ProcessEvents() {
        for (const auto& event : eventQueue) {
            // Process event
        }
        eventQueue.clear();  // Keeps capacity
    }
};
```

### Implementation

```cpp
// MODIFIED: EventBus with pre-allocation
class EventBus {
private:
    std::vector<Event> eventQueue;
    size_t maxCapacity = 1024;  // Default: handle 1K events/frame
    
    enum class AllocationMode : uint8_t {
        Strict,      // Fail if queue full
        Fallback,    // Allocate more (with warning)
        Discard      // Drop oldest events
    };
    AllocationMode allocationMode = AllocationMode::Fallback;

public:
    /**
     * @brief Pre-allocate event queue capacity
     * @param capacity Expected maximum events per frame
     * @param mode Behavior if capacity exceeded
     */
    void PreAllocate(size_t capacity, AllocationMode mode = AllocationMode::Fallback) {
        eventQueue.reserve(capacity);
        maxCapacity = capacity;
        allocationMode = mode;
        LOG_INFO("EventBus pre-allocated for {} events", capacity);
    }
    
    /**
     * @brief Emit event with capacity checking
     */
    bool Emit(const Event& event) {
        if (eventQueue.size() >= maxCapacity) {
            switch (allocationMode) {
                case AllocationMode::Strict:
                    LOG_ERROR("EventBus queue full: capacity={}, events={}", maxCapacity, eventQueue.size());
                    return false;  // Drop event
                    
                case AllocationMode::Fallback:
                    LOG_WARN("EventBus capacity exceeded, reallocating ({}→{})", maxCapacity, maxCapacity * 2);
                    maxCapacity *= 2;
                    eventQueue.reserve(maxCapacity);
                    // Fall through to emit
                    
                case AllocationMode::Discard:
                    if (!eventQueue.empty()) {
                        eventQueue.erase(eventQueue.begin());  // Drop oldest
                    }
                    break;
            }
        }
        
        eventQueue.push_back(event);
        return true;
    }
    
    /**
     * @brief Get current queue statistics
     */
    struct QueueStats {
        size_t eventCount = 0;
        size_t capacity = 0;
        float utilization = 0.0f;
    };
    
    QueueStats GetStats() const {
        return {
            .eventCount = eventQueue.size(),
            .capacity = maxCapacity,
            .utilization = static_cast<float>(eventQueue.size()) / maxCapacity
        };
    }
};
```

### Integration: RenderGraph Setup Phase

**File:** `libraries/RenderGraph/src/RenderGraph.cpp`

```cpp
void RenderGraph::Setup() {
    // ... existing setup code ...
    
    // Calculate expected event rate
    // Heuristic: each node can emit ~3-5 events per frame
    //   - Setup phase changes
    //   - Compilation invalidation
    //   - Resource transitions
    size_t expectedEvents = std::max(nodes.size() * 3, size_t(64));
    
    // Add some headroom for cascading events (resize can trigger 10+ invalidations)
    expectedEvents = expectedEvents * 2;
    
    eventBus->PreAllocate(expectedEvents, EventBus::AllocationMode::Fallback);
    
    LOG_INFO("EventBus configured: capacity={} (heuristic: {} nodes)", expectedEvents, nodes.size());
}
```

### Testing

```cpp
// File: libraries/EventBus/tests/EventBusPreAllocationTest.cpp

#include <gtest/gtest.h>
#include "EventBus/EventBus.h"

class EventBusPreAllocationTest : public ::testing::Test {
protected:
    EventBus eventBus;
};

TEST_F(EventBusPreAllocationTest, PreAllocateReservesCapacity) {
    eventBus.PreAllocate(100);
    auto stats = eventBus.GetStats();
    
    EXPECT_EQ(stats.capacity, 100);
    EXPECT_EQ(stats.eventCount, 0);
}

TEST_F(EventBusPreAllocationTest, EmitWithinCapacityNoReallocate) {
    eventBus.PreAllocate(50);
    
    for (int i = 0; i < 50; i++) {
        Event evt{.type = EventType::NodeDirty};
        EXPECT_TRUE(eventBus.Emit(evt));
    }
    
    auto stats = eventBus.GetStats();
    EXPECT_EQ(stats.capacity, 50);  // No reallocation
}

TEST_F(EventBusPreAllocationTest, StrictModeRejectsOverflow) {
    eventBus.PreAllocate(10, EventBus::AllocationMode::Strict);
    
    for (int i = 0; i < 10; i++) {
        Event evt{.type = EventType::NodeDirty};
        EXPECT_TRUE(eventBus.Emit(evt));
    }
    
    Event overflow{.type = EventType::NodeDirty};
    EXPECT_FALSE(eventBus.Emit(overflow));  // Rejected
}

TEST_F(EventBusPreAllocationTest, DiscardModeDropsOldest) {
    eventBus.PreAllocate(5, EventBus::AllocationMode::Discard);
    
    Event evt1{.type = EventType::NodeDirty, .data = 1};
    Event evt2{.type = EventType::NodeDirty, .data = 2};
    
    for (int i = 0; i < 6; i++) {
        Event evt{.type = EventType::NodeDirty, .data = i};
        eventBus.Emit(evt);
    }
    
    auto stats = eventBus.GetStats();
    EXPECT_EQ(stats.capacity, 5);
    EXPECT_EQ(stats.eventCount, 5);
    // Oldest (data=0) should be gone
}

TEST_F(EventBusPreAllocationTest, AllocationTrackingNoRuntimeAllocationsWithinCapacity) {
    // This test uses instrumentation from Phase 1 (Audit)
    AllocationTracker tracker;
    
    eventBus.PreAllocate(1000);
    tracker.StartTracking();
    
    for (int i = 0; i < 1000; i++) {
        Event evt{.type = EventType::NodeDirty};
        eventBus.Emit(evt);
    }
    
    auto allocations = tracker.GetAllocations();
    EXPECT_EQ(allocations.count, 0) << "EventBus allocated during frame";
}
```

---

## Phase 2.2: DescriptorSetCache Pre-Calculation

### Architecture Decision

Current descriptor allocation happens on-demand during Execute:
```cpp
// BEFORE: Dynamic allocation
node->Execute() {
    auto descriptors = GetOrCreateDescriptors(...);  // May allocate
    vkCmdBindDescriptorSets(..., descriptors);
}
```

New approach: Calculate all descriptor needs during Compile, allocate pool once:
```cpp
// AFTER: Pre-allocation during Compile
node->Compile() {
    auto needs = node->GetDescriptorRequirements();
    descriptorPool.PreAllocate(needs);  // Reserve all upfront
    node->Setup();  // Now safe to allocate specific sets
}

node->Execute() {
    vkCmdBindDescriptorSets(..., descriptors);  // No allocation
}
```

### Implementation Plan

**Step 1: Add descriptor requirement interface**

```cpp
// File: libraries/RenderGraph/include/Core/IDescriptorRequirements.h

namespace RenderGraph {

/**
 * @brief Descriptor set layout requirements
 */
struct DescriptorSetRequirement {
    uint32_t setIndex = 0;
    VkDescriptorSetLayoutBinding* bindings = nullptr;
    uint32_t bindingCount = 0;
    const char* debugName = "Unknown";
};

/**
 * @brief Interface for nodes to declare descriptor needs
 */
class IDescriptorRequirements {
public:
    virtual ~IDescriptorRequirements() = default;
    
    /**
     * @brief Get descriptor set requirements for this node
     * @return Array of descriptor set requirements, valid until next Compile()
     */
    virtual std::vector<DescriptorSetRequirement> GetDescriptorRequirements() const {
        return {};  // Default: no descriptors needed
    }
    
    /**
     * @brief Get estimated number of descriptor sets needed for full lifetime
     * Some nodes may need multiple sets for different configurations
     */
    virtual uint32_t GetEstimatedDescriptorSetCount() const {
        return 0;
    }
};

} // namespace RenderGraph
```

**Step 2: Modify TypedNode base class**

```cpp
// File: libraries/RenderGraph/include/Core/TypedNodeInstance.h

template<typename ConfigType>
class TypedNode : public NodeInstance, public IDescriptorRequirements {
protected:
    struct DescriptorAllocation {
        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> sets;
    };
    std::vector<DescriptorAllocation> descriptors;
    
public:
    // Default implementation (can be overridden)
    std::vector<DescriptorSetRequirement> GetDescriptorRequirements() const override {
        return {};
    }
    
    /**
     * @brief Pre-allocate descriptor sets for this node
     * Called during Compile phase, before CompileImpl()
     */
    void PreAllocateDescriptors(const DescriptorCache& cache) {
        auto reqs = GetDescriptorRequirements();
        descriptors.resize(reqs.size());
        
        for (size_t i = 0; i < reqs.size(); i++) {
            auto& req = reqs[i];
            auto layout = cache.GetOrCreateLayout(req.bindings, req.bindingCount, req.debugName);
            descriptors[i].layout = layout;
            
            // Pre-allocate expected count
            auto count = GetEstimatedDescriptorSetCount();
            descriptors[i].sets.reserve(count);
        }
    }
};
```

**Step 3: Update RenderGraph compilation**

```cpp
// File: libraries/RenderGraph/src/RenderGraph.cpp

void RenderGraph::CompileNode(NodeHandle handle) {
    auto node = nodes[handle].get();
    
    // Phase 1: Pre-allocate descriptors
    if (auto descriptorNode = dynamic_cast<IDescriptorRequirements*>(node)) {
        descriptorCache.PreAllocate(descriptorNode->GetDescriptorRequirements());
    }
    
    // Phase 2: Call node's compile
    node->Compile();
    
    // Phase 3: Register cleanup
    RegisterCleanupForNode(handle);
}
```

**Step 4: Example implementation**

```cpp
// File: libraries/RenderGraph/include/Nodes/GeometryRenderNode.h

class GeometryRenderNode : public TypedNode<GeometryRenderConfig> {
protected:
    struct GeometryDescriptors {
        VkDescriptorSet scene = VK_NULL_HANDLE;   // Scene data
        VkDescriptorSet material = VK_NULL_HANDLE; // Material parameters
    };
    GeometryDescriptors descriptors;
    
public:
    std::vector<DescriptorSetRequirement> GetDescriptorRequirements() const override {
        return {
            {
                .setIndex = 0,
                .bindings = sceneBindings,
                .bindingCount = 2,  // Camera + Scene
                .debugName = "GeometryScene"
            },
            {
                .setIndex = 1,
                .bindings = materialBindings,
                .bindingCount = 4,  // Albedo + Normal + Roughness + Metallic
                .debugName = "GeometryMaterial"
            }
        };
    }
    
    uint32_t GetEstimatedDescriptorSetCount() const override {
        return 4;  // Scene + 3 material variants (opaque, translucent, particles)
    }
    
    void CompileImpl(CompileContext& ctx) override {
        // Descriptors already pre-allocated
        // Just bind them
        auto sceneData = ctx.In(SCENE_DATA);
        descriptors.scene = descriptorCache.GetSet(0);
        UpdateDescriptorSet(descriptors.scene, sceneData);
    }
};
```

### Testing

```cpp
// File: libraries/RenderGraph/tests/DescriptorPreAllocationTest.cpp

TEST_F(DescriptorPreAllocationTest, NodeDeclareRequirements) {
    GeometryRenderNode node;
    auto reqs = node.GetDescriptorRequirements();
    
    EXPECT_EQ(reqs.size(), 2);
    EXPECT_STREQ(reqs[0].debugName, "GeometryScene");
    EXPECT_STREQ(reqs[1].debugName, "GeometryMaterial");
}

TEST_F(DescriptorPreAllocationTest, PreAllocateReservesCapacity) {
    DescriptorCache cache(device);
    GeometryRenderNode node;
    
    node.PreAllocateDescriptors(cache);
    
    // Verify sets reserved
    // (internal verification via allocation tracker)
}

TEST_F(DescriptorPreAllocationTest, NoAllocationDuringExecute) {
    AllocationTracker tracker;
    auto graph = CreateTestGraph();
    
    graph->Compile();  // Pre-allocate everything
    tracker.StartTracking();
    
    graph->Execute();  // Should not allocate
    
    auto allocations = tracker.GetAllocations();
    EXPECT_EQ(allocations.count, 0);
}
```

---

## Phase 2.3: Timeline FrameHistory Pre-Reservation

### Planned Timeline System Architecture

```cpp
// File: libraries/RenderGraph/include/Nodes/TimelineNode.h

class TimelineNode : public TypedNode<TimelineConfig> {
private:
    // Ring buffer for frame history
    struct FrameHistorySlot {
        std::unordered_map<ResourceID, Resource*> images;
        std::unordered_map<ResourceID, Resource*> buffers;
        uint32_t frameIndex = 0;
    };
    std::vector<FrameHistorySlot> frameHistory;
    uint32_t frameHistoryHead = 0;
    uint32_t maxHistoryFrames = 4;  // TAA, motion blur typically need 4 frames
    
    // Sub-graphs for each timeline configuration
    std::unordered_map<uint32_t, RenderGraph> subgraphs;
    std::unordered_map<uint32_t, SubGraphState> subgraphStates;
    
public:
    void Setup() override {
        // Pre-allocate frame history ring buffer
        frameHistory.resize(maxHistoryFrames);
        
        for (auto& slot : frameHistory) {
            slot.images.reserve(resourcesPerFrame);
            slot.buffers.reserve(resourcesPerFrame);
        }
        
        // Subscribe to frame events
        eventBus->Subscribe(EventType::FrameStart, this);
        eventBus->Subscribe(EventType::FrameEnd, this);
        
        LOG_INFO("TimelineNode pre-allocated {} frames", maxHistoryFrames);
    }
    
    void OnEvent(const Event& evt) override {
        if (evt.type == EventType::FrameEnd) {
            // Rotate history
            frameHistoryHead = (frameHistoryHead + 1) % maxHistoryFrames;
        }
    }
    
    /**
     * @brief Get resource from previous frame (no allocation)
     */
    Resource* GetPreviousFrameResource(uint32_t framesAgo, ResourceID id) {
        if (framesAgo >= maxHistoryFrames) return nullptr;
        
        uint32_t slotIdx = (frameHistoryHead - framesAgo + maxHistoryFrames) % maxHistoryFrames;
        auto& slot = frameHistory[slotIdx];
        
        auto it = slot.images.find(id);
        if (it != slot.images.end()) {
            return it->second;  // No allocation
        }
        
        it = slot.buffers.find(id);
        if (it != slot.buffers.end()) {
            return it->second;  // No allocation
        }
        
        return nullptr;
    }
    
    /**
     * @brief Store current frame resource (no allocation if slot pre-sized)
     */
    void StoreCurrentFrameResource(ResourceID id, Resource* res, bool isImage) {
        auto& currentSlot = frameHistory[frameHistoryHead];
        
        if (isImage) {
            currentSlot.images[id] = res;  // Uses pre-allocated capacity
        } else {
            currentSlot.buffers[id] = res;  // Uses pre-allocated capacity
        }
    }
};
```

### Integration with Temporal Effects (TAA Example)

```cpp
// File: libraries/RenderGraph/include/Nodes/TAATimelineNode.h

class TAATimelineNode : public TimelineNode {
private:
    struct TAAHistory {
        Resource* colorT0 = nullptr;  // Current frame
        Resource* colorT1 = nullptr;  // Previous frame
        Resource* motionVectors = nullptr;
    };
    std::vector<TAAHistory> taaHistory;
    
public:
    void Setup() override {
        TimelineNode::Setup();
        
        // Pre-allocate TAA history
        taaHistory.resize(2);  // Ping-pong only
        
        LOG_INFO("TAATimelineNode pre-allocated 2-frame history");
    }
    
    void CompileImpl(CompileContext& ctx) override {
        auto inputColor = ctx.In(INPUT_COLOR);
        auto motionVectors = ctx.In(MOTION_VECTORS);
        
        // Get previous frame
        auto prevColor = GetPreviousFrameResource(1, COLOR_RESOURCE);
        if (!prevColor) prevColor = inputColor;  // Fallback first frame
        
        // Perform TAA blend (no allocations)
        auto outputColor = BlendTAA(inputColor, prevColor, motionVectors);
        
        // Store for next frame (uses pre-allocated slot)
        StoreCurrentFrameResource(COLOR_RESOURCE, outputColor, true);
        
        ctx.Out(OUTPUT_COLOR, outputColor);
    }
};
```

### Sizing Heuristic

```cpp
// File: libraries/RenderGraph/src/RenderGraphSetup.cpp

void RenderGraph::CalculateTimelineHistorySize() {
    // Survey all temporal effects
    uint32_t maxTemporalFrames = 0;
    
    for (auto& node : nodes) {
        if (auto timelineNode = dynamic_cast<TimelineNode*>(node.get())) {
            maxTemporalFrames = std::max(maxTemporalFrames, timelineNode->GetMaxHistoryFrames());
        }
    }
    
    // Typical values:
    // - TAA: 4 frames
    // - Motion blur: 8 frames
    // - Temporal filtering: 2 frames
    // Default safe value
    maxTemporalFrames = std::max(maxTemporalFrames, 4u);
    
    LOG_INFO("Timeline max history: {} frames", maxTemporalFrames);
}
```

### Testing

```cpp
// File: libraries/RenderGraph/tests/TimelinePreAllocationTest.cpp

TEST_F(TimelinePreAllocationTest, FrameHistoryPreAllocated) {
    TAATimelineNode taa;
    taa.Setup();
    
    // Verify history ring buffer allocated
    // (Requires instrumentation to peek into private members via friend test class)
}

TEST_F(TimelinePreAllocationTest, GetPreviousFrameNoAllocation) {
    AllocationTracker tracker;
    TAATimelineNode taa;
    taa.Setup();
    
    tracker.StartTracking();
    
    for (int frame = 0; frame < 10; frame++) {
        auto prev = taa.GetPreviousFrameResource(1, COLOR_RESOURCE);
        // No allocation
    }
    
    EXPECT_EQ(tracker.GetAllocations().count, 0);
}

TEST_F(TimelinePreAllocationTest, StoreCurrentFrameNoAllocation) {
    AllocationTracker tracker;
    TAATimelineNode taa;
    taa.Setup();
    
    auto resource = CreateTestResource();
    tracker.StartTracking();
    
    for (int frame = 0; frame < 10; frame++) {
        taa.StoreCurrentFrameResource(COLOR_RESOURCE, resource, true);
    }
    
    EXPECT_EQ(tracker.GetAllocations().count, 0);
}
```

---

## Phase 2.4: Multi-GPU Transfer Queue Pre-Sizing

### Architecture

```cpp
// File: libraries/RenderGraph/include/MultiGPU/MultiGPUScheduler.h

class MultiGPUScheduler {
private:
    struct TransferJob {
        GPUHandle srcGPU;
        GPUHandle dstGPU;
        Resource* resource;
        uint64_t offset;
        uint64_t size;
        VkPipelineStageFlags waitStage;
        VkSemaphore timeline;
        uint64_t timelineValue;
    };
    
    std::vector<TransferJob> transferQueue;
    uint32_t transferQueueSize = 0;  // Tracks pending transfers
    uint32_t maxQueueSize = 256;     // Max concurrent transfers
    
public:
    void PreAllocate(uint32_t expectedTransfersPerFrame) {
        maxQueueSize = expectedTransfersPerFrame * 2;  // 2x headroom
        transferQueue.reserve(maxQueueSize);
        LOG_INFO("MultiGPUScheduler transfer queue capacity: {}", maxQueueSize);
    }
    
    bool ScheduleTransfer(const TransferJob& job) {
        if (transferQueueSize >= maxQueueSize) {
            LOG_ERROR("Transfer queue full: {} >= {}", transferQueueSize, maxQueueSize);
            return false;
        }
        
        if (transferQueueSize >= transferQueue.size()) {
            // Should not happen if pre-allocated correctly
            transferQueue.push_back(job);
        } else {
            transferQueue[transferQueueSize] = job;
        }
        transferQueueSize++;
        return true;
    }
    
    void ExecuteTransfers() {
        for (uint32_t i = 0; i < transferQueueSize; i++) {
            ExecuteTransfer(transferQueue[i]);
        }
        transferQueueSize = 0;  // Reset for next frame
    }
};
```

### Integration

```cpp
// File: libraries/RenderGraph/src/RenderGraph.cpp

void RenderGraph::Setup() {
    // ... existing code ...
    
    if (multiGPUEnabled) {
        uint32_t gpuCount = multiGPUScheduler->GetGPUCount();
        uint32_t nodesPerGPU = (nodes.size() + gpuCount - 1) / gpuCount;
        
        // Estimate transfers: worst case, every node output migrates between GPUs
        uint32_t expectedTransfersPerFrame = nodesPerGPU * gpuCount * 2;
        multiGPUScheduler->PreAllocate(expectedTransfersPerFrame);
    }
}
```

---

## Phase 2.5: CashSystem StagingBufferPool Pre-Warming

### Implementation

```cpp
// File: libraries/CashSystem/include/StagingBufferPool.h

class StagingBufferPool {
private:
    struct PoolBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        VkDeviceSize size = 0;
        bool inUse = false;
    };
    
    std::vector<PoolBuffer> availableBuffers;
    std::vector<PoolBuffer> inFlightBuffers;
    VkDeviceSize defaultBufferSize = 256 * 1024 * 1024;  // 256 MB
    
public:
    void PreWarm(uint32_t bufferCount, VkDeviceSize bufferSize) {
        availableBuffers.reserve(bufferCount);
        defaultBufferSize = bufferSize;
        
        for (uint32_t i = 0; i < bufferCount; i++) {
            auto buf = AllocateBuffer(bufferSize);
            availableBuffers.push_back({
                .buffer = buf.buffer,
                .memory = buf.memory,
                .mapped = buf.mapped,
                .size = bufferSize
            });
        }
        
        LOG_INFO("StagingBufferPool pre-warmed: {} buffers × {} MB", 
                 bufferCount, bufferSize / (1024 * 1024));
    }
    
    VkBuffer GetBuffer(VkDeviceSize size) {
        // Find suitable buffer
        for (auto& buf : availableBuffers) {
            if (buf.size >= size && !buf.inUse) {
                buf.inUse = true;
                inFlightBuffers.push_back(buf);
                availableBuffers.erase(
                    std::find(availableBuffers.begin(), availableBuffers.end(), buf)
                );
                return buf.buffer;
            }
        }
        
        // Fallback: allocate new (warns)
        LOG_WARN("StagingBufferPool exhausted, allocating new buffer ({})", size);
        auto newBuf = AllocateBuffer(size);
        inFlightBuffers.push_back({
            .buffer = newBuf.buffer,
            .memory = newBuf.memory,
            .mapped = newBuf.mapped,
            .size = size
        });
        return newBuf.buffer;
    }
    
    void ReturnBuffer(VkBuffer buffer) {
        auto it = std::find_if(inFlightBuffers.begin(), inFlightBuffers.end(),
                              [buffer](const PoolBuffer& b) { return b.buffer == buffer; });
        if (it != inFlightBuffers.end()) {
            availableBuffers.push_back(*it);
            inFlightBuffers.erase(it);
        }
    }
};
```

### Integration in CashSystem

```cpp
// File: libraries/CashSystem/src/TypedCacher.cpp

void TypedCacher::Setup() {
    // ... existing setup ...
    
    // Pre-calculate upload volume
    uint32_t expectedCachers = 5;  // VoxelScene, VoxelAABB, Acceleration, Mesh, etc.
    uint32_t maxUploadsPerCacher = 3;
    uint32_t bufferPerUpload = 256 * 1024 * 1024;
    
    stagingPool.PreWarm(
        expectedCachers * maxUploadsPerCacher,  // 15 buffers
        bufferPerUpload
    );
}
```

---

## Testing Strategy

### Unified Allocation Tracker

```cpp
// File: libraries/ResourceManagement/include/AllocationTracker.h

class AllocationTracker {
private:
    struct AllocationRecord {
        void* ptr;
        size_t size;
        std::string stackTrace;
        std::chrono::system_clock::time_point timestamp;
        std::string component;
    };
    
    std::vector<AllocationRecord> allocations;
    bool tracking = false;
    
public:
    void StartTracking() {
        tracking = true;
        allocations.clear();
    }
    
    void StopTracking() {
        tracking = false;
    }
    
    void Record(void* ptr, size_t size, std::string_view component) {
        if (!tracking) return;
        
        allocations.push_back({
            .ptr = ptr,
            .size = size,
            .stackTrace = CaptureStackTrace(),
            .timestamp = std::chrono::system_clock::now(),
            .component = std::string(component)
        });
    }
    
    struct Stats {
        size_t count = 0;
        size_t totalBytes = 0;
        std::unordered_map<std::string, size_t> byComponent;
        std::string largestAllocation;
        size_t largestSize = 0;
    };
    
    Stats GetStats() const {
        Stats stats;
        for (const auto& alloc : allocations) {
            stats.count++;
            stats.totalBytes += alloc.size;
            stats.byComponent[alloc.component] += alloc.size;
            
            if (alloc.size > stats.largestSize) {
                stats.largestSize = alloc.size;
                stats.largestAllocation = alloc.component;
            }
        }
        return stats;
    }
};
```

### Test Harness

```cpp
// File: libraries/RenderGraph/tests/NoRuntimeAllocationTest.cpp

class NoRuntimeAllocationTest : public ::testing::Test {
protected:
    AllocationTracker tracker;
    std::unique_ptr<RenderGraph> graph;
    
    void SetUp() override {
        graph = CreateTestGraph();
        graph->Setup();
        tracker.StartTracking();
    }
    
    void TearDown() override {
        tracker.StopTracking();
        auto stats = tracker.GetStats();
        EXPECT_EQ(stats.count, 0) << "Runtime allocations detected:\n"
            << "Total: " << stats.totalBytes << " bytes\n"
            << "Largest: " << stats.largestAllocation << " (" << stats.largestSize << " bytes)";
    }
};

TEST_F(NoRuntimeAllocationTest, CompilePhasePreAllocates) {
    // Compile should use pre-allocated resources
    graph->Compile();
    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.count, 0) << "Allocations during Compile phase";
}

TEST_F(NoRuntimeAllocationTest, ExecutePhaseNoAllocations) {
    graph->Compile();
    tracker.StartTracking();  // Reset tracker
    
    graph->Execute();
    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.count, 0) << "Allocations during Execute phase";
}

TEST_F(NoRuntimeAllocationTest, ProcessEventsNoAllocations) {
    graph->Compile();
    
    // Emit events
    Event evt{.type = EventType::NodeDirty};
    graph->eventBus->Emit(evt);
    
    tracker.StartTracking();
    graph->ProcessEvents();
    
    auto stats = tracker.GetStats();
    EXPECT_EQ(stats.count, 0) << "Allocations during ProcessEvents phase";
}
```

---

## Validation Checklist

### Before Each Sprint Merge

- [ ] EventBus pre-allocation test passes (`EventBusPreAllocationTest.cpp`)
- [ ] DescriptorCache pre-calculation test passes (`DescriptorPreAllocationTest.cpp`)
- [ ] Timeline frame history test passes (`TimelinePreAllocationTest.cpp`)
- [ ] Multi-GPU transfer queue test passes (`MultiGPUSchedulerTest.cpp`)
- [ ] StagingBufferPool pre-warm test passes (`StagingBufferPoolTest.cpp`)
- [ ] No runtime allocations during Execute (`NoRuntimeAllocationTest.cpp`)
- [ ] AllocationTracker integration enabled in Debug builds
- [ ] Memory budget dashboard shows zero allocations post-Setup
- [ ] PR description includes pre-allocation justification

### Performance Validation

- [ ] Compile phase completes within budget (4x slower than Execute is acceptable)
- [ ] Execute phase frame time stable (no stalls from allocation/deallocation)
- [ ] No Vulkan validation layer warnings for OOM/descriptor exhaustion
- [ ] Memory fragmentation trending downward (if applicable)

### Documentation

- [ ] Architecture decision document updated
- [ ] Pre-allocation strategy documented in code comments
- [ ] Estimated allocation sizes validated against actual usage
- [ ] Fallback behavior documented (what if pre-allocation insufficient)

---

## Success Metrics

| Metric | Target | Verification |
|--------|--------|--------------|
| EventBus queue pre-allocated | 100% | Test in Phase 2.1 |
| DescriptorSetCache pre-sized | 100% | Test in Phase 2.2 |
| Timeline frame history pre-reserved | 100% | Test in Phase 2.3 |
| Multi-GPU transfer queue pre-sized | 100% | Test in Phase 2.4 |
| StagingBufferPool pre-warmed | 100% | Test in Phase 2.5 |
| Zero runtime allocations Execute phase | ≥ 99% | NoRuntimeAllocationTest |
| Memory budget dashboard active | ✅ | Dashboard shows allocation trends |
| No frame hitches from memory | ≥ 99% | Profiler: zero spikes during Execute |

