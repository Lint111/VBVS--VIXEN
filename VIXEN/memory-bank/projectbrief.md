# Project Brief

## Project Name
VIXEN - Vulkan Interactive eXample Engine

## Overview
This is a learning-focused Vulkan graphics programming project following a chapter-based progression. Currently on Chapter 3, implementing the fundamental device handshake between the application and Vulkan drivers/devices.

## Goals
1. **Learn Vulkan fundamentals** through hands-on implementation
2. **Master the Vulkan device handshake** - establishing communication between application and GPU
3. **Build a foundation** for future graphics programming projects
4. **Follow best practices** in modern C++ (C++23) and Vulkan API usage

## Scope
### Current Chapter (Chapter 3 - Device Handshake)
- Vulkan instance creation and management
- Physical device enumeration and selection
- Logical device creation
- Layer and extension management
- Basic error handling and validation

### Future Chapters
- Surface and swapchain creation
- Command buffers and queues
- Rendering pipeline setup
- Shader integration
- Advanced Vulkan features

## Target Platform
- **Primary**: Windows 10/11
- **Build System**: CMake
- **Compiler**: MSVC (Visual Studio)
- **Graphics API**: Vulkan 1.4.321.1

## Success Criteria
1. Successfully initialize Vulkan instance
2. Enumerate and select physical devices
3. Create logical device with appropriate queues
4. Properly manage validation layers and extensions
5. Clean resource management and proper destruction
6. Code follows C++ programming guidelines
7. Maintainable, well-documented codebase

## Non-Goals
- Production-ready graphics engine
- Cross-platform support (focused on Windows for learning)
- Performance optimization (learning correct usage first)
- Advanced rendering techniques (reserved for future chapters)

## Constraints
- Must use Vulkan SDK 1.4.321.1
- Must follow C++23 standard
- Must adhere to project coding guidelines (cpp-programming-guidelins.md)
- Windows-specific platform code acceptable (VK_USE_PLATFORM_WIN32_KHR)
