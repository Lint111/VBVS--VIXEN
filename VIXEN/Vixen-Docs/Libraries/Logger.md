---
title: Logger Library
aliases: [Logger, Logging, ILoggable]
tags: [library, logger, debugging]
created: 2025-12-06
related:
  - "[[Overview]]"
  - "[[RenderGraph]]"
---

# Logger Library

Hierarchical logging infrastructure with terminal output, log extraction, and ILoggable interface for component integration.

---

## 1. Logger Class

### 1.1 Overview

```cpp
#include <Logger.h>
using namespace Vixen::Log;

Logger logger("MyComponent", true);  // name, enabled
logger.SetTerminalOutput(true);      // Print to console

logger.Info("Starting component");
logger.Debug("Debug details");
logger.Warning("Potential issue");
logger.Error("Error occurred");
logger.Critical("Fatal error");
```

### 1.2 Log Levels

| Level | Usage |
|-------|-------|
| `LOG_DEBUG` | Detailed debugging information |
| `LOG_INFO` | General operational messages |
| `LOG_WARNING` | Potential issues |
| `LOG_ERROR` | Recoverable errors |
| `LOG_CRITICAL` | Fatal errors |

---

## 2. Hierarchical Logging

Loggers support parent-child relationships for structured log extraction.

```cpp
auto parentLogger = std::make_shared<Logger>("RenderGraph", true);
auto childLogger = std::make_shared<Logger>("SwapChainNode", true);

parentLogger->AddChild(childLogger);

// Extract all logs recursively
std::string allLogs = parentLogger->ExtractLogs();
```

### 2.1 Output Format

```
[2025-12-06 10:30:00.123] [RenderGraph] [INFO] Graph compilation started
    [2025-12-06 10:30:00.125] [SwapChainNode] [DEBUG] Creating swapchain
    [2025-12-06 10:30:00.130] [SwapChainNode] [INFO] Swapchain created: 720x720
[2025-12-06 10:30:00.135] [RenderGraph] [INFO] Graph compilation complete
```

---

## 3. ILoggable Interface

Components implementing `ILoggable` can be integrated into the logging hierarchy.

```cpp
class MyNode : public ILoggable {
public:
    Logger& GetLogger() override { return logger_; }

private:
    Logger logger_{"MyNode", true};
};
```

---

## 4. FrameRateLogger

Specialized logger for frame timing statistics.

```cpp
FrameRateLogger fpsLogger;
fpsLogger.RecordFrame(deltaTime);
fpsLogger.MaybeLog(frameNumber);  // Logs every N frames
```

---

## 5. API Reference

### 5.1 Logger Methods

| Method | Description |
|--------|-------------|
| `SetEnabled(bool)` | Enable/disable logging |
| `SetTerminalOutput(bool)` | Enable console output |
| `AddChild(shared_ptr)` | Add child logger |
| `RemoveChild(Logger*)` | Remove child logger |
| `ExtractLogs(indent)` | Get all logs as string |
| `Clear()` | Clear this logger's entries |
| `ClearAll()` | Clear this and all children |

### 5.2 Logging Methods

| Method | Level |
|--------|-------|
| `Debug(msg)` | LOG_DEBUG |
| `Info(msg)` | LOG_INFO |
| `Warning(msg)` | LOG_WARNING |
| `Error(msg)` | LOG_ERROR |
| `Critical(msg)` | LOG_CRITICAL |
| `Log(level, msg)` | Custom level |

---

## 6. Code References

| File | Purpose |
|------|---------|
| `libraries/logger/Logger.h` | Logger class definition |
| `libraries/logger/Logger.cpp` | Logger implementation |
| `libraries/logger/ILoggable.h` | ILoggable interface |
| `libraries/logger/FrameRateLogger.h` | FPS logging |

---

## 7. Related Pages

- [[Overview]] - Library index
- [[RenderGraph]] - Uses logging for node lifecycle
# Related Pages

## 8. Logging Macros Reference

### 8.1 Node Classes (inherit from NodeInstance)

```cpp
#include "Core/NodeLogging.h"

NODE_LOG_DEBUG("message");
NODE_LOG_INFO("message");
NODE_LOG_WARNING("message");
NODE_LOG_ERROR("message");
NODE_LOG_CRITICAL("message");
```

### 8.2 ILoggable Classes (TypedCacher, DeviceRegistry, etc.)

```cpp
#include "ILoggable.h"

LOG_DEBUG("message");
LOG_INFO("message");
LOG_WARNING("message");
LOG_ERROR("message");
```

### 8.3 RenderGraph Class

```cpp
// Defined locally in RenderGraph.cpp
GRAPH_LOG_DEBUG("message");
GRAPH_LOG_INFO("message");
GRAPH_LOG_WARNING("message");
GRAPH_LOG_ERROR("message");
GRAPH_LOG_CRITICAL("message");  // Also outputs to std::cerr
```

---

## 9. Refactor Status (December 2025)

All major libraries now use proper logging instead of std::cout/cerr:

| Library | Status | Notes |
|---------|--------|-------|
| RenderGraph | ✅ Complete | NODE_LOG_*, GRAPH_LOG_* |
| CashSystem | ✅ Complete | LOG_* via TypedCacher |
| VulkanResources | ✅ Complete | ILoggable added to VulkanSwapChain, VulkanLayerAndExtension |
| Profiler | ✅ Complete | LOG_* with TODO for ILoggable |
| SVO | ✅ Complete | Utility classes use std::cerr |
| Application | ✅ Complete | Intentional user-facing output only |

**Remaining std::cerr usage** (by design):
- `SceneGenerator.cpp` - Utility class, no logger context
- `VoxelOctree.cpp` - Utility class, no logger context
- `CommandBufferUtility.cpp` - Standalone function
- `Logger.cpp` - Logger itself uses fprintf

---

