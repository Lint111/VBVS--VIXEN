# Libraries

VIXEN static libraries - reusable units of logic.

## Library Components

- **logger** - Logging infrastructure with ILoggable interface
- **EventBus** - Message passing and worker thread integration
- **ResourceManagement** - RM<T> wrapper with state tracking
- **ShaderManagement** - Device-agnostic shader compilation and hot reload
- **VulkanResources** - Vulkan resource management (device, swapchain, descriptors, pipelines, textures)
- **CashSystem** - Caching and resource management
- **RenderGraph** - Graph-based render system for managing Vulkan resources and execution

## Organization

Each library is a separate static library with modern CMake target-based dependencies. Libraries are built in dependency order.
