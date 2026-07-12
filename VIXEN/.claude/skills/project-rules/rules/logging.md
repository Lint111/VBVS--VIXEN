---
name: logging
description: Hierarchical logging system usage guide. Use when adding logging to nodes or ILoggable components. Explains why logs don't appear and how to enable terminal/file output. Prevents std::cout/cerr pollution.
allowed-tools: Read, Grep, Glob, Edit, Write
---

# Logging Skill

Guide for using VIXEN's hierarchical logging system correctly. Invoke this skill when:
- Adding logging to nodes or components
- Debugging why logs aren't appearing
- Considering using `std::cout` or `std::cerr` (DON'T - use the logger!)

## CRITICAL: Why Your Logs Don't Appear

**Loggers are DISABLED by default!** This is the #1 source of confusion.

```cpp
// In NodeInstance constructor:
nodeLogger = std::make_shared<Logger>(instanceName, false);  // <-- false = DISABLED!
```

To see your logs, you MUST enable them:

```cpp
// Enable logging (stores logs for later extraction)
nodeLogger->SetEnabled(true);

// Enable terminal output (prints to console in real-time)
nodeLogger->SetTerminalOutput(true);
```

## DO NOT USE std::cout/std::cerr

❌ **BAD** - Pollutes output, no hierarchy, no log extraction:
```cpp
std::cout << "Debug: processing node" << std::endl;
std::cerr << "Error: validation failed" << std::endl;
```

✅ **GOOD** - Uses hierarchical logger with proper levels:
```cpp
NODE_LOG_DEBUG("Processing node");
NODE_LOG_ERROR("Validation failed");
```

## Quick Reference: Node Logging

### 1. Using NODE_LOG_* Macros (Preferred for Nodes)

All nodes have `nodeLogger` member. Use the macros from `NodeLogging.h`:

```cpp
#include "Core/NodeLogging.h"

void MyNode::ExecuteImpl() {
    NODE_LOG_DEBUG("Starting execution");
    NODE_LOG_INFO("Processing " + std::to_string(count) + " items");
    NODE_LOG_WARNING("Buffer nearly full");
    NODE_LOG_ERROR("Failed to bind descriptor");
    NODE_LOG_CRITICAL("Device lost!");
}
```

### 2. Enabling Logging for a Node

Enable logging where you create/configure the node (typically VulkanGraphApplication or BenchmarkGraphFactory):

```cpp
// Get the node's logger and enable it
auto* myNode = graph->GetNode<MyNode>("myNodeInstance");
if (myNode && myNode->GetLogger()) {
    myNode->GetLogger()->SetEnabled(true);        // Store logs
    myNode->GetLogger()->SetTerminalOutput(true); // Print to console
}
```

### 3. Real Examples from Codebase

From `VulkanGraphApplication.cpp`:
```cpp
// Enable voxel node logging
auto voxelLogger = voxelGridNodePtr->GetLogger();
voxelLogger->SetEnabled(true);  // Enable to debug voxel rendering
voxelLogger->SetTerminalOutput(true);

// Enable dispatch node logging
auto dispatchLogger = dispatchNode->GetLogger();
dispatchLogger->SetEnabled(true);
dispatchLogger->SetTerminalOutput(true);
```

From `ComputeDispatchNode.cpp`:
```cpp
perfLogger_->SetEnabled(true);  // Enable manually when needed for debugging
perfLogger_->SetTerminalOutput(true);  // Print to terminal
```

## Quick Reference: ILoggable Components

For non-node components (GraphTopology, GraphLifecycleHooks, etc.), implement `ILoggable`:

### 1. Implementing ILoggable

```cpp
#include "ILoggable.h"

class MyComponent : public ILoggable {
public:
    MyComponent() {
        InitializeLogger("MyComponent", false);  // Name, enabled
    }
    
    void DoWork() {
        LOG_DEBUG("Starting work");
        LOG_INFO("Work complete");
    }
};
```

### 2. Enabling ILoggable Logging

```cpp
myComponent.SetLoggerEnabled(true);
myComponent.SetLoggerTerminalOutput(true);
```

### 3. Available Macros (from ILoggable.h)

```cpp
LOG_TRACE(msg)    // Trace-level (most verbose)
LOG_DEBUG(msg)    // Debug-level
LOG_INFO(msg)     // Info-level
LOG_WARNING(msg)  // Warning-level
LOG_ERROR(msg)    // Error-level
```

## Hierarchical Log Extraction

Logs are organized in parent-child hierarchy for structured output:

```cpp
// Add child logger to parent
parentLogger->AddChild(childLogger);

// Extract all logs (includes children, indented)
std::string allLogs = parentLogger->ExtractLogs();
```

Output format:
```
[2025-12-07 10:30:00.123] [RenderGraph] [INFO] Compilation started
    [2025-12-07 10:30:00.125] [SwapChainNode] [DEBUG] Creating swapchain
    [2025-12-07 10:30:00.130] [SwapChainNode] [INFO] Swapchain created
[2025-12-07 10:30:00.135] [RenderGraph] [INFO] Compilation complete
```

## Log Levels

| Level | Macro | When to Use |
|-------|-------|-------------|
| DEBUG | `NODE_LOG_DEBUG` | Detailed debugging info, verbose |
| INFO | `NODE_LOG_INFO` | Important operational events |
| WARNING | `NODE_LOG_WARNING` | Potential issues, recoverable |
| ERROR | `NODE_LOG_ERROR` | Errors that don't crash |
| CRITICAL | `NODE_LOG_CRITICAL` | Fatal errors |

## Creating Additional Loggers in Nodes

Some nodes create specialized loggers for subsystems:

```cpp
class ComputeDispatchNode : public TypedNode<...> {
private:
    std::shared_ptr<Logger> perfLogger_;  // Performance logging
    std::shared_ptr<Logger> gpuPerfLogger_;  // GPU timing
    
public:
    void SetupImpl() {
        // Create specialized logger
        perfLogger_ = std::make_shared<Logger>("ComputePerf", false);
        
        // Add as child of node's main logger for hierarchy
        if (nodeLogger) {
            nodeLogger->AddChild(perfLogger_);
        }
        
        // Enable if debugging
        perfLogger_->SetEnabled(true);
        perfLogger_->SetTerminalOutput(true);
    }
};
```

## Common Patterns

### Pattern 1: Conditional Debug Logging

```cpp
// Only log when explicitly enabled
if (nodeLogger && nodeLogger->IsEnabled()) {
    NODE_LOG_DEBUG("Expensive debug info: " + computeExpensiveString());
}
```

### Pattern 2: Logging with Formatting

```cpp
// Use ostringstream for complex formatting
std::ostringstream oss;
oss << "Processing " << count << " items, size=" << size << " bytes";
NODE_LOG_INFO(oss.str());

// Or inline
NODE_LOG_INFO("GPU time: " + std::to_string(gpuTimeMs) + " ms");
```

### Pattern 3: Scoped Debug Enable

```cpp
// Temporarily enable for debugging a specific section
bool wasEnabled = nodeLogger->IsEnabled();
nodeLogger->SetEnabled(true);
nodeLogger->SetTerminalOutput(true);

// ... debugging code ...

nodeLogger->SetEnabled(wasEnabled);  // Restore
```

## File References

| File | Purpose |
|------|---------|
| `libraries/logger/Logger.h` | Logger class definition |
| `libraries/logger/ILoggable.h` | ILoggable interface + LOG_* macros |
| `libraries/RenderGraph/include/Core/NodeLogging.h` | NODE_LOG_* macros |
| `libraries/RenderGraph/include/Core/NodeInstance.h` | nodeLogger member |
| `Vixen-Docs/Libraries/Logger.md` | Full documentation |

## Checklist: Adding Logging to a Node

1. [ ] Include `"Core/NodeLogging.h"`
2. [ ] Use `NODE_LOG_*` macros (not std::cout!)
3. [ ] Enable logging where node is created:
   - `node->GetLogger()->SetEnabled(true)`
   - `node->GetLogger()->SetTerminalOutput(true)`
4. [ ] Add child loggers to parent for hierarchy (optional)
5. [ ] Disable verbose logging before committing (set enabled=false)

## Checklist: Debugging Missing Logs

1. [ ] Is the logger enabled? `SetEnabled(true)`
2. [ ] Is terminal output enabled? `SetTerminalOutput(true)`
3. [ ] Is the message reaching the logger? (not short-circuited by early return)
4. [ ] Is the log level appropriate? (DEBUG may be filtered)
5. [ ] Are you checking the right output? (terminal vs log file vs ExtractLogs())
