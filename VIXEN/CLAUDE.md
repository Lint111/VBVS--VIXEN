# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Communication Style - MANDATORY

**Read and follow `documentation/Communication Guidelines.md` for all communication.** Key principles:

### Radical Conciseness
- **Maximum signal, minimum noise** - every word must serve a purpose
- **No conversational filler** - eliminate phrases like "Certainly!", "I hope this helps!", "As you requested..."
- **Lead with conclusion** - state the most important information first
- **Use structured data** - lists, tables, code blocks over prose
- **Report facts, not process** - state the plan/action/result, not your thinking process
- **Be economical** - if a sentence can be shorter, make it shorter

### No Sycophantic Language
- **NEVER** use "You're absolutely right!", "Excellent point!", or similar flattery
- **Brief acknowledgments only**: "Got it.", "I understand.", "I see the issue."
- **Proceed directly** to execution when possible

### Examples
- ❌ "Certainly! I'll help you with that. Let me start by reading the file..."
- ✅ [Proceeds directly with Read tool]

- ❌ "You're absolutely right! That's a great decision."
- ✅ "Got it." [proceeds with action]

**See `documentation/Communication Guidelines.md` for complete directive and examples.**

## Memory Bank - CRITICAL

The Memory Bank in `memory-bank/` provides persistent project context across sessions. **Read the appropriate files based on session type.**

### Quick Start (Every Task/Prompt)
Read these "hot" files that change frequently:
1. `memory-bank/activeContext.md` - Current focus, recent changes, active decisions
2. `memory-bank/progress.md` - Implementation status, what's done, what's left

### Full Context (New Conversation / After Reset)
When starting a fresh conversation or after conversation compression, read ALL files IN ORDER:
1. `memory-bank/projectbrief.md` - Project goals, scope, and success criteria
2. `memory-bank/productContext.md` - Why the project exists and design philosophy
3. `memory-bank/systemPatterns.md` - Architecture patterns and component design
4. `memory-bank/techContext.md` - Technology stack and development setup
5. `memory-bank/activeContext.md` - Current focus and recent decisions
6. `memory-bank/progress.md` - Implementation status and what's left to build

### Keeping Memory Bank Updated
Update the Memory Bank when:
- Implementing significant features or architectural changes
- Discovering new patterns or making important decisions
- User explicitly requests **"update memory bank"** (must review ALL files)
- Project status changes meaningfully

When updating:
- **Always update**: `activeContext.md` (current focus) and `progress.md` (status)
- **Update if changed**: `systemPatterns.md` (new patterns), `techContext.md` (tech changes)
- **Rarely update**: `projectbrief.md` and `productContext.md` (stable foundations)
- Keep documentation concise but comprehensive

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

## Coding Standards

All C++ code in this project must follow the guidelines specified in `documentation/cpp-programming-guidelins.md`. Key standards include:

- **Nomenclature**: PascalCase for classes, camelCase for functions/variables, ALL_CAPS for constants
- **Functions**: Keep functions short (<20 instructions), single purpose, use early returns to avoid nesting
- **Data**: Use const-correctness, prefer immutability, use std::optional for nullable values
- **Classes**: Follow SOLID principles, prefer composition over inheritance, keep classes small (<200 instructions, <10 public methods)
- **Memory**: Use smart pointers (std::unique_ptr, std::shared_ptr) over raw pointers, follow RAII principles
- **Modern C++**: Leverage C++23 features and the standard library (std::string, std::vector, std::filesystem, std::chrono, etc.)

See the full guidelines in `documentation/cpp-programming-guidelins.md` for complete details.