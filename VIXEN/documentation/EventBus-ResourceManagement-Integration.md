# EventBus & ResourceManagement Integration Guide

## Overview

Two new static libraries for clean separation of concerns:

1. **EventBus** - Generic message passing system with worker thread integration
2. **ResourceManagement** - RM<T> wrapper for resource state tracking

## EventBus Library

### Core Concept

Generic `{sender, message}` system with type-safe inheritance-based messages.

### Basic Usage

```cpp
#include <EventBus/MessageBus.h>
#include <EventBus/Message.h>

using namespace EventBus;

// Define custom message type
struct ShaderCompilationMessage : public Message {
    static constexpr MessageType TYPE = 100;  // User-defined type IDs start at 100
    
    std::string programName;
    std::vector<uint32_t> spirv;
    bool success;
    std::string errors;
    
    ShaderCompilationMessage(SenderID sender)
        : Message(sender, TYPE) {}
};

// Create message bus
MessageBus bus;

// Subscribe to specific message type
SubscriptionID subID = bus.Subscribe(ShaderCompilationMessage::TYPE, 
    [](const Message& msg) {
        auto& compMsg = static_cast<const ShaderCompilationMessage&>(msg);
        
        if (compMsg.success) {
            std::cout << "Shader compiled: " << compMsg.programName << "\n";
            UpdateShaderLibrary(compMsg.spirv);
        } else {
            std::cerr << "Compilation failed: " << compMsg.errors << "\n";
        }
        
        return true; // Handled
    });

// Publish message (queued for async processing)
auto msg = std::make_unique<ShaderCompilationMessage>(senderID);
msg->programName = "basic_shader";
msg->spirv = compiledData;
msg->success = true;
bus.Publish(std::move(msg));

// Process messages (once per frame in main loop)
bus.ProcessMessages();

// Unsubscribe when done
bus.Unsubscribe(subID);
```

### Worker Thread Integration

```cpp
#include <EventBus/WorkerThreadBridge.h>

// Define result message
struct CompilationResult : public Message {
    static constexpr MessageType TYPE = 101;
    
    std::vector<uint32_t> spirv;
    std::string errors;
    bool success;
    
    CompilationResult(std::vector<uint32_t> data)
        : Message(0, TYPE)
        , spirv(std::move(data))
        , success(true) {}
};

// Create bridge (starts worker thread)
WorkerThreadBridge<CompilationResult> bridge(&bus);

// Subscribe to results on main thread
bus.Subscribe(CompilationResult::TYPE, [](const Message& msg) {
    auto& result = static_cast<const CompilationResult&>(msg);
    
    if (result.success) {
        std::cout << "Async compilation complete!\n";
        // Update shader library on main thread (safe)
        UpdateShaderLibrary(result.spirv);
    }
    
    return true;
});

// Submit work to worker thread (non-blocking)
bridge.SubmitWork(senderID, []() {
    // This lambda executes on worker thread
    std::vector<uint32_t> spirv = CompileShaderOnWorkerThread(sourceCode);
    return CompilationResult(std::move(spirv));
});

// Main loop continues without stutter
while (running) {
    bus.ProcessMessages();  // Results arrive here from worker thread
    RenderFrame();
}

// Bridge destructor waits for worker thread to finish
```

### Message Flow

```
┌─────────────────────────────────────────────────────────────┐
│                    EventBus Message Flow                     │
└─────────────────────────────────────────────────────────────┘

PATTERN 1: Main Thread Messaging
═════════════════════════════════

Sender Thread              MessageBus              Subscriber Thread
─────────────              ──────────              ─────────────────
Publish(msg) ─────────→ [Queue]
                            ↓
                       ProcessMessages()
                            ↓
                       DispatchMessage(msg) ────→ handler(msg)
                                                      ↓
                                                  Handle message


PATTERN 2: Worker Thread Integration
════════════════════════════════════

Main Thread                Worker Thread           Main Thread
───────────                ─────────────           ───────────

SubmitWork(lambda)
     ↓
[Work Queue] ─────────→ Execute lambda()
                            ↓
                       result = lambda()
                            ↓
                       MessageBus::Publish(result)
                            ↓
                       [Message Queue]
                            ↓
ProcessMessages()  ←────────────────────────────────┘
     ↓
Subscriber receives result (safe on main thread)
```

## ResourceManagement Library

### Core Concept

`RM<T>` wrapper providing:
- Optional-like interface: `if (resource.Ready()) { use(resource.Value()); }`
- State tracking: `resource.Has(ResourceState::Outdated)`
- Generation tracking: Cache invalidation via `resource.GetGeneration()`
- Metadata storage: `resource.SetMetadata("key", value)`

### Basic Usage

```cpp
#include <ResourceManagement/RM.h>
#include <ResourceManagement/ResourceState.h>

using namespace ResourceManagement;

// Wrap Vulkan resource
RM<VkPipeline> pipeline;

// Create resource
VkPipeline vkPipeline = CreateVulkanPipeline();
pipeline.Set(vkPipeline);

// Optional-like access
if (pipeline.Ready()) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.Value());
}

// Alternative: Bool conversion
if (pipeline) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline);
}

// Value or default
VkPipeline pipelineHandle = pipeline.ValueOr(VK_NULL_HANDLE);
```

### State Management

```cpp
// Mark resource outdated (needs recompilation)
pipeline.MarkOutdated();

// Check state before cleanup
if (pipeline.Has(ResourceState::Outdated)) {
    vkDestroyPipeline(device, pipeline.Value(), nullptr);
    pipeline.Reset();
    RecreateFromNewShader();
}

// Lock resource (GPU in-flight protection)
pipeline.Lock();

if (pipeline.IsLocked()) {
    std::cout << "Cannot modify - GPU in use\n";
    return;
}

// Unlock after fence wait
WaitForFence(frameInFlightFence);
pipeline.Unlock();

// Combine state checks
if (pipeline.Has(ResourceState::Outdated) && !pipeline.IsLocked()) {
    SafeToRecreate();
}
```

### Generation Tracking

```cpp
// Detect stale caches
RM<VkPipeline> pipeline;
uint64_t cachedGeneration = 0;

// Initial creation
pipeline.Set(CreatePipeline());
cachedGeneration = pipeline.GetGeneration();  // Generation = 1

// Later... shader hot reload
UpdateShader();
pipeline.MarkOutdated();
pipeline.IncrementGeneration();  // Generation = 2

// Cache check
if (pipeline.GetGeneration() != cachedGeneration) {
    std::cout << "Cache stale - pipeline changed\n";
    vkDestroyPipeline(device, oldCachedPipeline, nullptr);
    cachedGeneration = pipeline.GetGeneration();
}
```

### Metadata Storage

```cpp
RM<VkImage> texture;

// Store metadata
texture.SetMetadata("file_path", std::string("/textures/diffuse.png"));
texture.SetMetadata("mip_levels", uint32_t(8));
texture.SetMetadata("format", VK_FORMAT_R8G8B8A8_SRGB);
texture.SetMetadata("load_time", std::chrono::steady_clock::now());

// Retrieve metadata
std::string path = texture.GetMetadata<std::string>("file_path");
uint32_t mips = texture.GetMetadata<uint32_t>("mip_levels");

// Safe retrieval with default
bool isCompressed = texture.GetMetadataOr<bool>("compressed", false);

// Check existence
if (texture.HasMetadata("file_path")) {
    std::string path = texture.GetMetadata<std::string>("file_path");
    ReloadFromDisk(path);
}
```

## Integration: EventBus + ResourceManagement

### Pattern 1: Hot Reload with State Management

```cpp
// ShaderNode emits compilation complete event
struct ShaderCompileCompleteMsg : public Message {
    static constexpr MessageType TYPE = 200;
    std::string programName;
    std::vector<uint32_t> spirv;
    bool success;
};

// PipelineNode manages pipeline resource
class PipelineNode {
    RM<VkPipeline> pipeline;
    uint64_t shaderGeneration = 0;
    
    void Setup(MessageBus* bus) {
        // Subscribe to shader compilation events
        bus->Subscribe(ShaderCompileCompleteMsg::TYPE, [this](const Message& msg) {
            auto& compMsg = static_cast<const ShaderCompileCompleteMsg&>(msg);
            
            if (compMsg.success) {
                // Mark pipeline outdated
                pipeline.MarkOutdated();
                pipeline.IncrementGeneration();
                
                std::cout << "Pipeline marked outdated by shader reload\n";
            }
            
            return true;
        });
    }
    
    void Compile() {
        // Check if recompilation needed
        if (!pipeline.Has(ResourceState::Outdated)) {
            std::cout << "Pipeline clean, skipping recompilation\n";
            return;
        }
        
        // Check if locked (GPU in-use)
        if (pipeline.IsLocked()) {
            std::cout << "Pipeline locked, deferring recompilation\n";
            return;
        }
        
        // Safe to recreate
        if (pipeline.Ready()) {
            vkDestroyPipeline(device, pipeline.Value(), nullptr);
        }
        
        VkPipeline newPipeline = CreatePipelineFromShader();
        pipeline.Set(newPipeline);
        pipeline.MarkReady();
        
        std::cout << "Pipeline recompiled (generation: " 
                  << pipeline.GetGeneration() << ")\n";
    }
};
```

### Pattern 2: Async Loading with Progress Tracking

```cpp
// Texture loading result
struct TextureLoadResult : public Message {
    static constexpr MessageType TYPE = 201;
    
    std::string textureName;
    VkImage image;
    VkImageView view;
    bool success;
    std::string error;
    
    TextureLoadResult() : Message(0, TYPE) {}
};

// TextureManager with async loading
class TextureManager {
    std::unordered_map<std::string, RM<VkImage>> textures;
    WorkerThreadBridge<TextureLoadResult> loader;
    
    TextureManager(MessageBus* bus) : loader(bus) {
        bus->Subscribe(TextureLoadResult::TYPE, [this](const Message& msg) {
            OnTextureLoaded(static_cast<const TextureLoadResult&>(msg));
            return true;
        });
    }
    
    void LoadTextureAsync(const std::string& name, const std::string& path) {
        // Create placeholder
        auto& texture = textures[name];
        texture.AddState(ResourceState::Pending);
        texture.SetMetadata("file_path", path);
        
        // Submit async load
        loader.SubmitWork(0, [name, path]() {
            TextureLoadResult result;
            result.textureName = name;
            
            try {
                result.image = LoadImageFromDisk(path);
                result.view = CreateImageView(result.image);
                result.success = true;
            } catch (const std::exception& e) {
                result.success = false;
                result.error = e.what();
            }
            
            return result;
        });
    }
    
    void OnTextureLoaded(const TextureLoadResult& result) {
        auto& texture = textures[result.textureName];
        
        if (result.success) {
            texture.Set(result.image);
            texture.RemoveState(ResourceState::Pending);
            texture.MarkReady();
            
            std::cout << "Texture loaded: " << result.textureName << "\n";
        } else {
            texture.AddState(ResourceState::Failed);
            texture.RemoveState(ResourceState::Pending);
            
            std::cerr << "Texture load failed: " << result.error << "\n";
        }
    }
    
    RM<VkImage>* GetTexture(const std::string& name) {
        auto it = textures.find(name);
        if (it == textures.end()) return nullptr;
        
        auto& texture = it->second;
        
        // Check if ready
        if (texture.Ready()) {
            return &texture;
        }
        
        // Check if still loading
        if (texture.Has(ResourceState::Pending)) {
            std::cout << "Texture still loading...\n";
            return nullptr;
        }
        
        // Check if failed
        if (texture.Has(ResourceState::Failed)) {
            std::string path = texture.GetMetadata<std::string>("file_path");
            std::cerr << "Texture failed to load: " << path << "\n";
            return nullptr;
        }
        
        return nullptr;
    }
};
```

### Pattern 3: Main Loop Integration

```cpp
int main() {
    // Create message bus
    MessageBus bus;
    
    #ifdef _DEBUG
    bus.SetLoggingEnabled(true);
    #endif
    
    // Create systems
    ShaderSystem shaderSystem(&bus);
    PipelineSystem pipelineSystem(&bus);
    TextureManager textureManager(&bus);
    
    // Main loop
    while (!shouldQuit) {
        // 1. Process all queued messages (worker thread results, events)
        bus.ProcessMessages();
        
        // 2. Recompile outdated resources
        pipelineSystem.RecompileDirtyPipelines();
        
        // 3. Wait for GPU (unlock resources)
        WaitForFence(frameInFlightFence);
        pipelineSystem.UnlockResources();
        
        // 4. Render frame
        RenderFrame();
        
        // 5. Lock resources in-flight
        pipelineSystem.LockResources();
        
        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }
    
    return 0;
}
```

## Best Practices

### EventBus

1. **Define message types as enums** for readability:
```cpp
enum class AppMessageType : MessageType {
    ShaderCompiled = 100,
    TextureLoaded = 101,
    PipelineInvalidated = 102,
};
```

2. **Use ProcessMessages() at safe points** (between frames, not during rendering)

3. **Keep handlers lightweight** - queue work if heavy processing needed

4. **Unsubscribe in destructors** to prevent dangling callbacks

### ResourceManagement

1. **Always check Ready() before access**:
```cpp
if (pipeline) {
    Use(*pipeline);
}
```

2. **Use generation tracking for caches**:
```cpp
if (resource.GetGeneration() != cachedGeneration) {
    InvalidateCache();
}
```

3. **Lock resources in-flight**:
```cpp
// Before submit
resource.Lock();

// After fence wait
WaitForFence(fence);
resource.Unlock();
```

4. **Store file paths in metadata** for hot reload:
```cpp
texture.SetMetadata("file_path", path);
// Later...
Reload(texture.GetMetadata<std::string>("file_path"));
```

## Summary

**EventBus**: Generic `{sender, message}` system with worker thread bridge
- Type-safe inheritance-based messages
- Queue-based async processing
- Worker thread → main thread integration

**ResourceManagement**: `RM<T>` wrapper with state tracking
- Optional-like interface (`Ready()`, `Value()`)
- State management (`Outdated`, `Locked`, `Pending`)
- Generation tracking (cache invalidation)
- Metadata storage (arbitrary key-value pairs)

**Together**: Clean hot reload architecture with zero frame stutter!
