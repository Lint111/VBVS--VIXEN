# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System

This is a CMake-based C++ Vulkan application project. The build system is configured for Windows with Visual Studio.

### Build Commands

```bash
# Generate build files (run from project root)
cmake -B build

# Build the project
cmake --build build --config Debug
# or
cmake --build build --config Release

# Alternative: Use Visual Studio solution
# Open build/3_0_DeviceHandshake.sln in Visual Studio and build from IDE
```

### Project Structure

- `source/` - C++ source files (.cpp)
- `include/` - Header files (.h)
- `build/` - CMake generated build files and Visual Studio project files
- `binaries/` - Output directory for compiled executables
- `.vscode/` - VSCode configuration for IntelliSense and file associations

## Architecture Overview

This is a Vulkan learning project implementing a basic device handshake. The architecture follows a layered approach:

### Core Components

1. **VulkanApplication** (`VulkanApplication.h/.cpp`)
   - Singleton pattern main application class
   - Orchestrates the Vulkan initialization process
   - Entry point: `VulkanApplication::GetInstance()->Initialize()`

2. **VulkanInstance** (`VulkanInstance.h/.cpp`)
   - Manages Vulkan instance creation and destruction
   - Handles layer and extension setup
   - Core method: `CreateInstance()` for Vulkan instance creation

3. **VulkanDevice** (`VulkanDevice.h/.cpp`)
   - Manages Vulkan logical device operations
   - Physical device selection and logical device creation

4. **VulkanLayerAndExtension** (`VulkanLayerAndExtension.h/.cpp`)
   - Handles Vulkan validation layers and extensions
   - Layer enumeration and validation

### Configuration

- **Language Standard**: C++23 and C23
- **Platform**: Windows (VK_USE_PLATFORM_WIN32_KHR)
- **Vulkan SDK**: Auto-detection via CMake or manual path specification
- **Default Extensions**: VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME
- **Default Layers**: VK_LAYER_LUNARG_api_dump

### Development Notes

- The project uses manual Vulkan SDK path fallback: `C:/VulkanSDK/1.4.321.1`
- VSCode is configured with Vulkan SDK include paths for IntelliSense
- Executable output goes to `binaries/` directory
- The main application creates extensions and layers in `main.cpp` and passes them to the application singleton

## File Organization

```
source/
├── main.cpp              # Entry point with extension/layer setup
├── VulkanApplication.cpp # Main application singleton
├── VulkanInstance.cpp    # Vulkan instance management
├── VulkanDevice.cpp      # Device selection and creation
└── VulkanLayerAndExtension.cpp # Layer/extension handling

include/
├── Headers.h             # Common includes (vulkan.h, vector, iostream)
├── VulkanApplication.h   # Main application interface
├── VulkanInstance.h      # Instance management interface
├── VulkanDevice.h        # Device management interface
└── VulkanLayerAndExtension.h # Layer/extension interface
```

This is Chapter 3 example code focusing on establishing the initial handshake between the application and Vulkan drivers/devices.