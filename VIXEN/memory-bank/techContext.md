# Technical Context

## Technology Stack

### Core Technologies
- **Graphics API**: Vulkan 1.4.321.1
- **Programming Language**: C++23 (primary), C23 (supporting)
- **Build System**: CMake 4.0.3+
- **Compiler**: MSVC (Visual Studio)
- **Platform**: Windows (Win32)

### Vulkan Components
- **Vulkan SDK**: Version 1.4.321.1
- **Platform Extension**: `VK_USE_PLATFORM_WIN32_KHR`
- **Core Extensions**:
  - `VK_KHR_SURFACE_EXTENSION_NAME`
  - `VK_KHR_WIN32_SURFACE_EXTENSION_NAME`
  - `VK_EXT_DEBUG_REPORT_EXTENSION_NAME`
  - `VK_KHR_SWAPCHAIN_EXTENSION_NAME`
- **Validation Layers**:
  - `VK_LAYER_KHRONOS_validation`

### Shader Compilation
- **Runtime Compilation**: Enabled via `BUILD_SPV_ON_COMPILE_TIME=ON`
- **GLSL to SPIR-V**: Uses glslang library from Vulkan SDK
- **Required Libraries**: SPIRV, glslang, OSDependent, glslang-default-resource-limits, SPIRV-Tools, SPIRV-Tools-opt
- **Offline Alternative**: glslangValidator.exe for pre-compilation

## Development Environment

### Directory Structure
```
VIXEN/
├── source/           # C++ implementation files (.cpp)
├── include/          # Header files (.h)
├── build/            # CMake generated files and VS solution
├── binaries/         # Compiled executables output
├── memory-bank/      # Project documentation and context
├── .vscode/          # VSCode configuration
├── CMakeLists.txt    # Build configuration
└── CLAUDE.md         # AI assistant project instructions
```

### Build Configuration

#### CMake Options
- `AUTO_LOCATE_VULKAN`: ON (auto-detect Vulkan SDK) or OFF (manual path)
- `BUILD_SPV_ON_COMPILE_TIME`: ON (runtime GLSL→SPIR-V) or OFF (use .spv files)

#### Manual Vulkan Path (fallback)
```cmake
VULKAN_SDK = "C:/VulkanSDK"
VULKAN_VERSION = "1.4.321.1"
VULKAN_PATH = "${VULKAN_SDK}/${VULKAN_VERSION}"
```

#### Build Commands
```bash
# Generate build files
cmake -B build

# Build Debug
cmake --build build --config Debug

# Build Release
cmake --build build --config Release

# Alternative: Open Visual Studio solution
# build/7e_ShadersWithSPIRV.sln
```

### Compiler Settings
- **C++ Standard**: C++23 (required)
- **C Standard**: C23 (required)
- **Output Directory**: `binaries/`
- **Configurations**: Debug, Release, MinSizeRel, RelWithDebInfo
- **Precompiled Headers**: RenderGraph (15 headers), ShaderManagement (9 headers)
- **Build Optimizations**: Ccache/sccache support, Ninja generator, Unity builds

### IDE Configuration

#### VSCode Setup
- **IntelliSense**: Configured with Vulkan SDK include paths
- **File Associations**: C++ headers and sources
- **Build Tasks**: CMake integration
- **Testing**: Test Explorer integration with GoogleTest
- **Coverage**: LCOV visualization with Coverage Gutters extension
- **Test Discovery**: `testMate.cpp.test.executables` pattern matching

## Dependencies

### Primary Dependencies
1. **Vulkan SDK 1.4.321.1**
   - Location: `C:/VulkanSDK/1.4.321.1`
   - Components: vulkan-1 library, headers, validation layers

2. **GLSL Shader Compiler (glslang)**
   - Path: `${VULKAN_PATH}/Include/glslang`
   - Libraries: SPIRV, glslang, OSDependent, etc.
   - Purpose: Runtime GLSL to SPIR-V compilation

### Library Linking
```cmake
# Debug builds: link *d.lib versions
# Release builds: link *.lib versions
VULKAN_LIB_LIST:
  - vulkan-1
  - SPIRV (debug: SPIRVd)
  - glslang (debug: glslangd)
  - OSDependent (debug: OSDependentd)
  - glslang-default-resource-limits (debug: glslang-default-resource-limitsd)
  - SPIRV-Tools (debug: SPIRV-Toolsd)
  - SPIRV-Tools-opt (debug: SPIRV-Tools-optd)
```

## Technical Constraints

### Platform Constraints
- **Windows Only**: Uses Win32-specific Vulkan extensions
- **MSVC Required**: Build system configured for Visual Studio
- **x64 Architecture**: Standard Windows 64-bit

### Vulkan Constraints
- **SDK Version**: Fixed at 1.4.321.1
- **Validation Layers**: Must be present for debug builds
- **Extension Requirements**: Surface and swapchain extensions mandatory

### Build Constraints
- **CMake Version**: Minimum 4.0.3
- **C++23 Features**: Code uses modern C++23 features
- **glslang Requirement**: Must be present if `BUILD_SPV_ON_COMPILE_TIME=ON`

## Development Workflow

### Setup Process
1. Install Vulkan SDK 1.4.321.1 to `C:/VulkanSDK/1.4.321.1`
2. Install Visual Studio with C++ support
3. Clone repository
4. Run `cmake -B build` from project root
5. Open `build/7e_ShadersWithSPIRV.sln` or use command line build

### Build Process
```bash
# Full rebuild
cmake -B build
cmake --build build --config Debug

# Incremental build
cmake --build build --config Debug
```

### Debug Process
- **Validation Layers**: Enable `VK_LAYER_KHRONOS_validation`
- **Debug Output**: Console output with validation messages
- **Visual Studio Debugger**: Attach to executable in `binaries/`

### Testing Approach
- **Automated Testing**: GoogleTest framework with 10 test suites
- **Coverage Analysis**: 40% coverage target, LCOV visualization
- **VS Code Integration**: Test Explorer, one-click debugging
- **Validation Layers**: Comprehensive Vulkan API error checking
- **Visual Confirmation**: Rendering output verification

## Tool Usage Patterns

### CMake
- Auto-detects Vulkan SDK when possible
- Generates Visual Studio solution files
- Configures include/link directories automatically
- Handles debug/release configurations
- **Modular Library Structure**: RenderGraph, EventBus, ShaderManagement, CashSystem, ResourceManagement, Logger, VulkanResources
- **Testing Support**: GoogleTest framework, CTest integration, ENABLE_COVERAGE option for LCOV

### Testing Framework (November 2025)
- **GoogleTest**: Unit testing framework (10 test suites)
- **Coverage**: 40% achieved (ResourceBudgetManager 90%, DeferredDestruction 95%, StatefulContainer 85%, SlotTask 90%, GraphTopology 90%)
- **VS Code Integration**: Test Explorer hierarchical view, coverage gutters (green/orange/red)
- **Test Suites**: ResourceManagement, GraphTopology, ShaderManagement, CashSystem, EventBus, NodeInstance, TypedConnection, SlotTask, StatefulContainer, DeferredDestruction
- **Documentation**: `docs/TEST_COVERAGE.md` (~400 pages), `docs/VS_CODE_TESTING_SETUP.md` (~800 pages)

### GLSL Compiler
- **Runtime**: Uses glslang library for on-the-fly compilation
- **Offline**: Can use glslangValidator.exe separately
- **Output**: SPIR-V bytecode (.spv format)

### Visual Studio
- Full IDE support for editing and debugging
- Solution file: `build/7e_ShadersWithSPIRV.sln`
- Project organization: source/ and include/ groups

## Common Commands

### CMake
```bash
# Configure with auto-detect
cmake -B build

# Configure with manual Vulkan path
cmake -B build -DAUTO_LOCATE_VULKAN=OFF

# Build specific configuration
cmake --build build --config Debug
cmake --build build --config Release

# Clean build
rm -rf build && cmake -B build
```

### GLSL Compilation (offline)
```bash
# Compile GLSL shader to SPIR-V
glslangValidator.exe shader.vert -V -o shader.vert.spv
glslangValidator.exe shader.frag -V -o shader.frag.spv
```

### Git
```bash
# Check status
git status

# Commit changes
git add .
git commit -m "Description"
```

## Environment Variables

### Expected Variables (Optional)
- `VULKAN_SDK`: Path to Vulkan SDK root (if not using auto-detection)

### Generated Variables (CMake)
- `VULKAN_PATH`: Full path to Vulkan SDK
- `Vulkan_INCLUDE_DIRS`: Include directory path
- `Vulkan_LIBRARY`: Vulkan library path

## Current Configuration Status

### Active Settings
- `AUTO_LOCATE_VULKAN`: ON
- `BUILD_SPV_ON_COMPILE_TIME`: ON
- Executable Name: `7e_ShadersWithSPIRV.exe`
- Output Directory: `binaries/`
- Validation Layers: Enabled

### Known Limitations
- Windows-only (no Linux/macOS support)
- Requires specific Vulkan SDK version
- MSVC compiler required (no GCC/Clang configuration)
