# System Patterns

## Architecture Overview

The project follows a **layered architecture** with clear separation of concerns. Each Vulkan subsystem is encapsulated in its own class, following object-oriented principles.

```
┌─────────────────────────────────────┐
│       VulkanApplication             │  ← Singleton orchestrator
│    (Application lifecycle mgmt)     │
└──────────────┬──────────────────────┘
               │
    ┌──────────┴──────────┬────────────────┬─────────────┐
    │                     │                │             │
    ▼                     ▼                ▼             ▼
┌──────────┐      ┌──────────────┐  ┌─────────────┐  ┌──────────────┐
│ Instance │      │    Device    │  │  Renderer   │  │ SwapChain    │
│  Object  │      │    Object    │  │   Object    │  │   Object     │
└──────────┘      └──────────────┘  └─────────────┘  └──────────────┘
     │                   │                │                 │
     ▼                   ▼                ▼                 ▼
┌──────────┐      ┌──────────────┐  ┌─────────────┐  ┌──────────────┐
│ Layers & │      │ Physical Dev │  │  Drawable   │  │   Shader     │
│Extensions│      │ Logical Dev  │  │   Objects   │  │   Objects    │
└──────────┘      └──────────────┘  └─────────────┘  └──────────────┘
```

## Core Design Patterns

### 1. Singleton Pattern
**Class**: `VulkanApplication`

**Implementation**:
```cpp
static std::unique_ptr<VulkanApplication> instance;
static std::once_flag onlyOnce;
static VulkanApplication* GetInstance();
```

**Purpose**: Ensures single application instance throughout lifetime

**Location**: `VulkanApplication.h:16-21`

### 2. Layered Initialization
The initialization follows a strict sequence:

```
main()
  → VulkanApplication::Initialize()
      → CreateVulkanInstance()
          → VulkanInstance.CreateInstance()
              → VulkanLayerAndExtension (layer/extension setup)
      → EnumeratePhysicalDevices()
      → HandShakeWithDevice()
          → VulkanDevice creation and logical device setup
  → VulkanApplication::Prepare()
      → Renderer preparation
      → SwapChain setup
      → Pipeline creation
  → VulkanApplication::render() (loop)
  → VulkanApplication::DeInitialize()
```

### 3. Resource Ownership
**Pattern**: Each manager class owns its Vulkan resources

- `VulkanInstance` owns `VkInstance`
- `VulkanDevice` owns physical/logical device handles
- `VulkanRenderer` owns rendering resources
- `VulkanSwapChain` owns swapchain resources

**Lifecycle**: RAII principles - construction acquires, destruction releases

### 4. Extension and Layer Management
**Class**: `VulkanLayerAndExtension`

**Responsibility**:
- Enumerate available layers/extensions
- Validate requested layers/extensions exist
- Provide layer/extension information

**Separation**:
- Instance extensions/layers (defined in main.cpp)
- Device extensions (defined in main.cpp)

## Key Component Responsibilities

### VulkanApplication (Orchestrator)
- **Single Responsibility**: Application lifecycle management
- **Key Methods**:
  - `Initialize()`: Sets up Vulkan instance and device
  - `Prepare()`: Prepares rendering resources
  - `Update()`: Updates scene/state
  - `render()`: Executes render loop
  - `DeInitialize()`: Cleans up resources

### VulkanInstance (Instance Manager)
- **Single Responsibility**: Vulkan instance lifecycle
- **Key Methods**:
  - `CreateInstance()`: Creates VkInstance with layers/extensions
  - Destruction handles VkInstance cleanup

### VulkanDevice (Device Manager)
- **Single Responsibility**: Physical and logical device management
- **Key Methods**:
  - Physical device selection
  - Logical device creation with queue families
  - Device properties and capabilities query

### VulkanRenderer (Rendering Manager)
- **Single Responsibility**: Manages rendering pipeline and execution
- **Coordinates**:
  - Command buffers
  - Synchronization primitives
  - Drawing operations

### VulkanSwapChain (Presentation Manager)
- **Single Responsibility**: Manages presentation swapchain
- **Handles**:
  - Swapchain creation and configuration
  - Image views
  - Surface capabilities

## Data Flow Patterns

### Initialization Flow
```
Extensions/Layers (main.cpp)
    ↓
VulkanApplication::CreateVulkanInstance()
    ↓
VulkanInstance::CreateInstance()
    ↓
VulkanApplication::EnumeratePhysicalDevices()
    ↓
VulkanApplication::HandShakeWithDevice()
    ↓
VulkanDevice creation
```

### Render Loop Flow
```
main() while loop
    ↓
VulkanApplication::render()
    ↓
VulkanRenderer::render()
    ↓
Command buffer execution
    ↓
Swapchain present
```

## Error Handling Strategy

### Pattern: VkResult Propagation
- Methods return `VkResult` for Vulkan operations
- Caller checks result and handles accordingly
- Exceptions used for catastrophic failures (in main.cpp)

### Validation Layers
- Enabled via `VK_LAYER_KHRONOS_validation`
- Debug callback for runtime validation
- Helps catch API misuse during development

## Memory Management Patterns

### Smart Pointers
- `std::unique_ptr` for singleton instance
- Raw pointers for Vulkan object handles (managed by owner classes)

### RAII Principle
- Constructors acquire resources
- Destructors release resources
- Prevents leaks through automatic cleanup

### Member Variables
- `gpuList` stored as member to prevent premature destruction
- Ownership clearly defined per class

## Configuration Pattern

### Compile-Time Configuration
- Platform defines: `VK_USE_PLATFORM_WIN32_KHR`
- Language standard: C++23

### Runtime Configuration
- Extensions and layers defined in `main.cpp`
- Passed to application during initialization
- Allows flexible configuration without recompilation

## Critical Implementation Paths

### Path 1: Instance Creation
`main.cpp` → `VulkanApplication::Initialize()` → `VulkanInstance::CreateInstance()`

### Path 2: Device Selection
`VulkanApplication::EnumeratePhysicalDevices()` → `VulkanApplication::HandShakeWithDevice()`

### Path 3: Rendering Setup
`VulkanApplication::Prepare()` → Renderer/SwapChain/Pipeline initialization

### Path 4: Frame Rendering
`main.cpp` loop → `VulkanApplication::render()` → `VulkanRenderer::render()`

### Path 5: Cleanup
`VulkanApplication::DeInitialize()` → Component destructors

## Future Extensibility

The architecture supports extension through:
1. Adding new component classes (e.g., `VulkanTexture`, `VulkanBuffer`)
2. Extending renderer capabilities
3. Adding new pipeline stages
4. Implementing additional rendering passes

Each addition follows the established pattern:
- Create dedicated class for subsystem
- Manage resource lifecycle via RAII
- Integrate with VulkanApplication orchestration
